#ifdef TOOLBOX_SONY_PROBE
#include "probe.h"
#include "tunnel.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <driver/usb_serial_jtag.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/platform_util.h>
#include <nvs_flash.h>
#include <wifi_manager.h>

namespace sony {
namespace {
void print(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    usb_serial_jtag_write_bytes(buffer, strlen(buffer), pdMS_TO_TICKS(250));
}
void memory(const char* stage) {
    const unsigned ram = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, psram = MALLOC_CAP_SPIRAM;
    print("%s internal free/min/largest=%u/%u/%u PSRAM free/min=%u/%u DMA largest=%u stack margin=%u B\r\n",
          stage, unsigned(heap_caps_get_free_size(ram)), unsigned(heap_caps_get_minimum_free_size(ram)),
          unsigned(heap_caps_get_largest_free_block(ram)), unsigned(heap_caps_get_free_size(psram)),
          unsigned(heap_caps_get_minimum_free_size(psram)), unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)),
          unsigned(uxTaskGetStackHighWaterMark(nullptr)));
}
struct Secret {
    std::string text;
    ~Secret() { clear(); }
    void clear() { if (!text.empty()) mbedtls_platform_zeroize(text.data(), text.size()); text.clear(); }
};
bool line(const char* prompt, std::string& out, size_t limit, Session* session = nullptr) {
    print("%s\r\n", prompt);
    out.clear();
    out.reserve(limit);
    int64_t until = esp_timer_get_time() + 120000000;
    bool overflow = false;
    // Windows can temporarily suspend USB SOF traffic while the serial port
    // is open. Bound idle time here and let Ctrl+C or quit end the session.
    while (esp_timer_get_time() < until) {
        if (session) {
            Event event{};
            int rc = session->event(event);
            if (rc < 0) { print("Event stream failed (%d)\r\n", rc); return false; }
            if (rc == 1) print("event=%04x tid=%lu params=%u first=%08lx\r\n", event.code,
                               (unsigned long)event.transaction, unsigned(event.count), (unsigned long)event.params[0]);
        }
        char ch;
        if (usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(20)) != 1) continue;
        if (ch == 3) return false;
        if (ch == '\r' || ch == '\n') {
            if (overflow) return false;
            if (!out.empty()) return true;
        } else if (ch == '\b' || ch == 127) {
            if (!out.empty()) out.pop_back();
        } else if (ch >= 32 && ch < 127) {
            if (out.size() < limit) out.push_back(ch);
            else overflow = true;
        }
    }
    return false;
}
void showProp(const Property& p, void*) {
    // The most common diagnostics fit one line. The service retains typed wire
    // values for variable-size properties and both lists for enum constraints.
    if (p.type < 1 || p.type > 8) return;
    uint64_t raw = 0;
    for (size_t i = 0; i < p.current.size; ++i) raw |= uint64_t(p.current.data[i]) << (8 * i);
    print("prop=%04x type=%04x write=%u enable=%u current=0x%llx form=%u constraintBytes=%u\r\n",
          p.code, p.type, p.writable, p.enabled, (unsigned long long)raw, p.form, unsigned(p.constraints.size));
}
void controls(Session& session) {
    int idle = -1, rec = -1;
    print("model=%s SDIO=%u D21D=%d\r\n", session.model(), session.version(), session.recordingRaw());
    print("advertised properties=%u controls=%u shutter=%u MOVIE=%u\r\n", unsigned(session.propertyCount()),
          unsigned(session.controlCount()), session.supportsCapture(), session.supportsMovie());
    std::string cmd;
    while (session.connected() && line("props | state | mark-idle | mark-rec | photo | start | stop | quit", cmd, 16, &session)) {
        if (cmd == "quit") break;
        int rc = 0;
        if (cmd == "props") rc = session.refresh(showProp);
        else if (cmd == "state" || cmd == "mark-idle" || cmd == "mark-rec") {
            rc = session.refresh();
            int raw = session.recordingRaw();
            if (!rc && cmd == "mark-idle" && raw >= 0 && raw != rec) idle = raw;
            if (!rc && cmd == "mark-rec" && raw >= 0 && raw != idle) rec = raw;
            print("D21D=%d observed idle=%d rec=%d (-1 unknown)\r\n", raw, idle, rec);
        } else if (cmd == "photo" || cmd == "start" || cmd == "stop") {
            int expected = cmd == "stop" ? rec : idle;
            if (expected < 0) { print("Observe body state and mark it first.\r\n"); continue; }
            rc = cmd == "photo" ? session.capture(expected) : session.movie(expected);
            if (!rc) {
                print("Button sequence acknowledged. Verify body / test card.\r\n");
                vTaskDelay(pdMS_TO_TICKS(500));
                rc = session.refresh();
                print("Readback D21D=%d\r\n", session.recordingRaw());
            }
        } else continue;
        if (rc) print("operation=%04x result=%d\r\n", session.lastOp(), rc);
    }
}
void probe() {
    memory("entry");
    std::string ssid, user, confirm;
    Secret wifiPassword, sshPassword;
    if (!line("Camera direct SSID", ssid, 32) || !line("Wi-Fi password (input not echoed)", wifiPassword.text, 64)) return;
    auto& wifi = WifiManager::GetInstance();
    if (nvs_flash_init() != ESP_OK || !wifi.Initialize()) { print("Wi-Fi initialization failed\r\n"); return; }
    if (!wifi.StartDirectStation(ssid, wifiPassword.text)) { print("STA start failed or radio busy\r\n"); return; }
    struct StationLease { WifiManager& wifi; ~StationLease() { wifi.StopStation(); } } lease{wifi};
    wifiPassword.clear();
    if (!wifi.WaitForStation(15000)) { print("STA association or DHCP failed\r\n"); return; }
    auto* iface = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip{};
    if (!iface || esp_netif_get_ip_info(iface, &ip) != ESP_OK || !ip.gw.addr) { print("Missing DHCP gateway\r\n"); return; }
    memory("STA");
    Tunnel tunnel;
    if (!tunnel.handshake(ip.gw.addr)) { print("SSH connect / key exchange failed (%d)\r\n", tunnel.lastError()); return; }
    memory("SSH KEX");
    print("KEX=%s host=%s cipher=%s MAC=%s\r\n", tunnel.algorithm(LIBSSH2_METHOD_KEX),
          tunnel.algorithm(LIBSSH2_METHOD_HOSTKEY), tunnel.algorithm(LIBSSH2_METHOD_CRYPT_CS),
          tunnel.algorithm(LIBSSH2_METHOD_MAC_CS));
    print("Compare with Access Authentication Info on camera: %s\r\n", tunnel.fingerprint());
    if (!line("Type MATCH only if the complete fingerprint matches, or quit", confirm, 5) || confirm != "MATCH") return;
    std::string pin = tunnel.fingerprint();
    if (!line("SSH user", user, 128) || !line("SSH password (input not echoed)", sshPassword.text, 256)) return;
    if (!tunnel.authenticate(pin.c_str(), user.c_str(), sshPassword.text.c_str())) { print("SSH authentication failed (%d)\r\n", tunnel.lastError()); return; }
    sshPassword.clear();
    constexpr size_t capacity = 64 * 1024;
    std::unique_ptr<uint8_t, decltype(&heap_caps_free)> scratch(
        static_cast<uint8_t*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), heap_caps_free);
    if (!scratch) { print("No PSRAM for PTP data\r\n"); return; }
    uint8_t guid[16] = {'T','o','o','l','B','o','x','O','S',0};
    if (esp_read_mac(guid + 10, ESP_MAC_WIFI_STA) != ESP_OK) return;
    Session session(tunnel, scratch.get(), capacity);
    int rc = session.start(guid);
    if (!rc) { memory("SDIO ready"); controls(session); }
    else print("PTP init operation=%04x result=%d InitFailReason=%lu\r\n", session.lastOp(), rc,
               (unsigned long)session.refusalReason());
    session.close();
}
}
void enterProbeOnRequest() {
    usb_serial_jtag_driver_config_t config{1024, 512};
    if (usb_serial_jtag_driver_install(&config) != ESP_OK) return;
    print("Sony diagnostic: send uppercase P within 4 seconds to enter\r\n");
    char start = 0;
    if (usb_serial_jtag_read_bytes(&start, 1, pdMS_TO_TICKS(4000)) == 1 && start == 'P') {
        TaskHandle_t parent = xTaskGetCurrentTaskHandle();
        if (xTaskCreate([](void* arg) {
            probe();
            memory("closed");
            print("Diagnostic ended. Rebooting to normal startup.\r\n");
            usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(300));
            xTaskNotifyGive(static_cast<TaskHandle_t>(arg));
            vTaskDelete(nullptr);
        }, "sony-probe", 16384, parent, 4, nullptr) == pdPASS) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            esp_restart();
        }
    }
    usb_serial_jtag_driver_uninstall();
}
} // namespace sony
#endif

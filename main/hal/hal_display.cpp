/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "display_frame.h"
#include <system/shell.h>
#include <system/touch_input.h>
#include "utils/settings/settings.h"
#include <mooncake_log.h>
#include <M5GFX.h>
#include <lgfx/v1/panel/Panel_AMOLED.hpp>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <memory>
#include <atomic>
#include <algorithm>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>

static const std::string_view _tag = "HAL-Display";

/* -------------------------------------------------------------------------- */
/*                               Amoled display                               */
/* -------------------------------------------------------------------------- */
static constexpr gpio_num_t cfg_pin_sclk = GPIO_NUM_40;
static constexpr gpio_num_t cfg_pin_io0  = GPIO_NUM_41;
static constexpr gpio_num_t cfg_pin_io1  = GPIO_NUM_42;
static constexpr gpio_num_t cfg_pin_io2  = GPIO_NUM_46;
static constexpr gpio_num_t cfg_pin_io3  = GPIO_NUM_45;
static constexpr gpio_num_t cfg_pin_cs   = GPIO_NUM_39;
static constexpr gpio_num_t cfg_pin_te   = GPIO_NUM_38;
static constexpr gpio_num_t cfg_pin_rst  = GPIO_NUM_NC;
static SemaphoreHandle_t panel_mutex;
// Keep the internal staging buffers small enough for Wi-Fi and BLE coexistence.
// Full frames live in PSRAM; only these alternating strips require DMA RAM.
static constexpr int dma_rows = 8;
static uint8_t* dma_buffers[2];

class Panel_CO5300 : public lgfx::Panel_AMOLED {
public:
    Panel_CO5300(void)
    {
        _cfg.memory_width = _cfg.panel_width = 480;
        _cfg.memory_height = _cfg.panel_height = 480;
        _write_depth                           = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth                            = lgfx::color_depth_t::rgb565_2Byte;
    }

    const uint8_t *getInitCommands(uint8_t listno) const override
    {
        static constexpr uint8_t list0[] = {
            0x11, 0 + CMD_INIT_DELAY,
            150,  // Sleep out
            0xC4, 1,
            0x80, 0x35,
            1,    0x00,  // TE high during vertical blanking (CO5300 TEON)
            0x44, 2,
            0x00, 0x00,  // TE at vertical blanking, rising edge starts a frame
            0x53, 1,
            0x20, 0x20,
            0,    0x36,
            1,    0,
            0x51, 1,
            0xA0, 0x29,
            0,    0xff,
            0xff  // end
        };
        switch (listno) {
            case 0:
                return list0;
            default:
                return nullptr;
        }
    }
};

class M5StopWatch : public M5GFX {
    lgfx::Bus_SPI _bus_instance;
    Panel_CO5300 _panel_instance;

public:
    M5StopWatch(void)
    {
    }

    // static constexpr int in_i2c_port                   = 0;  // I2C_NUM_0

    bool init_impl(bool use_reset, bool use_clear) override
    {
        {
            auto cfg = _bus_instance.config();

            cfg.freq_write = 80000000;
            cfg.freq_read  = 10000000;  // irrelevant

            cfg.pin_sclk = cfg_pin_sclk;
            cfg.pin_io0  = cfg_pin_io0;
            cfg.pin_io1  = cfg_pin_io1;
            cfg.pin_io2  = cfg_pin_io2;
            cfg.pin_io3  = cfg_pin_io3;

            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;  // SPI_MODE0;
            cfg.spi_3wire   = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        {
            auto cfg         = _panel_instance.config();
            cfg.pin_rst      = cfg_pin_rst;
            cfg.pin_cs       = cfg_pin_cs;
            cfg.panel_width  = 468;
            cfg.panel_height = 466;
            cfg.offset_x     = 6;
            cfg.offset_y     = 0;

            cfg.readable = false;

            _panel_instance.config(cfg);
        }

        setPanel(&_panel_instance);

        lgfx::pinMode(cfg_pin_te, lgfx::pin_mode_t::input_pullup);
        // lgfx::i2c::init(in_i2c_port);

        // io_expander.digitalWrite(PY32_L3B_EN_PIN, 1);
        // io_expander.digitalWrite(PY32_OLED_RST_PIN, 1);

        if (!LGFX_Device::init_impl(use_reset, use_clear)) return false;

        enableFrameBuffer(true);

        _panel_instance.setBrightness(128);

        return true;
    }

    bool enableFrameBuffer(bool auto_display = false)
    {
        if (_panel_instance.initPanelFb()) {
            auto fbPanel = _panel_instance.getPanelFb();
            if (fbPanel) {
                fbPanel->setBus(&_bus_instance);
                fbPanel->setAutoDisplay(auto_display);
                setPanel(fbPanel);
                return true;
            }
        }
        return false;
    }

    void disableFrameBuffer()
    {
        auto fbPanel = _panel_instance.getPanelFb();
        if (fbPanel) {
            _panel_instance.deinitPanelFb();
            setPanel(&_panel_instance);
        }
    }

    void setBrightness(uint8_t brightness)
    {
        xSemaphoreTake(panel_mutex, portMAX_DELAY);
        _panel_instance.setBrightness(brightness);
        xSemaphoreGive(panel_mutex);
    }

    uint32_t present(const uint8_t* pixels, uint32_t stride, const lv_area_t& area)
    {
        // The LVGL worker owns its PSRAM frame until this entire transfer ends.
        // Alternate internal DMA buffers so packing overlaps the previous burst.
        _panel_instance.startWrite();
        _panel_instance.setWindow(area.x1, area.y1, area.x2, area.y2);
        _panel_instance.start_qspi();
        unsigned index = 0;
        uint32_t packing_us = 0;
        for (int y = area.y1; y <= area.y2; y += dma_rows) {
            const int rows = std::min<int>(dma_rows, area.y2 - y + 1);
            auto* buffer = dma_buffers[index++ & 1];
            if (y != area.y1) {
                const int64_t pack_start = esp_timer_get_time();
                toolbox::display::pack_rows(buffer, pixels, stride, area, y, rows, false);
                packing_us += static_cast<uint32_t>(esp_timer_get_time() - pack_start);
            }
            // Packing overlaps DMA. Wait before touching SPI registers, including
            // the <=64-byte tail path which uses the SPI FIFO instead of DMA.
            _bus_instance.wait();
            _bus_instance.writeBytes(buffer, (area.x2 - area.x1 + 1) * rows * 2, true, true);
        }
        _panel_instance.endWrite(); // Wait for the final DMA burst and release CS.
        return packing_us;
    }
};

static std::unique_ptr<M5StopWatch> _display;
static std::unique_ptr<LGFX_Sprite> _canvas;

void Hal::display_init()
{
    mclog::tagInfo(_tag, "display init");

    // The AMOLED itself is the primary visible power indication. Keep the
    // separate PMIC power LED dark whenever the display is about to be used.
    setPowerLed(false);

    panel_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(panel_mutex ? ESP_OK : ESP_ERR_NO_MEM);

    _display = std::make_unique<M5StopWatch>();
    if (!_display->init()) {
        mclog::tagError(_tag, "display init failed");
        _display.reset();
    }

    // mclog::tagInfo(_tag, "create full screen canvas");
    // _canvas = std::make_unique<LGFX_Sprite>(_display.get());
    // _canvas->setPsram(true);
    // if (!_canvas->createSprite(_display->width(), _display->height())) {
    //     mclog::tagError(_tag, "canvas init failed");
    //     _canvas.reset();
    // }

    // Load brightness from settings
    auto brightness = getBackLightBrightness(true);
    setBackLightBrightness(brightness, false);
}

LGFX_Device &Hal::getDisplay()
{
    return *_display;
}

LGFX_Sprite &Hal::getCanvas()
{
    return *_canvas;
}

void Hal::updateCanvas()
{
    _canvas->pushSprite(0, 0);
}

void Hal::setBackLightBrightness(int brightness, bool saveToSettings)
{
    _bl_brightness = uitk::clamp(brightness, 0, 100);

    int set_target = uitk::map_range(_bl_brightness, 0, 100, 0, 255);

    // Never illuminate the redundant power LED while the AMOLED is on. Turn
    // it back on only when brightness is explicitly set to zero.
    if (_bl_brightness > 0) {
        setPowerLed(false);
    }
    _display->setBrightness(set_target);
    if (_bl_brightness == 0) {
        setPowerLed(true);
    }

    if (saveToSettings) {
        Settings settings(std::string(Hal::SettingsNs), true);
        settings.SetInt("bl_lev", _bl_brightness);
        mclog::tagInfo(_tag, "brightness saved to settings: {}", _bl_brightness);
    }
}

int Hal::getBackLightBrightness(bool loadFromSettings)
{
    if (loadFromSettings) {
        Settings settings(std::string(Hal::SettingsNs), false);
        _bl_brightness = settings.GetInt("bl_lev", 80);
        _bl_brightness = uitk::clamp(_bl_brightness, 10, 100);
        mclog::tagInfo(_tag, "brightness loaded from settings: {}", _bl_brightness);
    }
    return _bl_brightness;
}

/* -------------------------------------------------------------------------- */
/*                                  Touchpad                                  */
/* -------------------------------------------------------------------------- */
#include "drivers/cst820/cst820.h"

static std::unique_ptr<Cst820> _cst820;
static toolbox::TouchHistory touch_history;
static portMUX_TYPE touch_mux = portMUX_INITIALIZER_UNLOCKED;

static void touch_rtos_task(void*)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t last_good = GetHAL().millis();
    bool failed_gesture = false;
    while (true) {
        const bool valid = _cst820->read();
        const uint32_t now = GetHAL().millis();
        if (valid) {
            last_good = now;
            failed_gesture = false;
            const bool pressed = _cst820->isPressed();
            const int16_t x = _cst820->getX(), y = _cst820->getY();
            portENTER_CRITICAL(&touch_mux);
            // A release packet can carry the final movement. Feed that movement
            // before pen-up so LVGL can cancel a click and start scrolling.
            if (!pressed && _cst820->isReleaseEvent() && touch_history.latest.pressed &&
                x >= 0 && x < 468 && y >= 0 && y < 466) {
                touch_history.push({true, x, y, now});
            }
            touch_history.push({pressed, x, y, now});
            portEXIT_CRITICAL(&touch_mux);
        } else if (!failed_gesture && now - last_good >= 80) {
            portENTER_CRITICAL(&touch_mux);
            touch_history.cancel(now);
            portEXIT_CRITICAL(&touch_mux);
            failed_gesture = true;
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(4));
    }
}


void Hal::touchpad_init()
{
    mclog::tagInfo(_tag, "touchpad init");

    ioe_tp_reset();

    _cst820 = std::make_unique<Cst820>();
    if (!_cst820->begin(i2c_bus_get_internal_bus_handle(_i2c_bus))) {
        mclog::tagError(_tag, "touchpad init failed");
        _cst820.reset();
        return;
    }
    const BaseType_t created = xTaskCreatePinnedToCore(touch_rtos_task, "touch_sample", 4096,
                                                      nullptr, 7, nullptr, 1);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("Touch", "independent 4ms sampling, timestamped 64-event history");
}

Hal::TouchPoint Hal::getTouchPoint()
{
    portENTER_CRITICAL(&touch_mux);
    const auto sample = touch_history.latest;
    portEXIT_CRITICAL(&touch_mux);
    return {sample.pressed ? 1 : 0, sample.x, sample.y};
}

/* -------------------------------------------------------------------------- */
/*                                    Lvgl                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/m5stack/lv_m5_emulator/blob/main/src/utility/lvgl_port_m5stack.cpp
static SemaphoreHandle_t xGuiSemaphore;
static SemaphoreHandle_t frame_done;
static SemaphoreHandle_t te_ready;
static QueueHandle_t frame_queue;
static std::atomic<bool> _lvgl_update_enabled = false;
static bool frame_pending = false; // Accessed only while holding xGuiSemaphore.
static uint32_t frame_stride;
static lv_area_t dirty_area;
static bool has_dirty_area = false;
static std::atomic<uint32_t> te_edge_us{0}, te_period_us{0}, te_edges{0};
static std::atomic<bool> redraw_needed{false};

struct DisplayFrame {
    const uint8_t* pixels;
    lv_area_t area;
};

static void IRAM_ATTR te_interrupt(void*)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    const uint32_t previous = te_edge_us.exchange(now, std::memory_order_relaxed);
    if (previous) te_period_us.store(now - previous, std::memory_order_relaxed);
    te_edges.fetch_add(1, std::memory_order_relaxed);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(te_ready, &wake);
    if (wake) portYIELD_FROM_ISR();
}

static void display_rtos_task(void*)
{
    uint32_t frames = 0, te_timeouts = 0, late_edges = 0;
    int64_t interval_start = esp_timer_get_time();
    int64_t transfer_total = 0, transfer_max = 0, packing_total = 0;
    uint32_t over_budget = 0;
    uint32_t phase_max = 0;
    DisplayFrame frame;
    while (true) {
        if (xQueueReceive(frame_queue, &frame, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // LVGL renders directly in panel byte order. Stage only the
            // changed rectangle, with no intermediate full-screen conversion.
            const int64_t pack_start = esp_timer_get_time();
            toolbox::display::pack_rows(dma_buffers[0], frame.pixels, frame_stride,
                frame.area, frame.area.y1, std::min<int>(dma_rows, frame.area.y2 - frame.area.y1 + 1), false);
            uint32_t packing_us = static_cast<uint32_t>(esp_timer_get_time() - pack_start);
            xSemaphoreTake(panel_mutex, portMAX_DELAY);
            xSemaphoreTake(te_ready, 0);
            bool synchronized = false;
            for (int attempt = 0; attempt < 3 && !synchronized; ++attempt) {
                if (xSemaphoreTake(te_ready, pdMS_TO_TICKS(25)) != pdTRUE) {
                    ++te_timeouts;
                    break;
                }
                const uint32_t phase = static_cast<uint32_t>(esp_timer_get_time()) - te_edge_us.load(std::memory_order_relaxed);
                phase_max = std::max(phase_max, phase);
                synchronized = phase <= 500;
                if (!synchronized) ++late_edges;
            }
            if (synchronized) {
                const int64_t start = esp_timer_get_time();
                packing_us += _display->present(frame.pixels, frame_stride, frame.area);
                const int64_t transfer = esp_timer_get_time() - start;
                packing_total += packing_us;
                const uint32_t period = te_period_us.load();
                if (period && transfer > period) ++over_budget;
                transfer_total += transfer;
                transfer_max = std::max(transfer_max, transfer);
                ++frames;
            } else {
                // Keep the previous image when synchronization is lost. Retry
                // through LVGL, including a static page whose only frame failed.
                redraw_needed.store(true);
            }
            xSemaphoreGive(panel_mutex);
            xSemaphoreGive(frame_done);
        }
        const int64_t now = esp_timer_get_time();
        if (now - interval_start >= 5000000) {
            ESP_LOGI("Display", "refresh-v5: %.1f fps (includes idle), tx avg/max %lld/%lld us, copy avg %lld us, over-budget %lu, TE period %lu us, edges %lu, late %lu, timeout %lu, phase max %lu us",
                frames * 1000000.0 / (now - interval_start),
                frames ? transfer_total / frames : 0, transfer_max,
                frames ? packing_total / frames : 0, static_cast<unsigned long>(over_budget),
                static_cast<unsigned long>(te_period_us.load()), static_cast<unsigned long>(te_edges.exchange(0)),
                static_cast<unsigned long>(late_edges), static_cast<unsigned long>(te_timeouts), static_cast<unsigned long>(phase_max));
            interval_start = now;
            frames = te_timeouts = late_edges = phase_max = 0;
            transfer_total = transfer_max = packing_total = 0;
            over_budget = 0;
        }
    }
}

static uint32_t lvgl_millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

static void wait_for_frame(lv_display_t*)
{
    if (frame_pending) {
        xSemaphoreTake(frame_done, portMAX_DELAY);
        frame_pending = false;
    }
}

static void lvgl_rtos_task(void*)
{
    while (true) {
        uint32_t next = 8;
        if (_lvgl_update_enabled && xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE) {
            if (_lvgl_update_enabled) {
                // Keep the two cores from contending for PSRAM while the panel
                // is scanning a transfer. DIRECT also waits before buffer sync.
                wait_for_frame(nullptr);
                if (redraw_needed.exchange(false)) {
                    lv_obj_invalidate(lv_screen_active());
                    lv_obj_invalidate(lv_layer_top());
                }
                next = lv_timer_handler();
            }
            xSemaphoreGive(xGuiSemaphore);
        }
        // Yield to app lifecycle work, with an 8 ms upper bound for touch input.
        vTaskDelay(pdMS_TO_TICKS(std::clamp<uint32_t>(next, 1, 8)));
    }
}

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels)
{
    // DIRECT can flush several dirty areas for one rendered frame. Merge
    // them and submit once, after all widgets in the frame have been drawn.
    if (!has_dirty_area) {
        dirty_area = *area;
        has_dirty_area = true;
    } else {
        dirty_area.x1 = std::min(dirty_area.x1, area->x1);
        dirty_area.y1 = std::min(dirty_area.y1, area->y1);
        dirty_area.x2 = std::max(dirty_area.x2, area->x2);
        dirty_area.y2 = std::max(dirty_area.y2, area->y2);
    }
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    const DisplayFrame frame = {pixels, toolbox::display::aligned_area(
        dirty_area, lv_display_get_horizontal_resolution(disp), lv_display_get_vertical_resolution(disp))};
    has_dirty_area = false;
    frame_pending = true;
    // LVGL's flush-wait callback prevents reuse until DMA ends.
    const BaseType_t queued = xQueueSend(frame_queue, &frame, portMAX_DELAY);
    configASSERT(queued == pdTRUE);
}

static void lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static lv_indev_data_t last = {};
    toolbox::TouchSample sample;
    portENTER_CRITICAL(&touch_mux);
    const bool available = touch_history.pop(sample);
    const bool more = !touch_history.empty();
    portEXIT_CRITICAL(&touch_mux);
    if (available) {
        toolbox::route_touch_sample(indev, data, sample, more);
        last = *data;
    } else {
        *data = last;
        data->continue_reading = false;
    }
}

void Hal::lvgl_init()
{
    mclog::tagInfo(_tag, "lvgl init");

    lv_init();
    lv_tick_set_cb(lvgl_millis);

    static lv_display_t *disp = lv_display_create(_display->width(), _display->height());
    if (disp == NULL) {
        printf("lv_display_create failed\n");
        return;
    }

    lv_display_set_driver_data(disp, _display.get());
    lv_display_set_flush_cb(disp, lvgl_flush_cb);

    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    frame_stride = lv_draw_buf_width_to_stride(_display->width(), LV_COLOR_FORMAT_RGB565_SWAPPED);
    const size_t frame_bytes = frame_stride * _display->height();
    auto* buf1 = heap_caps_calloc(1, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    auto* buf2 = heap_caps_calloc(1, frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (auto& buffer : dma_buffers) {
        buffer = static_cast<uint8_t*>(heap_caps_malloc(_display->width() * dma_rows * 2,
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    }
    ESP_ERROR_CHECK(buf1 && buf2 && dma_buffers[0] && dma_buffers[1] ? ESP_OK : ESP_ERR_NO_MEM);
    lv_display_set_buffers_with_stride(disp, buf1, buf2, frame_bytes, frame_stride, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_wait_cb(disp, wait_for_frame);
    lv_timer_set_period(lv_display_get_refr_timer(disp), 16);

    lvTouchpad = lv_indev_create();
    LV_ASSERT_MALLOC(lvTouchpad);
    if (lvTouchpad == NULL) {
        printf("lv_indev_create failed\n");
        return;
    }
    lv_indev_set_driver_data(lvTouchpad, _display.get());
    lv_indev_set_type(lvTouchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvTouchpad, lvgl_read_cb);
    lv_indev_set_display(lvTouchpad, disp);

    lv_timer_set_period(lv_indev_get_read_timer(lvTouchpad), 4);
    lv_indev_set_scroll_limit(lvTouchpad, 6);

    xGuiSemaphore = xSemaphoreCreateMutex();
    frame_done = xSemaphoreCreateBinary();
    te_ready = xSemaphoreCreateBinary();
    frame_queue = xQueueCreate(1, sizeof(DisplayFrame));
    ESP_ERROR_CHECK(xGuiSemaphore && frame_done && te_ready && frame_queue ? ESP_OK : ESP_ERR_NO_MEM);
    // GPIO38 is the StopWatch's CO5300 tearing-effect output.
    const esp_err_t isr_result = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(isr_result == ESP_ERR_INVALID_STATE ? ESP_OK : isr_result);
    ESP_ERROR_CHECK(gpio_set_intr_type(cfg_pin_te, GPIO_INTR_POSEDGE));
    ESP_ERROR_CHECK(gpio_isr_handler_add(cfg_pin_te, te_interrupt, nullptr));
    ESP_ERROR_CHECK(gpio_intr_enable(cfg_pin_te));
    const BaseType_t display_task = xTaskCreatePinnedToCore(display_rtos_task, "display_dma", 4096,
                                                           nullptr, 6, nullptr, 0);
    const BaseType_t gui_task = xTaskCreatePinnedToCore(lvgl_rtos_task, "lvgl", 16 * 1024,
                                                       nullptr, 4, nullptr, 1);
    ESP_ERROR_CHECK(display_task == pdPASS && gui_task == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_LOGI("Display", "RGB565_SWAPPED DIRECT double buffer, TE GPIO38, 80MHz QSPI, refresh 16ms / independent touch 4ms");

    startLvglUpdate();

    {
        LvglLockGuard lock;
        uitk::lvgl_cpp::ScreenActive screen;
        screen.setBgColor(lv_color_black());
        GetHAL().bootLogo = std::make_unique<BootLogo>();
    }
}

bool Hal::lvglLock()
{
    return xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) == pdTRUE ? true : false;
}

void Hal::lvglUnlock()
{
    xSemaphoreGive(xGuiSemaphore);
}

void Hal::startLvglUpdate()
{
    _lvgl_update_enabled = true;
}

void Hal::stopLvglUpdate()
{
    _lvgl_update_enabled = false;
    // Preserve exclusive display ownership for direct-M5GFX apps after return.
    xSemaphoreTake(xGuiSemaphore, portMAX_DELAY);
    wait_for_frame(nullptr);
    xSemaphoreGive(xGuiSemaphore);
}

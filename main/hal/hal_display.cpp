/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "display_frame.h"
#include "refresh_metrics.h"
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
static constexpr int dma_rows = CONFIG_TOOLBOX_DMA_ROWS;
namespace perf = toolbox::perf;
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

        _panel_instance.setBrightness(128);

        return true;
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
                const auto pack_start = perf::now();
                toolbox::display::pack_rows(buffer, pixels, stride, area, y, rows, false);
                packing_us += perf::now() - pack_start;
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
        static uint32_t previous = 0;
        const auto start = perf::now();
        const bool valid = _cst820->read();
        const auto sampled = perf::now();
        const uint32_t now = GetHAL().millis();
        if (previous) perf::sample(perf::touch_sample_interval, start - previous);
        previous = start;
        perf::sample(perf::touch_read_time, sampled - start);
        if (!valid) perf::sample(perf::touch_read_error, 1);
        if (valid) {
            last_good = now;
            failed_gesture = false;
            const bool pressed = _cst820->isPressed();
            int16_t x = _cst820->getX(), y = _cst820->getY();
            portENTER_CRITICAL(&touch_mux);
            const auto overflows = touch_history.overflows();
            // A release packet can carry the final movement. Feed that movement
            // before pen-up so LVGL can cancel a click and start scrolling.
            if (!pressed && touch_history.latest.pressed) {
                if (_cst820->isReleaseEvent() && x >= 0 && x < 468 && y >= 0 && y < 466)
                    touch_history.push({true, x, y, now, false, sampled});
                x = touch_history.latest.x;
                y = touch_history.latest.y;
            }
            touch_history.push({pressed, x, y, now, false, sampled});
            const auto depth = touch_history.size();
            const auto overflow = touch_history.overflows() - overflows;
            portEXIT_CRITICAL(&touch_mux);
            if (depth) perf::sample(perf::touch_queue_depth, depth);
            if (overflow) perf::sample(perf::touch_overflow, overflow);
        } else if (!failed_gesture && now - last_good >= 80) {
            portENTER_CRITICAL(&touch_mux);
            touch_history.cancel(now);
            portEXIT_CRITICAL(&touch_mux);
            failed_gesture = true;
        }
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(4)) == pdFALSE) {
            // After a failed or slow bus transaction, resume at the normal cadence.
            wake = xTaskGetTickCount();
            xTaskDelayUntil(&wake, pdMS_TO_TICKS(4));
        }
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
static bool refresh_deferred = false;
static uint32_t frame_stride;
static toolbox::display::FrameRegions dirty_regions;
static toolbox::display::FrameRegions repair_regions; // Published before frame_done.
static std::atomic<uint32_t> te_edge_us{0}, te_period_us{0};
static uint32_t dirty_pixels, recent_input_us, pending_input_us, input_consumed_us, gui_acquired_us;
static uint32_t repair_input_us;
#ifdef CONFIG_TOOLBOX_PERF
static uint32_t render_started_us, previous_render_us, sync_started_us;
#endif

struct DisplayFrame {
    const uint8_t* pixels;
    toolbox::display::FrameRegions regions;
    uint32_t input_us;
    uint32_t activity_us;
};

static void IRAM_ATTR te_interrupt(void*)
{
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
    const uint32_t previous = te_edge_us.exchange(now, std::memory_order_relaxed);
    if (previous) te_period_us.store(now - previous, std::memory_order_relaxed);
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(te_ready, &wake);
    if (wake) portYIELD_FROM_ISR();
}

static bool wait_for_te()
{
    constexpr uint32_t phase_limit_us = 500; // V5 window, kept for panel comparison.
    static uint32_t used_edge = 0;
    const auto start = perf::now();
    xSemaphoreTake(te_ready, 0);
    uint32_t edge = te_edge_us.load(std::memory_order_relaxed);
    uint32_t phase = static_cast<uint32_t>(esp_timer_get_time()) - edge;
    if (edge == used_edge || phase > phase_limit_us) {
        if (xSemaphoreTake(te_ready, pdMS_TO_TICKS(25)) != pdTRUE) {
            perf::sample(perf::te_timeout, 1);
            perf::sample(perf::te_wait_time, perf::now() - start);
            return false;
        }
        edge = te_edge_us.load(std::memory_order_relaxed);
        phase = static_cast<uint32_t>(esp_timer_get_time()) - edge;
        if (edge == used_edge || phase > phase_limit_us) {
            used_edge = edge;
            perf::sample(perf::te_missed, 1);
            perf::sample(perf::te_wait_time, perf::now() - start);
            return false;
        }
    }
    used_edge = edge;
    perf::sample(perf::te_phase, phase);
    perf::sample(perf::te_wait_time, perf::now() - start);
    return true;
}

static uint32_t stage_first_strip(const DisplayFrame& frame, const lv_area_t& area)
{
    const auto start = perf::now();
    toolbox::display::pack_rows(dma_buffers[0], frame.pixels, frame_stride, area,
        area.y1, std::min<int>(dma_rows, area.y2 - area.y1 + 1), false);
    return perf::now() - start;
}

static void display_rtos_task(void*)
{
    uint32_t previous_present = 0;
    bool previous_active = false;
    DisplayFrame frame;
    while (true) {
        if (xQueueReceive(frame_queue, &frame, pdMS_TO_TICKS(1000)) == pdTRUE) {
            // Prepare the first strip before waiting for the panel's blanking edge.
            uint32_t packing_us = stage_first_strip(frame, frame.regions.areas[0]);
            xSemaphoreTake(panel_mutex, portMAX_DELAY);
            if (wait_for_te()) {
                const auto start = perf::now();
                unsigned pixels = 0, bursts = 0;
                for (unsigned i = 0; i < frame.regions.count; ++i) {
                    const auto& area = frame.regions.areas[i];
                    if (i) packing_us += stage_first_strip(frame, area);
                    packing_us += _display->present(frame.pixels, frame_stride, area);
                    pixels += lv_area_get_size(&area);
                    bursts += (area.y2 - area.y1 + dma_rows) / dma_rows;
                }
                const auto presented = perf::now();
                const auto period = te_period_us.load(std::memory_order_relaxed);
                const auto transfer = presented - start;
                perf::sample(perf::dma_transfer_time, transfer);
                perf::sample(perf::dma_pack_time, packing_us);
                perf::sample(perf::transfer_pixels, pixels);
                perf::sample(perf::dma_bursts, bursts);
                if (period) perf::sample(perf::te_period, period);
                if (period && transfer > period) perf::sample(perf::dma_over_budget, 1);
                const bool active = frame.activity_us && presented - frame.activity_us < 250000;
                if (previous_present) {
                    perf::sample(perf::present_interval, presented - previous_present);
                    if (active && previous_active)
                        perf::sample(perf::active_present_interval, presented - previous_present);
                }
                if (frame.input_us)
                    perf::sample(perf::input_to_present, presented - frame.input_us);
                previous_present = presented;
                previous_active = active;
            } else {
                repair_regions = frame.regions;
                repair_input_us = frame.input_us;
                perf::sample(perf::frame_drop, 1);
            }
            xSemaphoreGive(panel_mutex);
            xSemaphoreGive(frame_done);
        }
    }
}

static uint32_t lvgl_millis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

static bool finish_frame(TickType_t timeout)
{
    if (frame_pending && xSemaphoreTake(frame_done, timeout) == pdTRUE)
        frame_pending = false;
    return !frame_pending;
}

static void wait_for_frame(lv_display_t*)
{
    finish_frame(portMAX_DELAY);
}

static void refresh_if_ready(lv_timer_t* timer)
{
    // LVGL DIRECT sync copies pixels out of the buffer held by DMA.
    // Gate only this timer. Input, animations and application logic can
    // continue to invalidate objects while the previous frame is in flight.
    if (!finish_frame(0)) {
        refresh_deferred = true;
        return;
    }
    refresh_deferred = false;
    for (unsigned i = 0; i < repair_regions.count; ++i)
        lv_obj_invalidate_area(lv_screen_active(), &repair_regions.areas[i]);
    repair_regions.count = 0;
    if (!pending_input_us) pending_input_us = repair_input_us;
    repair_input_us = 0;
    lv_display_refr_timer(timer);
}

static bool lock_gui()
{
    const auto start = perf::now();
    if (xSemaphoreTake(xGuiSemaphore, portMAX_DELAY) != pdTRUE) return false;
    gui_acquired_us = perf::now();
    perf::sample(perf::gui_lock_wait, gui_acquired_us - start);
    return true;
}

static void unlock_gui()
{
    perf::sample(perf::gui_lock_hold, perf::now() - gui_acquired_us);
    xSemaphoreGive(xGuiSemaphore);
}

static void lvgl_rtos_task(void*)
{
    while (true) {
        uint32_t next = 8;
        if (_lvgl_update_enabled && lock_gui()) {
            if (_lvgl_update_enabled) {
                // Wake a paused refresh timer when a static frame needs repair.
                if (finish_frame(0) && (refresh_deferred || repair_regions.count)) {
                    auto* timer = lv_display_get_refr_timer(lv_display_get_default());
                    lv_timer_resume(timer);
                    lv_timer_ready(timer);
                }
                next = lv_timer_handler();
            }
            unlock_gui();
        }
        vTaskDelay(pdMS_TO_TICKS(std::clamp<uint32_t>(next, 1, 8)));
    }
}

#ifdef CONFIG_TOOLBOX_PERF
static void measure_refresh(lv_event_t* event)
{
    const auto now = perf::now();
    switch (lv_event_get_code(event)) {
        case LV_EVENT_REFR_START:
            render_started_us = sync_started_us = 0;
            break;
        case LV_EVENT_FLUSH_WAIT_FINISH:
            // The first wait before RENDER_START belongs to DIRECT buffer sync.
            if (!render_started_us) sync_started_us = now;
            break;
        case LV_EVENT_RENDER_START:
            if (sync_started_us) perf::sample(perf::buffer_sync_time, now - sync_started_us);
            if (previous_render_us) perf::sample(perf::render_interval, now - previous_render_us);
            previous_render_us = render_started_us = now;
            break;
        case LV_EVENT_RENDER_READY:
            if (render_started_us) perf::sample(perf::render_time, now - render_started_us);
            break;
        default:
            break;
    }
}
#endif

static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* pixels)
{
    dirty_regions.add(toolbox::display::aligned_area(
        *area, lv_display_get_horizontal_resolution(disp), lv_display_get_vertical_resolution(disp)));
    dirty_pixels += lv_area_get_size(area);
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);
        return;
    }
    const uint32_t input_us = perf::now() - input_consumed_us < 250000 ? pending_input_us : 0;
    const DisplayFrame frame = {pixels, dirty_regions, input_us, recent_input_us};
    perf::sample(perf::dirty_pixels, dirty_pixels);
    dirty_regions.count = dirty_pixels = pending_input_us = 0;
    frame_pending = true;
    const BaseType_t queued = xQueueSend(frame_queue, &frame, 0);
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
        if (sample.sampled_us) {
            perf::sample(perf::touch_queue_delay, perf::now() - sample.sampled_us);
            if (!sample.cancel) {
                pending_input_us = recent_input_us = sample.sampled_us;
                input_consumed_us = perf::now();
            }
        }
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
    auto* refresh_timer = lv_display_get_refr_timer(disp);
    lv_timer_set_cb(refresh_timer, refresh_if_ready);
    lv_timer_set_period(refresh_timer, 16);
#ifdef CONFIG_TOOLBOX_PERF
    lv_display_add_event_cb(disp, measure_refresh, LV_EVENT_ALL, nullptr);
#endif

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

    auto* input_timer = lv_indev_get_read_timer(lvTouchpad);
    lv_timer_set_period(input_timer, 4);
#ifdef CONFIG_TOOLBOX_PERF
    lv_timer_set_cb(input_timer, [](lv_timer_t* timer) {
        const auto start = perf::now();
        lv_indev_read(static_cast<lv_indev_t*>(lv_timer_get_user_data(timer)));
        perf::sample(perf::event_process_time, perf::now() - start);
    });
#endif
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
    ESP_LOGI("Display", "Refresh V6: RGB565_SWAPPED DIRECT, TE GPIO38, 80MHz QSPI, DMA %d rows, touch 4ms", dma_rows);

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
    return lock_gui();
}

void Hal::lvglUnlock()
{
    unlock_gui();
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

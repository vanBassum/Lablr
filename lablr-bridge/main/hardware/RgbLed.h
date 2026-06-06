#pragma once

#include "BoardConfig.h"
#include "led_strip.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// On-board addressable RGB LED (WS2812) driver — just enough to turn the
// thing OFF. The S3 DevKit's RGB pixel is painfully bright; we blank it at
// boot. Pin is in BoardConfig.h; set RGB_LED_PIN to -1 to compile this out.
// ──────────────────────────────────────────────────────────────

class RgbLed
{
    static constexpr const char* TAG = "RgbLed";

public:
    void Init()
    {
        if constexpr (BoardConfig::RGB_LED_PIN < 0) return;

        led_strip_config_t stripCfg = {};
        stripCfg.strip_gpio_num        = BoardConfig::RGB_LED_PIN;
        stripCfg.max_leds              = 1;
        stripCfg.led_model             = LED_MODEL_WS2812;
        stripCfg.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;

        led_strip_rmt_config_t rmtCfg = {};
        rmtCfg.clk_src          = RMT_CLK_SRC_DEFAULT;
        rmtCfg.resolution_hz    = 10 * 1000 * 1000;   // 10 MHz
        rmtCfg.mem_block_symbols = 64;

        esp_err_t err = led_strip_new_rmt_device(&stripCfg, &rmtCfg, &strip_);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "RGB LED init failed: %s", esp_err_to_name(err));
            strip_ = nullptr;
            return;
        }
        Off();
    }

    void Off()
    {
        if (strip_) led_strip_clear(strip_);   // zero all pixels = off
    }

private:
    led_strip_handle_t strip_ = nullptr;
};

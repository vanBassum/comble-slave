#pragma once

#include "BoardConfig.h"
#include "drivers/Ssd1306.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

// ──────────────────────────────────────────────────────────────
// The 0.42" OLED, as this board presents it.
//
// A CONCRETE driver, reached through BoardContext::GetDisplay() and
// deliberately NOT a role on BoardProvider — see the note there: the day one
// board grows a display is not the day every other board owes a MockDisplay.
// Application code that calls this only compiles for a board that has one,
// which is the intended check.
//
// This class owns the I2C BUS, and the Ssd1306I2c driver owns only its device
// on it. That split is the point: a second chip on the same two pins is
// another i2c_master_bus_add_device() away, and no driver has to be told to
// share. If that ever happens, the bus moves up into BoardContext and both
// drivers take a handle — which is why Ssd1306I2c::Init() takes one rather
// than making its own.
// ──────────────────────────────────────────────────────────────

class Display
{
    static constexpr const char *TAG = "Display";

public:
    Display() = default;

    Display(const Display &) = delete;
    Display &operator=(const Display &) = delete;
    Display(Display &&) = delete;
    Display &operator=(Display &&) = delete;

    /// Bring up the bus and the panel. False leaves the board headless rather
    /// than failing the boot — a slave with a dead screen still bridges a UART,
    /// and that is the job.
    bool Init()
    {
        i2c_master_bus_config_t bus_cfg = {};
        bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_cfg.i2c_port = BoardConfig::OLED_I2C_PORT;
        bus_cfg.sda_io_num = static_cast<gpio_num_t>(BoardConfig::OLED_PIN_SDA);
        bus_cfg.scl_io_num = static_cast<gpio_num_t>(BoardConfig::OLED_PIN_SCL);
        // 7 is the IDF default and filters the sub-300 ns spikes a long trace
        // picks up. Harmless on a short one.
        bus_cfg.glitch_ignore_cnt = 7;
        bus_cfg.flags.enable_internal_pullup = BoardConfig::OLED_INTERNAL_PULLUP;

        esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
            return false;
        }

        // Ask before talking. A missing panel otherwise shows up as a stream of
        // write timeouts from inside the driver, which reads like a driver bug
        // rather than an absent module.
        err = i2c_master_probe(bus_, BoardConfig::OLED_ADDR, 100);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "No OLED answering at 0x%02X on SDA=%d SCL=%d (%s)",
                     BoardConfig::OLED_ADDR, BoardConfig::OLED_PIN_SDA,
                     BoardConfig::OLED_PIN_SCL, esp_err_to_name(err));
            return false;
        }

        err = panel_.Init(bus_, BoardConfig::OLED_ADDR, BoardConfig::OLED_I2C_HZ);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "SSD1306 init failed: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(TAG, "OLED up: %ux%u at 0x%02X", panel_.Width(), panel_.Height(),
                 BoardConfig::OLED_ADDR);
        return true;
    }

    /// The driver, for whoever is drawing. Returned by reference and not
    /// wrapped: the application wants the full API, which is exactly what the
    /// concrete-accessor escape hatch is for.
    Ssd1306I2c &panel() { return panel_; }

    bool ok() const { return panel_.ok(); }

    static constexpr uint8_t Width()  { return BoardConfig::OLED_PANEL.width; }
    static constexpr uint8_t Height() { return BoardConfig::OLED_PANEL.height; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    Ssd1306I2c panel_{ BoardConfig::OLED_PANEL, BoardConfig::OLED_EXTERNAL_VCC };
};

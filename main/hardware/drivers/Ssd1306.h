#pragma once

#include <cstdint>
#include <cstring>
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "fonts/TextStyle.h"

// ──────────────────────────────────────────────────────────────
// SSD1306 monochrome OLED — a board-independent driver, taking its bus and its
// panel geometry as parameters the way every driver in this folder does.
//
// Ported from the firefly-guest project, where it drove the 0.42" OLED on an
// ESP32-C3 SuperMini. Two things were tidied on the way across and one thing
// was deliberately left exactly as it was; all three are worth reading before
// changing anything here.
//
// ── The geometry is a WINDOW, not a size ──────────────────────
// This is the part that looks like a hack and is not. An SSD1306 always
// addresses a 128x64 RAM. On a full 0.96" module the glass is that whole RAM.
// On the 0.42" 72x40 module the glass is a *window* onto it — columns 28..99,
// pages 3..7 — and the rest of the RAM is simply not wired to anything. So the
// column and page offsets below are the physical position of the glass, not a
// fudge factor someone tuned until it looked right, and 40 rows is exactly the
// five pages 3..7. Get that wrong and the picture is not slightly off, it is
// somewhere else.
//
// The original carried the offsets as two local constants inside show() and
// reported the visible size from two hardcoded literals in getWidth() and
// getHeight(), so the driver could only ever serve that one panel and said so
// nowhere. Here the window is a struct, and the two ready-made ones below are
// the panels this family actually ships as.
//
// ── What changed, and what it costs ───────────────────────────
//   * The framebuffer is the VISIBLE area, not the whole RAM. The original
//     allocated 128x64 (1 KB, with new[]) and pushed all eight pages every
//     frame, of which three addressed pages the 0.42" panel does not have. This
//     keeps 72x40 (360 B, no allocation at all) and pushes five. Identical
//     pixels on the glass, a third of the I2C traffic per frame.
//   * drawPixel clips to the visible window rather than to the RAM, so a caller
//     that trusts Width() cannot quietly draw into memory that is not wired up.
//   * The command stream for the 0.42" panel is BYTE-FOR-BYTE what the original
//     sent — same mux ratio, same COM pin config, same column and page
//     addresses. That is on purpose: those values are hardware-verified on this
//     panel and nothing here is worth the risk of "improving" them untested.
//
// ── SPI is not here ───────────────────────────────────────────
// The original also had an SSD1306_SPI peer. It is left out rather than carried
// dead: keeping it would put esp_driver_spi in this firmware's COMPONENT_REQUIRES
// for a bus nothing on this board uses. The seam that made it cheap is still
// here — writeCmd/writeData are the only things a transport implements — so
// adding it back is a ~25-line subclass, not a redesign.
// ──────────────────────────────────────────────────────────────

/// Where the glass sits inside the controller's RAM, and how big it is.
struct Ssd1306Panel
{
    uint8_t muxHeight;    // rows the controller is wired to scan -> MUX ratio
    uint8_t ramWidth;     // controller addressing width; 128 on every SSD1306
    uint8_t width;        // visible glass width, in pixels
    uint8_t height;       // visible glass height, in pixels (a multiple of 8)
    uint8_t colOffset;    // first RAM column the glass shows
    uint8_t pageOffset;   // first RAM page the glass shows
};

/// The 0.42" 72x40 module, as fitted to the ESP32-C3 SuperMini OLED board.
/// muxHeight stays 64 even though only 40 rows are visible — the controller is
/// wired for the full scan and the glass covers part of it. This is the
/// hardware-verified combination.
inline constexpr Ssd1306Panel kSsd1306_0p42_72x40{ 64, 128, 72, 40, 28, 3 };

/// The ordinary 0.96" 128x64 module, where the glass is the whole RAM.
/// Untested here — kept because it is the other panel this driver family is
/// always asked for, and because writing it down is what makes the window
/// concept above legible.
inline constexpr Ssd1306Panel kSsd1306_0p96_128x64{ 64, 128, 128, 64, 0, 0 };

// ──────────────────────────────────────────────────────────────
// The transport-agnostic core. A transport supplies writeCmd/writeData and
// gets everything else.
// ──────────────────────────────────────────────────────────────
class Ssd1306
{
    static constexpr const char *TAG = "Ssd1306";

    // ── SSD1306 command set, as much of it as is used ──
    static constexpr uint8_t kSetContrast      = 0x81;
    static constexpr uint8_t kSetEntireOn      = 0xA4;
    static constexpr uint8_t kSetNormInv       = 0xA6;
    static constexpr uint8_t kSetDisp          = 0xAE;
    static constexpr uint8_t kSetMemAddr       = 0x20;
    static constexpr uint8_t kSetDispStartLine = 0x40;
    static constexpr uint8_t kSetSegRemap      = 0xA0;
    static constexpr uint8_t kSetMuxRatio      = 0xA8;
    static constexpr uint8_t kSetComOutDir     = 0xC0;
    static constexpr uint8_t kSetDispOffset    = 0xD3;
    static constexpr uint8_t kSetComPinCfg     = 0xDA;
    static constexpr uint8_t kSetDispClkDiv    = 0xD5;
    static constexpr uint8_t kSetPrecharge     = 0xD9;
    static constexpr uint8_t kSetVcomDesel     = 0xDB;
    static constexpr uint8_t kSetChargePump    = 0x8D;

public:
    /// Big enough for a full 128x64 panel. The framebuffer is a fixed member
    /// rather than a new[]: the largest case is 1 KB, a driver that cannot fail
    /// to allocate is one less failure path, and nothing here wants a heap.
    static constexpr size_t kMaxBuffer = 128 * 8;

    explicit Ssd1306(const Ssd1306Panel &panel, bool externalVcc = false)
        : panel_(panel), externalVcc_(externalVcc), pages_(panel.height / 8)
    {
        memset(buffer_, 0, sizeof(buffer_));
    }

    virtual ~Ssd1306() = default;

    Ssd1306(const Ssd1306 &) = delete;
    Ssd1306 &operator=(const Ssd1306 &) = delete;
    Ssd1306(Ssd1306 &&) = delete;
    Ssd1306 &operator=(Ssd1306 &&) = delete;

    /// The VISIBLE size — what a caller may draw into. Derived from the panel,
    /// never a literal.
    uint8_t Width() const { return panel_.width; }
    uint8_t Height() const { return panel_.height; }

    /// Whether the geometry fits the framebuffer. False means the panel struct
    /// is wrong, not that the hardware is missing — check before Show().
    bool GeometryOk() const
    {
        return panel_.height % 8 == 0 &&
               static_cast<size_t>(panel_.width) * pages_ <= kMaxBuffer;
    }

    void InitDisplay()
    {
        const uint8_t cmds[] = {
            static_cast<uint8_t>(kSetDisp | 0x00),          // display off
            kSetMemAddr, 0x00,                              // horizontal addressing
            static_cast<uint8_t>(kSetDispStartLine | 0x00),
            static_cast<uint8_t>(kSetSegRemap | 0x01),      // column 127 -> SEG0
            kSetMuxRatio, static_cast<uint8_t>(panel_.muxHeight - 1),
            static_cast<uint8_t>(kSetComOutDir | 0x08),     // scan COM[N-1] -> COM0
            kSetDispOffset, 0x00,
            kSetComPinCfg,
            static_cast<uint8_t>((panel_.ramWidth > 2 * panel_.muxHeight) ? 0x02 : 0x12),
            kSetDispClkDiv, 0x80,
            kSetPrecharge, static_cast<uint8_t>(externalVcc_ ? 0x22 : 0xF1),
            kSetVcomDesel, 0x30,
            kSetContrast, 0xFF,
            kSetEntireOn,                                   // follow RAM, not all-on
            kSetNormInv,
            kSetChargePump, static_cast<uint8_t>(externalVcc_ ? 0x10 : 0x14),
            static_cast<uint8_t>(kSetDisp | 0x01),          // display on
        };
        for (uint8_t c : cmds) writeCmd(c);
        Fill(false);
        Show();
    }

    void PowerOff() { writeCmd(kSetDisp | 0x00); }
    void PowerOn()  { writeCmd(kSetDisp | 0x01); }

    void Contrast(uint8_t value) { writeCmd(kSetContrast); writeCmd(value); }
    void Invert(bool inv)        { writeCmd(kSetNormInv | (inv ? 1 : 0)); }

    /// Clear the framebuffer. Not sent until Show().
    void Fill(bool lit) { memset(buffer_, lit ? 0xFF : 0x00, BufferBytes()); }

    void DrawPixel(int x, int y, bool lit)
    {
        // Clipped to the VISIBLE window, not to the controller's RAM: a caller
        // that trusts Width() should not be able to write somewhere no glass is.
        if (x < 0 || x >= panel_.width || y < 0 || y >= panel_.height) return;
        const int page = y / 8;
        const int bit = y % 8;
        uint8_t &b = buffer_[x + page * panel_.width];
        if (lit) b |= static_cast<uint8_t>(1 << bit);
        else     b &= static_cast<uint8_t>(~(1 << bit));
    }

    void DrawChar(int x, int y, char c, const TextStyle &style)
    {
        if (!style.font) return;
        const uint8_t *glyph = style.font->GetGlyph(c);
        if (!glyph) return;

        for (int col = 0; col < style.font->width; ++col)
        {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < style.font->height; ++row)
            {
                if (!(bits & (1 << row))) continue;
                // One font pixel becomes a size x size block.
                for (int dx = 0; dx < style.size; ++dx)
                    for (int dy = 0; dy < style.size; ++dy)
                        DrawPixel(x + col * style.size + dx,
                                  y + row * style.size + dy,
                                  style.color);
            }
        }
    }

    void DrawText(int x, int y, const char *str, const TextStyle &style)
    {
        if (!style.font || !str) return;
        int cursorX = x;
        while (*str)
        {
            DrawChar(cursorX, y, *str++, style);
            cursorX += (style.font->width + 1) * style.size;
        }
    }

    /// Push the framebuffer to the glass. Only the visible pages are sent —
    /// see the window note at the top.
    void Show()
    {
        for (uint8_t page = 0; page < pages_; ++page)
        {
            const uint8_t col = panel_.colOffset;
            writeCmd(static_cast<uint8_t>(0xB0 + page + panel_.pageOffset));
            writeCmd(static_cast<uint8_t>(col & 0x0F));           // lower column
            writeCmd(static_cast<uint8_t>(0x10 | (col >> 4)));    // higher column
            writeData(&buffer_[page * panel_.width], panel_.width);
        }
    }

protected:
    virtual void writeCmd(uint8_t cmd) = 0;
    virtual void writeData(const uint8_t *data, size_t len) = 0;

    size_t BufferBytes() const
    {
        return static_cast<size_t>(panel_.width) * pages_;
    }

    Ssd1306Panel panel_;
    bool externalVcc_;
    uint8_t pages_;
    uint8_t buffer_[kMaxBuffer];
};

// ──────────────────────────────────────────────────────────────
// I2C transport. Takes an ALREADY-CREATED bus handle — it adds a device to it
// and does not own it, so the board keeps control of its own I2C bus and can
// hang a second chip off the same pins.
// ──────────────────────────────────────────────────────────────
class Ssd1306I2c : public Ssd1306
{
    static constexpr const char *TAG = "Ssd1306I2c";

public:
    explicit Ssd1306I2c(const Ssd1306Panel &panel, bool externalVcc = false)
        : Ssd1306(panel, externalVcc) {}

    /// Attach to `bus` at `addr` and bring the panel up. 0x3C is the address
    /// these modules are strapped to; 0x3D is the only other one they offer.
    esp_err_t Init(i2c_master_bus_handle_t bus, uint8_t addr = 0x3C,
                   uint32_t sclHz = 400000)
    {
        if (!GeometryOk())
        {
            ESP_LOGE(TAG, "Panel geometry does not fit the framebuffer");
            return ESP_ERR_INVALID_ARG;
        }

        address_ = addr;
        bus_ = bus;

        i2c_device_config_t dev_cfg = {};
        dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_cfg.device_address = address_;
        dev_cfg.scl_speed_hz = sclHz;

        const esp_err_t err = i2c_master_bus_add_device(bus_, &dev_cfg, &dev_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
            return err;
        }

        InitDisplay();
        ESP_LOGI(TAG, "Up at 0x%02X, %ux%u visible", address_, Width(), Height());
        return ESP_OK;
    }

    bool ok() const { return dev_ != nullptr; }

protected:
    void writeCmd(uint8_t cmd) override
    {
        // Control byte 0x80: Co = 1, D/C# = 0 — one command follows.
        const uint8_t frame[2] = { 0x80, cmd };
        const esp_err_t err = i2c_master_transmit(dev_, frame, sizeof(frame), 50);
        if (err != ESP_OK)
            ESP_LOGW(TAG, "writeCmd 0x%02X: %s", cmd, esp_err_to_name(err));
    }

    void writeData(const uint8_t *data, size_t len) override
    {
        // Control byte 0x40: Co = 0, D/C# = 1 — the rest of the frame is data.
        // Chunked because one page can outrun the driver's own buffer, and the
        // yield keeps a full-frame flush off the watchdog's back.
        constexpr size_t kChunk = 32;
        uint8_t frame[kChunk + 1];
        frame[0] = 0x40;

        while (len > 0)
        {
            const size_t n = (len > kChunk) ? kChunk : len;
            memcpy(&frame[1], data, n);

            const esp_err_t err = i2c_master_transmit(dev_, frame, n + 1, 100);
            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "writeData: %s", esp_err_to_name(err));
                return;
            }
            data += n;
            len -= n;
            vTaskDelay(0);
        }
    }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    uint8_t address_ = 0x3C;
};

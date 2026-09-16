#pragma once

#include "drivers/Ssd1306.h"

// ──────────────────────────────────────────────────────────────
// BoardContext configuration — ESP32-C3 SuperMini **OLED**.
//
// The same ~18x22 mm C3 board as esp32c3_supermini next door, with a 0.42"
// monochrome OLED soldered on: ESP32-C3FH4/FN4 (RISC-V, 4 MB flash, no PSRAM),
// USB-C wired to the chip's native USB Serial/JTAG, ceramic antenna.
//
// It is a SEPARATE board folder rather than a flag on the plain one, because
// the plain SuperMini has no OLED and every value below would be a lie there —
// and the convention here is one folder per target board. The two files are
// near-identical today; that is the cost, and it is cheaper than a board that
// claims hardware it has not got.
//
// ── The pin map is MEASURED, not guessed ──────────────────────
// Every OLED constant below came from firefly-guest, where this exact board
// ran a game on this exact panel — not from a datasheet or a vendor page. The
// 0.42" SuperMini variants are not consistent about this and the silkscreen
// does not say, so treat a blank panel as a wiring question only after
// checking that the module really is the 72x40 one.
// ──────────────────────────────────────────────────────────────

namespace BoardConfig
{
    // ── LED ────────────────────────────────────────────────────
    // The SuperMini's blue LED sits between 3V3 and GPIO8, so it lights when
    // the pin is driven LOW — active low, unlike the DevKit. GPIO8 is also a
    // boot strapping pin (it must read high at reset), but it is only driven
    // after the bootloader has run, so using it as an output is safe. Holding
    // the LED on across a reset is not.
    static constexpr int LED_PIN = 8;
    static constexpr bool LED_ACTIVE_HIGH = false;

    // ── 0.42" SSD1306 OLED, on I2C ─────────────────────────────
    // SDA/SCL are the two pins the onboard panel is wired to. They are NOT
    // free for anything else on this board.
    static constexpr int OLED_PIN_SDA = 5;
    static constexpr int OLED_PIN_SCL = 6;

    // I2C port 0. The C3 has two; nothing else here uses either.
    static constexpr int OLED_I2C_PORT = 0;

    // 400 kHz — fast mode, which these modules do without complaint. The
    // panel is 360 bytes a frame, so this is the difference between a ~10 ms
    // flush and a ~40 ms one.
    static constexpr uint32_t OLED_I2C_HZ = 400000;

    // 0x3C. The SSD1306 only offers 0x3C or 0x3D, strapped on the module; every
    // one of these boards seen so far is 0x3C.
    static constexpr uint8_t OLED_ADDR = 0x3C;

    // No external VCC rail — the panel runs off the controller's own charge
    // pump, which is what the module is built for. Setting this true on a
    // module without the rail gives a dark panel that still ACKs every write,
    // which is a confusing way to fail.
    static constexpr bool OLED_EXTERNAL_VCC = false;

    // Internal pull-ups are enough for the two short onboard traces. A module
    // on flying leads may want real resistors instead.
    static constexpr bool OLED_INTERNAL_PULLUP = true;

    // Which panel, and therefore where the glass sits inside the controller's
    // 128x64 RAM. See the window note at the top of Ssd1306.h — these offsets
    // are the physical position of the glass, not a tuning knob.
    static constexpr Ssd1306Panel OLED_PANEL = kSsd1306_0p42_72x40;

    // ── Everything else on this board ──────────────────────────
    //   GPIO9         BOOT button (strapping; low at reset = download mode)
    //   GPIO18/GPIO19 USB D-/D+ — taken by USB Serial/JTAG, do not reuse
    //   GPIO20/GPIO21 UART0 RX/TX — the bridged UART this product exists for
    //   GPIO5/GPIO6   the OLED, above
    // Free: GPIO0-GPIO4, GPIO7, GPIO10.
}

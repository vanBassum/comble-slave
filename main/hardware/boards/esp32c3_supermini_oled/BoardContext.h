#pragma once

#include "InitState.h"
#include "BoardConfig.h"
#include "interfaces/BoardProvider.h"
#include "drivers/GpioLed.h"
#include "Display.h"

// ──────────────────────────────────────────────────────────────
// The board layer's context for the ESP32-C3 SuperMini OLED: owns every
// hardware driver instance (and bus host) and answers BoardProvider.
//
// The bottom layer, and it depends on nothing above it — not the framework,
// not the application. Drivers take their pins and buses as constructor
// arguments, so nothing here needs the provider to find a peer; BoardProvider
// exists for the layer above.
//
// Every board folder provides a class named BoardContext; #include
// "BoardContext.h" resolves to the board selected with -DBOARD=<name>.
//
// Surface rules:
//   • role interfaces (Led&, ...) for devices the application addresses by
//     meaning — declared on BoardProvider, so every board owes every role and
//     binds a Mock* driver when not fitted;
//   • concrete driver accessors are the escape hatch for when the application
//     needs a driver's full API. GetDisplay() is one, and it stays OFF
//     BoardProvider: that is what stops the role list becoming the union of
//     every board's peripherals, and what keeps the DevKit and the plain
//     SuperMini from owing a MockDisplay.
// ──────────────────────────────────────────────────────────────

class BoardContext : public BoardProvider
{
    static constexpr const char *TAG = "Board";

public:
    BoardContext() = default;

    BoardContext(const BoardContext &) = delete;
    BoardContext &operator=(const BoardContext &) = delete;
    BoardContext(BoardContext &&) = delete;
    BoardContext &operator=(BoardContext &&) = delete;

    void Init();

    // ── Roles (BoardProvider) ──
    Led &GetLed() override { return led_; }

    // ── Concrete drivers (this board only) ──
    Display &GetDisplay() { return display_; }

    /// Whether the panel answered. A false here is not a boot failure: the
    /// adapter's job is bridging a UART, and it does that blind. UiManager
    /// checks this and stays out of the way.
    bool HasDisplay() const { return displayOk_; }

private:
    InitState initState_;

    // Hardware instances — buses first, then the drivers that use them.
    // (Display owns its own I2C bus; see the note in Display.h.)
    GpioLed led_{ BoardConfig::LED_PIN, BoardConfig::LED_ACTIVE_HIGH };
    Display display_;
    bool displayOk_ = false;
};

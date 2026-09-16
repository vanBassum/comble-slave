#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "Timer.h"

// ──────────────────────────────────────────────────────────────
// The adapter's screen: the six digits a host needs, and what state this
// adapter is in.
//
// An application manager like LedManager, reaching hardware the way the
// layering allows — app_.getBoard().GetDisplay(), a concrete accessor only a
// board with a panel defines. Everything display-shaped lives in the .cpp
// behind BOARD_HAS_DISPLAY (the board.cmake flag that decides whether this
// file is compiled at all), and this header deliberately names no driver type
// so it stays includable from a headless build.
//
// The passkey is the whole reason this exists. BleSlaveManager holds it and
// says so in its own header — "the six digits a host must be given to pair,
// shown on the OLED once that exists; logged at boot until then". This is that
// existing.
// ──────────────────────────────────────────────────────────────

class UiManager
{
    static constexpr const char *TAG = "UiManager";

public:
    explicit UiManager(AppProvider &app) : app_(app) {}

    UiManager(const UiManager &) = delete;
    UiManager &operator=(const UiManager &) = delete;
    UiManager(UiManager &&) = delete;
    UiManager &operator=(UiManager &&) = delete;

    void Init();

private:
    AppProvider &app_;
    InitState initState_;

    Timer timer_;

    void OnTick();

    /// Recompose the three lines and push them only if they differ from what is
    /// already on the glass. `force` skips the comparison, for the first draw.
    void Redraw(bool force);

    // What the panel currently shows. A flush is 360 bytes over I2C, so the
    // comparison is cheaper than the write it avoids — and on a bonded, idle
    // adapter it avoids every single one.
    char last1_[20] = {};
    char last2_[20] = {};
    char last3_[20] = {};
};

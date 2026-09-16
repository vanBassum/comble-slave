#pragma once

#include "AppProvider.h"
#include "BoardContext.h"
#include "StruxProvider.h"
#include "LedManager/LedManager.h"
#include "Ble/BleSlaveManager.h"
#include "Ui/UiManager.h"

// The application layer's context: owns this product's managers and answers AppProvider.
//
// This is the file a fork edits. Strux's own managers, their init order and their wiring
// all live one layer down in StruxContext, so pulling an improvement from the template
// does not touch anything here — which is the whole reason the layers were split.
//
// Adding an application manager means: create the class taking AppProvider&, add an
// accessor to AppProvider, add the member here, and call its Init() below. The framework
// does not need to be told it exists; the manager registers its own commands and
// settings into Strux from its Init().
class AppContext : public AppProvider
{
public:
    AppContext(BoardContext& board, StruxProvider& strux)
        : board_(board), strux_(strux) {}

    ~AppContext() = default;
    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    /// Bring the application up. Called last: every manager here registers into the
    /// framework, so the framework has to be ready before any of this runs.
    void Init()
    {
        ledManager_.Init();
        ble_.Init();
        // Last: it draws the passkey and the link state, so it wants the BLE
        // manager already up rather than a screen that says nothing for a tick.
        ui_.Init();
    }

    StruxProvider& getStrux() override { return strux_; }
    BoardContext& getBoard() override { return board_; }
    LedManager& getLedManager() override { return ledManager_; }
    BleSlaveManager& getBleSlave() override { return ble_; }

private:
    BoardContext& board_;
    StruxProvider& strux_;

    LedManager ledManager_{*this};

    // The BLE peripheral: advertising, pairing and ownership. On AppProvider
    // now, because UiManager needs the passkey and the link state — the rule
    // was that a manager earns its accessor when a peer actually needs one,
    // and one does.
    BleSlaveManager ble_{*this};

    // The OLED. Compiles to nothing on a board without a panel, and checks
    // BoardContext::HasDisplay() even on one that claims a panel — a dead
    // screen must not stop an adapter bridging its UART.
    UiManager ui_{*this};
};

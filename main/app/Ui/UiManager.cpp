#include "UiManager.h"
#include "esp_log.h"

#ifdef BOARD_HAS_DISPLAY

#include "BoardContext.h"
#include "StruxProvider.h"
#include "SystemManager.h"
#include "Ble/BleSlaveManager.h"
#include "Timer.h"

#include <cstdio>
#include <cstring>

// ──────────────────────────────────────────────────────────────
// 72x40 is 12 characters of 5x7 across and five rows down, and that is the
// whole design budget. So the screen answers exactly one question — what does
// someone standing in front of this adapter need to know right now? — and the
// answer is a state machine with three states:
//
//   not bonded   the six digits, big. Nothing else matters: the adapter is
//                waiting to be claimed and the number is the only way to do it.
//   bonded, away the name, and that it is owned but nobody is on the link.
//   bonded, live the name, and that a host is connected.
//
// Redrawn only when the text changes. The panel is 360 bytes over I2C at
// 400 kHz, so a redraw is cheap but not free, and a flush every tick would
// have the adapter spending its life talking to a screen nobody is reading.
// ──────────────────────────────────────────────────────────────

namespace
{
    constexpr const char *TAG = "UiManager";

    constexpr uint32_t kPollMs = 500;

    // 5x7 glyphs advance 6 px at size 1, 12 at size 2.
    constexpr int kLine1 = 0;
    constexpr int kLine2 = 10;
    constexpr int kLine3 = 22;
}

void UiManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    if (!app_.getBoard().HasDisplay())
    {
        // Not a failure. The adapter bridges a UART blind perfectly well, and
        // BleSlaveManager still logs the passkey at boot for exactly this case.
        ESP_LOGW(TAG, "No display — nothing to draw on");
        init.SetReady();
        return;
    }

    timer_.SetHandler([this] { OnTick(); });
    timer_.Init("ui", pdMS_TO_TICKS(kPollMs));

    // Draw once before the first tick, so the panel is right from the moment it
    // lights rather than up to half a second later.
    Redraw(true);
    timer_.Start();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

void UiManager::OnTick()
{
    Redraw(false);
}

void UiManager::Redraw(bool force)
{
    BleSlaveManager &ble = app_.getBleSlave();

    const bool bonded = ble.IsBonded();
    const bool connected = ble.IsConnected();

    // Compose first, compare, and only then touch the panel — see the note
    // above about not flushing every tick.
    char line1[20] = {};
    char line2[20] = {};
    char line3[20] = {};

    if (!bonded)
    {
        char name[32] = {};
        app_.getStrux().getSystemManager().GetDeviceName(name, sizeof(name));

        // Six digits at size 2 is 12 px a glyph — 72 px exactly, the full width
        // of the glass with nothing to spare. That is deliberate: it is the one
        // thing on this screen that has to be readable across a bench.
        snprintf(line1, sizeof(line1), "%.12s", name);
        snprintf(line2, sizeof(line2), "%06lu",
                 static_cast<unsigned long>(ble.Passkey() % 1000000UL));
        snprintf(line3, sizeof(line3), "pair me");
    }
    else
    {
        char name[32] = {};
        app_.getStrux().getSystemManager().GetDeviceName(name, sizeof(name));
        snprintf(line1, sizeof(line1), "%.12s", name);
        snprintf(line2, sizeof(line2), "%s", connected ? "LINKED" : "idle");
        snprintf(line3, sizeof(line3), "%s", connected ? "host on" : "owned");
    }

    if (!force &&
        strcmp(line1, last1_) == 0 &&
        strcmp(line2, last2_) == 0 &&
        strcmp(line3, last3_) == 0)
        return;

    snprintf(last1_, sizeof(last1_), "%s", line1);
    snprintf(last2_, sizeof(last2_), "%s", line2);
    snprintf(last3_, sizeof(last3_), "%s", line3);

    Ssd1306I2c &panel = app_.getBoard().GetDisplay().panel();
    panel.Fill(false);
    panel.DrawText(0, kLine1, line1, TextStyle::Default(1));
    panel.DrawText(0, kLine2, line2, TextStyle::Default(bonded ? 1 : 2));
    panel.DrawText(0, kLine3, line3, TextStyle::Default(1));
    panel.Show();
}

#else   // Board without a panel — the class exists and does nothing.

void UiManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init) return;
    ESP_LOGD(TAG, "This board has no display; nothing to draw on");
    init.SetReady();
}

void UiManager::OnTick() {}
void UiManager::Redraw(bool) {}

#endif

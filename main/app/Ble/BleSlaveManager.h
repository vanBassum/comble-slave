#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "TypedSettings.h"
#include <cstdint>

// ──────────────────────────────────────────────────────────────
// The slave's BLE half: it advertises, accepts one host, and bonds with it.
//
// We are the PERIPHERAL. A host scans, picks this adapter out of a list, pairs
// using the passkey this device holds, and from then on owns it.
//
// Ownership is the point, not a detail. Several people may each have their own
// host while sharing one bag of adapters, so "which host does this adapter
// answer to" has to survive a disconnect, a power cycle and somebody else's
// host scanning nearby. Three states, deliberately distinct:
//
//   connected   a host is on the link right now
//   bonded      a host owns this adapter — persisted in NVS, survives reboots
//   released    the bond has been deliberately erased; the adapter is free
//
// A disconnect moves only the first. That is why the adapter keeps advertising
// while bonded: it must stay findable by ITS host, and going quiet would make a
// dropped link look like a released adapter.
//
// Pairing is passkey-based, and the passkey lives HERE rather than on the host:
// this device is the thing you can physically pick up, so it is the thing that
// can prove which adapter you mean. It is generated once, persisted, and will
// be shown on the OLED — until then it is logged at boot.
// ──────────────────────────────────────────────────────────────

class BleSlaveManager
{
    static constexpr const char *TAG = "BleSlave";

public:
    explicit BleSlaveManager(AppProvider &app) : app_(app) {}

    BleSlaveManager(const BleSlaveManager &) = delete;
    BleSlaveManager &operator=(const BleSlaveManager &) = delete;
    BleSlaveManager(BleSlaveManager &&) = delete;
    BleSlaveManager &operator=(BleSlaveManager &&) = delete;

    void Init();

    /// Is a host on the link right now? Not the same as owned — see the header note.
    bool IsConnected() const { return connected_; }

    /// Does a host own this adapter? True across reboots once paired.
    bool IsBonded() const;

    /// The six digits a host must be given to pair. Shown on the OLED once that
    /// exists; logged at boot until then.
    uint32_t Passkey() const { return passkey_; }

    /// Erase every bond: the deliberate "release" of the three states above.
    /// Any currently connected host is dropped, because a host that keeps a link
    /// it no longer owns is exactly the confusion this is meant to prevent.
    void ReleaseAllBonds();

private:
    AppProvider &app_;
    InitState initState_;

    uint32_t passkey_ = 0;
    char advName_[32] = {};
    uint8_t ownAddrType_ = 0;
    volatile bool connected_ = false;
    uint16_t connHandle_ = 0;

    void LoadOrCreatePasskey();
    void StartAdvertising();
    void OnSync();
    int OnGapEvent(struct ble_gap_event *event);

    static void HostTaskStatic(void *param);
    static void OnSyncStatic();
    static void OnResetStatic(int reason);
    static int GapEventStatic(struct ble_gap_event *event, void *arg);

public:
    // Public only because the GATT table's function-pointer field needs it.
    static int GattAccessStatic(uint16_t conn, uint16_t attrHandle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

private:
    // ── Settings ──
    // Generated on first boot rather than shipped as a constant: a fleet of
    // adapters that all pair with 000000 is not a pairing scheme. NVS keys are
    // capped at 15 characters, checked at RUNTIME by Register().
    inline static UInt32Setting passkeySetting_{ "ble.passkey", "BLE Pairing Code", 0 };
};

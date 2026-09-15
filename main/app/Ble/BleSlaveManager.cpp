#include "BleSlaveManager.h"
#include "CombleBleProtocol.h"

#include "StruxProvider.h"
#include "SettingsManager.h"
#include "SystemManager.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_bt.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include <cstdio>
#include <cstring>

// Provided by NimBLE's bundled store; it is what makes bonds survive a reboot
// (CONFIG_BT_NIMBLE_NVS_PERSIST). Declared here because ESP-IDF does not ship a
// header for it.
extern "C" void ble_store_config_init(void);

static BleSlaveManager *s_instance = nullptr;

namespace
{
    uint16_t s_inboundHandle = 0;    // slave → host, notify
    uint16_t s_outboundHandle = 0;   // host → slave, write

    const struct ble_gatt_chr_def s_chrs[] = {
        {
            // Slave → host. Notify only: the host subscribes and we push. There is
            // nothing to read, so no read flag and no read handler.
            .uuid = &comble::CHR_INBOUND_UUID.u,
            .access_cb = &BleSlaveManager::GattAccessStatic,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_NOTIFY,
            .min_key_size = 0,
            .val_handle = &s_inboundHandle,
            .cpfd = nullptr,
        },
        {
            // Host → slave. Encryption AND authentication are demanded at the
            // ATTRIBUTE level, which is the whole ownership model in one line: a
            // host that connects without having paired cannot write a byte, and
            // the stack enforces that rather than our code remembering to.
            .uuid = &comble::CHR_OUTBOUND_UUID.u,
            .access_cb = &BleSlaveManager::GattAccessStatic,
            .arg = nullptr,
            .descriptors = nullptr,
            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC |
                     BLE_GATT_CHR_F_WRITE_AUTHEN,
            .min_key_size = 16,
            .val_handle = &s_outboundHandle,
            .cpfd = nullptr,
        },
        {},   // terminator
    };

    const struct ble_gatt_svc_def s_svcs[] = {
        {
            .type = BLE_GATT_SVC_TYPE_PRIMARY,
            .uuid = &comble::SERVICE_UUID.u,
            .includes = nullptr,
            .characteristics = s_chrs,
        },
        {},   // terminator
    };
}   // namespace

// ──────────────────────────────────────────────────────────────

void BleSlaveManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    app_.getStrux().getSettingsManager().Register({ &passkeySetting_ });
    LoadOrCreatePasskey();

    // The advertised name is the device name, so an adapter is identified on a
    // host's screen by the same string it calls itself everywhere else.
    app_.getStrux().getSystemManager().GetDeviceName(advName_, sizeof(advName_));

    s_instance = this;

    // BLE only: hand the Classic-BT controller's RAM back to the heap. On a C3
    // there is no Classic radio at all, but the call is harmless and keeps this
    // identical to the S3 host, where it frees real memory.
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb = &BleSlaveManager::OnSyncStatic;
    ble_hs_cfg.reset_cb = &BleSlaveManager::OnResetStatic;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // We hold the passkey and the user types it on the host, so we are
    // DisplayOnly and the host is KeyboardOnly.
    //
    // sm_mitm = 1 is the line that matters. Without it the pair silently
    // degrades to "just works" and ANY host in range gets in — which would
    // defeat the entire point of a shared bag of adapters having owners. It is
    // also why sm_io_cap must not be NO_INPUT_NO_OUTPUT.
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "GATT service registration failed: %d", rc);
        return;
    }

    ble_svc_gap_device_name_set(advName_);

    nimble_port_freertos_init(&BleSlaveManager::HostTaskStatic);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized — name '%s', pairing code %06lu",
             advName_, (unsigned long)passkey_);
}

void BleSlaveManager::LoadOrCreatePasskey()
{
    passkey_ = passkeySetting_.Get();
    if (passkey_ != 0 && passkey_ <= 999999) return;

    // First boot (or a nonsense stored value). A 6-digit code, drawn from the
    // hardware RNG rather than from the MAC: the MAC is in every advertising
    // packet, so deriving the code from it would put the secret on the air.
    //
    // 000000 is excluded deliberately — it doubles as "unset" above, and it is
    // the one code somebody would guess first.
    passkey_ = 1 + (esp_random() % 999999);
    passkeySetting_.Set(passkey_);
    app_.getStrux().getSettingsManager().Save();
    ESP_LOGI(TAG, "Generated a new pairing code");
}

bool BleSlaveManager::IsBonded() const
{
    // Ask the store rather than keeping a flag: the bond list is the truth, it
    // lives in NVS, and a cached copy would go stale the moment a bond was
    // dropped by the stack itself.
    ble_addr_t peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int count = 0;
    if (ble_store_util_bonded_peers(peers, &count,
                                    sizeof(peers) / sizeof(peers[0])) != 0)
        return false;
    return count > 0;
}

void BleSlaveManager::ReleaseAllBonds()
{
    // Drop the link first: a host still connected on a bond that no longer
    // exists is precisely the ambiguity the three-state model exists to avoid.
    if (connected_)
        ble_gap_terminate(connHandle_, BLE_ERR_REM_USER_CONN_TERM);

    const int rc = ble_store_clear();
    ESP_LOGW(TAG, "Released all bonds (rc=%d) — this adapter is free to pair again", rc);

    // The advertised flags changed, so the packet has to be rewritten.
    StartAdvertising();
}

// ──────────────────────────────────────────────────────────────
// NimBLE plumbing
// ──────────────────────────────────────────────────────────────

void BleSlaveManager::HostTaskStatic(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void BleSlaveManager::OnSyncStatic()
{
    if (s_instance) s_instance->OnSync();
}

void BleSlaveManager::OnResetStatic(int reason)
{
    ESP_LOGW("BleSlave", "NimBLE host reset, reason=%d", reason);
}

void BleSlaveManager::OnSync()
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &ownAddrType_);
    StartAdvertising();
}

void BleSlaveManager::StartAdvertising()
{
    // Advertising packet: flags (3) + our 128-bit service UUID (18) + the
    // manufacturer block (6) = 27 of the 31 available bytes. The name does not
    // fit and goes in the scan response — so a host must scan ACTIVELY to see it.
    uint8_t mfg[comble::MFG_DATA_LEN];
    mfg[0] = (uint8_t)(comble::COMPANY_ID & 0xFF);
    mfg[1] = (uint8_t)(comble::COMPANY_ID >> 8);
    mfg[comble::MFG_OFF_TYPE] = comble::MFG_TYPE_SLAVE_V1;

    uint8_t flags = 0;
    if (IsBonded()) flags |= comble::FLAG_BONDED;
    if (connected_) flags |= comble::FLAG_CONNECTED;
    mfg[comble::MFG_OFF_FLAGS] = flags;

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = &comble::SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.mfg_data = mfg;
    fields.mfg_data_len = sizeof(mfg);

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = {};
    rsp.name = reinterpret_cast<const uint8_t *>(advName_);
    rsp.name_len = (uint8_t)strlen(advName_);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params advParams = {};
    advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
    advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(ownAddrType_, nullptr, BLE_HS_FOREVER, &advParams,
                           &BleSlaveManager::GapEventStatic, this);
    if (rc != 0 && rc != BLE_HS_EALREADY)
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    else
        ESP_LOGI(TAG, "Advertising as '%s'%s", advName_,
                 (flags & comble::FLAG_BONDED) ? " (owned)" : " (free)");
}

int BleSlaveManager::GapEventStatic(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    return s_instance ? s_instance->OnGapEvent(event) : 0;
}

int BleSlaveManager::OnGapEvent(struct ble_gap_event *event)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            connected_ = true;
            connHandle_ = event->connect.conn_handle;
            ESP_LOGI(TAG, "Host connected (handle %u)", connHandle_);
            // Not advertising any more while connected — but the flags changed,
            // so the next StartAdvertising picks the new ones up.
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed (status %d)", event->connect.status);
            StartAdvertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        connected_ = false;
        ESP_LOGI(TAG, "Host disconnected (reason %d)", event->disconnect.reason);
        // Straight back on the air. A disconnect is NOT a release: if a host
        // owns this adapter it must be able to find it again, and going quiet
        // would make a dropped link indistinguishable from a freed adapter.
        StartAdvertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        StartAdvertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption change, status %d", event->enc_change.status);
        if (event->enc_change.status == 0)
            ESP_LOGI(TAG, "Link is encrypted — this host now owns the adapter");
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        // We are DisplayOnly, so the only action we should ever be asked for is
        // to show the number. The host types it.
        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            struct ble_sm_io io = {};
            io.action = BLE_SM_IOACT_DISP;
            io.passkey = passkey_;
            const int rc = ble_sm_inject_io(event->passkey.conn_handle, &io);
            ESP_LOGI(TAG, "Pairing: show code %06lu (inject rc=%d)",
                     (unsigned long)passkey_, rc);
        }
        else
        {
            ESP_LOGW(TAG, "Unexpected passkey action %d",
                     event->passkey.params.action);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // The peer has lost its keys and wants to pair again. Delete the old
        // bond and let it retry — refusing would strand a host that had been
        // factory-reset, with no way back except releasing the adapter by hand.
        {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0)
                ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        ESP_LOGW(TAG, "Peer asked to pair again — old bond deleted");
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Host subscribed: notify=%d", event->subscribe.cur_notify);
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU negotiated: %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

int BleSlaveManager::GattAccessStatic(uint16_t, uint16_t attrHandle,
                                      struct ble_gatt_access_ctxt *ctxt, void *)
{
    // Nothing to do with the bytes yet — the UART transport is the next layer
    // and this is the seam it will attach to. Writes are logged so pairing can
    // be proven end to end: a write arriving at all means the link is encrypted
    // and authenticated, because the attribute refuses otherwise.
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attrHandle == s_outboundHandle)
    {
        const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        ESP_LOGI("BleSlave", "Host wrote %u byte(s) — link is authenticated", len);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

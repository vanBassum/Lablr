#include "BleManager.h"
#include "SettingsManager.h"
#include "UsbHostManager.h"
#include "esp_log.h"
#include <cstring>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// Nordic UART Service UUIDs (128-bit, little-endian byte order for NimBLE).
//   Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
//   RX     : 6E400002-...  (write  — phone -> bridge)
//   TX     : 6E400003-...  (notify — bridge -> phone)
static const ble_uuid128_t UUID_SVC = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_RX = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t UUID_TX = BLE_UUID128_INIT(
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

BleManager* BleManager::s_instance_ = nullptr;

BleManager::BleManager(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
    connHandle_ = BLE_HS_CONN_HANDLE_NONE;
}

void BleManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    s_instance_ = this;

    rxStream_ = xStreamBufferCreate(RX_STREAM_SIZE, 1);
    if (!rxStream_)
    {
        ESP_LOGE(TAG, "Failed to allocate RX stream buffer");
        return;
    }

    // Drain task: RX stream buffer -> USB host.
    forwardTask_.Init("ble_forward", 5, 4096);
    forwardTask_.SetHandler([this]() { ForwardTaskLoop(); });
    forwardTask_.Run();

    // NimBLE controller + host stack.
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(err));
        return;
    }

    ble_hs_cfg.sync_cb  = &BleManager::OnHostSync;
    ble_hs_cfg.reset_cb = &BleManager::OnHostReset;

    // GATT service table. Static-local + zero-initialised so the pointers
    // (incl. &txValHandle_) stay valid after Init() returns and we sidestep
    // -Werror=missing-field-initializers (the struct grows fields per IDF
    // version). Index [2]/[1] stay zeroed as the NimBLE list terminators.
    static struct ble_gatt_chr_def chars[3] = {};
    chars[0].uuid       = &UUID_RX.u;
    chars[0].access_cb  = &BleManager::OnRxWrite;
    chars[0].flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;
    chars[1].uuid       = &UUID_TX.u;
    chars[1].access_cb  = &BleManager::OnTxAccess;
    chars[1].flags      = BLE_GATT_CHR_F_NOTIFY;
    chars[1].val_handle = &txValHandle_;

    static struct ble_gatt_svc_def services[2] = {};
    services[0].type            = BLE_GATT_SVC_TYPE_PRIMARY;
    services[0].uuid            = &UUID_SVC.u;
    services[0].characteristics = chars;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    if (ble_gatts_count_cfg(services) != 0 || ble_gatts_add_svcs(services) != 0)
    {
        ESP_LOGE(TAG, "GATT service registration failed");
        return;
    }

    // Prefer a larger ATT MTU so each write carries a useful payload.
    ble_att_set_preferred_mtu(247);

    // Device name from settings (falls back to the default in SettingsDefs).
    char name[33] = "lablr-bridge";
    serviceProvider_.getSettingsManager().getString("device.name", name, sizeof(name));
    ble_svc_gap_device_name_set(name);

    nimble_port_freertos_init(&BleManager::HostTask);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (NimBLE peripheral '%s')", name);
}

// ── NimBLE host task ──────────────────────────────────────────

void BleManager::HostTask(void* /*param*/)
{
    nimble_port_run();              // blocks until nimble_port_stop()
    nimble_port_freertos_deinit();
}

void BleManager::OnHostReset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset, reason=%d", reason);
}

void BleManager::OnHostSync()
{
    ble_hs_util_ensure_addr(0);
    if (s_instance_)
        s_instance_->StartAdvertising();
}

void BleManager::StartAdvertising()
{
    uint8_t ownAddrType = 0;
    if (ble_hs_id_infer_auto(0, &ownAddrType) != 0)
    {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed");
        return;
    }

    // Advertisement: flags + complete name. Service UUID goes in the scan rsp.
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char* name = ble_svc_gap_device_name();
    fields.name = (uint8_t*)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_hs_adv_fields rsp = {};
    rsp.uuids128 = (ble_uuid128_t*)&UUID_SVC;
    rsp.num_uuids128 = 1;
    rsp.uuids128_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params advParams = {};
    advParams.conn_mode = BLE_GAP_CONN_MODE_UND;
    advParams.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(ownAddrType, nullptr, BLE_HS_FOREVER,
                               &advParams, &BleManager::GapEvent, nullptr);
    if (rc != 0)
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    else
        ESP_LOGI(TAG, "Advertising started");
}

// ── GAP events ────────────────────────────────────────────────

int BleManager::GapEvent(struct ble_gap_event* event, void* /*arg*/)
{
    auto* self = s_instance_;
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Phone connected");
            if (self) self->connHandle_ = event->connect.conn_handle;
        }
        else
        {
            ESP_LOGW(TAG, "Connection failed (%d), re-advertising", event->connect.status);
            if (self) self->StartAdvertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Phone disconnected (reason %d)", event->disconnect.reason);
        if (self)
        {
            self->connHandle_   = BLE_HS_CONN_HANDLE_NONE;
            self->txSubscribed_ = false;
            self->StartAdvertising();
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (self && event->subscribe.attr_handle == self->txValHandle_)
            self->txSubscribed_ = event->subscribe.cur_notify;
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU now %d", event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

// ── RX characteristic write -> stream buffer ──────────────────

int BleManager::OnRxWrite(uint16_t /*conn*/, uint16_t /*attr*/,
                          struct ble_gatt_access_ctxt* ctxt, void* /*arg*/)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
        return BLE_ATT_ERR_UNLIKELY;

    uint8_t buf[512];
    uint16_t outLen = 0;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &outLen);
    if (rc != 0)
        return BLE_ATT_ERR_INSUFFICIENT_RES;

    if (s_instance_ && outLen)
        s_instance_->HandleRx(buf, outLen);
    return 0;
}

int BleManager::OnTxAccess(uint16_t /*conn*/, uint16_t /*attr*/,
                           struct ble_gatt_access_ctxt* /*ctxt*/, void* /*arg*/)
{
    // TX is notify-only; reads/writes aren't expected.
    return 0;
}

void BleManager::HandleRx(const uint8_t* data, size_t len)
{
    if (!rxStream_) return;
    // Brief block applies backpressure if USB can't keep up.
    xStreamBufferSend(rxStream_, data, len, pdMS_TO_TICKS(1000));
}

// ── Stream buffer -> USB host ─────────────────────────────────

void BleManager::ForwardTaskLoop()
{
    uint8_t chunk[DRAIN_CHUNK];
    auto& usb = serviceProvider_.getUsbHostManager();

    while (true)
    {
        size_t n = xStreamBufferReceive(rxStream_, chunk, sizeof(chunk), portMAX_DELAY);
        if (n == 0)
            continue;

        if (!usb.IsPrinterReady())
        {
            ESP_LOGW(TAG, "Dropping %u bytes — no printer attached", (unsigned)n);
            NotifyStatus("error:no-printer");
            continue;
        }

        int sent = usb.SendToPrinter(chunk, n);
        if (sent < 0)
            NotifyStatus("error:usb-write");
    }
}

void BleManager::NotifyStatus(const char* text)
{
    if (connHandle_ == BLE_HS_CONN_HANDLE_NONE || !txSubscribed_ || !txValHandle_)
        return;

    struct os_mbuf* om = ble_hs_mbuf_from_flat(text, strlen(text));
    if (!om) return;
    ble_gatts_notify_custom(connHandle_, txValHandle_, om);
}

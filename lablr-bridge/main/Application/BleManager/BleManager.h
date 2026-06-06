#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// BleManager — the phone-facing side of the bridge.
//
//   phone --BLE(GATT)--> BleManager --stream buffer--> UsbHostManager --> Dymo
//
// Exposes a Nordic-UART-style GATT service so a Web Bluetooth client (the
// Lablr PWA on Android) can stream a print payload:
//   • RX characteristic (write / write-no-response): phone -> bridge bytes
//   • TX characteristic (notify):                    bridge -> phone status
//
// Incoming RX bytes are pushed into a FreeRTOS stream buffer and drained by a
// dedicated task into UsbHostManager::SendToPrinter(). This keeps the NimBLE
// host task free of the blocking USB transfer and gives natural backpressure.
// ──────────────────────────────────────────────────────────────

class BleManager
{
    static constexpr const char* TAG = "BleManager";

    static constexpr size_t RX_STREAM_SIZE = 16 * 1024;  // buffered print bytes
    static constexpr size_t DRAIN_CHUNK    = 2048;

public:
    explicit BleManager(ServiceProvider& serviceProvider);

    BleManager(const BleManager&) = delete;
    BleManager& operator=(const BleManager&) = delete;
    BleManager(BleManager&&) = delete;
    BleManager& operator=(BleManager&&) = delete;

    void Init();

    /// Notify the connected phone with a status line (if subscribed).
    void NotifyStatus(const char* text);

private:
    ServiceProvider& serviceProvider_;
    InitState initState_;

    // RX byte pipe -> USB forwarder
    StreamBufferHandle_t rxStream_ = nullptr;
    Task forwardTask_;
    void ForwardTaskLoop();

    // Connection / subscription state (owned by the NimBLE host task)
    uint16_t connHandle_   = 0;     // BLE_HS_CONN_HANDLE_NONE when not connected
    uint16_t txValHandle_  = 0;     // value handle of the TX (notify) characteristic
    bool     txSubscribed_ = false;

    // NimBLE plumbing
    static void OnHostSync();
    static void OnHostReset(int reason);
    static void HostTask(void* param);
    void StartAdvertising();

    // GATT access + GAP event trampolines
    static int GapEvent(struct ble_gap_event* event, void* arg);
    static int OnRxWrite(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt* ctxt, void* arg);
    // Notify-only TX characteristic: no read/write semantics, just a stub.
    static int OnTxAccess(uint16_t conn, uint16_t attr,
                          struct ble_gatt_access_ctxt* ctxt, void* arg);

    // Push received bytes into the stream buffer (called from host task).
    void HandleRx(const uint8_t* data, size_t len);

    static BleManager* s_instance_;
};

#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Task.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "esp_websocket_client.h"
#include "esp_event.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// CloudLinkManager — the backend-facing side of the bridge (Option C).
//
//   PWA → lablr-api → (wss) → CloudLinkManager → UsbHostManager → Dymo
//
// Dials OUT to the hosted backend over a persistent secure WebSocket, so the
// printer is reachable from anywhere without LAN access or port forwarding.
// On connect it sends a JSON `hello` (agent id/name/printerReady); the backend
// relays rendered print jobs back as binary frames, which we stream straight
// to the USB printer. The bridge stays a dumb pipe (Preview = Print).
// ──────────────────────────────────────────────────────────────

class CloudLinkManager
{
    static constexpr const char* TAG = "CloudLinkManager";

    static constexpr size_t RX_STREAM_SIZE = 16 * 1024;  // buffered print bytes
    static constexpr size_t DRAIN_CHUNK    = 2048;
    static constexpr int    STATUS_PERIOD_MS = 15000;

public:
    explicit CloudLinkManager(ServiceProvider& serviceProvider);

    CloudLinkManager(const CloudLinkManager&) = delete;
    CloudLinkManager& operator=(const CloudLinkManager&) = delete;
    CloudLinkManager(CloudLinkManager&&) = delete;
    CloudLinkManager& operator=(CloudLinkManager&&) = delete;

    void Init();

private:
    ServiceProvider& serviceProvider_;
    InitState initState_;

    esp_websocket_client_handle_t client_ = nullptr;
    char agentId_[32] = {};

    // Incoming print bytes (from backend) -> USB, drained on a dedicated task
    // so the websocket task never blocks on a USB transfer.
    StreamBufferHandle_t rxStream_ = nullptr;
    Task forwardTask_;
    Task statusTask_;

    void ForwardTaskLoop();
    void StatusTaskLoop();
    void SendHello();
    void SendStatus();

    static void WsEventHandler(void* arg, esp_event_base_t base, int32_t id, void* data);
};

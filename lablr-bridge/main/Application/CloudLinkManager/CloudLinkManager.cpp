#include "CloudLinkManager.h"
#include "SettingsManager.h"
#include "UsbHostManager.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_crt_bundle.h"
#include <cstdio>
#include <cstring>

CloudLinkManager::CloudLinkManager(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void CloudLinkManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    auto& settings = serviceProvider_.getSettingsManager();
    if (!settings.getBool("cloud.enabled", false))
    {
        ESP_LOGI(TAG, "Cloud link disabled (set cloud.enabled)");
        init.SetReady();
        return;
    }

    char url[160] = {};
    if (!settings.getString("cloud.url", url, sizeof(url)) || url[0] == '\0')
    {
        ESP_LOGW(TAG, "cloud.enabled but no cloud.url set");
        init.SetReady();
        return;
    }

    char token[96] = {};
    settings.getString("cloud.token", token, sizeof(token));

    char name[33] = "lablr-bridge";
    settings.getString("device.name", name, sizeof(name));

    // Stable agent id from the factory MAC (unique per device).
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(agentId_, sizeof(agentId_), "lablr-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Append the shared token as a query param (backend also accepts Bearer).
    char fullUri[280] = {};
    if (token[0] != '\0')
        snprintf(fullUri, sizeof(fullUri), "%s%ctoken=%s",
                 url, strchr(url, '?') ? '&' : '?', token);
    else
        snprintf(fullUri, sizeof(fullUri), "%s", url);

    rxStream_ = xStreamBufferCreate(RX_STREAM_SIZE, 1);
    if (!rxStream_)
    {
        ESP_LOGE(TAG, "Failed to allocate RX stream buffer");
        return;
    }

    forwardTask_.Init("cloud_forward", 5, 4096);
    forwardTask_.SetHandler([this]() { ForwardTaskLoop(); });
    forwardTask_.Run();

    statusTask_.Init("cloud_status", 4, 3072);
    statusTask_.SetHandler([this]() { StatusTaskLoop(); });
    statusTask_.Run();

    esp_websocket_client_config_t cfg = {};
    cfg.uri = fullUri;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;   // trust public CAs for wss://
    cfg.reconnect_timeout_ms = 5000;
    cfg.network_timeout_ms = 10000;

    client_ = esp_websocket_client_init(&cfg);
    if (!client_)
    {
        ESP_LOGE(TAG, "websocket client init failed");
        return;
    }
    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY,
                                  &CloudLinkManager::WsEventHandler, this);
    esp_websocket_client_start(client_);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (agent %s -> %s)", agentId_, url);
}

// ── WebSocket events ──────────────────────────────────────────

void CloudLinkManager::WsEventHandler(void* arg, esp_event_base_t /*base*/,
                                      int32_t id, void* data)
{
    auto* self = static_cast<CloudLinkManager*>(arg);
    auto* ev = static_cast<esp_websocket_event_data_t*>(data);

    switch (id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to backend");
        self->SendHello();
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected from backend");
        break;

    case WEBSOCKET_EVENT_DATA:
        // Binary (0x02) or continuation (0x00) frames carry print bytes; stream
        // them to USB. Ignore text/ping/pong/close control frames.
        if ((ev->op_code == 0x02 || ev->op_code == 0x00) && ev->data_len > 0 && self->rxStream_)
            xStreamBufferSend(self->rxStream_, ev->data_ptr, ev->data_len, pdMS_TO_TICKS(1000));
        break;

    default:
        break;
    }
}

// ── Hello / status ────────────────────────────────────────────

void CloudLinkManager::SendHello()
{
    char name[33] = "lablr-bridge";
    serviceProvider_.getSettingsManager().getString("device.name", name, sizeof(name));
    const bool ready = serviceProvider_.getUsbHostManager().IsPrinterReady();

    char msg[160];
    int n = snprintf(msg, sizeof(msg),
        "{\"type\":\"hello\",\"id\":\"%s\",\"name\":\"%s\",\"printerReady\":%s}",
        agentId_, name, ready ? "true" : "false");
    esp_websocket_client_send_text(client_, msg, n, pdMS_TO_TICKS(2000));
}

void CloudLinkManager::SendStatus()
{
    if (!client_ || !esp_websocket_client_is_connected(client_)) return;
    const bool ready = serviceProvider_.getUsbHostManager().IsPrinterReady();

    char msg[80];
    int n = snprintf(msg, sizeof(msg),
        "{\"type\":\"status\",\"printerReady\":%s}", ready ? "true" : "false");
    esp_websocket_client_send_text(client_, msg, n, pdMS_TO_TICKS(2000));
}

void CloudLinkManager::StatusTaskLoop()
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
        SendStatus();
    }
}

// ── Backend bytes -> USB ──────────────────────────────────────

void CloudLinkManager::ForwardTaskLoop()
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
            continue;
        }
        usb.SendToPrinter(chunk, n);
    }
}

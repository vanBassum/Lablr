#include "UsbHostManager.h"
#include "usb/usb_helpers.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include <cstring>

// Size of the reusable OUT transfer buffer. Print payloads are streamed in
// chunks of this size; it must be a multiple of the endpoint max packet size,
// so we round down once the real MPS is known.
static constexpr size_t OUT_XFER_BUF = 4096;

UsbHostManager::UsbHostManager(ServiceProvider& serviceProvider)
    : serviceProvider_(serviceProvider)
{
}

void UsbHostManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    // 1. Install the USB Host Library. intr_flags level 1 is the common default.
    usb_host_config_t hostConfig = {};
    hostConfig.skip_phy_setup = false;
    hostConfig.intr_flags = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&hostConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        return;
    }

    // 2. Register our client (receives device connect/disconnect events).
    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous = false;
    clientConfig.max_num_event_msg = 5;
    clientConfig.async.client_event_callback = &UsbHostManager::ClientEventCallback;
    clientConfig.async.callback_arg = this;
    err = usb_host_client_register(&clientConfig, &clientHdl_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
        return;
    }

    // 3. Pre-allocate the reusable OUT transfer.
    err = usb_host_transfer_alloc(OUT_XFER_BUF, 0, &outXfer_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_transfer_alloc failed: %s", esp_err_to_name(err));
        return;
    }
    outXfer_->callback = &UsbHostManager::OutTransferCallback;
    outXfer_->context  = this;

    // 4. Spin up the two event pumps. The library daemon must run to enumerate
    //    devices; the client pump dispatches our connect/disconnect callbacks.
    daemonTask_.Init("usb_daemon", 4, 4096);
    daemonTask_.SetHandler([this]() { DaemonTaskLoop(); });
    daemonTask_.Run();

    clientTask_.Init("usb_client", 4, 4096);
    clientTask_.SetHandler([this]() { ClientTaskLoop(); });
    clientTask_.Run();

    init.SetReady();
    ESP_LOGI(TAG, "Initialized (USB host, waiting for printer)");
}

// ── Event pumps ───────────────────────────────────────────────

void UsbHostManager::DaemonTaskLoop()
{
    while (true)
    {
        uint32_t eventFlags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);

        if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
        {
            // No clients left — nothing to do here for now.
        }
        if (eventFlags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
        {
            // All devices freed.
        }
    }
}

void UsbHostManager::ClientTaskLoop()
{
    while (true)
    {
        usb_host_client_handle_events(clientHdl_, portMAX_DELAY);
    }
}

// ── Device connect / disconnect ───────────────────────────────

void UsbHostManager::ClientEventCallback(const usb_host_client_event_msg_t* msg, void* arg)
{
    auto* self = static_cast<UsbHostManager*>(arg);
    switch (msg->event)
    {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGI(TAG, "USB device connected (addr %d)", msg->new_dev.address);
        if (!self->devHdl_)
            self->OpenAndClaim(msg->new_dev.address);
        break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGW(TAG, "USB device disconnected");
        self->CloseDevice();
        break;

    default:
        break;
    }
}

bool UsbHostManager::OpenAndClaim(uint8_t devAddr)
{
    esp_err_t err = usb_host_device_open(clientHdl_, devAddr, &devHdl_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "device_open failed: %s", esp_err_to_name(err));
        devHdl_ = nullptr;
        return false;
    }

    const usb_config_desc_t* cfg = nullptr;
    err = usb_host_get_active_config_descriptor(devHdl_, &cfg);
    if (err != ESP_OK || !cfg)
    {
        ESP_LOGE(TAG, "get_active_config_descriptor failed: %s", esp_err_to_name(err));
        CloseDevice();
        return false;
    }

    // Walk every interface, prefer a printer-class one, and grab its bulk
    // OUT (and optional bulk IN) endpoints.
    bool found = false;
    for (int ifn = 0; ifn < cfg->bNumInterfaces && !found; ++ifn)
    {
        int offset = 0;
        const usb_intf_desc_t* intf =
            usb_parse_interface_descriptor(cfg, ifn, 0, &offset);
        if (!intf)
            continue;

        // Skip non-printer interfaces when the device exposes several. A pure
        // single-interface device still works because we fall through below.
        if (cfg->bNumInterfaces > 1 && intf->bInterfaceClass != USB_CLASS_PRINTER)
            continue;

        uint8_t epOut = 0, epIn = 0;
        uint16_t mpsOut = 0;
        for (int e = 0; e < intf->bNumEndpoints; ++e)
        {
            int epOffset = offset;
            const usb_ep_desc_t* ep =
                usb_parse_endpoint_descriptor_by_index(intf, e, cfg->wTotalLength, &epOffset);
            if (!ep)
                continue;
            if (USB_EP_DESC_GET_XFERTYPE(ep) != USB_BM_ATTRIBUTES_XFER_BULK)
                continue;

            if (USB_EP_DESC_GET_EP_DIR(ep))   // 1 = IN
            {
                epIn = ep->bEndpointAddress;
            }
            else                               // 0 = OUT
            {
                epOut  = ep->bEndpointAddress;
                mpsOut = USB_EP_DESC_GET_MPS(ep);
            }
        }

        if (epOut)
        {
            err = usb_host_interface_claim(clientHdl_, devHdl_, intf->bInterfaceNumber, 0);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "interface_claim(%d) failed: %s",
                         intf->bInterfaceNumber, esp_err_to_name(err));
                continue;
            }
            ifaceNum_  = intf->bInterfaceNumber;
            epOutAddr_ = epOut;
            epInAddr_  = epIn;
            epOutMps_  = mpsOut ? mpsOut : 64;
            found = true;
        }
    }

    if (!found)
    {
        ESP_LOGE(TAG, "No bulk OUT endpoint found — not a printer?");
        CloseDevice();
        return false;
    }

    printerReady_ = true;
    ESP_LOGI(TAG, "Printer ready: iface %u, EP OUT 0x%02x (mps %u), EP IN 0x%02x",
             ifaceNum_, epOutAddr_, epOutMps_, epInAddr_);
    return true;
}

void UsbHostManager::CloseDevice()
{
    printerReady_ = false;
    if (devHdl_)
    {
        usb_host_interface_release(clientHdl_, devHdl_, ifaceNum_);
        usb_host_device_close(clientHdl_, devHdl_);
        devHdl_ = nullptr;
    }
    epOutAddr_ = 0;
    epInAddr_  = 0;
}

// ── Bulk OUT transfer ─────────────────────────────────────────

void UsbHostManager::OutTransferCallback(usb_transfer_t* transfer)
{
    auto* self = static_cast<UsbHostManager*>(transfer->context);
    self->xferStatus_ = transfer->status;
    self->xferDone_.Give();
}

int UsbHostManager::SendToPrinter(const uint8_t* data, size_t len, uint32_t timeoutMs)
{
    if (!printerReady_ || !outXfer_ || !data || len == 0)
        return -1;

    LOCK(sendMutex_);

    // The OUT transfer buffer is bounded; chunk on a max-packet-size boundary
    // so the printer never sees a short packet mid-stream.
    const size_t chunkMax = (OUT_XFER_BUF / epOutMps_) * epOutMps_;
    size_t sent = 0;

    while (sent < len)
    {
        const size_t chunk = (len - sent) < chunkMax ? (len - sent) : chunkMax;
        memcpy(outXfer_->data_buffer, data + sent, chunk);
        outXfer_->num_bytes        = chunk;
        outXfer_->device_handle    = devHdl_;
        outXfer_->bEndpointAddress = epOutAddr_;
        outXfer_->timeout_ms       = timeoutMs;

        xferStatus_ = -1;
        esp_err_t err = usb_host_transfer_submit(outXfer_);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "transfer_submit failed: %s", esp_err_to_name(err));
            return -1;
        }

        if (!xferDone_.Take(pdMS_TO_TICKS(timeoutMs + 500)))
        {
            ESP_LOGE(TAG, "transfer timed out");
            return -1;
        }
        if (xferStatus_ != USB_TRANSFER_STATUS_COMPLETED)
        {
            ESP_LOGE(TAG, "transfer status %d", xferStatus_);
            return -1;
        }

        sent += outXfer_->actual_num_bytes;
        if (outXfer_->actual_num_bytes != (int)chunk)
        {
            // Short write — printer stalled or buffer full. Stop here.
            ESP_LOGW(TAG, "short write: %d/%u", outXfer_->actual_num_bytes, (unsigned)chunk);
            break;
        }
    }

    return (int)sent;
}

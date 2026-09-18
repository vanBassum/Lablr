#include "UsbHostManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "usb/usb_helpers.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include <cstring>

// Printer class (USB Device Class Definition for Printing Devices 1.1).
static constexpr uint8_t PRINTER_CLASS = 0x07;

// Class-specific requests on a printer interface. bmRequestType for the two
// reads is 0xA1: device-to-host | class | recipient interface.
static constexpr uint8_t REQ_GET_DEVICE_ID   = 0x00;
static constexpr uint8_t REQ_GET_PORT_STATUS = 0x01;
static constexpr uint8_t RT_CLASS_IFACE_IN   = 0xA1;

UsbHostManager::UsbHostManager(AppProvider& app)
    : app_(app)
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

    usb_host_config_t hostConfig = {};
    hostConfig.skip_phy_setup = false;
    hostConfig.intr_flags     = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&hostConfig);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_install failed: %s", esp_err_to_name(err));
        return;
    }

    usb_host_client_config_t clientConfig = {};
    clientConfig.is_synchronous              = false;
    clientConfig.max_num_event_msg            = 5;
    clientConfig.async.client_event_callback  = &UsbHostManager::ClientEventCallback;
    clientConfig.async.callback_arg           = this;
    err = usb_host_client_register(&clientConfig, &clientHdl_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_client_register failed: %s", esp_err_to_name(err));
        return;
    }

    // One OUT transfer and one IN transfer, allocated once. The USB host library
    // needs DMA-capable memory for these and allocates it itself, which is
    // another reason not to hand it a buffer of ours.
    if (usb_host_transfer_alloc(OUT_XFER_BUF, 0, &outXfer_) != ESP_OK ||
        usb_host_transfer_alloc(IN_XFER_BUF, 0, &inXfer_) != ESP_OK)
    {
        ESP_LOGE(TAG, "usb_host_transfer_alloc failed");
        return;
    }
    outXfer_->callback = &UsbHostManager::TransferCallback;
    outXfer_->context  = this;
    inXfer_->callback  = &UsbHostManager::TransferCallback;
    inXfer_->context   = this;

    daemonTask_.Init("usb_daemon", 4, 4096);
    daemonTask_.SetHandler([this]() { DaemonTaskLoop(); });
    daemonTask_.Run();

    clientTask_.Init("usb_client", 4, 4096);
    clientTask_.SetHandler([this]() { ClientTaskLoop(); });
    clientTask_.Run();

    app_.getStrux().getCommandManager().Register(this, commands_);

    init.SetReady();
    ESP_LOGI(TAG, "USB host up on the OTG port - waiting for a device");
}

// ──────────────────────────────────────────────────────────────
// The two mandatory event pumps
// ──────────────────────────────────────────────────────────────

void UsbHostManager::DaemonTaskLoop()
{
    while (true)
    {
        uint32_t eventFlags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &eventFlags);
        // NO_CLIENTS and ALL_FREE matter only to a host that wants to uninstall
        // the library. This one runs for the lifetime of the device.
    }
}

void UsbHostManager::ClientTaskLoop()
{
    while (true)
        usb_host_client_handle_events(clientHdl_, portMAX_DELAY);
}

// ──────────────────────────────────────────────────────────────
// Connect / disconnect
// ──────────────────────────────────────────────────────────────

void UsbHostManager::ClientEventCallback(const usb_host_client_event_msg_t* msg, void* arg)
{
    auto* self = static_cast<UsbHostManager*>(arg);
    switch (msg->event)
    {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        ESP_LOGI(TAG, "device connected at address %d", msg->new_dev.address);
        if (!self->devHdl_)
            self->OpenAndClaim(msg->new_dev.address);
        break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        ESP_LOGW(TAG, "device disconnected");
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
    devAddr_ = devAddr;

    LogDeviceDescriptor();

    const usb_config_desc_t* cfg = nullptr;
    err = usb_host_get_active_config_descriptor(devHdl_, &cfg);
    if (err != ESP_OK || !cfg)
    {
        ESP_LOGE(TAG, "get_active_config_descriptor failed: %s", esp_err_to_name(err));
        CloseDevice();
        return false;
    }

    // Describe everything BEFORE choosing anything. When a printer turns out to
    // speak a dialect we did not expect, this log is the evidence.
    LogConfiguration(cfg);

    // Prefer a printer-class interface; fall back to any interface with a bulk
    // OUT endpoint, because a vendor-specific printer is a real thing and
    // refusing it would be refusing to look.
    bool found = false;
    for (int pass = 0; pass < 2 && !found; ++pass)
    {
        const bool printerOnly = (pass == 0);

        for (int ifn = 0; ifn < cfg->bNumInterfaces && !found; ++ifn)
        {
            int offset = 0;
            const usb_intf_desc_t* intf =
                usb_parse_interface_descriptor(cfg, ifn, 0, &offset);
            if (!intf) continue;
            if (printerOnly && intf->bInterfaceClass != PRINTER_CLASS) continue;

            uint8_t  epOut = 0, epIn = 0;
            uint16_t mpsOut = 0, mpsIn = 0;
            EndpointInfo seen[MAX_ENDPOINTS_LOGGED] = {};
            size_t seenCount = 0;

            for (int e = 0; e < intf->bNumEndpoints; ++e)
            {
                int epOffset = offset;
                const usb_ep_desc_t* ep =
                    usb_parse_endpoint_descriptor_by_index(intf, e, cfg->wTotalLength, &epOffset);
                if (!ep) continue;

                if (seenCount < MAX_ENDPOINTS_LOGGED)
                    seen[seenCount++] = { ep->bEndpointAddress, ep->bmAttributes,
                                          static_cast<uint16_t>(USB_EP_DESC_GET_MPS(ep)),
                                          ep->bInterval };

                if (USB_EP_DESC_GET_XFERTYPE(ep) != USB_BM_ATTRIBUTES_XFER_BULK) continue;

                if (USB_EP_DESC_GET_EP_DIR(ep)) { epIn  = ep->bEndpointAddress; mpsIn  = USB_EP_DESC_GET_MPS(ep); }
                else                            { epOut = ep->bEndpointAddress; mpsOut = USB_EP_DESC_GET_MPS(ep); }
            }

            if (!epOut) continue;

            err = usb_host_interface_claim(clientHdl_, devHdl_,
                                           intf->bInterfaceNumber, intf->bAlternateSetting);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "interface_claim(%u) failed: %s",
                         intf->bInterfaceNumber, esp_err_to_name(err));
                continue;
            }

            ifaceNum_      = intf->bInterfaceNumber;
            altSet_        = intf->bAlternateSetting;
            ifaceClass_    = intf->bInterfaceClass;
            ifaceSubClass_ = intf->bInterfaceSubClass;
            ifaceProto_    = intf->bInterfaceProtocol;
            epOutAddr_     = epOut;
            epInAddr_      = epIn;
            epOutMps_      = mpsOut ? mpsOut : 64;
            epInMps_       = mpsIn ? mpsIn : 64;
            printerClass_  = (intf->bInterfaceClass == PRINTER_CLASS);
            endpointCount_ = seenCount;
            memcpy(endpoints_, seen, sizeof(seen));
            found = true;
        }
    }

    if (!found)
    {
        ESP_LOGE(TAG, "no interface with a bulk OUT endpoint - this is not a printer");
        CloseDevice();
        return false;
    }

    ready_ = true;
    ESP_LOGI(TAG, "claimed interface %u alt %u (class %02x/%02x/%02x), "
                  "EP OUT 0x%02x mps %u, EP IN 0x%02x mps %u",
             ifaceNum_, altSet_, ifaceClass_, ifaceSubClass_, ifaceProto_,
             epOutAddr_, epOutMps_, epInAddr_, epInMps_);

    // Ask the printer to name itself. This runs on the client-event task, which
    // must not block - but a control transfer on the default pipe is dispatched
    // by the same pump we are inside, so waiting here would deadlock. Log a
    // pointer to the command instead and let a caller ask on its own task.
    if (printerClass_)
        ESP_LOGI(TAG, "printer-class interface - run 'usb status' for its IEEE-1284 device ID");

    return true;
}

void UsbHostManager::CloseDevice()
{
    ready_        = false;
    printerClass_ = false;
    if (devHdl_)
    {
        usb_host_interface_release(clientHdl_, devHdl_, ifaceNum_);
        usb_host_device_close(clientHdl_, devHdl_);
        devHdl_ = nullptr;
    }
    vid_ = pid_ = bcdDevice_ = 0;
    devAddr_ = 0;
    epOutAddr_ = epInAddr_ = 0;
    endpointCount_ = 0;
    manufacturer_[0] = product_[0] = serial_[0] = '\0';
}

// ──────────────────────────────────────────────────────────────
// Descriptor reading and logging
// ──────────────────────────────────────────────────────────────

void UsbHostManager::CopyStringDesc(const usb_str_desc_t* desc, char* out, size_t outSize)
{
    out[0] = '\0';
    if (!desc || outSize == 0) return;

    // USB string descriptors are UTF-16LE. Labels and model names are ASCII in
    // practice, and a literal on this wire has to be ASCII anyway (see the
    // execution-charset note in CLAUDE.md), so anything above 0x7e becomes '?'
    // rather than half a code point.
    const size_t chars = (desc->bLength >= 2) ? (desc->bLength - 2) / 2 : 0;
    size_t n = 0;
    for (size_t i = 0; i < chars && n + 1 < outSize; ++i)
    {
        const uint16_t c = desc->wData[i];
        out[n++] = (c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '?';
    }
    out[n] = '\0';
}

void UsbHostManager::LogDeviceDescriptor()
{
    const usb_device_desc_t* dev = nullptr;
    if (usb_host_get_device_descriptor(devHdl_, &dev) != ESP_OK || !dev)
    {
        ESP_LOGW(TAG, "could not read the device descriptor");
        return;
    }

    vid_       = dev->idVendor;
    pid_       = dev->idProduct;
    bcdDevice_ = dev->bcdDevice;

    usb_device_info_t info = {};
    if (usb_host_device_info(devHdl_, &info) == ESP_OK)
    {
        CopyStringDesc(info.str_desc_manufacturer, manufacturer_, sizeof(manufacturer_));
        CopyStringDesc(info.str_desc_product,      product_,      sizeof(product_));
        CopyStringDesc(info.str_desc_serial_num,   serial_,       sizeof(serial_));
    }

    ESP_LOGI(TAG, "device %04x:%04x rev %04x  class %02x/%02x/%02x  ep0 mps %u  configs %u",
             vid_, pid_, bcdDevice_,
             dev->bDeviceClass, dev->bDeviceSubClass, dev->bDeviceProtocol,
             dev->bMaxPacketSize0, dev->bNumConfigurations);
    ESP_LOGI(TAG, "  manufacturer '%s'  product '%s'  serial '%s'",
             manufacturer_, product_, serial_);
}

const char* UsbHostManager::XferTypeName(uint8_t attributes)
{
    switch (attributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK)
    {
    case USB_BM_ATTRIBUTES_XFER_CONTROL: return "control";
    case USB_BM_ATTRIBUTES_XFER_ISOC:    return "isochronous";
    case USB_BM_ATTRIBUTES_XFER_BULK:    return "bulk";
    case USB_BM_ATTRIBUTES_XFER_INT:     return "interrupt";
    default:                             return "unknown";
    }
}

void UsbHostManager::LogConfiguration(const usb_config_desc_t* cfg)
{
    ESP_LOGI(TAG, "configuration %u: %u interfaces, %u mA, attributes %02x",
             cfg->bConfigurationValue, cfg->bNumInterfaces,
             cfg->bMaxPower * 2, cfg->bmAttributes);

    for (int ifn = 0; ifn < cfg->bNumInterfaces; ++ifn)
    {
        int offset = 0;
        const usb_intf_desc_t* intf = usb_parse_interface_descriptor(cfg, ifn, 0, &offset);
        if (!intf) continue;

        ESP_LOGI(TAG, "  interface %u alt %u: class %02x sub %02x proto %02x, %u endpoints",
                 intf->bInterfaceNumber, intf->bAlternateSetting,
                 intf->bInterfaceClass, intf->bInterfaceSubClass,
                 intf->bInterfaceProtocol, intf->bNumEndpoints);

        for (int e = 0; e < intf->bNumEndpoints; ++e)
        {
            int epOffset = offset;
            const usb_ep_desc_t* ep =
                usb_parse_endpoint_descriptor_by_index(intf, e, cfg->wTotalLength, &epOffset);
            if (!ep) continue;
            ESP_LOGI(TAG, "    endpoint 0x%02x %-3s %-11s mps %u interval %u",
                     ep->bEndpointAddress,
                     USB_EP_DESC_GET_EP_DIR(ep) ? "IN" : "OUT",
                     XferTypeName(ep->bmAttributes),
                     USB_EP_DESC_GET_MPS(ep), ep->bInterval);
        }
    }
}

// ──────────────────────────────────────────────────────────────
// Transfers
// ──────────────────────────────────────────────────────────────

void UsbHostManager::TransferCallback(usb_transfer_t* transfer)
{
    auto* self = static_cast<UsbHostManager*>(transfer->context);
    self->xferStatus_ = transfer->status;
    self->xferDone_.Give();
}

int UsbHostManager::Send(const uint8_t* data, size_t len, uint32_t timeoutMs)
{
    if (!ready_ || !outXfer_ || !data || len == 0) return -1;

    LOCK(sendMutex_);

    // Chunk on a max-packet-size boundary. A bulk transfer that is not a
    // multiple of MPS ends with a short packet, which the printer reads as
    // "that was the end" - fine for the last chunk, a truncated job anywhere
    // else.
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
        if (usb_host_transfer_submit(outXfer_) != ESP_OK)
        {
            ESP_LOGE(TAG, "transfer_submit failed at offset %u", (unsigned)sent);
            return -1;
        }

        if (!xferDone_.Take(pdMS_TO_TICKS(timeoutMs + 500)))
        {
            ESP_LOGE(TAG, "transfer timed out at offset %u", (unsigned)sent);
            return -1;
        }
        if (xferStatus_ != USB_TRANSFER_STATUS_COMPLETED)
        {
            ESP_LOGE(TAG, "transfer status %d at offset %u", xferStatus_, (unsigned)sent);
            return -1;
        }

        sent += outXfer_->actual_num_bytes;
        if (outXfer_->actual_num_bytes != static_cast<int>(chunk))
        {
            ESP_LOGW(TAG, "short write %d/%u - printer stopped accepting",
                     outXfer_->actual_num_bytes, (unsigned)chunk);
            break;
        }
    }

    return static_cast<int>(sent);
}

int UsbHostManager::ControlTransfer(uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint8_t* data, uint16_t wLength, uint32_t timeoutMs)
{
    if (!devHdl_) return -1;

    usb_transfer_t* xfer = nullptr;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &xfer) != ESP_OK)
        return -1;

    auto* setup = reinterpret_cast<usb_setup_packet_t*>(xfer->data_buffer);
    setup->bmRequestType = bmRequestType;
    setup->bRequest      = bRequest;
    setup->wValue        = wValue;
    setup->wIndex        = wIndex;
    setup->wLength       = wLength;

    xfer->device_handle    = devHdl_;
    xfer->bEndpointAddress = 0;
    xfer->num_bytes        = sizeof(usb_setup_packet_t) + wLength;
    xfer->timeout_ms       = timeoutMs;
    xfer->callback         = &UsbHostManager::TransferCallback;
    xfer->context          = this;

    xferStatus_ = -1;
    int received = -1;
    if (usb_host_transfer_submit_control(clientHdl_, xfer) == ESP_OK &&
        xferDone_.Take(pdMS_TO_TICKS(timeoutMs + 500)) &&
        xferStatus_ == USB_TRANSFER_STATUS_COMPLETED)
    {
        received = xfer->actual_num_bytes - static_cast<int>(sizeof(usb_setup_packet_t));
        if (received < 0) received = 0;
        if (data && received > 0)
            memcpy(data, xfer->data_buffer + sizeof(usb_setup_packet_t),
                   (received < wLength) ? received : wLength);
    }
    else
    {
        ESP_LOGW(TAG, "control request %02x/%02x failed (status %d)",
                 bmRequestType, bRequest, xferStatus_);
    }

    usb_host_transfer_free(xfer);
    return received;
}

bool UsbHostManager::GetPortStatus(uint8_t& statusOut)
{
    if (!ready_ || !printerClass_) return false;
    LOCK(sendMutex_);
    uint8_t s = 0;
    const int n = ControlTransfer(RT_CLASS_IFACE_IN, REQ_GET_PORT_STATUS,
                                  0, ifaceNum_, &s, 1, 1000);
    if (n < 1) return false;
    statusOut = s;
    return true;
}

bool UsbHostManager::GetDeviceId(char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!ready_ || !printerClass_) return false;

    LOCK(sendMutex_);

    uint8_t buf[DEVICE_ID_MAX] = {};
    // wValue is the config index, wIndex is (interface << 8) | alternate setting.
    const uint16_t wIndex = static_cast<uint16_t>((ifaceNum_ << 8) | altSet_);
    const int n = ControlTransfer(RT_CLASS_IFACE_IN, REQ_GET_DEVICE_ID,
                                  0, wIndex, buf, sizeof(buf), 1000);
    if (n < 3) return false;

    // The reply is a big-endian 16-bit length (which INCLUDES the two length
    // bytes) followed by the ASCII ID.
    size_t declared = (static_cast<size_t>(buf[0]) << 8) | buf[1];
    if (declared < 2) return false;
    declared -= 2;
    if (declared > static_cast<size_t>(n) - 2) declared = static_cast<size_t>(n) - 2;

    size_t copy = (declared < outSize - 1) ? declared : outSize - 1;
    memcpy(out, buf + 2, copy);
    out[copy] = '\0';
    return true;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError UsbHostManager::Cmd_UsbStatus(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("attached", ready_);

    if (!ready_)
    {
        resp.field("note", "nothing enumerated on the USB host port");
        return RequestError::Ok;
    }

    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", vid_, pid_);
    resp.field("id", vidpid);
    resp.field("vendorId", static_cast<uint32_t>(vid_));
    resp.field("productId", static_cast<uint32_t>(pid_));
    resp.field("deviceRelease", static_cast<uint32_t>(bcdDevice_));
    resp.field("address", static_cast<uint32_t>(devAddr_));
    resp.field("manufacturer", manufacturer_);
    resp.field("product", product_);
    resp.field("serial", serial_);
    resp.field("printerClass", printerClass_);

    {
        auto iface = resp.object("interface");
        iface.field("number", static_cast<uint32_t>(ifaceNum_));
        iface.field("alternateSetting", static_cast<uint32_t>(altSet_));
        iface.field("class", static_cast<uint32_t>(ifaceClass_));
        iface.field("subClass", static_cast<uint32_t>(ifaceSubClass_));
        iface.field("protocol", static_cast<uint32_t>(ifaceProto_));
    }

    {
        auto arr = resp.array("endpoints");
        for (size_t i = 0; i < endpointCount_; ++i)
        {
            auto ep = arr.object();
            char addr[8];
            snprintf(addr, sizeof(addr), "0x%02x", endpoints_[i].addr);
            ep.field("address", addr);
            ep.field("direction", (endpoints_[i].addr & 0x80) ? "in" : "out");
            ep.field("type", XferTypeName(endpoints_[i].attributes));
            ep.field("maxPacketSize", static_cast<uint32_t>(endpoints_[i].mps));
            ep.field("interval", static_cast<uint32_t>(endpoints_[i].interval));
        }
    }

    char deviceId[DEVICE_ID_MAX] = {};
    if (GetDeviceId(deviceId, sizeof(deviceId)))
        resp.field("deviceId", deviceId);

    uint8_t portStatus = 0;
    if (GetPortStatus(portStatus))
    {
        resp.field("portStatus", static_cast<uint32_t>(portStatus));
        // Bit meanings from the printer class spec. Reported as flags because
        // "paper out" is the answer a caller actually wants.
        resp.field("paperEmpty", (portStatus & 0x20) != 0);
        resp.field("selected",   (portStatus & 0x10) != 0);
        resp.field("noError",    (portStatus & 0x08) != 0);
    }

    return RequestError::Ok;
}

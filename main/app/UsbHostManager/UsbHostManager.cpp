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

    // The OUT transfer, allocated once and reused for every chunk of every job.
    // The USB host library needs DMA-capable memory for it and allocates that
    // itself, which is another reason not to hand it a buffer of ours.
    //
    // There is no IN transfer: the only thing ever read back is the printer's
    // one-byte status, and that is a control request on the default pipe with a
    // transfer of its own.
    if (!AllocOutXfer())
        return;

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
    // Assembled locally and published in one assignment at the end. Nothing can
    // read it meanwhile: ready_ is false, so AcquireDevice refuses, so no
    // transfer can be looking at half-filled facts.
    Attached a;
    a.devAddr = devAddr;

    esp_err_t err = usb_host_device_open(clientHdl_, devAddr, &devHdl_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "device_open failed: %s", esp_err_to_name(err));
        devHdl_ = nullptr;
        return false;
    }

    LogDeviceDescriptor(a);

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

            a.ifaceNum      = intf->bInterfaceNumber;
            a.altSet        = intf->bAlternateSetting;
            a.ifaceClass    = intf->bInterfaceClass;
            a.ifaceSubClass = intf->bInterfaceSubClass;
            a.ifaceProto    = intf->bInterfaceProtocol;
            a.epOutAddr     = epOut;
            a.epInAddr      = epIn;
            a.epOutMps      = mpsOut ? mpsOut : 64;
            a.epInMps       = mpsIn ? mpsIn : 64;
            a.printerClass  = (intf->bInterfaceClass == PRINTER_CLASS);
            a.endpointCount = seenCount;
            memcpy(a.endpoints, seen, sizeof(seen));
            found = true;
        }
    }

    if (!found)
    {
        ESP_LOGE(TAG, "no interface with a bulk OUT endpoint - this is not a printer");
        CloseDevice();
        return false;
    }

    a.ready = true;
    {
        LOCK(stateMutex_);
        dev_ = a;
        closePending_ = false;
    }
    ready_.store(true);

    ESP_LOGI(TAG, "claimed interface %u alt %u (class %02x/%02x/%02x), "
                  "EP OUT 0x%02x mps %u, EP IN 0x%02x mps %u",
             a.ifaceNum, a.altSet, a.ifaceClass, a.ifaceSubClass, a.ifaceProto,
             a.epOutAddr, a.epOutMps, a.epInAddr, a.epInMps);

    // Ask the printer to name itself. This runs on the client-event task, which
    // must not block - but a control transfer on the default pipe is dispatched
    // by the same pump we are inside, so waiting here would deadlock. Log a
    // pointer to the command instead and let a caller ask on its own task.
    if (a.printerClass)
        ESP_LOGI(TAG, "printer-class interface - run 'usb status' for its IEEE-1284 device ID");

    return true;
}

void UsbHostManager::CloseDevice()
{
    LOCK(stateMutex_);

    // Stop new transfers first, so the count below can only fall.
    ready_.store(false);
    dev_.ready    = false;
    closePending_ = true;

    // Closing the device while a transfer is still using its handle is what
    // made unplugging mid-job a race. If one is in flight, the library will
    // complete it with a no-device status in a moment and its waiter does the
    // closing on the way out - see ReleaseDevice. This runs on the client-event
    // pump, which must not block, and it does not: stateMutex_ is never held
    // across a transfer.
    if (inFlight_ == 0)
        DoClose();
    else
        ESP_LOGW(TAG, "disconnect while %d transfer(s) in flight - deferring the close",
                 inFlight_);
}

void UsbHostManager::DoClose()
{
    if (devHdl_)
    {
        // Only give back what was actually claimed. OpenAndClaim comes here on
        // its own failure paths too, before there is an interface to release.
        if (dev_.epOutAddr) usb_host_interface_release(clientHdl_, devHdl_, dev_.ifaceNum);
        usb_host_device_close(clientHdl_, devHdl_);
        devHdl_ = nullptr;
    }
    dev_          = Attached{};
    closePending_ = false;
    ready_.store(false);
}

bool UsbHostManager::AcquireDevice(Attached& snap, usb_device_handle_t& devOut)
{
    LOCK(stateMutex_);
    if (!dev_.ready || !devHdl_ || closePending_) return false;
    snap   = dev_;
    devOut = devHdl_;
    ++inFlight_;
    return true;
}

void UsbHostManager::ReleaseDevice()
{
    LOCK(stateMutex_);
    if (--inFlight_ == 0 && closePending_)
        DoClose();
}

bool UsbHostManager::Snapshot(Attached& out) const
{
    LOCK(stateMutex_);
    out = dev_;
    return out.ready;
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

void UsbHostManager::LogDeviceDescriptor(Attached& a)
{
    const usb_device_desc_t* dev = nullptr;
    if (usb_host_get_device_descriptor(devHdl_, &dev) != ESP_OK || !dev)
    {
        ESP_LOGW(TAG, "could not read the device descriptor");
        return;
    }

    a.vid       = dev->idVendor;
    a.pid       = dev->idProduct;
    a.bcdDevice = dev->bcdDevice;

    usb_device_info_t info = {};
    if (usb_host_device_info(devHdl_, &info) == ESP_OK)
    {
        CopyStringDesc(info.str_desc_manufacturer, a.manufacturer, sizeof(a.manufacturer));
        CopyStringDesc(info.str_desc_product,      a.product,      sizeof(a.product));
        CopyStringDesc(info.str_desc_serial_num,   a.serial,       sizeof(a.serial));
    }

    ESP_LOGI(TAG, "device %04x:%04x rev %04x  class %02x/%02x/%02x  ep0 mps %u  configs %u",
             a.vid, a.pid, a.bcdDevice,
             dev->bDeviceClass, dev->bDeviceSubClass, dev->bDeviceProtocol,
             dev->bMaxPacketSize0, dev->bNumConfigurations);
    ESP_LOGI(TAG, "  manufacturer '%s'  product '%s'  serial '%s'",
             a.manufacturer, a.product, a.serial);
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

bool UsbHostManager::AllocOutXfer()
{
    if (outXfer_) return true;
    if (usb_host_transfer_alloc(OUT_XFER_BUF, 0, &outXfer_) != ESP_OK)
    {
        outXfer_ = nullptr;
        ESP_LOGE(TAG, "usb_host_transfer_alloc failed - the printer cannot be written to");
        return false;
    }
    outXfer_->callback = &UsbHostManager::TransferCallback;
    // context is per-submit: it points at that attempt's completion record.
    return true;
}

void UsbHostManager::TransferCallback(usb_transfer_t* transfer)
{
    auto* x = static_cast<Xfer*>(transfer->context);
    x->status = transfer->status;

    // Whichever side finds the flag ALREADY set knows the other got here first
    // and has gone, so that side owns the cleanup. See the comment on Xfer.
    if (x->claimed.exchange(true))
    {
        if (x->orphan) usb_host_transfer_free(x->orphan);
        delete x;
        return;
    }
    x->done.Give();
}

bool UsbHostManager::AwaitXfer(Xfer* x, uint32_t timeoutMs,
                               usb_transfer_t* oneShot, int& statusOut)
{
    if (!x->done.Take(pdMS_TO_TICKS(timeoutMs)))
    {
        // Published before the claim, so a callback that observes the claim
        // also observes this.
        x->orphan = oneShot;
        if (!x->claimed.exchange(true))
            return false;      // still queued - x and oneShot are the callback's now

        // It completed in the gap between the timeout expiring and the claim,
        // so the cleanup came back to us after all.
    }
    statusOut = x->status;
    delete x;
    return true;
}

void UsbHostManager::RecoverOutEndpoint(usb_device_handle_t dev, uint8_t ep)
{
    // Halt, then flush - which cancels everything queued and runs its callbacks
    // - then clear, which is what lets the pipe carry traffic again. Flush is
    // legal only on a halted endpoint, so the order is not a preference.
    if (usb_host_endpoint_halt(dev, ep) != ESP_OK)
    {
        ESP_LOGW(TAG, "endpoint halt failed - the OUT pipe stays wedged until replug");
        return;
    }
    if (usb_host_endpoint_flush(dev, ep) != ESP_OK)
        ESP_LOGW(TAG, "endpoint flush failed");
    if (usb_host_endpoint_clear(dev, ep) != ESP_OK)
        ESP_LOGW(TAG, "endpoint clear failed");
}

int UsbHostManager::Send(const uint8_t* data, size_t len, uint32_t timeoutMs)
{
    if (!data || len == 0) return -1;

    LOCK(sendMutex_);

    Attached snap;
    usb_device_handle_t dev = nullptr;
    if (!outXfer_ || !AcquireDevice(snap, dev)) return -1;

    // Chunk on a max-packet-size boundary. A bulk transfer that is not a
    // multiple of MPS ends with a short packet, which the printer reads as
    // "that was the end" - fine for the last chunk, a truncated job anywhere
    // else.
    const size_t chunkMax = (OUT_XFER_BUF / snap.epOutMps) * snap.epOutMps;
    size_t sent   = 0;
    bool   failed = false;

    while (sent < len)
    {
        const size_t chunk = (len - sent) < chunkMax ? (len - sent) : chunkMax;
        memcpy(outXfer_->data_buffer, data + sent, chunk);
        outXfer_->num_bytes        = chunk;
        outXfer_->device_handle    = dev;
        outXfer_->bEndpointAddress = snap.epOutAddr;

        auto* x = new Xfer();
        outXfer_->context = x;

        if (usb_host_transfer_submit(outXfer_) != ESP_OK)
        {
            ESP_LOGE(TAG, "transfer_submit failed at offset %u", (unsigned)sent);
            delete x;                       // never queued, so nobody else has it
            failed = true;
            break;
        }

        int status = -1;
        if (!AwaitXfer(x, timeoutMs, outXfer_, status))
        {
            // Still queued. The host library does not implement per-transfer
            // timeouts, so it will never end by itself and outXfer_->data_buffer
            // would never be ours to overwrite again. The endpoint has to be
            // halted and flushed to force the completion, and the transfer goes
            // with it: the callback frees it when the flush finally fires, and a
            // fresh one is allocated here so the next job still has a buffer.
            ESP_LOGE(TAG, "transfer stuck at offset %u - halting the endpoint",
                     (unsigned)sent);
            outXfer_ = nullptr;
            RecoverOutEndpoint(dev, snap.epOutAddr);
            AllocOutXfer();
            failed = true;
            break;
        }

        if (status != USB_TRANSFER_STATUS_COMPLETED)
        {
            ESP_LOGE(TAG, "transfer status %d at offset %u", status, (unsigned)sent);
            failed = true;
            break;
        }

        sent += outXfer_->actual_num_bytes;
        if (outXfer_->actual_num_bytes != static_cast<int>(chunk))
        {
            ESP_LOGW(TAG, "short write %d/%u - printer stopped accepting",
                     outXfer_->actual_num_bytes, (unsigned)chunk);
            break;
        }
    }

    ReleaseDevice();
    return failed ? -1 : static_cast<int>(sent);
}

int UsbHostManager::ControlTransfer(uint8_t bmRequestType, uint8_t bRequest,
                                    uint16_t wValue, uint16_t wIndex,
                                    uint8_t* data, uint16_t wLength, uint32_t timeoutMs)
{
    Attached snap;
    usb_device_handle_t dev = nullptr;
    if (!AcquireDevice(snap, dev)) return -1;

    usb_transfer_t* xfer = nullptr;
    if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + wLength, 0, &xfer) != ESP_OK)
    {
        ReleaseDevice();
        return -1;
    }

    auto* setup = reinterpret_cast<usb_setup_packet_t*>(xfer->data_buffer);
    setup->bmRequestType = bmRequestType;
    setup->bRequest      = bRequest;
    setup->wValue        = wValue;
    setup->wIndex        = wIndex;
    setup->wLength       = wLength;

    auto* x = new Xfer();
    xfer->device_handle    = dev;
    xfer->bEndpointAddress = 0;
    xfer->num_bytes        = sizeof(usb_setup_packet_t) + wLength;
    xfer->callback         = &UsbHostManager::TransferCallback;
    xfer->context          = x;

    if (usb_host_transfer_submit_control(clientHdl_, xfer) != ESP_OK)
    {
        ESP_LOGW(TAG, "control request %02x/%02x could not be submitted",
                 bmRequestType, bRequest);
        delete x;
        usb_host_transfer_free(xfer);
        ReleaseDevice();
        return -1;
    }

    int status   = -1;
    int received = -1;

    if (!AwaitXfer(x, timeoutMs, xfer, status))
    {
        // Still queued on the DEFAULT pipe, which has no halt or flush of its
        // own - so there is nothing to force it and no moment at which this
        // task may free it. The transfer and its record belong to the callback
        // now. Freeing one here, while the driver still owned it, is what used
        // to corrupt the internal heap.
        ESP_LOGW(TAG, "control request %02x/%02x abandoned after %u ms",
                 bmRequestType, bRequest, (unsigned)timeoutMs);
        ReleaseDevice();
        return -1;
    }

    if (status == USB_TRANSFER_STATUS_COMPLETED)
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
                 bmRequestType, bRequest, status);
    }

    usb_host_transfer_free(xfer);
    ReleaseDevice();
    return received;
}

bool UsbHostManager::GetPortStatus(uint8_t& statusOut)
{
    Attached snap;
    if (!Snapshot(snap) || !snap.printerClass) return false;

    LOCK(sendMutex_);
    uint8_t s = 0;
    const int n = ControlTransfer(RT_CLASS_IFACE_IN, REQ_GET_PORT_STATUS,
                                  0, snap.ifaceNum, &s, 1, 1000);
    if (n < 1) return false;
    statusOut = s;
    return true;
}

bool UsbHostManager::GetDeviceId(char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    Attached snap;
    if (!Snapshot(snap) || !snap.printerClass) return false;

    LOCK(sendMutex_);

    uint8_t buf[DEVICE_ID_MAX] = {};
    // wValue is the config index, wIndex is (interface << 8) | alternate setting.
    const uint16_t wIndex = static_cast<uint16_t>((snap.ifaceNum << 8) | snap.altSet);
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

    // One consistent copy of the device, taken once. Reading the live state
    // field by field could report a vendor id from the printer that was just
    // unplugged next to an endpoint list from nothing at all.
    Attached a;
    const bool attached = Snapshot(a);

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("attached", attached);

    if (!attached)
    {
        resp.field("note", "nothing enumerated on the USB host port");
        return RequestError::Ok;
    }

    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", a.vid, a.pid);
    resp.field("id", vidpid);
    resp.field("vendorId", static_cast<uint32_t>(a.vid));
    resp.field("productId", static_cast<uint32_t>(a.pid));
    resp.field("deviceRelease", static_cast<uint32_t>(a.bcdDevice));
    resp.field("address", static_cast<uint32_t>(a.devAddr));
    resp.field("manufacturer", a.manufacturer);
    resp.field("product", a.product);
    resp.field("serial", a.serial);
    resp.field("printerClass", a.printerClass);

    {
        auto iface = resp.object("interface");
        iface.field("number", static_cast<uint32_t>(a.ifaceNum));
        iface.field("alternateSetting", static_cast<uint32_t>(a.altSet));
        iface.field("class", static_cast<uint32_t>(a.ifaceClass));
        iface.field("subClass", static_cast<uint32_t>(a.ifaceSubClass));
        iface.field("protocol", static_cast<uint32_t>(a.ifaceProto));
    }

    {
        auto arr = resp.array("endpoints");
        for (size_t i = 0; i < a.endpointCount; ++i)
        {
            auto ep = arr.object();
            char addr[8];
            snprintf(addr, sizeof(addr), "0x%02x", a.endpoints[i].addr);
            ep.field("address", addr);
            ep.field("direction", (a.endpoints[i].addr & 0x80) ? "in" : "out");
            ep.field("type", XferTypeName(a.endpoints[i].attributes));
            ep.field("maxPacketSize", static_cast<uint32_t>(a.endpoints[i].mps));
            ep.field("interval", static_cast<uint32_t>(a.endpoints[i].interval));
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

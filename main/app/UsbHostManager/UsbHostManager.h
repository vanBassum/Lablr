#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "Mutex.h"
#include "Semaphore.h"
#include "Task.h"
#include "usb/usb_host.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// UsbHostManager — the ESP32-S3's USB OTG port in HOST mode, and nothing about
// printing.
//
// This is the transport half of the print path. It enumerates whatever is
// plugged in, claims a printer-class interface if the device has one, and moves
// bytes over its bulk endpoints. It does not know what a raster line is, what a
// label is, or that DYMO exists — PrintManager owns all of that and reaches this
// through the provider like any other peer.
//
// The split is worth one sentence of justification: a printer protocol that
// reached into usb_host_* directly could not be read without also knowing the
// USB host library's threading rules, and those rules are the whole content of
// this file. Two files, one seam, no hierarchy: there is no PrinterTransport
// interface here, because there is one transport and inventing a second
// implementation for a device we do not own would be fiction.
//
// ── Threading, which the USB host library is strict about ──
//
// Two pumps are mandatory: the library daemon (usb_host_lib_handle_events) does
// enumeration, and the client pump (usb_host_client_handle_events) dispatches
// this client's connect/disconnect callbacks. Both must keep running or the bus
// stops.
//
// The connect callback runs ON the client pump, so it must not block. That is
// why OpenAndClaim only opens, reads descriptors and claims — it never
// transfers. Anything that waits for a transfer runs on the caller's task
// (a command handler), never here.
//
// ── What "inspection" means here ──
//
// Enumeration logs the device descriptor, every interface and every endpoint
// before anything is claimed, and `usb status` returns the same facts as JSON.
// That is deliberate: the alternative is guessing a printer's protocol from its
// model number, and a device that is willing to describe itself should be asked.
// IEEE-1284 GET_DEVICE_ID (a printer-class control request) is part of that —
// it is the printer naming its own command set, which is the single most useful
// fact about it.
// ──────────────────────────────────────────────────────────────

class UsbHostManager
{
    static constexpr const char* TAG = "UsbHostManager";

    /// The reusable OUT transfer. Rounded down to a multiple of the real
    /// endpoint max-packet-size once that is known, so the printer never sees a
    /// short packet in the middle of a job (a short packet is an end-of-transfer
    /// marker on bulk, and one mid-stream is how a job gets truncated).
    static constexpr size_t OUT_XFER_BUF = 4096;

    /// The IN transfer, for status. Printer status is one byte; this is
    /// generous because an endpoint's MPS is 64 and the buffer must hold one.
    static constexpr size_t IN_XFER_BUF = 64;

    /// Longest IEEE-1284 device ID we will ask for. The reply is
    /// [len_hi][len_lo][ascii...] and real ones are a few hundred bytes.
    static constexpr size_t DEVICE_ID_MAX = 512;

    static constexpr size_t MAX_ENDPOINTS_LOGGED = 8;

public:
    explicit UsbHostManager(AppProvider& app);

    UsbHostManager(const UsbHostManager&) = delete;
    UsbHostManager& operator=(const UsbHostManager&) = delete;
    UsbHostManager(UsbHostManager&&) = delete;
    UsbHostManager& operator=(UsbHostManager&&) = delete;

    void Init();

    /// True once a device is enumerated and a bulk OUT endpoint is claimed.
    bool IsReady() const { return ready_; }

    /// Vendor and product of the attached device, 0 when nothing is attached.
    uint16_t VendorId()  const { return vid_; }
    uint16_t ProductId() const { return pid_; }

    /// The product string the device reported, or "" if it gave none.
    const char* ProductName() const { return product_; }

    /// Push bytes at the bulk OUT endpoint, blocking until they are all gone.
    /// Returns bytes accepted, or -1 on error / nothing attached. Serialised:
    /// there is one transfer buffer and one printer.
    int Send(const uint8_t* data, size_t len, uint32_t timeoutMs = 5000);

    /// Read the printer-class status byte over the control pipe (GET_PORT_STATUS).
    /// Returns false if the device has no printer interface or refuses.
    bool GetPortStatus(uint8_t& statusOut);

    /// Ask the device for its IEEE-1284 ID string. This is the printer stating
    /// its manufacturer, model and COMMAND SET, which is how we learn what
    /// raster dialect it speaks rather than assuming one. `out` is NUL
    /// terminated. False when the device has no printer interface or refuses.
    bool GetDeviceId(char* out, size_t outSize);

private:
    AppProvider& app_;
    InitState    initState_;

    usb_host_client_handle_t clientHdl_ = nullptr;
    usb_device_handle_t      devHdl_    = nullptr;

    Task daemonTask_;
    Task clientTask_;

    // ── What is attached, filled during enumeration ──
    bool     ready_        = false;
    bool     printerClass_ = false;   ///< the claimed interface is bInterfaceClass 7
    uint16_t vid_          = 0;
    uint16_t pid_          = 0;
    uint16_t bcdDevice_    = 0;
    uint8_t  devAddr_      = 0;
    char     manufacturer_[64] = {};
    char     product_[64]      = {};
    char     serial_[64]       = {};

    uint8_t  ifaceNum_  = 0;
    uint8_t  altSet_    = 0;
    uint8_t  ifaceClass_ = 0, ifaceSubClass_ = 0, ifaceProto_ = 0;
    uint8_t  epOutAddr_ = 0;
    uint8_t  epInAddr_  = 0;
    uint16_t epOutMps_  = 64;
    uint16_t epInMps_   = 64;

    /// A flat record of every endpoint seen on the claimed interface, kept only
    /// so `usb status` can report it without re-walking descriptors.
    struct EndpointInfo { uint8_t addr; uint8_t attributes; uint16_t mps; uint8_t interval; };
    EndpointInfo endpoints_[MAX_ENDPOINTS_LOGGED] = {};
    size_t       endpointCount_ = 0;

    usb_transfer_t* outXfer_ = nullptr;
    usb_transfer_t* inXfer_  = nullptr;
    Semaphore       xferDone_;
    int             xferStatus_ = -1;
    mutable Mutex   sendMutex_;

    void DaemonTaskLoop();
    void ClientTaskLoop();

    bool OpenAndClaim(uint8_t devAddr);
    void CloseDevice();

    void LogDeviceDescriptor();
    void LogConfiguration(const usb_config_desc_t* cfg);

    /// One blocking control transfer on the default pipe. Returns the number of
    /// data bytes received, or -1.
    int ControlTransfer(uint8_t bmRequestType, uint8_t bRequest,
                        uint16_t wValue, uint16_t wIndex,
                        uint8_t* data, uint16_t wLength, uint32_t timeoutMs);

    static void ClientEventCallback(const usb_host_client_event_msg_t* msg, void* arg);
    static void TransferCallback(usb_transfer_t* transfer);

    static void CopyStringDesc(const usb_str_desc_t* desc, char* out, size_t outSize);
    static const char* XferTypeName(uint8_t attributes);

    // ── Commands ──
    RequestError Cmd_UsbStatus(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "usb", "status", &InvokeCommand<&UsbHostManager::Cmd_UsbStatus>,
          "Describe the USB device attached to the host port: vendor and product "
          "id, its own manufacturer/product/serial strings, the interface that "
          "was claimed with its class/subclass/protocol, every endpoint with "
          "direction, transfer type and max packet size, and - if it is a "
          "printer - its IEEE-1284 device ID, which is the printer naming its "
          "own model and command set. Nothing is attached is a normal answer." },
    };
};

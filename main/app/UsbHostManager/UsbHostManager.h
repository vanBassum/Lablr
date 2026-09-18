#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "Mutex.h"
#include "Semaphore.h"
#include "Task.h"
#include "usb/usb_host.h"
#include <atomic>
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

    /// A flat record of every endpoint seen on the claimed interface, kept only
    /// so `usb status` can report it without re-walking descriptors.
    struct EndpointInfo { uint8_t addr; uint8_t attributes; uint16_t mps; uint8_t interval; };

    /// Everything enumeration learned about the attached device.
    ///
    /// One struct rather than twenty members because it is read from command
    /// tasks while the client-event task may be clearing it: a reader takes a
    /// consistent COPY in one assignment under the state lock, instead of
    /// racing field by field and reporting half of one device and half of none.
    struct Attached
    {
        bool     ready        = false;
        bool     printerClass = false;   ///< the claimed interface is bInterfaceClass 7
        uint16_t vid = 0, pid = 0, bcdDevice = 0;
        uint8_t  devAddr = 0;

        char manufacturer[64] = {};
        char product[64]      = {};
        char serial[64]       = {};

        uint8_t  ifaceNum = 0, altSet = 0;
        uint8_t  ifaceClass = 0, ifaceSubClass = 0, ifaceProto = 0;
        uint8_t  epOutAddr = 0, epInAddr = 0;
        uint16_t epOutMps = 64, epInMps = 64;

        EndpointInfo endpoints[MAX_ENDPOINTS_LOGGED] = {};
        size_t       endpointCount = 0;
    };

    /// True once a device is enumerated and a bulk OUT endpoint is claimed.
    /// Advisory only - it can go false the instant after it is read, so it
    /// answers "is a printer plugged in" and never guards a transfer. The
    /// authoritative check is inside Send/ControlTransfer, under the lock.
    bool IsReady() const { return ready_.load(); }

    /// A consistent copy of what is attached. False when nothing is.
    bool Snapshot(Attached& out) const;

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

    // ── One transfer's completion ──
    //
    // Heap-allocated per attempt rather than shared, and owned JOINTLY by the
    // waiter and the transfer callback, because the two can race at exactly one
    // moment: the waiter's timeout.
    //
    // A timeout is not hypothetical here. usb_transfer_t::timeout_ms is
    // documented "currently not supported yet" in the USB host component this
    // builds against, so the driver never times a transfer out by itself - a
    // transfer that does not complete stays queued forever and OUR wait is the
    // only limit. Abandoning one is therefore a state that has to be designed,
    // not an edge case.
    //
    // `claimed` is the agreement. Whichever side finds it ALREADY true knows
    // the other got there first and has gone, so that side owns the cleanup.
    // Both interleavings are safe, and neither can free twice.
    struct Xfer
    {
        Semaphore         done;
        int               status = -1;
        std::atomic<bool> claimed{false};
        /// Set by an abandoning waiter, before it claims: the transfer object
        /// the callback must free along with this, or null when the transfer is
        /// a reusable one the manager keeps.
        usb_transfer_t*   orphan = nullptr;
    };

    // ── What is attached ──
    //
    // `dev_`, `devHdl_`, `inFlight_` and `closePending_` are all guarded by
    // stateMutex_, which is held only for the moments it takes to read or
    // publish them - never across a transfer. `ready_` is a lock-free echo of
    // dev_.ready for IsReady().
    //
    // Lock order where both are taken: sendMutex_ THEN stateMutex_, never the
    // reverse. CloseDevice runs on the client-event pump and takes only
    // stateMutex_, so a 30-second print cannot block a disconnect.
    std::atomic<bool> ready_{false};
    mutable Mutex     stateMutex_;
    Attached          dev_;
    int               inFlight_     = 0;   ///< transfers using devHdl_ right now
    bool              closePending_ = false;

    usb_transfer_t* outXfer_ = nullptr;
    mutable Mutex   sendMutex_;            ///< one transfer buffer, one printer

    void DaemonTaskLoop();
    void ClientTaskLoop();

    bool OpenAndClaim(uint8_t devAddr);
    void CloseDevice();
    void DoClose();                        ///< stateMutex_ must be held

    /// Take a reference on the attached device so it cannot be closed out from
    /// under a transfer, and copy its facts. False when nothing is attached or
    /// a disconnect is already being drained. Every success must be paired with
    /// ReleaseDevice().
    bool AcquireDevice(Attached& snap, usb_device_handle_t& devOut);
    void ReleaseDevice();

    /// Wait for one transfer to complete.
    ///
    /// True: the completion landed, `statusOut` is valid, and the transfer
    /// object is the caller's again. False: it is STILL QUEUED in the driver -
    /// `x` and `oneShot` now belong to the callback and the caller must touch
    /// neither. Freeing a transfer the driver still owns is what corrupted the
    /// internal heap before this existed.
    static bool AwaitXfer(Xfer* x, uint32_t timeoutMs,
                          usb_transfer_t* oneShot, int& statusOut);

    /// Halt, flush and clear the bulk OUT endpoint, which is the only way to
    /// force a stuck transfer to complete. Flush is legal only on a halted
    /// endpoint and clear is what makes it carry traffic again.
    void RecoverOutEndpoint(usb_device_handle_t dev, uint8_t ep);

    /// Allocate the reusable OUT transfer if there is not one. Called at Init
    /// and again after a stuck transfer was handed to its callback.
    bool AllocOutXfer();

    void LogDeviceDescriptor(Attached& a);
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

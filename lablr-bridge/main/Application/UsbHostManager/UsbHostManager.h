#pragma once

#include "ServiceProvider.h"
#include "InitState.h"
#include "Mutex.h"
#include "Semaphore.h"
#include "Task.h"
#include "usb/usb_host.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// UsbHostManager — drives the ESP32-S3 USB OTG port in HOST mode and
// talks to the attached Dymo LabelWriter.
//
// Role in the bridge:   phone --BLE--> BleManager --> UsbHostManager --USB--> Dymo
//
// The Dymo presents itself as a USB printer-class device (bInterfaceClass
// 0x07). We claim that interface, locate its bulk OUT endpoint, and stream
// the raw bytes the host (PWA) produced straight to the printer. There is no
// reinterpretation of the payload here — the bridge is a dumb pipe, which is
// exactly what "Preview = Print" in Lablr requires.
// ──────────────────────────────────────────────────────────────

class UsbHostManager
{
    static constexpr const char* TAG = "UsbHostManager";

    // The Dymo LabelWriter advertises bInterfaceClass == USB_CLASS_PRINTER
    // (0x07), a macro already provided by usb/usb_types_ch9.h.

public:
    explicit UsbHostManager(ServiceProvider& serviceProvider);

    UsbHostManager(const UsbHostManager&) = delete;
    UsbHostManager& operator=(const UsbHostManager&) = delete;
    UsbHostManager(UsbHostManager&&) = delete;
    UsbHostManager& operator=(UsbHostManager&&) = delete;

    void Init();

    /// True once a printer is enumerated and its bulk OUT endpoint is claimed.
    bool IsPrinterReady() const { return printerReady_; }

    /// Stream raw bytes to the printer's bulk OUT endpoint. Blocks until the
    /// whole buffer is transferred (or an error/timeout occurs).
    /// Returns the number of bytes accepted, or -1 on error / no printer.
    int SendToPrinter(const uint8_t* data, size_t len, uint32_t timeoutMs = 5000);

    /// Print a self-contained test pattern (bring-up: proves the USB path
    /// without the phone). Runs on its own task — safe to call from anywhere.
    void RequestTestPrint() { testTrigger_.Give(); }

    // Bring-up convenience: auto-print the test pattern when a printer is first
    // detected. Off now that real print transports (cloud/BLE) drive printing;
    // RequestTestPrint() is still available on demand.
    static constexpr bool PRINT_TEST_ON_CONNECT = false;

private:
    ServiceProvider& serviceProvider_;
    InitState initState_;

    // USB Host Library handles
    usb_host_client_handle_t clientHdl_ = nullptr;
    usb_device_handle_t      devHdl_    = nullptr;

    // Daemon (library event) + client event pumps
    Task daemonTask_;
    Task clientTask_;

    // Test-print runs on its own task so SendToPrinter never blocks inside the
    // USB client-event task (which must stay free to dispatch the OUT callback).
    Task      testTask_;
    Semaphore testTrigger_;

    // Claimed printer interface / endpoints
    bool     printerReady_   = false;
    uint8_t  ifaceNum_       = 0;
    uint8_t  epOutAddr_      = 0;
    uint8_t  epInAddr_       = 0;     // status endpoint, if the printer exposes one
    uint16_t epOutMps_       = 64;

    // Single reusable OUT transfer + its completion signal
    usb_transfer_t* outXfer_ = nullptr;
    Semaphore       xferDone_;
    int             xferStatus_ = -1;     // last transfer status (USB_TRANSFER_STATUS_*)
    mutable Mutex   sendMutex_;           // serialise SendToPrinter callers

    void DaemonTaskLoop();
    void ClientTaskLoop();
    void TestTaskLoop();
    void PrintTestPattern();

    bool OpenAndClaim(uint8_t devAddr);
    void CloseDevice();

    static void ClientEventCallback(const usb_host_client_event_msg_t* msg, void* arg);
    static void OutTransferCallback(usb_transfer_t* transfer);
};

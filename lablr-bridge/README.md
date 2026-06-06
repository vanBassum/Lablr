# lablr-bridge

*A BLE ⇄ USB bridge that lets a phone print to a USB-only Dymo LabelWriter.*

The [Lablr](..) PWA renders a label to a 1-bit bitmap and prints it. On
desktop that goes straight to the printer over WebUSB. On an Android phone
there is no USB path to a Dymo LabelWriter — so this firmware turns an
**ESP32-S3** into the missing link:

```
   Lablr PWA (phone)
        │  Web Bluetooth — writes the print payload
        ▼
   ┌─────────────────────────┐
   │   ESP32-S3 (this fw)     │
   │  BleManager  ──►  UsbHostManager
   │  (NimBLE GATT)    (USB host, bulk OUT)
   └─────────────────────────┘
        │  USB (host mode)
        ▼
   Dymo LabelWriter 450
```

The bridge is a **dumb pipe**: bytes written to the BLE characteristic are
forwarded verbatim to the printer's bulk OUT endpoint. No reinterpretation —
which is exactly what Lablr's "Preview = Print" invariant requires.

Built on [Strux](../../Strux), trimmed to the gateway core (console, settings,
WiFi, OTA, web UI for config/status) with two new managers added: `BleManager`
and `UsbHostManager`.

---

## Hardware

- **ESP32-S3** dev board, using its **native USB connector** (the OTG port on
  GPIO19/20 — *not* the UART/JTAG port).
- A **5 V supply for the printer's VBUS**. The S3 cannot power a LabelWriter;
  feed VBUS from an external supply and share ground. A powered USB hub or a
  small OTG power-injection cable works.
- Optional status LED (see [`BoardConfig.h`](main/hardware/BoardConfig.h)).

> ⚠ The Dymo LabelWriter draws more than the S3 can source. Powering VBUS
> externally is mandatory, not optional.

---

## How it works

### BleManager — the phone-facing side
A NimBLE peripheral advertising a **Nordic-UART-style** GATT service (so a Web
Bluetooth client can use it without a custom profile):

| Characteristic | UUID suffix | Direction | Purpose |
|----------------|-------------|-----------|---------|
| RX | `…0002` | write / write-no-rsp | phone → bridge: print bytes |
| TX | `…0003` | notify | bridge → phone: status (`error:no-printer`, …) |

Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`.

Incoming RX bytes are pushed into a FreeRTOS **stream buffer** and drained by a
dedicated task — keeping the NimBLE host task free of the blocking USB transfer
and giving natural backpressure when the printer is slow.

### UsbHostManager — the printer-facing side
Installs the IDF 6.0 USB Host Library (`espressif/usb` registry component),
runs the library + client event pumps, and on device-connect:

1. opens the device and reads its active config descriptor,
2. finds the **printer-class** interface (`bInterfaceClass 0x07`), or the first
   interface with a bulk OUT endpoint,
3. claims it and records the bulk OUT (and optional bulk IN) endpoints,
4. exposes `SendToPrinter(data, len)` — chunked, MPS-aligned bulk writes.

---

## Build & flash

Requires **ESP-IDF v6.0+** (the USB Host Library is a managed component now, so
the first build downloads `espressif/usb`).

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Flash/partition layout assumes an **8 MB** S3 module (see
[`partitions.csv`](partitions.csv)). If yours is 4 MB, shrink the OTA slots and
set `CONFIG_ESPTOOLPY_FLASHSIZE_4MB` — WiFi + BLE + USB host + dual OTA is a
tight fit in 4 MB.

The React web UI (inherited from Strux) is built automatically if `pnpm` is
installed; it serves device config, WiFi setup, live logs, and OTA. It is **not**
on the print path — printing is BLE → USB only.

---

## Status / what's stubbed

This is a working scaffold. The structure (managers, event pumps, GATT service,
bulk-OUT path) is complete and compiles, but the printer-specific bits want
real-hardware validation:

- **Interface/endpoint selection** assumes the Dymo exposes a standard
  printer-class bulk OUT endpoint. Confirm against your unit's descriptors.
- **Printer status** (bulk IN reads → TX notify) is wired for discovery but not
  yet streamed back.
- **VBUS / connection robustness** (re-enumeration, error recovery) is minimal.

---

## Project layout

```
lablr-bridge/
├── main/
│   ├── main.cpp                       # Boot — Init() calls only
│   ├── Application/
│   │   ├── BleManager/                # NimBLE peripheral (phone side)   ← new
│   │   ├── UsbHostManager/            # USB host bulk OUT (Dymo side)    ← new
│   │   ├── ConsoleManager/            # Log capture + WebSocket
│   │   ├── SettingsManager/           # NVS key/value
│   │   ├── NetworkManager/            # WiFi STA/AP (for config/OTA)
│   │   ├── CommandManager/            # WebSocket RPC
│   │   ├── DeviceManager/             # LED + future hardware
│   │   ├── UpdateManager/             # OTA
│   │   └── WebServerManager/          # HTTP + WebSocket
│   ├── hardware/BoardConfig.h         # Pins / USB notes
│   └── lib/                           # rtos / json / system helpers
├── frontend/                          # React config/status UI (Strux-derived)
├── partitions.csv                     # 8 MB S3 layout
└── sdkconfig.defaults                 # BLE + USB host + OTA defaults
```

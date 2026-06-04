import { useState } from "react"
import { Button } from "@/components/ui/button"

// Minimal WebUSB typings — lib.dom doesn't ship these without @types/w3c-web-usb,
// and we only touch the handful of members used below.
interface UsbEndpoint {
  endpointNumber: number
  direction: "in" | "out"
  type: "bulk" | "interrupt" | "isochronous"
  packetSize: number
}
interface UsbAlternate {
  alternateSetting: number
  interfaceClass: number
  interfaceSubclass: number
  interfaceProtocol: number
  endpoints: UsbEndpoint[]
}
interface UsbInterface {
  interfaceNumber: number
  claimed: boolean
  alternates: UsbAlternate[]
}
interface UsbConfiguration {
  configurationValue: number
  interfaces: UsbInterface[]
}
interface UsbDevice {
  vendorId: number
  productId: number
  productName?: string
  manufacturerName?: string
  serialNumber?: string
  configuration: UsbConfiguration | null
  configurations: UsbConfiguration[]
  open(): Promise<void>
  close(): Promise<void>
  selectConfiguration(value: number): Promise<void>
  claimInterface(n: number): Promise<void>
  releaseInterface(n: number): Promise<void>
}
interface Usb {
  requestDevice(opts: {
    filters: Array<{ vendorId?: number; productId?: number }>
  }): Promise<UsbDevice>
}

// DYMO's USB vendor ID. The product ID varies per model; we leave it open so any
// DYMO device shows in the chooser and we can read the real productId from it.
const DYMO_VENDOR_ID = 0x0922

function getUsb(): Usb | null {
  return (navigator as unknown as { usb?: Usb }).usb ?? null
}

const hex = (n: number) => "0x" + n.toString(16).padStart(4, "0")

export function WebUsbProbe() {
  const [log, setLog] = useState<string[]>([])
  const append = (line: string) => setLog((l) => [...l, line])

  async function probe() {
    setLog([])
    const usb = getUsb()
    if (!usb) {
      append("✗ WebUSB not available. Use Chrome/Edge over HTTPS or localhost.")
      return
    }

    let device: UsbDevice
    try {
      append("Requesting device — pick the DYMO in the chooser…")
      device = await usb.requestDevice({
        filters: [{ vendorId: DYMO_VENDOR_ID }],
      })
    } catch (e) {
      append(`✗ No device selected / request failed: ${(e as Error).message}`)
      return
    }

    append(
      `✓ Selected: ${device.manufacturerName ?? "?"} — ${device.productName ?? "?"}`,
    )
    append(
      `  vendorId=${hex(device.vendorId)}  productId=${hex(device.productId)}  serial=${device.serialNumber ?? "?"}`,
    )

    try {
      await device.open()
      append("✓ device.open() ok")
    } catch (e) {
      append(`✗ device.open() failed: ${(e as Error).message}`)
      return
    }

    try {
      if (device.configuration === null) await device.selectConfiguration(1)
      append("✓ configuration selected")
    } catch (e) {
      append(`✗ selectConfiguration failed: ${(e as Error).message}`)
    }

    // Dump the descriptor tree — this tells us which bulk-OUT endpoint to push
    // raster bytes to once we move on to actually printing.
    for (const cfg of device.configurations) {
      append(`Config ${cfg.configurationValue}:`)
      for (const intf of cfg.interfaces) {
        const alt = intf.alternates[0]
        append(
          `  Interface ${intf.interfaceNumber}  (class ${alt.interfaceClass}, sub ${alt.interfaceSubclass}, proto ${alt.interfaceProtocol})`,
        )
        for (const ep of alt.endpoints) {
          append(
            `    ep#${ep.endpointNumber}  ${ep.direction.toUpperCase()}  ${ep.type}  ${ep.packetSize}B`,
          )
        }
      }
    }

    // Try to claim interface 0. On Windows this is the usual wall: if the OS
    // printer driver (usbprint.sys) owns the device, the claim fails here.
    const first = device.configurations[0]?.interfaces[0]
    if (first) {
      try {
        await device.claimInterface(first.interfaceNumber)
        append(
          `✓ claimInterface(${first.interfaceNumber}) ok — Chrome owns the device, raw output is possible 🎉`,
        )
        await device.releaseInterface(first.interfaceNumber)
      } catch (e) {
        append(`✗ claimInterface failed: ${(e as Error).message}`)
        append(
          "  On Windows this almost always means the OS printer driver owns the device.",
        )
        append(
          "  Workaround: use Zadig to bind the WinUSB driver to the DYMO (disables normal printing).",
        )
      }
    }

    await device.close()
    append("Done.")
  }

  return (
    <div className="flex flex-col gap-3">
      <Button onClick={probe}>Connect DYMO (WebUSB)</Button>
      <pre className="bg-muted max-h-96 overflow-auto rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {log.length
          ? log.join("\n")
          : "Plug in the DYMO LabelWriter 450, then click. (Chrome/Edge, localhost or HTTPS.)"}
      </pre>
    </div>
  )
}

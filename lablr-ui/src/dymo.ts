// Minimal WebUSB transport for the DYMO LabelWriter 450.
//
// Rendering and the LW450 raster encoding now live on the backend (the single
// renderer). This module only opens the device and pushes already-built job
// bytes to its bulk OUT endpoint — so "preview = print" holds: the bytes here
// are exactly what the backend rendered and previewed.

export const DYMO_VENDOR_ID = 0x0922
export const DYMO_LW450_PRODUCT_ID = 0x0020

// --- Minimal WebUSB typings (lib.dom omits these without @types/w3c-web-usb) ---
interface UsbOutTransferResult {
  status: "ok" | "stall" | "babble"
  bytesWritten: number
}
export interface UsbDevice {
  vendorId: number
  open(): Promise<void>
  close(): Promise<void>
  selectConfiguration(value: number): Promise<void>
  claimInterface(n: number): Promise<void>
  releaseInterface(n: number): Promise<void>
  transferOut(endpointNumber: number, data: Uint8Array): Promise<UsbOutTransferResult>
  configuration: unknown
}
interface UsbConnectionEvent {
  device: UsbDevice
}
interface Usb {
  requestDevice(opts: {
    filters: Array<{ vendorId?: number; productId?: number }>
  }): Promise<UsbDevice>
  getDevices(): Promise<UsbDevice[]>
  addEventListener(type: "disconnect", listener: (e: UsbConnectionEvent) => void): void
  removeEventListener(type: "disconnect", listener: (e: UsbConnectionEvent) => void): void
}

function getUsb(): Usb {
  const usb = (navigator as unknown as { usb?: Usb }).usb
  if (!usb) throw new Error("WebUSB unavailable — use Chrome/Edge on localhost or HTTPS.")
  return usb
}

function usbOrNull(): Usb | null {
  return (navigator as unknown as { usb?: Usb }).usb ?? null
}

/** Open + claim interface 0 on an already-selected device, ready for output. */
async function claim(device: UsbDevice): Promise<UsbDevice> {
  await device.open()
  if (device.configuration === null) await device.selectConfiguration(1)
  await device.claimInterface(0)
  return device
}

/** Prompt for the DYMO (needs a user gesture), then open + claim it. */
export async function openDymo(): Promise<UsbDevice> {
  const device = await getUsb().requestDevice({
    filters: [{ vendorId: DYMO_VENDOR_ID }],
  })
  return claim(device)
}

/** Reconnect to a previously-granted DYMO without prompting; null if none. */
export async function getGrantedDymo(): Promise<UsbDevice | null> {
  const usb = usbOrNull()
  if (!usb) return null
  const device = (await usb.getDevices()).find((d) => d.vendorId === DYMO_VENDOR_ID)
  return device ? claim(device) : null
}

/** Subscribe to USB unplug events; returns an unsubscribe function. */
export function onUsbDisconnect(cb: (device: UsbDevice) => void): () => void {
  const usb = usbOrNull()
  if (!usb) return () => {}
  const handler = (e: UsbConnectionEvent) => cb(e.device)
  usb.addEventListener("disconnect", handler)
  return () => usb.removeEventListener("disconnect", handler)
}

/** Send a backend-built LW450 job to the printer. ep#2 is the bulk OUT endpoint. */
export async function printBytes(device: UsbDevice, bytes: Uint8Array): Promise<void> {
  const result = await device.transferOut(2, bytes)
  if (result.status !== "ok") throw new Error(`transferOut status: ${result.status}`)
}

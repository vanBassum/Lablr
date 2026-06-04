// Minimal WebUSB driver for the DYMO LabelWriter 450 (raster mode).
//
// Protocol verified against the CUPS DYMO driver (rastertolabel.c), DYMO's
// LW 450-series command manual, and the SYN raster-mode spec:
//   reset:        0x1B x100, then ESC @ (0x1B 0x40)
//   label length: ESC L (0x1B 0x4C) hi lo   — length in dots
//   bytes/line:   ESC D (0x1B 0x44) n        — n = 1..84
//   raster line:  SYN (0x16) + n data bytes  — MSB = leftmost dot, bit 1 = black
//   eject:        ESC E (0x1B 0x45)          — form feed to tear bar

export const DYMO_VENDOR_ID = 0x0922
export const DYMO_LW450_PRODUCT_ID = 0x0020
export const DPI = 300
export const MAX_BYTES_PER_LINE = 84

const ESC = 0x1b
const SYN = 0x16

/** Millimetres to printer dots at the head's native resolution. */
export const mmToDots = (mm: number) => Math.round((mm / 25.4) * DPI)

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

/**
 * Pack canvas pixels into 1-bit raster lines, MSB = leftmost dot, bit 1 = black.
 * A pixel counts as black when its luminance is below `threshold` (0..255).
 * Width is rounded up to a whole number of bytes.
 */
export function packToRaster(
  image: ImageData,
  threshold = 128,
): { bytesPerLine: number; lines: Uint8Array } {
  const { width, height, data } = image
  const bytesPerLine = Math.ceil(width / 8)
  const lines = new Uint8Array(bytesPerLine * height)

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const i = (y * width + x) * 4
      // Luminance; treat transparent pixels as white.
      const a = data[i + 3]
      const lum = a === 0 ? 255 : (data[i] + data[i + 1] + data[i + 2]) / 3
      if (lum < threshold) {
        lines[y * bytesPerLine + (x >> 3)] |= 0x80 >> (x & 7)
      }
    }
  }
  return { bytesPerLine, lines }
}

/**
 * Where to place the label bitmap on the print head (physical calibration),
 * in dots. The label is composited onto a full-head-width bitmap at this
 * position — pure pixels, so it always moves (unlike the ESC B dot-tab, which
 * the LabelWriter 450 ignores in practice).
 */
export interface HeadOffset {
  x?: number // left, dots
  y?: number // top, dots
}

/** Build the command stream for one label from packed full-width raster lines. */
export function buildJob(
  lines: Uint8Array,
  bytesPerLine: number,
  height: number,
): Uint8Array {
  if (bytesPerLine > MAX_BYTES_PER_LINE) {
    throw new Error(`bytesPerLine ${bytesPerLine} exceeds head max ${MAX_BYTES_PER_LINE}`)
  }
  const out: number[] = []

  // Reset: flush any partial command, then ESC @.
  for (let i = 0; i < 100; i++) out.push(ESC)
  out.push(ESC, 0x40)

  // ESC L hi lo — label length in dots.
  out.push(ESC, 0x4c, (height >> 8) & 0xff, height & 0xff)
  // ESC D n — bytes per line.
  out.push(ESC, 0x44, bytesPerLine)

  // One SYN + data per raster line.
  for (let y = 0; y < height; y++) {
    out.push(SYN)
    const start = y * bytesPerLine
    for (let b = 0; b < bytesPerLine; b++) out.push(lines[start + b])
  }

  // ESC E — form feed / eject to tear bar.
  out.push(ESC, 0x45)
  return Uint8Array.from(out)
}

/**
 * Composite a label-sized canvas onto a full-head-width bitmap at the head
 * offset, pack it, and print on an opened DYMO. ep#2 is bulk OUT. The label
 * canvas is expected to already be 1-bit (the renderer thresholds it), so a
 * mid threshold here is just a safety net.
 */
export async function printCanvas(
  device: UsbDevice,
  canvas: HTMLCanvasElement,
  offset: HeadOffset = {},
  threshold = 128,
): Promise<void> {
  const x = Math.max(0, Math.round(offset.x ?? 0))
  const y = Math.max(0, Math.round(offset.y ?? 0))

  const head = document.createElement("canvas")
  head.width = MAX_BYTES_PER_LINE * 8 // full head width (672 dots)
  head.height = canvas.height + y
  const ctx = head.getContext("2d")
  if (!ctx) throw new Error("no 2d context")
  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, head.width, head.height)
  ctx.drawImage(canvas, x, y)

  const image = ctx.getImageData(0, 0, head.width, head.height)
  const { bytesPerLine, lines } = packToRaster(image, threshold)
  const job = buildJob(lines, bytesPerLine, head.height)
  const result = await device.transferOut(2, job)
  if (result.status !== "ok") {
    throw new Error(`transferOut status: ${result.status}`)
  }
}

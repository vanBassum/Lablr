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
  open(): Promise<void>
  close(): Promise<void>
  selectConfiguration(value: number): Promise<void>
  claimInterface(n: number): Promise<void>
  releaseInterface(n: number): Promise<void>
  transferOut(endpointNumber: number, data: BufferSource): Promise<UsbOutTransferResult>
  configuration: unknown
}
interface Usb {
  requestDevice(opts: {
    filters: Array<{ vendorId?: number; productId?: number }>
  }): Promise<UsbDevice>
}

function getUsb(): Usb {
  const usb = (navigator as unknown as { usb?: Usb }).usb
  if (!usb) throw new Error("WebUSB unavailable — use Chrome/Edge on localhost or HTTPS.")
  return usb
}

/** Prompt for the DYMO, then open + claim interface 0 ready for output. */
export async function openDymo(): Promise<UsbDevice> {
  const device = await getUsb().requestDevice({
    filters: [{ vendorId: DYMO_VENDOR_ID }],
  })
  await device.open()
  if (device.configuration === null) await device.selectConfiguration(1)
  await device.claimInterface(0)
  return device
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

/** Build the full command stream for one label from packed raster lines. */
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

  // One SYN + data per raster line. (No blank-line skipping yet — simplest first.)
  for (let y = 0; y < height; y++) {
    out.push(SYN)
    const start = y * bytesPerLine
    for (let b = 0; b < bytesPerLine; b++) out.push(lines[start + b])
  }

  // ESC E — form feed / eject to tear bar.
  out.push(ESC, 0x45)
  return Uint8Array.from(out)
}

/** Render a canvas, pack it, and print it on an opened DYMO. ep#2 is bulk OUT. */
export async function printCanvas(
  device: UsbDevice,
  canvas: HTMLCanvasElement,
  threshold = 128,
): Promise<void> {
  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")
  const image = ctx.getImageData(0, 0, canvas.width, canvas.height)
  const { bytesPerLine, lines } = packToRaster(image, threshold)
  const job = buildJob(lines, bytesPerLine, canvas.height)
  const result = await device.transferOut(2, job)
  if (result.status !== "ok") {
    throw new Error(`transferOut status: ${result.status}`)
  }
}

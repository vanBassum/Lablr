import { mmToDots } from "@/dymo"
import type { Media, Orientation, Template } from "./types"

// 1-bit cutoff. High so anti-aliased thin lines/text edges round to solid black
// rather than printing faint. Tune here to make everything lighter/darker.
const INK_THRESHOLD = 176

/**
 * Collapse the canvas to pure black/white in place, so the preview shows
 * exactly what prints (preview = print) and thin AA lines come out solid.
 */
function thresholdTo1Bit(
  ctx: CanvasRenderingContext2D,
  w: number,
  h: number,
): void {
  const img = ctx.getImageData(0, 0, w, h)
  const d = img.data
  for (let i = 0; i < d.length; i += 4) {
    const lum = d[i + 3] === 0 ? 255 : (d[i] + d[i + 1] + d[i + 2]) / 3
    const v = lum < INK_THRESHOLD ? 0 : 255
    d[i] = d[i + 1] = d[i + 2] = v
    d[i + 3] = 255
  }
  ctx.putImageData(img, 0, 0)
}

/**
 * Parse a coordinate value (number in mm or string like "50%" of media width/height).
 */
function parseCoord(value: number | string, mediaSize: number): number {
  if (typeof value === "number") return value
  if (typeof value === "string" && value.endsWith("%")) {
    const percent = parseFloat(value)
    return (percent / 100) * mediaSize
  }
  return parseFloat(value)
}

/**
 * Measure text to see if it fits in the given box (in canvas dots).
 * Returns true if it fits, false if it doesn't (or would need to wrap).
 */
function textFits(
  ctx: CanvasRenderingContext2D,
  text: string,
  boxW: number,
  boxH: number,
  wrap: boolean,
  maxLines?: number,
): boolean {
  if (!wrap) {
    // Single-line text: check if it fits horizontally
    const m = ctx.measureText(text)
    return m.width <= boxW
  }

  // Wrapped text: split into lines and check if they fit
  const lines = wrapText(ctx, text, boxW)
  if (maxLines && lines.length > maxLines) return false

  const lineHeight = Math.ceil(ctx.measureText("M").actualBoundingBoxAscent + ctx.measureText("M").actualBoundingBoxDescent)
  const totalHeight = lines.length * lineHeight
  return totalHeight <= boxH
}

/**
 * Wrap text into lines that fit the given width (in canvas dots).
 * Uses binary search on characters to keep lines packed.
 */
function wrapText(ctx: CanvasRenderingContext2D, text: string, maxWidth: number): string[] {
  const lines: string[] = []
  let remaining = text

  while (remaining) {
    // Find how many characters fit on this line
    let fit = 0
    for (let i = 1; i <= remaining.length; i++) {
      const m = ctx.measureText(remaining.slice(0, i))
      if (m.width > maxWidth) {
        fit = i - 1
        break
      }
      fit = i
    }

    if (fit === 0) {
      // Even one character doesn't fit; take it anyway to avoid infinite loop
      fit = 1
    }

    // Break at word boundary (space) if possible, otherwise break at character limit
    let breakAt = fit
    const chunk = remaining.slice(0, fit)
    const lastSpace = chunk.lastIndexOf(" ")
    if (lastSpace > 0) {
      // Always prefer breaking at a space
      breakAt = lastSpace
    }
    // else: no space found, break at character limit (unavoidable)

    lines.push(remaining.slice(0, breakAt).trim())
    remaining = remaining.slice(breakAt).trim()
  }

  return lines
}

/**
 * Render template on media.
 * Template elements use percentages (5%, 50%, etc.) of media width/height.
 * Canvas size = media size (in dots). No scaling needed, just percentages.
 * Landscape swaps media dimensions upfront.
 */
export function renderLabel(
  canvas: HTMLCanvasElement,
  template: Template,
  fieldValues: Record<string, string>,
  media: Media,
  orientation: Orientation = "portrait",
): void {
  // Media dimensions (landscape swaps them)
  const landscape = orientation === "landscape"
  const mediaW = landscape ? media.size.h : media.size.w
  const mediaH = landscape ? media.size.w : media.size.h

  // Canvas = media size in dots
  const Wd = mmToDots(mediaW)
  const Hd = mmToDots(mediaH)
  canvas.width = Wd
  canvas.height = Hd

  const ctx = canvas.getContext("2d")!
  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, Wd, Hd)
  ctx.fillStyle = "black"

  // Render elements
  for (const element of template.elements) {
    if (element.type !== "text") continue

    const text = element.field ? fieldValues[element.field] ?? "" : element.text ?? ""
    if (!text) continue

    // Element rect: percentages of media dimensions
    const elemMm = {
      x: parseCoord(element.rect.x, mediaW),
      y: parseCoord(element.rect.y, mediaH),
      w: parseCoord(element.rect.width, mediaW),
      h: parseCoord(element.rect.height, mediaH),
    }

    // Convert to canvas dots
    const boxX = mmToDots(elemMm.x)
    const boxY = mmToDots(elemMm.y)
    const boxW = mmToDots(elemMm.w)
    const boxH = mmToDots(elemMm.h)

    // Auto-fit: binary search for largest font that fits
    const maxFontSize = mmToDots(element.font.maxSize)
    const minFontSize = mmToDots(element.font.minSize)
    let fontSize = maxFontSize

    while (fontSize >= minFontSize) {
      ctx.font = `${element.font.weight === "bold" ? "bold " : ""}${fontSize}px sans-serif`
      if (textFits(ctx, text, boxW, boxH, element.wrap ?? false, element.maxLines)) {
        break
      }
      fontSize -= 1
    }

    if (fontSize < minFontSize) continue

    // Render text with alignment
    ctx.font = `${element.font.weight === "bold" ? "bold " : ""}${fontSize}px sans-serif`
    ctx.textBaseline = "top"

    const align = element.align ?? "center"
    const valign = element.valign ?? "center"
    ctx.textAlign = align

    // Horizontal alignment
    let textX = boxX
    if (align === "center") textX += boxW / 2
    else if (align === "right") textX += boxW

    // Vertical alignment
    const metrics = ctx.measureText("M")
    const lineHeight = Math.ceil(metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent)
    const lines = (element.wrap ?? false) ? wrapText(ctx, text, boxW) : [text]
    const totalHeight = lines.length * lineHeight

    let textY = boxY
    if (valign === "center") textY += Math.max(0, (boxH - totalHeight) / 2)
    else if (valign === "bottom") textY += Math.max(0, boxH - totalHeight)

    // Draw
    lines.forEach((line, i) => {
      ctx.fillText(line, textX, textY + i * lineHeight)
    })
  }

  thresholdTo1Bit(ctx, Wd, Hd)
}

/**
 * An alignment test pattern for calibrating a media's head offset: a full-edge
 * border, a corner-to-corner cross, and a center crosshair, sized to the media.
 * Print it, see where it lands on the physical label, then nudge the offset.
 */
export function renderCalibration(canvas: HTMLCanvasElement, media: Media): void {
  const W = mmToDots(media.size.w)
  const H = mmToDots(media.size.h)
  canvas.width = W
  canvas.height = H

  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, W, H)
  ctx.strokeStyle = "black"
  ctx.fillStyle = "black"

  // Border hugging the label edges.
  ctx.lineWidth = 3
  ctx.strokeRect(1.5, 1.5, W - 3, H - 3)

  // Corner-to-corner cross.
  ctx.lineWidth = 2
  ctx.beginPath()
  ctx.moveTo(0, 0)
  ctx.lineTo(W, H)
  ctx.moveTo(W, 0)
  ctx.lineTo(0, H)
  ctx.stroke()

  // Center crosshair.
  const cx = W / 2
  const cy = H / 2
  const r = mmToDots(3)
  ctx.beginPath()
  ctx.moveTo(cx - r, cy)
  ctx.lineTo(cx + r, cy)
  ctx.moveTo(cx, cy - r)
  ctx.lineTo(cx, cy + r)
  ctx.stroke()

  // Size label, top-left.
  ctx.font = `${mmToDots(3)}px sans-serif`
  ctx.textBaseline = "top"
  ctx.textAlign = "left"
  ctx.fillText(`${media.size.w}×${media.size.h}`, mmToDots(2), mmToDots(2))

  thresholdTo1Bit(ctx, W, H)
}

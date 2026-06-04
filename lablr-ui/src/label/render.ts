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
 * Map mm coordinates from the design space to canvas dots.
 * Assumes canvas transforms have already been applied for rotation/centering.
 */
function designToCanvasDots(
  designMm: { x: number; y: number; w: number; h: number },
  designSize: { width: number; height: number },
  media: { w: number; h: number },
): { x: number; y: number; w: number; h: number } {
  // Contain scaling: fit design into media without clipping
  const scale = Math.min(media.w / designSize.width, media.h / designSize.height)

  // Convert mm to canvas dots (accounting for scaling)
  const x = mmToDots(designMm.x * scale)
  const y = mmToDots(designMm.y * scale)
  const w = mmToDots(designMm.w * scale)
  const h = mmToDots(designMm.h * scale)

  return { x, y, w, h }
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
 * Render a template + draft values onto `canvas` as a 1-bit-ready black/white bitmap.
 * The canvas is always the MEDIA's physical size. The template's designSize is scaled
 * to fit the media using "contain" (aspect ratio preserved, centered). `orientation`
 * decides whether the design is upright or rotated 90° within the label.
 */
export function renderLabel(
  canvas: HTMLCanvasElement,
  template: Template,
  fieldValues: Record<string, string>,
  media: Media,
  orientation: Orientation = "portrait",
): void {
  const landscape = orientation === "landscape"

  // In landscape, media dimensions are swapped (54×70 → 70×54)
  const mediaW = landscape ? media.size.h : media.size.w
  const mediaH = landscape ? media.size.w : media.size.h

  const Wd = mmToDots(mediaW)
  const Hd = mmToDots(mediaH)
  canvas.width = Wd
  canvas.height = Hd

  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, Wd, Hd)
  ctx.fillStyle = "black"

  // Use designSize if provided, otherwise template is responsive (fills media)
  const isResponsive = !template.designSize

  const designSize = template.designSize || { width: mediaW, height: mediaH }
  const designW = isResponsive ? mediaW : designSize.width
  const designH = isResponsive ? mediaH : designSize.height

  // Contain scaling: fit design into media
  const scale = isResponsive ? 1 : Math.min(mediaW / designW, mediaH / designH)
  const scaledW = designW * scale
  const scaledH = designH * scale

  // Center the design on the media (in mm)
  const offsetMm = {
    x: (mediaW - scaledW) / 2,
    y: (mediaH - scaledH) / 2,
  }

  // Just translate to the design offset (no rotation needed)
  ctx.save()
  ctx.translate(mmToDots(offsetMm.x), mmToDots(offsetMm.y))

  // For each element, auto-fit text and render it (in design space, pre-scaled)
  for (const element of template.elements) {
    if (element.type !== "text") continue

    // Get the text to render
    const text = element.field ? fieldValues[element.field] ?? "" : element.text ?? ""
    if (!text) continue

    // Parse element coordinates (supports both mm and percentage)
    // For responsive templates in landscape, percentages are still based on original media
    const elemMm = {
      x: parseCoord(element.rect.x, landscape && isResponsive ? media.size.h : media.size.w),
      y: parseCoord(element.rect.y, landscape && isResponsive ? media.size.w : media.size.h),
      w: parseCoord(element.rect.width, landscape && isResponsive ? media.size.h : media.size.w),
      h: parseCoord(element.rect.height, landscape && isResponsive ? media.size.w : media.size.h),
    }

    // Map element rect from design space to canvas dots (accounting for contain scaling)
    const rect = designToCanvasDots(
      elemMm,
      template.designSize || { width: mediaW, height: mediaH },
      { w: mediaW, h: mediaH },
    )

    const boxX = rect.x
    const boxY = rect.y
    const boxW = rect.w
    const boxH = rect.h

    // Binary search for the largest font size that fits
    let fontSize = mmToDots(element.font.maxSize)
    let minFontSize = mmToDots(element.font.minSize)

    while (fontSize >= minFontSize) {
      const fontStr = `${element.font.weight === "bold" ? "bold " : ""}${fontSize}px sans-serif`
      ctx.font = fontStr

      if (textFits(ctx, text, boxW, boxH, element.wrap ?? false, element.maxLines)) {
        break
      }
      fontSize -= 1
    }

    if (fontSize < minFontSize) continue // Give up if we can't fit even at min size

    // Render the text
    const fontStr = `${element.font.weight === "bold" ? "bold " : ""}${fontSize}px sans-serif`
    ctx.font = fontStr
    ctx.textBaseline = "top"

    const align = element.align ?? "center"
    const valign = element.valign ?? "center"
    ctx.textAlign = align

    // Horizontal position
    let textX = boxX
    if (align === "center") textX += boxW / 2
    else if (align === "right") textX += boxW

    // Vertical position (estimate line height)
    const metrics = ctx.measureText("M")
    const lineHeight = Math.ceil(metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent)

    const lines = (element.wrap ?? false) ? wrapText(ctx, text, boxW) : [text]
    const totalHeight = lines.length * lineHeight

    let textY = boxY
    if (valign === "center") textY += Math.max(0, (boxH - totalHeight) / 2)
    else if (valign === "bottom") textY += Math.max(0, boxH - totalHeight)

    // Render each line
    lines.forEach((line, i) => {
      ctx.fillText(line, textX, textY + i * lineHeight)
    })
  }

  ctx.restore()
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

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
 * Map mm coordinates from the design space to the media, applying contain scaling.
 * In landscape, the design is rotated 90° within the physical media.
 */
function designToMedia(
  designMm: { x: number; y: number; w: number; h: number },
  designSize: { width: number; height: number },
  media: { w: number; h: number },
  landscape: boolean,
): { x: number; y: number; w: number; h: number } {
  // In landscape, design dimensions are swapped, then rotated onto the media.
  const designW = landscape ? designSize.height : designSize.width
  const designH = landscape ? designSize.width : designSize.height

  // Contain scaling: fit design into media without clipping
  const scale = Math.min(media.w / designW, media.h / designH)

  // Scaled design size
  const scaledW = designW * scale
  const scaledH = designH * scale

  // Center the scaled design on the media
  const offsetX = (media.w - scaledW) / 2
  const offsetY = (media.h - scaledH) / 2

  if (!landscape) {
    // Portrait: direct scaling and centering
    return {
      x: offsetX + designMm.x * scale,
      y: offsetY + designMm.y * scale,
      w: designMm.w * scale,
      h: designMm.h * scale,
    }
  }

  // Landscape: rotate design 90° CCW, then translate to center
  // Point (x, y) in the rotated space is at (y, designW - x) in the original space
  const origX = designMm.y
  const origY = designW - (designMm.x + designMm.w)
  return {
    x: offsetX + origX * scale,
    y: offsetY + origY * scale,
    w: designMm.h * scale,
    h: designMm.w * scale,
  }
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

    // Try to break at a word boundary
    let breakAt = fit
    const chunk = remaining.slice(0, fit)
    const lastSpace = chunk.lastIndexOf(" ")
    if (lastSpace > 0 && lastSpace > fit * 0.6) {
      breakAt = lastSpace
    }

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
  const Wd = mmToDots(media.size.w)
  const Hd = mmToDots(media.size.h)
  canvas.width = Wd
  canvas.height = Hd

  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, Wd, Hd)
  ctx.fillStyle = "black"

  const landscape = orientation === "landscape"

  // For each element, auto-fit text and render it
  for (const element of template.elements) {
    if (element.type !== "text") continue

    // Get the text to render
    const text = element.field ? fieldValues[element.field] ?? "" : element.text ?? ""
    if (!text) continue

    // Map element rect from design space to media space (in mm)
    const rectMm = designToMedia(
      { x: element.rect.x, y: element.rect.y, w: element.rect.width, h: element.rect.height },
      template.designSize,
      media.size,
      landscape,
    )

    // Convert to canvas dots
    const boxX = mmToDots(rectMm.x)
    const boxY = mmToDots(rectMm.y)
    const boxW = mmToDots(rectMm.w)
    const boxH = mmToDots(rectMm.h)

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

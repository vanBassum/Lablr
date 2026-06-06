import type { Draft, LabelStock, Orientation, Printer, TemplateElement } from "@/types"
import { getPictogram } from "@/services/pictograms"

export interface RenderInput {
  draft: Draft
  elements: TemplateElement[]
  orientation: Orientation
  stock: LabelStock
  printer: Printer
}

/**
 * The single renderer. Produces the bitmap used for BOTH preview and print.
 * No debug borders, no margin guides, no offset correction are baked in here —
 * those would corrupt the print payload. The print-head offset (stock
 * calibration + manual nudge) is applied separately at print time.
 */
export class RenderService {
  render(canvas: HTMLCanvasElement, input: RenderInput): void {
    const { draft, elements, orientation, stock, printer } = input

    const ctx = canvas.getContext("2d")
    if (!ctx) throw new Error("Could not get canvas context")

    // Stock is authored portrait; landscape rotates it, so swap dimensions.
    const isLandscape = orientation === "landscape"
    const stockW = isLandscape ? stock.heightMm : stock.widthMm
    const stockH = isLandscape ? stock.widthMm : stock.heightMm

    const mmToDots = printer.dpi / 25.4
    canvas.width = Math.round(stockW * mmToDots)
    canvas.height = Math.round(stockH * mmToDots)

    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, canvas.width, canvas.height)
    ctx.fillStyle = "black"

    // Margins define a safe area (a blank border). TEXT boxes are clamped to it
    // so text shrinks to fit within the border. Pictograms are graphics placed by
    // the template, so they keep their authored size (clamping would resize them
    // unevenly) — position them inside the margins in the template if needed.
    const m = stock.marginsMm
    const safeX0 = (m?.left ?? 0) * mmToDots
    const safeY0 = (m?.top ?? 0) * mmToDots
    const safeX1 = canvas.width - (m?.right ?? 0) * mmToDots
    const safeY1 = canvas.height - (m?.bottom ?? 0) * mmToDots

    for (const el of elements) {
      const value = draft.fields[el.field] ?? ""
      if (!value) continue

      if (el.type === "pictogram") {
        drawPictogram(
          ctx,
          value,
          el.rect.x * mmToDots,
          el.rect.y * mmToDots,
          el.rect.width * mmToDots,
          el.rect.height * mmToDots,
        )
        continue
      }

      // Text: clamp the box to the safe area so shrink-to-fit keeps it inside.
      const x0 = Math.max(el.rect.x * mmToDots, safeX0)
      const y0 = Math.max(el.rect.y * mmToDots, safeY0)
      const x1 = Math.min((el.rect.x + el.rect.width) * mmToDots, safeX1)
      const y1 = Math.min((el.rect.y + el.rect.height) * mmToDots, safeY1)
      if (x1 - x0 <= 0 || y1 - y0 <= 0) continue

      drawText(ctx, value, el, x0, y0, x1 - x0, y1 - y0, mmToDots)
    }
  }
}

function drawPictogram(
  ctx: CanvasRenderingContext2D,
  name: string,
  rectX: number,
  rectY: number,
  rectW: number,
  rectH: number,
): void {
  const img = getPictogram(name)
  if (!img) return // unknown or not yet loaded — a re-render draws it once ready
  // Fit square, centered, preserving the symbol's aspect ratio.
  const side = Math.min(rectW, rectH)
  const dx = rectX + (rectW - side) / 2
  const dy = rectY + (rectH - side) / 2
  ctx.drawImage(img, dx, dy, side, side)
}

function drawText(
  ctx: CanvasRenderingContext2D,
  value: string,
  el: TemplateElement,
  rectX: number,
  rectY: number,
  rectW: number,
  rectH: number,
  mmToDots: number,
): void {
  const { size, lines, lineH } = fitText(ctx, value, el, rectW, rectH, mmToDots)
  if (size <= 0) return

  ctx.font = fontString(el, size)
  ctx.textBaseline = "top"
  ctx.textAlign = el.align ?? "left"

  const x =
    el.align === "center" ? rectX + rectW / 2 : el.align === "right" ? rectX + rectW : rectX

  const blockH = lines.length * lineH
  const startY =
    el.valign === "center"
      ? rectY + (rectH - blockH) / 2
      : el.valign === "bottom"
        ? rectY + rectH - blockH
        : rectY

  lines.forEach((line, i) => ctx.fillText(line, x, startY + i * lineH))
}

function fontString(el: TemplateElement, sizePx: number): string {
  const weight = el.font?.weight === "bold" ? "bold " : ""
  return `${weight}${sizePx}px sans-serif`
}

const LINE_HEIGHT = 1.2

/**
 * Shrink-to-fit using real canvas metrics. Steps the font size down from
 * maxSizeMm toward minSizeMm until the text fits the rectangle (width, and
 * height/maxLines once wrapped). `fit: "none"` renders at maxSizeMm.
 */
function fitText(
  ctx: CanvasRenderingContext2D,
  text: string,
  el: TemplateElement,
  rectW: number,
  rectH: number,
  mmToDots: number,
): { size: number; lines: string[]; lineH: number } {
  const maxPx = (el.font?.maxSizeMm ?? 3) * mmToDots
  const minPx = (el.font?.minSizeMm ?? 1.5) * mmToDots
  const wrap = !!el.wrap
  const maxLines = el.maxLines

  // minSizeMm is the *preferred* floor. shrink-to-fit may go below it (down to a
  // tiny absolute floor) so text fits rather than overflowing/being clipped.
  const ABS_FLOOR = 0.8 * mmToDots
  const floorPx = el.fit === "shrink" ? Math.min(minPx, ABS_FLOOR) : minPx

  // Assumes ctx.font is already set to the size being measured.
  const layout = () => (wrap ? wrapText(ctx, text, rectW) : [text])

  if (el.fit !== "shrink") {
    ctx.font = fontString(el, maxPx)
    return { size: maxPx, lines: clampLines(layout(), maxLines), lineH: maxPx * LINE_HEIGHT }
  }

  for (let size = maxPx; size >= floorPx; size -= 0.5) {
    ctx.font = fontString(el, size)
    const lines = layout()
    const widthOk = lines.every((l) => ctx.measureText(l).width <= rectW)
    const linesOk = !maxLines || lines.length <= maxLines
    const heightOk = lines.length * size * LINE_HEIGHT <= rectH
    if (widthOk && linesOk && heightOk) return { size, lines, lineH: size * LINE_HEIGHT }
  }

  ctx.font = fontString(el, floorPx)
  return { size: floorPx, lines: clampLines(layout(), maxLines), lineH: floorPx * LINE_HEIGHT }
}

/** Cap to maxLines, ellipsising the last kept line if content was dropped. */
function clampLines(lines: string[], maxLines?: number): string[] {
  if (!maxLines || lines.length <= maxLines) return lines
  const kept = lines.slice(0, maxLines)
  kept[kept.length - 1] = kept[kept.length - 1].replace(/\s+\S*$/, "") + "…"
  return kept
}

/** Greedy word wrap; a single word wider than maxW is kept on its own line. */
function wrapText(ctx: CanvasRenderingContext2D, text: string, maxW: number): string[] {
  const words = text.split(/\s+/).filter(Boolean)
  if (words.length === 0) return [text]
  const lines: string[] = []
  let current = words[0]
  for (let i = 1; i < words.length; i++) {
    const candidate = `${current} ${words[i]}`
    if (ctx.measureText(candidate).width <= maxW) {
      current = candidate
    } else {
      lines.push(current)
      current = words[i]
    }
  }
  lines.push(current)
  return lines
}

export const renderService = new RenderService()

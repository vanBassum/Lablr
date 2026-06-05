import type { Draft, LabelStock, Orientation, Printer, TemplateElement } from "@/types"

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

    for (const el of elements) {
      if (el.type !== "text") continue
      const value = draft.fields[el.field] ?? ""
      if (!value) continue

      const rectX = el.rect.x * mmToDots
      const rectY = el.rect.y * mmToDots
      const rectW = el.rect.width * mmToDots
      const rectH = el.rect.height * mmToDots

      const { size, lines, lineH } = fitText(ctx, value, el, rectW, rectH, mmToDots)
      if (size <= 0) continue

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
  }
}

function fontString(el: TemplateElement, sizePx: number): string {
  const weight = el.font?.weight === "bold" ? "bold " : ""
  return `${weight}${sizePx}px sans-serif`
}

const LINE_HEIGHT = 1.2

/**
 * Shrink-to-fit using real canvas metrics. Steps the font size down from
 * maxSizeMm toward minSizeMm until the text fits the rectangle (width, and
 * height once wrapped). `fit: "none"` renders at maxSizeMm and may overflow.
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

  if (el.fit !== "shrink") {
    ctx.font = fontString(el, maxPx)
    const lines = wrap ? wrapText(ctx, text, rectW) : [text]
    return { size: maxPx, lines, lineH: maxPx * LINE_HEIGHT }
  }

  for (let size = maxPx; size >= minPx; size -= 0.5) {
    ctx.font = fontString(el, size)
    const lines = wrap ? wrapText(ctx, text, rectW) : [text]
    const widthOk = lines.every((l) => ctx.measureText(l).width <= rectW)
    const heightOk = lines.length * size * LINE_HEIGHT <= rectH
    if (widthOk && heightOk) return { size, lines, lineH: size * LINE_HEIGHT }
  }

  ctx.font = fontString(el, minPx)
  const lines = wrap ? wrapText(ctx, text, rectW) : [text]
  return { size: minPx, lines, lineH: minPx * LINE_HEIGHT }
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

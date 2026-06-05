import type { Draft, Template, Label, Printer } from "@/types"

export class RenderService {
  /**
   * Render a draft to a canvas using a template.
   */
  render(
    canvas: HTMLCanvasElement,
    draft: Draft,
    template: Template,
    label: Label,
    printer: Printer,
  ) {
    const ctx = canvas.getContext("2d")
    if (!ctx) throw new Error("Could not get canvas context")

    // Step 1: Get label dimensions
    let widthMm = label.widthMm
    let heightMm = label.heightMm

    // Step 2: Apply template orientation (portrait = swap, landscape = keep)
    if (template.orientation === "portrait") {
      ;[widthMm, heightMm] = [heightMm, widthMm]
    }

    // Step 3: Get DPI and conversion factor
    const mmToDots = printer.dpi / 25.4
    const widthPixels = widthMm * mmToDots
    const heightPixels = heightMm * mmToDots

    // Step 4: Set canvas size
    canvas.width = widthPixels
    canvas.height = heightPixels

    // Step 5: Draw white background
    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, widthPixels, heightPixels)

    // Step 6: Apply margins and offset corrections
    const margins = label.marginsMm || { top: 0, left: 0, right: 0, bottom: 0 }
    const offset = label.offsetCorrectionMm || { x: 0, y: 0 }

    // Step 7: Render each element
    ctx.fillStyle = "black"

    for (const element of template.elements) {
      if (element.type === "text") {
        const fieldValue = draft.fields[element.field] || ""
        if (!fieldValue) continue

        // Convert rectangle from mm to pixels
        const rectX = (element.rect.x + offset.x) * mmToDots
        const rectY = (element.rect.y + offset.y) * mmToDots
        const rectW = element.rect.width * mmToDots
        const rectH = element.rect.height * mmToDots

        // Fit text to rectangle
        const fontSize = this.fitTextToRectangle(
          ctx,
          fieldValue,
          element.font.maxSizeMm * mmToDots,
          element.font.minSizeMm * mmToDots,
          rectW,
          rectH,
          element.font.weight,
        )

        if (fontSize <= 0) continue

        ctx.font = `${element.font.weight === "bold" ? "bold " : ""}${fontSize}px monospace`
        ctx.textAlign = element.align
        ctx.textBaseline = element.valign === "center" ? "middle" : element.valign

        // Calculate position within rectangle based on alignment
        let x = rectX
        if (element.align === "center") {
          x = rectX + rectW / 2
        } else if (element.align === "right") {
          x = rectX + rectW
        }

        let y = rectY
        if (element.valign === "center") {
          y = rectY + rectH / 2
        } else if (element.valign === "bottom") {
          y = rectY + rectH
        } else {
          // "top" - baseline at top
          y = rectY + fontSize * 0.8
        }

        ctx.fillText(fieldValue, x, y)
      }
    }

    // Step 8: Draw border for debugging
    ctx.strokeStyle = "black"
    ctx.lineWidth = 1
    ctx.strokeRect(0, 0, widthPixels, heightPixels)
  }

  private fitTextToRectangle(
    ctx: CanvasRenderingContext2D,
    text: string,
    maxSizePixels: number,
    minSizePixels: number,
    widthPixels: number,
    heightPixels: number,
    weight: string,
  ): number {
    let size = Math.min(maxSizePixels, heightPixels * 0.9)
    const step = 0.5

    while (size >= minSizePixels) {
      ctx.font = `${weight === "bold" ? "bold " : ""}${size}px monospace`
      const metrics = ctx.measureText(text)
      const textWidth = metrics.width
      const textHeight =
        (metrics.fontBoundingBoxAscent || 0) + (metrics.fontBoundingBoxDescent || size)

      if (textWidth <= widthPixels * 0.95 && textHeight <= heightPixels * 0.95) {
        return size
      }

      size -= step
    }

    return minSizePixels
  }
}

export const renderService = new RenderService()

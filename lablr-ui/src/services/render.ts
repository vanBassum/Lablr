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

    // Step 2: Apply template orientation
    if (template.orientation === "landscape") {
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
        ctx.textBaseline = element.valign

        // Calculate position within rectangle based on alignment
        const x = rectX + (element.align === "left" ? 0 : element.align === "right" ? rectW : rectW / 2)
        const y = rectY + (element.valign === "top" ? fontSize : element.valign === "bottom" ? rectH : rectH / 2)

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
    let size = maxSizePixels
    const step = 0.5

    while (size >= minSizePixels) {
      ctx.font = `${weight === "bold" ? "bold " : ""}${size}px monospace`
      const metrics = ctx.measureText(text)
      const textWidth = metrics.width
      const textHeight = size // Approximate height

      if (textWidth <= widthPixels && textHeight <= heightPixels) {
        return size
      }

      size -= step
    }

    return minSizePixels
  }
}

export const renderService = new RenderService()

import type { Draft, Template, Media, Printer } from "@/types"

export class RenderService {
  /**
   * Render a draft to a canvas using a template.
   */
  render(
    canvas: HTMLCanvasElement,
    draft: Draft,
    template: Template,
    media: Media,
    printer: Printer,
  ) {
    const ctx = canvas.getContext("2d")
    if (!ctx) throw new Error("Could not get canvas context")

    // Step 1: Get physical dimensions and convert to pixels
    let widthMm = media.widthMm
    let heightMm = media.heightMm

    // Step 2: Apply orientation
    if (template.orientation === "landscape") {
      ;[widthMm, heightMm] = [heightMm, widthMm]
    }

    // Step 3: Convert to pixels
    const mmToDots = printer.dpi / 25.4
    const widthPixels = widthMm * mmToDots
    const heightPixels = heightMm * mmToDots

    // Step 4: Set canvas size
    canvas.width = widthPixels
    canvas.height = heightPixels

    // Step 5: Draw white background
    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, widthPixels, heightPixels)

    // Step 6: Draw 1-bit black for text (for now, just render text in center)
    ctx.fillStyle = "black"
    ctx.textAlign = "center"
    ctx.textBaseline = "middle"

    // Simple demo: render all fields as text stacked vertically in center
    const fieldValues = Object.entries(draft.fields)
      .map(([, value]) => value)
      .join(" / ")

    const fontSize = Math.max(20, heightPixels / 3)
    ctx.font = `${fontSize}px monospace`

    ctx.fillText(fieldValues, widthPixels / 2, heightPixels / 2)

    // For debugging: draw a border
    ctx.strokeStyle = "black"
    ctx.lineWidth = 1
    ctx.strokeRect(0, 0, widthPixels, heightPixels)
  }
}

export const renderService = new RenderService()

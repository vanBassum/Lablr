import { RefObject, useMemo } from "react"
import { configService } from "@/services/config"
import type { Draft, Template } from "@/types"

export function LabelPreviewContainer({
  draft,
  template,
  canvasRef,
}: {
  draft: Draft
  template: Template
  canvasRef: RefObject<HTMLCanvasElement | null>
}) {
  // Calculate the correct aspect ratio
  const dimensions = useMemo(() => {
    const label = configService.getLabel(template.label)
    const printer = configService.getPrinter(template.printerId)

    if (!label || !printer) return { width: 300, height: 400 }

    let widthMm = label.widthMm
    let heightMm = label.heightMm

    // Apply orientation: portrait means swap, landscape means keep as-is
    if (template.orientation === "portrait") {
      ;[widthMm, heightMm] = [heightMm, widthMm]
    }

    // Convert to pixels
    const mmToDots = printer.dpi / 25.4
    const widthPixels = widthMm * mmToDots
    const heightPixels = heightMm * mmToDots

    // Scale to fit in reasonable preview size (max 300px width)
    const maxWidth = 300
    const scale = Math.min(1, maxWidth / widthPixels)

    return {
      width: Math.round(widthPixels * scale),
      height: Math.round(heightPixels * scale),
    }
  }, [template])

  return (
    <div
      className="flex items-center justify-center rounded-md bg-white p-1 shadow-sm ring-1 ring-black/10"
      style={{ width: `${dimensions.width}px`, height: `${dimensions.height}px` }}
    >
      <canvas
        ref={canvasRef}
        style={{
          width: "100%",
          height: "100%",
          imageRendering: "pixelated",
          display: "block",
        }}
      />
    </div>
  )
}

import { useEffect, useRef } from "react"
import { renderService } from "@/services/render"
import { configService } from "@/services/config"
import type { Draft } from "@/types"

export function DraftPreview({ draft, width = 100, height = 100 }: { draft: Draft; width?: number; height?: number }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    if (!canvasRef.current) return

    const compatibleTemplates = configService.getCompatibleTemplates(draft)
    const template = compatibleTemplates[0]

    if (!template) return

    const label = configService.getLabel(template.label)
    const printer = configService.getPrinter(template.printerId)

    if (!label || !printer) return

    // Create an offscreen canvas for rendering
    const offscreenCanvas = document.createElement("canvas")
    renderService.render(offscreenCanvas, draft, template, label, printer)

    // Draw the rendered label onto the preview canvas, scaled down
    const ctx = canvasRef.current.getContext("2d")
    if (!ctx) return

    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, width, height)

    const scale = Math.min(width / offscreenCanvas.width, height / offscreenCanvas.height)
    const scaledWidth = offscreenCanvas.width * scale
    const scaledHeight = offscreenCanvas.height * scale
    const x = (width - scaledWidth) / 2
    const y = (height - scaledHeight) / 2

    ctx.drawImage(offscreenCanvas, x, y, scaledWidth, scaledHeight)
  }, [draft])

  return <canvas ref={canvasRef} width={width} height={height} style={{ width: `${width}px`, height: `${height}px` }} />
}

import { useEffect, useRef } from "react"
import { renderService } from "@/services/render"
import { configService } from "@/services/config"
import { usePictogramsReady } from "@/services/pictograms"
import type { Draft } from "@/types"

export function DraftPreview({
  draft,
  width = 100,
  height = 100,
}: {
  draft: Draft
  width?: number
  height?: number
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const pictogramsReady = usePictogramsReady()

  useEffect(() => {
    if (!canvasRef.current) return

    const template = configService.getMatchingTemplates(draft)[0]
    if (!template) return

    const orientation = configService.getTemplateOrientations(template)[0]
    const stock = configService.getLabelStock(template.label)
    if (!stock) return

    const printer = configService.getPrinterForStock(stock)
    const elements = configService.getTemplateElements(template, orientation)

    // Render the real bitmap offscreen, then scale it into the thumbnail.
    const offscreen = document.createElement("canvas")
    renderService.render(offscreen, { draft, elements, orientation, stock, printer })

    const ctx = canvasRef.current.getContext("2d")
    if (!ctx) return

    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, width, height)

    const scale = Math.min(width / offscreen.width, height / offscreen.height)
    const scaledWidth = offscreen.width * scale
    const scaledHeight = offscreen.height * scale
    const x = (width - scaledWidth) / 2
    const y = (height - scaledHeight) / 2

    ctx.drawImage(offscreen, x, y, scaledWidth, scaledHeight)
  }, [draft, width, height, pictogramsReady])

  return (
    <canvas
      ref={canvasRef}
      width={width}
      height={height}
      style={{ width: `${width}px`, height: `${height}px` }}
    />
  )
}

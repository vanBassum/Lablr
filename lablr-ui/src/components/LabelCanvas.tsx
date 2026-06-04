import { useEffect, useRef, type RefObject } from "react"
import { renderLabel } from "@/label/render"
import type { Media, Template } from "@/label/types"

/**
 * A white-framed label preview, scaled so its longest edge is `maxEdgePx`.
 * Pass `canvasRef` when the caller needs the canvas (e.g. to print it).
 */
export function LabelCanvas({
  template,
  values,
  media,
  maxEdgePx,
  canvasRef,
}: {
  template: Template
  values: Record<string, string>
  media: Media
  maxEdgePx: number
  canvasRef?: RefObject<HTMLCanvasElement | null>
}) {
  const internal = useRef<HTMLCanvasElement>(null)
  const ref = canvasRef ?? internal

  useEffect(() => {
    if (ref.current) renderLabel(ref.current, template, values, media)
  }, [template, values, media, ref])

  const scale = Math.min(maxEdgePx / media.size.w, maxEdgePx / media.size.h)

  return (
    <div
      className="flex items-center justify-center rounded-md bg-white p-1 shadow-sm ring-1 ring-black/10"
      style={{ width: media.size.w * scale, height: media.size.h * scale }}
    >
      <canvas
        ref={ref}
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

import { useRef, type RefObject } from "react"

/**
 * A white-framed label preview canvas.
 * (Render pipeline to be designed)
 */
export function LabelCanvas({
  width = 300,
  height = 400,
  canvasRef,
}: {
  width?: number
  height?: number
  canvasRef?: RefObject<HTMLCanvasElement | null>
}) {
  const internal = useRef<HTMLCanvasElement>(null)
  const ref = canvasRef ?? internal

  return (
    <div
      className="flex items-center justify-center rounded-md bg-white p-1 shadow-sm ring-1 ring-black/10"
      style={{ width, height }}
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

import { useMemo } from "react"
import type { LabelStock, Orientation, Printer } from "@/types"

/**
 * Sizes the on-screen preview box to the label's aspect ratio and shows the
 * backend-rendered PNG (`src`). The backend is the single renderer, so this image
 * is the exact 1-bit bitmap that prints — CSS just scales it down to fit.
 */
export function LabelPreviewContainer({
  stock,
  orientation,
  printer,
  src,
}: {
  stock: LabelStock
  orientation: Orientation
  printer: Printer
  src: string
}) {
  const dimensions = useMemo(() => {
    const isLandscape = orientation === "landscape"
    const widthMm = isLandscape ? stock.heightMm : stock.widthMm
    const heightMm = isLandscape ? stock.widthMm : stock.heightMm

    const mmToDots = printer.dpi / 25.4
    const widthPixels = widthMm * mmToDots
    const heightPixels = heightMm * mmToDots

    const maxWidth = 300
    const scale = Math.min(1, maxWidth / widthPixels)
    return {
      width: Math.round(widthPixels * scale),
      height: Math.round(heightPixels * scale),
    }
  }, [stock, orientation, printer])

  return (
    <div
      className="flex items-center justify-center rounded-md bg-white p-1 shadow-sm ring-1 ring-black/10"
      style={{ width: `${dimensions.width}px`, height: `${dimensions.height}px` }}
    >
      <img
        src={src}
        alt="Label preview"
        style={{
          width: "100%",
          height: "100%",
          imageRendering: "pixelated",
          display: "block",
          objectFit: "contain",
        }}
      />
    </div>
  )
}

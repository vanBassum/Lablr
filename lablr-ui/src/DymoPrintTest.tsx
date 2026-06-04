import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas, MAX_BYTES_PER_LINE } from "@/dymo"

const LABEL_MM = 25
const LABEL_DOTS = mmToDots(LABEL_MM) // ~296 dots of content @ 300 DPI
const HEAD_DOTS = MAX_BYTES_PER_LINE * 8 // 672 — full print-head width

/**
 * Draw the asymmetric diagnostic pattern onto a full-head-width canvas,
 * translated by (offsetX, offsetY) dots. Offset lives in the rendered
 * bitmap, so the preview stays identical to what prints.
 */
function drawTestPattern(
  canvas: HTMLCanvasElement,
  offsetX: number,
  offsetY: number,
) {
  const ctx = canvas.getContext("2d")
  if (!ctx) return
  const s = LABEL_DOTS

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, canvas.width, canvas.height)

  ctx.save()
  ctx.translate(offsetX, offsetY)
  ctx.fillStyle = "black"
  ctx.strokeStyle = "black"

  // Border — shows whether all four edges land on the label.
  ctx.lineWidth = 6
  ctx.strokeRect(3, 3, s - 6, s - 6)

  // Filled triangle, TOP-LEFT — asymmetry marker, prints first (leading edge).
  ctx.beginPath()
  ctx.moveTo(3, 3)
  ctx.lineTo(s / 3, 3)
  ctx.lineTo(3, s / 3)
  ctx.closePath()
  ctx.fill()

  // Diagonal corner-to-corner — catches mirroring.
  ctx.lineWidth = 2
  ctx.beginPath()
  ctx.moveTo(0, 0)
  ctx.lineTo(s, s)
  ctx.stroke()

  // Text.
  ctx.font = "bold 38px sans-serif"
  ctx.textAlign = "center"
  ctx.fillText("LABLR", s / 2, s / 2)
  ctx.font = "22px sans-serif"
  ctx.fillText(`${LABEL_MM}mm`, s / 2, s / 2 + 30)
  ctx.restore()
}

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    if (canvasRef.current) drawTestPattern(canvasRef.current, offsetX, offsetY)
  }, [offsetX, offsetY])

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas) return
    setBusy(true)
    setStatus("Opening DYMO…")
    try {
      const device = await openDymo()
      setStatus("Sending raster…")
      await printCanvas(device, canvas)
      await device.releaseInterface(0)
      await device.close()
      setStatus(`✓ Printed at offset X=${offsetX} Y=${offsetY} dots.`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Print + alignment calibration (preview = print)</h2>
      <canvas
        ref={canvasRef}
        width={HEAD_DOTS}
        height={LABEL_DOTS}
        className="border"
        style={{
          width: 360,
          height: (360 * LABEL_DOTS) / HEAD_DOTS,
          imageRendering: "pixelated",
          background: "white",
        }}
      />
      <p className="text-muted-foreground text-xs">
        Full {HEAD_DOTS}-dot head width shown; the {LABEL_DOTS}-dot ({LABEL_MM}mm)
        label content is positioned by the offsets below.
      </p>

      <div className="flex flex-wrap items-end gap-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs">X offset (dots)</span>
          <input
            type="number"
            value={offsetX}
            step={8}
            onChange={(e) => setOffsetX(Number(e.target.value))}
            className="bg-background w-24 rounded border px-2 py-1 font-mono"
          />
        </label>
        <label className="flex flex-col gap-1">
          <span className="text-xs">Y offset (dots)</span>
          <input
            type="number"
            value={offsetY}
            step={8}
            onChange={(e) => setOffsetY(Number(e.target.value))}
            className="bg-background w-24 rounded border px-2 py-1 font-mono"
          />
        </label>
        <Button onClick={handlePrint} disabled={busy}>
          Print test label
        </Button>
      </div>

      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status ||
          "Nudge X/Y until the border frames the label, then tell me the values."}
      </pre>
    </div>
  )
}

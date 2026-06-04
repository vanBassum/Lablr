import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas } from "@/dymo"

const LABEL_MM = 25
const SIZE = mmToDots(LABEL_MM) // ~296 dots @ 300 DPI

/** Draw an asymmetric diagnostic pattern so orientation/scale errors are obvious. */
function drawTestPattern(canvas: HTMLCanvasElement) {
  const ctx = canvas.getContext("2d")
  if (!ctx) return
  const s = canvas.width

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, s, s)
  ctx.fillStyle = "black"
  ctx.strokeStyle = "black"

  // Full border — shows whether all four edges land on the label.
  ctx.lineWidth = 6
  ctx.strokeRect(3, 3, s - 6, s - 6)

  // Filled triangle in the TOP-LEFT corner — the asymmetry marker (prints first).
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
}

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    if (canvasRef.current) drawTestPattern(canvasRef.current)
  }, [])

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
      setStatus(`✓ Sent ${SIZE}×${SIZE} dots (${LABEL_MM}mm). Watch the printer.`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Print test (preview = print)</h2>
      <canvas
        ref={canvasRef}
        width={SIZE}
        height={SIZE}
        className="border"
        style={{
          width: 200,
          height: 200,
          imageRendering: "pixelated",
          background: "white",
        }}
      />
      <div>
        <Button onClick={handlePrint} disabled={busy}>
          Print test label to DYMO
        </Button>
      </div>
      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status || "The canvas above is the exact bitmap that will be printed."}
      </pre>
    </div>
  )
}

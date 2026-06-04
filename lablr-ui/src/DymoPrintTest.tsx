import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas, MAX_BYTES_PER_LINE } from "@/dymo"
import { renderLabel } from "@/label/render"
import { sampleDrafts, templateById } from "@/label/templates"

const HEAD_DOTS = MAX_BYTES_PER_LINE * 8

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [draftIndex, setDraftIndex] = useState(0)
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  const draft = sampleDrafts[draftIndex]
  const template = templateById(draft.templateId)

  useEffect(() => {
    if (canvasRef.current && template) {
      renderLabel(canvasRef.current, template, draft.values, { offsetX, offsetY })
    }
  }, [template, draft, offsetX, offsetY])

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
      setStatus(`✓ Printed "${draft.values.name}" (offset X=${offsetX} Y=${offsetY}).`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  if (!template) return <p>Unknown template: {draft.templateId}</p>

  const labelH = mmToDots(template.size.h)

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Render a draft → preview = print</h2>

      <label className="flex flex-col gap-1">
        <span className="text-xs">Test draft ({template.name})</span>
        <select
          value={draftIndex}
          onChange={(e) => setDraftIndex(Number(e.target.value))}
          className="bg-background w-64 rounded border px-2 py-1"
        >
          {sampleDrafts.map((d, i) => (
            <option key={i} value={i}>
              {d.values.name} — {d.values.subtitle}
            </option>
          ))}
        </select>
      </label>

      <canvas
        ref={canvasRef}
        className="border"
        style={{
          width: 360,
          height: (360 * labelH) / HEAD_DOTS,
          imageRendering: "pixelated",
          background: "white",
        }}
      />
      <p className="text-muted-foreground text-xs">
        Full {HEAD_DOTS}-dot head width; the {template.size.w}×{template.size.h}mm
        label content is positioned by the offsets. This canvas is the exact
        print payload.
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
          Print label
        </Button>
      </div>

      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status || "Pick a draft and print. Swap drafts to see the template re-render."}
      </pre>
    </div>
  )
}

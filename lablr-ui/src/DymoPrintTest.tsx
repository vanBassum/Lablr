import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas, MAX_BYTES_PER_LINE } from "@/dymo"
import { renderLabel } from "@/label/render"
import { loadDrafts, loadTemplates, templateAccepts } from "@/label/templates"
import type { Draft, Template } from "@/label/types"

const HEAD_DOTS = MAX_BYTES_PER_LINE * 8

const draftName = (d: Draft) => d.label ?? Object.values(d.values).join(" · ")

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[] | null>(null)
  const [loadError, setLoadError] = useState("")
  const [draftIndex, setDraftIndex] = useState(0)
  const [templateId, setTemplateId] = useState("")
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    Promise.all([loadTemplates(), loadDrafts()])
      .then(([t, d]) => {
        setTemplates(t)
        setDrafts(d)
      })
      .catch((e) => setLoadError((e as Error).message))
  }, [])

  const draft = drafts?.[draftIndex]
  // Templates that can render this draft (draft supplies all their fields).
  const compatible =
    templates && draft ? templates.filter((t) => templateAccepts(t, draft)) : []
  // Keep the chosen template valid for the current draft; fall back to the first.
  const template = compatible.find((t) => t.id === templateId) ?? compatible[0]

  useEffect(() => {
    if (canvasRef.current && template && draft) {
      renderLabel(canvasRef.current, template, draft.values, { offsetX, offsetY })
    }
  }, [template, draft, offsetX, offsetY])

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !draft) return
    setBusy(true)
    setStatus("Opening DYMO…")
    try {
      const device = await openDymo()
      setStatus("Sending raster…")
      await printCanvas(device, canvas)
      await device.releaseInterface(0)
      await device.close()
      setStatus(`✓ Printed "${draftName(draft)}" (offset X=${offsetX} Y=${offsetY}).`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  if (loadError) return <p>Failed to load config: {loadError}</p>
  if (!templates || !drafts) return <p>Loading config…</p>
  if (!draft) return <p>No drafts found in config.</p>

  const labelH = template ? mmToDots(template.size.h) : mmToDots(25)

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Render a draft → preview = print</h2>

      <div className="flex flex-wrap gap-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs">Draft (the data)</span>
          <select
            value={draftIndex}
            onChange={(e) => setDraftIndex(Number(e.target.value))}
            className="bg-background w-56 rounded border px-2 py-1"
          >
            {drafts.map((d, i) => (
              <option key={i} value={i}>
                {draftName(d)}
              </option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs">Template ({compatible.length} fit this draft)</span>
          <select
            value={template?.id ?? ""}
            onChange={(e) => setTemplateId(e.target.value)}
            disabled={compatible.length === 0}
            className="bg-background w-64 rounded border px-2 py-1"
          >
            {compatible.map((t) => (
              <option key={t.id} value={t.id}>
                {t.name} — {t.size.w}×{t.size.h}mm
              </option>
            ))}
          </select>
        </label>
      </div>

      {template ? (
        <>
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
            label is positioned by the offsets. This canvas is the exact print payload.
          </p>
        </>
      ) : (
        <p className="text-muted-foreground text-sm">
          No template fits this draft's fields ({Object.keys(draft.values).join(", ")}).
        </p>
      )}

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
        <Button onClick={handlePrint} disabled={busy || !template}>
          Print label
        </Button>
      </div>

      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status ||
          "Pick a draft, then a template that fits it. Same draft → big or small label."}
      </pre>
    </div>
  )
}

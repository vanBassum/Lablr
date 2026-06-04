import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas, MAX_BYTES_PER_LINE } from "@/dymo"
import { renderLabel } from "@/label/render"
import { draftsFrom, loadTemplates } from "@/label/templates"
import type { Draft, Template } from "@/label/types"

const HEAD_DOTS = MAX_BYTES_PER_LINE * 8

/** A human label for a draft — generic, since every template has different fields. */
const draftLabel = (d: Draft) => Object.values(d.values).join(" · ")

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[]>([])
  const [loadError, setLoadError] = useState("")
  const [templateId, setTemplateId] = useState("")
  const [draftIndex, setDraftIndex] = useState(0)
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    loadTemplates()
      .then((t) => {
        setTemplates(t)
        setDrafts(draftsFrom(t))
        setTemplateId(t[0]?.id ?? "")
      })
      .catch((e) => setLoadError((e as Error).message))
  }, [])

  const template = templates?.find((t) => t.id === templateId)
  const templateDrafts = drafts.filter((d) => d.templateId === templateId)
  const draft = templateDrafts[draftIndex]

  useEffect(() => {
    if (canvasRef.current && template && draft) {
      renderLabel(canvasRef.current, template, draft.values, { offsetX, offsetY })
    }
  }, [template, draft, offsetX, offsetY])

  function selectTemplate(id: string) {
    setTemplateId(id)
    setDraftIndex(0)
  }

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
      setStatus(`✓ Printed "${draftLabel(draft)}" (offset X=${offsetX} Y=${offsetY}).`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  if (loadError) return <p>Failed to load templates: {loadError}</p>
  if (!templates) return <p>Loading templates…</p>
  if (!template) return <p>No templates found in config.</p>

  const labelH = mmToDots(template.size.h)

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Render a draft → preview = print</h2>

      <div className="flex flex-wrap gap-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs">Template</span>
          <select
            value={templateId}
            onChange={(e) => selectTemplate(e.target.value)}
            className="bg-background w-56 rounded border px-2 py-1"
          >
            {templates.map((t) => (
              <option key={t.id} value={t.id}>
                {t.name}
              </option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs">Test draft</span>
          <select
            value={draftIndex}
            onChange={(e) => setDraftIndex(Number(e.target.value))}
            className="bg-background w-56 rounded border px-2 py-1"
          >
            {templateDrafts.map((d, i) => (
              <option key={i} value={i}>
                {draftLabel(d)}
              </option>
            ))}
          </select>
        </label>
      </div>

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
        <Button onClick={handlePrint} disabled={busy || !draft}>
          Print label
        </Button>
      </div>

      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status || "Switch template or draft — the preview re-renders from the YAML."}
      </pre>
    </div>
  )
}

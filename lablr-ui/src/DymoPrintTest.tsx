import { useEffect, useRef, useState } from "react"
import { Button } from "@/components/ui/button"
import { mmToDots, openDymo, printCanvas } from "@/dymo"
import { renderLabel } from "@/label/render"
import {
  loadDrafts,
  loadMedia,
  loadTemplates,
  templateAccepts,
  templateFitsMedia,
} from "@/label/templates"
import type { Draft, Media, Template } from "@/label/types"

const PREVIEW_PX_PER_MM = 8 // on-screen scale; the canvas itself is at printer dots

const draftName = (d: Draft) => d.label ?? Object.values(d.values).join(" · ")

export function DymoPrintTest() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[] | null>(null)
  const [media, setMedia] = useState<Media[] | null>(null)
  const [loadError, setLoadError] = useState("")
  const [draftIndex, setDraftIndex] = useState(0)
  const [templateId, setTemplateId] = useState("")
  const [mediaId, setMediaId] = useState("")
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [status, setStatus] = useState("")
  const [busy, setBusy] = useState(false)

  useEffect(() => {
    Promise.all([loadTemplates(), loadDrafts(), loadMedia()])
      .then(([t, d, m]) => {
        setTemplates(t)
        setDrafts(d)
        setMedia(m)
        setMediaId(m[0]?.id ?? "")
      })
      .catch((e) => setLoadError((e as Error).message))
  }, [])

  const draft = drafts?.[draftIndex]
  const compatible =
    templates && draft ? templates.filter((t) => templateAccepts(t, draft)) : []
  const template = compatible.find((t) => t.id === templateId) ?? compatible[0]
  const selectedMedia = media?.find((m) => m.id === mediaId) ?? media?.[0]
  const fits = template && selectedMedia ? templateFitsMedia(template, selectedMedia) : true

  // When the draft changes, default to its suggested template (if it fits the
  // draft), otherwise the first compatible one. Still freely overridable.
  useEffect(() => {
    if (!templates || !draft) return
    const comp = templates.filter((t) => templateAccepts(t, draft))
    setTemplateId(
      draft.template && comp.some((t) => t.id === draft.template)
        ? draft.template
        : (comp[0]?.id ?? ""),
    )
  }, [draft, templates])

  useEffect(() => {
    if (canvasRef.current && template && draft && selectedMedia) {
      renderLabel(canvasRef.current, template, draft.values, selectedMedia)
    }
  }, [template, draft, selectedMedia])

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !draft || !selectedMedia) return
    setBusy(true)
    setStatus("Opening DYMO…")
    try {
      // Head offset = media calibration + manual nudge. X is byte-granular (ESC B).
      const totalX = mmToDots(selectedMedia.offset?.x ?? 0) + offsetX
      const totalY = mmToDots(selectedMedia.offset?.y ?? 0) + offsetY
      const device = await openDymo()
      setStatus("Sending raster…")
      await printCanvas(device, canvas, {
        dotTabBytes: Math.round(Math.max(0, totalX) / 8),
        topBlankLines: Math.round(Math.max(0, totalY)),
      })
      await device.releaseInterface(0)
      await device.close()
      setStatus(`✓ Printed "${draftName(draft)}".`)
    } catch (e) {
      setStatus(`✗ ${(e as Error).message}`)
    } finally {
      setBusy(false)
    }
  }

  if (loadError) return <p>Failed to load config: {loadError}</p>
  if (!templates || !drafts || !media) return <p>Loading config…</p>
  if (!draft) return <p>No drafts found in config.</p>
  if (!selectedMedia) return <p>No media found in config.</p>

  return (
    <div className="flex flex-col gap-3">
      <h2 className="font-medium">Render a draft → preview = print</h2>

      <div className="flex flex-wrap gap-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs">Draft (the data)</span>
          <select
            value={draftIndex}
            onChange={(e) => setDraftIndex(Number(e.target.value))}
            className="bg-background w-52 rounded border px-2 py-1"
          >
            {drafts.map((d, i) => (
              <option key={i} value={i}>
                {draftName(d)}
              </option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs">Template ({compatible.length} fit draft)</span>
          <select
            value={template?.id ?? ""}
            onChange={(e) => setTemplateId(e.target.value)}
            disabled={compatible.length === 0}
            className="bg-background w-60 rounded border px-2 py-1"
          >
            {compatible.map((t) => (
              <option key={t.id} value={t.id}>
                {t.name} — {t.size.w}×{t.size.h}mm
              </option>
            ))}
          </select>
        </label>

        <label className="flex flex-col gap-1">
          <span className="text-xs">Media (physical label)</span>
          <select
            value={selectedMedia.id}
            onChange={(e) => setMediaId(e.target.value)}
            className="bg-background w-56 rounded border px-2 py-1"
          >
            {media.map((m) => (
              <option key={m.id} value={m.id}>
                {m.name} — {m.size.w}×{m.size.h}mm
              </option>
            ))}
          </select>
        </label>
      </div>

      {template ? (
        <>
          {/* Wrapper sized to the physical label so the preview matches its shape. */}
          <div
            className="border bg-white"
            style={{
              width: selectedMedia.size.w * PREVIEW_PX_PER_MM,
              height: selectedMedia.size.h * PREVIEW_PX_PER_MM,
            }}
          >
            <canvas
              ref={canvasRef}
              style={{
                width: "100%",
                height: "100%",
                imageRendering: "pixelated",
                display: "block",
              }}
            />
          </div>
          {fits ? (
            <p className="text-muted-foreground text-xs">
              {selectedMedia.size.w}×{selectedMedia.size.h}mm label, shown to scale.
              This canvas is the exact print payload.
            </p>
          ) : (
            <p className="text-xs text-red-600">
              ⚠ Template ({template.size.w}×{template.size.h}mm) is larger than the{" "}
              {selectedMedia.size.w}×{selectedMedia.size.h}mm media — it will clip. Use a
              bigger media or smaller template.
            </p>
          )}
        </>
      ) : (
        <p className="text-muted-foreground text-sm">
          No template fits this draft's fields ({Object.keys(draft.values).join(", ")}).
        </p>
      )}

      <div className="flex flex-wrap items-end gap-4">
        <label className="flex flex-col gap-1">
          <span className="text-xs">Offset nudge X (dots)</span>
          <input
            type="number"
            value={offsetX}
            step={8}
            onChange={(e) => setOffsetX(Number(e.target.value))}
            className="bg-background w-28 rounded border px-2 py-1 font-mono"
          />
        </label>
        <label className="flex flex-col gap-1">
          <span className="text-xs">Offset nudge Y (dots)</span>
          <input
            type="number"
            value={offsetY}
            step={8}
            onChange={(e) => setOffsetY(Number(e.target.value))}
            className="bg-background w-28 rounded border px-2 py-1 font-mono"
          />
        </label>
        <Button onClick={handlePrint} disabled={busy || !template}>
          Print label
        </Button>
      </div>

      <pre className="bg-muted rounded p-3 font-mono text-xs whitespace-pre-wrap">
        {status ||
          "Draft = data · Template = layout · Media = physical label. Offset positions the label on the head (calibrate in the media YAML; nudge here to find it)."}
      </pre>
    </div>
  )
}

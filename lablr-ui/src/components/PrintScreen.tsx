import { useEffect, useRef, useState } from "react"
import { ChevronDown, Loader2, Printer } from "lucide-react"
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
import { Button } from "@/components/ui/button"
import { Card, CardContent } from "@/components/ui/card"
import { Badge } from "@/components/ui/badge"
import { Label } from "@/components/ui/label"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { WebUsbProbe } from "@/WebUsbProbe"

const PREVIEW_MAX_PX = 280 // longest preview edge

const draftName = (d: Draft) => d.label ?? Object.values(d.values).join(" · ")

export function PrintScreen() {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[] | null>(null)
  const [media, setMedia] = useState<Media[] | null>(null)
  const [loadError, setLoadError] = useState("")
  const [draftIndex, setDraftIndex] = useState(0)
  const [seenDraft, setSeenDraft] = useState(0)
  const [templateOverride, setTemplateOverride] = useState<string | null>(null)
  const [mediaId, setMediaId] = useState("")
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [advanced, setAdvanced] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)
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

  // Reset the template override when the draft changes — render-phase reset, the
  // React-recommended alternative to calling setState inside an effect.
  if (seenDraft !== draftIndex) {
    setSeenDraft(draftIndex)
    setTemplateOverride(null)
  }

  const draft = drafts?.[draftIndex]
  const compatible =
    templates && draft ? templates.filter((t) => templateAccepts(t, draft)) : []
  // The draft suggests a template (overridable); else fall back to the first that fits.
  const suggestedId =
    draft?.template && compatible.some((t) => t.id === draft.template)
      ? draft.template
      : compatible[0]?.id
  const template =
    compatible.find((t) => t.id === (templateOverride ?? suggestedId)) ?? compatible[0]
  const selectedMedia = media?.find((m) => m.id === mediaId) ?? media?.[0]
  const fits = template && selectedMedia ? templateFitsMedia(template, selectedMedia) : true

  useEffect(() => {
    if (canvasRef.current && template && draft && selectedMedia) {
      renderLabel(canvasRef.current, template, draft.values, selectedMedia)
    }
  }, [template, draft, selectedMedia])

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !draft || !selectedMedia) return
    setBusy(true)
    setStatus({ ok: true, msg: "Opening printer…" })
    try {
      const totalX = mmToDots(selectedMedia.offset?.x ?? 0) + offsetX
      const totalY = mmToDots(selectedMedia.offset?.y ?? 0) + offsetY
      const device = await openDymo()
      setStatus({ ok: true, msg: "Sending…" })
      await printCanvas(device, canvas, {
        dotTabBytes: Math.round(Math.max(0, totalX) / 8),
        topBlankLines: Math.round(Math.max(0, totalY)),
      })
      await device.releaseInterface(0)
      await device.close()
      setStatus({ ok: true, msg: `Printed “${draftName(draft)}”` })
    } catch (e) {
      setStatus({ ok: false, msg: (e as Error).message })
    } finally {
      setBusy(false)
    }
  }

  if (loadError)
    return <p className="text-destructive p-4 text-sm">Failed to load config: {loadError}</p>
  if (!templates || !drafts || !media)
    return <p className="text-muted-foreground p-4 text-sm">Loading…</p>
  if (!draft) return <p className="p-4 text-sm">No drafts found in config.</p>
  if (!selectedMedia) return <p className="p-4 text-sm">No media found in config.</p>

  const scale = Math.min(
    PREVIEW_MAX_PX / selectedMedia.size.w,
    PREVIEW_MAX_PX / selectedMedia.size.h,
  )

  return (
    <>
      <main className="flex flex-1 flex-col gap-5 p-4 pb-32">
        {/* Preview */}
        <Card className="overflow-hidden">
          <CardContent className="flex flex-col items-center gap-3 py-6">
            <div
              className="flex items-center justify-center rounded-md bg-white p-1 shadow-sm ring-1 ring-black/10"
              style={{ width: selectedMedia.size.w * scale, height: selectedMedia.size.h * scale }}
            >
              {template ? (
                <canvas
                  ref={canvasRef}
                  style={{
                    width: "100%",
                    height: "100%",
                    imageRendering: "pixelated",
                    display: "block",
                  }}
                />
              ) : (
                <span className="px-2 text-center text-xs text-neutral-500">
                  No template fits this draft
                </span>
              )}
            </div>
            <div className="text-muted-foreground flex items-center gap-2 text-xs">
              <span>
                {selectedMedia.name} · {selectedMedia.size.w}×{selectedMedia.size.h}mm
              </span>
              {template &&
                (fits ? (
                  <Badge variant="secondary">Fits</Badge>
                ) : (
                  <Badge variant="destructive">Clips</Badge>
                ))}
            </div>
          </CardContent>
        </Card>

        {/* Controls */}
        <div className="flex flex-col gap-4">
          <Field label="What to print">
            <Select value={String(draftIndex)} onValueChange={(v) => setDraftIndex(Number(v))}>
              <SelectTrigger className="h-11 w-full">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {drafts.map((d, i) => (
                  <SelectItem key={i} value={String(i)}>
                    {draftName(d)}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </Field>

          <Field label="Template">
            <Select
              value={template?.id ?? ""}
              onValueChange={setTemplateOverride}
              disabled={compatible.length === 0}
            >
              <SelectTrigger className="h-11 w-full">
                <SelectValue placeholder="No fitting template" />
              </SelectTrigger>
              <SelectContent>
                {compatible.map((t) => (
                  <SelectItem key={t.id} value={t.id}>
                    {t.name} · {t.size.w}×{t.size.h}mm
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </Field>

          <Field label="Label (loaded media)">
            <Select value={selectedMedia.id} onValueChange={setMediaId}>
              <SelectTrigger className="h-11 w-full">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {media.map((m) => (
                  <SelectItem key={m.id} value={m.id}>
                    {m.name} · {m.size.w}×{m.size.h}mm
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </Field>
        </div>

        {/* Advanced */}
        <div className="flex flex-col gap-3">
          <Button
            variant="ghost"
            size="sm"
            className="text-muted-foreground w-fit"
            onClick={() => setAdvanced((v) => !v)}
          >
            <ChevronDown
              className={advanced ? "rotate-180 transition-transform" : "transition-transform"}
            />
            Advanced
          </Button>
          {advanced && (
            <div className="flex flex-col gap-4 rounded-lg border p-4">
              <div className="flex flex-wrap items-end gap-4">
                <Field label="Offset nudge X (dots)">
                  <NumberInput value={offsetX} onChange={setOffsetX} />
                </Field>
                <Field label="Offset nudge Y (dots)">
                  <NumberInput value={offsetY} onChange={setOffsetY} />
                </Field>
              </div>
              <p className="text-muted-foreground text-xs">
                Calibrate the label position in the media YAML; nudge here to find the value.
              </p>
              <WebUsbProbe />
            </div>
          )}
        </div>
      </main>

      {/* Sticky print bar */}
      <div className="bg-background/80 sticky bottom-0 z-10 border-t px-4 pt-3 pb-[max(0.75rem,env(safe-area-inset-bottom))] backdrop-blur">
        {status && (
          <p className={`mb-2 text-center text-sm ${status.ok ? "text-muted-foreground" : "text-destructive"}`}>
            {status.msg}
          </p>
        )}
        <Button
          className="h-12 w-full text-base"
          onClick={handlePrint}
          disabled={busy || !template}
        >
          {busy ? <Loader2 className="animate-spin" /> : <Printer />}
          {busy ? "Printing…" : "Print label"}
        </Button>
      </div>
    </>
  )
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-1.5">
      <Label className="text-muted-foreground text-xs">{label}</Label>
      {children}
    </div>
  )
}

function NumberInput({
  value,
  onChange,
}: {
  value: number
  onChange: (n: number) => void
}) {
  return (
    <input
      type="number"
      value={value}
      step={8}
      onChange={(e) => onChange(Number(e.target.value))}
      className="border-input bg-background h-9 w-28 rounded-md border px-3 font-mono text-sm"
    />
  )
}

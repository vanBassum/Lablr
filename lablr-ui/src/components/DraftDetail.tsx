import { useRef, useState, type ReactNode } from "react"
import { ChevronDown, ChevronLeft, Loader2, Printer, Settings2 } from "lucide-react"
import { mmToDots, openDymo, printCanvas } from "@/dymo"
import {
  draftName,
  pickMedia,
  pickTemplate,
  templateAccepts,
  templateFitsMedia,
} from "@/label/templates"
import type { Draft, Media, Template } from "@/label/types"
import { LabelCanvas } from "@/components/LabelCanvas"
import { WebUsbProbe } from "@/WebUsbProbe"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Label } from "@/components/ui/label"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import {
  Drawer,
  DrawerContent,
  DrawerHeader,
  DrawerTitle,
  DrawerTrigger,
} from "@/components/ui/drawer"

export function DraftDetail({
  draft,
  templates,
  media,
  onBack,
}: {
  draft: Draft
  templates: Template[]
  media: Media[]
  onBack: () => void
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const suggested = pickTemplate(draft, templates)
  const [templateOverride, setTemplateOverride] = useState<string | null>(null)
  const [mediaId, setMediaId] = useState(() => pickMedia(suggested, media)?.id ?? "")
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [advanced, setAdvanced] = useState(false)
  const [busy, setBusy] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)

  const compatible = templates.filter((t) => templateAccepts(t, draft))
  const template =
    compatible.find((t) => t.id === (templateOverride ?? suggested?.id)) ?? suggested
  const selectedMedia = media.find((m) => m.id === mediaId) ?? media[0]
  const fits = template && selectedMedia ? templateFitsMedia(template, selectedMedia) : true

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !selectedMedia) return
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

  return (
    <>
      <main className="flex flex-1 flex-col items-center gap-4 p-4 pb-32">
        <div className="flex w-full items-center gap-1">
          <Button variant="ghost" size="icon" aria-label="Back" onClick={onBack}>
            <ChevronLeft />
          </Button>
          <span className="font-medium">{draftName(draft)}</span>
        </div>

        {template && selectedMedia ? (
          <LabelCanvas
            canvasRef={canvasRef}
            template={template}
            values={draft.values}
            media={selectedMedia}
            maxEdgePx={280}
          />
        ) : (
          <p className="text-muted-foreground text-sm">No template fits this draft.</p>
        )}

        {selectedMedia && (
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
        )}
      </main>

      {/* Sticky action bar: Print + gear */}
      <div className="bg-background/80 sticky bottom-0 z-10 border-t px-4 pt-3 pb-[max(0.75rem,env(safe-area-inset-bottom))] backdrop-blur">
        {status && (
          <p
            className={`mb-2 text-center text-sm ${status.ok ? "text-muted-foreground" : "text-destructive"}`}
          >
            {status.msg}
          </p>
        )}
        <div className="flex gap-2">
          <Button
            className="h-12 flex-1 text-base"
            onClick={handlePrint}
            disabled={busy || !template}
          >
            {busy ? <Loader2 className="animate-spin" /> : <Printer />}
            {busy ? "Printing…" : "Print label"}
          </Button>

          <Drawer>
            <DrawerTrigger asChild>
              <Button variant="outline" size="icon" className="size-12" aria-label="Options">
                <Settings2 />
              </Button>
            </DrawerTrigger>
            <DrawerContent>
              <DrawerHeader>
                <DrawerTitle>Label options</DrawerTitle>
              </DrawerHeader>
              <div className="mx-auto flex w-full max-w-md flex-col gap-4 px-4 pb-8">
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
                  <Select value={selectedMedia?.id ?? ""} onValueChange={setMediaId}>
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
                      Calibrate the label position in the media YAML; nudge here to find it.
                    </p>
                    <WebUsbProbe />
                  </div>
                )}
              </div>
            </DrawerContent>
          </Drawer>
        </div>
      </div>
    </>
  )
}

function Field({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="flex flex-col gap-1.5">
      <Label className="text-muted-foreground text-xs">{label}</Label>
      {children}
    </div>
  )
}

function NumberInput({ value, onChange }: { value: number; onChange: (n: number) => void }) {
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

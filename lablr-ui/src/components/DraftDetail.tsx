import { useRef, useState, type ReactNode } from "react"
import {
  ChevronDown,
  ChevronLeft,
  Loader2,
  Printer,
  RotateCw,
  Settings2,
} from "lucide-react"
import { mmToDots } from "@/dymo"
import { usePrinter } from "@/printer"
import {
  compatiblePresets,
  defaultPreset,
  draftName,
  pickMedia,
  pickTemplate,
  templateFitsMedia,
} from "@/label/templates"
import type { Draft, Media, Orientation, Preset, Template } from "@/label/types"
import { LabelCanvas } from "@/components/LabelCanvas"
import { WebUsbProbe } from "@/WebUsbProbe"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Label } from "@/components/ui/label"
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
  presets,
  onBack,
}: {
  draft: Draft
  templates: Template[]
  media: Media[]
  presets: Preset[]
  onBack: () => void
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const printer = usePrinter()
  const options = compatiblePresets(draft, presets, templates)
  const [presetId, setPresetId] = useState(
    () => defaultPreset(draft, presets, templates)?.id ?? "",
  )
  const [seenPreset, setSeenPreset] = useState(presetId)
  const [orientationOverride, setOrientationOverride] = useState<Orientation | null>(null)
  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [advanced, setAdvanced] = useState(false)
  const [busy, setBusy] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)

  const preset = options.find((p) => p.id === presetId) ?? options[0]
  // Orientation defaults to the selected preset's; reset the override when the
  // preset changes (render-phase reset, not an effect).
  if (seenPreset !== presetId) {
    setSeenPreset(presetId)
    setOrientationOverride(null)
  }
  const orientation: Orientation = orientationOverride ?? preset?.orientation ?? "portrait"
  const template =
    (preset && templates.find((t) => t.id === preset.template)) ??
    pickTemplate(draft, templates)
  const selectedMedia =
    (preset && media.find((m) => m.id === preset.media)) ?? pickMedia(template, media)
  const fits =
    template && selectedMedia
      ? templateFitsMedia(template, selectedMedia, orientation)
      : true

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !selectedMedia) return
    setBusy(true)
    setStatus({ ok: true, msg: "Printing…" })
    try {
      const totalX = mmToDots(selectedMedia.offset?.x ?? 0) + offsetX
      const totalY = mmToDots(selectedMedia.offset?.y ?? 0) + offsetY
      // Prints on the persistent connection; connects once on first use.
      await printer.print(canvas, {
        dotTabBytes: Math.round(Math.max(0, totalX) / 8),
        topBlankLines: Math.round(Math.max(0, totalY)),
      })
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
            orientation={orientation}
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

        {/* Output options: preset (the big/small choice) + orientation. */}
        <div className="flex flex-wrap items-center justify-center gap-2">
          {options.map((p) => (
            <Button
              key={p.id}
              size="sm"
              variant={p.id === preset?.id ? "default" : "outline"}
              onClick={() => setPresetId(p.id)}
            >
              {p.name}
            </Button>
          ))}
          <Button
            size="sm"
            variant="outline"
            onClick={() =>
              setOrientationOverride(orientation === "portrait" ? "landscape" : "portrait")
            }
          >
            <RotateCw />
            {orientation === "portrait" ? "Portrait" : "Landscape"}
          </Button>
        </div>
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
              <Button variant="outline" size="icon" className="size-12" aria-label="Advanced">
                <Settings2 />
              </Button>
            </DrawerTrigger>
            <DrawerContent>
              <DrawerHeader>
                <DrawerTitle>Calibration & diagnostics</DrawerTitle>
              </DrawerHeader>
              <div className="mx-auto flex w-full max-w-md flex-col gap-4 px-4 pb-8">
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
                <Button
                  variant="ghost"
                  size="sm"
                  className="text-muted-foreground w-fit"
                  onClick={() => setAdvanced((v) => !v)}
                >
                  <ChevronDown
                    className={advanced ? "rotate-180 transition-transform" : "transition-transform"}
                  />
                  WebUSB diagnostics
                </Button>
                {advanced && <WebUsbProbe />}
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

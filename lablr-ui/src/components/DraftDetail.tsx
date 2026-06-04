import { useRef, useState, type ReactNode } from "react"
import {
  ChevronDown,
  ChevronLeft,
  Crosshair,
  Loader2,
  Printer,
  RotateCw,
  Settings2,
} from "lucide-react"
import { DPI, mmToDots } from "@/dymo"
import { usePrinter } from "@/printer"
import {
  defaultPreset,
  draftName,
  printerForMedia,
} from "@/label/templates"
import type {
  Draft,
  Media,
  Orientation,
  Preset,
  Printer as PrinterProfile,
  Template,
} from "@/label/types"
import { renderCalibration } from "@/label/render"
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
  printers,
  onBack,
}: {
  draft: Draft
  templates: Template[]
  media: Media[]
  presets: Preset[]
  printers: PrinterProfile[]
  onBack: () => void
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const printer = usePrinter()

  // Independent template + media selection (not locked to preset)
  const defaultPresetVal = defaultPreset(draft, presets, templates)
  const [templateId, setTemplateId] = useState(() => defaultPresetVal?.template ?? templates[0]?.id ?? "")
  const [mediaId, setMediaId] = useState(() => defaultPresetVal?.media ?? media[0]?.id ?? "")
  const [orientation, setOrientation] = useState<Orientation>(() => defaultPresetVal?.orientation ?? "portrait")

  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [advanced, setAdvanced] = useState(false)
  const [busy, setBusy] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)

  const template = templates.find((t) => t.id === templateId)
  const selectedMedia = media.find((m) => m.id === mediaId)
  const fits = true

  const printerProfile = printerForMedia(selectedMedia, printers)

  // Head offset in dots = the media's calibrated offset + the manual nudge.
  // (Per media, because a roll is specific to a printer.)
  function headOffset() {
    return {
      x: mmToDots(selectedMedia?.offset?.x ?? 0) + offsetX,
      y: mmToDots(selectedMedia?.offset?.y ?? 0) + offsetY,
    }
  }
  const dotsToMm = (dots: number) => ((dots / DPI) * 25.4).toFixed(1)

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas || !selectedMedia) return
    setBusy(true)
    setStatus({ ok: true, msg: "Printing…" })
    try {
      // Prints on the persistent connection; connects once on first use.
      await printer.print(canvas, headOffset())
      setStatus({ ok: true, msg: `Printed “${draftName(draft)}”` })
    } catch (e) {
      setStatus({ ok: false, msg: (e as Error).message })
    } finally {
      setBusy(false)
    }
  }

  // Print a border + corner-to-corner cross to calibrate this media's offset.
  async function printCalibration() {
    if (!selectedMedia) return
    const canvas = document.createElement("canvas")
    renderCalibration(canvas, selectedMedia)
    setBusy(true)
    setStatus({ ok: true, msg: "Printing alignment pattern…" })
    try {
      await printer.print(canvas, headOffset())
      setStatus({ ok: true, msg: "Printed alignment pattern" })
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
            fields={draft.fields}
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
              {printerProfile ? ` · ${printerProfile.name}` : ""}
            </span>
            {template &&
              (fits ? (
                <Badge variant="secondary">Fits</Badge>
              ) : (
                <Badge variant="destructive">Clips</Badge>
              ))}
          </div>
        )}

        {/* Template + Media selection */}
        <div className="flex flex-col items-center gap-2 text-sm">
          <div className="flex flex-wrap items-center justify-center gap-2">
            {templates.map((t) => (
              <Button
                key={t.id}
                size="sm"
                variant={t.id === templateId ? "default" : "outline"}
                onClick={() => setTemplateId(t.id)}
              >
                {t.name}
              </Button>
            ))}
          </div>
          <div className="flex flex-wrap items-center justify-center gap-2">
            {media.map((m) => (
              <Button
                key={m.id}
                size="sm"
                variant={m.id === mediaId ? "default" : "outline"}
                onClick={() => setMediaId(m.id)}
              >
                {m.name}
              </Button>
            ))}
          </div>
          <Button
            size="sm"
            variant="outline"
            onClick={() => setOrientation(orientation === "portrait" ? "landscape" : "portrait")}
          >
            <RotateCw className="mr-1 size-3" />
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
                  Print, nudge to align. Offset ≈ {dotsToMm(headOffset().x)}×
                  {dotsToMm(headOffset().y)}mm — bake into this media's <code>offset</code>.
                </p>
                <Button
                  variant="outline"
                  size="sm"
                  className="w-fit"
                  onClick={printCalibration}
                  disabled={busy || !selectedMedia}
                >
                  <Crosshair />
                  Print alignment pattern
                </Button>
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

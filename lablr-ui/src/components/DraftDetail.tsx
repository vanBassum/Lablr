import { useState, type ReactNode } from "react"
import {
  ChevronDown,
  ChevronLeft,
  Crosshair,
  Loader2,
  Printer,
  Settings2,
} from "lucide-react"
import { usePrintTarget } from "@/printTarget"
import { LabelPreviewContainer } from "@/components/LabelPreviewContainer"
import { WebUsbProbe } from "@/WebUsbProbe"
import { Button } from "@/components/ui/button"
import { Label } from "@/components/ui/label"
import {
  Drawer,
  DrawerContent,
  DrawerHeader,
  DrawerTitle,
  DrawerTrigger,
} from "@/components/ui/drawer"
import { configService } from "@/services/config"
import { draftService } from "@/services/drafts"
import { draftPreviewUrl } from "@/services/printApi"
import type { Orientation } from "@/types"

export function DraftDetail({
  draftId,
  onBack,
}: {
  draftId: string
  onBack: () => void
}) {
  const printTarget = usePrintTarget()

  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [busy, setBusy] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)

  const draft = draftService.getDraft(draftId)
  const draftName = draft ? Object.values(draft.fields)[0] : draftId

  // Auto-discover matching templates; let the user pick when more than one fits.
  const matchingTemplates = draft ? configService.getMatchingTemplates(draft) : []
  const [templateId, setTemplateId] = useState<string>("")
  const template =
    matchingTemplates.find((t) => t.id === templateId) ?? matchingTemplates[0]

  const orientations = template ? configService.getTemplateOrientations(template) : []
  const [orientation, setOrientation] = useState<Orientation | "">("")
  const activeOrientation: Orientation =
    (orientations.includes(orientation as Orientation)
      ? (orientation as Orientation)
      : orientations[0]) ?? "portrait"

  const stock = template ? configService.getLabelStock(template.label) : undefined
  const printer = stock ? configService.getPrinterForStock(stock) : undefined

  // The backend is the single renderer — the preview is the exact bitmap it prints.
  const previewSrc = template
    ? draftPreviewUrl(draftId, template.id, activeOrientation)
    : ""

  const dotsToMm = (dots: number) => (printer ? ((dots / printer.dpi) * 25.4).toFixed(1) : "0.0")

  async function handlePrint() {
    if (!template) return
    setBusy(true)
    setStatus({ ok: true, msg: "Printing…" })
    try {
      // Offset nudge (dots) is applied by the backend at print time on top of the
      // stock calibration — never baked into the preview.
      await printTarget.print({
        draftId,
        templateId: template.id,
        orientation: activeOrientation,
        nudgeX: offsetX,
        nudgeY: offsetY,
      })
      setStatus({ ok: true, msg: `Printed "${draftName}"` })
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
          <span className="font-medium">{draftName}</span>
        </div>

        {template && draft && stock && printer ? (
          <>
            <LabelPreviewContainer
              stock={stock}
              orientation={activeOrientation}
              printer={printer}
              src={previewSrc}
            />
            {matchingTemplates.length > 1 ? (
              <OptionGroup label="Template">
                {matchingTemplates.map((t) => (
                  <Button
                    key={t.id}
                    variant={t.id === template.id ? "default" : "outline"}
                    size="sm"
                    onClick={() => setTemplateId(t.id)}
                  >
                    {t.name}
                  </Button>
                ))}
              </OptionGroup>
            ) : (
              <p className="text-muted-foreground text-sm">Template: {template.name}</p>
            )}
            {orientations.length > 1 && (
              <OptionGroup label="Orientation">
                {orientations.map((o) => (
                  <Button
                    key={o}
                    variant={o === activeOrientation ? "default" : "outline"}
                    size="sm"
                    className="capitalize"
                    onClick={() => setOrientation(o)}
                  >
                    {o}
                  </Button>
                ))}
              </OptionGroup>
            )}
          </>
        ) : (
          <p className="text-destructive text-sm">No compatible template found</p>
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
                  Print, nudge to align. Offset ≈ {dotsToMm(offsetX)}×{dotsToMm(offsetY)}mm
                </p>
                <Button
                  variant="ghost"
                  size="sm"
                  className="text-muted-foreground w-fit"
                  onClick={() => setStatus({ ok: true, msg: "Calibration pattern: not implemented" })}
                >
                  <Crosshair />
                  Print alignment pattern
                </Button>
                <Button
                  variant="ghost"
                  size="sm"
                  className="text-muted-foreground w-fit"
                >
                  <ChevronDown className="transition-transform" />
                  WebUSB diagnostics
                </Button>
                <WebUsbProbe />
              </div>
            </DrawerContent>
          </Drawer>
        </div>
      </div>
    </>
  )
}

/** A labeled row of choice buttons (e.g. Template, Orientation). */
function OptionGroup({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="flex w-full flex-col items-center gap-1.5">
      <span className="text-muted-foreground text-xs font-medium tracking-wide uppercase">
        {label}
      </span>
      <div className="flex flex-wrap justify-center gap-2">{children}</div>
    </div>
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

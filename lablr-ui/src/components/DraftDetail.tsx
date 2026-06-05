import { useRef, useState, useEffect, type ReactNode } from "react"
import {
  ChevronDown,
  ChevronLeft,
  Crosshair,
  Loader2,
  Printer,
  Settings2,
} from "lucide-react"
import { DPI } from "@/dymo"
import { usePrinter } from "@/printer"
import { LabelCanvas } from "@/components/LabelCanvas"
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
import { renderService } from "@/services/render"
import { draftService } from "@/services/drafts"

export function DraftDetail({
  draftId,
  onBack,
}: {
  draftId: string
  onBack: () => void
}) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const printer = usePrinter()

  const [offsetX, setOffsetX] = useState(0)
  const [offsetY, setOffsetY] = useState(0)
  const [busy, setBusy] = useState(false)
  const [status, setStatus] = useState<{ ok: boolean; msg: string } | null>(null)

  // Load draft from service
  const draft = draftService.getDraft(draftId)
  const draftName = draft ? Object.values(draft.fields)[0] : draftId

  // Find compatible template
  const compatibleTemplates = draft ? configService.getCompatibleTemplates(draft) : []
  const template = compatibleTemplates[0]

  // Re-render when template changes
  useEffect(() => {
    if (!canvasRef.current || !template || !draft) return

    const label = configService.getLabel(template.label)
    const templatePrinter = configService.getPrinter(template.printerId)

    if (!label || !templatePrinter) return

    renderService.render(canvasRef.current, draft, template, label, templatePrinter)
  }, [template, draft])

  const dotsToMm = (dots: number) => ((dots / DPI) * 25.4).toFixed(1)

  async function handlePrint() {
    const canvas = canvasRef.current
    if (!canvas) return
    setBusy(true)
    setStatus({ ok: true, msg: "Printing…" })
    try {
      await printer.print(canvas, { x: offsetX, y: offsetY })
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

        <LabelCanvas canvasRef={canvasRef} width={300} height={400} />

        {template ? (
          <p className="text-muted-foreground text-sm">Template: {template.id}</p>
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
            disabled={busy}
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

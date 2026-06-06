import { useEffect, useRef, useState } from "react"
import { ChevronLeft, Loader2, Trash2, X } from "lucide-react"
import type { Template } from "@/types"
import { deleteTemplate, listTemplates } from "@/services/templatesApi"
import { renderService } from "@/services/render"
import { configService } from "@/services/config"
import { usePictogramsReady } from "@/services/pictograms"
import { Button } from "@/components/ui/button"

// Lists templates with a live preview + delete. Authoring is done via the AI
// (MCP upsert_template); here you see what each looks like and remove unwanted
// ones. The preview uses the canonical renderer with sample field values, so
// it matches a real print's layout. Click a preview to enlarge it.
export function TemplatesPage() {
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [preview, setPreview] = useState<Template | null>(null)

  function load() {
    listTemplates()
      .then((t) => {
        setTemplates(t)
        setError(null)
      })
      .catch((e) => setError((e as Error).message))
  }
  useEffect(load, [])

  const goHome = () => (window.location.hash = "")

  return (
    <main className="flex flex-1 flex-col gap-3 p-4">
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="icon" aria-label="Back" onClick={goHome}>
          <ChevronLeft />
        </Button>
        <span className="font-medium">Templates</span>
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      {templates === null ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading…
        </div>
      ) : templates.length === 0 ? (
        <p className="text-muted-foreground text-sm">No templates yet.</p>
      ) : (
        <ul className="flex flex-col gap-2">
          {templates.map((t) => {
            const box = fitBox(t, 110, 76)
            return (
            <li key={t.id} className="bg-card flex items-center gap-3 rounded-lg border p-3">
              <button
                aria-label={`Preview ${t.name || t.id}`}
                onClick={() => setPreview(t)}
                className="shrink-0 overflow-hidden rounded border bg-white leading-none transition hover:ring-2 hover:ring-black/20"
              >
                <TemplateRender template={t} w={box.w} h={box.h} />
              </button>
              <div className="min-w-0 flex-1">
                <div className="truncate text-sm font-medium">{t.name || t.id}</div>
                <div className="text-muted-foreground truncate text-xs">
                  {t.id} · {t.label}
                  {t.requiredFields?.length ? ` · ${t.requiredFields.join(", ")}` : ""}
                </div>
              </div>
              <Button
                variant="ghost"
                size="icon"
                aria-label={`Delete ${t.id}`}
                onClick={async () => {
                  if (!window.confirm(`Delete template "${t.id}"?`)) return
                  try {
                    await deleteTemplate(t.id)
                    load()
                  } catch (e) {
                    setError((e as Error).message)
                  }
                }}
              >
                <Trash2 className="text-destructive size-4" />
              </Button>
            </li>
            )
          })}
        </ul>
      )}

      {preview && <PreviewModal template={preview} onClose={() => setPreview(null)} />}
    </main>
  )
}

// Larger preview in a centered modal; click backdrop / X / Esc to close.
function PreviewModal({ template, onClose }: { template: Template; onClose: () => void }) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose()
    window.addEventListener("keydown", onKey)
    return () => window.removeEventListener("keydown", onKey)
  }, [onClose])

  // Fit the label's aspect ratio within a sensible box.
  const { w, h, wMm, hMm } = fitBox(template, 320, 360)

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/50 p-4"
      onClick={onClose}
    >
      <div
        className="bg-card flex flex-col items-center gap-3 rounded-lg border p-4 shadow-xl"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex w-full items-center gap-2">
          <span className="flex-1 truncate text-sm font-medium">{template.name || template.id}</span>
          <Button variant="ghost" size="icon" className="size-7" aria-label="Close" onClick={onClose}>
            <X className="size-4" />
          </Button>
        </div>
        <div className="rounded border bg-white">
          <TemplateRender template={template} w={w} h={h} />
        </div>
        <span className="text-muted-foreground text-xs">
          {wMm}×{hMm}mm · sample values
        </span>
      </div>
    </div>
  )
}

// The on-screen size for a template's preview: the label's real aspect ratio
// (portrait/landscape aware) scaled to fit within maxW×maxH.
function fitBox(template: Template, maxW: number, maxH: number) {
  const stock = configService.getLabelStock(template.label)
  const orientation = configService.getTemplateOrientations(template)[0] ?? "portrait"
  const landscape = orientation === "landscape"
  const wMm = stock ? (landscape ? stock.heightMm : stock.widthMm) : 50
  const hMm = stock ? (landscape ? stock.widthMm : stock.heightMm) : 50
  const scale = Math.min(maxW / wMm, maxH / hMm)
  return { w: Math.max(1, Math.round(wMm * scale)), h: Math.max(1, Math.round(hMm * scale)), wMm, hMm }
}

// Renders a template with placeholder field values (field names as sample text,
// a real pictogram for any pictogram slots), fit-scaled into a w×h canvas.
function TemplateRender({ template, w, h }: { template: Template; w: number; h: number }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const picReady = usePictogramsReady()

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext("2d")
    if (!ctx) return
    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, w, h)

    const stock = configService.getLabelStock(template.label)
    if (!stock) return
    const orientation = configService.getTemplateOrientations(template)[0] ?? "portrait"
    const printer = configService.getPrinterForStock(stock)
    const elements = configService.getTemplateElements(template, orientation)

    const picNames = Object.keys(configService.getPictogramRegistry())
    const fields: Record<string, string> = {}
    for (const el of elements)
      fields[el.field] = el.type === "pictogram" ? (picNames[0] ?? "") : el.field

    const off = document.createElement("canvas")
    renderService.render(off, { draft: { id: "preview", fields }, elements, orientation, stock, printer })

    const scale = Math.min(w / off.width, h / off.height)
    const sw = off.width * scale
    const sh = off.height * scale
    ctx.drawImage(off, (w - sw) / 2, (h - sh) / 2, sw, sh)
  }, [template, w, h, picReady])

  return (
    <canvas
      ref={canvasRef}
      width={w}
      height={h}
      style={{ width: w, height: h, imageRendering: "pixelated", display: "block" }}
    />
  )
}

import { useEffect, useRef, useState } from "react"
import { ChevronLeft, Loader2, Trash2 } from "lucide-react"
import type { Template } from "@/types"
import { deleteTemplate, listTemplates } from "@/services/templatesApi"
import { renderService } from "@/services/render"
import { configService } from "@/services/config"
import { usePictogramsReady } from "@/services/pictograms"
import { Button } from "@/components/ui/button"

// Lists templates with a live preview + delete. Authoring is done via the AI
// (MCP upsert_template); here you see what each looks like and remove unwanted
// ones. The preview uses the canonical renderer with sample field values, so
// it matches a real print's layout.
export function TemplatesPage() {
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [error, setError] = useState<string | null>(null)

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
          {templates.map((t) => (
            <li key={t.id} className="bg-card flex items-center gap-3 rounded-lg border p-3">
              <TemplateThumb template={t} />
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
          ))}
        </ul>
      )}
    </main>
  )
}

// A small test render of a template, using placeholder field values: field
// names as sample text, and a real pictogram for any pictogram slots.
function TemplateThumb({ template }: { template: Template }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const picReady = usePictogramsReady()
  const W = 104
  const H = 72

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext("2d")
    if (!ctx) return
    ctx.fillStyle = "white"
    ctx.fillRect(0, 0, W, H)

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

    const scale = Math.min(W / off.width, H / off.height)
    const sw = off.width * scale
    const sh = off.height * scale
    ctx.drawImage(off, (W - sw) / 2, (H - sh) / 2, sw, sh)
  }, [template, picReady])

  return (
    <canvas
      ref={canvasRef}
      width={W}
      height={H}
      style={{ width: W, height: H, imageRendering: "pixelated" }}
      className="shrink-0 rounded border bg-white"
    />
  )
}

import { useEffect, useState } from "react"
import { ChevronLeft, Loader2, Trash2 } from "lucide-react"
import type { Template } from "@/types"
import { deleteTemplate, listTemplates } from "@/services/templatesApi"
import { Button } from "@/components/ui/button"

// Lists templates with delete. Authoring is done via the AI (MCP upsert_template);
// here you can see what exists and remove ones you don't want — like label stocks.
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

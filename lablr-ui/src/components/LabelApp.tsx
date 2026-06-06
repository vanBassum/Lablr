import { useEffect, useState } from "react"
import { DraftDetail } from "@/components/DraftDetail"
import { DraftPreview } from "@/components/DraftPreview"
import { draftService } from "@/services/drafts"
import type { Draft } from "@/types"

export function LabelApp() {
  const [selected, setSelected] = useState<string | null>(null)
  const drafts = draftService.getDrafts()

  // Deep link: #/d/{id} opens straight into a stored draft (the AI handoff).
  useEffect(() => {
    const match = window.location.hash.match(/^#\/d\/([\w-]+)/)
    if (!match) return
    draftService.fetchDraft(match[1]).then((d) => {
      if (d) setSelected(d.id)
    })
  }, [])

  const closeDraft = () => {
    if (window.location.hash.startsWith("#/d/")) {
      history.replaceState(null, "", window.location.pathname + window.location.search)
    }
    setSelected(null)
  }

  if (selected) {
    return <DraftDetail draftId={selected} onBack={closeDraft} />
  }

  return (
    <main className="flex-1 p-4">
      <h1 className="text-muted-foreground mb-3 text-sm">Pick a label to print</h1>
      <div className="grid grid-cols-2 gap-3">
        {drafts.map((draft) => (
          <DraftCard key={draft.id} draft={draft} onClick={() => setSelected(draft.id)} />
        ))}
      </div>

      <footer className="text-muted-foreground mt-8 text-center text-xs">
        <p>
          Ask your AI assistant for a label — it creates a draft and sends a link
          that opens here, ready to print.
        </p>
      </footer>
    </main>
  )
}

function DraftCard({
  draft,
  onClick,
}: {
  draft: Draft
  onClick: () => void
}) {
  const name = Object.values(draft.fields)[0] || draft.id

  return (
    <button
      onClick={onClick}
      className="bg-card hover:bg-muted/40 flex flex-col items-center gap-2 rounded-xl border p-3 text-center transition active:scale-[0.98]"
    >
      <div className="flex h-28 w-full items-center justify-center rounded bg-white ring-1 ring-black/10">
        <DraftPreview draft={draft} width={100} height={100} />
      </div>
      <span className="line-clamp-2 text-sm font-medium">{name}</span>
    </button>
  )
}

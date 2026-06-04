import { useEffect, useState } from "react"
import {
  draftName,
  loadDrafts,
  loadMedia,
  loadTemplates,
  pickMedia,
  pickTemplate,
} from "@/label/templates"
import type { Draft, Media, Template } from "@/label/types"
import { LabelCanvas } from "@/components/LabelCanvas"
import { DraftDetail } from "@/components/DraftDetail"

export function LabelApp() {
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[] | null>(null)
  const [media, setMedia] = useState<Media[] | null>(null)
  const [loadError, setLoadError] = useState("")
  const [selected, setSelected] = useState<number | null>(null)

  useEffect(() => {
    Promise.all([loadTemplates(), loadDrafts(), loadMedia()])
      .then(([t, d, m]) => {
        setTemplates(t)
        setDrafts(d)
        setMedia(m)
      })
      .catch((e) => setLoadError((e as Error).message))
  }, [])

  if (loadError)
    return <p className="text-destructive p-4 text-sm">Failed to load config: {loadError}</p>
  if (!templates || !drafts || !media)
    return <p className="text-muted-foreground p-4 text-sm">Loading…</p>

  if (selected !== null && drafts[selected]) {
    return (
      <DraftDetail
        draft={drafts[selected]}
        templates={templates}
        media={media}
        onBack={() => setSelected(null)}
      />
    )
  }

  return (
    <main className="flex-1 p-4">
      <h1 className="text-muted-foreground mb-3 text-sm">Pick a label to print</h1>
      <div className="grid grid-cols-2 gap-3">
        {drafts.map((d, i) => (
          <DraftCard
            key={i}
            draft={d}
            templates={templates}
            media={media}
            onClick={() => setSelected(i)}
          />
        ))}
      </div>
    </main>
  )
}

function DraftCard({
  draft,
  templates,
  media,
  onClick,
}: {
  draft: Draft
  templates: Template[]
  media: Media[]
  onClick: () => void
}) {
  const template = pickTemplate(draft, templates)
  const m = pickMedia(template, media)

  return (
    <button
      onClick={onClick}
      className="bg-card hover:bg-muted/40 flex flex-col items-center gap-2 rounded-xl border p-3 text-center transition active:scale-[0.98]"
    >
      <div className="flex h-28 items-center justify-center">
        {template && m ? (
          <LabelCanvas template={template} values={draft.values} media={m} maxEdgePx={120} />
        ) : (
          <span className="text-muted-foreground text-xs">no template</span>
        )}
      </div>
      <span className="line-clamp-2 text-sm font-medium">{draftName(draft)}</span>
    </button>
  )
}

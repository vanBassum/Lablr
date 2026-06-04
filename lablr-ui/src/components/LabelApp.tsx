import { useEffect, useState } from "react"
import {
  defaultPreset,
  draftName,
  loadDrafts,
  loadMedia,
  loadPresets,
  loadPrinters,
  loadTemplates,
  pickMedia,
  pickTemplate,
} from "@/label/templates"
import type { Draft, Media, Preset, Printer, Template } from "@/label/types"
import { parseDraftFromHash } from "@/label/deeplink"
import { LabelCanvas } from "@/components/LabelCanvas"
import { DraftDetail } from "@/components/DraftDetail"

export function LabelApp() {
  const [templates, setTemplates] = useState<Template[] | null>(null)
  const [drafts, setDrafts] = useState<Draft[] | null>(null)
  const [media, setMedia] = useState<Media[] | null>(null)
  const [presets, setPresets] = useState<Preset[] | null>(null)
  const [printers, setPrinters] = useState<Printer[] | null>(null)
  const [loadError, setLoadError] = useState("")
  const [selected, setSelected] = useState<number | null>(null)
  // A draft passed in via a #/draft?... deep link (AI → link → print).
  const [linkedDraft, setLinkedDraft] = useState<Draft | null>(() =>
    parseDraftFromHash(window.location.hash),
  )

  useEffect(() => {
    Promise.all([loadTemplates(), loadDrafts(), loadMedia(), loadPresets(), loadPrinters()])
      .then(([t, d, m, p, pr]) => {
        setTemplates(t)
        setDrafts(d)
        setMedia(m)
        setPresets(p)
        setPrinters(pr)
      })
      .catch((e) => setLoadError((e as Error).message))
  }, [])

  useEffect(() => {
    const onHash = () => setLinkedDraft(parseDraftFromHash(window.location.hash))
    window.addEventListener("hashchange", onHash)
    return () => window.removeEventListener("hashchange", onHash)
  }, [])

  function closeLinkedDraft() {
    setLinkedDraft(null)
    if (window.location.hash) {
      window.history.replaceState(null, "", window.location.pathname + window.location.search)
    }
  }

  if (loadError)
    return <p className="text-destructive p-4 text-sm">Failed to load config: {loadError}</p>
  if (!templates || !drafts || !media || !presets || !printers)
    return <p className="text-muted-foreground p-4 text-sm">Loading…</p>

  // A deep link takes precedence — open straight into the linked draft.
  if (linkedDraft) {
    return (
      <DraftDetail
        draft={linkedDraft}
        templates={templates}
        media={media}
        presets={presets}
        printers={printers}
        onBack={closeLinkedDraft}
      />
    )
  }

  if (selected !== null && drafts[selected]) {
    return (
      <DraftDetail
        draft={drafts[selected]}
        templates={templates}
        media={media}
        presets={presets}
        printers={printers}
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
            presets={presets}
            onClick={() => setSelected(i)}
          />
        ))}
      </div>

      <footer className="text-muted-foreground mt-8 text-center text-xs">
        <a
          href={`${import.meta.env.BASE_URL}llms.txt`}
          target="_blank"
          rel="noreferrer"
          className="underline underline-offset-2"
        >
          Make a label with ChatGPT →
        </a>
        <p className="mt-1">
          Paste this site's link in a chat and ask for any part.
        </p>
      </footer>
    </main>
  )
}

function DraftCard({
  draft,
  templates,
  media,
  presets,
  onClick,
}: {
  draft: Draft
  templates: Template[]
  media: Media[]
  presets: Preset[]
  onClick: () => void
}) {
  // Preview the draft's default preset (else best-fit template + media).
  const preset = defaultPreset(draft, presets, templates)
  const template =
    (preset && templates.find((t) => t.id === preset.template)) ??
    pickTemplate(draft, templates)
  const m = (preset && media.find((x) => x.id === preset.media)) ?? pickMedia(template, media)

  return (
    <button
      onClick={onClick}
      className="bg-card hover:bg-muted/40 flex flex-col items-center gap-2 rounded-xl border p-3 text-center transition active:scale-[0.98]"
    >
      <div className="flex h-28 items-center justify-center">
        {template && m ? (
          <LabelCanvas
            template={template}
            fields={draft.fields}
            media={m}
            orientation={preset?.orientation}
            maxEdgePx={110}
          />
        ) : (
          <span className="text-muted-foreground text-xs">no template</span>
        )}
      </div>
      <span className="line-clamp-2 text-sm font-medium">{draftName(draft)}</span>
    </button>
  )
}

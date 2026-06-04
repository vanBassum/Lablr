import { load } from "js-yaml"
import { parseMedia, parsePrinter, parseTemplate } from "./load"
import type { Draft, Media, Orientation, Preset, Printer, Template } from "./types"

// Config is fetched at runtime from /config (the public folder), NOT bundled —
// so templates/drafts can be edited or added without rebuilding, matching the
// decoupled config direction (roadmap items 19–20). public/ is the interim host
// before this becomes a served label-config repo.
const CONFIG_BASE = `${import.meta.env.BASE_URL}config/`

async function fetchText(url: string): Promise<string> {
  const res = await fetch(url)
  if (!res.ok) throw new Error(`${url}: ${res.status} ${res.statusText}`)
  return res.text()
}

/** Read the template manifest, then fetch and parse each listed template. */
export async function loadTemplates(): Promise<Template[]> {
  const base = `${CONFIG_BASE}templates/`
  const ids = JSON.parse(await fetchText(`${base}index.json`)) as string[]
  return Promise.all(
    ids.map(async (id) => parseTemplate(await fetchText(`${base}${id}.yaml`))),
  )
}

/** Load the shared draft fixtures (data only — not bound to any template). */
export async function loadDrafts(): Promise<Draft[]> {
  return load(await fetchText(`${CONFIG_BASE}drafts.yaml`)) as Draft[]
}

/** Load presets — reusable (template + media) output formats. */
export async function loadPresets(): Promise<Preset[]> {
  return load(await fetchText(`${CONFIG_BASE}presets.yaml`)) as Preset[]
}

/** Read the media manifest, then fetch and parse each physical-label profile. */
export async function loadMedia(): Promise<Media[]> {
  const base = `${CONFIG_BASE}media/`
  const ids = JSON.parse(await fetchText(`${base}index.json`)) as string[]
  return Promise.all(
    ids.map(async (id) => parseMedia(await fetchText(`${base}${id}.yaml`))),
  )
}

/** Read the printer manifest, then fetch and parse each printer profile. */
export async function loadPrinters(): Promise<Printer[]> {
  const base = `${CONFIG_BASE}printers/`
  const ids = JSON.parse(await fetchText(`${base}index.json`)) as string[]
  return Promise.all(
    ids.map(async (id) => parsePrinter(await fetchText(`${base}${id}.yaml`))),
  )
}

/** The printer to print a media on: first compatible (per `media.printers`), else first. */
export function printerForMedia(
  media: Media | undefined,
  printers: Printer[],
): Printer | undefined {
  if (media?.printers?.length) {
    const match = printers.find((p) => media.printers!.includes(p.id))
    if (match) return match
  }
  return printers[0]
}

/** A template can render a draft when the draft supplies all the fields it needs. */
export function templateAccepts(template: Template, draft: Draft): boolean {
  return template.fields.every((f) => f.key in draft.values)
}

/** A human-readable name for a draft (its label, or its values joined). */
export const draftName = (d: Draft) => d.label ?? Object.values(d.values).join(" · ")

/**
 * A template fits a media when its design size is no larger than the label.
 * In landscape the label's dimensions are effectively swapped.
 */
export function templateFitsMedia(
  template: Template,
  media: Media,
  orientation: Orientation = "portrait",
): boolean {
  const w = orientation === "landscape" ? media.size.h : media.size.w
  const h = orientation === "landscape" ? media.size.w : media.size.h
  return template.size.w <= w && template.size.h <= h
}

/** The template to show for a draft by default: its suggestion, else first that fits. */
export function pickTemplate(draft: Draft, templates: Template[]): Template | undefined {
  const compatible = templates.filter((t) => templateAccepts(t, draft))
  return (
    (draft.template && compatible.find((t) => t.id === draft.template)) || compatible[0]
  )
}

/** The media to show for a template by default: first that fits, else first available. */
export function pickMedia(
  template: Template | undefined,
  media: Media[],
): Media | undefined {
  if (!template) return media[0]
  return media.find((m) => templateFitsMedia(template, m)) ?? media[0]
}

/** Presets whose template can render this draft's data. */
export function compatiblePresets(
  draft: Draft,
  presets: Preset[],
  templates: Template[],
): Preset[] {
  return presets.filter((p) => {
    const t = templates.find((x) => x.id === p.template)
    return t ? templateAccepts(t, draft) : false
  })
}

/** Default preset for a draft: the one using its suggested template, else the first. */
export function defaultPreset(
  draft: Draft,
  presets: Preset[],
  templates: Template[],
): Preset | undefined {
  const compatible = compatiblePresets(draft, presets, templates)
  return compatible.find((p) => p.template === draft.template) ?? compatible[0]
}

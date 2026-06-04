import { parseMedia, parseTemplate } from "./load"
import type { Draft, Media, Template } from "./types"

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
  return JSON.parse(await fetchText(`${CONFIG_BASE}drafts.json`)) as Draft[]
}

/** Read the media manifest, then fetch and parse each physical-label profile. */
export async function loadMedia(): Promise<Media[]> {
  const base = `${CONFIG_BASE}media/`
  const ids = JSON.parse(await fetchText(`${base}index.json`)) as string[]
  return Promise.all(
    ids.map(async (id) => parseMedia(await fetchText(`${base}${id}.yaml`))),
  )
}

/** A template can render a draft when the draft supplies all the fields it needs. */
export function templateAccepts(template: Template, draft: Draft): boolean {
  return template.fields.every((f) => f.key in draft.values)
}

/** A template fits a media when its design size is no larger than the label. */
export function templateFitsMedia(template: Template, media: Media): boolean {
  return template.size.w <= media.size.w && template.size.h <= media.size.h
}

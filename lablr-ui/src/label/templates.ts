import { parseTemplate } from "./load"
import type { Draft, Template } from "./types"

// Config is fetched at runtime from /config (the public folder), NOT bundled —
// so templates can be edited/added without rebuilding, matching the decoupled
// config direction (roadmap items 19–20). public/ is the interim host before
// this becomes a served label-config repo.
const CONFIG_BASE = `${import.meta.env.BASE_URL}config/templates/`

async function fetchText(url: string): Promise<string> {
  const res = await fetch(url)
  if (!res.ok) throw new Error(`${url}: ${res.status} ${res.statusText}`)
  return res.text()
}

/** Read the manifest, then fetch and parse each listed template. */
export async function loadTemplates(): Promise<Template[]> {
  const ids = JSON.parse(await fetchText(`${CONFIG_BASE}index.json`)) as string[]
  return Promise.all(
    ids.map(async (id) => parseTemplate(await fetchText(`${CONFIG_BASE}${id}.yaml`))),
  )
}

/** Drafts derived from each template's bundled sample fixtures. */
export function draftsFrom(templates: Template[]): Draft[] {
  return templates.flatMap((t) =>
    (t.samples ?? []).map((values) => ({ templateId: t.id, values })),
  )
}

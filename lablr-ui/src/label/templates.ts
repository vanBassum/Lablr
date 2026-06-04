import smdBasicYaml from "./templates/smd-basic.yaml?raw"
import { parseTemplate } from "./load"
import type { Draft, Template } from "./types"

// Config-as-code in the PWA for now; templates move to the label-config repo
// later (roadmap item 20), loaded the same way (parse YAML → Template).
export const templates: Template[] = [parseTemplate(smdBasicYaml)]

export function templateById(id: string): Template | undefined {
  return templates.find((t) => t.id === id)
}

/** Drafts derived from each template's bundled sample fixtures. */
export const sampleDrafts: Draft[] = templates.flatMap((t) =>
  (t.samples ?? []).map((values) => ({ templateId: t.id, values })),
)

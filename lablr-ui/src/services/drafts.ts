import { load } from "js-yaml"
import type { Draft } from "@/types"

// Drafts authored as YAML in public/label-config/drafts. The id is derived from
// the filename (draft files contain only `fields`).
const draftModules = import.meta.glob("../../public/label-config/drafts/*.yaml", {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>

const drafts: Draft[] = Object.entries(draftModules).map(([file, raw]) => {
  const parsed = load(raw) as { fields?: Record<string, string> }
  const id = file.split("/").pop()!.replace(/\.yaml$/, "")
  return { id, fields: parsed.fields ?? {} }
})

export class DraftService {
  getDrafts(): Draft[] {
    return drafts
  }

  getDraft(id: string): Draft | undefined {
    return drafts.find((d) => d.id === id)
  }
}

export const draftService = new DraftService()

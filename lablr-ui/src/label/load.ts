import { load } from "js-yaml"
import type { Template } from "./types"

/** Parse a YAML template string into a Template, with light structural checks. */
export function parseTemplate(yamlText: string): Template {
  const t = load(yamlText) as Template | undefined
  if (!t || typeof t.id !== "string") {
    throw new Error("template: missing string `id`")
  }
  if (!t.size || typeof t.size.w !== "number" || typeof t.size.h !== "number") {
    throw new Error(`template ${t.id}: missing size { w, h } in mm`)
  }
  if (!Array.isArray(t.fields) || t.fields.length === 0) {
    throw new Error(`template ${t.id}: missing fields`)
  }
  if (!t.layout) {
    throw new Error(`template ${t.id}: missing layout`)
  }
  return t
}

import { load } from "js-yaml"
import type { Media, Template } from "./types"

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

/** Parse a YAML media string into a Media, with light structural checks. */
export function parseMedia(yamlText: string): Media {
  const m = load(yamlText) as Media | undefined
  if (!m || typeof m.id !== "string") {
    throw new Error("media: missing string `id`")
  }
  if (!m.size || typeof m.size.w !== "number" || typeof m.size.h !== "number") {
    throw new Error(`media ${m.id}: missing size { w, h } in mm`)
  }
  return m
}

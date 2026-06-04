import { load } from "js-yaml"
import type { Media, Printer, Template } from "./types"

/** Parse a YAML template string into a Template, with light structural checks. */
export function parseTemplate(yamlText: string): Template {
  const t = load(yamlText) as Template | undefined
  if (!t || typeof t.id !== "string") {
    throw new Error("template: missing string `id`")
  }
  // designSize is optional; if present, validate it
  if (t.designSize && (typeof t.designSize.width !== "number" || typeof t.designSize.height !== "number")) {
    throw new Error(`template ${t.id}: designSize must have width and height in mm`)
  }
  if (!t.fields || typeof t.fields !== "object") {
    throw new Error(`template ${t.id}: missing fields object`)
  }
  if (!Array.isArray(t.elements) || t.elements.length === 0) {
    throw new Error(`template ${t.id}: missing elements array`)
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

/** Parse a YAML printer profile, with light structural checks. */
export function parsePrinter(yamlText: string): Printer {
  const p = load(yamlText) as Printer | undefined
  if (!p || typeof p.id !== "string") {
    throw new Error("printer: missing string `id`")
  }
  return p
}

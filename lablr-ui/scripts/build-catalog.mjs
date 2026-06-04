// Compile the template config into a single public/catalog.json that the AI
// fetches to know which templates + fields exist. Generated from the YAML so it
// never drifts. Runs as part of `pnpm build` (and `pnpm catalog`).
import { readFile, writeFile } from "node:fs/promises"
import { load } from "js-yaml"

const TEMPLATES = new URL("../public/config/templates/", import.meta.url)

let templates = []
try {
  const ids = JSON.parse(await readFile(new URL("index.json", TEMPLATES), "utf8"))
  for (const id of ids) {
    const t = load(await readFile(new URL(`${id}.yaml`, TEMPLATES), "utf8"))
    const fields = Object.entries(t.fields ?? {}).map(([key, field]) => ({ key, label: field.label }))
    templates.push({
      id: t.id,
      name: t.name,
      designSize: t.designSize,
      fields,
    })
  }
} catch (e) {
  // Config directory not yet created (e.g., during redesign)
  templates = []
}

const catalog = {
  linkFormat: "#/draft?t=<templateId>&<fieldKey>=<value>&...",
  howto:
    "Pick the one template that matches what the user is labeling. Fill EVERY " +
    "field listed for it. URL-encode values. Return this site's origin + linkFormat.",
  templates,
}

const out = new URL("../public/catalog.json", import.meta.url)
await writeFile(out, JSON.stringify(catalog, null, 2) + "\n")
console.log(`catalog.json: ${templates.length} templates`)

// --- llms.txt: self-contained instructions an AI can read in one fetch ---
const BASE = "https://vanbassum.github.io/Lablr/"

const list = templates
  .map(
    (t) =>
      `- ${t.id} — "${t.name}" (${t.size?.w}×${t.size?.h}mm) — fields: ` +
      t.fields.map((f) => f.key).join(", "),
  )
  .join("\n")

// Example from the electronics template if present, else the first one.
const ex = templates.find((t) => t.id === "smd-basic") ?? templates[0]
let exUrl = ""
if (ex) {
  const exValues =
    ex.id === "smd-basic"
      ? { name: "TIP31", subtitle: "NPN power transistor", package: "TO-220" }
      : Object.fromEntries(ex.fields.map((f) => [f.key, "value"]))
  const exQuery = Object.entries(exValues)
    .map(([k, v]) => `${k}=${encodeURIComponent(v)}`)
    .join("&")
  exUrl = `${BASE}#/draft?t=${ex.id}&${exQuery}`
}

const llms = templates.length > 0
  ? `# Lablr — label maker

When the user asks for a label, build a URL they can open on their phone to
preview and print it. Reply with that URL.

## Steps
1. Pick ONE template below that matches what the user is labeling.
2. Fill EVERY field it lists (omit none).
3. Build this URL, URL-encoding each value:
   ${BASE}#/draft?t=<templateId>&<fieldKey>=<value>&...
4. Reply with the full URL.

## Templates
${list}

## Example
"a TIP31 NPN power transistor in a TO-220 package" →
${exUrl}
`
  : `# Lablr — label maker (templates in redesign)

Configuration is being redesigned. Check back soon.
`

await writeFile(new URL("../public/llms.txt", import.meta.url), llms)
console.log(`llms.txt: ${templates.length} templates`)

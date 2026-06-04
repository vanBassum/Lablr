// Compile the template config into a single public/catalog.json that the AI
// fetches to know which templates + fields exist. Generated from the YAML so it
// never drifts. Runs as part of `pnpm build` (and `pnpm catalog`).
import { readFile, writeFile } from "node:fs/promises"
import { load } from "js-yaml"

const TEMPLATES = new URL("../public/config/templates/", import.meta.url)

const ids = JSON.parse(await readFile(new URL("index.json", TEMPLATES), "utf8"))

const templates = []
for (const id of ids) {
  const t = load(await readFile(new URL(`${id}.yaml`, TEMPLATES), "utf8"))
  templates.push({
    id: t.id,
    name: t.name,
    size: t.size,
    fields: (t.fields ?? []).map((f) => ({ key: f.key, label: f.label })),
  })
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

import type { Template } from "@/types"

// Thin REST client for template management (list + delete). Authoring/editing
// templates is done via the AI/MCP (upsert_template); the UI just lists and
// removes them, mirroring label stocks.

export function listTemplates(): Promise<Template[]> {
  return fetch("/api/templates").then((r) => {
    if (!r.ok) throw new Error(`${r.status}`)
    return r.json() as Promise<Template[]>
  })
}

export async function deleteTemplate(id: string): Promise<void> {
  const res = await fetch(`/api/templates/${encodeURIComponent(id)}`, { method: "DELETE" })
  if (!res.ok && res.status !== 404) throw new Error(`${res.status}`)
}

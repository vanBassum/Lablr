import type { LabelStock, Printer } from "@/types"

// Thin REST client for managing label stocks (and reading printers for the
// compatible-printers picker). Mirrors the backend's /api CRUD.

async function json<T>(res: Response): Promise<T> {
  if (!res.ok) {
    let message = `${res.status}`
    try {
      const body = await res.json()
      if (body?.error) message = body.error
    } catch {
      /* non-JSON error body */
    }
    throw new Error(message)
  }
  return res.json() as Promise<T>
}

export function listLabels(): Promise<LabelStock[]> {
  return fetch("/api/labels").then((r) => json<LabelStock[]>(r))
}

export function listPrinters(): Promise<Printer[]> {
  return fetch("/api/printers").then((r) => json<Printer[]>(r))
}

export function saveLabel(label: LabelStock): Promise<LabelStock> {
  return fetch(`/api/labels/${encodeURIComponent(label.id)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(label),
  }).then((r) => json<LabelStock>(r))
}

export async function deleteLabel(id: string): Promise<void> {
  const res = await fetch(`/api/labels/${encodeURIComponent(id)}`, { method: "DELETE" })
  if (!res.ok && res.status !== 404) throw new Error(`${res.status}`)
}

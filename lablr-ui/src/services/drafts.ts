import type { Draft } from "@/types"

// Drafts live in the backend's in-memory store. The list is loaded once at
// startup; a single draft (e.g. an AI deep link #/d/{id}) can be fetched on demand.

let resolveReady!: () => void
export const draftsReady = new Promise<void>((r) => (resolveReady = r))

export class DraftService {
  private drafts = new Map<string, Draft>()

  async load(): Promise<void> {
    const res = await fetch("/api/drafts")
    if (!res.ok) throw new Error(`drafts load failed: ${res.status}`)
    const list = (await res.json()) as Draft[]
    this.drafts = new Map(list.map((d) => [d.id, d]))
    resolveReady()
  }

  getDrafts(): Draft[] {
    return Array.from(this.drafts.values())
  }

  getDraft(id: string): Draft | undefined {
    return this.drafts.get(id)
  }

  /** Fetch a single draft by id (for deep links), caching it locally. */
  async fetchDraft(id: string): Promise<Draft | undefined> {
    const cached = this.drafts.get(id)
    if (cached) return cached
    const res = await fetch(`/api/drafts/${id}`)
    if (!res.ok) return undefined
    const draft = (await res.json()) as Draft
    this.drafts.set(draft.id, draft)
    return draft
  }
}

export const draftService = new DraftService()

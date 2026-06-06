// Registered print bridges (Option C). Each is a persistent record with a secret
// token; the device connects to the backend with it and its live state appears
// here. The backend relays rendered job bytes to a chosen bridge.

export interface PrintAgent {
  id: string
  name: string
  online: boolean
  status: "ready" | "no-printer" | "offline" | string
  isDefault: boolean
  deviceId?: string | null
  lastSeen?: string | null
  createdAt: string
}

/** Returned only by create — the token is shown to the user once. */
export interface PrintAgentCreated {
  id: string
  name: string
  token: string
}

async function ok(res: Response): Promise<void> {
  if (res.ok) return
  let message = `${res.status}`
  try {
    const body = await res.json()
    if (body?.error) message = body.error
  } catch {
    /* non-JSON error body */
  }
  throw new Error(message)
}

async function json<T>(res: Response): Promise<T> {
  await ok(res)
  return res.json() as Promise<T>
}

export function listAgents(): Promise<PrintAgent[]> {
  return fetch("/api/agents").then((r) => json<PrintAgent[]>(r))
}

export function createAgent(name: string): Promise<PrintAgentCreated> {
  return fetch("/api/agents", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name }),
  }).then((r) => json<PrintAgentCreated>(r))
}

export function renameAgent(id: string, name: string): Promise<PrintAgent> {
  return fetch(`/api/agents/${encodeURIComponent(id)}`, {
    method: "PUT",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name }),
  }).then((r) => json<PrintAgent>(r))
}

export async function deleteAgent(id: string): Promise<void> {
  const res = await fetch(`/api/agents/${encodeURIComponent(id)}`, { method: "DELETE" })
  if (!res.ok && res.status !== 404) throw new Error(`${res.status}`)
}

export async function setDefaultAgent(id: string): Promise<void> {
  await ok(await fetch(`/api/agents/${encodeURIComponent(id)}/default`, { method: "POST" }))
}

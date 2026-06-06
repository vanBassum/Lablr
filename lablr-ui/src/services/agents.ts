// Live print-bridge agents: ESP devices connected to the backend over WebSocket
// (Option C). The backend relays the rendered job bytes to a chosen agent, which
// streams them to its USB printer. Distinct from config "printers" (profiles).

export interface PrintAgent {
  id: string
  name: string
  status: "ready" | "no-printer" | string
  connectedAt: string
  lastSeen: string
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

export function listAgents(): Promise<PrintAgent[]> {
  return fetch("/api/agents").then(async (r) => {
    await ok(r)
    return r.json() as Promise<PrintAgent[]>
  })
}

/** Hand a rendered job (raw printer bytes) to the backend for relay to an agent. */
export async function printToAgent(id: string, bytes: Uint8Array): Promise<void> {
  const res = await fetch(`/api/agents/${encodeURIComponent(id)}/print`, {
    method: "POST",
    headers: { "Content-Type": "application/octet-stream" },
    body: bytes as BlobPart,
  })
  await ok(res)
}

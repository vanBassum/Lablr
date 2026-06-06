// Client for the backend renderer. The backend is the SINGLE renderer: the PWA
// fetches the rendered PNG to preview and the rendered job bytes to print, so
// what's previewed is exactly what prints. The frontend never rasterizes.

export interface PrintParams {
  draftId: string
  templateId?: string
  orientation?: string
  nudgeX?: number
  nudgeY?: number
}

function qs(params: Record<string, string | number | undefined>): string {
  const sp = new URLSearchParams()
  for (const [k, v] of Object.entries(params))
    if (v !== undefined && v !== "") sp.set(k, String(v))
  return sp.toString()
}

/** URL of the preview PNG for a draft (use as an <img> src). */
export function draftPreviewUrl(draftId: string, templateId?: string, orientation?: string): string {
  return `/api/render/preview?${qs({ draftId, templateId, orientation })}`
}

/** URL of the preview PNG for a template with sample values (Templates page). */
export function templatePreviewUrl(templateId: string, orientation?: string): string {
  return `/api/render/template-preview?${qs({ templateId, orientation })}`
}

/** Fetch the DYMO job bytes for a draft (desktop WebUSB transferOut). */
export async function fetchJobBytes(p: PrintParams): Promise<Uint8Array> {
  const res = await fetch(`/api/render/job?${qs({ ...p })}`)
  if (!res.ok) throw new Error(await errorText(res))
  return new Uint8Array(await res.arrayBuffer())
}

/** Print a draft on a bridge: the backend renders and relays the bytes. */
export async function printDraftToAgent(agentId: string, p: PrintParams): Promise<void> {
  const res = await fetch("/api/print/draft", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      draftId: p.draftId,
      templateId: p.templateId,
      agentId,
      orientation: p.orientation,
      nudgeX: p.nudgeX ?? 0,
      nudgeY: p.nudgeY ?? 0,
    }),
  })
  if (!res.ok) throw new Error(await errorText(res))
}

async function errorText(res: Response): Promise<string> {
  try {
    const body = await res.json()
    if (body?.error) return body.error as string
  } catch {
    /* non-JSON body */
  }
  return `${res.status}`
}

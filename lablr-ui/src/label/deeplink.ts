import type { Draft } from "./types"

/**
 * Parse a draft from a deep-link hash like
 *   #/draft?t=chemical-small&name=Acetone&formula=C3H6O
 * `t` is the suggested template; `label` (optional) is a display name; every
 * other param is a field value. Returns null if the hash isn't a draft link.
 */
export function parseDraftFromHash(hash: string): Draft | null {
  const m = hash.match(/^#\/draft\??(.*)$/)
  if (!m) return null

  const params = new URLSearchParams(m[1])
  const values: Record<string, string> = {}
  let template: string | undefined
  let label: string | undefined

  for (const [k, v] of params) {
    if (k === "t") template = v
    else if (k === "label") label = v
    else values[k] = v
  }

  return { label, template, values }
}

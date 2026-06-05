import { useEffect, useState } from "react"
import { load } from "js-yaml"
import registryRaw from "../../public/label-config/pictograms.yaml?raw"
import type { Pictogram } from "@/types"

// Named symbols → image assets, defined in label-config/pictograms.yaml.
// Images are static assets in public/, fetched by URL and preloaded once so the
// (synchronous) renderer can draw them without re-fetching.

const registry = (load(registryRaw) as { pictograms?: Record<string, Pictogram> }).pictograms ?? {}

const BASE = import.meta.env.BASE_URL
const images = new Map<string, HTMLImageElement>()

const entries = Object.entries(registry)
let pending = entries.length
let resolveReady!: () => void
export const pictogramsReady = new Promise<void>((r) => (resolveReady = r))
if (pending === 0) resolveReady()

// Track successful loads explicitly: official GHS SVGs often declare only a
// viewBox (no width/height), so img.naturalWidth can be 0 even when drawable.
const loaded = new Set<string>()

for (const [name, def] of entries) {
  const img = new Image()
  const done = () => {
    if (--pending === 0) resolveReady()
  }
  img.onload = () => {
    loaded.add(name)
    done()
  }
  img.onerror = () => {
    console.error(`Pictogram "${name}" failed to load: ${def.image}`)
    done()
  }
  img.src = `${BASE}label-config/pictograms/${def.image}`
  images.set(name, img)
}

/** A loaded, drawable image for a pictogram name, or undefined if unknown/unloaded. */
export function getPictogram(name: string): HTMLImageElement | undefined {
  return loaded.has(name) ? images.get(name) : undefined
}

/** True once every pictogram image has settled, so a re-render can draw them. */
export function usePictogramsReady(): boolean {
  const [ready, setReady] = useState(false)
  useEffect(() => {
    let active = true
    pictogramsReady.then(() => active && setReady(true))
    return () => {
      active = false
    }
  }, [])
  return ready
}

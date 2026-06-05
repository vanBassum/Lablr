import { useEffect, useState } from "react"
import { configReady, configService } from "@/services/config"

// Named symbols → image assets. The registry comes from the loaded config
// (GET /api/config); the SVGs are served by the backend at /pictograms. Images
// are preloaded once so the (synchronous) renderer can draw them.

const images = new Map<string, HTMLImageElement>()
const loaded = new Set<string>()

let resolveReady!: () => void
export const pictogramsReady = new Promise<void>((r) => (resolveReady = r))

// Preload once the config (and thus the registry) is available.
configReady.then(() => {
  const registry = configService.getPictogramRegistry()
  const entries = Object.entries(registry)
  let pending = entries.length
  if (pending === 0) {
    resolveReady()
    return
  }
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
    img.src = `/pictograms/${def.image}`
    images.set(name, img)
  }
})

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

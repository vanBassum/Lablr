import { useEffect, useState } from "react"
import { configReady, configService } from "@/services/config"
import { draftsReady, draftService } from "@/services/drafts"

// Kick off the runtime loads once, and expose a readiness gate for the app shell.
// Rendering (incl. pictograms) happens on the backend now, so the gate only
// waits on config + drafts.

let started = false
function start() {
  if (started) return
  started = true
  configService.load().catch((e) => console.error("config load failed", e))
  draftService.load().catch((e) => console.error("drafts load failed", e))
}

export function useAppReady(): boolean {
  const [ready, setReady] = useState(false)
  useEffect(() => {
    start()
    let active = true
    Promise.all([configReady, draftsReady]).then(() => active && setReady(true))
    return () => {
      active = false
    }
  }, [])
  return ready
}

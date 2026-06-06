/* eslint-disable react-refresh/only-export-components */
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useState,
  type ReactNode,
} from "react"
import { usePrinter } from "@/printer"
import { buildJobFromCanvas, type HeadOffset } from "@/dymo"
import { listAgents, printToAgent, type PrintAgent } from "@/services/agents"

// A print target is where a job goes: the local USB printer (WebUSB) or a
// remote bridge agent (relayed by the backend, Option C). The selected target
// drives the Print button; the header dropdown sets it.

export type PrintTarget =
  | { kind: "usb"; id: "usb"; name: string; ready: boolean }
  | { kind: "agent"; id: string; name: string; ready: boolean }

interface PrintTargetApi {
  targets: PrintTarget[]
  selectedId: string
  selected?: PrintTarget
  setSelectedId: (id: string) => void
  print: (canvas: HTMLCanvasElement, offset?: HeadOffset) => Promise<void>
}

const Ctx = createContext<PrintTargetApi | undefined>(undefined)
const STORAGE_KEY = "lablr.printTarget"
const USB_ID = "usb"
const webUsbAvailable =
  typeof navigator !== "undefined" && "usb" in (navigator as object)

export function PrintTargetProvider({ children }: { children: ReactNode }) {
  const usb = usePrinter()
  const [agents, setAgents] = useState<PrintAgent[]>([])
  const [selectedId, setSelectedIdState] = useState<string>(
    () => localStorage.getItem(STORAGE_KEY) ?? (webUsbAvailable ? USB_ID : ""),
  )

  // Poll the backend for connected bridges so they appear/disappear live.
  useEffect(() => {
    let active = true
    const load = () =>
      listAgents()
        .then((a) => active && setAgents(a))
        .catch(() => {})
    load()
    const t = setInterval(load, 5000)
    return () => {
      active = false
      clearInterval(t)
    }
  }, [])

  const targets = useMemo<PrintTarget[]>(() => {
    const list: PrintTarget[] = []
    if (webUsbAvailable) {
      const ready = usb.status === "connected" || usb.status === "printing"
      list.push({ kind: "usb", id: USB_ID, name: "USB (this device)", ready })
    }
    for (const a of agents)
      if (a.online) // only connected bridges can receive a job
        list.push({ kind: "agent", id: a.id, name: a.name, ready: a.status === "ready" })
    return list
  }, [agents, usb.status])

  // When there's no valid explicit selection, prefer the server-marked default
  // bridge (if online), else the local USB printer, else the first target.
  useEffect(() => {
    if (targets.length === 0 || targets.some((t) => t.id === selectedId)) return
    const def = agents.find((a) => a.isDefault && a.online)
    const pick =
      (def && targets.find((t) => t.id === def.id)) ??
      targets.find((t) => t.id === USB_ID) ??
      targets[0]
    setSelectedIdState(pick.id)
  }, [targets, selectedId, agents])

  const setSelectedId = useCallback((id: string) => {
    setSelectedIdState(id)
    localStorage.setItem(STORAGE_KEY, id)
  }, [])

  const print = useCallback(
    async (canvas: HTMLCanvasElement, offset?: HeadOffset) => {
      const target = targets.find((t) => t.id === selectedId)
      if (!target) throw new Error("No printer selected")
      if (target.kind === "usb") {
        await usb.print(canvas, offset) // WebUSB; prompts/connects on first use
      } else {
        const bytes = buildJobFromCanvas(canvas, offset)
        await printToAgent(target.id, bytes) // backend relays to the bridge
      }
    },
    [targets, selectedId, usb],
  )

  const selected = targets.find((t) => t.id === selectedId)

  return (
    <Ctx.Provider value={{ targets, selectedId, selected, setSelectedId, print }}>
      {children}
    </Ctx.Provider>
  )
}

export function usePrintTarget(): PrintTargetApi {
  const ctx = useContext(Ctx)
  if (!ctx) throw new Error("usePrintTarget must be used within a PrintTargetProvider")
  return ctx
}

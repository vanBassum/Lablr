/* eslint-disable react-refresh/only-export-components */
import {
  createContext,
  useCallback,
  useContext,
  useEffect,
  useRef,
  useState,
  type ReactNode,
} from "react"
import {
  getGrantedDymo,
  onUsbDisconnect,
  openDymo,
  printCanvas,
  type HeadOffset,
  type UsbDevice,
} from "@/dymo"

export type PrinterStatus = "disconnected" | "connecting" | "connected" | "printing"

interface PrinterApi {
  status: PrinterStatus
  error: string
  connect: () => Promise<void>
  disconnect: () => Promise<void>
  print: (canvas: HTMLCanvasElement, offset?: HeadOffset) => Promise<void>
}

const PrinterContext = createContext<PrinterApi | undefined>(undefined)

export function PrinterProvider({ children }: { children: ReactNode }) {
  // The open device lives in a ref so prints don't re-render the tree.
  const deviceRef = useRef<UsbDevice | null>(null)
  const [status, setStatus] = useState<PrinterStatus>("disconnected")
  const [error, setError] = useState("")

  // Silently reconnect to a previously-granted printer on load (no prompt).
  useEffect(() => {
    let active = true
    getGrantedDymo()
      .then((d) => {
        if (active && d) {
          deviceRef.current = d
          setStatus("connected")
        }
      })
      .catch(() => {})
    return () => {
      active = false
    }
  }, [])

  // Reflect physical unplug.
  useEffect(
    () =>
      onUsbDisconnect((d) => {
        if (d === deviceRef.current) {
          deviceRef.current = null
          setStatus("disconnected")
        }
      }),
    [],
  )

  const connect = useCallback(async () => {
    setError("")
    setStatus("connecting")
    try {
      deviceRef.current = await openDymo()
      setStatus("connected")
    } catch (e) {
      setStatus("disconnected")
      setError((e as Error).message)
      throw e
    }
  }, [])

  const disconnect = useCallback(async () => {
    const d = deviceRef.current
    deviceRef.current = null
    setStatus("disconnected")
    try {
      if (d) {
        await d.releaseInterface(0)
        await d.close()
      }
    } catch {
      // already gone — ignore
    }
  }, [])

  const print = useCallback(
    async (canvas: HTMLCanvasElement, offset?: HeadOffset) => {
      setError("")
      // Connect once on first use (or after a disconnect); reuse thereafter.
      if (!deviceRef.current) await connect()
      const d = deviceRef.current
      if (!d) throw new Error("No printer connected")
      setStatus("printing")
      try {
        await printCanvas(d, canvas, offset)
        setStatus("connected")
      } catch (e) {
        setStatus("connected")
        setError((e as Error).message)
        throw e
      }
    },
    [connect],
  )

  return (
    <PrinterContext.Provider value={{ status, error, connect, disconnect, print }}>
      {children}
    </PrinterContext.Provider>
  )
}

export function usePrinter(): PrinterApi {
  const ctx = useContext(PrinterContext)
  if (!ctx) throw new Error("usePrinter must be used within a PrinterProvider")
  return ctx
}

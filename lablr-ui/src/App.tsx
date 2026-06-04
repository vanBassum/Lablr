import { WebUsbProbe } from "@/WebUsbProbe"

export function App() {
  return (
    <div className="flex min-h-svh p-6">
      <div className="flex max-w-2xl min-w-0 flex-col gap-4 text-sm leading-loose">
        <div>
          <h1 className="font-medium">Lablr — WebUSB printer probe</h1>
          <p className="text-muted-foreground">
            Item 4 spike: confirm Chrome can open and claim the DYMO over WebUSB.
          </p>
        </div>
        <WebUsbProbe />
      </div>
    </div>
  )
}

export default App

import { WebUsbProbe } from "@/WebUsbProbe"
import { DymoPrintTest } from "@/DymoPrintTest"

export function App() {
  return (
    <div className="flex min-h-svh p-6">
      <div className="flex max-w-2xl min-w-0 flex-col gap-8 text-sm leading-loose">
        <div>
          <h1 className="font-medium">Lablr — item 4: print a label</h1>
          <p className="text-muted-foreground">
            Render → preview → raster → print to the DYMO LabelWriter 450 over WebUSB.
          </p>
        </div>
        <DymoPrintTest />
        <details>
          <summary className="text-muted-foreground cursor-pointer">
            WebUSB connection probe (diagnostics)
          </summary>
          <div className="mt-3">
            <WebUsbProbe />
          </div>
        </details>
      </div>
    </div>
  )
}

export default App

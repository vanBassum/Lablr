import { LightbulbIcon } from "lucide-react"
import { toast } from "sonner"

import { Button } from "@/components/ui/button"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Switch } from "@/components/ui/switch"
import { Label } from "@/components/ui/label"
import { useDeviceInfo } from "@/hooks/use-device-info"
import { describe, useLed } from "@/hooks/use-led"

/**
 * The home screen, and it is the PRODUCT — nothing else.
 *
 * In this template the product is the LED demo: the board LED is lit while the
 * device is connected to its relay server, and the whole feature is one app-layer
 * manager (main/app/LedManager) plus this file. That is the worked example of how a
 * feature is added, browser half included.
 *
 * A real product replaces the CONTENTS of this file and the first entry in
 * AppSidebar's nav, and deletes LedManager along with `getLed`/`setLed` in
 * backend.ts and the `use-led` hook. The route id stays "home", so a bookmark to
 * "/" keeps landing on whatever that product's own screen is.
 */
export default function HomePage() {
  const { state, busy, error, setEnabled, refresh } = useLed()
  const info = useDeviceInfo()

  return (
    <div className="mx-auto max-w-2xl space-y-6">
      <div className="flex items-center gap-3">
        <LightbulbIcon
          className={`size-6 ${state?.on ? "text-primary" : "text-muted-foreground"}`}
        />
        <h1 className="text-2xl font-bold">LED</h1>
      </div>

      <Card>
        <CardContent className="flex items-center justify-between gap-4">
          <div>
            <p className="text-sm font-medium">{describe(state)}</p>
            <p className="text-muted-foreground text-sm">
              The board LED is lit while the device is connected to its relay server.
              Turn the indication off to leave it dark regardless of the link.
            </p>
          </div>
          <div className="flex shrink-0 items-center gap-2">
            <Label htmlFor="led-enabled" className="sr-only">
              Show the relay link on the onboard LED
            </Label>
            <Switch
              id="led-enabled"
              checked={state?.enabled === true}
              onCheckedChange={(next) =>
                setEnabled(next, (m) =>
                  toast.error("Failed to switch the LED", { description: m }),
                )
              }
              disabled={state === null || busy}
            />
          </div>
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>State</CardTitle>
        </CardHeader>
        <CardContent>
          <dl className="grid grid-cols-2 gap-x-8 gap-y-3 text-sm">
            <Field
              label="Indication"
              value={state ? (state.enabled ? "On" : "Off") : "…"}
            />
            <Field
              label="Relay link"
              value={state ? (state.connected ? "Connected" : "Down") : "…"}
            />
            <Field label="LED" value={state ? (state.on ? "Lit" : "Dark") : "…"} />
            <Field label="Device" value={info?.name ?? "…"} />
          </dl>

          {error && (
            <p className="text-destructive mt-4 text-sm">
              Last poll failed: {error}. Showing the last known state.
            </p>
          )}

          <Button variant="outline" className="mt-4" onClick={refresh}>
            Refresh now
          </Button>
        </CardContent>
      </Card>
    </div>
  )
}

function Field({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <dt className="text-muted-foreground text-xs">{label}</dt>
      <dd className="font-medium">{value}</dd>
    </div>
  )
}

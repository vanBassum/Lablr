import { useState } from "react"
import { DraftDetail } from "@/components/DraftDetail"

export function LabelApp() {
  const [selected, setSelected] = useState<string | null>(null)

  if (selected) {
    return (
      <DraftDetail
        draftName={selected}
        onBack={() => setSelected(null)}
      />
    )
  }

  return (
    <main className="flex-1 p-4">
      <h1 className="text-muted-foreground mb-3 text-sm">Pick a label to print</h1>
      <div className="grid grid-cols-2 gap-3">
        <DraftCardPlaceholder label="Label 1" onClick={() => setSelected("Label 1")} />
        <DraftCardPlaceholder label="Label 2" onClick={() => setSelected("Label 2")} />
        <DraftCardPlaceholder label="Label 3" onClick={() => setSelected("Label 3")} />
        <DraftCardPlaceholder label="Label 4" onClick={() => setSelected("Label 4")} />
      </div>

      <footer className="text-muted-foreground mt-8 text-center text-xs">
        <a
          href={`${import.meta.env.BASE_URL}llms.txt`}
          target="_blank"
          rel="noreferrer"
          className="underline underline-offset-2"
        >
          Make a label with ChatGPT →
        </a>
        <p className="mt-1">
          Paste this site's link in a chat and ask for any part.
        </p>
      </footer>
    </main>
  )
}

function DraftCardPlaceholder({
  label,
  onClick,
}: {
  label: string
  onClick: () => void
}) {
  return (
    <button
      onClick={onClick}
      className="bg-card hover:bg-muted/40 flex flex-col items-center gap-2 rounded-xl border p-3 text-center transition active:scale-[0.98]"
    >
      <div className="flex h-28 items-center justify-center rounded bg-slate-100">
        <span className="text-muted-foreground text-xs">(render pipeline)</span>
      </div>
      <span className="line-clamp-2 text-sm font-medium">{label}</span>
    </button>
  )
}

import { draftPreviewUrl } from "@/services/printApi"
import type { Draft } from "@/types"

// Thumbnail of a draft, rendered by the backend (the single renderer) — the same
// bitmap that prints, scaled to fit width×height.
export function DraftPreview({
  draft,
  width = 100,
  height = 100,
}: {
  draft: Draft
  width?: number
  height?: number
}) {
  return (
    <img
      src={draftPreviewUrl(draft.id)}
      alt=""
      width={width}
      height={height}
      style={{
        width: `${width}px`,
        height: `${height}px`,
        objectFit: "contain",
        imageRendering: "pixelated",
        display: "block",
      }}
    />
  )
}

import type { Draft, Template } from "./types"

// First hardcoded template (roadmap item 9). Config-as-code for now; moves to
// the label-config repo later (item 20).
export const smdBasic: Template = {
  id: "smd-basic",
  name: "SMD / through-hole part",
  widthMm: 25,
  heightMm: 25,
  fields: [
    { key: "name", label: "Part", type: "text" },
    { key: "subtitle", label: "Description", type: "text" },
    { key: "package", label: "Package", type: "text" },
  ],
  layout: {
    type: "stack",
    direction: "vertical",
    gap: 10,
    children: [
      { type: "text", field: "name", size: 52, weight: "bold", align: "center" },
      { type: "text", field: "subtitle", size: 20, align: "center" },
      { type: "text", field: "package", size: 30, align: "center" },
    ],
  },
}

export const templates: Template[] = [smdBasic]

export function templateById(id: string): Template | undefined {
  return templates.find((t) => t.id === id)
}

// Sample drafts — design-time fixtures so a template can be previewed against
// real data while it's being built.
export const sampleDrafts: Draft[] = [
  {
    templateId: "smd-basic",
    values: { name: "BC547", subtitle: "NPN Transistor", package: "TO-92" },
  },
  {
    templateId: "smd-basic",
    values: { name: "LM358", subtitle: "Dual Op-Amp", package: "SOIC-8" },
  },
  {
    templateId: "smd-basic",
    values: { name: "100nF", subtitle: "Ceramic X7R 50V", package: "0805" },
  },
]

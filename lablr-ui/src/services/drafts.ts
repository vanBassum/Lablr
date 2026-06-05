import type { Draft } from "@/types"

// Draft data matching public/label-config/drafts/*.yaml files
const SAMPLE_DRAFTS: Draft[] = [
  {
    id: "draft-1",
    fields: {
      name: "BC547",
      type: "NPN",
    },
  },
  {
    id: "draft-2",
    fields: {
      name: "LED",
      type: "Red 5mm",
    },
  },
  {
    id: "draft-3",
    fields: {
      name: "10kΩ",
      type: "Resistor",
    },
  },
  {
    id: "draft-4",
    fields: {
      name: "C1",
      type: "100µF Cap",
    },
  },
]

export class DraftService {
  getDrafts(): Draft[] {
    return SAMPLE_DRAFTS
  }

  getDraft(id: string): Draft | undefined {
    return SAMPLE_DRAFTS.find((d) => d.id === id)
  }
}

export const draftService = new DraftService()

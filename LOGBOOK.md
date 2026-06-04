# Lablr — Logbook

Append-only record of brainstorms and decisions. Newest entries go at the bottom. Don't rewrite history — if a decision is reversed, add a new entry explaining why.

---

## 2026-06-04 — End-to-end workflow defined (roadmap item 1)

**The real happy path** (workshop scenario): PC is off. A package arrives with parts. User grabs their phone and tells ChatGPT *"I need a label for this device, I want to store it in my Aidetek Small container."* ChatGPT creates a draft. User opens the PWA, sees the preview, taps Print. Label prints.

**v1 target flow (achievable, matches frontend-first):**
```
Talk to ChatGPT → it creates a DRAFT (data only: preset + field values)
              → draft saved on the HOSTED server (vanbassum.com, not the PC)
              → phone PWA opens the draft → renders → shows preview → one tap → prints over Bluetooth
```

Key points:
- ChatGPT never renders or prints. It only fills in draft *data*. The phone stays the canonical renderer AND the printing device — consistent with frontend-first.
- "PC is off" works because the draft lives on the hosted server, not the user's machine.
- This makes a small **server + draft storage required** for the real happy path (store a draft / fetch a draft — no rendering on the server). It is no longer "someday, if needed."

**Platform:** User is on **Android** → Chrome supports Web Bluetooth + PWA install. The phone-centric flow is viable; no iOS limitation to design around.

**The "even better" dream** — ChatGPT shows the draft and you just say "print it", never opening the PWA — is deferred, blocked by two hard walls:
1. ChatGPT can't talk to a Bluetooth printer (Web Bluetooth needs a browser + a user gesture on the phone).
2. ChatGPT showing the *real* preview would require server-side rendering, which violates "preview = print" (no second renderer).

This dream only becomes possible with a **networked (WiFi) printer** + server-side rendering — a heavier architecture, noted as a possible future, not v1.

**Consequence for roadmap item 2 (choose printer):** the printer choice decides which future is open. Bluetooth (e.g. Niimbot) → phone-driven flow (v1 target above). Networked printer → server could print directly, enabling the hands-free ChatGPT dream later.

**Note:** This does not change build order — we still prove plain printing first, before any ChatGPT wiring.

---

## 2026-06-04 — Refinement: ChatGPT hands off via a deep link

ChatGPT can return a **link** after creating the draft. That's the handoff mechanism:
```
talk to ChatGPT → it POSTs a draft to the server → server returns a draft link
   → user taps link → PWA opens DIRECTLY on that draft → preview → one tap → print
```

**Why this is good:**
- Removes the last navigation friction — no opening the app and hunting for the draft; the link lands you on it.
- Sidesteps the "ChatGPT can't show the real preview" wall entirely — it doesn't need to; the link carries you into the PWA where the single real preview lives.
- Still fully frontend-first: ChatGPT only produces draft *data* + a URL; the phone renders/previews/prints.

**Implication for later:** the server must mint a draft ID and the PWA needs deep-link routing (`/draft/:id`). Both small.

---

## 2026-06-04 — First printer + transport locked (roadmap items 2 & 3)

**Decision:** prototype against a **Dymo LabelWriter 450** already on hand, driven over **WebUSB from desktop Chrome on the dev PC**. Niimbot (Bluetooth) remains the production target.

**Why the Dymo first:**
- Already owned — fastest way to prove the rendering→print pipeline (roadmap items 4–10) against real hardware.
- The **450** is the freely-usable model: no RFID label lock (that's the 550 — avoid), third-party rolls fine, open/documented raster protocol, lots of OSS prior art.
- Direct thermal (fades) and 300 DPI — fine for a prototype. DPI is already a media/printer-profile property, so this difference from the Niimbot is handled by construction.

**Why WebUSB-from-PC first (not BLE, not OTG):**
- The 450 is **USB only** — no Bluetooth at all, so the Web Bluetooth path can't apply to it.
- Printing from desktop Chrome over WebUSB needs **no USB-OTG and no BLE**, removing every transport unknown for the prototype. (Driving a Dymo over WebUSB through Android OTG is plausible but unverified — deferred, not on the critical path.)

**Consequence (reverses the earlier "Web Bluetooth only" framing):** transport is no longer Bluetooth-only. It sits behind a single `Printer` interface with swappable implementations — **first impl WebUSB (desktop), second impl Web Bluetooth (Niimbot on Android)**. This abstraction is needed anyway for multiple printers (item 24); the Dymo just forces it early. The bitmap pipeline is identical across transports — only byte-delivery differs, so "preview = print" is unaffected. CLAUDE.md "Platform & transport" updated accordingly.

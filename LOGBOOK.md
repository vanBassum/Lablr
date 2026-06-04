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

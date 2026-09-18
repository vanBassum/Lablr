---
id: 2026-09-15-12h30
date: 2026-09-15
time: "12:30"
title: A mechanism with no second consumer is a seam the template pays for
builds-on: 2026-09-09-23h10
supersedes: 2026-09-09-21h50
---

**Before:** `2026-09-09-21h50` and `2026-09-09-23h10` built the case that a shell which
owns no page is the only shell that knows no commands, and the code followed it all the
way: `ui modules` as a manifest command, a `UiModule` per manager, four ES-module
bundles, a vendored contract, an import map publishing one React, a shared `_ui` kit.
Both shells could be read end to end without finding the name of a device command. That
was the property the design was for, and it held.

**What changed it:** DPS50xx, the first real fork, walked away from it — and could,
because `UiManager` documented the empty-manifest fallback, so the divergence was one
setting rather than a fork (`DPS50xx docs/reasoning/2026-09-10-17h24`). Then Bas, on the
template: *we decided against the whole modules stuff in the frontend.*

What that makes visible is a question the original design never had to answer, because
at the time there was only one product: **who is the second consumer?** The module seam
buys isolation between a page and the shell hosting it. That is worth ~2,000 lines only
when one shell hosts pages from products that cannot be built together. One product's
own shell hosting its own pages is not that, and neither is a template.

**What it cost, now that both ends are countable.**

- Upstream carried the mechanism: `UiManager`, `UiModule`, `ModuleHost`, the module
  registry, the contract, the React facades, `check-modules.mjs`, two dev middlewares,
  a hand-rolled primitives kit, and four per-module Vite/TS configs.
- The fork that took it paid ~2,000 lines and gave them all back.
- The running bill was the louder signal: the shared Tailwind `utilities` layer, two
  React instances, `export * from "react"` emitting nothing usable, four concurrent
  requests against a ten-socket lwIP budget. Five notes in two days are about the
  *seam*; none of them is about a feature.

**The delta, stated generally.** A seam is priced by the number of independently built
things that must meet at it. The module design was not wrong about isolation; it was
sized for a fleet that does not exist yet. A template's job is to make a first product
cheap, and a mechanism whose second consumer is hypothetical makes it more expensive —
a fork had to learn the module build before it could draw one screen.

**Why deleted rather than gated.** DPS gates it, correctly: the mechanism is upstream's,
so the fork's cheapest move is a setting. Upstream has no such asymmetry. With no
bundles shipped, `ui modules` would answer empty forever, which a relay already reads
as "serve the whole page" — so the gated manager would be a framework manager with
zero users and four declarations pointing at files that do not exist. That is the same
call as the MQTT/HA removal: git keeps it at `f7e0501`, and a fork that genuinely hosts
many products in one shell resurrects it rather than the template carrying it for them.

**What survived the revert, and is the durable part.** The pages themselves came back
byte-identical from `5498679` — `ConsolePage` and `SettingsPage` needed no edit at all,
and `FirmwarePage` came back with the one thing the module era actually added,
`partition status`. So a year of page work was never at stake; only the seam was. That
is itself the tell: if a mechanism can be removed without touching what it was
mechanism *for*, it was never load-bearing for the product.

What is **not** superseded is `2026-09-09-23h10`'s own subject. A shell that owns pages
does know device command names — `SettingsPage` says `settings list` — and that is now
accepted rather than solved, because with one shell per product there is no second
implementation to drift from. The day there are two, that note is the argument to
re-read.

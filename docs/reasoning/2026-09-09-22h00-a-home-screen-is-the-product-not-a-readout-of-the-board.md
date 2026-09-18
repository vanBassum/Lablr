---
id: 2026-09-09-22h00
date: 2026-09-09
time: "22:00"
title: A home screen is the product, not a readout of the board
builds-on: 2026-09-09-21h50
supersedes:
---

**Before:** the module system shipped with the LED as *both* a dashboard card and a page
behind its own sidebar entry, and the home page opened with a Device Info card — project,
firmware, ESP-IDF version, chip, CPU, IP, free heap, min free heap, device time — with
contributed cards below it.

That looked like generosity: two ways to reach the feature, and the device's vitals
up front. It was neither.

**What changed it:** Bas, looking at the running device — *"the home pages should be
specific to the project. I want that to directly show whatever is the most relevant to
whatever we build."*

The observation underneath it is about **who this file belongs to**. `HomePage.tsx` is in
a template. Anything it draws appears on every product built from Strux whether or not
that product wants it — so a shell-owned card is not a default, it is a decision imposed
on every fork. And the specific card it drew was the wrong content by its own measure: a
chip name and a heap figure are read *once, when something is wrong*, and never while
using the thing. On a device with one feature they pushed that feature below the fold.

So the home screen renders contributed cards and **nothing of its own**. The device's own
readout moved into a dialog behind the sidebar footer — which already shows the version
and the link state, so it is exactly where somebody looks when they want to know more
about either, and it needs no nav entry at all. That was Bas's second thought and a better
one than the Info *page* I was about to build: a page would have spent a permanent slot in
every fork's navigation on reference material.

**The page/card question resolved with it.** Two doors into one room is not two features.
A feature that *is* what the device is for belongs on the screen you land on, so the LED
declares a card and no page; the card absorbed what the page had (the state grid, the
manual refresh) and the page is gone. `UiPage` stays in the contract for a module with
genuinely more to show than a card can hold — which the LED never had.

**What this did not cost, and that is the point.** No contract change and no `hostApi`
bump. `pages` and `cards` both already existed; the LED simply stopped declaring one of
them, and `UiModule` gained a cards-only constructor. A shape change this visible passing
through a versioned seam without touching it is the seam working — the alternative reading,
that the contract needed a new "primary surface" concept, would have been the contract
having modelled the wrong thing.

The relay shell followed for free: a device's default view is now its cards, under an
*Overview* entry the shell owns the way it owns Devices and Cache, with manifest pages
after it. Both shells put the same bundle's card on the first screen, from the same
manifest, over different transports.

**The residual, stated rather than hidden:** a device that contributes no modules now has
an empty home screen. It says so and points at Settings, Console and Firmware. That is
the template's honest default — a fresh fork has no features yet — and an empty screen is
a better prompt than a card of numbers pretending to be a dashboard.

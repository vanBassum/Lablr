---
id: 2026-09-09-23h10
date: 2026-09-09
time: "23:10"
title: A shell that owns no page is the only shell that knows no commands
builds-on: 2026-09-09-21h50
supersedes: 2026-09-09-22h00
---

**Before:** modules were for the view a shell *could not have described*. Console,
Settings and Firmware were compiled into each shell, and the argument for keeping them
there was that they are framework features every device has, and that `settings list`
already describes itself — a declaration should stay a declaration.

**What changed it:** Bas, three times, converging. *Console, settings and firmware
should be modules.* *The shell never adds any menu items under 'device'.* *So also
overview if we want that, and home, are modules.*

The declaration argument was right about the data and wrong about the conclusion. What
it missed is visible in what writing those pages actually required: to give the relay a
Settings page I had to teach it `settings list`, `log list` and `partition status` **by
name**. That is the exact coupling the module system exists to remove, added by hand,
two hours after building the thing that removes it. And it was two implementations of
every page — six for three — each able to drift from the firmware whose data it showed.

So the rule is not "modules are for what a shell could not describe". It is:

> **A shell contributes nothing to a device's navigation.**

Everything else follows from it, including the parts I would not have chosen:

- **Cards had to go.** They rendered into a home screen the shell owned. Once no shell
  owns a page under a device, nothing hosts a card. One extension point where there
  were two.
- **The landing page is the first page the manifest declares.** Declaration order,
  which makes it the firmware's choice. `UiManager` head-inserts and the app's managers
  initialise after the framework's, so a product's own page lands first without anyone
  arranging it.
- **The framework ships modules of its own**, registered by the manager that owns the
  commands — `ConsoleManager` declares `console` the way it declares its command table.
  A fork gets them for free, which was the objection to making them modules, and gets
  them by the same mechanism as everything else.

**What it cost, and this is the honest part.** The contract went to 2, because three
pages needed things `request` cannot say: `upload` (a firmware image is not an
argument — it is a session), `download` (the same in reverse), and `logs` (the device's
session-0 broadcast). Without `logs` a Console module could only poll, which would have
been a *regression* on the shell it came from — so "everything is a module" forced a
capability rather than merely relocating code. `hostApi` min and max are both 2, so
this is fleet-visible: a v1 shell meeting a v2 device says "needs a newer shell".

It also cost a shared primitive kit. A module cannot use either shell's components —
this shell is radix-ui and the relay's is `@base-ui/react` — so four modules meant four
hand-rolled buttons until `modules/_ui` existed. And the settings page lost prismjs on
the way in: 30 KB of syntax highlighting is 30 KB of flash partition.

**What survives from [the note this supersedes](2026-09-09-22h00-a-home-screen-is-the-product-not-a-readout-of-the-board.md):**
the insight, not the mechanism. A home screen should be the product, and a chip name
and a heap figure are reference material that belongs behind the sidebar footer. What
changed is who provides that home screen. It was the shell, hosting cards; it is now
simply the firmware's first declared page. The reasoning was right and the implementation
was the last place the shell still had an opinion about a device.

**The measure of whether this worked** is not that it is tidier. It is that both shells
can now be read end to end without finding the name of a single device command.

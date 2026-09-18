---
id: 2026-09-09-21h50
date: 2026-09-09
time: "21:50"
title: The manifest is a command, so a shell can be complete before any module loads
builds-on: 2026-08-11-17h21
supersedes:
---

**Before:** the plan for device-hosted UI put the manifest in a *file* — `www/ui.json`,
fetched over HTTP alongside the bundles. It reads naturally: the modules are files, so
their index is a file too.

**What changed it:** two invariants the project already holds, which together decide it.
Device HTTP serves static files to a LAN browser and nothing else — there is no HTTP
command route and off-LAN there is no device HTTP at all. And anything the framework
needs from the application is *registered*, not fetched. A manifest file breaks both: it
puts a semantic question ("what can you show me?") on the file transport, and it makes
the answer something the build writes rather than something a manager declares.

So `ui modules` is an ordinary command in an ordinary table, and `UiManager` is the
eleventh framework manager. `LedManager` now registers commands, settings, a telemetry
point **and** a UI contribution without a single edit inside `strux/` — the same shape as
[registration](2026-08-11-17h21-registration-fuses-binding-a-setting-with-publishing-it.md),
and the worked example finally completing itself.

**What that buys, and it is the load-bearing property:** navigation is drawn *before any
module code has been fetched*. The firmware declares the pages and cards statically; the
bundle supplies what is behind them. Had only `activate()` known what a module
contributes, a shell would have to import every bundle just to draw a sidebar — which
throws away the lazy loading that was the point. It also means the manifest never passes
through the relay's file cache, so a nav entry cannot be stale while its page is gone.

The duplication that costs — ids in C++ and ids in the bundle — needed rules for the
three ways they can disagree, because every one of them is otherwise silent:

| | |
|---|---|
| Declared, not registered | Nav entry stays and says so. Hiding it makes a firmware/module mismatch undiagnosable |
| Registered, not declared | Ignored, warned once. Honouring it makes nav depend on running module code |
| Registered twice | Last wins, warned |

**What the second shell taught, that one could not.** The relay's shell is inherently
multi-device, and two boards can declare a page id — `led` — drawn by two different
bundles from two different firmwares. The device shell keeps its module registry in
module scope and cannot be wrong; a singleton there is correct. Copying that shape to the
relay would have let one device's registration answer for another: a page rendering the
wrong board's UI, which is worse than an error because it looks like it worked. So the
relay's registry is per device, and the bundle URL is per device too — `/devices/<id>/…`
— which makes them separate ES module instances by construction rather than by
discipline.

Designing the contract against **two real applications** instead of one plus a
hypothetical is what surfaced that, and the same ordering caught two more: an icon name
valid on one shell's lucide and absent on the other (so resolution must fall back, never
throw), and the fact that a refusal and a silence are different answers. `ui modules`
refused means "this firmware ships no modules" — the mixed-fleet case, and the common one.
Silence is a fault. Only the flags on the wire can tell them apart, so the relay records
it there (`RelayException.Refused`) instead of pattern-matching message text later, and
answers the shell with a classification rather than an exception.

**The one thing that genuinely breaks** is two React instances — every hook throws — and
it is invisible to a build. The facade's first version used `export * from "react"`, which
compiled, emitted, shipped, and exported a handful of mangled internals, because React is
CommonJS and a star re-export has no statically known names to forward. Caught by reading
the emitted chunk, not by a failing build. Hence `scripts/check-modules.mjs`: every bare
specifier a module imports must be declared by the import map, and every name must be
exported by the facade the map points at. A class of failure that is silent at build time
and fatal at runtime has to be made loud at build time on purpose; nothing does it for
free.

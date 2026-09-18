---
id: 2026-09-09-23h55
date: 2026-09-09
time: "23:55"
title: Dropping preflight isolates a module's elements, not its utility classes
builds-on: 2026-09-09-23h10
---

**Before:** a module bundling its own CSS was considered isolated from the shell because
it imports Tailwind's theme and utilities but **not preflight**. `theme.css` says so in
as many words, and the reasoning was sound as far as it went: a second reset would
restyle the host's own elements from underneath it, and not having one means it cannot.

**What it missed:** a shell and a module are both Tailwind v4, so both emit
`@layer utilities`, and layer names are resolved per DOCUMENT. Two stylesheets naming the
same layer are in *one* layer, not two. Inside a layer the last matching rule of equal
specificity wins, and `adoptStyles` appended the module's `<style>` to `<head>` — so on
every class name the two happened to share, the module won.

The shell that got hurt is the shadcn sidebar both shells use:

```
className="group peer hidden text-sidebar-foreground md:block"
```

`.hidden{display:none}` and `@media(min-width:48rem){.md\:block{display:block}}` are
both plain utilities in `utilities`. A media query carries **no specificity**, so the
only thing deciding between them is document order. A module that emitted `.hidden`
therefore hid the sidebar at every width — and the firmware module does emit it, for a
file input it hides on purpose. Measured on the live relay shell: sidebar `display:none`,
`width:0`, while `data-state` still said `expanded`. Moving that one `<style>` to the
front of `<head>` brought it back to 256px with nothing else changed.

**So the rule is not "a module ships no reset". It is:**

> **A module's stylesheet must lose every tie with the shell's.**

`adoptStyles` inserts at the front of `<head>` instead of appending. The shell wins the
classes it also defines — and since both sides generate the same core utilities from the
same tokens, those are the same declarations anyway — while a module still gets every
utility the shell never emitted. One line, and it ships inside the bundle, so it fixes
both shells without either of them knowing.

**What made it hard to see.** The failure had no error, no warning and no console
output: a stylesheet that wins a cascade it should have lost looks exactly like a
correct stylesheet. And it presented as *the shell's* bug — a sidebar that vanished when
a device was selected — on a device whose firmware was blameless. The bug was not even
in the module being viewed: `.hidden` reached the LED bundle because
`modules/_ui/module.css` scanned `../**/src/**`, one level up, and so every bundle
carried every sibling's utilities. That is fixed too (each module now declares its own
tree), but it is the smaller half: the firmware module would still have hidden the
sidebar the moment anyone opened it.

**What this does not do** is isolate a module properly. It orders two global
stylesheets; it does not scope one to a subtree. A module can still restyle the shell by
emitting a rule the shell never emitted, and the durable answer is a shadow root or a
build step that scopes every rule to the module's own container. Neither is worth it
today — with ties going the shell's way the realistic collisions are gone — but the
claim "a module cannot affect the shell" is still not true, and should not be written
down as if it were.

---
id: 2026-09-19-12h05
date: 2026-09-19
time: "12:05"
title: Cheap work goes first, because a half-loaded list reads as broken
---

**Correcting a conclusion**, not a premise. The note that made the strip draw SVGs
([[2026-09-18-18h25-the-strip-does-not-need-an-honest-picture-only-the-preview-does]])
was right about the delta and wrong about what followed from it. It claimed the preview
should move ahead of the other rows' thumbnails, on the grounds that the preview had only
ever been last because thumbnails used to be expensive too.

**That reasoning inverted the very thing it had just established.** Thumbnails stopped
being expensive - so the argument for keeping the preview behind them got *stronger*, not
weaker. When each thumbnail was a second of ThorVG, putting the strip first meant the
preview waited a minute behind eighty of them, and moving it up was the only way to see
anything. Now the whole visible strip is a handful of small file reads: running it first
delays the preview by almost nothing, and running it *second* leaves the list visibly
half-drawn while the one slow job holds the queue.

**The general form is about what the delay is spent on, not how long it is.** Ordering by
"what does the user want most" is the intuitive rule and it is wrong when the costs are
lopsided: a cheap job ahead of an expensive one is nearly free, while the reverse charges
every cheap job the expensive one's full price. So the rule is cheapest-first whenever the
cheap work is bounded and small - which is precisely the property the previous note
established and then failed to use.

There is also a perception asymmetry the ordering has to respect. A list with gaps in it
reads as broken; a preview panel that is briefly showing the right label at the right shape
does not, because step 1 already put that label's own SVG there. So the expensive job is
the one that can afford to be late, on both counts.

Order is now: the selected label's SVG, then every visible row's SVG, then the device
render.

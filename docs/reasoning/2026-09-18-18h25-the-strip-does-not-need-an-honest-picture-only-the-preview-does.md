---
id: 2026-09-18-18h25
date: 2026-09-18
time: "18:25"
title: The strip does not need an honest picture, only the preview does
---

**The rule was that every picture on the Print page comes from the device**, and it was a
good rule with a real reason behind it: the browser has none of the device's `/fonts`, so
an SVG drawn by the browser shows text in a substitute font, and a preview that lies about
the font is worse than no preview. `render svg` exists so that what you look at and what
comes out of the printer are the same rasteriser at the same dot geometry.

That rule was then applied to the thumbnail strip, and the strip paid for it. A thumbnail
was a full device render - 96 px, but still ThorVG, still one at a time, still about a
second. Eighty labels meant eighty jobs, and a whole apparatus grew to manage the queue:
an IntersectionObserver so only rows the eye had reached were rendered, an attempt record
so a failed design was not retried forever, a stand-in picture so the preview panel had
something in it, and a priority order putting the expensive job last.

**The delta is that honesty is a requirement of the preview, not of the page.** The two
pictures answer different questions. The preview answers *will this print correctly*, and
only the device can answer it. The strip answers *which of these is the one I want*, and a
substitute font does not stop anyone recognising their own label. Demanding one standard of
both is what made the cheap question expensive.

So the strip now shows the SVG itself, handed to the browser as a data URL. The cost is one
`fs read` - which the strip was *already making*, because a design's own size comes from its
root element and that read is how the "drawn for other stock" warning works. The device
render that followed it was asking the device to rasterise a file the browser was holding.

Two things fall out that were not the point but are worth having:

- **The order changed.** With thumbnails cheap, the preview no longer has to wait behind
  every visible row: it goes second, right after the selected label's own SVG lands as the
  stand-in. The slow job is now the only slow job, so it can go early instead of last.
- **The device stopped being the bottleneck for browsing.** Every command runs on the one
  web-server task, so a page that queued eighty renders was a page that made the whole
  device unresponsive to anything else while it loaded.

**The general form:** when one mechanism serves two questions and only one of them has a
correctness requirement, check whether the requirement was ever asked of the other. The
expensive property is usually being paid for somewhere it does not buy anything.

Builds on [[2026-09-09-22h00-a-home-screen-is-the-product-not-a-readout-of-the-board]].

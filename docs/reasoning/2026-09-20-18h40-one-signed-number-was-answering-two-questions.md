---
id: 2026-09-20-18h40
date: 2026-09-20
time: "18:40"
title: One signed number was answering two questions
---

**A constraint discovered by noticing an impossible reading.** A medium carried
`offsetXUm`/`offsetYUm`, and `PrintableDots` treated a NEGATIVE offset as paper the
printer could not reach. Both behaviours were correct on their own terms, and together
they said something that cannot be true: that moving the artwork down the label makes the
label shorter. Nudging an alignment changed the reported printable height, as though ink
position were a property of the paper.

**The number was doing two jobs because two facts had been measured at once.** When
square25 was calibrated, -1016 and -3133 um came off one printed grid, and nothing at the
time distinguished "the design sits here" from "the machine cannot reach this far". One
measurement, one field, and for that roll the two happen to be equal - which is exactly
why the conflation survived: on the only calibrated stock, the wrong model and the right
model give identical answers.

**Separating them needed a third owner, not a second field.** The first attempt was to
add unprintable margins to the medium beside its offsets. That is still wrong, because
the strip the mechanism cannot reach is not a fact about the paper at all - it is the
same for every roll anyone loads, and putting it on each medium means re-measuring a
printer constant per roll and having no way to notice when two rolls disagree about it.
So there are three things now, each owned by whatever it is a fact about: the PRINTER's
dead zone, the MEDIUM's own extra margin, and the ALIGNMENT.

    printable = size - printerDead - mediumMargin
    placement = align - printerDead - mediumMargin

Alignment appears in the second and not the first, and that asymmetry is the whole
content of the change.

**What this makes testable was previously unaskable.** `docs/next-up.md` has carried
"is the vertical offset actually a constant?" for days, and under the old model there was
no way to express either answer: every roll had its own offset, so agreement and
disagreement looked the same. Now the claim that it is a machine constant is written down
as the printer's `deadTopUm`, and a roll that genuinely needs a different figure has to
say so in its own `marginTopUm`. The question became answerable by being given somewhere
to be wrong.

**The compatibility rule is where this could have gone silently wrong, and nearly did.**
Media on devices still carry the old offsets, so those are honoured as the finished
placement verbatim - not reinterpreted - and the printable area comes from the printer
instead. Seeding the built-in dead zone with square25's own measured numbers is what
makes the split invisible for it: same placement, same 283 x 258 printable, to the dot.

But MIGRATING one is a conversion, not a copy. An old offset named the finished head
position; alignment is measured from the first reachable dot; they differ by exactly the
dead zone. Writing `align = oldOffset` would subtract the dead zone twice and shift every
calibrated roll - which on paper looks like a printer fault, not a firmware one. The
first draft of the migration did exactly that, and what caught it was writing the host
test before trusting the code: square25 must migrate to alignment ZERO, because it was
never being nudged, it was being cropped.

Builds on the arithmetic being pure and layer-free, which is why it sits in
`lib/common/DotGeometry.h` where a test can reach it rather than inside either manager.

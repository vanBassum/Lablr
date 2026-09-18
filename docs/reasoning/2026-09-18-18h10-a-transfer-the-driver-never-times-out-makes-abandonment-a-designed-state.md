---
id: 2026-09-18-18h10
date: 2026-09-18
time: "18:10"
title: A transfer the driver never times out makes abandonment a designed state
---

**The belief** was that `usb_transfer_t::timeout_ms` did what its name says. Every submit
in `UsbHostManager` set it, the waiter took a semaphore with the same number plus a little
slack, and the slack was there because the driver was expected to give up first. On that
reading a wait that expires is a formality: the transfer is already over, and cleaning up
after it is just tidying.

**It is not implemented.** The header in the component this firmware builds against says
so in the field's own doc comment - *"currently not supported yet"* - and it has said so
for every IDF version back to at least v5.5. The number is carried around and ignored. A
transfer that does not complete stays queued forever, and the only timeout in the system
is the waiter's own.

**The delta is what that makes a timeout mean.** It is not "the transfer is over and I am
late"; it is "the transfer is *still running* and I have stopped waiting for it". The
driver still owns the transfer object, still owns its DMA buffer, and will still call the
callback whenever the device eventually answers - or never. So everything the old code did
on that path was wrong in a way that reads as correct:

- `ControlTransfer` called `usb_host_transfer_free(xfer)` on the way out of every path,
  timeout included. That is handing the allocator memory the driver is about to write to.
  It is a heap corruption with a fuse on it, lit by a printer that NAKs `GET_DEVICE_ID`
  for one second.
- `Send` returned -1 and left the transfer queued, then reused `outXfer_->data_buffer`
  for the next chunk - and the completion, when it came, gave a *shared binary semaphore*
  that the next waiter was about to take. One late transfer made the following one return
  instantly with a stale status, which is a truncated label reported as a successful
  print.

Both are the same mistake: treating a timeout as the end of the transfer's life rather
than the end of the waiter's interest in it.

**What the shape has to be, then, is joint ownership with exactly one owner at a time.**
A completion record is allocated per attempt and carries an atomic `claimed` flag. The
waiter and the callback both exchange it; whichever finds it *already* true knows the
other got there first and has gone, and that side does the freeing. Both interleavings are
safe, including the narrow one where the completion lands between the wait expiring and
the claim. Nothing is freed twice and nothing is freed early.

Recovery is separate from safety, and only the bulk endpoint has any. `usb_host_endpoint_halt`
followed by `flush` cancels what is queued and runs its callbacks, and `clear` makes the
pipe carry traffic again - so a stuck print can be forced to an end and the next one still
works. The default pipe has none of those, so an abandoned control transfer is simply left
to the callback, and a status read that hangs costs one transfer object rather than the
heap.

**The general form**, which is the part worth carrying to the next driver: a timeout is
only the end of an operation when something else is enforcing it. When the timeout belongs
to the *caller* and not to the machinery, every resource the operation touches has two
possible owners from that moment on, and the code has to name which one - or it will pick
both.

Related: [[2026-09-09-15h10-a-lossy-link-cannot-be-told-from-a-lost-request-without-counters]],
which is the same lesson on a different wire: what a timeout means depends entirely on
what is still running underneath it.

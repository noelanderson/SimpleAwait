# Hardware validation

This directory holds on-target validation sketches used to confirm behavior that
host tests cannot cover (native clock monotonicity, IRQ → `ThreadSafeFlag` wake,
long-running allocator recovery, and representative peripheral wakeups).

These sketches reach into `arduinoawait::detail::` to exercise internal behavior
and are developer tools, not part of the public application API. Host CI does not
run them, but it compiles them on every first-class target to confirm the
relevant platform backend links.

Current sketches:

- `ClockMonotonic/` — M1: reads the native 64-bit microsecond clock and checks it
  is monotonic and actually advances (portable across RP2040/RP2350/ESP32; the
  platform layer selects the backend).
- `FramePoolCheck/` — M2: allocates and frees frames (including an over-aligned
  one) and confirms full recovery, forcing the allocator to compile and link for
  the target's word size and alignment.
- `TaskLifecycle/` — M3: creates and destroys an unscheduled lazy `Task<void>` and
  confirms its coroutine frame is taken from the pool and returned on destruction
  (full recovery), exercising the coroutine promise's pool-backed
  `operator new`/`delete` for the target's coroutine ABI.
- `SchedulerRun/` — M4: drives the fixed-slot scheduler through many bounded
  `poll()` passes, confirming FIFO run order, `current_task()` inside/outside a
  pass, `TaskHandle::done()` after completion, and slot reuse across passes with a
  bounded active-task count and no heap use.

Additional target-specific validation is added as later milestones land (for
example IRQ → `ThreadSafeFlag` wake on RP2040, RP2350, and ESP32-S3).

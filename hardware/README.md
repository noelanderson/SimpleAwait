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
- `TimerWait/` — M5: a task repeatedly `co_await delay_ms(500)` and reports the
  interval actually measured from the native 64-bit microsecond clock, while a
  second task yields continuously (confirming the sleeping timer task does not
  starve ready work). Forces the timer path to link the native clock backend.
- `ChildAwait/` — M6: a parent task `co_await`s a sequence of child tasks (each
  doing timed work) and confirms they complete in order, while a heartbeat task
  runs concurrently. Forces the child-await path (`TaskAwaiter` -> scheduler
  `start_child`) and the coroutine ABI to compile and link for the target.
- `EventWake/` — M7: three waiter tasks `co_await` one manual-reset Event; a
  controller `set()`s it to release them and reports whether the wake order was
  FIFO, then `clear()`s it for the next round. Forces the Event / WaitQueue path
  (park in `waiting_local`, `wake_all` to the ready FIFO) to compile and link.
- `FlagIRQ/` — M8: a pin-change interrupt timestamps the event and calls
  `ThreadSafeFlag::set()` from ISR context; a coroutine `co_await`s the flag and
  reports the IRQ -> coroutine wake latency from the native clock, while a
  heartbeat task runs concurrently. Forces the external-signal path (platform
  `CriticalSection`, ISR `set()`, the scheduler external-pending resolution) to
  compile and link, and confirms an ISR reliably wakes a waiting coroutine.

Additional target-specific validation is added as later milestones land (for
example IRQ → `ThreadSafeFlag` wake on RP2040, RP2350, and ESP32-S3).

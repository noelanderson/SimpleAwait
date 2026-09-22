# Hardware validation

This directory holds on-target validation sketches used to confirm behavior that
host tests cannot cover (native clock monotonicity, IRQ → `ThreadSafeFlag` wake,
long-running allocator recovery, and representative peripheral wakeups).

These sketches reach into `simpleawait::detail::` to exercise internal behavior
and are developer tools, not part of the public application API. Host CI does not
run them, but it compiles them on every first-class target to confirm the
relevant platform backend links.

Current sketches:

- `ClockMonotonic/` — reads the native 64-bit microsecond clock and checks it
  is monotonic and actually advances (portable across RP2040/RP2350/ESP32; the
  platform layer selects the backend).
- `FramePoolCheck/` — allocates and frees frames (including an over-aligned
  one) and confirms full recovery, forcing the allocator to compile and link for
  the target's word size and alignment.
- `TaskLifecycle/` — creates and destroys an unscheduled lazy `Task<void>` and
  confirms its coroutine frame is taken from the pool and returned on destruction
  (full recovery), exercising the coroutine promise's pool-backed
  `operator new`/`delete` for the target's coroutine ABI.
- `SchedulerRun/` — drives the fixed-slot scheduler through many bounded
  `poll()` passes, confirming FIFO run order, `current_task()` inside/outside a
  pass, `TaskHandle::done()` after completion, and slot reuse across passes with a
  bounded active-task count and no heap use.
- `TimerWait/` — a task repeatedly `co_await delay_ms(500)` and reports the
  interval actually measured from the native 64-bit microsecond clock, while a
  second task yields continuously (confirming the sleeping timer task does not
  starve ready work). Forces the timer path to link the native clock backend.
- `ChildAwait/` — a parent task `co_await`s a sequence of child tasks (each
  doing timed work) and confirms they complete in order, while a heartbeat task
  runs concurrently. Forces the child-await path (`TaskAwaiter` -> scheduler
  `start_child`) and the coroutine ABI to compile and link for the target.
- `EventWake/` — three waiter tasks `co_await` one manual-reset Event; a
  controller `set()`s it to release them and reports whether the wake order was
  FIFO, then `clear()`s it for the next round. Forces the Event / WaitQueue path
  (park in `waiting_local`, `wake_all` to the ready FIFO) to compile and link.
- `FlagIRQ/` — a SELF-DRIVING IRQ-storm stress test. A repeating hardware-timer
  interrupt (RP2040/RP2350 repeating timer; ESP32 `hw_timer`) calls
  `ThreadSafeFlag::set()` from ISR context at a fixed rate; a coroutine `co_await`s
  the flag while a heartbeat task runs concurrently. After a fixed number of signals
  it prints a deterministic `RESULT=PASS`/`FAIL` verdict over Serial covering
  liveness, coalescing (`wakes <= signals`), no coroutine body ever running in ISR
  context, and a watchdog drain of a final signal. The shared 64-bit ISR timestamp
  is read/written under the `CriticalSection` so it cannot tear on 32-bit cores.
  Forces the external-signal path (platform `CriticalSection`, ISR `set()`, the
  scheduler external-pending resolution) to compile and link, and produces on-device
  pass/fail evidence that an ISR reliably wakes a waiting coroutine without resuming
  coroutine code from the ISR. On-device runs provide the hardware evidence; host CI only compiles it.
- `QueueProducerConsumer/` — a producer sends a monotonically increasing
  counter into a small bounded `Queue<uint32_t,4>`; a consumer receives and checks
  strict FIFO order. The small capacity forces both full-send and empty-receive
  suspension, and the ever-growing counter wraps the ring many times; a reporter
  prints an IN-ORDER / OUT-OF-ORDER verdict. Forces the Queue path (aligned ring
  storage, sender/receiver wait sets, direct hand-off, back-pressure) to compile,
  link, and run on device.

Additional target-specific validation runs on device (for example IRQ → `ThreadSafeFlag` wake on RP2040, RP2350, and ESP32-S3).

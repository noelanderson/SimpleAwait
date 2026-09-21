# Changelog

All notable changes to ArduinoAwait are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — M9: Queue<T, Capacity>

- `arduinoawait::Queue<T, Capacity>` (`src/arduinoawait/queue.h`) — the frozen V1
  bounded, scheduler-local FIFO with blocking `send`/`receive` (V1_API_CONTRACT §11,
  ARCHITECTURE §17). `co_await q.send(v)` completes immediately when a receiver is
  waiting (direct hand-off) or capacity exists, else the sender suspends in FIFO
  order carrying its value on its own coroutine frame; `co_await q.receive()`
  completes immediately when data is buffered, else the receiver suspends in FIFO
  order. A receive that frees a slot admits the oldest waiting sender (its value
  moves to the tail, preserving send order); a send that finds a waiting receiver
  hands the value straight to it. Wakeups ENQUEUE tasks (they never inline-resume),
  matching the §9 poll() model. `trySend`/`tryReceive` are the non-suspending
  variants; `empty`/`full`/`size`/`capacity` are observers. Storage is a fixed ring
  of `alignas(T)` raw cells with placement construction/destruction, so `T` need not
  be default-constructible and no global heap is used. Value delivery uses
  `std::move_if_noexcept`, so move-only, non-default-constructible, AND copy-only
  (no move constructor) payloads are all supported. There are no ISR methods in V1.
  A foreign/nested await (the awaiting coroutine is not the running task) is rejected
  with `invalid_task` rather than stranded; destroying a queue with parked senders or
  receivers raises `object_destroyed_with_waiters`. A parked awaiter is the intrusive
  list node and unlinks itself if its coroutine frame is destroyed while still parked
  (e.g. when the scheduler tears down parked frames at shutdown while the queue
  outlives it), so no dangling waiter link is ever left behind. `Queue` is a friend of
  `Scheduler` (reusing `running_is()`/`park_running()`/`wake_slot()`), so the frozen
  §7 scheduler surface is unchanged.
- Host tests (MSVC + Clang 23.1.1, C++20 and C++23): `test_m9_queue` (basic
  try/await send+receive; full-trySend failure; empty-receive and full-send
  suspension with deferred, never-inline wakeups; FIFO value order; FIFO sender and
  receiver waiter order; ring-index wrapping; a 1000-item producer/consumer stress;
  move-only `unique_ptr`, non-default-constructible, and copy-only [deleted move
  ctor] payloads across the immediate-buffer, direct-receiver, and parked-sender
  delivery paths; exactly-once construction/destruction including a non-empty
  destroy; and a no-heap canary over the trivial-payload mechanics), `test_m9_errors`
  (destroy-with-parked-sender and destroy-with-parked-receiver ->
  `object_destroyed_with_waiters`; foreign receive-on-empty and send-on-full ->
  `invalid_task`, not stranded, no state mutation), and `test_m9_shutdown` (a
  namespace-scope queue that outlives the scheduler singleton: parked awaiters unlink
  as their frames are torn down at teardown, so the queue's destructor sees no waiter
  and the process exits cleanly).
- Golden example `examples/07_QueueProducerConsumer` (a producer and a slower
  consumer exchange values through a bounded queue with automatic back-pressure) and
  `hardware/QueueProducerConsumer` validation sketch (a producer/consumer pair over a
  small queue verifies strict FIFO order on device and reports IN-ORDER/OUT-OF-ORDER).
  Both compile for RP2040 and RP2350 (Arm and RISC-V); CI adds them to the compile
  matrix.

### Added — M8: ThreadSafeFlag and external/IRQ signaling

- `arduinoawait::ThreadSafeFlag` (`src/arduinoawait/threadsafeflag.h`) — the frozen
  V1 single-waiter, auto-reset, coalescing external-context signal bridge
  (V1_API_CONTRACT §10, ARCHITECTURE §16). `set()` is the only method callable from
  a supported external/IRQ/callback/other-core context: under a short platform
  critical section it marks the flag signaled and the scheduler externally-pending,
  then returns — it never manipulates scheduler lists and never resumes coroutine
  code. `poll()` resolves pending signals in scheduler context (ARCHITECTURE §9
  step 4) and wakes the single waiter for a later pass. `co_await flag.wait()` on a
  set flag consumes the signal without suspending (auto-reset); a wait before the
  signal suspends until a later poll resolves it; repeated `set()` while signaled
  coalesces into one pending signal. A second simultaneous waiter is a
  deterministic error (`multiple_flag_waiters`); a foreign/nested await (the
  awaiting coroutine is not the running task) is rejected with `invalid_task`
  rather than stranded; destroying a flag with a parked waiter raises
  `object_destroyed_with_waiters` (and unarms it first for safety).
- `detail::CriticalSection` (`src/arduinoawait/detail/platform_sync.h`) — the
  platform short critical section that owns cross-context ordering (do not assume
  `std::atomic` is ISR-safe on every target). Backends: RP2040/RP2350
  `save_and_disable_interrupts()`/`restore_interrupts()` — STATE-PRESERVING, so it
  is safe inside an ISR (same-core IRQ exclusion; a spinlock override is available
  for cross-core core1->core0 signaling), ESP32 a FreeRTOS `portMUX` spinlock (IRQ +
  multicore), and a host no-op (single-threaded deterministic tests). A generic
  Arduino target (no first-class core, no override) is UNSUPPORTED: there is no
  portable way to restore the prior interrupt state, and the scheduler's external-
  signal poll step (`detail::poll_external_signals`, run by `poll()` every pass)
  needs a usable critical section, so the umbrella `<ArduinoAwait.h>` fails to
  compile with a `#error` directing the user to the
  `ARDUINOAWAIT_CRITICAL_SECTION_OVERRIDE` or a first-class target. The
  scheduler gains a `detail::poll_external_signals()` step-4 hook and private
  `running_is()`/`park_running()`/`wake_slot()` helpers; `ThreadSafeFlag` is a friend
  of `Scheduler`, so the frozen §7 surface is unchanged.
- Host tests (MSVC + Clang 23.1.1, C++20 and C++23): `test_m8_flag` (wait-before-set
  with set() never resuming inline; set-before-wait completing without suspension;
  repeated set() coalescing; auto-reset on consume; a signal injected in the
  `await_ready`/`await_suspend` window is not lost; the armed list wakes exactly the
  signaled flags with head/middle/tail removal; no-heap canary) and `test_m8_errors`
  (second waiter -> `multiple_flag_waiters`; foreign wait outside poll() ->
  `invalid_task`; a foreign/nested await takes precedence over the single-waiter
  check -> `invalid_task`, never masked as `multiple_flag_waiters`; destroy-with-
  waiter -> `object_destroyed_with_waiters`), plus a negative-compile regression
  (`neg_flag_generic_unsupported`) asserting the umbrella header is rejected on a
  generic Arduino target without an override. The host critical section is a no-op, so these exercise the STATE
  MACHINE deterministically; real on-device IRQ/multicore safety is validated by the
  `hardware/FlagIRQ` stress sketch as a pre-V1-release gate (AGENTS.md §16) and is
  NOT yet run on hardware.
- Golden example `examples/06_ThreadSafeFlagIRQ` (a pin interrupt calls
  `ThreadSafeFlag::set()`; a coroutine `co_await`s it). `hardware/FlagIRQ` is a
  self-driving IRQ-storm stress sketch: a repeating hardware-timer ISR calls `set()`
  at a fixed rate and the sketch prints a deterministic `RESULT=PASS`/`FAIL` verdict
  over Serial (liveness, coalescing `wakes<=signals`, no coroutine body ever running
  in ISR context, and a watchdog drain), reading its shared 64-bit ISR timestamp
  under the critical section so it cannot tear on 32-bit cores. Both compile for
  RP2040 and RP2350 (Arm and RISC-V); the on-device RUN remains a pre-release gate.

### Added — M7: WaitQueue and Event

- `arduinoawait::Event` (`src/arduinoawait/event.h`) — the frozen V1 scheduler-
  local, manual-reset, multi-waiter synchronization primitive (V1_API_CONTRACT
  §9). `co_await ev.wait()` on a clear Event suspends the task; `set()` latches the
  Event and wakes all current waiters in FIFO order (they run on a LATER poll
  pass, appended behind tasks already ready — no inline resume); the Event stays
  set until `clear()`, so `wait()` on a set Event completes without suspension and
  `clear()` affects only future waits. There is no ISR `set()` (Event is
  scheduler-context only; external contexts use ThreadSafeFlag, a later
  milestone). Destroying an Event that still has parked waiters invokes the
  deterministic hook with `Error::object_destroyed_with_waiters`.
- The scheduler (`src/arduinoawait/scheduler.h`) gains an intrusive FIFO
  `WaitQueue` (ARCHITECTURE §14, a private nested type holding task slots via the
  shared `next` link — no heap, no separate nodes) and a `waiting_local` task
  state, plus `wait_on()`/`wake_all()`. `Event` is a friend of `Scheduler` and
  holds a `Scheduler::WaitQueue`, so the frozen §7 public Scheduler surface is
  unchanged. `wait_on()` applies the M6 lesson: a wait whose awaiting coroutine is
  not the running task (foreign or nested) is rejected with `Error::invalid_task`
  and does not suspend the caller, rather than stranding it.
- Host tests (MSVC + Clang 23.1.1, C++20 and C++23): `test_m7_event` (clear wait
  suspends; `set()` wakes all waiters in FIFO order; woken tasks run on a later
  poll, not inline; `wait()` on a set Event does not suspend; the Event stays set;
  `clear()` affects only future waits; and a global new/delete no-heap canary) and
  `test_m7_errors` (a foreign coroutine waiting outside `poll()` is rejected with
  `invalid_task` and resumes; destroying an Event with a parked waiter raises
  `object_destroyed_with_waiters`).
- Golden example `examples/05_Event` (a one-shot "start line" releasing several
  waiting runners with a single `set()`). `hardware/EventWake` validation sketch
  confirms multi-waiter FIFO wake on-target. All compile for RP2040 and RP2350
  (Arm and RISC-V).

### Added — M6: Parent/child await

- `Task<void>::operator co_await() && noexcept` (`src/arduinoawait/task.h`) — the
  frozen V1 sequential child await (V1_API_CONTRACT §4). `co_await someTask()` runs
  the child Task to completion as a child of the awaiting task. Awaiting consumes
  the Task (rvalue-qualified): it transfers the child's coroutine frame into the
  scheduler and empties the Task, so a second await of the now-empty Task fails
  deterministically via the error hook (`Error::task_awaited_twice`) without
  suspending or hanging.
- The scheduler (`src/arduinoawait/scheduler.h`) gains a `waiting_child` state and
  a per-slot `parent` link (no heap; the child reuses the fixed slot pool via the
  same `acquire_slot`/frame ownership as `create_task`). `detail::start_child()`
  transfers the child frame into a slot linked to the running parent and moves the
  parent to `waiting_child`. On child completion `poll()`/`run_pass` releases the
  child frame exactly once, clears the linkage, and enqueues the parent at the
  ready FIFO **tail** so it resumes on a LATER pass (ARCHITECTURE §19 — no inline
  resume, no symmetric transfer). Nested children compose naturally (each level
  links to its parent). If no slot is available for the child, the scheduler
  reports `Error::task_limit`, releases the child frame, and requeues the parent so
  it is not lost.
- Host tests (MSVC + Clang 23.1.1, C++20 and C++23): `test_m6_childawait` (parent
  does not resume before the child completes; parent resumes on a later pass, not
  inline; nested parent→child→grandchild ordering; a child that itself suspends on
  a timer while the parent waits; and full frame-pool recovery after 50 rounds of
  nested-child stress — the M6 gate — plus a global new/delete no-heap canary),
  `test_m6_errors` (second await → `task_awaited_twice`, child ran exactly once,
  coroutine continues), and `test_m6_exhaust` (single-slot child-await exhaustion →
  `task_limit`, child frame released, parent requeued).
- Golden example `examples/04_ParentChild` (a parent sequences child steps via
  `co_await`, with a concurrent heartbeat task). `hardware/ChildAwait` validation
  sketch runs a parent/child chain on-target. All compile for RP2040 and RP2350
  (Arm and RISC-V).

### Added — M5: yield(), delay(), and timer waits

- `arduinoawait::yield()` (`YieldAwaitable`) and `arduinoawait::delay(uint32_t ms)`
  / `delay_ms(uint32_t ms)` / `delay_us(uint64_t us)` (`DelayAwaitable`) in
  `src/arduinoawait/delay.h` — the frozen V1 timing awaitables (V1_API_CONTRACT
  §8). `co_await yield()` and `co_await delay(0)`/`delay_ms(0)`/`delay_us(0)` are
  fair yield points: they always suspend and requeue for a LATER `poll()` pass
  (zero duration is never an immediate `await_ready()` success), so a continuously
  yielding task cannot starve another ready task. A positive delay suspends until
  the 64-bit monotonic microsecond clock reaches `now + duration`.
- The scheduler (`src/arduinoawait/scheduler.h`) gains a fixed per-slot timer
  wait: a `waiting_timer` state plus an absolute `deadline_us`, with no separate
  timer nodes or heap. `poll()` now performs ARCHITECTURE §9 step 5 — after
  sampling the clock it moves every due timer to the ready FIFO (deterministic
  slot order for equal deadlines) before snapshotting the pass budget, so a woken
  timer runs in that pass and there is no early or repeat wake. A cached nearest
  deadline makes idle polls before the next deadline O(1); the O(n) rescan runs
  only when a timer is actually due.
- Deadline overflow reuses the M1 deadline math: `now + duration` exceeding
  `uint64_t` invokes the deterministic hook with `Error::deadline_overflow` (never
  a silent wrap/saturation). Under a non-halting hook the task is requeued (fair
  fallback) rather than lost.
- Host tests: `test_m5_timers` (yield/`delay(0)` one-step-per-pass fairness,
  positive-delay wake only when due, multiple deadlines in deadline order across
  passes, equal deadlines in deterministic slot order within a pass, no early/no
  repeat wake, looped re-arming, and a global new/delete no-heap canary) and
  `test_m5_overflow` (deadline_overflow raised and the task requeued, plus a
  non-overflowing control), each on a test-controlled fake clock. Pass on MSVC and
  Clang 23.1.1 at C++20 and C++23.
- Golden example `examples/03_YieldFairness` (two 1:1-interleaving yielders plus a
  `delay_ms`-based reporter); `examples/01_Blink` is completed with its real
  `delay_ms()`-driven blink body. `hardware/TimerWait` validation sketch measures
  real delay intervals against the native clock. All compile for RP2040 and RP2350
  (Arm and RISC-V).
- Docs: V1_API_CONTRACT §8 gains a usage note that Arduino's global `::yield()` /
  `::delay()` collide under `using namespace arduinoawait;`, so qualify
  `arduinoawait::yield()` and prefer `delay_ms()`/`delay_us()` (signatures
  unchanged).

### Added — M4: Cooperative scheduler, TaskHandle, ready FIFO

- `arduinoawait::Scheduler` (`src/arduinoawait/scheduler.h`): a fixed-memory
  cooperative scheduler. Task state lives in a fixed array of
  `ARDUINOAWAIT_MAX_TASKS` slots (no heap, no `std::` containers); the ready queue
  is an intrusive FIFO threaded through the slots. `poll()` runs one bounded pass
  with frozen V1 semantics (ARCHITECTURE §9): it samples the 64-bit clock, then
  snapshots the ready count as the pass budget and resumes each of those tasks at
  most once, so a task made ready during the pass (e.g. by `spawn()` from within a
  running task) runs on a later `poll()` and no task ever runs twice in one pass.
  A completed task's frame is destroyed immediately and its slot becomes a
  tombstone.
- `TaskId { TaskSlot slot; TaskGeneration generation; }` and `TaskHandle`
  (frozen V1 surface, V1_API_CONTRACT §5–§6): `TaskHandle` is a copyable,
  non-owning, generation-checked identity. It stays `valid()` after its task
  completes (`done()` returns true) until the slot is reused; slot reuse
  increments the generation, so an older handle to a reused slot becomes
  `valid() == false`. A default/stale handle reports `valid() == false` and
  `done() == false`.
- Free-function scheduling API: `create_task()` (returns an observable
  `[[nodiscard]] TaskHandle`), `spawn()` (detached), `current_task()` (the task
  being resumed, or invalid outside a pass), and `poll()` — all driving the
  process-wide `scheduler()` singleton. Scheduling an empty/invalid Task invokes
  the deterministic error hook (`Error::invalid_task`); slot exhaustion invokes it
  with `Error::task_limit`; re-entering `poll()` from within a pass invokes it with
  `Error::scheduler_reentry`. On any rejection the passed Task is left untouched
  and the caller retains frame ownership (released normally when that Task is
  destroyed). `create_task()`/`spawn()` match the frozen §6 signatures exactly
  (not `noexcept`); the `Scheduler` public surface is exactly the frozen §7 members
  (`poll`, `hasReadyTasks`, `hasPendingTasks`, `activeTaskCount`, `currentTask`);
  scheduling is reached through the free functions.
- Lifetime/robustness hardening (from independent review): destroying a suspended
  frame runs its by-value parameter destructors, so scheduler teardown marks a
  shutdown state (making `poll()` a no-op and refusing `schedule()`) and detaches
  each slot before destroying its frame — a reentrant destructor cannot resume a
  frame being destroyed or double-free it. Generation counters retire a slot at
  `UINT32_MAX` instead of wrapping, so a rolled-over generation can never resurrect
  an earlier handle. A compile-time check rejects `ARDUINOAWAIT_MAX_TASKS` beyond
  the representable `TaskSlot` range.
- Host tests: `test_m4_scheduler` (explicit scheduling and lazy bodies, detached
  `spawn`, FIFO run order, the bounded pass budget — a task created mid-pass runs
  on the next pass and none runs twice — `current_task()` inside vs outside a
  task, and a global `operator new`/`delete` canary proving the scheduler and
  frames never touch the heap); `test_m4_errors` (empty/moved-from rejection,
  single-slot `task_limit` exhaustion, slot reuse bumping the generation and
  invalidating the stale handle, the `scheduler_reentry` guard, and generation
  retirement at the `uint32` boundary via a test-only seam); `test_m4_poll_clock`
  (exactly one clock sample per accepted pass, none for a rejected nested poll);
  and `test_m4_shutdown` (a reentrant-teardown regression: a pending task whose
  by-value parameter destructor reenters the scheduler at singleton destruction —
  no double-free, no body execution, scheduling refused). A negative-compile test
  asserts the capacity check. All pass on MSVC and Clang 23.1.1 at C++20 and C++23.
- Golden examples: `examples/02_TwoTasks` (two tasks scheduled concurrently and
  run by a single `poll()` pass, using only the M4 API) and `examples/01_Blink`
  (the M4 structural skeleton; its `delay()`-driven blink body is completed at
  M5). `hardware/SchedulerRun` validation sketch runs the scheduler across many
  passes on-target. All compile for RP2040, RP2350 (Arm and RISC-V).

### Added — M3: Lazy Task<void> and ownership

- `arduinoawait::Task<void>` (`src/arduinoawait/task.h`): a lazy, move-only
  coroutine handle. Calling a Task-returning coroutine allocates the frame from
  the fixed pool and suspends at `initial_suspend` — the body does not run until
  the Task is scheduled/awaited (later milestones). Ownership is a single token:
  moving transfers it and empties the source; a default/moved-from Task owns
  nothing and its destructor is a no-op; destroying an unscheduled Task destroys
  its frame exactly once and returns the bytes to the pool. `explicit operator
  bool()` reports ownership. `Task<T>` for any `T != void` is a clear compile
  error (`static_assert`).
- The `Task<void>` promise's `operator new`/`operator delete` (plain, `nothrow`,
  and sized) route through the process-wide frame pool
  (`detail::frame_pool()`, `src/arduinoawait/detail/global_frame_pool.h`), so no
  coroutine frame ever touches the global heap. On exhaustion the deterministic
  hook is invoked with `Error::frame_pool_exhausted`; with a non-halting override
  the coroutine returns the empty Task via `get_return_object_on_allocation_failure`
  (no exception, works under `-fno-exceptions`). An unhandled exception routes to
  `Error::unhandled_exception`.
- Host tests: laziness (body not run), frame-from-pool with a global new/delete
  canary (no heap), move-construct/assign ownership transfer, moved-from/default
  harmlessness, full recovery over many create/destroy cycles, and a small-pool
  exhaustion test (frame_pool_exhausted + empty Task + recovery). Pass on MSVC and
  Clang 23.1.1 at C++20 and C++23.
- `hardware/TaskLifecycle` validation sketch (developer tool; not host CI) that
  forces the coroutine promise + pool integration to compile/link on every target.

### Added — M2: Fixed coroutine frame allocator

- `detail::FramePool<Bytes>` (`src/arduinoawait/detail/frame_pool.h`): a fixed
  byte arena that hands out variable-size, aligned blocks for coroutine frames
  with no global heap fallback. It is a coalescing first-fit free list —
  arbitrary free order, adjacent free blocks merge, and full capacity is
  recovered once every block is freed (`bytesUsed() == 0`). Alignment is honored
  up to and beyond `alignof(std::max_align_t)` (over-aligned requests are padded
  within the block). Exhaustion is deterministic: `allocate()` returns `nullptr`
  and records a failure rather than allocating from the heap (the caller applies
  the `Error::frame_pool_exhausted` policy in a later milestone). Requests are
  robust: oversized/overflowing sizes are rejected with checked subtractive
  arithmetic, over-aligned requests compute padding as integers (never forming an
  out-of-arena pointer before the fit is validated), a non-power-of-two alignment
  is rejected, and a capacity that would not fit the 32-bit block header fields is
  a compile error. Block headers live in the arena and are created with placement
  `new` / re-accessed through `std::launder` for well-defined object lifetime.
  Stats: `bytesUsed`, `bytesFree`, `peakBytesUsed`, `allocationFailures`,
  `capacity`, `blockOverhead`.
- Host test covering exact-capacity allocation, many small frames, mixed sizes
  with non-overlap, arbitrary destruction order, fragmentation/coalescing with
  full recovery, over-aligned requests, oversized/overflow requests, invalid
  alignment, deterministic exhaustion, peak tracking, and a whole-test global
  `operator new`/`delete` canary proving the pool never touches the heap. A
  persistent negative-compile test asserts the capacity static_assert. (Clang
  ASan/UBSan run in CI; MSVC ASan is environmentally blocked on the dev host.)
- `hardware/FramePoolCheck` validation sketch (developer tool; not host CI) that
  exercises the allocator on-target and, via CI, compiles/links it for every
  target's word size and alignment.

### Added — M1: Platform clock abstraction

- Deterministic `arduinoawait::Error` enum (frozen V1 surface) in
  `src/arduinoawait/error.h`.
- 64-bit monotonic microsecond platform clock `detail::platform_now_us()`
  (`src/arduinoawait/detail/platform_clock.h`) with compile-time backend
  selection verified against the real cores: `time_us_64()` on RP2040/RP2350
  (Arm and RISC-V, selected by `ARDUINO_ARCH_RP2040`), `esp_timer_get_time()` on
  ESP32 (`ARDUINO_ARCH_ESP32`), a software-extended 32-bit `micros()` secondary
  backend for generic Arduino, and an injectable `ARDUINOAWAIT_CLOCK_NOW_US`
  override for tests. There is no silent host default: a host build must inject a
  clock (or opt into a `steady_clock` adapter via `ARDUINOAWAIT_HOST_REALTIME_CLOCK`),
  so a test that forgets injection gets a link error rather than nondeterministic
  real time. All scheduler timing flows through this one function; no other
  source calls a platform clock primitive directly.
- Deadline arithmetic (`src/arduinoawait/detail/time_math.h`): `ms_to_us`
  widening, an `add_overflows` predicate, and `compute_deadline(now, dur, out&)`,
  which returns a `bool` success indicator (never conflating a valid maximum
  deadline with failure), writes the deadline only on success, and routes a
  would-be `uint64` overflow to the error hook as `Error::deadline_overflow`
  (V1 forbids silent wrap/saturation). It contains no unreachable code, so it
  compiles under strict warnings with either the default `[[noreturn]]` hook or a
  returning override.
- Host tests for exact/monotonic clock values, the 32-bit extender wrap, ms→us
  widening, the overflow predicate, the deadline bool contract, the default-hook
  compile path, and a death test for the default overflow-halt policy.
- `hardware/ClockMonotonic` validation sketch (developer tool; not host CI) that
  checks the native clock is monotonic AND advances across `loop()` batches, and
  which CI compiles on every target so the native backend is linked, not just the
  header.

### Added — M0: Repository and build skeleton

- Arduino library layout under `src/` with the public include `ArduinoAwait.h`.
- Configuration header `src/arduinoawait/config.h` exposing the V1 configuration
  surface (`ARDUINOAWAIT_MAX_TASKS`, `ARDUINOAWAIT_FRAME_POOL_BYTES`,
  `ARDUINOAWAIT_ON_ERROR`, `ARDUINOAWAIT_ENABLE_DIAGNOSTICS`,
  `ARDUINOAWAIT_ENABLE_ISR`) with overridable defaults.
- Compile-time coroutine-support checks in
  `src/arduinoawait/detail/coroutine_support.h` (fails clearly without C++20
  standard coroutines; does not rely on `__cplusplus`).
- Library version header `src/arduinoawait/version.h`.
- Arduino metadata (`library.properties`) and PlatformIO metadata
  (`library.json`).
- Host CMake test target (`CMakeLists.txt`, `test/`) with deterministic host
  tests compiled at the C++20 language floor and, where available, C++23.
- `examples/Empty/` build-skeleton sketch for target compile checks.
- CI skeleton (`.github/workflows/ci.yml`) covering host GCC/Clang C++20/C++23
  builds across `-O0`/`-O2`/`-Os`, ASan/UBSan, RP2040/RP2350 (Arm and RISC-V)
  and ESP32/ESP32-S3 example compiles, and a metadata gate
  (`.github/scripts/lint_metadata_gate.py`) that runs arduino-lint and fails on
  any error except the documented `LP012` "Arduino" name-prefix deviation.
- Project docs, `LICENSE` (MIT), and this changelog.

### Hardened after independent milestone review

- The default embedded halt (`arduinoawait::detail::halt`) now performs a
  per-iteration `volatile` access so the deterministic halt loop is preserved
  under the C++20 forward-progress rules ([intro.progress]) at every
  optimization level, without relying on the later P2809 fix.
- Added host tests, driven by portable CMake wrappers so they behave correctly
  across single- and multi-config generators and POSIX/Windows: a
  two-translation-unit ODR check (inline version constant and error-handler
  template each have one definition program-wide); a death test proving the
  default error hook terminates abnormally after reaching the hook (replacing a
  CTest `WILL_FAIL`, which does not reliably invert SIGABRT on POSIX); a
  persistent negative-compile probe that must fail *with* the coroutine-support
  diagnostic (rejecting unrelated/infrastructure build failures) and forwards
  the build configuration; and a self-test for the metadata gate.
- Reworked the metadata CI job into a documented gate
  (`.github/scripts/lint_metadata_gate.py`) that runs arduino-lint at
  specification compliance and fails on any error except the intentional
  `LP012` "Arduino" name-prefix deviation inherent to the fixed project name
  (the maintainer name-prefix remains a non-blocking warning). The gate fails
  closed on malformed, empty, non-library, schema-invalid (unknown rule
  result/level values), or internally inconsistent (summary errorCount vs.
  inspected failures) reports; the CI step installs arduino-lint correctly and
  distinguishes a linter crash from ordinary rule
  errors. Corrected the README example-build commands (`--library .`) and
  coverage wording.

No coroutine scheduling is implemented at M0.

[Unreleased]: https://github.com/ArduinoAwait/ArduinoAwait

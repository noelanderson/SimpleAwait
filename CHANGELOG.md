# Changelog

All notable changes to SimpleAwait are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0]

Initial release. SimpleAwait is a deterministic, fixed-memory cooperative
coroutine library for Arduino built on native C++20 coroutines — no RTOS, and no
heap allocation for coroutine frames. The public API is frozen; see
[docs/simpleawait/V1_API_CONTRACT.md](docs/simpleawait/V1_API_CONTRACT.md).

### Added

- **`Task<void>`** — a lazy, move-only coroutine handle. Calling a Task-returning
  coroutine allocates its frame from a fixed pool (never the global heap) and
  suspends immediately; the body runs only once the Task is scheduled or awaited.
  Frame ownership is a single token with deterministic move and destroy behavior.
- **Cooperative scheduler** — fixed-capacity and statically allocated.
  `create_task` returns an observable `TaskHandle`, `spawn` is fire-and-forget,
  `current_task` identifies the running Task, and `poll()` runs one bounded,
  FIFO-fair pass that resumes each task ready at the start of the pass at most
  once. `TaskHandle` carries a generation counter so stale handles are detected.
- **Timers** — `yield()` and `delay()` / `delay_ms()` / `delay_us()` awaitables on
  a 64-bit monotonic microsecond timebase. `delay(0)` and `yield()` are real
  fairness points, equal deadlines wake in FIFO order, and deadline overflow
  follows a deterministic, documented policy.
- **Parent/child await** — `co_await`-ing a child `Task` suspends the parent
  until the child completes; the parent resumes on a later `poll()` pass.
- **`Event`** — scheduler-local, manual-reset, multi-waiter synchronization with
  FIFO wake order.
- **`ThreadSafeFlag`** — a single-waiter, auto-reset, coalescing signal usable
  from external/ISR contexts on the first-class targets; its `set()` path never
  runs user code.
- **`Queue<T, Capacity>`** — a bounded, scheduler-local FIFO with blocking
  `send` / `receive`, non-blocking `trySend` / `tryReceive`, direct value
  hand-off, and support for move-only, non-default-constructible, and copy-only
  payloads.
- **`waitUntil(predicate)`** — header-defined coroutine composition over
  `yield()`.
- **Diagnostics** (opt-in via `SIMPLEAWAIT_ENABLE_DIAGNOSTICS`) — an
  allocation-free `Stats` / `stats()` snapshot of scheduler and frame-pool
  counters, with zero cost when disabled.
- **Platform clock backends** — `time_us_64()` on RP2040/RP2350,
  `esp_timer_get_time()` on ESP32, and an injectable microsecond clock for host
  tests, all behind a single platform abstraction.
- **Deterministic failure** — a configurable `SIMPLEAWAIT_ON_ERROR` hook whose
  default halts (aborts on host, spins on device) with no `Serial` dependency.
  There is no heap fallback: frame-pool and task-slot exhaustion fail
  deterministically.
- **Configuration** — `SIMPLEAWAIT_MAX_TASKS`, `SIMPLEAWAIT_FRAME_POOL_BYTES`,
  `SIMPLEAWAIT_ENABLE_DIAGNOSTICS`, `SIMPLEAWAIT_ENABLE_ISR`,
  `SIMPLEAWAIT_ON_ERROR`, and `SIMPLEAWAIT_CLOCK_NOW_US` compile-time overrides.
- **Examples** — `01_Blink`, `02_TwoTasks`, `03_YieldFairness`, `04_ParentChild`,
  `05_Event`, `06_ThreadSafeFlagIRQ`, `07_QueueProducerConsumer`, and
  `08_WaitUntil`.
- **Tests** — a deterministic host test suite covering the public API and its
  failure modes, run on GCC and Clang across `-O0`/`-O2`/`-Os` at C++20 and
  C++23, and under AddressSanitizer/UndefinedBehaviorSanitizer. On-target
  validation sketches live in [`hardware/`](hardware/).

### Targets

First-class targets are RP2040, RP2350 (Arm and RISC-V where the toolchain
supports it), and the ESP32 family (including ESP32-S3). C++20 is the minimum
language level; C++23 and later are supported but not required.

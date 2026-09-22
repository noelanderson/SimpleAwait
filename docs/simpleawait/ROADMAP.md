# SimpleAwait roadmap

SimpleAwait **1.0.0 is complete and its public API is frozen** — see
[`V1_API_CONTRACT.md`](V1_API_CONTRACT.md). This document tracks work that is
deliberately **out of scope for 1.0** and may be considered for a future release.

Nothing here is a commitment. None of it should be pulled into a 1.0.x patch
merely because it is convenient to implement while touching adjacent code. Each
item is a deliberate, separately reviewed decision that must preserve the
invariants in [`../../AGENTS.md`](../../AGENTS.md) and
[`ARCHITECTURE.md`](ARCHITECTURE.md) — fixed memory, single-owner coroutine
frames, cooperative FIFO scheduling, and deterministic failure.

## Candidate features (next minor release)

- `Task<T>` result values — the `Task<T>` template is already declared, so typed
  tasks need not rename the core type;
- `AsyncLock` / mutual exclusion between tasks;
- cooperative cancellation;
- timeout composition (e.g. `withTimeout`);
- `when_any` / `when_all` combinators;
- `TaskGroup` structured concurrency.

## Longer-term candidates

- a cross-core / ISR-producer `Queue` variant (ordinary `Queue` stays
  scheduler-local);
- low-power nearest-deadline hardware wake (the core scheduler stays free of
  per-delay hardware alarms);
- GPIO edge, DMA, and PIO completion awaitables;
- UART / SPI / I2C peripheral adapters;
- `std::chrono` duration overloads for `delay`;
- optional dual-scheduler multicore examples.

## Explicitly not planned

The following would change the library's character and are not on the roadmap
without a compelling, measured case:

- preemptive scheduling, task priorities, executors, or work stealing;
- futures/promises layered over Tasks;
- an RTOS abstraction or FreeRTOS task wrappers;
- symmetric coroutine transfer introduced solely to reduce scheduler operations.

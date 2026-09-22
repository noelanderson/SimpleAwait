# SimpleAwait Implementation Plan

**Objective:** Deliver a small, testable V1 in gated milestones.  
**Language floor:** C++20  
**First-class targets:** RP2040, RP2350, ESP32

Do not implement later milestones until the current milestone's acceptance gate is green.

---

## M0 — Repository and build skeleton

### Deliverables

- Arduino library layout;
- PlatformIO metadata;
- host CMake test target;
- top-level public include `SimpleAwait.h`;
- configuration header;
- compile-time coroutine support checks;
- CI skeleton;
- reference docs included in repository.

### Required checks

- host C++20 compile;
- host C++23 compile as compatibility check where available;
- Arduino-Pico RP2040 empty example compile;
- Arduino-Pico RP2350 empty example compile;
- Arduino-ESP32 empty example compile.

### Gate

No functional coroutine scheduling yet. Build matrix must be green.

---

## M1 — Platform clock abstraction

### Deliverables

Internal API:

```cpp
using tick_t = uint64_t;
uint64_t platform_now_us() noexcept;
```

Backends:

- RP2040/RP2350 -> `time_us_64()`;
- ESP32 -> `esp_timer_get_time()`;
- host -> injected fake clock;
- optional generic Arduino compatibility backend separated from first-class targets.

### Tests

- host fake clock exact values;
- monotonic deadline comparisons;
- millisecond-to-microsecond widening;
- microsecond API values;
- deadline overflow rejected through `Error::deadline_overflow`;
- generic 32-bit extender rollover test if compatibility backend exists.

### Hardware validation

Read native clock repeatedly on RP2040/RP2350/ESP32-S3 and verify monotonic behavior.

### Gate

No scheduler source file may directly call `millis()`, `micros()`, `time_us_64()`, or `esp_timer_get_time()` except through platform clock implementation.

---

## M2 — Fixed coroutine frame allocator

### Deliverables

- fixed byte arena;
- variable-size coroutine frame allocation;
- required alignment;
- free/coalesce or equivalent reuse;
- deterministic exhaustion error;
- no heap fallback;
- allocator stats hooks.

### Tests

- exact-capacity allocation;
- many small frames;
- mixed sizes;
- arbitrary destruction order;
- fragmentation/coalescing;
- full recovery;
- over-aligned frame case where supported;
- global allocation instrumentation;
- ASan/UBSan.

### Gate

After every allocator stress run:

```text
frameBytesUsed == 0
```

when all test frames are destroyed.

---

## M3 — Lazy Task<void> and ownership

### Deliverables

- `Task<void>` coroutine promise;
- `initial_suspend` lazy semantics;
- move-only Task;
- unscheduled Task destruction;
- invalid/moved-from behavior;
- error routing for unhandled exceptions/no-exception builds.

### Tests

- calling coroutine does not execute body;
- move constructor/assignment transfer ownership;
- moved-from destructor harmless;
- unscheduled destructor releases frame;
- no double destroy;
- invalid Task operations fail deterministically.

### Gate

All ownership tests green under sanitizers.

---

## M4 — Scheduler slots, TaskHandle, ready FIFO

### Deliverables

- fixed task registry/slot storage;
- generation counters;
- `TaskId`;
- `TaskHandle`;
- `create_task()`;
- `spawn()`;
- FIFO ready queue;
- `current_task()`;
- scheduler reentry guard;
- exact bounded `poll()` pass algorithm.

### Tests

- explicit scheduling;
- spawn detachment;
- FIFO order;
- pass-budget behavior;
- task slot exhaustion;
- slot reuse increments generation;
- stale handle invalid after reuse;
- `current_task()` inside/outside task;
- reentry error.

### Golden examples

- `01_Blink` skeleton;
- `02_TwoTasks`.

### Gate

No heap allocation and no task can execute twice in one scheduler pass.

---

## M5 — yield(), delay(), timer waits

### Deliverables

- `yield()`;
- `delay()`;
- `delay_ms()`;
- `delay_us()`;
- fixed timer wait structure;
- cached nearest deadline or equivalent O(1) idle-before-deadline fast path;
- deterministic equal-deadline ordering.

### Required semantics

```cpp
co_await yield();
co_await delay(0);
co_await delay_ms(0);
co_await delay_us(0);
```

all suspend and requeue for a later poll.

### Tests

- zero-delay fairness;
- positive delays;
- multiple deadline order;
- equal deadlines;
- large duration conversion;
- deadline-overflow error path;
- no early wake;
- no repeat wake;
- idle poll fast path behavior where measurable.

### Golden example

- `03_YieldFairness`.

### Gate

A continuously yielding task cannot starve another ready task.

---

## M6 — Parent/child await

### Deliverables

- `Task<void>::operator co_await() &&`;
- parent/child ownership transfer;
- parent `waiting_child` state;
- child completion cleanup;
- parent enqueue at ready FIFO tail;
- no symmetric transfer.

### Tests

- parent does not resume before child completion;
- nested children;
- child suspends on timer;
- child completes and is destroyed once;
- parent resumes on later poll, not inline;
- attempted second await fails deterministically.

### Golden example

- `04_ParentChild`.

### Gate

No recursive resume chain and full frame recovery after nested-child stress.

---

## M7 — WaitQueue and Event

### Deliverables

- intrusive FIFO WaitQueue;
- Event manual-reset state;
- `wait()`;
- `set()`;
- `clear()`;
- `isSet()`;
- destruction-with-waiters error.

### Tests

- clear wait suspends;
- set wakes all in FIFO order;
- Event remains set;
- wait while set does not suspend;
- clear affects future waits;
- tasks woken by set run on later poll;
- Event destruction with waiters rejected.

### Golden example

- `05_Event`.

### Gate

No Event API is callable as documented ISR-safe.

---

## M8 — ThreadSafeFlag and platform external signaling

### Deliverables

- single-waiter ThreadSafeFlag;
- auto-reset behavior;
- coalesced set state;
- scheduler external-pending bridge;
- target-specific short critical-section/synchronization backend;
- documented context guarantees for RP2040/RP2350/ESP32.

### Tests

Host simulated concurrency/state tests:

- set-before-wait;
- wait-before-set;
- repeated set coalesces;
- consume auto-clears;
- second waiter errors;
- set never inline-resumes;
- pending notification processed at poll start.

Hardware tests:

- GPIO/timer IRQ -> ThreadSafeFlag -> coroutine wake on RP2040;
- equivalent on RP2350;
- equivalent on ESP32-S3;
- stress repeated external signaling.

### Golden example

- `06_ThreadSafeFlagIRQ`.

### Gate

No deadlock under IRQ stress and no coroutine body executes in ISR context.

---

## M9 — Queue<T,N>

### Deliverables

- bounded ring buffer;
- raw/aligned object storage or equivalent;
- FIFO values;
- FIFO sender waiters;
- FIFO receiver waiters;
- send/receive awaiters;
- trySend/tryReceive;
- object construction/destruction correctness.

### Tests

- basic send/receive;
- empty receive suspension;
- full send suspension;
- FIFO values;
- FIFO waiters;
- ring index wrapping;
- move-only/non-default-constructible payload where supported;
- destructor calls exactly once;
- queue destruction with waiters rejected;
- long producer/consumer stress.

### Golden example

- `07_QueueProducerConsumer`.

### Gate

No dynamic allocation and no leaked payload objects.

---

## M10 — waitUntil helper and V1 integration

### Deliverables

- header-defined `waitUntil(predicate)` composition;
- final public header cleanup;
- diagnostics stats;
- library metadata;
- README V1 usage;
- blocking-code warning;
- scheduler-local vs external-context primitive documentation.

### Tests

- false predicate yields fairly;
- eventual true predicate resumes;
- immediate true predicate completes;
- other tasks continue while waiting.

### Golden example

- `08_WaitUntil`.

### Gate

All V1 API examples compile unchanged.

---

## M11 — V1 hardening

### Stress suites

- 100,000+ task completions;
- repeated child nesting;
- allocator fragmentation/recovery;
- repeated timer creation/completion;
- Event fan-out;
- ThreadSafeFlag IRQ storm within documented operating assumptions;
- Queue producer/consumer saturation.

### Build modes

Host:

```text
C++20 -O0
C++20 -O2
C++20 -Os where supported
ASan
UBSan
C++23 compatibility compile
```

Targets:

```text
RP2040
RP2350 Arm
RP2350 RISC-V where supported
ESP32
ESP32-S3
```

### Required final invariants

```text
activeTasks == 0 after completed test cycles
frameBytesUsed == 0 after completed test cycles
allocationFailures == 0 except explicit exhaustion tests
no stale waiter links
no double resumes
no double destroys
```

---

## Deferred post-V1 milestones

### V1.1 candidates

- `Task<T>` result values;
- AsyncLock;
- cancellation;
- timeout composition;
- when_any/when_all;
- TaskGroup.

### Later candidates

- CrossCoreQueue/ThreadSafeQueue;
- low-power nearest-deadline hardware wake;
- GPIO edge awaitables;
- DMA/PIO completion awaitables;
- UART/SPI/I2C adapters;
- chrono duration overloads;
- optional dual-scheduler multicore examples.

These must not be pulled into V1 merely because they are convenient to implement while touching adjacent code.

---

## Suggested coding-agent prompt for M0-M2

> Implement milestones M0 through M2 only. Follow `docs/simpleawait/V1_API_CONTRACT.md`, `docs/simpleawait/ARCHITECTURE.md`, `AGENTS.md`, and `docs/simpleawait/SimpleAwait_Implementation_Spec.md`. C++20 is the minimum language level; do not require C++23. Establish the build matrix, implement the platform `uint64_t` microsecond clock abstraction (`time_us_64()` for RP2040/RP2350, `esp_timer_get_time()` for ESP32, injected fake clock on host), then adapt the fixed variable-size TinyAwait-style coroutine frame allocator. Do not implement Task scheduling yet. Do not use global heap fallback. Add deterministic clock and allocator tests including sanitizers and allocation instrumentation. Stop after M2 acceptance criteria pass.

---

## Suggested coding-agent prompt for M3-M6

> Implement milestones M3 through M6 only on top of the green M0-M2 foundation. Add lazy move-only Task<void>, scheduler slots with generation-backed TaskHandle, explicit create_task/spawn, current_task, FIFO ready scheduling, bounded poll-pass semantics, yield/delay using the existing 64-bit microsecond clock, and parent/child awaiting. Do not implement Event, ThreadSafeFlag, Queue, cancellation, symmetric transfer, or typed Task results. Newly readied work must run on a later poll pass. Add the full ownership/fairness/timer/child test suite and preserve zero scheduler heap allocation.

---

## Suggested coding-agent prompt for M7-M10

> Implement milestones M7 through M10 only. Add intrusive WaitQueue, scheduler-local manual-reset Event, single-waiter auto-reset coalescing ThreadSafeFlag with platform-specific external-context signaling, bounded scheduler-local Queue<T,N>, waitUntil composition, diagnostics, examples, and V1 documentation. Do not add ISR methods to Event or Queue. ThreadSafeFlag::set() must never resume coroutine code directly. Add host and required hardware tests, and finish only when all V1 golden examples compile unchanged on the target matrix.

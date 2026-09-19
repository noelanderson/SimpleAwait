# ArduinoAwait Coding-Agent Instructions

This file is the operating contract for AI coding agents implementing ArduinoAwait.

Read these files before changing code:

1. `docs/arduinoawait/V1_API_CONTRACT.md`
2. `docs/arduinoawait/ARCHITECTURE.md`
3. `docs/arduinoawait/ArduinoAwait_Implementation_Spec.md`
4. `docs/arduinoawait/IMPLEMENTATION_PLAN.md`

If they conflict, do not silently choose one interpretation. For implementation work, the intended precedence is:

```text
docs/arduinoawait/V1_API_CONTRACT.md
    > docs/arduinoawait/ARCHITECTURE.md
    > docs/arduinoawait/ArduinoAwait_Implementation_Spec.md
    > docs/arduinoawait/IMPLEMENTATION_PLAN.md
```

When a conflict is discovered, update the documents together before implementing behavior that would make the inconsistency permanent.

---


## Independent review

At every milestone gate, use the independent review instructions in:

`/.github/agents/REVIEW.md`

The primary implementation prompt is available at:

`/.github/prompts/PRIMARY_AGENT_PROMPT.md`

Use a different model family or competing model for review when the development environment supports it.

---

## 1. Goal

Implement a small, deterministic, fixed-memory cooperative coroutine library for Arduino using native standard C++ coroutines.

Primary targets:

- RP2040;
- RP2350 Arm;
- RP2350 RISC-V where supported by the Arduino-Pico toolchain;
- ESP32 family, with ESP32-S3 as a required hardware target.

C++20 is the minimum supported language level. C++23 and later are allowed, but the core must not require post-C++20 features.

---

## 2. Core rules

You MUST:

- use native C++ coroutines;
- keep Task creation lazy;
- use explicit `create_task()`/`spawn()` for concurrent work;
- keep coroutine-frame ownership singular and explicit;
- use fixed-memory scheduler structures;
- use the fixed variable-size frame pool;
- keep ready and waiter ordering FIFO;
- make `delay(0)` and `yield()` real suspension/fairness points;
- run all user coroutine resumes through scheduler context;
- keep Event and Queue scheduler-local;
- use ThreadSafeFlag as the external-context notification primitive;
- use a 64-bit monotonic microsecond internal timebase;
- use `time_us_64()` on RP2040/RP2350;
- use `esp_timer_get_time()` on ESP32;
- use an injected fake microsecond clock for host tests;
- preserve deterministic failure behavior;
- add/update tests with every behavior change.

---

## 3. Things you MUST NOT do

Do not:

- allocate coroutine frames from the global heap;
- fall back to heap allocation when the frame pool is exhausted;
- use `std::vector`, `std::deque`, `std::list`, `std::map`, `std::unordered_map`, or `std::function` in the core scheduler;
- implement Task using FreeRTOS tasks;
- call `xTaskCreate()` from `create_task()` or `spawn()`;
- resume coroutine user code directly from an ISR;
- make ordinary Event or Queue implicitly ISR-safe;
- use a hardware timer object/alarm per coroutine delay;
- change the first-class scheduler clock back to `millis()` or 32-bit `micros()`;
- use a C++23+ feature without a C++20-compatible path;
- add task priorities, executors, thread pools, Futures, or callbacks to solve problems already expressible with Tasks/awaitables;
- introduce symmetric transfer in V1;
- perform unrelated refactors while implementing a feature;
- rewrite proven allocator logic for style alone;
- broaden desktop/platform support beyond deterministic host testing;
- add a third-party runtime dependency without an explicit requirement.

---

## 4. Change discipline

One architectural subsystem per logical change.

Good sequence:

```text
clock abstraction
then allocator
then Task ownership
then scheduler FIFO
then timers/yield
then child await
then WaitQueue/Event
then ThreadSafeFlag
then Queue
```

Bad change:

> "Refactor allocator, rename Task, add cancellation, switch timer model, and optimize ISR wakeups."

Do not proceed to the next implementation milestone while the current milestone's required host tests are failing.

---

## 5. Scheduler behavior is frozen

For V1, `poll()`:

1. samples the 64-bit clock;
2. processes pending external signals;
3. moves due timers to ready FIFO;
4. snapshots current ready count as the pass budget;
5. resumes each budgeted task at most once;
6. leaves newly readied tasks for a later `poll()`;
7. returns.

Do not change this into "run until idle".

Do not inline-resume a parent when a child completes.

Do not make `delay(0)` immediately ready.

---

## 6. Clock rules

Internal type:

```cpp
using tick_t = uint64_t; // microseconds
```

First-class backend APIs:

```text
RP2040/RP2350: time_us_64()
ESP32:          esp_timer_get_time()
Host:           injected fake uint64 microsecond clock
```

All scheduler clock reads flow through one platform abstraction.

Do not scatter direct platform clock calls through scheduler code.

Duration conversion must widen before multiplication.

Deadline overflow must follow the documented deterministic policy; never silently wrap.

Generic Arduino support may extend 32-bit `micros()` separately, but that compatibility backend must not dictate first-class target architecture.

---

## 7. Ownership checklist

Before committing any Task/lifetime change, verify:

- Who owns the frame before this operation?
- Who owns it after this operation?
- Is there exactly one owner?
- Can a moved-from Task destroy it?
- Can the scheduler destroy it while a Task still thinks it owns it?
- Can the task be enqueued twice?
- Can a stale TaskHandle refer to reused storage?
- Is the generation counter checked?
- Is a waiting task still linked anywhere when it completes/destroys?

Double resume, double destroy, and dangling waiter bugs are release blockers.

---

## 8. Frame allocator rules

The allocator must:

- remain fixed capacity;
- support variable-sized coroutine frames;
- satisfy required alignment;
- support arbitrary free order;
- recover all capacity after all frames are freed;
- fail deterministically when full.

When changing allocator code, run all allocator tests at multiple optimization levels and under ASan/UBSan.

Do not substitute one fixed maximum-sized slot per coroutine unless the architecture document is deliberately changed and RAM impact is justified.

---

## 9. WaitQueue rules

WaitQueue is scheduler-local and FIFO.

A task can be linked to only one local queue at a time.

A single intrusive next link may be shared by ready/Event/Queue queues because state exclusivity guarantees only one linkage at a time.

`remove(task)` may be O(n); do not add extra pointers merely to optimize future cancellation without measurements.

---

## 10. Event rules

Event is:

- scheduler-local;
- manual-reset;
- multi-waiter;
- FIFO wake order.

Never add `Event::setFromISR()` in V1.

External contexts use ThreadSafeFlag.

---

## 11. ThreadSafeFlag rules

ThreadSafeFlag is:

- single-waiter;
- auto-reset on consumption;
- coalescing;
- allocation-free;
- safe only in the external contexts explicitly guaranteed by the platform backend.

Its external `set()` path must not execute user code.

Do not blindly assume `std::atomic` is ISR-safe on RP2040. Validate the generated/runtime implementation or use the platform-specific short critical-section strategy defined in architecture.

Hardware-test IRQ signaling on every required architecture family.

---

## 12. Queue rules

`Queue<T,N>` is a bounded scheduler-local FIFO.

It must not require T to be default-constructible solely because storage exists.

Prefer aligned raw storage plus placement construction/destruction or an equivalent fixed-storage mechanism.

Test move-only/non-default-constructible values if the implementation claims support.

Ordinary Queue does not get ISR methods in V1.

---

## 13. C++ policy

Compile/test at the C++20 language floor.

Do not use C++23 library facilities as unconditional dependencies.

Exceptions must not be required. Support `-fno-exceptions` style embedded builds.

RTTI must not be required.

Avoid virtual dispatch in the scheduler core.

Prefer ordinary readable C++ over template metaprogramming cleverness.

---

## 14. Test policy

Every behavior change needs a deterministic host test.

Required categories:

- Task lazy creation and destruction;
- move semantics;
- create_task/spawn;
- TaskHandle generation/stale handles;
- FIFO fairness;
- zero-delay yield;
- positive timer order;
- equal-deadline timer order;
- deadline overflow;
- current_task;
- child await and later-poll parent continuation;
- scheduler reentry;
- Event state/waiters;
- ThreadSafeFlag coalescing/single waiter;
- Queue empty/full/waiters/index wrap/object lifetime;
- frame pool exhaustion/recovery;
- task slot exhaustion/reuse/generation;
- no unexpected global allocation.

Run host tests with:

- debug/no optimization;
- normal optimization;
- size-oriented optimization where practical;
- ASan;
- UBSan.

---

## 15. Golden examples

The following examples are executable API contracts and must continue to compile without source changes once introduced:

```text
01_Blink
02_TwoTasks
03_YieldFairness
04_ParentChild
05_Event
06_ThreadSafeFlagIRQ
07_QueueProducerConsumer
08_WaitUntil
```

If a public API change requires editing these examples, update `docs/arduinoawait/V1_API_CONTRACT.md` first.

---

## 16. Target build matrix

At minimum maintain compile coverage for:

```text
Arduino-Pico / RP2040
Arduino-Pico / RP2350 Arm
Arduino-Pico / RP2350 RISC-V when core/toolchain supports it
Arduino-ESP32 / ESP32
Arduino-ESP32 / ESP32-S3
```

Hardware runtime coverage must include at least RP2040, RP2350, and ESP32-S3 before V1 release.

Do not hard-code guessed Arduino architecture macros. Verify macros from the actual cores and isolate target detection in the platform layer.

---

## 17. Source/reference discipline

The design is informed by:

- TinyAwait;
- MicroPython asyncio;
- CPython asyncio;
- s_task;
- Raspberry Pi Pico SDK;
- ESP-IDF.

When copying or adapting source code rather than only ideas, record:

```text
source repository
commit/tag
source file
license
what was copied/adapted
```

Do not mix code with incompatible licensing into the repository.

Prefer independent implementation of behavioral ideas when licensing/provenance is unclear.

---

## 18. Do not optimize before correctness

Avoid these until all V1 correctness tests pass:

- symmetric coroutine transfer;
- timer heap/pairing heap;
- per-platform alarm scheduling;
- custom assembly;
- lock-free cross-core queues;
- coroutine migration;
- scheduler priorities.

A small O(n) path for at most tens of tasks is acceptable when it saves RAM and makes lifetime behavior easier to prove.

---

## 19. Required completion note for coding-agent work

For each completed milestone, report:

1. files changed;
2. public API changes, if any;
3. invariants affected;
4. tests added;
5. exact tests/builds run;
6. target compile results;
7. known limitations;
8. memory-size changes if measured.

Do not claim a target is supported merely because host tests pass.

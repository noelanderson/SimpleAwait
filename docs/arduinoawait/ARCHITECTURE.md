# ArduinoAwait Architecture

**Status:** Normative architecture for V1 implementation  
**Language floor:** C++20  
**Primary targets:** RP2040, RP2350 (Arm and RISC-V), ESP32 family  
**Related:** `ArduinoAwait_Implementation_Spec.md`, `V1_API_CONTRACT.md`, `../../AGENTS.md`, `IMPLEMENTATION_PLAN.md`

---

## 1. Architectural intent

ArduinoAwait is a statically allocated, cooperative coroutine runtime for Arduino-class MCUs. It is intentionally closer in behavior to MicroPython `asyncio` than to an RTOS, while using native standard C++ coroutines and a fixed-memory model inspired by TinyAwait.

The architecture optimizes for:

1. deterministic ownership and lifetime;
2. no global heap allocation for coroutine frames or scheduler metadata;
3. predictable cooperative scheduling;
4. a very small scheduler core;
5. safe separation between scheduler-local work and external/IRQ signaling;
6. portability across RP2040, RP2350, and ESP32 without exposing their RTOS/hardware differences in the Task API.

The core is not a general executor framework and is not a portability layer for desktop operating systems.

---

## 2. Language policy

C++20 is the minimum supported language level. The library must compile in C++20 mode and should also compile under C++23 and later standards.

The V1 implementation must not require a C++23-or-later feature. Any optional use of newer features must be feature-gated and have an equivalent C++20 path.

Native standard coroutine support is required:

```cpp
#include <coroutine>
```

The implementation must check coroutine feature availability and fail with a clear compile-time message when unavailable.

---

## 3. High-level component model

```text
Application code
     |
     | Task<void>, create_task(), spawn(), co_await
     v
+---------------------------+
|        Scheduler          |
|                           |
|  ready FIFO               |
|  timer wait set           |
|  current task             |
|  task registry/slots      |
|  external-signal pending  |
+------+-------------+------+
       |             |
       |             +--------------------+
       |                                  |
       v                                  v
 Scheduler-local waits              Cross-context signal
 Event / Queue / child              ThreadSafeFlag
       |                                  |
       +----------------+-----------------+
                        |
                        v
                coroutine_handle.resume()

Platform layer
   |
   +-- clock: uint64_t monotonic microseconds
   +-- short critical-section primitives
   +-- target/compiler feature detection
```

The scheduler never directly knows about GPIO, UART, SPI, I2C, PIO, USB, Wi-Fi, displays, or sensors. Those are higher-level awaitables built on the core primitives.

---

## 4. Non-negotiable invariants

The implementation must preserve all of the following invariants.

### 4.1 Single frame owner

Every live coroutine frame has exactly one lifetime owner at all times.

Possible owners are:

- an unscheduled `Task` object;
- the scheduler after `create_task()` or `spawn()`;
- the parent/child await relationship while a child task is awaited.

`TaskHandle` is observational and never owns a coroutine frame.

### 4.2 One scheduler state per task

A scheduled task has exactly one logical scheduler state at a time.

It may be:

- ready;
- running;
- waiting on a timer;
- waiting on a scheduler-local primitive;
- waiting on a child;
- completed.

It must never simultaneously be on two ready/wait queues.

### 4.3 No direct ISR coroutine execution

External contexts may mark work pending. They must never invoke user coroutine code or call `resume()` directly.

### 4.4 No implicit detached execution

Calling a coroutine function creates a lazy `Task`. It does not silently schedule background work.

### 4.5 No scheduler heap fallback

If fixed scheduler/coroutine memory is exhausted, the library follows the configured deterministic error path. It must not fall back to global `new`, `malloc`, `std::vector`, or another heap-backed structure.

### 4.6 All resumptions go through the scheduler in V1

V1 does not use symmetric transfer or immediate inline parent continuation. A completed child enqueues its parent at the tail of the ready FIFO.

This rule intentionally sacrifices a small optimization to simplify lifetime, fairness, diagnostics, and testing.

---

## 5. Task state machine

Recommended internal states:

```cpp
enum class TaskState : uint8_t {
    created,
    ready,
    running,
    waiting_timer,
    waiting_local,
    waiting_child,
    completed
};
```

Canonical transition graph:

```text
                    create_task()/spawn()
          +-----------------------------------+
          |                                   v
      +---------+                         +-------+
      | created | -- co_await as child -->| ready |
      +---------+                         +---+---+
                                              |
                                           resume
                                              |
                                              v
                                          +-------+
                                          |running|
                                          +---+---+
                                              |
              +-------------------------------+-----------------------------+
              |                |               |              |             |
            yield          delay>0          Event/Queue      child        return
              |                |               |              |             |
              v                v               v              v             v
           ready        waiting_timer     waiting_local  waiting_child  completed
              ^                |               |              |             |
              |                +------- due ---+---- wake -----+             |
              |                                                               |
              +----------------------- enqueue parent/work --------------------+
```

Illegal transitions are programming/runtime errors in diagnostic builds.

Examples of illegal states:

- resuming a task already `running`;
- scheduling a task already scheduler-owned;
- completing the same task twice;
- placing a waiting task on a second wait queue;
- destroying a frame while it remains linked to scheduler metadata.

---

## 6. Task ownership model

| Situation | Frame owner | Scheduler-visible? |
|---|---|---:|
| `auto t = foo();` | `t` | No |
| `co_await foo();` | parent/child relationship | Yes, once started |
| `create_task(foo())` | scheduler | Yes |
| `spawn(foo())` | scheduler | Yes |
| Task waiting on timer/event/queue | scheduler/parent relationship remains unchanged | Yes |
| Completed scheduler-owned task | scheduler destroys frame | Until cleanup |
| Unscheduled `Task` destructor | `Task` destroys frame | No |

`Task` is move-only. Moving transfers the single ownership token. The moved-from object becomes empty.

An unscheduled Task destructor destroys its coroutine frame without executing it further.

---

## 7. TaskHandle identity and stale-handle protection

A `TaskHandle` must not be a raw pointer or raw coroutine handle exposed as stable identity.

Use a scheduler slot identity containing at least:

```cpp
struct TaskId {
    uint16_t slot;
    uint32_t generation;
};
```

The exact integer widths may be adjusted, but the design must include a generation counter so a handle to a completed task cannot accidentally refer to a later task that reuses the same slot.

`TaskHandle::valid()` checks both slot bounds and generation match. A completed slot retains its generation/tombstone state until reuse, so a handle remains valid and reports `done() == true` after completion. Reusing the slot increments generation; older handles then become invalid and report `done() == false`. The implementation must not dereference stale frame storage.

---

## 8. Scheduler data model

Recommended fixed scheduler metadata:

```cpp
struct TaskControl {
    std::coroutine_handle<> handle;
    TaskState state;

    // Generation-backed public identity.
    uint32_t generation;

    // Intrusive queue link. A task is only on one scheduler-local FIFO at once.
    TaskControl* next;

    // Awaited child continuation relationship.
    TaskControl* parent;

    // Timer deadline when state == waiting_timer.
    uint64_t deadline_us;

    // Optional diagnostic fields under compile-time guard.
};
```

This is conceptual, not mandatory byte layout. The implementation may place some fields in `promise_type`, but it must avoid duplicating ownership/state information unnecessarily.

A task must not need separately heap-allocated scheduler nodes.

---

## 9. Exact V1 scheduler pass

`Scheduler::poll()` must be bounded and deterministic.

V1 algorithm:

1. reject/recover from scheduler reentry according to configured error policy;
2. mark scheduler as inside `poll()`;
3. sample `PlatformClock::now_us()` once;
4. if an external-signal-pending indicator is set, process cross-context signaling and enqueue newly ready tasks;
5. inspect timer waits using the sampled time and enqueue all due tasks in deterministic order;
6. snapshot the number of tasks currently in the ready FIFO; call this `budget`;
7. repeat at most `budget` times:
   - pop one task from the ready FIFO;
   - validate it is `ready`;
   - set it to `running`;
   - set `current_task`;
   - resume it once;
   - clear `current_task` after control returns;
   - process completion or the new suspended state;
8. tasks that become ready while step 7 is executing are appended to the FIFO but are not included in the current `budget`;
9. clear the in-poll marker;
10. return to Arduino `loop()`.

Consequences:

- `yield()` always waits until a later `poll()` pass;
- `delay(0)` is equivalent to `yield()`;
- an Event set by one running task wakes waiters for a later scheduler pass;
- a child completion enqueues its parent for a later scheduler pass;
- no recursive resume chain is needed.

This is the V1 reference behavior and must be tested as such.

---

## 10. Ready FIFO

The ready queue is intrusive and fixed-memory.

Required operations:

```text
push_back(TaskControl*)     O(1)
pop_front()                 O(1)
size()                      O(1)
```

Ready ordering is FIFO.

A task that yields is appended after tasks already ready.

A task woken by an Event/Queue/child/timer is appended after tasks already ready at the moment of wakeup.

---

## 11. Timer architecture

### 11.1 Native scheduler timebase

The internal clock type is:

```cpp
using tick_t = uint64_t; // microseconds
```

Absolute deadlines use `uint64_t` microseconds.

The scheduler does not use `millis()` or a 32-bit timestamp on first-class targets.

### 11.2 RP2040/RP2350

Use the Pico SDK monotonic 64-bit microsecond timer:

```cpp
uint64_t time_us_64();
```

This is the preferred backend for RP2040 and RP2350, including RP2350 Arm and RISC-V when building through the supported Arduino-Pico environment.

Reference: <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html>

### 11.3 ESP32

Use:

```cpp
int64_t esp_timer_get_time();
```

Convert its non-negative scheduler-lifetime result to `uint64_t`.

Reference: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html>

Deep sleep begins a new scheduler/time epoch unless a future persistence feature explicitly defines otherwise.

### 11.4 Host tests

Use an injected fake clock:

```cpp
struct FakeClock {
    static inline uint64_t now_us_value = 0;
    static uint64_t now_us() noexcept { return now_us_value; }
};
```

No wall-clock sleeping is required in unit tests.

### 11.5 Generic Arduino compatibility

A compatibility backend may software-extend a 32-bit `micros()` source. This is not the architecture used by the first-class targets.

If such extension is implemented, it must document the requirement to sample frequently enough to observe every underlying wrap.

### 11.6 Timer container

V1 should retain TinyAwait's small fixed-memory timer philosophy rather than introduce a general dynamic priority queue.

Acceptable implementation:

- a fixed timer slot/wait structure sized from task capacity;
- a cached nearest deadline so idle polls before the next deadline are O(1);
- when the nearest deadline is reached, scan/rebuild as needed.

Timer insertion may be O(n) because task counts are intentionally small.

### 11.7 Deadline overflow

Duration conversion must widen before multiplication.

V1 policy: if `now_us + duration_us` would overflow `uint64_t`, reject the operation through the configured error hook using `Error::deadline_overflow`. Silent wrap and saturation are forbidden in V1.

---

## 12. Hardware alarms and low-power operation

V1 uses the native hardware clock but does not map each coroutine timer onto a Pico hardware alarm or ESP Timer object.

The event loop remains driven by `poll()`.

A future low-power extension may:

1. identify the nearest scheduler deadline;
2. arm one platform wake source;
3. sleep the MCU;
4. wake into scheduler context;
5. let normal `poll()` process due tasks.

The alarm ISR must never resume user coroutine code directly.

---

## 13. Coroutine frame allocator

The allocator is a fixed-size arena and should preserve/adapt TinyAwait's proven variable-size frame approach.

Requirements:

- no global heap fallback;
- supports compiler-requested coroutine-frame alignment;
- supports variable-sized frames;
- supports arbitrary destruction order;
- coalesces reusable adjacent free regions if using a free-list/block allocator;
- returns complete capacity after all tasks are destroyed;
- deterministic failure path on exhaustion;
- host tests instrument global allocation to detect accidental fallback.

At minimum, alignment must satisfy `alignof(std::max_align_t)`, and the allocator must honor any larger alignment explicitly requested by the compiler/runtime mechanism used for coroutine allocation.

---

## 14. WaitQueue

Scheduler-local synchronization primitives share an intrusive FIFO waiter abstraction.

Conceptual API:

```cpp
class WaitQueue {
public:
    bool empty() const noexcept;
    void push_back(TaskControl*) noexcept;
    TaskControl* pop_front() noexcept;
    bool remove(TaskControl*) noexcept; // may be O(n)
};
```

A singly linked queue is preferred for RAM efficiency. Future cancellation may remove a waiter in O(n); this is acceptable for the expected small task count.

A waiting task's generic intrusive `next` link may be reused because a task cannot be ready and waiting simultaneously.

---

## 15. Event

`Event` is scheduler-local and manual-reset.

State:

```text
clear
set
```

Behavior:

- `wait()` on a set Event completes without suspension;
- `wait()` on a clear Event appends the task to the Event waiter FIFO;
- `set()` changes state to set and moves all current waiters to scheduler ready FIFO in FIFO order;
- Event remains set until `clear()`;
- `clear()` affects future waits only;
- no `setFromISR()` exists on ordinary Event.

Destroying an Event with active waiters is a programming error unless a future explicit shutdown/cancellation semantic is added.

---

## 16. ThreadSafeFlag

`ThreadSafeFlag` is the deliberately separate external-context bridge, inspired by MicroPython `asyncio.ThreadSafeFlag`.

Semantics:

- single waiter;
- auto-reset when a signal is consumed;
- repeated `set()` calls while already set coalesce into one pending signal;
- `set()` may be called from the target's documented IRQ/callback/other-core contexts;
- `wait()` is called only from scheduler coroutine context;
- a second simultaneous waiter is a deterministic programming error;
- `set()` never resumes coroutine code directly.

Conceptual state:

```text
signaled: false/true
waiter: none/TaskControl*
```

### 16.1 External pending strategy

To avoid scanning external flags on every idle poll, the scheduler should maintain a cheap global/per-scheduler `external_pending` indication.

`ThreadSafeFlag::set()`:

1. enters the platform's short cross-context-safe metadata protection;
2. marks the flag signaled;
3. marks scheduler external work pending;
4. exits protection;
5. returns without scheduler list manipulation that would execute user code.

At the beginning of `poll()`, if external work is pending, scheduler context resolves signaled flags/waiters and enqueues tasks normally.

Implementation may use another fixed pending structure if it is demonstrably safer/smaller, but it must remain bounded and allocation-free.

### 16.2 Platform synchronization caution

Do not assume `std::atomic` is automatically ISR-safe on every target. Some RP2040 atomic operations may be implemented using locks. Platform code must be selected/tested for ISR and multicore behavior.

Short critical sections must prevent same-core IRQ preemption while any shared cross-core lock is held, avoiding the classic "ISR spins on a lock held by interrupted code" deadlock.

The platform layer owns these details; generic coroutine code does not.

---

## 17. Queue<T, N>

Ordinary `Queue<T,N>` is scheduler-local.

Data ordering and waiter ordering are FIFO.

Required behavior:

- `send()` completes immediately when capacity exists;
- otherwise sender suspends in FIFO order;
- `receive()` completes immediately when data exists;
- otherwise receiver suspends in FIFO order;
- successful receive creates capacity and may wake the oldest sender;
- successful send creates data and may wake the oldest receiver;
- wakeups enqueue tasks; they do not inline-resume them.

### 17.1 Object storage

Do not require `T` to be default constructible merely for queue storage.

Preferred storage uses aligned uninitialized slots plus placement construction/destruction, or an equivalent fixed-storage mechanism.

The queue should support moveable non-default-constructible values where reasonably possible in C++20.

No dynamic container is allowed.

### 17.2 ISR use

Ordinary Queue has no ISR methods in V1.

A later `ThreadSafeQueue`/`CrossCoreQueue` is a separate abstraction with separate synchronization cost and semantics.

---

## 18. waitUntil

V1 `waitUntil(predicate)` is a convenience composition, not a new scheduler primitive.

Preferred implementation is a small coroutine helper equivalent to:

```cpp
template <class Predicate>
Task<void> waitUntil(Predicate predicate) {
    while (!predicate()) {
        co_await yield();
    }
}
```

This keeps arbitrary predicate invocation out of scheduler internals.

A future optimized polling awaitable may be added only if profiling justifies it.

---

## 19. Parent/child await

Awaiting a child Task transfers the child's frame ownership into the parent/child relationship and starts/schedules the child.

The parent enters `waiting_child`.

When child completes:

1. record child completion;
2. destroy/release child frame exactly once;
3. clear parent/child linkage;
4. enqueue parent at ready FIFO tail;
5. parent runs in a later scheduler pass.

V1 does not use symmetric transfer.

---

## 20. current_task

The Scheduler sets `current_task` immediately before invoking `resume()` and clears it immediately after control returns.

`current_task()` outside coroutine execution returns an invalid TaskHandle.

The handle uses generation-checked identity, not a naked pointer.

---

## 21. Error policy

Core failures are deterministic and non-exception-based.

Representative errors:

```cpp
enum class Error : uint8_t {
    none,
    task_limit,
    frame_pool_exhausted,
    scheduler_reentry,
    invalid_task,
    task_already_scheduled,
    task_awaited_twice,
    multiple_flag_waiters,
    object_destroyed_with_waiters,
    deadline_overflow,
    unhandled_exception,
    internal_error
};
```

Exact V1 names are frozen in `V1_API_CONTRACT.md`.

The default error hook may abort/halt, but the core does not depend on `Serial`.

Builds with exceptions disabled must be supported.

---

## 22. Multicore policy

Coroutines never migrate between schedulers/cores.

For RP2040/RP2350 a future dual-loop model may use one Scheduler per core:

```cpp
void loop()  { scheduler0.poll(); }
void loop1() { scheduler1.poll(); }
```

Ordinary Event and Queue remain local to one scheduler.

Cross-core payload transport is deferred to a dedicated bounded primitive such as `CrossCoreQueue<T,N>`.

`ThreadSafeFlag` may be used for simple notification where the platform backend explicitly supports the calling context.

---

## 23. ESP32/FreeRTOS boundary

A C++ ArduinoAwait Task is not a FreeRTOS task.

`create_task()` and `spawn()` do not call `xTaskCreate()`.

The default scheduler runs within normal Arduino execution context. FreeRTOS-specific integration may exist only in the platform bridge or optional adapters.

---

## 24. STL and runtime policy

Allowed/expected core facilities include:

```text
std::coroutine_handle
std::move / std::forward
std::array where appropriate
std::byte
type_traits
utility
new placement construction
```

Core scheduler code must not use heap-backed containers or ownership abstractions such as:

```text
std::vector
std::deque
std::list
std::map
std::unordered_map
std::function
std::shared_ptr
```

`std::unique_ptr` is not used to represent coroutine-frame ownership; the custom Task ownership model is explicit.

---

## 25. Diagnostics

Diagnostics are compile-time optional and allocation-free.

Recommended metrics:

```cpp
struct Stats {
    size_t active_tasks;
    size_t peak_tasks;
    size_t ready_tasks;
    size_t waiting_timers;
    size_t frame_bytes_used;
    size_t peak_frame_bytes_used;
    size_t frame_bytes_free;
    size_t allocation_failures;
};
```

Diagnostics must not change scheduling semantics.

---

## 26. Testing architecture

### Host tests

Use a fake 64-bit microsecond clock and deterministic scheduler stepping.

Test:

- every legal state transition;
- every illegal state transition/error path;
- lazy Task ownership;
- generation-safe stale TaskHandle behavior;
- ready FIFO fairness;
- no same-pass resume after yield;
- parent resumes on later poll after child completion;
- timer ordering and identical deadlines;
- deadline-overflow policy;
- frame allocator fragmentation/recovery;
- Event FIFO wakes;
- ThreadSafeFlag coalescing and single waiter;
- Queue data/waiter ordering;
- destruction-with-waiters errors;
- scheduler reentry;
- accidental global allocation.

Use ASan and UBSan on host builds.

### Target tests

First-class hardware matrix:

- RP2040;
- RP2350 Arm;
- RP2350 RISC-V when supported by the chosen Arduino-Pico toolchain;
- ESP32-S3;
- ESP32 baseline where practical.

Target tests verify native clock backend monotonicity, IRQ signal bridge, long-running allocation recovery, and representative peripheral wakeups.

---

## 27. Reference decisions

Behavioral references:

- TinyAwait: fixed-memory coroutine allocation and small scheduler concepts;
- MicroPython asyncio: explicit task scheduling, zero-sleep fairness, Event vs ThreadSafeFlag split, waiter-oriented synchronization;
- CPython asyncio: structured concurrency concepts for post-V1;
- s_task: embedded queue/event patterns only, not stackful implementation architecture.

Platform clock references:

- Raspberry Pi `time_us_64()`: <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html>
- Espressif `esp_timer_get_time()`: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html>

---

## 28. Architecture decision summary

For code-generation purposes, the following choices are frozen for V1:

1. C++20 minimum; C++23+ allowed.
2. Lazy move-only `Task<void>`.
3. Explicit `create_task()` and `spawn()`.
4. Scheduler-owned FIFO ready queue.
5. `poll()` executes a snapshot budget; newly ready tasks run on a later poll.
6. No symmetric transfer in V1.
7. `delay(0)` is exactly a fair yield point.
8. Internal clock is `uint64_t` microseconds.
9. RP2040/RP2350 use `time_us_64()`.
10. ESP32 uses `esp_timer_get_time()`.
11. Hardware alarms are not used per coroutine timer in V1.
12. Fixed variable-size coroutine frame pool; no heap fallback.
13. Intrusive FIFO WaitQueue for scheduler-local waits.
14. Event is manual-reset and scheduler-local.
15. ThreadSafeFlag is single-waiter, auto-reset, coalescing, external-context safe through platform code.
16. Queue is bounded, static, FIFO, and scheduler-local.
17. `waitUntil()` is composition, not scheduler machinery.
18. TaskHandle uses slot + generation stale-handle protection.
19. All user coroutine resumes happen in scheduler context.
20. FreeRTOS is not the Task implementation.

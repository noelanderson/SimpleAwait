# Arduino Cooperative Coroutine Library
## Implementation Specification

**Working name:** `SimpleAwait`  
**Alternative names:** `TinyAwaitArduino`, `MicroAwait`, `CoroArduino`

**Target version:** 0.3 / MVP design  
**Language:** C++20 minimum; C++23 and later supported  
**Primary environments:** Arduino / PlatformIO  
**Primary MCU targets:** RP2040, RP2350, ESP32 family

---

# 1. Purpose

Implement a small, deterministic, cooperative asynchronous task library for Arduino-class microcontrollers using native standard C++ coroutines, with C++20 as the minimum supported language level.

The library shall allow embedded code to be written using normal sequential control flow:

```cpp
Task blink() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay(500);

        digitalWrite(LED_BUILTIN, LOW);
        co_await delay(500);
    }
}
```

instead of manual state machines:

```cpp
if (state == ON && millis() - last >= 500) {
    digitalWrite(LED_BUILTIN, LOW);
    state = OFF;
    last = millis();
}
```

The design shall provide **cooperative concurrency**, not preemptive threading.

Only one coroutine executes at a time within a scheduler. Context changes occur only at explicit coroutine suspension points.

---

# 2. Design heritage

SimpleAwait SHALL deliberately combine ideas from four sources while keeping an Arduino-specific implementation.

## 2.1 TinyAwait — implementation foundation

Use **TinyAwait** as the primary low-level implementation starting point, preserving its embedded-specific strengths where they remain correct:

- native standard C++ `co_await`;
- heap-free coroutine frame allocation;
- fixed total coroutine memory;
- variable-size coroutine frames;
- no thread creation;
- no RTOS requirement;
- nested/child coroutine execution;
- inexpensive scheduler polling;
- compact timer/scheduler data structures.

TinyAwait's allocator and scheduler ideas should be reused or adapted where they remain appropriate. Its `uint32_t`/`millis()` timebase is **not** a compatibility requirement for SimpleAwait: first-class RP2040/RP2350/ESP32 targets use a 64-bit monotonic microsecond platform clock.

## 2.2 MicroPython `asyncio` — behavioral model

Use current MicroPython `asyncio` as the primary behavioral inspiration for the task/scheduler API because it addresses the same class of problem on constrained microcontrollers.

Adopt these ideas:

- creating a coroutine object does not implicitly mean "detached task";
- explicit task scheduling via `create_task()`;
- `current_task()` introspection;
- FIFO cooperative fairness;
- a zero-duration sleep as a real yield point;
- task-local `Event` distinct from cross-context signaling;
- a single-waiter, auto-reset `ThreadSafeFlag` for IRQ/thread/core-to-event-loop notification;
- waiter queues for locks/events/tasks;
- composition of higher-level operations such as timeout/gather from simpler primitives;
- a small API rather than a complete desktop `asyncio` clone.

Do not copy MicroPython's dynamic-memory implementation. Translate the behavioral model into statically allocated C++ structures.

## 2.3 CPython `asyncio` — structured concurrency reference

Use modern CPython `asyncio` selectively for higher-level API ideas, especially:

- `Task` as an awaitable scheduled unit of work;
- `create_task()` as explicit scheduling;
- `TaskGroup` as a structured-concurrency model;
- cancellation-aware timeout composition.

Do not import CPython concepts that add little value on an MCU, such as a general `Future`/callback/executor hierarchy.

## 2.4 `s_task` — embedded primitive reference

Use `s_task` as a behavioral/API reference for:

- bounded channels/queues;
- events;
- timeout-capable waits;
- explicit interrupt/task communication.

Do **not** copy the `s_task` implementation architecture. `s_task` is stackful, allocates per-task stack storage, and contains broad platform/libuv support outside this project's scope.

## 2.5 Design reference links

- TinyAwait: <https://github.com/jafarmemar/TinyAwait>
- MicroPython asyncio documentation: <https://docs.micropython.org/en/latest/library/asyncio.html>
- MicroPython source: <https://github.com/micropython/micropython/tree/master/extmod/asyncio>
- CPython asyncio tasks: <https://docs.python.org/3/library/asyncio-task.html>
- s_task: <https://github.com/xhawk18/s_task>

---

# 3. Primary design goals

The implementation MUST prioritize, in order:

1. predictable behavior;
2. fixed memory usage;
3. simple Arduino integration;
4. readable `co_await`-based application code;
5. low RAM overhead;
6. low flash overhead;
7. architecture independence;
8. deterministic failure behavior;
9. straightforward implementation;
10. straightforward debugging.

Feature completeness is less important than simplicity.

---

# 4. Explicit non-goals

The V1 library SHALL NOT implement:

- preemptive multitasking;
- POSIX threads;
- `std::thread`;
- FreeRTOS tasks as coroutine tasks;
- task migration between CPU cores;
- dynamic STL containers;
- arbitrary desktop OS support;
- libuv integration;
- networking abstraction;
- filesystem abstraction;
- thread pools;
- priority scheduling;
- task priorities;
- work stealing;
- general-purpose executors;
- exceptions as normal control flow;
- blocking synchronization primitives;
- implicit multicore synchronization.

The library shall remain usable on ESP32 despite the presence of FreeRTOS, but FreeRTOS shall not define the public programming model.

---

# 5. Supported platforms

## Required

### Raspberry Pi RP2040

Support:

- Raspberry Pi Pico;
- Pico W where Arduino core supports required C++20 coroutine facilities;
- Earle Philhower Arduino-Pico core.

### Raspberry Pi RP2350

Support:

- ARM Cortex-M33 build;
- RISC-V build where supported by the Arduino toolchain;
- Pico 2 class boards.

### ESP32

At minimum test:

- ESP32;
- ESP32-S3.

Desirable:

- ESP32-C3;
- ESP32-C6.

---

# 6. Language and toolchain requirements

C++20 is the **minimum supported language level**, not the only supported language level. The library SHALL compile under conforming C++20 builds and SHOULD also compile unchanged under C++23 and later standards.

The core public API and implementation MUST NOT require a feature newer than C++20. Newer-language optimizations MAY be used only behind feature-detection guards with a C++20-equivalent path.

At compile time verify native coroutine support using feature checks such as:

```cpp
__cpp_impl_coroutine
```

and availability of:

```cpp
#include <coroutine>
```

and, where provided by the standard library:

```cpp
__cpp_lib_coroutine
```

If coroutine support is unavailable, compilation MUST fail with a readable error such as:

```text
SimpleAwait requires C++20 or later with standard coroutine support
```

Do not determine compatibility solely through `__cplusplus`.

Supported builds may select `-std=c++20`, `-std=gnu++20`, `-std=c++23`, `-std=gnu++23`, or a later equivalent according to the Arduino core/toolchain. Tests MUST include at least one C++20-mode build because that is the compatibility floor.

---

# 7. Core architecture

The library shall consist conceptually of:

```text
Application
    │
    ▼
Task<T> / TaskHandle
    │
    ├──────────────────────┐
    ▼                      ▼
Awaitables              Synchronization
    │                      │
    ├─ delay                ├─ Event
    ├─ yield                ├─ Queue<T,N>
    ├─ waitUntil            ├─ AsyncLock [V1.1]
    └─ child task           └─ ThreadSafeFlag
             │
             ▼
         Scheduler
      ┌──────┼─────────┐
      ▼      ▼         ▼
 ready FIFO timers   wait queues
      │      │         │
      └──────┴─────────┘
             │
             ▼
      coroutine_handle

External execution contexts
 IRQ / core1 / RTOS callback
             │
             ▼
      ThreadSafeFlag
             │
             ▼
         Scheduler
```

The scheduler MUST NOT know about:

- GPIO;
- UART;
- I²C;
- SPI;
- USB;
- Wi-Fi;
- displays;
- sensors.

Those integrations shall be implemented using awaitables and cross-context signaling primitives layered above the scheduler.

The design SHALL distinguish between:

1. **scheduler-local primitives** such as `Event` and `Queue`, which may manipulate waiter queues directly; and
2. **cross-context primitives** such as `ThreadSafeFlag`, which may be signaled from IRQs, another core, callbacks, or other execution contexts but never directly resume coroutine bodies there.

---

# 8. Public namespace

All public library entities shall live beneath:

```cpp
namespace simpleawait {
}
```

Optional alias:

```cpp
namespace sa = simpleawait;
```

Avoid placing library internals in the global namespace.

---

# 9. Core user API

The desired application style is:

```cpp
#include <SimpleAwait.h>

using namespace simpleawait;

Task<void> blink() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay(500);

        digitalWrite(LED_BUILTIN, LOW);
        co_await delay(500);
    }
}

Task<void> application() {
    auto blinkTask = create_task(blink());

    // Other cooperative work...
    while (true) {
        co_await delay(1000);
    }
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    spawn(application());
}

void loop() {
    simpleawait::poll();
}
```

The API SHALL distinguish:

```cpp
// Create an inert/lazy Task object.
auto task = foo();

// Sequential composition: start child and wait for it.
co_await foo();

// Concurrent scheduling with a handle/owner.
auto taskHandle = create_task(foo());

// Explicit fire-and-forget detached scheduling.
spawn(foo());
```

A convenience singleton scheduler is preferred for the common Arduino case, while explicit scheduler instances may be supported for advanced/multicore use.

---

# 10. Task model

## 10.1 Task type

Implement first:

```cpp
Task<void>
```

and design for later:

```cpp
Task<T>
```

Recommended public syntax:

```cpp
Task<void> foo();
Task<int> getValue();
```

V1 MAY implement only `Task<void>`, but the promise/lifetime model MUST not prevent later `Task<T>` support.

## 10.2 Lazy task creation

Calling a coroutine-producing function SHALL create a Task but SHALL NOT implicitly detach it:

```cpp
auto task = foo();
```

The Task begins execution when it is:

- awaited with `co_await`;
- scheduled with `create_task()`; or
- detached with `spawn()`.

This normally implies `initial_suspend()` behavior equivalent to a lazy coroutine.

## 10.3 TaskHandle

Provide a small non-copying or reference-like scheduled task handle:

```cpp
TaskHandle handle = create_task(foo());
```

At minimum it should support:

```cpp
bool done() const;
bool valid() const;
```

Post-MVP it may support:

```cpp
void cancel();
TaskId id() const;
```

A `TaskHandle` must not itself require heap allocation.

## 10.4 current_task()

Provide:

```cpp
TaskHandle current_task();
```

or an equivalent lightweight task reference.

While a coroutine is running, the Scheduler SHALL track the current task. Outside coroutine execution, `current_task()` shall return an invalid/empty handle.

This facility is required in V1 because it is low-cost and useful for diagnostics, cancellation design, and synchronization internals.

---

# 11. Task execution semantics

Three execution modes shall be distinct.

## 11.1 Awaited child task

```cpp
Task<void> initialize() {
    co_await initializeDisplay();
    co_await initializeSensors();
}
```

The parent coroutine MUST remain suspended until the child completes.

```text
parent
  |
  +--> child starts
         |
         +-- suspend / resume as needed
         |
         +-- complete
  |
  +--> parent becomes ready
```

Child completion shall release or transfer ownership of the child coroutine frame exactly once before or as part of continuation handling.

## 11.2 Scheduled task

```cpp
auto h = create_task(worker());
```

The task SHALL be placed on the scheduler's ready FIFO and execute cooperatively. `create_task()` SHALL return without running arbitrary user coroutine code inline unless this is specifically documented and proven necessary; default behavior should be scheduling for a later scheduler step.

The returned handle observes the scheduled task but does not create a second frame owner.

## 11.3 Detached task

```cpp
spawn(statusLed());
```

The scheduler assumes lifetime ownership until completion.

---

# 12. create_task() and detached execution

## 12.1 create_task()

Implement:

```cpp
[[nodiscard]] TaskHandle create_task(Task<void>&& task);
```

Later generalize for `Task<T>` as needed.

`create_task()` SHALL:

1. validate that the Task owns a coroutine frame;
2. transfer scheduling/lifetime responsibility to the scheduler;
3. enqueue the task at the back of the ready FIFO;
4. return a lightweight handle identifying the scheduled task;
5. not allocate from the global heap.

## 12.2 spawn()

Implement:

```cpp
void spawn(Task<void>&& task);
```

as explicit fire-and-forget scheduling.

Conceptually:

```cpp
spawn(foo());
```

is equivalent to creating a scheduled task and intentionally discarding the observation handle.

`spawn()` MUST be explicit. Users must not need to retain a C++ object merely to keep detached work alive.

---

# 13. No implicit detachment

Calling a Task-producing function MUST NOT silently start a detached task.

Bad:

```cpp
foo();   // must not silently become scheduler-owned background work
```

Good:

```cpp
co_await foo();          // sequential
create_task(foo());      // concurrent, observable
spawn(foo());            // concurrent, explicitly detached
```

Reasons:

- matches the mental model used by Python `asyncio`;
- makes lifetime ownership explicit;
- catches accidentally discarded coroutine objects;
- simplifies future typed results;
- simplifies cancellation and TaskGroup semantics;
- supports structured concurrency.

If adapting TinyAwait requires a temporary compatibility path for its existing eager/detached behavior, isolate that behavior behind a compatibility macro and do not make it the SimpleAwait default.

---

# 14. Scheduler

Provide:

```cpp
class Scheduler;
```

Minimum interface:

```cpp
class Scheduler {
public:
    void poll();

    bool hasReadyTasks() const;
    bool hasPendingTasks() const;
    std::size_t activeTaskCount() const;

    TaskHandle currentTask() const;
};
```

Common-case singleton/helper API:

```cpp
Scheduler& scheduler();
void poll();
TaskHandle current_task();
```

The common Arduino API SHOULD NOT require explicit Scheduler construction.

---

# 15. Scheduler semantics

`poll()` SHALL perform bounded cooperative scheduler work and return to Arduino `loop()`.

At a high level it shall:

1. sample the monotonic clock once where practical;
2. process pending cross-context notifications such as `ThreadSafeFlag` wakeups;
3. identify expired timers and enqueue their tasks onto the ready FIFO;
4. take ready tasks in FIFO order;
5. set `current_task` before resuming a coroutine;
6. resume coroutine code only in scheduler context;
7. clear/update `current_task` after suspension/completion;
8. process coroutine completion and release frames exactly once;
9. return without waiting for future work.

It MUST NOT block waiting for timers or events.

Typical loop:

```cpp
void loop() {
    simpleawait::poll();

    // ordinary Arduino code remains valid
}
```

The scheduler shall not invoke arbitrary user coroutine code directly from an IRQ or other external execution context.

---

# 16. Fairness

SimpleAwait SHALL use FIFO ready-task semantics by default.

When a running task voluntarily yields, it SHALL be placed behind tasks that are already ready.

Required fairness rule:

> A continuously yielding task must not starve another ready task.

Recommended scheduler-pass rule:

> A coroutine resumed during one `poll()` pass may be resumed at most once during that pass unless a deliberate child-continuation optimization is used and proven not to violate fairness.

The following MUST both create fairness points:

```cpp
co_await yield();
co_await delay(0);
```

This intentionally follows MicroPython `asyncio.sleep_ms(0)` semantics rather than TinyAwait's current zero-delay immediate-ready behavior.

---

# 17. Yield awaitable

Implement:

```cpp
co_await yield();
```

Semantics:

- `await_ready()` SHALL be false;
- suspend the current coroutine;
- enqueue it at the back of the ready FIFO;
- allow already-ready tasks to run first;
- resume during a subsequent scheduler opportunity.

Example:

```cpp
Task<void> processing() {
    while (true) {
        processSmallChunk();
        co_await yield();
    }
}
```

`yield()` should be implemented as the canonical zero-delay scheduler yield primitive, and `delay(0)` MAY internally delegate to the same mechanism.

---

# 18. Delay awaitable

Implement:

```cpp
co_await delay(milliseconds);
```

Examples:

```cpp
co_await delay(0);
co_await delay(10);
co_await delay(500);
```

## 18.1 Zero-duration semantics

`delay(0)` MUST suspend at least once and yield fairly:

```cpp
co_await delay(0);
```

is semantically equivalent to:

```cpp
co_await yield();
```

Do not implement zero delay as `await_ready() == true`.

## 18.2 Positive delays

A positive delay SHALL suspend the task until the deadline is due, then place the task at the back of the ready FIFO.

Direct numeric `co_await 500` compatibility MAY be provided but SHALL NOT be the preferred documented API.

---

# 19. Delay and internal time types

The public millisecond convenience API SHALL accept at least:

```cpp
uint32_t
```

for calls such as:

```cpp
co_await delay(500);
co_await delay_ms(500);
```

The scheduler's **native internal timebase SHALL NOT be milliseconds**. Define an internal monotonic microsecond tick type:

```cpp
namespace simpleawait::detail {
    using tick_t = uint64_t;   // microseconds from platform clock epoch
}
```

All internal absolute deadlines SHALL use `tick_t`. Millisecond delays are converted using checked/widened arithmetic:

```cpp
const tick_t delayUs = static_cast<tick_t>(delayMs) * 1000ULL;
```

V1 SHOULD provide:

```cpp
co_await delay(uint32_t milliseconds);
co_await delay_ms(uint32_t milliseconds);
```

V1 MAY additionally provide:

```cpp
co_await delay_us(uint64_t microseconds);
```

A later optional overload may accept `std::chrono` durations, but `<chrono>` SHALL NOT be required to implement the core scheduler.

With the first-class platform backends, ordinary scheduler deadline comparison may use normal 64-bit monotonic comparisons. Do not retain TinyAwait's `uint32_t` epoch/wrap machinery in the generic scheduler merely for compatibility.

If computing `now_us + duration_us` would overflow `uint64_t`, V1 SHALL invoke the configured error hook with `Error::deadline_overflow`. Silent wrap or saturation is not permitted.

---

# 20. Monotonic clock abstraction

## 20.1 Contract

All scheduler timing SHALL go through one platform clock abstraction. Scheduler code MUST NOT directly call `millis()`, `micros()`, Pico SDK clock functions, or ESP-IDF clock functions outside the clock backend.

Required conceptual interface:

```cpp
namespace simpleawait::detail {

using tick_t = uint64_t;

struct PlatformClock {
    static tick_t now_us() noexcept;
};

}
```

`now_us()` SHALL provide a monotonic microsecond count for the lifetime of the scheduler/runtime epoch. It does not represent wall-clock time and SHALL NOT have timezone/calendar semantics.

## 20.2 RP2040 and RP2350 backend

For Arduino-Pico builds targeting RP2040 or RP2350, use the Pico SDK 64-bit timer API:

```cpp
#include <pico/time.h>

uint64_t PlatformClock::now_us() noexcept {
    return time_us_64();
}
```

The Raspberry Pi SDK documents `time_us_64()` as the full 64-bit hardware timer value that monotonically increases from power-up. This backend is the preferred implementation for both RP2040 and RP2350, including RP2350 Arm and RISC-V builds where exposed by the Arduino-Pico core.

Do not use the 32-bit `time_us_32()` value for scheduler deadlines.

## 20.3 ESP32 backend

For Arduino-ESP32 builds, use ESP-IDF's high-resolution timer clock:

```cpp
#include <esp_timer.h>

uint64_t PlatformClock::now_us() noexcept {
    return static_cast<uint64_t>(esp_timer_get_time());
}
```

`esp_timer_get_time()` provides a 64-bit microsecond count since ESP Timer initialization. Deep sleep starts a new timer epoch; SimpleAwait SHALL treat a deep-sleep wake/reboot as a new scheduler lifetime unless a future persistence feature explicitly defines otherwise.

## 20.4 Host-test backend

Host tests SHALL inject a deterministic 64-bit microsecond clock rather than sleeping the host thread:

```cpp
struct FakeClock {
    static inline uint64_t now = 0;

    static uint64_t now_us() noexcept {
        return now;
    }
};
```

Tests advance `FakeClock::now` explicitly.

## 20.5 Generic Arduino compatibility backend

Other Arduino platforms MAY be supported through a compatibility backend. Such a backend is secondary to RP2040/RP2350/ESP32 support.

If only a 32-bit `micros()` source is available, software MAY extend it to 64 bits. The implementation MUST document the sampling requirement needed to observe every underlying wrap and MUST protect extension state if accessed from more than one execution context.

A generic compatibility backend MUST NOT force first-class platforms back onto a `millis()` or 32-bit timebase.

## 20.6 Hardware alarms are not the scheduler

The V1 scheduler SHALL use platform clocks for time measurement but SHALL NOT create a Pico hardware alarm or ESP Timer object for every coroutine delay. `poll()` remains the portable scheduling mechanism.

Hardware alarm/wakeup integration MAY be added later as an optional low-power optimization that arms only the nearest scheduler deadline. Such an optimization MUST preserve the same public timing semantics and MUST NOT resume user coroutine code directly from an interrupt.

## 20.7 Clock override/testing policy

Prefer a compile-time clock policy or internal platform specialization over a public macro. If an override hook is exposed, it SHALL return `uint64_t` microseconds and be documented as an advanced/testing facility.

Reference APIs:

- Raspberry Pi Pico SDK `time_us_64()`: <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html>
- ESP-IDF `esp_timer_get_time()`: <https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_timer.html>

---

# 21. waitUntil()

Implement:

```cpp
co_await waitUntil(predicate);
```

Example:

```cpp
co_await waitUntil([] {
    return Serial.available() > 0;
});
```

Predicate requirements:

```cpp
bool predicate();
```

The predicate shall be evaluated cooperatively from scheduler context.

Do not poll it from interrupts.

---

# 22. Timed waitUntil()

V1.1 should support:

```cpp
WaitResult result = co_await waitUntil(predicate, timeoutMs);
```

Example:

```cpp
auto result = co_await waitUntil(
    [] { return digitalRead(BUTTON) == LOW; },
    5000
);

if (result == WaitResult::ready) {
    ...
}
```

Define:

```cpp
enum class WaitResult {
    ready,
    timeout
};
```

---

# 23. Event primitive

Implement a scheduler-local manual-reset event:

```cpp
class Event;
```

Minimum API:

```cpp
Event event;

co_await event.wait();

event.set();
event.clear();

bool event.isSet() const;
```

`reset()` MAY be supplied as an alias for `clear()`, but `clear()` is preferred because it matches familiar asyncio semantics.

Semantics:

- Events start cleared.
- `wait()` returns immediately without suspension when already set.
- `wait()` suspends when cleared and adds the current task to the Event's waiter queue.
- `set()` marks the Event set and schedules all current waiters in FIFO order.
- The Event remains set until `clear()`.
- Future `wait()` calls while set complete immediately.

**Important:** ordinary `Event::set()` is scheduler-context only. It is NOT safe to call from an ISR, another core, an RTOS callback, or other concurrent context. Use `ThreadSafeFlag` for that purpose.

---

# 24. ThreadSafeFlag

Implement a separate cross-context primitive inspired by MicroPython `asyncio.ThreadSafeFlag`:

```cpp
class ThreadSafeFlag;
```

Minimum API:

```cpp
ThreadSafeFlag flag;

flag.set();        // may be called from supported external contexts
flag.clear();

co_await flag.wait();
```

Required semantics:

- starts cleared;
- supports at most one waiting task at a time;
- `set()` records the signaled state without directly resuming coroutine code;
- if a task is waiting, the scheduler notices the pending signal and schedules that task;
- `wait()` returns immediately if already set;
- successful `wait()` automatically clears/reset the flag;
- repeated `set()` calls before the waiter consumes the flag coalesce into one signaled state;
- no payload is stored;
- no heap allocation occurs;
- operations callable from ISR/external context must be bounded and must not invoke arbitrary user code.

Typical usage:

```cpp
ThreadSafeFlag dmaDone;

void dmaISR() {
    dmaDone.set();
}

Task<void> transfer() {
    startDMA();
    co_await dmaDone.wait();
    processResult();
}
```

The implementation MAY use target-specific atomics or very short critical sections, but the public semantics must remain common across RP2040, RP2350, and ESP32.

---

# 25. Queue

Implement a statically allocated bounded queue:

```cpp
template<typename T, std::size_t Capacity>
class Queue;
```

Example:

```cpp
Queue<SensorReading, 8> samples;
```

There MUST be no dynamic allocation.

---

# 26. Queue asynchronous receive

Support:

```cpp
T value = co_await queue.receive();
```

If data exists:

- complete immediately.

If queue is empty:

- suspend coroutine;
- resume when data becomes available.

---

# 27. Queue asynchronous send

Support:

```cpp
co_await queue.send(value);
```

If free capacity exists:

- enqueue immediately.

If full:

- suspend sender;
- resume when capacity becomes available.

---

# 28. Queue non-blocking operations

Also expose:

```cpp
bool trySend(const T&);
bool trySend(T&&);

bool tryReceive(T& result);

bool empty() const;
bool full() const;

std::size_t size() const;
constexpr std::size_t capacity() const;
```

---

# 29. Queue ordering

Queue data MUST use FIFO ordering.

Waiting receivers SHOULD resume FIFO.

Waiting senders SHOULD resume FIFO.

Avoid starvation.

---

# 30. Queue storage

Use compile-time fixed storage.

Acceptable options:

```cpp
std::array<T, Capacity>
```

if toolchain/library support is reliable.

Alternatively implement raw aligned storage.

No:

```cpp
std::vector
std::deque
std::list
```

and no heap allocation.

---

# 31. External-context / ISR interaction

The core scheduler MUST NOT resume coroutine handles directly inside an ISR, another core, or an unrelated RTOS callback.

Cross-context interaction shall follow this model:

```text
IRQ / other core / callback
 |
 +--> update bounded fixed-size state
 |
 +--> set ThreadSafeFlag / cross-context primitive
 |
 +--> return immediately

Arduino loop / scheduler context
 |
 +--> poll()
       |
       +--> observe pending notification
       |
       +--> enqueue waiting task
       |
       +--> resume coroutine normally
```

Coroutine bodies always execute in scheduler context.

Ordinary `Event`, `Queue`, and `AsyncLock` SHALL be documented as scheduler-local unless explicitly stated otherwise.

---

# 32. Cross-context signal API

`ThreadSafeFlag` is the required V1 bridge for ISR/thread/core-to-scheduler notification.

Do NOT add `Event::setFromISR()` to V1 merely for convenience. Keeping ordinary Event separate avoids making every Event operation pay for cross-context synchronization and makes safety rules explicit.

`ThreadSafeFlag::set()` MUST:

- be bounded;
- allocate nothing;
- not resume a coroutine directly;
- not call arbitrary user code;
- be safe in the documented target contexts;
- preserve a pending signal until consumed by the waiter.

If a platform requires an explicit "wake scheduler" hook in addition to storing the flag, hide it behind the platform layer.

---

# 33. Cross-context queues

Ordinary:

```cpp
Queue<T,N>
```

is scheduler-local and SHALL NOT claim ISR/core/thread safety.

Do not add `trySendFromISR()` to ordinary Queue in V1.

Where a payload must cross an execution-context boundary, the preferred initial patterns are:

1. store data in fixed/atomic/shared state and signal a `ThreadSafeFlag`; or
2. use a future dedicated primitive such as:

```cpp
ThreadSafeQueue<T,N>
CrossCoreQueue<T,N>
```

A future `ThreadSafeQueue<T,N>` should be fixed-capacity and intentionally narrower than ordinary Queue, with explicit producer/consumer and blocking rules. It must not simply wrap ordinary Queue with broad locks.

---

# 34. Critical sections

Provide an internal platform abstraction:

```cpp
enterCritical();
exitCritical();
```

or RAII:

```cpp
CriticalSection guard;
```

Implement per target as required.

Do not disable interrupts around coroutine execution.

Critical sections shall only protect very short scheduler/ISR metadata operations.

---

# 35. Memory model

A core requirement:

> Normal operation MUST NOT allocate coroutine frames from the global heap.

Maintain a fixed global or scheduler-owned coroutine frame arena.

Default configuration:

```cpp
SIMPLEAWAIT_MAX_TASKS
SIMPLEAWAIT_FRAME_POOL_BYTES
```

Recommended defaults:

```cpp
#define SIMPLEAWAIT_MAX_TASKS 32
#define SIMPLEAWAIT_FRAME_POOL_BYTES 4096
```

---

# 36. Variable-size coroutine frames

Do NOT allocate an identical fixed-sized slot for every coroutine.

Compiler-generated coroutine frames vary significantly.

The arena shall support:

```text
+--------------------------------------+
| task A 96B | task B 240B | task C   |
+--------------------------------------+
```

rather than:

```text
+----------+----------+----------+
| 512B     | 512B     | 512B     |
+----------+----------+----------+
```

Preserve TinyAwait's variable-size frame allocation strategy unless testing reveals a correctness issue.

---

# 37. No heap fallback

If arena allocation fails:

DO NOT:

```cpp
malloc(...)
new ...
```

as fallback.

Instead invoke an error hook.

---

# 38. Error handling

Define:

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

Provide compile-time override:

```cpp
#define SIMPLEAWAIT_ON_ERROR(error) ...
```

Default behavior may be:

```cpp
abort();
```

or embedded-safe infinite halt.

Development mode MAY output diagnostic information if Serial is initialized, but core library MUST NOT depend on Serial.

---

# 39. Exceptions

Embedded builds should work correctly with:

```text
-fno-exceptions
```

If coroutine `unhandled_exception()` exists because the C++ coroutine contract requires it, route to:

```cpp
SIMPLEAWAIT_ON_ERROR(Error::unhandled_exception)
```

Do not implement exception propagation between tasks in V1.

---

# 40. Cancellation

Cancellation is NOT required for V1, but internal data structures MUST make it implementable without redesigning every wait primitive.

The key requirement is that a suspended scheduled task knows what wait structure currently owns it, so cancellation can unlink it safely.

Future API may provide:

```cpp
taskHandle.cancel();
```

and/or:

```cpp
CancellationToken
```

Unlike Python, C++ builds may run with exceptions disabled. Therefore do not assume cancellation will be implemented by injecting an exception. A future cancellation design should use explicit task state/cancellation points and deterministic cleanup semantics suitable for `-fno-exceptions`.

Timeout and TaskGroup design SHALL take future cancellation into account.

---

# 41. Typed Tasks

Target post-MVP API:

```cpp
Task<int> readSensor() {
    co_return analogRead(A0);
}

Task process() {
    int result = co_await readSensor();
}
```

Requirements when implemented:

- value stored in coroutine frame;
- returned without heap allocation;
- move-only types supported where practical;
- `void` specialization supported;
- no dependence on exceptions.

---

# 42. Structured concurrency, TaskGroup, and when_all

Post-MVP, prefer structured task ownership for groups of related concurrent tasks.

Target API concept:

```cpp
TaskGroup group;

group.create_task(taskA());
group.create_task(taskB());
group.create_task(taskC());

co_await group.join();
```

`TaskGroup` SHALL:

- own the logical lifetime of children;
- wait for all children before `join()` completes;
- provide deterministic behavior if one child fails/cancels once those facilities exist;
- avoid heap allocation by using task metadata already owned by the scheduler or a statically bounded group representation.

A convenience:

```cpp
co_await when_all(taskA(), taskB(), taskC());
```

may be layered on top of TaskGroup-like machinery.

Do not add special-case scheduler logic solely for `when_all`; implement it as library-level composition where practical.

---

# 43. when_any

Post-MVP:

```cpp
auto result = co_await when_any(
    buttonPressed(),
    delay(5000)
);
```

Return enough information to identify the winner.

Example:

```cpp
if (result.index == 0) {
    ...
}
```

Do not require `std::variant` if it substantially increases binary size.

---

# 44. Timeout composition

Higher-level timeouts should be library composition built from tasks, timers, and cancellation rather than special scheduler opcodes.

Preferred eventual API:

```cpp
auto result = co_await timeout(queue.receive(), 5000);
```

or:

```cpp
auto result = co_await timeout(event.wait(), 5000);
```

V1 may retain a simple timeout-specific helper for `waitUntil` if that reduces complexity, but the architecture SHALL allow generic timeout composition later.

When cancellation is implemented, `timeout()` should cancel or otherwise terminate the losing operation cleanly rather than leaving an orphaned waiter behind.

---

# 45. Multicore policy

Each scheduler instance belongs to exactly one execution context/core.

Coroutines SHALL NOT migrate between cores.

For RP2040/RP2350:

```cpp
Scheduler scheduler0;
Scheduler scheduler1;

void loop() {
    scheduler0.poll();
}

void loop1() {
    scheduler1.poll();
}
```

Each scheduler owns its own:

- ready queue;
- timer state;
- task metadata.

Coroutine handles created under one scheduler must remain there.

---

# 46. Cross-core communication

Do not make ordinary:

```cpp
Queue<T,N>
Event
```

automatically multicore-safe.

Simple notifications from another core SHOULD be supported through `ThreadSafeFlag` where target atomics/critical-section semantics permit it.

Payload-bearing cross-core communication shall use a separate future primitive:

```cpp
CrossCoreQueue<T,N>
```

Architecture:

```text
Core 0                      Core 1

Scheduler                   Scheduler
   |                           |
Task A                        Task D
Task B                        Task E
   |                           ^
   +---- CrossCoreQueue -------+
```

Platform-specific implementation may use:

- RP2040/RP2350 hardware spinlocks/FIFO/atomics;
- ESP32 FreeRTOS or atomic primitives internally.

These mechanisms shall remain hidden from ordinary Task/Event/Queue APIs.

---

# 47. ESP32 policy

ESP32's underlying FreeRTOS implementation shall be treated as a platform detail.

A `Task` is NOT a FreeRTOS task.

The following:

```cpp
spawn(foo());
```

shall create a coroutine, not:

```cpp
xTaskCreate(...)
```

The default scheduler runs from normal Arduino `loop()` context.

---

# 48. Arduino integration

Normal setup:

```cpp
void setup() {
    spawn(application());
}

void loop() {
    poll();
}
```

Do not require library initialization unless necessary.

If initialization is required, prefer:

```cpp
SimpleAwait.begin();
```

but zero-configuration initialization is preferred.

---

# 49. Header organization

Recommended repository:

```text
SimpleAwait/
│
├── src/
│   ├── SimpleAwait.h
│   │
│   └── simpleawait/
│       ├── config.h
│       ├── task.h
│       ├── task_handle.h
│       ├── scheduler.h
│       ├── frame_pool.h
│       ├── delay.h
│       ├── yield.h
│       ├── wait_until.h
│       ├── wait_queue.h
│       ├── event.h
│       ├── thread_safe_flag.h
│       ├── queue.h
│       ├── async_lock.h          # V1.1
│       ├── critical_section.h
│       ├── platform_clock.h
│       └── detail/
│           ├── intrusive_queue.h
│           ├── timer_list.h
│           └── coroutine_traits.h
│
├── examples/
│   ├── Blink/
│   ├── TwoBlinkers/
│   ├── CreateTask/
│   ├── ParentChild/
│   ├── YieldFairness/
│   ├── WaitUntil/
│   ├── Events/
│   ├── ThreadSafeFlagIRQ/
│   ├── QueueProducerConsumer/
│   └── AsyncLock/               # V1.1
│
├── test/
│
├── hardware/
│   ├── rp2040/
│   ├── rp2350/
│   └── esp32/
│
├── library.properties
├── library.json
├── README.md
├── LICENSE
└── CHANGELOG.md
```

---

# 50. Header-only preference

Prefer header-only implementation unless separation materially improves:

- generated code size;
- compilation time;
- platform integration.

---

# 51. Intrusive scheduler data structures and WaitQueue

Prefer intrusive structures where practical.

Coroutine/task metadata should contain the linkage needed for ready and wait structures, avoiding separately allocated scheduler nodes.

Provide an internal FIFO `WaitQueue` abstraction used by Event, Queue, AsyncLock, Task completion waiters, and future cancellation logic.

Conceptual shape:

```cpp
class WaitQueue {
public:
    void push(TaskControl* task);
    TaskControl* pop();
    void remove(TaskControl* task);   // needed for future cancellation
    bool empty() const;
};
```

A suspended task should know which wait structure currently owns it:

```cpp
struct TaskControl {
    std::coroutine_handle<> handle;

    TaskState state;
    WaitQueue* waitQueue;

    TaskControl* next;
    TaskControl* prev;      // optional; useful if O(1) cancellation unlink is required

    uint32_t deadline;
};
```

Do not require every task to carry unnecessary pointers if profiling shows the RAM cost is too high. An O(n) bounded removal from a singly linked intrusive list is acceptable for small configured task counts if it substantially reduces per-task RAM.

Timer storage may remain separate from the ready/wait intrusive structures; do not copy MicroPython's pairing heap unless measurement proves it superior for the configured Arduino workloads.

---

# 52. Scheduler states

Recommended internal task state:

```cpp
enum class TaskState : uint8_t {
    created,
    ready,
    running,
    waitingTimer,
    waitingEvent,
    waitingFlag,
    waitingQueueSend,
    waitingQueueReceive,
    waitingLock,
    waitingChild,
    completed,
    cancelled
};
```

`cancelled` may be reserved until cancellation is implemented.

Debug builds should assert illegal state transitions.

At any instant a task may be present in at most one scheduler wait structure, excluding separately maintained timer metadata used solely for timeout composition if carefully designed.

---

# 53. No reentrant polling

Calling:

```cpp
poll()
```

from inside a currently executing `poll()` shall be prohibited.

Debug builds:

```cpp
SIMPLEAWAIT_ON_ERROR(Error::scheduler_reentry);
```

Document:

```cpp
Task foo() {
    // DON'T call poll() here
}
```

---

# 54. Coroutine resume safety

Implement awaiters carefully to avoid:

- double resume;
- destroying a running coroutine;
- dangling continuation;
- recursive unbounded coroutine chains.

Prefer scheduling continuations through scheduler state where possible.

V1 SHALL NOT use symmetric transfer or inline continuation. Child completion enqueues the parent at the ready FIFO tail for a later `poll()` pass. This keeps all user coroutine resumes on one scheduler path.

A future version may evaluate symmetric transfer only after V1 lifetime and fairness behavior is stable and fully tested.

---

# 55. Lifetime rules

Every coroutine frame shall have exactly one frame owner.

Possible owners:

```text
unscheduled Task object
OR
Scheduler (scheduled/detached task)
OR
Parent/structured task relationship represented through Scheduler metadata
```

Never two frame owners.

Unscheduled lazy Task:

```text
Task object
    |
    +-- owns frame until await/create_task/spawn/destruction
```

Scheduled task:

```text
Task temporary
    |
 create_task()
    |
 Scheduler owns frame
    |
 TaskHandle observes task metadata
```

Detached task:

```text
Task temporary
    |
 spawn()
    |
 Scheduler owns frame until completion
```

Awaited child:

```text
Parent task
 |
 starts child
 |
 parent waits
 |
 child completes
 |
 destroy/release child exactly once
 |
 parent becomes ready
```

`TaskHandle` MUST NOT become a second coroutine-frame owner.

---

# 56. Destroying unfinished Task objects

An unfinished Task that has NOT been spawned and is destroyed shall safely destroy its coroutine frame.

An unfinished Task owned by the scheduler shall not be destroyed by user-facing temporary objects.

Move operations MUST transfer ownership.

Copy operations SHOULD be deleted:

```cpp
Task(const Task&) = delete;
Task& operator=(const Task&) = delete;
```

---

# 57. Configuration

Supported configuration macros:

```cpp
SIMPLEAWAIT_MAX_TASKS
SIMPLEAWAIT_FRAME_POOL_BYTES
SIMPLEAWAIT_CLOCK_NOW_US()   // optional advanced/testing override
SIMPLEAWAIT_ON_ERROR(error)
SIMPLEAWAIT_ENABLE_DIAGNOSTICS
SIMPLEAWAIT_ENABLE_ISR
```

Keep configuration surface intentionally small.

---

# 58. Multiple translation units

Configuration MUST behave consistently across all translation units.

Preferred usage:

```cpp
// AwaitConfig.h

#define SIMPLEAWAIT_MAX_TASKS 16
#define SIMPLEAWAIT_FRAME_POOL_BYTES 3072

#include <SimpleAwait.h>
```

Use C++17/20 inline variables where suitable to avoid duplicate scheduler state.

---

# 59. Diagnostics

Optional diagnostics API:

```cpp
struct Stats {
    size_t activeTasks;
    size_t peakTasks;

    size_t frameBytesUsed;
    size_t peakFrameBytesUsed;
    size_t frameBytesFree;

    size_t readyTasks;
    size_t timers;
    size_t waitingTasks;

    size_t allocationFailures;
};
```

Expose:

```cpp
Stats stats();
TaskHandle current_task();
```

Debug/diagnostic builds MAY expose a bounded numeric `TaskId` to improve logs without requiring strings or heap allocation.

Diagnostics MUST NOT allocate memory.

---

# 60. Memory introspection

Provide:

```cpp
constexpr size_t framePoolBytes();
size_t frameBytesUsed();
size_t frameBytesFree();
size_t activeTaskCount();
```

This is especially important for embedded debugging.

---

# 61. Performance requirements

Steady-state:

```cpp
poll();
```

when:

- no ready task exists;
- no cross-context signal is pending;
- timers exist;
- nearest timer has not expired;

should be O(1) where practical.

Preserve TinyAwait's cached-nearest-deadline style optimization unless a different implementation benchmarks better without materially increasing per-task RAM.

Ready-queue enqueue/dequeue MUST be O(1).

Do not adopt MicroPython's pairing-heap task queue merely for conceptual similarity; SimpleAwait's expected small fixed task counts and fixed-memory goal favor simpler timer metadata unless measurement shows otherwise.

---

# 62. Scheduler complexity priorities

Prioritize:

```text
idle poll                    O(1)
ready enqueue                O(1)
ready dequeue                O(1)
yield / delay(0)             O(1)
ThreadSafeFlag set           O(1)
ThreadSafeFlag consume       O(1)
event set                    O(number awakened)
queue try operations         O(1)
WaitQueue enqueue/dequeue    O(1)
```

Cancellation unlink MAY initially be O(n) because configured task counts are small and bounded.

Timer insertion may initially be O(n). Do not introduce a dynamic heap/priority queue unless benchmarks demonstrate a real need.

---

# 63. Arduino blocking API caveat

Documentation MUST explain that cooperative coroutines cannot compensate for blocking operations.

For example:

```cpp
Task bad() {
    while (true) {
        doSomethingForThreeSeconds(); // blocks every task
        co_await yield();
    }
}
```

A task runs until it:

- returns;
- reaches `co_await`;
- otherwise yields CPU through an awaitable.

---

# 64. Example: two concurrent blinkers

Required example:

```cpp
Task blink(int pin, uint32_t period) {
    pinMode(pin, OUTPUT);

    while (true) {
        digitalWrite(pin, HIGH);
        co_await delay(period);

        digitalWrite(pin, LOW);
        co_await delay(period);
    }
}

void setup() {
    spawn(blink(LED1, 100));
    spawn(blink(LED2, 350));
}

void loop() {
    poll();
}
```

---

# 65. Example: parent/child

Required:

```cpp
Task startupAnimation() {
    co_await delay(100);
    co_await delay(100);
}

Task application() {
    co_await startupAnimation();

    // child completed
    ...
}
```

---

# 66. Example: producer/consumer

Required:

```cpp
Queue<int, 8> values;

Task producer() {
    while (true) {
        int value = analogRead(A0);

        co_await values.send(value);
        co_await delay(10);
    }
}

Task consumer() {
    while (true) {
        int value = co_await values.receive();

        Serial.println(value);
    }
}
```

---

# 67. Example: event

Required:

```cpp
Event connected;

Task networkTask() {
    ...

    connected.set();
}

Task uiTask() {
    co_await connected.wait();

    showConnected();
}
```

---

# 68. Example: waitUntil

Required:

```cpp
Task waitForSerial() {
    co_await waitUntil([] {
        return Serial.available() > 0;
    });

    int value = Serial.read();
}
```

---

# 69. Test strategy

Testing MUST be layered.

## Host deterministic tests

Build the library against an injected 64-bit microsecond fake clock.

Example:

```cpp
uint64_t fakeTimeUs;

uint64_t testClockUs() {
    return fakeTimeUs;
}
```

Tests manually advance time without sleeping the host thread.

Do not require sleeping host threads.

---

# 70. Required V1 tests

Implement deterministic tests for all of the following.

## Basic Task/lifetime

- coroutine creation is lazy;
- an unscheduled Task does not execute;
- unscheduled Task destruction releases its frame;
- `co_await task` starts/awaits correctly;
- `create_task()` schedules without implicit detachment;
- `spawn()` explicitly detaches;
- completion releases scheduler-owned frames;
- move semantics transfer ownership exactly once;
- `TaskHandle` does not own/double-destroy a frame.

## current_task()

- invalid outside coroutine execution;
- identifies the currently running task;
- changes correctly between tasks;
- cleared after suspension/completion.

## Delay and fairness

- delay 0 always suspends;
- delay 0 behaves as a fair yield point;
- `yield()` behaves equivalently for scheduling fairness;
- delay 1;
- several simultaneous timers;
- timers completing in different order;
- same-deadline timers;
- continuously yielding task does not starve another ready task.

## Clock/deadline boundaries

For the native 64-bit scheduler clock, test deadlines near `UINT64_MAX` using an injected fake clock to ensure checked duration conversion/deadline handling cannot overflow silently. If the chosen deadline policy saturates or rejects impossible delays, verify that behavior explicitly.

Separately test the **generic 32-bit compatibility clock extender** across an underlying `micros()` wrap (for example `0xFFFFFFF0` through `0x00000020`). First-class RP2040/RP2350/ESP32 scheduler tests SHALL use the 64-bit microsecond clock model.

## Parent/child

- parent awaits child;
- parent remains suspended;
- child completes;
- child frame destroyed exactly once;
- parent resumes exactly once.

## Event

- wait on cleared Event;
- set wakes all current waiters;
- set Event stays set;
- future wait returns immediately while set;
- clear causes later wait to suspend again;
- FIFO wake scheduling;
- Event rejects/does not claim cross-context safety.

## ThreadSafeFlag

- starts cleared;
- waiter suspends;
- set wakes waiter through scheduler, not inline;
- set-before-wait returns immediately;
- wait auto-clears;
- repeated set calls coalesce;
- second simultaneous waiter fails deterministically;
- simulated ISR/external set during scheduler metadata operations does not corrupt state.

## Queue

- send/receive;
- queue full;
- queue empty;
- waiting sender;
- waiting receiver;
- FIFO data ordering;
- FIFO waiter ordering;
- ring-index wraparound;
- Queue remains scheduler-local.

## Frame allocator

- many small tasks;
- mixed frame sizes;
- LIFO destruction;
- non-LIFO destruction;
- fragmentation;
- full pool recovery.

## Capacity

With capacity N:

```text
N live tasks succeeds
N+1 fails deterministically
```

## Scheduler

- empty scheduler;
- ready FIFO behavior;
- reentrant poll detection;
- task completes during poll;
- child completes during poll;
- pending cross-context flag handled on next scheduler opportunity.

---

# 71. Heap detection testing

Host test harness SHALL override or track:

```cpp
operator new
operator delete
malloc
free
```

After library initialization, normal Task scheduling should require zero global heap allocation.

Alternatively link with allocation instrumentation.

Any unexpected allocation shall fail the test.

---

# 72. Sanitizer testing

Host CI SHALL execute using:

- AddressSanitizer;
- UndefinedBehaviorSanitizer.

Test:

```text
-O0
-O2
-Os
```

at minimum with GCC.

Where practical also test Clang.

---

# 73. Hardware testing

Required hardware matrix:

```text
RP2040
RP2350 ARM
ESP32-S3
```

Desired:

```text
RP2350 RISC-V
ESP32-C3
ESP32-C6
```

Each target shall run a common serial test suite.

---

# 74. Hardware stress test

Create firmware that repeatedly:

- creates child coroutines;
- starts detached tasks;
- waits timers;
- sends/receives queues;
- triggers events;
- destroys tasks.

Minimum target:

```text
100,000+ coroutine completions
```

Success criteria:

```text
activeTasks == 0
frameBytesUsed == 0
unexpectedErrors == 0
```

after each complete stress cycle.

---

# 75. Long-running test

Run a mixed workload for at least several minutes.

Track:

- task completions;
- peak frame usage;
- free memory if platform allows;
- resets;
- watchdog events;
- panics;
- allocation failures.

There shall be no progressive loss of frame-pool capacity.

---

# 76. Build systems

Required:

## Arduino library

Provide:

```text
library.properties
```

## PlatformIO

Provide:

```text
library.json
```

## Host tests

CMake permitted exclusively for tests and development.

Desktop support is not a product requirement.

---

# 77. CI

GitHub Actions should:

1. build host tests GCC;
2. build host tests Clang;
3. run unit tests;
4. run ASan;
5. run UBSan;
6. compile RP2040 example;
7. compile RP2350 example;
8. compile ESP32 example;
9. validate Arduino library metadata;
10. optionally report library size.

---

# 78. Code style

Prefer straightforward embedded C++.

Do:

```cpp
if (condition) {
    ...
}
```

Avoid unnecessary:

- concepts;
- template metaprogramming;
- type erasure;
- virtual inheritance;
- runtime polymorphism;
- STL abstraction layers.

Standard C++ coroutines are already the sophisticated mechanism. C++20 is the compatibility floor; everything around the coroutine mechanism should be boring and should not require C++23+ features.

---

# 79. Documentation philosophy

Every primitive should answer four questions:

```text
What does it do?
When does it suspend?
When does it resume?
Who owns the coroutine while suspended?
```

For ISR-capable primitives additionally:

```text
Can this function be called from ISR context?
```

---

# 80. MVP implementation stages

Implement in this order.

## Phase 1 — Preserve and isolate TinyAwait low-level core

Implement/adapt:

- variable-size coroutine frame arena;
- coroutine allocation/deallocation;
- timer scheduling;
- 64-bit microsecond platform clock abstraction;
- overflow-safe 64-bit deadlines;
- deterministic error hook;
- host fake clock.

Do not yet expose TinyAwait's implicit detached execution as the SimpleAwait model.

Acceptance: allocator/timer regression tests pass unchanged or with equivalent coverage.

## Phase 2 — Lazy Task, Scheduler ready FIFO, create_task/spawn

Implement:

```cpp
Task<void>
TaskHandle
create_task()
spawn()
current_task()
poll()
```

Requirements:

- Task creation is lazy;
- `create_task()` enqueues to ready FIFO;
- `spawn()` explicitly detaches;
- current task is tracked during resume;
- scheduler owns scheduled frames.

Acceptance:

```cpp
auto h = create_task(blink());
spawn(statusLed());
```

work with no implicit Task detachment.

## Phase 3 — delay and fair yield

Implement:

```cpp
co_await delay(ms);
co_await yield();
```

Required:

```cpp
co_await delay(0);
```

always suspends and has the same fairness semantics as `yield()`.

Acceptance: two always-ready tasks alternate without starvation.

## Phase 4 — WaitQueue and Event

Implement internal FIFO `WaitQueue` plus scheduler-local manual-reset:

```cpp
Event
```

Acceptance: multiple Event waiters wake in deterministic FIFO scheduling order, and future waiters complete immediately until `clear()`.

## Phase 5 — ThreadSafeFlag

Implement:

```cpp
ThreadSafeFlag
```

Acceptance:

- a simulated/hardware IRQ can call `set()`;
- coroutine code is never resumed in IRQ context;
- one waiter wakes in scheduler context;
- auto-reset semantics work;
- repeated sets coalesce.

## Phase 6 — Queue

Implement:

```cpp
Queue<T,N>
```

Acceptance: producer/consumer runs indefinitely without heap usage; full/empty suspension and FIFO ordering are correct.

## Phase 7 — waitUntil

Implement:

```cpp
waitUntil(predicate)
```

Acceptance: Serial/GPIO predicate polling works without blocking other coroutines and obeys scheduler fairness.

## Phase 8 — Hardware/ISR validation

Validate ThreadSafeFlag and scheduler metadata protection on:

- RP2040;
- RP2350;
- ESP32-S3.

Use GPIO or timer IRQ tests. Do not add Event ISR methods just to make tests easier.

## Phase 9 — V1.1 extensions

After V1 is stable, add in this approximate order:

1. `Task<T>`;
2. `AsyncLock`;
3. cancellation;
4. generic `timeout()`;
5. `TaskGroup` / `when_all`;
6. `when_any`;
7. `ThreadSafeQueue` / `CrossCoreQueue` only with concrete use cases.

---

# 81. Features explicitly deferred from V1

Do not implement until the V1 core is stable:

```text
Task<T>
Task cancellation
CancellationToken
AsyncLock
TaskGroup
when_all
when_any
generic timeout()
Semaphore
ThreadSafeQueue
CrossCoreQueue
FreeRTOS executor integration
USB awaitables
Serial awaitables
Wire/I2C awaitables
SPI awaitables
GPIO-edge awaitables
power-aware idle
```

These shall not complicate the MVP scheduler.

`ThreadSafeFlag` is **not** deferred; it is part of V1 because safe peripheral/IRQ-to-coroutine signaling is foundational on the target MCUs.

---

# 82. AsyncLock policy

Do NOT add `AsyncLock` to the initial MVP, but reserve it for V1.1 because it solves an important cooperative-concurrency case: ownership spanning suspension points.

Within one cooperative scheduler:

```cpp
x++;
```

cannot normally be interrupted by another coroutine unless execution suspends.

Therefore ordinary short shared-state changes generally do not need an async mutex.

An `AsyncLock` is useful when ownership must remain exclusive across `co_await`, for example an asynchronous I²C transaction:

```cpp
auto guard = co_await i2cLock.acquire();
startTransfer();
co_await i2cDone.wait();
// guard releases here
```

Target semantics should follow the useful MicroPython Lock rule:

- if unlocked, `acquire()` obtains it immediately;
- otherwise the task joins a FIFO waiter queue;
- `release()` transfers logical ownership to the next waiter before that waiter runs, avoiding a race where a later task steals the lock;
- no dynamic allocation.

Do not use `AsyncLock` as the solution for ISR synchronization. IRQ/core communication belongs in `ThreadSafeFlag`, `ThreadSafeQueue`, or platform critical sections.

---

# 83. API design rule

Every operation that may suspend MUST visibly use:

```cpp
co_await
```

Avoid functions whose names conceal scheduling behavior.

Good:

```cpp
auto x = co_await queue.receive();
```

Bad:

```cpp
auto x = queue.receiveBlocking();
```

---

# 84. Desired final programming model

The library should make ordinary cooperative application code natural:

```cpp
Queue<SensorReading, 8> readings;
Event displayReady;
ThreadSafeFlag dmaDone;

Task<void> readSensors() {
    while (true) {
        SensorReading r = sampleSensors();
        co_await readings.send(r);
        co_await delay(10);
    }
}

Task<void> display() {
    initializeDisplay();
    displayReady.set();

    while (true) {
        auto reading = co_await readings.receive();
        draw(reading);
        co_await yield();
    }
}

Task<void> dmaWorker() {
    while (true) {
        startDMA();
        co_await dmaDone.wait();
        consumeDMA();
    }
}

Task<void> application() {
    auto sensorTask = create_task(readSensors());
    auto displayTask = create_task(display());
    auto dmaTask = create_task(dmaWorker());

    co_await displayReady.wait();

    Serial.println("System ready");

    while (true) {
        serviceApplication();
        co_await delay(100);
    }
}

void dmaISR() {
    dmaDone.set();
}

void setup() {
    Serial.begin(115200);
    spawn(application());
}

void loop() {
    simpleawait::poll();
}
```

The programming model should communicate intent clearly:

```cpp
co_await operation();       // sequential dependency
create_task(operation());   // concurrent scheduled task
spawn(operation());         // explicitly detached task
co_await delay(0);          // fair cooperative yield
```

No callback pyramid.

No per-feature manual state machine.

No RTOS task required.

No per-coroutine task stack.

No dynamic coroutine-frame allocation.

No direct coroutine execution from IRQ context.

---

# 85. Definition of done for V1

V1 is complete when all of the following are true:

- RP2040 builds and runs;
- RP2350 builds and runs;
- ESP32-S3 builds and runs;
- native standard coroutines are used and the library builds in C++20 mode (minimum language level);
- global heap is not used for coroutine frames;
- fixed frame pool works;
- variable-size coroutine frames work;
- `Task<void>` is lazy and move-only;
- `TaskHandle` works without owning the coroutine frame;
- `create_task()` explicitly schedules work;
- `spawn()` explicitly detaches work;
- discarded Task-producing calls do not silently become background tasks;
- `current_task()` works;
- parent/child `co_await` works;
- ready tasks use FIFO scheduling;
- `delay()` works;
- `delay(0)` always yields fairly;
- `yield()` works;
- `Event` works and is scheduler-local;
- `ThreadSafeFlag` works and can safely signal scheduler work from supported IRQ/external contexts;
- `Queue<T,N>` works and is scheduler-local;
- `waitUntil()` works;
- RP2040/RP2350/ESP32 timers use the 64-bit microsecond platform clock;
- the optional generic 32-bit compatibility clock extender passes rollover tests;
- task-limit exhaustion fails deterministically;
- frame-pool exhaustion fails deterministically;
- no coroutine resumes twice;
- no coroutine frame is destroyed twice;
- no completed coroutine leaks;
- no scheduler-local waiter remains linked after its task completes;
- host sanitizer tests pass;
- hardware IRQ signaling stress tests pass;
- hardware task/frame stress tests pass;
- Arduino examples compile;
- PlatformIO examples compile;
- documentation clearly distinguishes cooperative scheduling, blocking calls, scheduler-local primitives, and cross-context primitives.

---

# 86. AI coding-agent constraints

The implementing agent MUST follow these rules:

1. Preserve/adapt TinyAwait's proven fixed-memory allocator and compact scheduler ideas where possible, but replace its `uint32_t`/`millis()` timebase with the specified 64-bit microsecond platform clock on first-class targets.
2. Use MicroPython `asyncio` as a behavioral reference, not as a memory-management implementation to copy.
3. Make Task creation lazy by default.
4. Require explicit `create_task()` or `spawn()` for concurrent scheduling.
5. Make `delay(0)` and `yield()` real fairness points.
6. Use FIFO ordering for the ready queue and scheduler-local waiter queues unless a requirement says otherwise.
7. Keep ordinary `Event` and `Queue` scheduler-local.
8. Use `ThreadSafeFlag` as the V1 external-context signaling primitive.
9. Never resume coroutine user code directly from an ISR or other core.
10. Do not rewrite working allocator/timer logic merely for stylistic reasons.
11. Add one subsystem per change.
12. Add tests before or alongside every subsystem.
13. Do not add platform abstraction unless required by RP2040, RP2350, ESP32, or deterministic host tests.
14. Do not use heap allocation to simplify implementation.
15. Do not introduce FreeRTOS into the generic Task model.
16. Do not introduce desktop support except deterministic host testing.
17. Do not introduce Boost or other runtime dependencies.
18. Prefer compile-time-sized and intrusive data structures.
19. Keep coroutine ownership explicit and singular.
20. Make waiter ownership explicit enough to support future cancellation.
21. Treat double resume, double destroy, dangling waiters, and lifetime bugs as critical correctness failures.
22. Maintain the 64-bit monotonic microsecond clock contract on first-class targets and isolate any 32-bit wrap extension to the generic compatibility backend.
23. Run host tests after every architectural change.
24. Run target compile tests after every public API change.
25. Preserve examples as executable API documentation.
26. Do not implement `Future`, callback-handle, executor, or thread-pool abstractions unless a concrete target requirement appears.
27. Avoid expanding scope without an explicit requirement.
28. If implementation simplicity conflicts with API cleverness, choose simplicity.

---

# 87. Recommended first coding-agent assignment

The first implementation prompt should be:

> Fork or adapt the current TinyAwait implementation into an Arduino-focused library named SimpleAwait. Preserve its heap-free variable-sized coroutine-frame allocator and compact scheduler/lifetime ideas, but replace its `uint32_t`/`millis()` timebase with SimpleAwait's `uint64_t` monotonic microsecond clock abstraction. Use `time_us_64()` on RP2040/RP2350, `esp_timer_get_time()` on ESP32, and an injected fake microsecond clock for host tests.
>
> Replace TinyAwait's documented implicit/eager detached coroutine model with a lazy, move-only `Task<void>` model. Calling a Task-producing coroutine must create an unscheduled Task. Implement explicit `create_task(Task<void>&&)`, `spawn(Task<void>&&)`, `TaskHandle`, `current_task()`, and a FIFO ready queue.
>
> `create_task()` must transfer scheduling/lifetime ownership to the scheduler and enqueue the Task without heap allocation. `spawn()` must explicitly detach. A discarded unscheduled Task must release its frame rather than silently run in the background.
>
> Implement `delay(uint32_t)` and `yield()`. `delay(0)` MUST always suspend and behave as a fair scheduler yield, placing the task behind work already ready.
>
> Preserve child `co_await` behavior so a parent may await another Task and resume exactly once after the child completes.
>
> Do not yet implement Event, ThreadSafeFlag, Queue, cancellation, typed Task results, AsyncLock, TaskGroup, multicore queues, or FreeRTOS task integration.
>
> Add deterministic host tests covering lazy creation, ownership, move semantics, create_task scheduling, spawn detachment, current_task, parent/child awaiting, FIFO fairness, 64-bit-microsecond delay scheduling, delay(0), yield, deadline-overflow policy, frame-pool exhaustion, task-count exhaustion, optional generic 32-bit clock-extension rollover, and complete frame-pool recovery.
>
> Preserve zero heap allocation for coroutine frames and scheduler metadata and do not add dynamic STL containers.
>
> Finish only when host GCC/Clang sanitizer tests pass and Blink/TwoBlinkers/YieldFairness examples compile for ESP32, RP2040, and RP2350.

---

# 88. Recommended second coding-agent assignment

After the first assignment passes:

> Extend SimpleAwait with an intrusive FIFO WaitQueue, scheduler-local manual-reset Event, single-waiter auto-reset ThreadSafeFlag, and statically allocated Queue<T,N>.
>
> Event must support `wait()`, `set()`, `clear()`, and `isSet()`. Event is scheduler-context only. Multiple waiters must be supported; `set()` schedules all current waiters in FIFO order and leaves the Event set until clear(). Do not add Event::setFromISR().
>
> ThreadSafeFlag must support `set()`, `clear()`, and `wait()`. It is the V1 bridge from IRQ/thread/core/callback contexts into the coroutine scheduler. It permits one waiter, coalesces repeated sets, auto-clears when consumed, allocates nothing, and must never resume coroutine code directly from the external context.
>
> Queue<T,N> must provide `send()`, `receive()`, `trySend()`, `tryReceive()`, `size()`, `empty()`, `full()`, and `capacity()`. `send()` suspends when full and `receive()` suspends when empty. Data and waiter ordering are FIFO. Ordinary Queue is scheduler-local; do not add ISR methods to it.
>
> The internal WaitQueue representation must make future cancellation unlink possible without redesigning Event, Queue, Lock, and Task waits.
>
> Add exhaustive deterministic tests for Event state/waiters, ThreadSafeFlag set-before-wait/set-while-waiting/repeated-set/single-waiter constraints, Queue empty/full/index-wrap/suspended senders and receivers/FIFO order, task destruction, dangling-waiter prevention, and frame-pool recovery.
>
> Add at least one RP2040/RP2350/ESP32 hardware example where a GPIO or hardware timer IRQ sets a ThreadSafeFlag and a coroutine resumes later in scheduler context.

---

# 89. Architectural principle

When implementation choices are ambiguous, use this rule:

> **SimpleAwait is a statically allocated, C++20-minimum, Arduino-native equivalent in spirit to MicroPython asyncio — not a tiny RTOS.**

Use:

- MicroPython `asyncio` for the cooperative task/event-loop behavioral model;
- TinyAwait for fixed-memory C++ coroutine allocation and compact scheduler/timer data-structure ideas;
- selected `s_task` ideas for bounded embedded synchronization;
- CPython `asyncio` only for higher-level structured-concurrency ideas where they remain appropriate on MCUs.

The library exists to make Arduino state-machine code readable while retaining deterministic embedded memory behavior.

It should feel like:

```cpp
co_await operation();
create_task(worker());
```

not like:

```cpp
xTaskCreate(...);
```

and not like:

```cpp
std::thread(...);
```

The default implementation SHALL remain cooperative, fixed-memory, event-loop driven, and explicit about boundaries between scheduler context and external execution contexts.

---

# 90. Asyncio-inspired behavior mapping

The following table is normative guidance for implementation behavior, not a requirement to duplicate Python naming exactly.

| Python / MicroPython concept | SimpleAwait concept | Notes |
| --- | --- | --- |
| coroutine object | `Task<T>` | Lazy/unscheduled until awaited or scheduled |
| `asyncio.create_task()` | `create_task()` | Explicit concurrent scheduling |
| `asyncio.current_task()` | `current_task()` | Lightweight current-task reference |
| `asyncio.sleep_ms(n)` | `delay(n)` | Millisecond native API |
| `asyncio.sleep_ms(0)` | `delay(0)` / `yield()` | Must suspend and yield fairly |
| `asyncio.Event` | `Event` | Manual-reset, scheduler-local |
| `asyncio.ThreadSafeFlag` | `ThreadSafeFlag` | Single waiter, auto-reset, external-context signal |
| `asyncio.Lock` | `AsyncLock` | V1.1; protects ownership across suspension |
| Task await | `co_await Task` / scheduled Task handle later | Wait for completion |
| `asyncio.wait_for` | `timeout()` | Post-MVP composition + cancellation |
| `asyncio.gather` | `when_all()` | Prefer TaskGroup machinery underneath |
| `asyncio.TaskGroup` | `TaskGroup` | Post-MVP structured concurrency |
| Future/callback APIs | intentionally omitted | Not required for target use cases |

Key difference from Python implementations:

> SimpleAwait must achieve these semantics without a garbage-collected heap, using fixed-capacity scheduler metadata and a fixed coroutine-frame pool.


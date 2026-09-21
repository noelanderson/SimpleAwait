# ArduinoAwait V1 Public API Contract

**Status:** Frozen public surface for V1 code generation  
**Language floor:** C++20  
**Namespace:** `arduinoawait`

This file freezes the intended V1 API shape. Implementation details may vary, but generated code must not rename or materially alter these APIs without first updating this contract and the architecture/spec documents.

---

## 1. Includes and namespace

Primary include:

```cpp
#include <ArduinoAwait.h>
```

All public declarations are under:

```cpp
namespace arduinoawait {
    // ...
}
```

No required global namespace aliases are provided.

---

## 2. Fundamental public types

```cpp
namespace arduinoawait {

using TaskSlot = uint16_t;
using TaskGeneration = uint32_t;

struct TaskId {
    TaskSlot slot;
    TaskGeneration generation;

    friend constexpr bool operator==(TaskId, TaskId) = default;
};

}
```

If target constraints require a different slot width, that may change before first release, but generation-backed identity is mandatory.

---

## 3. Error

```cpp
namespace arduinoawait {

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

}
```

Error reporting is configured by the library error hook. Public APIs do not normally throw exceptions.

---

## 4. Task<T>

The public template is declared from V1 so later typed Tasks do not require renaming the core type.

```cpp
namespace arduinoawait {

template <class T = void>
class Task;

template <>
class Task<void> {
public:
    Task() noexcept;
    Task(Task&& other) noexcept;
    Task& operator=(Task&& other) noexcept;

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task();

    explicit operator bool() const noexcept;

    // Awaiting consumes/transfers ownership of this Task.
    auto operator co_await() && noexcept;
};

}
```

V1 only requires functional `Task<void>` support. Instantiation of unsupported `Task<T>` must fail clearly at compile time rather than silently behaving incorrectly.

Task-producing coroutine functions are lazy.

```cpp
Task<void> foo();

auto t = foo(); // does not run foo yet
```

---

## 5. TaskHandle

```cpp
namespace arduinoawait {

class TaskHandle {
public:
    constexpr TaskHandle() noexcept = default;

    bool valid() const noexcept;
    bool done() const noexcept;
    TaskId id() const noexcept;

    explicit operator bool() const noexcept {
        return valid();
    }
};

}
```

`TaskHandle` is copyable observational identity and does not own the coroutine frame.

A handle remains valid after its task completes until that scheduler slot is reused. During that interval `done()` returns true. When the slot is reused, its generation increments and the older handle becomes invalid. A default-constructed or stale handle returns `valid() == false` and `done() == false`.

V1 does not expose cancellation on `TaskHandle`.

---

## 6. Task scheduling

```cpp
namespace arduinoawait {

[[nodiscard]] TaskHandle create_task(Task<void>&& task);
void spawn(Task<void>&& task);
TaskHandle current_task() noexcept;

}
```

Semantics:

```cpp
auto h = create_task(foo()); // schedules concurrently, observable
spawn(foo());                // schedules concurrently, intentionally detached
co_await foo();              // sequential child await
```

`create_task()` and `spawn()` enqueue work; they do not execute arbitrary user coroutine code inline.

Passing an empty, previously scheduled, or otherwise invalid Task invokes the configured deterministic error policy.

---

## 7. Scheduler

```cpp
namespace arduinoawait {

class Scheduler {
public:
    void poll();

    bool hasReadyTasks() const noexcept;
    bool hasPendingTasks() const noexcept;
    size_t activeTaskCount() const noexcept;

    TaskHandle currentTask() const noexcept;
};

Scheduler& scheduler() noexcept;
void poll();

}
```

The common Arduino application uses the singleton helper:

```cpp
void loop() {
    arduinoawait::poll();
}
```

`poll()` never waits for a future deadline.

---

## 8. delay and yield

```cpp
namespace arduinoawait {

class YieldAwaitable;
class DelayAwaitable;

[[nodiscard]] YieldAwaitable yield() noexcept;

[[nodiscard]] DelayAwaitable delay(uint32_t milliseconds) noexcept;
[[nodiscard]] DelayAwaitable delay_ms(uint32_t milliseconds) noexcept;
[[nodiscard]] DelayAwaitable delay_us(uint64_t microseconds) noexcept;

}
```

`delay(ms)` is an alias/equivalent of `delay_ms(ms)`.

Required semantics:

```cpp
co_await yield();
co_await delay(0);
co_await delay_ms(0);
co_await delay_us(0);
```

all suspend and yield fairly; zero duration is not an immediate `await_ready()` success.

Positive delays use the internal 64-bit monotonic microsecond clock. If `now_us + duration_us` would overflow `uint64_t`, the operation invokes the configured error hook with `Error::deadline_overflow`; silent wrap and saturation are not V1 behavior.

> **Arduino name collision.** The Arduino core declares global `::yield()` and `::delay()`. Under `using namespace arduinoawait;` a bare `yield()` or `delay(ms)` is therefore ambiguous with the core's globals. Call `arduinoawait::yield()` (qualified) — or use the `aa::` alias — and prefer `delay_ms()`/`delay_us()`, which have no core equivalent. This is a usage convention only; the frozen signatures above are unchanged.

---

## 9. Event

```cpp
namespace arduinoawait {

class Event {
public:
    Event() noexcept = default;
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    class Awaiter;

    [[nodiscard]] Awaiter wait() noexcept;

    void set();
    void clear() noexcept;
    bool isSet() const noexcept;
};

}
```

Semantics:

- manual-reset;
- scheduler-context only;
- `set()` wakes all current waiters in FIFO order and leaves Event set;
- `clear()` resets it;
- a set Event makes `wait()` complete without suspension;
- ordinary Event has no ISR method.

Destroying an Event with active waiters is a deterministic programming error.

---

## 10. ThreadSafeFlag

```cpp
namespace arduinoawait {

class ThreadSafeFlag {
public:
    ThreadSafeFlag() noexcept;
    ThreadSafeFlag(const ThreadSafeFlag&) = delete;
    ThreadSafeFlag& operator=(const ThreadSafeFlag&) = delete;

    class Awaiter;

    // May be called from supported external/IRQ/callback/other-core contexts.
    void set() noexcept;

    // Scheduler-context operation.
    void clear() noexcept;

    bool isSet() const noexcept;

    [[nodiscard]] Awaiter wait() noexcept;
};

}
```

Semantics:

- one simultaneous waiter maximum;
- auto-reset when `wait()` consumes a signal;
- repeated `set()` while already set coalesces;
- `set()` never resumes coroutine user code directly;
- second simultaneous waiter triggers deterministic error handling.

The exact external contexts supported by each platform backend must be documented and hardware-tested.

---

## 11. Queue<T, Capacity>

```cpp
namespace arduinoawait {

template <class T, size_t Capacity>
class Queue {
public:
    static_assert(Capacity > 0);

    Queue() noexcept;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    class SendAwaiter;
    class ReceiveAwaiter;

    [[nodiscard]] SendAwaiter send(const T& value);
    [[nodiscard]] SendAwaiter send(T&& value);

    [[nodiscard]] ReceiveAwaiter receive() noexcept;

    bool trySend(const T& value);
    bool trySend(T&& value);
    bool tryReceive(T& out);

    bool empty() const noexcept;
    bool full() const noexcept;
    size_t size() const noexcept;
    static constexpr size_t capacity() noexcept { return Capacity; }
};

}
```

Requirements:

- statically allocated bounded storage;
- FIFO data order;
- FIFO sender/receiver waiter order;
- no default-constructibility requirement imposed merely by storage allocation;
- supports move construction where `T` supports it;
- scheduler-local only in V1;
- no ISR `trySendFromISR()` method on ordinary Queue.

Destroying Queue with active waiters is a deterministic programming error.

---

## 12. waitUntil

V1 exposes `waitUntil` as a header-defined coroutine composition:

```cpp
namespace arduinoawait {

template <class Predicate>
Task<void> waitUntil(Predicate predicate) {
    while (!predicate()) {
        co_await yield();
    }
}

}
```

Equivalent forwarding/perfect-forwarding implementation is allowed.

The scheduler does not type-erase or poll arbitrary predicate callbacks internally.

---

## 13. Clock-facing public API

The platform clock is intentionally not a normal application API in V1.

Internal contract:

```cpp
namespace arduinoawait::detail {
using tick_t = uint64_t;
uint64_t platform_now_us() noexcept;
}
```

An advanced/test override may be provided via build configuration, but normal applications use `delay*()` and do not depend on the backend clock function.

---

## 14. Diagnostics

When diagnostics are enabled:

```cpp
namespace arduinoawait {

struct Stats {
    size_t activeTasks;
    size_t peakTasks;
    size_t readyTasks;
    size_t waitingTimers;
    size_t frameBytesUsed;
    size_t peakFrameBytesUsed;
    size_t frameBytesFree;
    size_t allocationFailures;
};

Stats stats() noexcept;

}
```

Diagnostic field naming may be normalized before first published release, but once implementation begins the tests and examples must use one consistent form.

---

## 15. Configuration contract

Supported compile-time configuration names:

```cpp
ARDUINOAWAIT_MAX_TASKS
ARDUINOAWAIT_FRAME_POOL_BYTES
ARDUINOAWAIT_ON_ERROR(error)
ARDUINOAWAIT_ENABLE_DIAGNOSTICS
ARDUINOAWAIT_ENABLE_ISR
```

Optional advanced/test clock override, if implemented:

```cpp
ARDUINOAWAIT_CLOCK_NOW_US()
```

It returns a `uint64_t` count in microseconds.

The normal RP2040/RP2350/ESP32 platform backends do not require the user to define it.

---

## 16. V1 intentionally absent APIs

The following are not part of V1 public API:

```text
Task<T> results for T != void
cancel()
CancellationToken
TaskGroup
when_all
when_any
timeout
AsyncLock
Semaphore
ThreadSafeQueue
CrossCoreQueue
Future/Promise abstraction
Executor abstraction
FreeRTOS Task wrappers
per-task priority
```

They may be introduced later without changing the core V1 ownership model.

---

## 17. Canonical V1 example

```cpp
#include <ArduinoAwait.h>

using namespace arduinoawait;

Event ready;
Queue<int, 8> samples;

Task<void> producer() {
    while (true) {
        const int value = analogRead(A0);
        co_await samples.send(value);
        co_await delay(10);
    }
}

Task<void> consumer() {
    ready.set();

    while (true) {
        int value = co_await samples.receive();
        Serial.println(value);
        co_await yield();
    }
}

Task<void> application() {
    spawn(producer());
    spawn(consumer());

    co_await ready.wait();

    while (true) {
        co_await delay(1000);
    }
}

void setup() {
    Serial.begin(115200);
    spawn(application());
}

void loop() {
    poll();
}
```

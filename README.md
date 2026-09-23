# SimpleAwait

A small, deterministic, fixed-memory cooperative coroutine library for Arduino,
built on native standard **C++20 coroutines**.

SimpleAwait lets you write embedded control flow as ordinary sequential code:

```cpp
#include <SimpleAwait.h>
using namespace simpleawait;

// Led on 500ms, off 1s, repeat
Task<void> blink() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay_ms(500);
        digitalWrite(LED_BUILTIN, LOW);
        co_await delay_ms(1000);
    }
}

// Each worker wakes on its own interval and reports how long it actually slept.
Task<void> worker(const char* name, uint32_t period_ms) {
    uint32_t last = millis();
    while (true) {
        co_await delay_ms(period_ms);
        uint32_t now = millis();
        Serial.print(name);
        Serial.print(": ");
        Serial.print(now - last);
        Serial.println(" ms since last run");
        last = now;
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    spawn(blink());
    spawn(worker("worker A", 1350));
    spawn(worker("worker B", 1200));
}

void loop() {
    poll();
}
```

instead of hand-written `millis()` state machines. It is conceptually a
statically allocated, C++20, Arduino-native equivalent in spirit to MicroPython
`asyncio` — **not** a tiny RTOS.

## Why would I use this?
Write sequential-looking asynchronous Arduino code without an RTOS and without giving up deterministic memory use.
| Approach                  | Code style                |               Heap required | Preemptive |   RTOS |
| ------------------------- | ------------------------- | --------------------------: | ---------: | -----: |
| `millis()` state machines | manual state machine      |                          No |         No |     No |
| TaskScheduler-style       | callbacks                 |                   No/varies |         No |     No |
| FreeRTOS tasks            | sequential                |                     Usually |        Yes |    Yes |
| **SimpleAwait**           | **sequential `co_await`** | **No coroutine-frame heap** |     **No** | **No** |


## First-class targets

- Raspberry Pi RP2040 (Arduino-Pico)
- Raspberry Pi RP2350, Arm and RISC-V (Arduino-Pico)
- ESP32 family, with ESP32-S3 as a required hardware target (Arduino-ESP32)

Language floor is C++20. C++23 and later are supported but never required.

## Design principles

- Native C++20 coroutines; cooperative (not preemptive) scheduling.
- Fixed-memory: statically allocated scheduler metadata and a fixed, variable
  -size coroutine frame pool. No global heap allocation for coroutine frames and
  no heap fallback on exhaustion.
- Deterministic failure behavior through a configurable error hook.
- 64-bit monotonic microsecond timebase (`time_us_64()` on RP2040/RP2350,
  `esp_timer_get_time()` on ESP32, injected fake clock for host tests).
- Scheduler-local `Event`/`Queue`; external/ISR notification via
  `ThreadSafeFlag`. No coroutine body ever runs in ISR context.

## Core primitives

Every application drives the scheduler by calling
[`poll()`](docs/simpleawait/V1_API_CONTRACT.md#7-scheduler) from `loop()`; each
`poll()` resumes the tasks that were ready at the start of the pass and leaves any
work it readies for the next pass. The exact, frozen scheduler pass is enumerated
in [architecture §9](docs/simpleawait/ARCHITECTURE.md#9-exact-v1-scheduler-pass).

```cpp
#include <SimpleAwait.h>
using namespace simpleawait;

void setup() { spawn(blink()); }
void loop()  { poll(); }
```

The summaries below are a quick reference; the **authoritative** signatures and
semantics are the frozen
[V1 API Contract](docs/simpleawait/V1_API_CONTRACT.md), with design rationale in
[ARCHITECTURE](docs/simpleawait/ARCHITECTURE.md) and the full narrative in the
[implementation spec](docs/simpleawait/SimpleAwait_Implementation_Spec.md). The
[detailed repository architecture](docs/simpleawait/ARCHITECTURE_DETAILED.md)
maps the implementation's classes, ownership, states, runtime flows, platform
boundaries, and test structure. Each entry below links its contract section
(API) and architecture section (design).

- **Tasks.**

  `Task<void>` is a lazy, move-only coroutine whose frame lives in the fixed pool
  and whose body runs only inside `poll()`.

  `spawn(task())` schedules the `Task` detached;

  `create_task(task())` schedules `Task` and also returns a  generation-checked
  `TaskHandle` whose `done()` stays observable until the slot is reused.

  Awaiting a child (`co_await child()`) runs it to completion and
  resumes the parent on a later pass; `current_task()` identifies the running
  task.

  *Spec: API [§4 Task](docs/simpleawait/V1_API_CONTRACT.md#4-task),
  [§5 TaskHandle](docs/simpleawait/V1_API_CONTRACT.md#5-taskhandle),
  [§6 scheduling](docs/simpleawait/V1_API_CONTRACT.md#6-task-scheduling); design
  [§5 state machine](docs/simpleawait/ARCHITECTURE.md#5-task-state-machine),
  [§19 parent/child](docs/simpleawait/ARCHITECTURE.md#19-parentchild-await).*
- **Time.**

  `co_await yield()` is a fair yield point;

  `co_await delay_ms(n)` / `delay_us(n)` / `delay(n)` suspend on the 64-bit microsecond
  timebase, and even a zero duration re-queues for a later pass instead of busy-spinning.

  *Spec: API
  [§8 delay and yield](docs/simpleawait/V1_API_CONTRACT.md#8-delay-and-yield);
  design [§11 timer architecture](docs/simpleawait/ARCHITECTURE.md#11-timer-architecture).*
- **Conditions.**

  `co_await waitUntil(pred)` is a header-only composition that parks the task at
  fair yield points until `pred()` returns true; the scheduler never polls the
  predicate itself.

  *Spec: API
  [§12 waitUntil](docs/simpleawait/V1_API_CONTRACT.md#12-waituntil); design
  [§18 waitUntil](docs/simpleawait/ARCHITECTURE.md#18-waituntil).*
- **Signaling.**

  `Event` is scheduler-local, manual-reset, and multi-waiter.

  `co_await ev.wait()` suspends until `ev.set()` wakes every current waiter in
  FIFO order and leaves the event set (so a later `wait()` passes straight
  through); `ev.clear()` resets it.

  *Spec: API [§9 Event](docs/simpleawait/V1_API_CONTRACT.md#9-event);
  design [§15 Event](docs/simpleawait/ARCHITECTURE.md#15-event).*
- **Messaging.**

  `Queue<T, N>` is a bounded, scheduler-local FIFO.

  `co_await q.send(v)` and `T v = co_await q.receive()` block with automatic
  back-pressure and FIFO value and waiter order; `trySend`/`tryReceive` are the
  non-blocking variants, and `T` need not be default-constructible.

  *Spec: API
  [§11 Queue](docs/simpleawait/V1_API_CONTRACT.md#11-queuet-capacity); design
  [§17 Queue](docs/simpleawait/ARCHITECTURE.md#17-queuet-n).*
- **External signaling.**

  `ThreadSafeFlag` is the single-waiter bridge from an ISR or other-core context.

  `flag.set()` is the only method safe to call from there; it coalesces repeated
  signals and never runs coroutine code, and a task `co_await flag.wait()`s for
  it and auto-resets the flag on consumption.

  *Spec:
  API [§10 ThreadSafeFlag](docs/simpleawait/V1_API_CONTRACT.md#10-threadsafeflag);
  design [§16 ThreadSafeFlag](docs/simpleawait/ARCHITECTURE.md#16-threadsafeflag).*
- **Diagnostics.**

  With `SIMPLEAWAIT_ENABLE_DIAGNOSTICS=1`, `stats()` returns an
  allocation-free `Stats` snapshot (active/peak/ready/waiting-timer task counts
  and frame-pool bytes used/peak/free plus allocation failures).

  *Spec: API
  [§14 Diagnostics](docs/simpleawait/V1_API_CONTRACT.md#14-diagnostics); design
  [§25 Diagnostics](docs/simpleawait/ARCHITECTURE.md#25-diagnostics).*

## Examples

The [`examples/`](examples/) directory holds eight runnable golden examples plus a
build skeleton. See [Compiling an example](#compiling-an-example-arduino) to build
one for your board.

| Example | What it shows |
|---|---|
| [`01_Blink`](examples/01_Blink) | The canonical cooperative LED blink — one task toggles the LED with `co_await delay_ms()` instead of a `millis()` state machine. |
| [`02_TwoTasks`](examples/02_TwoTasks) | Two independent tasks scheduled concurrently with `create_task` and `spawn`, resumed in FIFO order within each `poll()` pass. |
| [`03_YieldFairness`](examples/03_YieldFairness) | Cooperative fairness — tasks that only `co_await yield()` advance in lockstep and never starve one another. |
| [`04_ParentChild`](examples/04_ParentChild) | Structured sequential composition — a parent task `co_await`s child tasks one after another. |
| [`05_Event`](examples/05_Event) | Several tasks wait on one manual-reset `Event`; a single `set()` releases them all in FIFO order. |
| [`06_ThreadSafeFlagIRQ`](examples/06_ThreadSafeFlagIRQ) | The interrupt-to-coroutine bridge — an ISR calls `ThreadSafeFlag::set()` and a task `co_await`s the flag. |
| [`07_QueueProducerConsumer`](examples/07_QueueProducerConsumer) | A producer and consumer exchange values through a bounded `Queue<T,N>` with automatic back-pressure. |
| [`08_WaitUntil`](examples/08_WaitUntil) | A task suspends at fair yield points with `co_await waitUntil(pred)` until a condition holds. |
| [`Empty`](examples/Empty) | The minimal build skeleton — includes the header and compiles on every target; a starting point for new sketches. |

## The cooperative model: never block

Scheduling is cooperative, not preemptive: a task runs until it `co_await`s. Code
that busy-waits or blocks (`delay()` the Arduino builtin, `while (!ready) {}`,
long computations, blocking I/O) stalls **every** task and the whole `poll()`
loop. Yield control instead — `co_await delay_ms(n)`, `co_await yield()`,
`co_await waitUntil(pred)`, or await an `Event`/`Queue`/`ThreadSafeFlag`.

`Event` and `Queue` are **scheduler-context only** — use them between tasks, never
from an interrupt. The only primitive whose `set()` is safe from an ISR, a
hardware callback, or another core is `ThreadSafeFlag`; it merely marks a pending
signal that the next `poll()` resolves, so no coroutine ever executes in ISR
context. On generic Arduino cores without a first-class backend, that ISR-safe
critical section is unavailable, so `<SimpleAwait.h>` requires
`SIMPLEAWAIT_CRITICAL_SECTION_OVERRIDE` there (RP2040/RP2350/ESP32 need nothing).

## Configuration

Define any of these before including `<SimpleAwait.h>` (defaults shown):

| Macro | Default | Purpose |
|---|---|---|
| `SIMPLEAWAIT_MAX_TASKS` | `32` | Maximum concurrently scheduled tasks |
| `SIMPLEAWAIT_FRAME_POOL_BYTES` | `4096` | Coroutine frame pool size (bytes) |
| `SIMPLEAWAIT_ON_ERROR(error)` | halt/abort | Deterministic error hook |
| `SIMPLEAWAIT_ENABLE_DIAGNOSTICS` | `0` | Compile in diagnostic counters |
| `SIMPLEAWAIT_ENABLE_ISR` | `0` | Reserved V1 toggle; the current `ThreadSafeFlag` bridge is compiled regardless of this value |

### Sizing tasks and coroutine frames

`SIMPLEAWAIT_MAX_TASKS` and `SIMPLEAWAIT_FRAME_POOL_BYTES` control different
fixed-memory resources:

- `SIMPLEAWAIT_MAX_TASKS` reserves scheduler metadata and limits how many tasks
  the scheduler can own simultaneously.
- `SIMPLEAWAIT_FRAME_POOL_BYTES` reserves the arena that holds all live
  coroutine frames.

The limits are configured independently, but every scheduled task needs both a
scheduler slot and a frame. Effective concurrency is therefore bounded by the
smaller resource:

```text
min(task slots, variable-sized coroutine frames that fit in the frame pool)
```

There is no exact bytes-per-task constant. The compiler determines each frame's
size from the coroutine's parameters, local state, active awaiters, alignment,
and target ABI. As a result:

- large frames can exhaust the pool before all task slots are used;
- many small frames can exhaust task slots while pool bytes remain;
- an unscheduled lazy `Task<void>` consumes frame bytes but no scheduler slot;
- an awaited child temporarily needs its own frame and scheduler slot while its
  parent remains live.

#### What makes a frame larger

Parameters passed by value normally live in the frame:

```cpp
Task<void> process(int id, LargeConfig config) {
    co_await delay_ms(100);
    use(id, config);
}
```

This retains a full `LargeConfig`. A pointer or reference normally retains only
an address, but the referenced object must outlive the coroutine:

```cpp
Task<void> process(const LargeConfig& config) {
    co_await delay_ms(100);
    use(config); // config must still exist here
}
```

Never use a reference merely to save frame bytes if it can dangle while the task
is suspended.

Local variables generally need frame storage when their values remain live
across a `co_await`:

```cpp
Task<void> largeFrame() {
    uint8_t buffer[256];
    fill(buffer);
    co_await delay_ms(100);
    consume(buffer); // buffer must survive in the frame
}
```

End a large local's lifetime before suspending when it is no longer needed:

```cpp
Task<void> smallerFrame() {
    {
        uint8_t buffer[256];
        fillAndConsume(buffer);
    } // buffer need not survive the suspension

    co_await delay_ms(100);
}
```

Awaiters can also carry data. A suspended `Queue<T,N>::send(value)` retains its
`T` in the sender's coroutine frame, and `waitUntil(predicate)` stores the
predicate by value. Large queue values or large by-value lambda captures can
therefore increase frame requirements.

Frame layout is compiler-, optimization-, and target-dependent. `sizeof(Task<void>)`
reports only the small coroutine handle wrapper, not the allocated frame size.
Measure the actual target build rather than copying host measurements.

#### Practical sizing workflow

1. Start with the defaults and enable diagnostics in a shared configuration
   header included before `<SimpleAwait.h>`:

   ```cpp
   #define SIMPLEAWAIT_ENABLE_DIAGNOSTICS 1
   #include <SimpleAwait.h>
   ```

2. Exercise the maximum realistic workload: create the largest expected set of
   concurrent tasks, enter queue back-pressure paths, and run nested child tasks.
3. Inspect `stats().peakTasks`, `stats().peakFrameBytesUsed`, and
   `stats().allocationFailures`.
4. Set `SIMPLEAWAIT_MAX_TASKS` above the observed simultaneous task count,
   including parents waiting on children.
5. Set `SIMPLEAWAIT_FRAME_POOL_BYTES` above the observed peak frame usage with
   headroom for compiler, optimization, and feature changes.
6. Repeat the measurement on every target/toolchain configuration you ship.

Keep both limits intentional. Raising only one does not increase usable
concurrency when the other is already the bottleneck.

## Building and testing (host)

Host builds exist for deterministic testing only; the desktop is not a product
target. With CMake and any C++20 compiler:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are compiled at C++20 and, where the toolchain supports it, also at C++23.
Pass `-DSIMPLEAWAIT_ENABLE_SANITIZERS=ON` (GCC/Clang) to build the host tests
with AddressSanitizer and UndefinedBehaviorSanitizer.

## Compiling an example (Arduino)

The `--library .` flag points arduino-cli at this repository as the library
source (there is no installed copy in a fresh checkout):

```sh
# RP2040
arduino-cli compile --fqbn rp2040:rp2040:rpipico --library . examples/Empty
# RP2350 (Arm)
arduino-cli compile --fqbn rp2040:rp2040:rpipico2 --library . examples/Empty
# RP2350 (RISC-V)
arduino-cli compile --fqbn rp2040:rp2040:rpipico2:arch=riscv --library . examples/Empty
# ESP32
arduino-cli compile --fqbn esp32:esp32:esp32 --library . examples/Empty
# ESP32-S3
arduino-cli compile --fqbn esp32:esp32:esp32s3 --library . examples/Empty
```

## Documentation

Architecture and specification documentation lives under
[`docs/simpleawait/`](docs/simpleawait/):

- [`V1_API_CONTRACT.md`](docs/simpleawait/V1_API_CONTRACT.md) — the frozen public API
- [`ARCHITECTURE.md`](docs/simpleawait/ARCHITECTURE.md) — the normative design
- [`ARCHITECTURE_DETAILED.md`](docs/simpleawait/ARCHITECTURE_DETAILED.md) — the
  non-normative implementation-oriented component, class, lifecycle, and flow map
- [`SimpleAwait_Implementation_Spec.md`](docs/simpleawait/SimpleAwait_Implementation_Spec.md) — implementation notes and rationale

## Packaging and publication notes

`SimpleAwait` is authored and maintained by **Noel Anderson** under the [MIT](LICENSE) license.

The library passes Arduino Lint against the library specification: the CI
metadata check runs `arduino-lint --compliance specification` and fails closed
on any error-level rule, so metadata regressions break the build.

## License

[MIT](LICENSE).

# SimpleAwait

A small, deterministic, fixed-memory cooperative coroutine library for Arduino,
built on native standard **C++20 coroutines**.

SimpleAwait lets you write embedded control flow as ordinary sequential code:

```cpp
#include <SimpleAwait.h>
using namespace simpleawait;

Task<void> blink() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay_ms(500);
        digitalWrite(LED_BUILTIN, LOW);
        co_await delay_ms(500);
    }
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    spawn(blink());
}

void loop() { poll(); }
```

instead of hand-written `millis()` state machines. It is conceptually a
statically allocated, C++20, Arduino-native equivalent in spirit to MicroPython
`asyncio` — **not** a tiny RTOS.

> **Status: 1.0.0 — the V1 API is complete and frozen.** `Task`, the cooperative
> scheduler (`create_task`/`spawn`/`poll`/`current_task`), `yield()`/`delay*()`,
> parent/child `co_await`, `Event`, `ThreadSafeFlag`, `Queue<T,N>`, and
> `waitUntil` are covered by a deterministic host test suite (MSVC and Clang,
> C++20 and C++23, plus ASan/UBSan) and eight golden examples that compile across
> the full target matrix — RP2040, RP2350 (Arm and RISC-V), and ESP32/ESP32-S3 —
> in CI. On-target validation sketches live in [`hardware/`](hardware/). The
> example above compiles and runs.

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

Every application drives the scheduler by calling `poll()` from `loop()`; each
`poll()` resumes the tasks that were ready at the start of the pass (work made
ready during a pass runs on the next one).

```cpp
#include <SimpleAwait.h>
using namespace simpleawait;

void setup() { spawn(blink()); }
void loop()  { poll(); }
```

- **Tasks.** `Task<void>` is a lazy, move-only coroutine. `spawn(task())` hands a
  task to the scheduler; `create_task(task())` also returns a generation-checked
  `TaskHandle`. Awaiting a child task (`co_await child()`) runs it to completion
  and resumes the parent on a later pass. `current_task()` identifies the running
  task.
- **Time.** `co_await yield()` is a fair yield point; `co_await delay_ms(n)` /
  `delay_us(n)` / `delay(n)` suspend on the 64-bit microsecond timebase.
  `co_await waitUntil(pred)` yields until `pred()` becomes true.
- **`Event`** — scheduler-local, manual-reset, multi-waiter. `co_await ev.wait()`
  suspends until `ev.set()` wakes all waiters (FIFO); `ev.clear()` resets it.
- **`Queue<T, N>`** — bounded, scheduler-local FIFO. `co_await q.send(v)` and
  `T v = co_await q.receive()` block with automatic back-pressure and FIFO value
  and waiter order; `trySend`/`tryReceive` are the non-blocking variants.
- **`ThreadSafeFlag`** — the single-waiter bridge from an ISR/other-core context:
  `flag.set()` is the only method safe to call from there, and it never runs
  coroutine code; a task `co_await flag.wait()`s for it.
- **Diagnostics.** With `SIMPLEAWAIT_ENABLE_DIAGNOSTICS=1`, `stats()` returns an
  allocation-free snapshot (active/peak/ready/waiting-timer task counts and
  frame-pool bytes used/peak/free plus allocation failures).

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
| `SIMPLEAWAIT_ENABLE_ISR` | `0` | Compile in external/ISR signaling |

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

The normative specification lives under [`docs/simpleawait/`](docs/simpleawait/):

- [`V1_API_CONTRACT.md`](docs/simpleawait/V1_API_CONTRACT.md) — the frozen public API
- [`ARCHITECTURE.md`](docs/simpleawait/ARCHITECTURE.md) — the normative design
- [`SimpleAwait_Implementation_Spec.md`](docs/simpleawait/SimpleAwait_Implementation_Spec.md) — implementation notes and rationale

## Packaging and publication notes

The packaging metadata (`library.properties`, `library.json`) declares
`SimpleAwait` version `1.0.0`, authored and maintained by **Noel Anderson**,
under the [MIT](LICENSE) license. Update the `url` / `repository` fields if the
canonical repository differs from the default.

The library passes Arduino Lint against the library specification: the CI
metadata check runs `arduino-lint --compliance specification` and fails closed
on any error-level rule, so metadata regressions break the build.

## License

[MIT](LICENSE).

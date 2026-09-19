# ArduinoAwait

A small, deterministic, fixed-memory cooperative coroutine library for Arduino,
built on native standard **C++20 coroutines**.

ArduinoAwait lets you write embedded control flow as ordinary sequential code:

```cpp
#include <ArduinoAwait.h>
using namespace arduinoawait;

Task<void> blink() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay(500);
        digitalWrite(LED_BUILTIN, LOW);
        co_await delay(500);
    }
}
```

instead of hand-written `millis()` state machines. It is conceptually a
statically allocated, C++20, Arduino-native equivalent in spirit to MicroPython
`asyncio` — **not** a tiny RTOS.

> **Status: milestone M0 (build skeleton).** The public include, configuration
> surface, and compile-time coroutine-support checks exist. They are verified by
> the host test suite (C++20 and C++23) and by `examples/Empty` compiles for
> RP2040 and RP2350 (Arm and RISC-V); the ESP32/ESP32-S3 example compiles run in
> CI. The scheduler, `Task`, timers, and synchronization primitives are
> implemented in subsequent milestones. The example above shows the intended V1
> API and does not compile yet.

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

## Configuration

Define any of these before including `<ArduinoAwait.h>` (defaults shown):

| Macro | Default | Purpose |
|---|---|---|
| `ARDUINOAWAIT_MAX_TASKS` | `32` | Maximum concurrently scheduled tasks |
| `ARDUINOAWAIT_FRAME_POOL_BYTES` | `4096` | Coroutine frame pool size (bytes) |
| `ARDUINOAWAIT_ON_ERROR(error)` | halt/abort | Deterministic error hook |
| `ARDUINOAWAIT_ENABLE_DIAGNOSTICS` | `0` | Compile in diagnostic counters |
| `ARDUINOAWAIT_ENABLE_ISR` | `0` | Compile in external/ISR signaling |

## Building and testing (host)

Host builds exist for deterministic testing only; the desktop is not a product
target. With CMake and any C++20 compiler:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Tests are compiled at C++20 and, where the toolchain supports it, also at C++23.
Pass `-DARDUINOAWAIT_ENABLE_SANITIZERS=ON` (GCC/Clang) to build the host tests
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

The normative specification lives under [`docs/arduinoawait/`](docs/arduinoawait/):

- [`V1_API_CONTRACT.md`](docs/arduinoawait/V1_API_CONTRACT.md) — frozen public API
- [`ARCHITECTURE.md`](docs/arduinoawait/ARCHITECTURE.md) — normative design
- [`ArduinoAwait_Implementation_Spec.md`](docs/arduinoawait/ArduinoAwait_Implementation_Spec.md)
- [`IMPLEMENTATION_PLAN.md`](docs/arduinoawait/IMPLEMENTATION_PLAN.md) — milestones

## Packaging and publication notes

The MIT license, the `ArduinoAwait contributors` copyright holder, and the
repository URL in the packaging metadata are initial defaults; update them to
match the project's chosen license and canonical repository when published.

The library name **intentionally** starts with "Arduino". Arduino Lint reports
this as rule `LP012` ("name starts with Arduino", reserved for official
libraries) at every compliance level. This is a fixed part of the public
identity (`ArduinoAwait.h`, namespace `arduinoawait`) and only affects a future
Arduino Library Manager submission decision. The CI metadata check validates the
metadata and tolerates only this one documented deviation; it does not enable
arduino-lint "official" mode.

## License

[MIT](LICENSE).

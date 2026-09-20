# Changelog

All notable changes to ArduinoAwait are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added — M3: Lazy Task<void> and ownership

- `arduinoawait::Task<void>` (`src/arduinoawait/task.h`): a lazy, move-only
  coroutine handle. Calling a Task-returning coroutine allocates the frame from
  the fixed pool and suspends at `initial_suspend` — the body does not run until
  the Task is scheduled/awaited (later milestones). Ownership is a single token:
  moving transfers it and empties the source; a default/moved-from Task owns
  nothing and its destructor is a no-op; destroying an unscheduled Task destroys
  its frame exactly once and returns the bytes to the pool. `explicit operator
  bool()` reports ownership. `Task<T>` for any `T != void` is a clear compile
  error (`static_assert`).
- The `Task<void>` promise's `operator new`/`operator delete` (plain, `nothrow`,
  and sized) route through the process-wide frame pool
  (`detail::frame_pool()`, `src/arduinoawait/detail/global_frame_pool.h`), so no
  coroutine frame ever touches the global heap. On exhaustion the deterministic
  hook is invoked with `Error::frame_pool_exhausted`; with a non-halting override
  the coroutine returns the empty Task via `get_return_object_on_allocation_failure`
  (no exception, works under `-fno-exceptions`). An unhandled exception routes to
  `Error::unhandled_exception`.
- Host tests: laziness (body not run), frame-from-pool with a global new/delete
  canary (no heap), move-construct/assign ownership transfer, moved-from/default
  harmlessness, full recovery over many create/destroy cycles, and a small-pool
  exhaustion test (frame_pool_exhausted + empty Task + recovery). Pass on MSVC and
  Clang 23.1.1 at C++20 and C++23.
- `hardware/TaskLifecycle` validation sketch (developer tool; not host CI) that
  forces the coroutine promise + pool integration to compile/link on every target.

### Added — M2: Fixed coroutine frame allocator

- `detail::FramePool<Bytes>` (`src/arduinoawait/detail/frame_pool.h`): a fixed
  byte arena that hands out variable-size, aligned blocks for coroutine frames
  with no global heap fallback. It is a coalescing first-fit free list —
  arbitrary free order, adjacent free blocks merge, and full capacity is
  recovered once every block is freed (`bytesUsed() == 0`). Alignment is honored
  up to and beyond `alignof(std::max_align_t)` (over-aligned requests are padded
  within the block). Exhaustion is deterministic: `allocate()` returns `nullptr`
  and records a failure rather than allocating from the heap (the caller applies
  the `Error::frame_pool_exhausted` policy in a later milestone). Requests are
  robust: oversized/overflowing sizes are rejected with checked subtractive
  arithmetic, over-aligned requests compute padding as integers (never forming an
  out-of-arena pointer before the fit is validated), a non-power-of-two alignment
  is rejected, and a capacity that would not fit the 32-bit block header fields is
  a compile error. Block headers live in the arena and are created with placement
  `new` / re-accessed through `std::launder` for well-defined object lifetime.
  Stats: `bytesUsed`, `bytesFree`, `peakBytesUsed`, `allocationFailures`,
  `capacity`, `blockOverhead`.
- Host test covering exact-capacity allocation, many small frames, mixed sizes
  with non-overlap, arbitrary destruction order, fragmentation/coalescing with
  full recovery, over-aligned requests, oversized/overflow requests, invalid
  alignment, deterministic exhaustion, peak tracking, and a whole-test global
  `operator new`/`delete` canary proving the pool never touches the heap. A
  persistent negative-compile test asserts the capacity static_assert. (Clang
  ASan/UBSan run in CI; MSVC ASan is environmentally blocked on the dev host.)
- `hardware/FramePoolCheck` validation sketch (developer tool; not host CI) that
  exercises the allocator on-target and, via CI, compiles/links it for every
  target's word size and alignment.

### Added — M1: Platform clock abstraction

- Deterministic `arduinoawait::Error` enum (frozen V1 surface) in
  `src/arduinoawait/error.h`.
- 64-bit monotonic microsecond platform clock `detail::platform_now_us()`
  (`src/arduinoawait/detail/platform_clock.h`) with compile-time backend
  selection verified against the real cores: `time_us_64()` on RP2040/RP2350
  (Arm and RISC-V, selected by `ARDUINO_ARCH_RP2040`), `esp_timer_get_time()` on
  ESP32 (`ARDUINO_ARCH_ESP32`), a software-extended 32-bit `micros()` secondary
  backend for generic Arduino, and an injectable `ARDUINOAWAIT_CLOCK_NOW_US`
  override for tests. There is no silent host default: a host build must inject a
  clock (or opt into a `steady_clock` adapter via `ARDUINOAWAIT_HOST_REALTIME_CLOCK`),
  so a test that forgets injection gets a link error rather than nondeterministic
  real time. All scheduler timing flows through this one function; no other
  source calls a platform clock primitive directly.
- Deadline arithmetic (`src/arduinoawait/detail/time_math.h`): `ms_to_us`
  widening, an `add_overflows` predicate, and `compute_deadline(now, dur, out&)`,
  which returns a `bool` success indicator (never conflating a valid maximum
  deadline with failure), writes the deadline only on success, and routes a
  would-be `uint64` overflow to the error hook as `Error::deadline_overflow`
  (V1 forbids silent wrap/saturation). It contains no unreachable code, so it
  compiles under strict warnings with either the default `[[noreturn]]` hook or a
  returning override.
- Host tests for exact/monotonic clock values, the 32-bit extender wrap, ms→us
  widening, the overflow predicate, the deadline bool contract, the default-hook
  compile path, and a death test for the default overflow-halt policy.
- `hardware/ClockMonotonic` validation sketch (developer tool; not host CI) that
  checks the native clock is monotonic AND advances across `loop()` batches, and
  which CI compiles on every target so the native backend is linked, not just the
  header.

### Added — M0: Repository and build skeleton

- Arduino library layout under `src/` with the public include `ArduinoAwait.h`.
- Configuration header `src/arduinoawait/config.h` exposing the V1 configuration
  surface (`ARDUINOAWAIT_MAX_TASKS`, `ARDUINOAWAIT_FRAME_POOL_BYTES`,
  `ARDUINOAWAIT_ON_ERROR`, `ARDUINOAWAIT_ENABLE_DIAGNOSTICS`,
  `ARDUINOAWAIT_ENABLE_ISR`) with overridable defaults.
- Compile-time coroutine-support checks in
  `src/arduinoawait/detail/coroutine_support.h` (fails clearly without C++20
  standard coroutines; does not rely on `__cplusplus`).
- Library version header `src/arduinoawait/version.h`.
- Arduino metadata (`library.properties`) and PlatformIO metadata
  (`library.json`).
- Host CMake test target (`CMakeLists.txt`, `test/`) with deterministic host
  tests compiled at the C++20 language floor and, where available, C++23.
- `examples/Empty/` build-skeleton sketch for target compile checks.
- CI skeleton (`.github/workflows/ci.yml`) covering host GCC/Clang C++20/C++23
  builds across `-O0`/`-O2`/`-Os`, ASan/UBSan, RP2040/RP2350 (Arm and RISC-V)
  and ESP32/ESP32-S3 example compiles, and a metadata gate
  (`.github/scripts/lint_metadata_gate.py`) that runs arduino-lint and fails on
  any error except the documented `LP012` "Arduino" name-prefix deviation.
- Project docs, `LICENSE` (MIT), and this changelog.

### Hardened after independent milestone review

- The default embedded halt (`arduinoawait::detail::halt`) now performs a
  per-iteration `volatile` access so the deterministic halt loop is preserved
  under the C++20 forward-progress rules ([intro.progress]) at every
  optimization level, without relying on the later P2809 fix.
- Added host tests, driven by portable CMake wrappers so they behave correctly
  across single- and multi-config generators and POSIX/Windows: a
  two-translation-unit ODR check (inline version constant and error-handler
  template each have one definition program-wide); a death test proving the
  default error hook terminates abnormally after reaching the hook (replacing a
  CTest `WILL_FAIL`, which does not reliably invert SIGABRT on POSIX); a
  persistent negative-compile probe that must fail *with* the coroutine-support
  diagnostic (rejecting unrelated/infrastructure build failures) and forwards
  the build configuration; and a self-test for the metadata gate.
- Reworked the metadata CI job into a documented gate
  (`.github/scripts/lint_metadata_gate.py`) that runs arduino-lint at
  specification compliance and fails on any error except the intentional
  `LP012` "Arduino" name-prefix deviation inherent to the fixed project name
  (the maintainer name-prefix remains a non-blocking warning). The gate fails
  closed on malformed, empty, non-library, schema-invalid (unknown rule
  result/level values), or internally inconsistent (summary errorCount vs.
  inspected failures) reports; the CI step installs arduino-lint correctly and
  distinguishes a linter crash from ordinary rule
  errors. Corrected the README example-build commands (`--library .`) and
  coverage wording.

No coroutine scheduling is implemented at M0.

[Unreleased]: https://github.com/ArduinoAwait/ArduinoAwait

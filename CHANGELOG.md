# Changelog

All notable changes to ArduinoAwait are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project aims to follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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

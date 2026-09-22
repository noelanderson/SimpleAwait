#pragma once

// SimpleAwait — compile-time configuration.
//
// This header defines the small, intentionally minimal configuration surface
// for SimpleAwait. Every macro may be overridden by defining it BEFORE any
// SimpleAwait header is included (for example in an application "AwaitConfig.h"
// that defines the macro and then includes <SimpleAwait.h>, or via a build
// system -D flag).
//
// Supported configuration macros (see docs/simpleawait/V1_API_CONTRACT.md §15):
//
//   SIMPLEAWAIT_MAX_TASKS          Maximum number of concurrently scheduled
//                                   tasks (scheduler slot capacity).
//   SIMPLEAWAIT_FRAME_POOL_BYTES   Total bytes reserved for the fixed coroutine
//                                   frame pool. No heap fallback is ever used.
//   SIMPLEAWAIT_ON_ERROR(error)    Deterministic error hook. Receives a
//                                   simpleawait::Error value. Must not depend on
//                                   Serial.
//   SIMPLEAWAIT_ENABLE_DIAGNOSTICS Set to 1 to compile in allocation-free
//                                   diagnostic counters/stats.
//   SIMPLEAWAIT_ENABLE_ISR         Set to 1 to compile in external/ISR context
//                                   signaling support (ThreadSafeFlag bridge).
//   SIMPLEAWAIT_CLOCK_NOW_US()     Optional advanced/testing override returning
//                                   a uint64_t microsecond count. Normal
//                                   RP2040/RP2350/ESP32 backends do not require
//                                   the user to define it.
//
// Configuration MUST be identical across all translation units; prefer defining
// overrides in one shared configuration header included everywhere the library
// is used.

#include <cstddef>
#include <cstdint>
#include <cstdlib>

// -----------------------------------------------------------------------------
// Task capacity
// -----------------------------------------------------------------------------
#ifndef SIMPLEAWAIT_MAX_TASKS
#  define SIMPLEAWAIT_MAX_TASKS 32
#endif

#if (SIMPLEAWAIT_MAX_TASKS) < 1
#  error "SIMPLEAWAIT_MAX_TASKS must be at least 1"
#endif

// -----------------------------------------------------------------------------
// Coroutine frame pool size (bytes)
// -----------------------------------------------------------------------------
#ifndef SIMPLEAWAIT_FRAME_POOL_BYTES
#  define SIMPLEAWAIT_FRAME_POOL_BYTES 4096
#endif

#if (SIMPLEAWAIT_FRAME_POOL_BYTES) < 1
#  error "SIMPLEAWAIT_FRAME_POOL_BYTES must be a positive byte count"
#endif

// -----------------------------------------------------------------------------
// Diagnostics / ISR feature toggles
// -----------------------------------------------------------------------------
#ifndef SIMPLEAWAIT_ENABLE_DIAGNOSTICS
#  define SIMPLEAWAIT_ENABLE_DIAGNOSTICS 0
#endif

#ifndef SIMPLEAWAIT_ENABLE_ISR
#  define SIMPLEAWAIT_ENABLE_ISR 0
#endif

// -----------------------------------------------------------------------------
// Deterministic error hook
// -----------------------------------------------------------------------------
// The default hook halts deterministically. On hosted builds it aborts so tests
// fail loudly; on embedded builds it spins forever, keeping the core free of any
// Serial dependency. It is a function template so the default accepts whatever
// error argument type is passed (including simpleawait::Error).
namespace simpleawait {
namespace detail {

[[noreturn]] inline void halt() noexcept {
    // The default embedded halt must be preserved on every language standard and
    // optimization level. An empty infinite loop is eligible for removal under
    // the C++20 forward-progress rules ([intro.progress]), which would let the
    // deterministic halt fall through. A per-iteration volatile access is an
    // observable side effect that keeps the loop intact without relying on the
    // later P2809 "trivial infinite loops are not UB" fix. Using a for(;;) header
    // (rather than a volatile condition) keeps the function unconditionally
    // non-returning so [[noreturn]] stays warning-clean.
    for (;;) {
        volatile unsigned sa_halt_tick = 0u;
        (void)sa_halt_tick;
    }
}

template <class ErrorType>
[[noreturn]] inline void default_error_handler(ErrorType /*error*/) noexcept {
#if defined(ARDUINO)
    halt();
#else
    ::std::abort();
#endif
}

} // namespace detail
} // namespace simpleawait

#ifndef SIMPLEAWAIT_ON_ERROR
#  define SIMPLEAWAIT_ON_ERROR(error) (::simpleawait::detail::default_error_handler((error)))
#endif

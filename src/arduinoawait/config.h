#pragma once

// ArduinoAwait — compile-time configuration.
//
// This header defines the small, intentionally minimal configuration surface
// for ArduinoAwait. Every macro may be overridden by defining it BEFORE any
// ArduinoAwait header is included (for example in an application "AwaitConfig.h"
// that defines the macro and then includes <ArduinoAwait.h>, or via a build
// system -D flag).
//
// Supported configuration macros (see docs/arduinoawait/V1_API_CONTRACT.md §15):
//
//   ARDUINOAWAIT_MAX_TASKS          Maximum number of concurrently scheduled
//                                   tasks (scheduler slot capacity).
//   ARDUINOAWAIT_FRAME_POOL_BYTES   Total bytes reserved for the fixed coroutine
//                                   frame pool. No heap fallback is ever used.
//   ARDUINOAWAIT_ON_ERROR(error)    Deterministic error hook. Receives an
//                                   arduinoawait::Error value once that enum is
//                                   introduced. Must not depend on Serial.
//   ARDUINOAWAIT_ENABLE_DIAGNOSTICS Set to 1 to compile in allocation-free
//                                   diagnostic counters/stats.
//   ARDUINOAWAIT_ENABLE_ISR         Set to 1 to compile in external/ISR context
//                                   signaling support (ThreadSafeFlag bridge).
//   ARDUINOAWAIT_CLOCK_NOW_US()     Optional advanced/testing override returning
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
#ifndef ARDUINOAWAIT_MAX_TASKS
#  define ARDUINOAWAIT_MAX_TASKS 32
#endif

#if (ARDUINOAWAIT_MAX_TASKS) < 1
#  error "ARDUINOAWAIT_MAX_TASKS must be at least 1"
#endif

// -----------------------------------------------------------------------------
// Coroutine frame pool size (bytes)
// -----------------------------------------------------------------------------
#ifndef ARDUINOAWAIT_FRAME_POOL_BYTES
#  define ARDUINOAWAIT_FRAME_POOL_BYTES 4096
#endif

#if (ARDUINOAWAIT_FRAME_POOL_BYTES) < 1
#  error "ARDUINOAWAIT_FRAME_POOL_BYTES must be a positive byte count"
#endif

// -----------------------------------------------------------------------------
// Diagnostics / ISR feature toggles
// -----------------------------------------------------------------------------
#ifndef ARDUINOAWAIT_ENABLE_DIAGNOSTICS
#  define ARDUINOAWAIT_ENABLE_DIAGNOSTICS 0
#endif

#ifndef ARDUINOAWAIT_ENABLE_ISR
#  define ARDUINOAWAIT_ENABLE_ISR 0
#endif

// -----------------------------------------------------------------------------
// Deterministic error hook
// -----------------------------------------------------------------------------
// The default hook halts deterministically. On hosted builds it aborts so tests
// fail loudly; on embedded builds it spins forever, keeping the core free of any
// Serial dependency. It is a function template so that the default is valid
// before the arduinoawait::Error enum is introduced in a later milestone and for
// any error argument type an application might pass.
namespace arduinoawait {
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
        volatile unsigned aa_halt_tick = 0u;
        (void)aa_halt_tick;
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
} // namespace arduinoawait

#ifndef ARDUINOAWAIT_ON_ERROR
#  define ARDUINOAWAIT_ON_ERROR(error) (::arduinoawait::detail::default_error_handler((error)))
#endif

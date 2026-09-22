#pragma once

// SimpleAwait — platform monotonic microsecond clock.
//
// The scheduler's internal timebase is a 64-bit monotonic microsecond count.
// ALL scheduler timing flows through detail::platform_now_us(); no other
// scheduler source file may call a platform clock primitive directly. Exactly
// one backend is selected at compile time:
//
//   SIMPLEAWAIT_CLOCK_NOW_US  advanced/testing override (e.g. host fake clock)
//   ARDUINO_ARCH_RP2040        RP2040 and RP2350 (Arm/RISC-V): time_us_64()
//   ARDUINO_ARCH_ESP32         ESP32 family: esp_timer_get_time()
//   ARDUINO (generic)          software-extended 32-bit micros() (secondary)
//   host (none of the above)   no default clock: inject via SIMPLEAWAIT_CLOCK_NOW_US
//                              (deterministic tests) or opt into a real-time
//                              adapter with SIMPLEAWAIT_HOST_REALTIME_CLOCK
//
// The target-detection macros are the ones the real cores define, verified from
// the installed Arduino-Pico (ARDUINO_ARCH_RP2040 for every RP2040/RP2350 board;
// PICO_RP2040/PICO_RP2350 distinguish the chip) and Arduino-ESP32 toolchains.

#include <cstdint>

#include "../config.h"

namespace simpleawait {
namespace detail {

using tick_t = uint64_t; // monotonic microseconds since the runtime epoch

// Widen a 32-bit microsecond source to 64 bits by counting wraps. It MUST be
// sampled often enough to observe every 32-bit wrap (~71.6 minutes); a missed
// wrap loses ~71.6 minutes. It is not interrupt- or multicore-safe on its own.
// Only the generic-Arduino compatibility backend uses this; first-class targets
// have native 64-bit clocks.
struct Micros32Extender {
    uint32_t last = 0;
    uint32_t wraps = 0;

    constexpr uint64_t extend(uint32_t now32) noexcept {
        if (now32 < last) {
            ++wraps;
        }
        last = now32;
        return (static_cast<uint64_t>(wraps) << 32) | now32;
    }
};

} // namespace detail
} // namespace simpleawait

// ---------------------------------------------------------------------------
// Backend selection (exactly one).
// ---------------------------------------------------------------------------
#if defined(SIMPLEAWAIT_CLOCK_NOW_US)

namespace simpleawait {
namespace detail {
inline uint64_t platform_now_us() noexcept {
    return static_cast<uint64_t>(SIMPLEAWAIT_CLOCK_NOW_US());
}
} // namespace detail
} // namespace simpleawait

#elif defined(ARDUINO_ARCH_RP2040)

#include <pico/time.h>
namespace simpleawait {
namespace detail {
inline uint64_t platform_now_us() noexcept {
    return ::time_us_64();
}
} // namespace detail
} // namespace simpleawait

#elif defined(ARDUINO_ARCH_ESP32)

#include <esp_timer.h>
namespace simpleawait {
namespace detail {
inline uint64_t platform_now_us() noexcept {
    return static_cast<uint64_t>(::esp_timer_get_time());
}
} // namespace detail
} // namespace simpleawait

#elif defined(ARDUINO)

#include <Arduino.h>
namespace simpleawait {
namespace detail {
// Generic Arduino compatibility backend: software-extend 32-bit micros(). This
// is secondary to the first-class targets and inherits Micros32Extender's
// sampling requirement.
inline uint64_t platform_now_us() noexcept {
    static Micros32Extender extender;
    return extender.extend(static_cast<uint32_t>(::micros()));
}
} // namespace detail
} // namespace simpleawait

#elif defined(SIMPLEAWAIT_HOST_REALTIME_CLOCK)

#include <chrono>
namespace simpleawait {
namespace detail {
// Opt-in host real-time backend (non-target): real monotonic microseconds from
// std::chrono::steady_clock. This is a benchmark/adapter convenience, never the
// default; deterministic host tests inject a fake clock via
// SIMPLEAWAIT_CLOCK_NOW_US instead. Desktop support is not broadened beyond
// deterministic host testing.
inline uint64_t platform_now_us() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}
} // namespace detail
} // namespace simpleawait

#else

namespace simpleawait {
namespace detail {
// Host with no backend selected: there is deliberately NO default host clock, so
// a test that forgets to inject one cannot silently receive nondeterministic
// real time. Deterministic host tests inject a clock via SIMPLEAWAIT_CLOCK_NOW_US;
// an explicit real-time adapter is available via SIMPLEAWAIT_HOST_REALTIME_CLOCK.
// platform_now_us() is declared but not defined here, so *using* the clock
// without providing one is a link-time error rather than silent real time.
uint64_t platform_now_us() noexcept;
} // namespace detail
} // namespace simpleawait

#endif

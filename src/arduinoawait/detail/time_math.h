#pragma once

// ArduinoAwait — deadline arithmetic on the 64-bit microsecond timebase.
//
// Duration math widens before multiplying and checks for overflow before it can
// happen. Per the V1 policy, a deadline that would overflow uint64 is a
// deterministic error (Error::deadline_overflow) routed through the configured
// error hook; silent wrap or saturation is not V1 behavior.

#include <cstdint>

#include "../config.h"
#include "../error.h"
#include "platform_clock.h" // tick_t

namespace arduinoawait {
namespace detail {

// Widen milliseconds to microseconds. A uint32 millisecond count times 1000 has
// a maximum of ~4.29e12, which fits comfortably in uint64, so this conversion
// cannot overflow.
constexpr tick_t ms_to_us(uint32_t milliseconds) noexcept {
    return static_cast<tick_t>(milliseconds) * 1000ULL;
}

// True if a + b would overflow the 64-bit microsecond timebase.
constexpr bool add_overflows(tick_t a, tick_t b) noexcept {
    return b > (UINT64_MAX - a);
}

// Compute the absolute deadline `now + duration_us` on the monotonic timebase.
// On overflow, invoke the deterministic error hook with Error::deadline_overflow
// (V1 forbids silent wrap/saturation). The default hook is [[noreturn]]; if a
// non-halting override returns, a saturated sentinel (UINT64_MAX) is returned so
// a caller never observes a wrapped deadline.
inline tick_t compute_deadline(tick_t now, tick_t duration_us) noexcept {
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4702) // unreachable code after a [[noreturn]] hook
#endif
    if (add_overflows(now, duration_us)) {
        ARDUINOAWAIT_ON_ERROR(Error::deadline_overflow);
        return UINT64_MAX;
    }
    return now + duration_us;
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
}

} // namespace detail
} // namespace arduinoawait

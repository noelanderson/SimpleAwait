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
//
// On success, writes the deadline to `out` and returns true. On overflow, it
// does NOT modify `out`, invokes the deterministic error hook with
// Error::deadline_overflow (V1 forbids silent wrap/saturation), and returns
// false. The returned bool is the unambiguous success indicator — the deadline
// is never conflated with a failure sentinel.
//
// The default error hook is [[noreturn]], so a false return is observed only
// under a non-halting override (e.g. host tests). The function is structured so
// the single trailing return is reachable via the non-overflow path; there is
// no unreachable statement after the (possibly [[noreturn]]) hook, so it
// compiles cleanly under strict warnings with either error policy.
[[nodiscard]] inline bool compute_deadline(tick_t now, tick_t duration_us,
                                           tick_t& out) noexcept {
    const bool overflow = add_overflows(now, duration_us);
    if (overflow) {
        ARDUINOAWAIT_ON_ERROR(Error::deadline_overflow);
    } else {
        out = now + duration_us;
    }
    return !overflow;
}

} // namespace detail
} // namespace arduinoawait

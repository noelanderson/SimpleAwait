#pragma once

// SimpleAwait — yield() and delay() timer awaitables (V1_API_CONTRACT §8).
//
// These suspend the current task cooperatively:
//   * yield() and delay(0)/delay_ms(0)/delay_us(0) requeue for a LATER poll() pass
//     (a fair yield point; zero duration is never an immediate await_ready success);
//   * a positive delay suspends until the 64-bit monotonic microsecond clock
//     reaches now + duration; the task is woken by a later poll()'s timer pass.
//
// Deadline overflow (now + duration exceeding uint64) is the deterministic
// Error::deadline_overflow (see detail/time_math.h). The suspend logic runs
// through the scheduler; user coroutine code is never resumed inline.

#include <coroutine>
#include <cstdint>

#include "detail/platform_clock.h" // tick_t
#include "detail/time_math.h"      // ms_to_us
#include "scheduler.h"

namespace simpleawait {

// A fair yield point: always suspends and requeues for a later poll() pass.
class YieldAwaitable {
public:
    constexpr YieldAwaitable() noexcept = default;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {
        scheduler().yield_current();
    }
    void await_resume() const noexcept {}
};

// Suspends until now + duration on the monotonic timebase (a fair yield when the
// duration is zero).
class DelayAwaitable {
public:
    explicit constexpr DelayAwaitable(detail::tick_t duration_us) noexcept
        : duration_us_(duration_us) {}

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {
        scheduler().arm_current_timer(duration_us_);
    }
    void await_resume() const noexcept {}

private:
    detail::tick_t duration_us_ = 0;
};

[[nodiscard]] inline YieldAwaitable yield() noexcept { return YieldAwaitable{}; }

[[nodiscard]] inline DelayAwaitable delay_us(uint64_t microseconds) noexcept {
    return DelayAwaitable{static_cast<detail::tick_t>(microseconds)};
}
[[nodiscard]] inline DelayAwaitable delay_ms(uint32_t milliseconds) noexcept {
    return DelayAwaitable{detail::ms_to_us(milliseconds)};
}
[[nodiscard]] inline DelayAwaitable delay(uint32_t milliseconds) noexcept {
    return delay_ms(milliseconds);
}

} // namespace simpleawait

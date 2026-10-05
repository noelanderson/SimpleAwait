#pragma once

// Optional platform wait used by poll_and_wait().
//
// ESP32 blocks the calling FreeRTOS task on a statically allocated binary
// semaphore until the nearest coroutine timer or an external ThreadSafeFlag
// signal. Other targets keep poll_and_wait() behaviorally equivalent to poll();
// a future backend may add a target-native wait without changing scheduler
// semantics or resuming coroutine code outside scheduler context.

#include <cstdint>

#include "platform_clock.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace simpleawait {
namespace detail {

// Convert a finite microsecond deadline to a tick timeout, rounding upward so a
// timer never resumes early. Explicit parameters keep the arithmetic
// deterministic and host-testable without FreeRTOS headers.
constexpr uint64_t platform_timeout_ticks(
    tick_t now,
    tick_t deadline,
    uint64_t ticks_per_second,
    uint64_t maximum_finite_ticks) noexcept {
    if (deadline <= now || ticks_per_second == 0 || maximum_finite_ticks == 0) {
        return 0;
    }

    const uint64_t remaining_us = deadline - now;
    const uint64_t whole_seconds = remaining_us / 1000000ULL;
    const uint64_t remaining_fraction_us = remaining_us % 1000000ULL;
    if (whole_seconds > maximum_finite_ticks / ticks_per_second) {
        return maximum_finite_ticks;
    }

    uint64_t ticks = whole_seconds * ticks_per_second;
    const uint64_t fractional_numerator =
        remaining_fraction_us * ticks_per_second;
    const uint64_t fractional_ticks =
        fractional_numerator / 1000000ULL +
        ((fractional_numerator % 1000000ULL) != 0 ? 1ULL : 0ULL);
    if (fractional_ticks > maximum_finite_ticks - ticks) {
        return maximum_finite_ticks;
    }
    return ticks + fractional_ticks;
}

#if defined(ARDUINO_ARCH_ESP32)

inline StaticSemaphore_t scheduler_wake_storage;
inline SemaphoreHandle_t scheduler_wake = nullptr;

// Initialize only from poll_and_wait() scheduler context, never from a global
// constructor. A ThreadSafeFlag waiter cannot first become armed until a
// scheduler pass, so preparation precedes any wake that must reach this wait.
inline void platform_prepare_scheduler_wait() noexcept {
    if (scheduler_wake == nullptr) {
        scheduler_wake = xSemaphoreCreateBinaryStatic(&scheduler_wake_storage);
    }
}

// ThreadSafeFlag::set() calls this after releasing its metadata spinlock. A
// binary semaphore intentionally preserves ThreadSafeFlag's coalescing behavior.
inline void platform_wake_scheduler() noexcept {
    if (scheduler_wake == nullptr) {
        return;
    }

    if (xPortInIsrContext()) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(scheduler_wake, &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    } else {
        xSemaphoreGive(scheduler_wake);
    }
}

inline TickType_t platform_ticks_until(tick_t now, tick_t deadline) noexcept {
    if (deadline == UINT64_MAX) {
        return portMAX_DELAY;
    }

    const uint64_t maximum_finite_ticks =
        static_cast<uint64_t>(portMAX_DELAY) - 1;
    return static_cast<TickType_t>(
        platform_timeout_ticks(
            now,
            deadline,
            configTICK_RATE_HZ,
            maximum_finite_ticks));
}

// Wait for a timer or external signal. A scheduler with neither retains a
// one-tick fallback so Arduino work after poll_and_wait() is not hidden forever.
inline void platform_wait_until(tick_t deadline, bool externally_wakeable) noexcept {
    if (scheduler_wake == nullptr ||
        (deadline == UINT64_MAX && !externally_wakeable)) {
        vTaskDelay(1);
        return;
    }

    const TickType_t timeout =
        platform_ticks_until(platform_now_us(), deadline);
    if (timeout != 0) {
        xSemaphoreTake(scheduler_wake, timeout);
    }
}

#else

inline void platform_prepare_scheduler_wait() noexcept {}
inline void platform_wake_scheduler() noexcept {}
inline void platform_wait_until(tick_t deadline, bool externally_wakeable) noexcept {
#if defined(SIMPLEAWAIT_TEST_PLATFORM_WAIT_HOOK)
    SIMPLEAWAIT_TEST_PLATFORM_WAIT_HOOK(deadline, externally_wakeable);
#else
    (void)deadline;
    (void)externally_wakeable;
#endif
}

#endif

} // namespace detail
} // namespace simpleawait

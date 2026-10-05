// poll_and_wait test — explicit idle-wait adapter preserves bounded poll passes.
//
// Host platform waiting is intentionally a no-op, so this test verifies the
// portable scheduler contract and deadline conversion. ESP32 semaphore/ISR
// behavior is covered by the FlagIRQ hardware validation sketch.

#include <cstdint>

namespace {
uint64_t g_now = 0;
int g_last_error = -1;
int g_platform_wait_count = 0;
int g_wait_count_after_reentry = -1;

void record_platform_wait(uint64_t, bool) noexcept {
    ++g_platform_wait_count;
}
} // namespace

#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define SIMPLEAWAIT_TEST_PLATFORM_WAIT_HOOK(deadline, externally_wakeable) \
    record_platform_wait((deadline), (externally_wakeable))

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::delay_ms;
using simpleawait::Error;
using simpleawait::poll_and_wait;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;
using simpleawait::ThreadSafeFlag;
using simpleawait::detail::platform_timeout_ticks;

namespace {
int g_phase = 0;

Task<void> timer_task() {
    ++g_phase;
    co_await delay_ms(5);
    ++g_phase;
}

Task<void> flag_task(ThreadSafeFlag* flag) {
    ++g_phase;
    co_await flag->wait();
    ++g_phase;
}

Task<void> reentrant_poll_and_wait_task() {
    poll_and_wait();
    g_wait_count_after_reentry = g_platform_wait_count;
    co_return;
}
} // namespace

int main() {
    constexpr uint64_t maximum = UINT32_MAX - 1ULL;
    static_assert(platform_timeout_ticks(1000, 1000, 1000, maximum) == 0);
    static_assert(platform_timeout_ticks(1000, 1001, 1000, maximum) == 1);
    static_assert(platform_timeout_ticks(1000, 2000, 1000, maximum) == 1);
    static_assert(platform_timeout_ticks(1000, 2001, 1000, maximum) == 2);
    static_assert(platform_timeout_ticks(0, 1500000, 100, maximum) == 150);
    static_assert(platform_timeout_ticks(
        0, UINT64_MAX - 1, 1000, maximum) == maximum);

    g_phase = 0;
    spawn(timer_task());
    poll_and_wait();
    SA_CHECK(g_phase == 1);
    SA_CHECK(scheduler().activeTaskCount() == 1);

    g_now = 4999;
    poll_and_wait();
    SA_CHECK(g_phase == 1);

    g_now = 5000;
    poll_and_wait();
    SA_CHECK(g_phase == 2);
    SA_CHECK(scheduler().activeTaskCount() == 0);

    ThreadSafeFlag flag;
    g_phase = 0;
    spawn(flag_task(&flag));
    poll_and_wait();
    SA_CHECK(g_phase == 1);

    flag.set();
    SA_CHECK(g_phase == 1);
    poll_and_wait();
    SA_CHECK(g_phase == 2);
    SA_CHECK(scheduler().activeTaskCount() == 0);

    g_last_error = -1;
    g_platform_wait_count = 0;
    g_wait_count_after_reentry = -1;
    spawn(reentrant_poll_and_wait_task());
    poll_and_wait();
    SA_CHECK(g_last_error == static_cast<int>(Error::scheduler_reentry));
    SA_CHECK(g_wait_count_after_reentry == 0);
    SA_CHECK(g_platform_wait_count == 1);
    SA_CHECK(scheduler().activeTaskCount() == 0);

    SA_RUN_TESTS();
}

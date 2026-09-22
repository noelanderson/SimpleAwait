// Deadline-overflow test.
//
// A positive delay whose absolute deadline (now + duration) would exceed the
// 64-bit microsecond timebase invokes the deterministic error hook with
// Error::deadline_overflow — never a silent wrap or saturation. Under this
// non-halting recording hook the task is requeued (fair-yield fallback) rather
// than lost, so it deterministically continues on a later pass.

#include <cstdint>

namespace {
int g_last_error = -1;
unsigned long long g_now = 0;
int g_after = 0;
}
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::create_task;
using simpleawait::delay_us;
using simpleawait::Error;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::Task;
using simpleawait::TaskHandle;

namespace {
Task<void> overflower() {
    co_await delay_us(1000); // overflows when now is within 1000 us of UINT64_MAX
    ++g_after;
}

Task<void> okDelay() {
    co_await delay_us(100);
    ++g_after;
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- overflow: hook fires with deadline_overflow, task is not lost ----
    g_now = UINT64_MAX - 500ULL; // now + 1000 us overflows uint64
    g_last_error = -1;
    g_after = 0;
    TaskHandle h = create_task(overflower());
    poll(); // arm timer -> overflow -> hook + requeue (fair fallback)
    SA_CHECK(g_last_error == static_cast<int>(Error::deadline_overflow));
    SA_CHECK(!h.done());       // requeued, not completed this pass
    SA_CHECK(g_after == 0);
    poll(); // requeued task resumes past the co_await and completes
    SA_CHECK(h.done());
    SA_CHECK(g_after == 1);
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- a non-overflowing delay arms normally and raises no error ----
    g_now = 1000ULL;
    g_last_error = -1;
    g_after = 0;
    TaskHandle h2 = create_task(okDelay());
    poll(); // arms a 100 us timer at deadline 1100
    SA_CHECK(g_last_error == -1); // no error
    SA_CHECK(!h2.done());
    g_now = 1100ULL;
    poll(); // due -> wakes -> completes
    SA_CHECK(h2.done());
    SA_CHECK(g_after == 1);
    SA_CHECK(sch.activeTaskCount() == 0);

    SA_RUN_TESTS();
}

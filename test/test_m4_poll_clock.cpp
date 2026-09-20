// M4 poll() clock-sampling test (MEDIUM M2).
//
// ARCHITECTURE §9 step 1: each accepted poll() pass samples the 64-bit clock
// exactly once, through the single platform abstraction. A rejected poll() (a
// reentrant nested call during a pass) must not sample. Verified with a counting
// fake clock and a recording error hook (so the reentry rejection does not halt).

#include <cstdint>

namespace {
int g_clock_reads = 0;
unsigned long long g_now = 0;
int g_last_error = -1;
}
#define ARDUINOAWAIT_CLOCK_NOW_US() (++g_clock_reads, ++g_now)
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::Task;

namespace {
Task<void> noop() { co_return; }

Task<void> reenters() {
    poll(); // rejected (scheduler_reentry): must NOT sample the clock
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();

    // No sampling before any poll().
    AA_CHECK(g_clock_reads == 0);

    // An empty poll() still samples exactly once (step 1 precedes the budget).
    poll();
    AA_CHECK(g_clock_reads == 1);

    // An occupied poll() samples exactly once.
    (void)create_task(noop());
    poll();
    AA_CHECK(g_clock_reads == 2);
    AA_CHECK(sch.activeTaskCount() == 0);

    // A rejected nested poll() (reentry) adds no sample: the outer accepted pass
    // samples once; the inner rejected call samples zero.
    g_last_error = -1;
    const int before = g_clock_reads;
    (void)create_task(reenters());
    poll();
    AA_CHECK(g_last_error == static_cast<int>(Error::scheduler_reentry));
    AA_CHECK(g_clock_reads == before + 1); // exactly one sample for the accepted pass

    AA_RUN_TESTS();
}

// Diagnostics test — Stats/stats() snapshot (V1_API_CONTRACT §14).
//
// Compiled with SIMPLEAWAIT_ENABLE_DIAGNOSTICS=1. Verifies the scheduler and
// frame-pool counters: idle is all-zero with frames free; active tasks are counted
// with frame bytes in use and peaks tracked; and after a completed cycle the live
// counts and frame bytes return to zero while the high-water marks are retained and
// no allocation failure occurred (the invariants asserted across stress cycles).

#include <cstddef>
#include <cstdint>

#define SIMPLEAWAIT_ENABLE_DIAGNOSTICS 1
namespace { unsigned long long g_now = 0; }
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::delay_us;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Stats;
using simpleawait::stats;
using simpleawait::Task;

namespace {
Task<void> sleeper(uint64_t us) {
    co_await delay_us(us); // parks on a timer
}
Task<void> spinner(int n) {
    for (int i = 0; i < n; ++i) {
        co_await simpleawait::yield();
    }
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- idle: all counts zero, all frame bytes free, no failures ----
    {
        const Stats s = stats();
        SA_CHECK(s.activeTasks == 0);
        SA_CHECK(s.readyTasks == 0);
        SA_CHECK(s.waitingTimers == 0);
        SA_CHECK(s.frameBytesUsed == 0);
        SA_CHECK(s.frameBytesFree > 0);
        SA_CHECK(s.allocationFailures == 0);
    }

    // ---- active tasks are counted; frames are in use; peaks track ----
    g_now = 0;
    spawn(sleeper(1000)); // -> waiting_timer after its first run
    spawn(spinner(3));    // -> ready (keeps yielding)
    poll();
    {
        const Stats s = stats();
        SA_CHECK(s.activeTasks == 2);
        SA_CHECK(s.peakTasks >= 2);
        SA_CHECK(s.waitingTimers == 1);              // the sleeper
        SA_CHECK(s.readyTasks >= 1);                 // the spinner requeued
        SA_CHECK(s.frameBytesUsed > 0);
        SA_CHECK(s.peakFrameBytesUsed >= s.frameBytesUsed);
        SA_CHECK(s.allocationFailures == 0);
    }

    // ---- after a completed cycle: live counts and frame bytes are zero again,
    //      high-water marks retained, no allocation failure ----
    g_now = 2000; // past the sleeper's deadline
    int guard = 0;
    while (sch.activeTaskCount() > 0 && guard++ < 100) {
        poll();
    }
    {
        const Stats s = stats();
        SA_CHECK(s.activeTasks == 0);
        SA_CHECK(s.readyTasks == 0);
        SA_CHECK(s.waitingTimers == 0);
        SA_CHECK(s.frameBytesUsed == 0);   // every frame recovered
        SA_CHECK(s.peakTasks >= 2);        // high-water mark retained
        SA_CHECK(s.peakFrameBytesUsed > 0);
        SA_CHECK(s.allocationFailures == 0);
    }

    SA_RUN_TESTS();
}

// M10 diagnostics test — Stats/stats() snapshot (V1_API_CONTRACT §14).
//
// Compiled with ARDUINOAWAIT_ENABLE_DIAGNOSTICS=1. Verifies the scheduler and
// frame-pool counters: idle is all-zero with frames free; active tasks are counted
// with frame bytes in use and peaks tracked; and after a completed cycle the live
// counts and frame bytes return to zero while the high-water marks are retained and
// no allocation failure occurred (the invariants M11 asserts across stress cycles).

#include <cstddef>
#include <cstdint>

#define ARDUINOAWAIT_ENABLE_DIAGNOSTICS 1
namespace { unsigned long long g_now = 0; }
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::delay_us;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Stats;
using arduinoawait::stats;
using arduinoawait::Task;

namespace {
Task<void> sleeper(uint64_t us) {
    co_await delay_us(us); // parks on a timer
}
Task<void> spinner(int n) {
    for (int i = 0; i < n; ++i) {
        co_await arduinoawait::yield();
    }
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- idle: all counts zero, all frame bytes free, no failures ----
    {
        const Stats s = stats();
        AA_CHECK(s.activeTasks == 0);
        AA_CHECK(s.readyTasks == 0);
        AA_CHECK(s.waitingTimers == 0);
        AA_CHECK(s.frameBytesUsed == 0);
        AA_CHECK(s.frameBytesFree > 0);
        AA_CHECK(s.allocationFailures == 0);
    }

    // ---- active tasks are counted; frames are in use; peaks track ----
    g_now = 0;
    spawn(sleeper(1000)); // -> waiting_timer after its first run
    spawn(spinner(3));    // -> ready (keeps yielding)
    poll();
    {
        const Stats s = stats();
        AA_CHECK(s.activeTasks == 2);
        AA_CHECK(s.peakTasks >= 2);
        AA_CHECK(s.waitingTimers == 1);              // the sleeper
        AA_CHECK(s.readyTasks >= 1);                 // the spinner requeued
        AA_CHECK(s.frameBytesUsed > 0);
        AA_CHECK(s.peakFrameBytesUsed >= s.frameBytesUsed);
        AA_CHECK(s.allocationFailures == 0);
    }

    // ---- after a completed cycle: live counts and frame bytes are zero again,
    //      high-water marks retained, no allocation failure (M11 invariants) ----
    g_now = 2000; // past the sleeper's deadline
    int guard = 0;
    while (sch.activeTaskCount() > 0 && guard++ < 100) {
        poll();
    }
    {
        const Stats s = stats();
        AA_CHECK(s.activeTasks == 0);
        AA_CHECK(s.readyTasks == 0);
        AA_CHECK(s.waitingTimers == 0);
        AA_CHECK(s.frameBytesUsed == 0);   // every frame recovered
        AA_CHECK(s.peakTasks >= 2);        // high-water mark retained
        AA_CHECK(s.peakFrameBytesUsed > 0);
        AA_CHECK(s.allocationFailures == 0);
    }

    AA_RUN_TESTS();
}

// M5 timer/yield test — cooperative timing.
//
// Uses a test-controlled fake clock (g_now is set explicitly, not advanced by
// reads) to deterministically verify: yield() and delay(0) suspend to a LATER
// poll() pass (no same-pass resume, fair), positive delays wake only when due,
// multiple deadlines wake in deadline order across passes, equal deadlines wake
// deterministically (slot order) in one pass, a woken task is not re-woken, and a
// task can re-arm a timer in a loop. A global new/delete canary proves the timer
// path allocates nothing on the heap.

#include <cstddef>
#include <cstdlib>

namespace { unsigned long long g_new_calls = 0; }
void* operator new(std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// Test-controlled monotonic clock: reads return g_now unchanged.
namespace { unsigned long long g_now = 0; }
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::delay;
using arduinoawait::delay_ms;
using arduinoawait::delay_us;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::TaskHandle;
using arduinoawait::yield;

namespace {
int g_yield_runs = 0;
int g_zero_runs = 0;
int g_sleeper_phase = 0;
int g_wake_order[8] = {};
int g_wake_count = 0;
int g_periodic_ticks = 0;

// One task per zero-duration public entry point. Each records a pre-await marker
// (1) then a post-await marker (2); all four must suspend to a LATER pass.
int g_zp[4] = {};
Task<void> zeroYield()   { g_zp[0] = 1; co_await yield();       g_zp[0] = 2; }
Task<void> zeroDelay()   { g_zp[1] = 1; co_await delay(0);      g_zp[1] = 2; }
Task<void> zeroDelayMs() { g_zp[2] = 1; co_await delay_ms(0);   g_zp[2] = 2; }
Task<void> zeroDelayUs() { g_zp[3] = 1; co_await delay_us(0);   g_zp[3] = 2; }

// Runs `count` iterations, yielding between each; the body advances by exactly
// one step per poll() pass.
Task<void> yielder(int count) {
    for (int i = 0; i < count; ++i) {
        ++g_yield_runs;
        co_await yield();
    }
    ++g_yield_runs;
}

// delay(0)/delay_us(0) must behave exactly like yield().
Task<void> zeroDelayer(int count) {
    for (int i = 0; i < count; ++i) {
        ++g_zero_runs;
        co_await delay_ms(0);
    }
    ++g_zero_runs;
}

Task<void> sleeper() {
    ++g_sleeper_phase;          // phase 1: before the delay
    co_await delay_ms(100);
    ++g_sleeper_phase;          // phase 2: after the delay expired
}

Task<void> waker(int label, unsigned int ms) {
    co_await delay_ms(ms);
    g_wake_order[g_wake_count++] = label;
}

Task<void> periodic(int count) {
    for (int i = 0; i < count; ++i) {
        co_await delay_ms(10);  // deadline is relative to each wake
        ++g_periodic_ticks;
    }
}
} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_new_calls;

    // ---- yield(): one resume per poll() pass (no same-pass re-run) ----
    g_now = 0;
    g_yield_runs = 0;
    TaskHandle hy = create_task(yielder(3));
    for (int pass = 1; pass <= 3; ++pass) {
        poll();
        AA_CHECK(g_yield_runs == pass); // exactly one step per pass
        AA_CHECK(!hy.done());
    }
    poll();
    AA_CHECK(g_yield_runs == 4);
    AA_CHECK(hy.done());

    // ---- delay(0) is a fair yield (equivalent to yield()) ----
    g_zero_runs = 0;
    TaskHandle hz = create_task(zeroDelayer(2));
    poll();
    AA_CHECK(g_zero_runs == 1); // suspended to a later pass, not re-run this pass
    poll();
    AA_CHECK(g_zero_runs == 2);
    poll();
    AA_CHECK(g_zero_runs == 3 && hz.done());

    // ---- every zero-duration entry point suspends to a LATER pass ----
    for (int i = 0; i < 4; ++i) {
        g_zp[i] = 0;
    }
    spawn(zeroYield());
    spawn(zeroDelay());
    spawn(zeroDelayMs());
    spawn(zeroDelayUs());
    poll(); // each runs to its pre-await marker and suspends (no same-pass resume)
    for (int i = 0; i < 4; ++i) {
        AA_CHECK(g_zp[i] == 1); // stopped at the pre-await marker
    }
    poll(); // each resumes on the later pass and completes
    for (int i = 0; i < 4; ++i) {
        AA_CHECK(g_zp[i] == 2); // post-await marker ran exactly once
    }

    // ---- positive delay: wakes only when the deadline is reached ----
    g_now = 0;
    g_sleeper_phase = 0;
    TaskHandle hs = create_task(sleeper());
    poll(); // arms a 100 ms timer; does not complete
    AA_CHECK(g_sleeper_phase == 1);
    AA_CHECK(!hs.done());
    g_now = 99999; // 99.999 ms: not yet due
    poll();
    AA_CHECK(g_sleeper_phase == 1); // no early wake
    AA_CHECK(!hs.done());
    g_now = 100000; // exactly due
    poll();
    AA_CHECK(g_sleeper_phase == 2);
    AA_CHECK(hs.done());

    // ---- multiple + equal deadlines: deadline order across passes, slot order
    //      within a pass; no early wake; no repeat wake ----
    g_now = 0;
    g_wake_count = 0;
    spawn(waker(1, 100)); // slot A, deadline 100 ms
    spawn(waker(2, 200)); // slot B, deadline 200 ms
    spawn(waker(3, 100)); // slot C, deadline 100 ms (equal to waker 1)
    poll(); // now=0: all three arm timers; none due
    AA_CHECK(g_wake_count == 0);
    g_now = 100000; // waker 1 and 3 due; waker 2 not
    poll();
    AA_CHECK(g_wake_count == 2);
    AA_CHECK(g_wake_order[0] == 1 && g_wake_order[1] == 3); // equal deadline -> slot order
    g_now = 199999;
    poll();
    AA_CHECK(g_wake_count == 2); // waker 2 not yet due
    g_now = 200000;
    poll();
    AA_CHECK(g_wake_count == 3 && g_wake_order[2] == 2);
    g_now = 500000;
    poll();
    AA_CHECK(g_wake_count == 3); // no repeat wake after completion

    // ---- multiple DISTINCT deadlines all overdue at one poll wake in DEADLINE
    //      order, not slot order (regression: earlier-slot task has the LATER
    //      deadline, so a slot-order scan would wake them backwards) ----
    g_now = 0;
    g_wake_count = 0;
    spawn(waker(1, 200)); // lower slot, LATER deadline (200 ms)
    spawn(waker(2, 100)); // higher slot, EARLIER deadline (100 ms)
    poll();               // arm both; none due
    AA_CHECK(g_wake_count == 0);
    g_now = 300000;       // both overdue in a single poll
    poll();
    AA_CHECK(g_wake_count == 2);
    AA_CHECK(g_wake_order[0] == 2 && g_wake_order[1] == 1); // 100 ms before 200 ms

    // ---- a task can re-arm a timer in a loop (relative deadlines) ----
    g_now = 0;
    g_periodic_ticks = 0;
    TaskHandle hp = create_task(periodic(3));
    poll(); // now=0: arms first 10 ms timer
    AA_CHECK(g_periodic_ticks == 0);
    g_now = 10000;
    poll();
    AA_CHECK(g_periodic_ticks == 1);
    g_now = 20000;
    poll();
    AA_CHECK(g_periodic_ticks == 2);
    g_now = 30000;
    poll();
    AA_CHECK(g_periodic_ticks == 3 && hp.done());

    AA_CHECK(sch.activeTaskCount() == 0);
    // The timer/yield path never touched the global heap.
    AA_CHECK(g_new_calls == newAtStart);

    AA_RUN_TESTS();
}

// M8 ThreadSafeFlag test — single-waiter, auto-reset, coalescing external signal.
//
// The host CriticalSection is a no-op, so these tests exercise the STATE MACHINE
// deterministically (real IRQ/multicore safety is validated on hardware): a wait
// before set() suspends and is woken only by a later poll (set() never resumes
// inline); a set() before wait() completes wait() without suspension; repeated
// set() coalesces into a single signal; and consuming a signal auto-resets it. A
// global new/delete canary proves the flag path is heap-free.

#include <cstddef>
#include <cstdlib>

namespace { unsigned long long g_new_calls = 0; }
void* operator new(std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_new_calls; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace { unsigned long long g_now = 0; }
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::ThreadSafeFlag;

namespace {
int g_phase = 0;

Task<void> waiter(ThreadSafeFlag* f) {
    ++g_phase; // 1: before the wait
    co_await f->wait();
    ++g_phase; // 2: after the signal is consumed
}
} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_new_calls;

    // ---- wait before set: set() does not resume inline; poll() resolves ----
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        spawn(waiter(&flag));
        poll(); // waiter parks on the clear flag
        AA_CHECK(g_phase == 1);
        AA_CHECK(!flag.isSet());
        flag.set(); // "external" signal
        AA_CHECK(flag.isSet());
        AA_CHECK(g_phase == 1); // set() did NOT resume the waiter inline
        poll(); // §9 step 4 resolves the pending signal -> waiter woken and runs
        AA_CHECK(g_phase == 2);
        AA_CHECK(!flag.isSet()); // consumed (auto-reset)
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- set before wait: a pending signal makes wait() complete without suspend ----
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        flag.set();
        AA_CHECK(flag.isSet());
        spawn(waiter(&flag));
        poll(); // wait() consumes the pending signal; no suspension
        AA_CHECK(g_phase == 2);
        AA_CHECK(!flag.isSet());
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- repeated set() coalesces into one signal; a later waiter is not pre-signaled ----
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        spawn(waiter(&flag));
        poll();
        AA_CHECK(g_phase == 1);
        flag.set();
        flag.set();
        flag.set(); // three sets coalesce into a single pending signal
        poll();
        AA_CHECK(g_phase == 2); // exactly one wake
        AA_CHECK(!flag.isSet());
        // A fresh waiter must not observe a leftover signal.
        g_phase = 0;
        spawn(waiter(&flag));
        poll();
        AA_CHECK(g_phase == 1); // still waiting: the coalesced signal was consumed once
        flag.set();
        poll();
        AA_CHECK(g_phase == 2);
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // The flag path allocated nothing on the global heap.
    AA_CHECK(g_new_calls == newAtStart);

    AA_RUN_TESTS();
}

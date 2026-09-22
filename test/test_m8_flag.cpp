// M8 ThreadSafeFlag test — single-waiter, auto-reset, coalescing external signal.
//
// The host CriticalSection is a no-op, so these tests exercise the STATE MACHINE
// deterministically (real IRQ/multicore safety must be validated on hardware —
// pending before V1 release): a wait before set() suspends and is woken only by a
// later poll (set() never resumes inline); a set() before wait() completes wait()
// without suspension; repeated set() coalesces into a single signal; a signal that
// arrives in the window between await_ready() and await_suspend() is not lost; the
// armed list wakes exactly the signaled flags (head/middle/tail removal); and
// consuming a signal auto-resets it. A global new/delete canary proves the flag
// path is heap-free.

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
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;
using simpleawait::ThreadSafeFlag;

namespace {
int g_phase = 0;

Task<void> waiter(ThreadSafeFlag* f) {
    ++g_phase; // 1: before the wait
    co_await f->wait();
    ++g_phase; // 2: after the signal is consumed
}

Task<void> countWaiter(ThreadSafeFlag* f, int* count) {
    co_await f->wait();
    ++(*count); // incremented only when THIS flag's waiter is woken
}

// Wraps the real flag awaiter to inject an external set() into the window BETWEEN
// await_ready() returning false and await_suspend() parking the task. This is the
// host-deterministic stand-in for a set() racing the suspend on hardware: the
// signal must not be lost — the task parks and a later poll() still wakes it.
ThreadSafeFlag* g_inject_flag = nullptr;
struct InjectAwaiter {
    ThreadSafeFlag::Awaiter inner;
    bool await_ready() noexcept {
        const bool ready = inner.await_ready();
        if (!ready) {
            g_inject_flag->set(); // signal lands after the "not ready" decision
        }
        return ready;
    }
    bool await_suspend(std::coroutine_handle<> h) noexcept { return inner.await_suspend(h); }
    void await_resume() noexcept { inner.await_resume(); }
};
Task<void> injectWaiter(ThreadSafeFlag* f) {
    ++g_phase; // 1: before the wait
    co_await InjectAwaiter{f->wait()};
    ++g_phase; // 2: woken despite the race-window signal
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
        SA_CHECK(g_phase == 1);
        SA_CHECK(!flag.isSet());
        flag.set(); // "external" signal
        SA_CHECK(flag.isSet());
        SA_CHECK(g_phase == 1); // set() did NOT resume the waiter inline
        poll(); // §9 step 4 resolves the pending signal -> waiter woken and runs
        SA_CHECK(g_phase == 2);
        SA_CHECK(!flag.isSet()); // consumed (auto-reset)
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- set before wait: a pending signal makes wait() complete without suspend ----
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        flag.set();
        SA_CHECK(flag.isSet());
        spawn(waiter(&flag));
        poll(); // wait() consumes the pending signal; no suspension
        SA_CHECK(g_phase == 2);
        SA_CHECK(!flag.isSet());
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- repeated set() coalesces into one signal; a later waiter is not pre-signaled ----
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        spawn(waiter(&flag));
        poll();
        SA_CHECK(g_phase == 1);
        flag.set();
        flag.set();
        flag.set(); // three sets coalesce into a single pending signal
        poll();
        SA_CHECK(g_phase == 2); // exactly one wake
        SA_CHECK(!flag.isSet());
        // A fresh waiter must not observe a leftover signal.
        g_phase = 0;
        spawn(waiter(&flag));
        poll();
        SA_CHECK(g_phase == 1); // still waiting: the coalesced signal was consumed once
        flag.set();
        poll();
        SA_CHECK(g_phase == 2);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- signal in the await_ready/await_suspend window is not lost ----
    // set() fires after await_ready() returns false but before the task parks. On
    // hardware this is the set()-races-suspend interleaving; here it is injected
    // deterministically. The task must still park and be woken by a later poll().
    {
        ThreadSafeFlag flag;
        g_phase = 0;
        g_inject_flag = &flag;
        spawn(injectWaiter(&flag));
        poll(); // await_ready false -> inject set() -> park (armed AND signaled)
        SA_CHECK(g_phase == 1);   // parked: the injected set() did NOT resume inline
        SA_CHECK(flag.isSet());   // signal is pending, held for resolution
        poll();                   // §9 step 4 resolves it -> waiter woken
        SA_CHECK(g_phase == 2);   // woken exactly once; no lost signal
        SA_CHECK(!flag.isSet());  // consumed (auto-reset)
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- armed list wakes exactly the signaled flags (head/middle/tail removal) ----
    // Three flags each have a parked waiter (armed list: f3 -> f2 -> f1). Signaling
    // only the middle one must wake only its waiter and splice it out of the middle
    // of the list, leaving the other two armed; the remaining two then resolve.
    {
        ThreadSafeFlag f1, f2, f3;
        int c1 = 0, c2 = 0, c3 = 0;
        spawn(countWaiter(&f1, &c1));
        spawn(countWaiter(&f2, &c2));
        spawn(countWaiter(&f3, &c3));
        poll(); // all three park
        SA_CHECK(c1 == 0 && c2 == 0 && c3 == 0);
        f2.set(); // signal the MIDDLE armed flag only
        poll();
        SA_CHECK(c1 == 0 && c2 == 1 && c3 == 0); // only f2's waiter woke
        SA_CHECK(!f2.isSet());
        f1.set(); // head of the remaining list
        f3.set(); // tail of the remaining list
        poll();
        SA_CHECK(c1 == 1 && c2 == 1 && c3 == 1); // all resolved
        SA_CHECK(!f1.isSet() && !f3.isSet());
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // The flag path allocated nothing on the global heap.
    SA_CHECK(g_new_calls == newAtStart);

    SA_RUN_TESTS();
}

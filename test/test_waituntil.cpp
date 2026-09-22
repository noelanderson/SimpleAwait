// WaitUntil test — header-defined coroutine composition over yield().
//
// A predicate that is already true completes without waiting for a change; a false
// predicate suspends at fair yield points so concurrent tasks keep running; and the
// waiter resumes once the predicate eventually becomes true. waitUntil is a pure
// composition (co_await'd as a child Task here), not a scheduler primitive.

#include <cstddef>

namespace { unsigned long long g_now = 0; }
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;
using simpleawait::waitUntil;

namespace {
bool g_pred = false;
int g_done = 0;
int g_other = 0;

Task<void> waiter() {
    co_await waitUntil([] { return g_pred; }); // predicate reads the shared flag
    ++g_done;
}
Task<void> counter(int n) {
    for (int i = 0; i < n; ++i) {
        ++g_other;
        co_await simpleawait::yield();
    }
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- an already-true predicate completes (does not wait for a change) ----
    {
        g_pred = true;
        g_done = 0;
        spawn(waiter());
        int guard = 0;
        while (g_done == 0 && guard++ < 10) {
            poll();
        }
        SA_CHECK(g_done == 1);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- a false predicate suspends fairly; other tasks keep running ----
    {
        g_pred = false;
        g_done = 0;
        g_other = 0;
        spawn(waiter());
        spawn(counter(1000));
        for (int i = 0; i < 10; ++i) {
            poll();
        }
        SA_CHECK(g_done == 0);  // still waiting: the predicate is false
        SA_CHECK(g_other >= 5); // the concurrent task advanced (fair yielding)

        // ---- the waiter resumes once the predicate becomes true ----
        g_pred = true;
        int guard = 0;
        while (g_done == 0 && guard++ < 10) {
            poll();
        }
        SA_CHECK(g_done == 1);

        // Drain the counter task so the scheduler returns to idle.
        guard = 0;
        while (sch.activeTaskCount() > 0 && guard++ < 4000) {
            poll();
        }
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    SA_RUN_TESTS();
}

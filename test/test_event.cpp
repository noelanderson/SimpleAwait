// Event test — scheduler-local manual-reset multi-waiter Event.
//
// Verifies (V1 §9 / ARCHITECTURE §15): wait() on a clear Event suspends; set()
// wakes all current waiters in FIFO order and leaves the Event set; woken tasks
// run on a LATER poll (not inline in set()); wait() on a set Event completes
// without suspension; the Event stays set until clear(); and clear() affects only
// future waits. A global new/delete canary proves the Event path is heap-free.

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

using simpleawait::Event;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;

namespace {
int g_woke[16] = {};
int g_woken = 0;

Task<void> waiter(Event* ev, int label) {
    co_await ev->wait();
    g_woke[g_woken++] = label;
}

// Records a marker, then sets the Event from WITHIN a running pass (in-pass wake).
Task<void> setterTask(Event* ev) {
    g_woke[g_woken++] = 90;
    ev->set();
    co_return;
}

// An unrelated ready task, to check that woken waiters do not jump ahead of work
// already ready when the setter runs.
Task<void> plainTask(int label) {
    g_woke[g_woken++] = label;
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_new_calls;
    Event ev;

    // ---- clear wait suspends; set wakes all FIFO; woken run on a LATER poll ----
    g_woken = 0;
    spawn(waiter(&ev, 1));
    spawn(waiter(&ev, 2));
    spawn(waiter(&ev, 3));
    poll(); // each runs to `co_await ev.wait()` and parks (the Event is clear)
    SA_CHECK(g_woken == 0);   // clear wait suspended all three
    SA_CHECK(!ev.isSet());
    ev.set();                 // latch + wake all current waiters (FIFO)
    SA_CHECK(ev.isSet());
    SA_CHECK(g_woken == 0);   // set() does not resume inline
    poll();                   // the three woken tasks run in FIFO order
    SA_CHECK(g_woken == 3);
    SA_CHECK(g_woke[0] == 1 && g_woke[1] == 2 && g_woke[2] == 3);

    // ---- wait while set does not suspend; the Event remains set ----
    g_woken = 0;
    spawn(waiter(&ev, 4));
    poll(); // ev is set -> wait() completes without suspension, in one poll
    SA_CHECK(g_woken == 1 && g_woke[0] == 4);
    SA_CHECK(ev.isSet()); // remains set until clear()

    // ---- clear affects only future waits ----
    ev.clear();
    SA_CHECK(!ev.isSet());
    g_woken = 0;
    spawn(waiter(&ev, 5));
    poll(); // ev is clear again -> suspends
    SA_CHECK(g_woken == 0);
    ev.set();
    poll();
    SA_CHECK(g_woken == 1 && g_woke[0] == 5);

    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- in-pass set() leaves woken work for a LATER pass (preserving the
    //      already-ready FIFO order), and clear() after set() cannot revoke an
    //      already-issued wake ----
    {
        Event ev2;
        g_woken = 0;
        spawn(waiter(&ev2, 1));
        spawn(waiter(&ev2, 2));
        poll(); // both park on the clear Event
        SA_CHECK(g_woken == 0);
        spawn(setterTask(&ev2)); // sets ev2 during its own run
        spawn(plainTask(80));    // unrelated ready task queued after the setter
        poll(); // setter (90) then plain (80) run; the waiters it woke do NOT run
                // this pass (queued after the budget snapshot)
        SA_CHECK(g_woken == 2 && g_woke[0] == 90 && g_woke[1] == 80);
        SA_CHECK(ev2.isSet());
        ev2.clear(); // clearing after set() must not revoke the already-issued wakes
        SA_CHECK(!ev2.isSet());
        poll(); // the woken waiters now run, in FIFO order
        SA_CHECK(g_woken == 4 && g_woke[2] == 1 && g_woke[3] == 2);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // The Event path allocated nothing on the global heap.
    SA_CHECK(g_new_calls == newAtStart);
    // ev has no waiters here; its destructor at scope end raises no error.

    SA_RUN_TESTS();
}

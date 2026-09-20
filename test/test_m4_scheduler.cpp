// M4 scheduler test — core cooperative scheduling.
//
// Verifies explicit scheduling and detached spawn, FIFO run order, the bounded
// pass budget (a task created during a pass runs on a LATER pass, and no task
// runs twice in one pass), current_task() inside vs outside a task, and that no
// coroutine frame or scheduler metadata touches the global heap.

#include <cstddef>
#include <cstdlib>

namespace {
unsigned long long g_global_new_calls = 0;
}

void* operator new(std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::current_task;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::TaskHandle;

namespace {
int g_order[64];
int g_count = 0;
bool g_current_valid_in_task = false;

Task<void> recorder(int label) {
    g_order[g_count++] = label;
    co_return;
}

// Records that it ran, then creates another task DURING the pass.
Task<void> spawner() {
    g_order[g_count++] = 50;
    spawn(recorder(99));
    co_return;
}

Task<void> checks_current() {
    g_current_valid_in_task = current_task().valid();
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_global_new_calls;
    AA_CHECK(sch.activeTaskCount() == 0);

    // ---- explicit scheduling: create_task readies; poll runs it ----
    {
        g_count = 0;
        TaskHandle h = create_task(recorder(1));
        AA_CHECK(h.valid());
        AA_CHECK(!h.done());              // not run yet
        AA_CHECK(sch.hasReadyTasks());
        AA_CHECK(sch.activeTaskCount() == 1);
        AA_CHECK(g_count == 0);           // lazy: body not run before poll

        poll();
        AA_CHECK(g_count == 1 && g_order[0] == 1);
        AA_CHECK(h.done());               // completed tombstone
        AA_CHECK(sch.activeTaskCount() == 0);
        AA_CHECK(!sch.hasReadyTasks());
    }

    // ---- spawn is detached but still runs ----
    {
        g_count = 0;
        spawn(recorder(7));
        AA_CHECK(sch.activeTaskCount() == 1);
        poll();
        AA_CHECK(g_count == 1 && g_order[0] == 7);
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- FIFO run order ----
    {
        g_count = 0;
        spawn(recorder(10));
        spawn(recorder(20));
        spawn(recorder(30));
        poll();
        AA_CHECK(g_count == 3);
        AA_CHECK(g_order[0] == 10 && g_order[1] == 20 && g_order[2] == 30);
    }

    // ---- bounded pass: a task created during the pass runs on the NEXT pass ----
    {
        g_count = 0;
        spawn(spawner());
        poll(); // spawner runs (records 50) and creates recorder(99)
        AA_CHECK(g_count == 1 && g_order[0] == 50); // recorder(99) did NOT run this pass
        AA_CHECK(sch.hasReadyTasks());              // it is pending
        poll(); // now recorder(99) runs
        AA_CHECK(g_count == 2 && g_order[1] == 99);
        AA_CHECK(!sch.hasReadyTasks());
    }

    // ---- current_task() inside vs outside a task ----
    {
        AA_CHECK(!current_task().valid()); // outside any task
        g_current_valid_in_task = false;
        spawn(checks_current());
        poll();
        AA_CHECK(g_current_valid_in_task); // valid while the task ran
        AA_CHECK(!current_task().valid()); // invalid again afterward
    }

    // No coroutine frame or scheduler node ever used the global heap.
    AA_CHECK(g_global_new_calls == newAtStart);
    AA_CHECK(sch.activeTaskCount() == 0);

    AA_RUN_TESTS();
}

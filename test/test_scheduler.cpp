// Scheduler test — core cooperative scheduling.
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

// The scheduler samples the 64-bit clock once per poll() pass (ARCHITECTURE §9
// step 1); inject a deterministic, allocation-free fake so the no-heap canary
// below stays valid.
namespace { unsigned long long g_fake_now_us = 0; }
#define SIMPLEAWAIT_CLOCK_NOW_US() (++g_fake_now_us)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::create_task;
using simpleawait::current_task;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;
using simpleawait::TaskHandle;

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
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- explicit scheduling: create_task readies; poll runs it ----
    {
        g_count = 0;
        TaskHandle h = create_task(recorder(1));
        SA_CHECK(h.valid());
        SA_CHECK(!h.done());              // not run yet
        SA_CHECK(sch.hasReadyTasks());
        SA_CHECK(sch.activeTaskCount() == 1);
        SA_CHECK(g_count == 0);           // lazy: body not run before poll

        poll();
        SA_CHECK(g_count == 1 && g_order[0] == 1);
        SA_CHECK(h.done());               // completed tombstone
        SA_CHECK(sch.activeTaskCount() == 0);
        SA_CHECK(!sch.hasReadyTasks());
    }

    // ---- spawn is detached but still runs ----
    {
        g_count = 0;
        spawn(recorder(7));
        SA_CHECK(sch.activeTaskCount() == 1);
        poll();
        SA_CHECK(g_count == 1 && g_order[0] == 7);
        SA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- FIFO run order ----
    {
        g_count = 0;
        spawn(recorder(10));
        spawn(recorder(20));
        spawn(recorder(30));
        poll();
        SA_CHECK(g_count == 3);
        SA_CHECK(g_order[0] == 10 && g_order[1] == 20 && g_order[2] == 30);
    }

    // ---- bounded pass: a task created during the pass runs on the NEXT pass ----
    {
        g_count = 0;
        spawn(spawner());
        poll(); // spawner runs (records 50) and creates recorder(99)
        SA_CHECK(g_count == 1 && g_order[0] == 50); // recorder(99) did NOT run this pass
        SA_CHECK(sch.hasReadyTasks());              // it is pending
        poll(); // now recorder(99) runs
        SA_CHECK(g_count == 2 && g_order[1] == 99);
        SA_CHECK(!sch.hasReadyTasks());
    }

    // ---- current_task() inside vs outside a task ----
    {
        SA_CHECK(!current_task().valid()); // outside any task
        g_current_valid_in_task = false;
        spawn(checks_current());
        poll();
        SA_CHECK(g_current_valid_in_task); // valid while the task ran
        SA_CHECK(!current_task().valid()); // invalid again afterward
    }

    // No coroutine frame or scheduler node ever used the global heap.
    SA_CHECK(g_global_new_calls == newAtStart);
    SA_CHECK(sch.activeTaskCount() == 0);

    SA_RUN_TESTS();
}

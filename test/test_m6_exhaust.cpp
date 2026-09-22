// M6 child-await slot-exhaustion test.
//
// With a single task slot the parent occupies it, so awaiting a child cannot
// obtain a slot. The scheduler reports task_limit, releases the child frame
// (the child body never runs), and requeues the parent so it is not lost.

#include <cstdint>

#define SIMPLEAWAIT_MAX_TASKS 1

namespace { int g_last_error = -1; }
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define SIMPLEAWAIT_CLOCK_NOW_US() (0ull)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::create_task;
using simpleawait::Error;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::Task;
using simpleawait::TaskHandle;

namespace {
int g_child_runs = 0;
int g_after = 0;

Task<void> exhaustChild() {
    ++g_child_runs; // must NOT run: no slot, frame released before first resume
    co_return;
}

Task<void> exhaustParent() {
    co_await exhaustChild(); // no free slot for the child -> task_limit
    ++g_after;               // parent continues (degraded) after the hook fires
}
} // namespace

int main() {
    auto& sch = scheduler();

    g_last_error = -1;
    g_child_runs = 0;
    g_after = 0;
    TaskHandle h = create_task(exhaustParent()); // occupies the only slot
    int guard = 0;
    while (!h.done() && guard++ < 20) {
        poll();
    }
    SA_CHECK(h.done());
    SA_CHECK(g_last_error == static_cast<int>(Error::task_limit));
    SA_CHECK(g_child_runs == 0); // child never ran; its frame was released
    SA_CHECK(g_after == 1);      // parent was requeued, not lost
    SA_CHECK(sch.activeTaskCount() == 0);

    SA_RUN_TESTS();
}

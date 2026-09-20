// M4 scheduler errors/identity test.
//
// Uses a single-slot scheduler and the recording error hook to verify slot
// exhaustion (task_limit), generation-safe stale handles (a slot reuse bumps the
// generation and invalidates older handles), and the scheduler reentry guard
// (poll() called from within a task -> scheduler_reentry).

#include <cstdint>

#define ARDUINOAWAIT_MAX_TASKS 1

namespace {
int g_last_error = -1;
}
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::Task;
using arduinoawait::TaskHandle;

namespace {
Task<void> noop() { co_return; }

Task<void> reenters() {
    poll(); // illegal: poll() from within a scheduler pass
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- slot exhaustion: the single slot fills, the next create fails ----
    g_last_error = -1;
    TaskHandle h1 = create_task(noop());
    AA_CHECK(h1.valid());
    AA_CHECK(sch.activeTaskCount() == 1);

    TaskHandle overflow = create_task(noop()); // no free slot -> task_limit
    AA_CHECK(!overflow.valid());
    AA_CHECK(g_last_error == static_cast<int>(Error::task_limit));

    poll(); // h1 completes -> tombstone (handle stays valid/done until reuse)
    AA_CHECK(h1.valid());
    AA_CHECK(h1.done());
    AA_CHECK(sch.activeTaskCount() == 0);

    // ---- slot reuse bumps generation; the stale handle becomes invalid ----
    TaskHandle h2 = create_task(noop()); // reuses the only slot -> generation++
    AA_CHECK(h2.valid());
    AA_CHECK(h1.id().slot == h2.id().slot);         // same slot...
    AA_CHECK(h1.id().generation != h2.id().generation); // ...new generation
    AA_CHECK(!h1.valid());                           // stale handle now invalid
    AA_CHECK(!h1.done());
    poll();
    AA_CHECK(h2.done());

    // ---- reentry guard: poll() from within a task is rejected ----
    g_last_error = -1;
    (void)create_task(reenters());
    poll(); // the inner poll() must trip scheduler_reentry
    AA_CHECK(g_last_error == static_cast<int>(Error::scheduler_reentry));
    AA_CHECK(sch.activeTaskCount() == 0); // the task still completed

    AA_RUN_TESTS();
}

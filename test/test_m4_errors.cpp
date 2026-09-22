// M4 scheduler errors/identity test.
//
// Uses a single-slot scheduler and the recording error hook to verify slot
// exhaustion (task_limit), generation-safe stale handles (a slot reuse bumps the
// generation and invalidates older handles), and the scheduler reentry guard
// (poll() called from within a task -> scheduler_reentry).

#include <cstdint>
#include <utility>

#define SIMPLEAWAIT_MAX_TASKS 1

namespace {
int g_last_error = -1;
unsigned long long g_fake_now_us = 0;
}
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define SIMPLEAWAIT_CLOCK_NOW_US() (++g_fake_now_us)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::create_task;
using simpleawait::Error;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::Task;
using simpleawait::TaskHandle;
using simpleawait::detail::force_slot_generation;

namespace {
Task<void> noop() { co_return; }

Task<void> reenters() {
    poll(); // illegal: poll() from within a scheduler pass
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- empty / moved-from Task is rejected with invalid_task ----
    g_last_error = -1;
    TaskHandle empty = create_task(Task<void>{}); // default-constructed: owns nothing
    SA_CHECK(!empty.valid());
    SA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
    SA_CHECK(sch.activeTaskCount() == 0); // no slot consumed by a rejected task
    {
        Task<void> a = noop();
        Task<void> b = std::move(a); // a is now moved-from (owns nothing)
        g_last_error = -1;
        TaskHandle movedFrom = create_task(std::move(a));
        SA_CHECK(!movedFrom.valid());
        SA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        SA_CHECK(sch.activeTaskCount() == 0);
        // b still owns its frame; it is released when b leaves this scope.
    }

    // ---- slot exhaustion: the single slot fills, the next create fails ----
    g_last_error = -1;
    TaskHandle h1 = create_task(noop());
    SA_CHECK(h1.valid());
    SA_CHECK(sch.activeTaskCount() == 1);

    TaskHandle overflow = create_task(noop()); // no free slot -> task_limit
    SA_CHECK(!overflow.valid());
    SA_CHECK(g_last_error == static_cast<int>(Error::task_limit));

    poll(); // h1 completes -> tombstone (handle stays valid/done until reuse)
    SA_CHECK(h1.valid());
    SA_CHECK(h1.done());
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- slot reuse bumps generation; the stale handle becomes invalid ----
    TaskHandle h2 = create_task(noop()); // reuses the only slot -> generation++
    SA_CHECK(h2.valid());
    SA_CHECK(h1.id().slot == h2.id().slot);         // same slot...
    SA_CHECK(h1.id().generation != h2.id().generation); // ...new generation
    SA_CHECK(!h1.valid());                           // stale handle now invalid
    SA_CHECK(!h1.done());
    poll();
    SA_CHECK(h2.done());

    // ---- reentry guard: poll() from within a task is rejected ----
    g_last_error = -1;
    (void)create_task(reenters());
    poll(); // the inner poll() must trip scheduler_reentry
    SA_CHECK(g_last_error == static_cast<int>(Error::scheduler_reentry));
    SA_CHECK(sch.activeTaskCount() == 0); // the task still completed

    // ---- generation retirement at the exact uint32 boundary: no wrap (H2) ----
    // Drive the single slot to UINT32_MAX-1, then prove: (a) one more acquisition
    // advances it to a LIVE UINT32_MAX generation; (b) the slot is then retired —
    // REPEATED scheduling attempts are all rejected without touching the
    // generation (no wrap to 0, no reuse); (c) the max-generation handle keeps its
    // identity and an older handle is never resurrected.
    g_last_error = -1;
    TaskHandle older = create_task(noop());
    poll(); // older completes -> tombstone
    SA_CHECK(older.done());
    const simpleawait::TaskSlot genSlot = older.id().slot;

    force_slot_generation(sch, genSlot, UINT32_MAX - 1u); // one below the boundary
    SA_CHECK(!older.valid());                             // older generation no longer matches
    SA_CHECK(!older.done());

    TaskHandle atMax = create_task(noop()); // completed-slot reuse advances gen -> UINT32_MAX
    SA_CHECK(atMax.valid());
    SA_CHECK(atMax.id().generation == UINT32_MAX);        // MAX is a valid LIVE generation
    SA_CHECK(atMax.id().slot == genSlot);
    poll(); // atMax completes -> tombstone at generation UINT32_MAX
    SA_CHECK(atMax.done());

    // Retired: multiple further attempts must ALL fail as task_limit, leaving the
    // generation untouched (no wrap, no resurrection).
    for (int attempt = 0; attempt < 3; ++attempt) {
        g_last_error = -1;
        TaskHandle rejected = create_task(noop());
        SA_CHECK(!rejected.valid());
        SA_CHECK(g_last_error == static_cast<int>(Error::task_limit));
        SA_CHECK(atMax.valid());  // max-generation handle still identifies its completed task
        SA_CHECK(atMax.done());
        SA_CHECK(!older.valid()); // the older handle is never resurrected
    }
    SA_CHECK(sch.activeTaskCount() == 0);

    SA_RUN_TESTS();
}

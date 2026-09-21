// M4 scheduler errors/identity test.
//
// Uses a single-slot scheduler and the recording error hook to verify slot
// exhaustion (task_limit), generation-safe stale handles (a slot reuse bumps the
// generation and invalidates older handles), and the scheduler reentry guard
// (poll() called from within a task -> scheduler_reentry).

#include <cstdint>
#include <utility>

#define ARDUINOAWAIT_MAX_TASKS 1

namespace {
int g_last_error = -1;
unsigned long long g_fake_now_us = 0;
}
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define ARDUINOAWAIT_CLOCK_NOW_US() (++g_fake_now_us)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::Task;
using arduinoawait::TaskHandle;
using arduinoawait::detail::force_slot_generation;

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
    AA_CHECK(!empty.valid());
    AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
    AA_CHECK(sch.activeTaskCount() == 0); // no slot consumed by a rejected task
    {
        Task<void> a = noop();
        Task<void> b = std::move(a); // a is now moved-from (owns nothing)
        g_last_error = -1;
        TaskHandle movedFrom = create_task(std::move(a));
        AA_CHECK(!movedFrom.valid());
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(sch.activeTaskCount() == 0);
        // b still owns its frame; it is released when b leaves this scope.
    }

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

    // ---- generation retirement at the exact uint32 boundary: no wrap (H2) ----
    // Drive the single slot to UINT32_MAX-1, then prove: (a) one more acquisition
    // advances it to a LIVE UINT32_MAX generation; (b) the slot is then retired —
    // REPEATED scheduling attempts are all rejected without touching the
    // generation (no wrap to 0, no reuse); (c) the max-generation handle keeps its
    // identity and an older handle is never resurrected.
    g_last_error = -1;
    TaskHandle older = create_task(noop());
    poll(); // older completes -> tombstone
    AA_CHECK(older.done());
    const arduinoawait::TaskSlot genSlot = older.id().slot;

    force_slot_generation(sch, genSlot, UINT32_MAX - 1u); // one below the boundary
    AA_CHECK(!older.valid());                             // older generation no longer matches
    AA_CHECK(!older.done());

    TaskHandle atMax = create_task(noop()); // completed-slot reuse advances gen -> UINT32_MAX
    AA_CHECK(atMax.valid());
    AA_CHECK(atMax.id().generation == UINT32_MAX);        // MAX is a valid LIVE generation
    AA_CHECK(atMax.id().slot == genSlot);
    poll(); // atMax completes -> tombstone at generation UINT32_MAX
    AA_CHECK(atMax.done());

    // Retired: multiple further attempts must ALL fail as task_limit, leaving the
    // generation untouched (no wrap, no resurrection).
    for (int attempt = 0; attempt < 3; ++attempt) {
        g_last_error = -1;
        TaskHandle rejected = create_task(noop());
        AA_CHECK(!rejected.valid());
        AA_CHECK(g_last_error == static_cast<int>(Error::task_limit));
        AA_CHECK(atMax.valid());  // max-generation handle still identifies its completed task
        AA_CHECK(atMax.done());
        AA_CHECK(!older.valid()); // the older handle is never resurrected
    }
    AA_CHECK(sch.activeTaskCount() == 0);

    AA_RUN_TESTS();
}

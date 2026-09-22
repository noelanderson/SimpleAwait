// M6 parent/child await test — sequential child await.
//
// Verifies (ARCHITECTURE §19): the parent does not resume before the child
// completes; the parent resumes on a LATER poll pass (never inline); nested
// children compose; a child may itself suspend on a timer while the parent waits;
// and frames fully recover after nested-child stress (child destroyed exactly
// once). A test-controlled fake clock drives the timer-child case, and a global
// new/delete canary proves the child-await path never touches the heap.

#include <cstddef>
#include <cstdlib>
#include <utility>

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

using simpleawait::create_task;
using simpleawait::delay_ms;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::Task;
using simpleawait::TaskHandle;
using simpleawait::detail::frame_pool;

namespace {
int g_seq[32] = {};
int g_seqn = 0;

void mark(int v) { g_seq[g_seqn++] = v; }

// --- basic parent/child ---
Task<void> child() {
    mark(1);
    co_return;
}
Task<void> parent() {
    mark(0);
    co_await child();
    mark(2);
}

// --- nested: parent -> child -> grandchild ---
Task<void> grandchild() { mark(10); co_return; }
Task<void> childNested() {
    mark(11);
    co_await grandchild();
    mark(12);
}
Task<void> parentNested() {
    mark(13);
    co_await childNested();
    mark(14);
}

// --- child suspends on a timer while the parent waits ---
Task<void> timerChild() {
    mark(20);
    co_await delay_ms(100);
    mark(21);
}
Task<void> timerParent() {
    mark(22);
    co_await timerChild();
    mark(23);
}
} // namespace

int main() {
    auto& sch = scheduler();
    const unsigned long long newAtStart = g_new_calls;

    // ---- parent does not resume before child completes; resumes a later pass ----
    g_now = 0;
    g_seqn = 0;
    TaskHandle hp = create_task(parent());
    poll(); // parent runs to `co_await child()` and suspends (waiting_child)
    SA_CHECK(g_seqn == 1 && g_seq[0] == 0); // only the parent's pre-await mark
    SA_CHECK(!hp.done());
    poll(); // child runs and completes; parent enqueued but NOT resumed this pass
    SA_CHECK(g_seqn == 2 && g_seq[1] == 1); // child ran
    SA_CHECK(!hp.done());                   // parent still not resumed (later poll)
    poll(); // parent resumes after the child
    SA_CHECK(g_seqn == 3 && g_seq[2] == 2);
    SA_CHECK(hp.done());
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- nested children compose in the correct order ----
    g_seqn = 0;
    TaskHandle hn = create_task(parentNested());
    int guard = 0;
    while (!hn.done() && guard++ < 20) {
        poll();
    }
    SA_CHECK(hn.done());
    SA_CHECK(g_seqn == 5);
    SA_CHECK(g_seq[0] == 13 && g_seq[1] == 11 && g_seq[2] == 10 &&
             g_seq[3] == 12 && g_seq[4] == 14);
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- a child may suspend on a timer while the parent waits ----
    g_now = 0;
    g_seqn = 0;
    TaskHandle ht = create_task(timerParent());
    poll(); // parent -> await child
    poll(); // child runs -> marks 20 -> arms a 100 ms timer
    SA_CHECK(g_seqn == 2 && g_seq[0] == 22 && g_seq[1] == 20);
    g_now = 50000;
    poll(); // not due: child sleeps, parent still waiting
    SA_CHECK(g_seqn == 2);
    SA_CHECK(!ht.done());
    g_now = 100000;
    poll(); // child wakes -> marks 21 -> completes -> enqueues parent
    SA_CHECK(g_seqn == 3 && g_seq[2] == 21);
    SA_CHECK(!ht.done());
    poll(); // parent resumes -> marks 23
    SA_CHECK(g_seqn == 4 && g_seq[3] == 23);
    SA_CHECK(ht.done());
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- full frame recovery after nested-child stress (gate) ----
    const size_t poolBefore = frame_pool().bytesUsed();
    for (int rep = 0; rep < 50; ++rep) {
        g_seqn = 0;
        TaskHandle h = create_task(parentNested());
        int g = 0;
        while (!h.done() && g++ < 20) {
            poll();
        }
        SA_CHECK(h.done());
        SA_CHECK(g_seqn == 5);
    }
    SA_CHECK(frame_pool().bytesUsed() == poolBefore); // every frame recovered
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- an extracted-but-unawaited awaiter releases the child frame (no leak) ----
    {
        const size_t poolBase = frame_pool().bytesUsed();
        {
            Task<void> t = child();
            auto awaiter = std::move(t).operator co_await(); // extracts the child frame
            (void)awaiter; // never awaited: the awaiter destructor must free the frame
        }
        SA_CHECK(frame_pool().bytesUsed() == poolBase); // frame recovered, not leaked
    }

    // No child-await frame or scheduler node ever touched the global heap.
    SA_CHECK(g_new_calls == newAtStart);

    SA_RUN_TESTS();
}

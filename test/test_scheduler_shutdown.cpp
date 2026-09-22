// Scheduler shutdown regression.
//
// Destroying a suspended coroutine frame runs its by-value parameter
// destructors. If such a destructor reenters the scheduler during Scheduler
// teardown, a naive destructor could resume a frame it is destroying or double
// free it, and could accept new work into an already-visited slot (leak).
//
// This test schedules — but never polls — a task whose by-value parameter's
// destructor calls poll() and create_task(). The task is left pending, so the
// process-wide scheduler singleton destroys its frame at program exit. A
// namespace-scope sentinel (constructed before main, hence destroyed AFTER the
// singleton) then verifies teardown was clean: no error hook fired (no double
// free / internal_error), the body never ran (no resume during destruction), the
// reentrant parameter destructor ran exactly once, and scheduling was refused
// during teardown.

#include <cstdio>
#include <cstdlib>

namespace {
int g_errors = 0;          // any error-hook invocation (esp. internal_error)
int g_body_runs = 0;       // reentrant task body executions (must stay 0)
int g_param_dtors = 0;     // live by-value parameter destructions (must be 1)
int g_teardown_refused = 0; // create_task() during teardown returned invalid (must be 1)
int g_teardown_polls = 0;  // poll() calls made during teardown (must be >= 1)
int g_detach_observed = 0; // dying task seen as a completed tombstone in the reentrant dtor (must be 1)
}
#define SIMPLEAWAIT_ON_ERROR(error) (void)((g_errors += 1), static_cast<int>(error))
#define SIMPLEAWAIT_CLOCK_NOW_US() (0ull)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::create_task;
using simpleawait::poll;
using simpleawait::Task;
using simpleawait::TaskHandle;

namespace {

// Handle to the pending scheduled task, observed from the reentrant destructor.
TaskHandle g_scheduled;

Task<void> idle() { co_return; }

// Move-only. The default member initializer makes every freshly constructed
// instance "live"; a move steals liveness from its source. Exactly one instance
// (the one living in the coroutine frame) is live, and only it reenters.
struct Reentrant {
    bool live = true;
    Reentrant() = default;
    Reentrant(Reentrant&& other) noexcept { other.live = false; }
    Reentrant& operator=(Reentrant&&) = delete;
    ~Reentrant() {
        if (!live) {
            return;
        }
        ++g_param_dtors;
        // Detach-before-destroy: at this point the dying scheduled task must
        // already be a completed tombstone (valid() AND done() both hold). If the
        // frame were destroyed BEFORE its slot was detached, done() would be false
        // here (the slot would still be ready and scheduler-owned).
        if (g_scheduled.valid() && g_scheduled.done()) {
            ++g_detach_observed;
        }
        poll();                 // must be a no-op during teardown (no reentry, no crash)
        ++g_teardown_polls;
        TaskHandle h = create_task(idle()); // must be refused during teardown
        if (!h.valid()) {
            ++g_teardown_refused;
        }
    }
};

Task<void> reentrantTask(Reentrant /*param*/) {
    ++g_body_runs; // must NOT run: the task is never resumed
    co_return;
}

// Constructed before main (dynamic init), therefore destroyed AFTER the
// function-local static scheduler/pool singletons. Validates teardown.
struct FinalCheck {
    ~FinalCheck() {
        const bool ok = (g_errors == 0) && (g_body_runs == 0) &&
                        (g_param_dtors == 1) && (g_teardown_refused == 1) &&
                        (g_teardown_polls >= 1) && (g_detach_observed == 1);
        if (!ok) {
            std::fprintf(stderr,
                         "shutdown FAIL: errors=%d body=%d paramdtors=%d refused=%d polls=%d detach=%d\n",
                         g_errors, g_body_runs, g_param_dtors, g_teardown_refused,
                         g_teardown_polls, g_detach_observed);
            std::_Exit(70);
        }
        std::fprintf(stderr, "shutdown teardown OK\n");
    }
} g_final_check;

} // namespace

int main() {
    // Schedule the reentrant task but DO NOT poll it: it stays pending in a slot,
    // so its frame is destroyed by the scheduler singleton at program exit.
    TaskHandle h = create_task(reentrantTask(Reentrant{}));
    SA_CHECK(h.valid());
    SA_CHECK(!h.done()); // never polled
    g_scheduled = h;     // observed from the reentrant destructor at teardown

    SA_RUN_TESTS(); // returns 0; FinalCheck runs at exit and enforces clean teardown
}

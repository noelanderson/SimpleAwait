// Child-await error test — a second await of a consumed Task fails
// deterministically (task_awaited_twice) without suspending or hanging.

#include <coroutine>
#include <cstdint>
#include <utility>

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
int g_after_double = 0;
int g_foreign_after = 0;

Task<void> childOk() {
    ++g_child_runs;
    co_return;
}

Task<void> doubleAwait() {
    Task<void> t = childOk();
    co_await std::move(t); // first await consumes t and runs the child
    co_await std::move(t); // second await: t is empty -> task_awaited_twice
    ++g_after_double;      // reached: the failed await neither suspends nor hangs
}

// A foreign (non-SimpleAwait) coroutine that runs eagerly and self-destroys. It
// awaits an SimpleAwait Task while NO scheduler pass is active (current_ is
// null), which must fail deterministically instead of stranding the caller.
struct EagerTask {
    struct promise_type {
        EagerTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
};

EagerTask foreignParent() {
    co_await childOk(); // outside poll(): no running parent -> invalid_task, no strand
    ++g_foreign_after;  // reached: the caller resumes rather than hanging forever
}

int g_nested_after = 0;
int g_outer_after = 0;

// A foreign coroutine invoked synchronously from inside a running SimpleAwait
// task. current_ is non-null (the outer task), but the awaiting coroutine is THIS
// foreign one, so the await must be rejected rather than attached to the outer.
EagerTask nestedForeign() {
    co_await childOk();
    ++g_nested_after; // reached: the rejected foreign await lets the caller continue
}

Task<void> outerTask() {
    nestedForeign(); // runs eagerly while outerTask is the currently running task
    ++g_outer_after;
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();

    g_last_error = -1;
    g_child_runs = 0;
    g_after_double = 0;
    TaskHandle h = create_task(doubleAwait());
    int guard = 0;
    while (!h.done() && guard++ < 20) {
        poll();
    }
    SA_CHECK(h.done());
    SA_CHECK(g_child_runs == 1); // child ran exactly once (the first await)
    SA_CHECK(g_last_error == static_cast<int>(Error::task_awaited_twice));
    SA_CHECK(g_after_double == 1); // coroutine continued past the failed second await
    SA_CHECK(sch.activeTaskCount() == 0);

    // ---- a foreign coroutine awaiting a Task outside poll() must not hang ----
    g_last_error = -1;
    g_child_runs = 0;
    g_foreign_after = 0;
    foreignParent(); // runs eagerly to completion; must resume, not strand
    SA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
    SA_CHECK(g_foreign_after == 1); // caller resumed (not stranded forever)
    SA_CHECK(g_child_runs == 0);    // child never ran; its frame was released
    SA_CHECK(sch.activeTaskCount() == 0); // nothing left scheduled

    // ---- a foreign await NESTED inside a running task is rejected, not misattached ----
    g_last_error = -1;
    g_child_runs = 0;
    g_nested_after = 0;
    g_outer_after = 0;
    TaskHandle ho = create_task(outerTask());
    int g2 = 0;
    while (!ho.done() && g2++ < 20) {
        poll();
    }
    SA_CHECK(ho.done());
    SA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
    SA_CHECK(g_nested_after == 1); // foreign continuation ran (not stranded)
    SA_CHECK(g_child_runs == 0);   // child rejected, never ran
    SA_CHECK(g_outer_after == 1);  // outer task unaffected, completed normally
    SA_CHECK(sch.activeTaskCount() == 0);

    SA_RUN_TESTS();
}

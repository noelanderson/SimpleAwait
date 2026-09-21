// M6 child-await error test — a second await of a consumed Task fails
// deterministically (task_awaited_twice) without suspending or hanging.

#include <coroutine>
#include <cstdint>
#include <utility>

namespace { int g_last_error = -1; }
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define ARDUINOAWAIT_CLOCK_NOW_US() (0ull)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::Task;
using arduinoawait::TaskHandle;

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

// A foreign (non-ArduinoAwait) coroutine that runs eagerly and self-destroys. It
// awaits an ArduinoAwait Task while NO scheduler pass is active (current_ is
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
    AA_CHECK(h.done());
    AA_CHECK(g_child_runs == 1); // child ran exactly once (the first await)
    AA_CHECK(g_last_error == static_cast<int>(Error::task_awaited_twice));
    AA_CHECK(g_after_double == 1); // coroutine continued past the failed second await
    AA_CHECK(sch.activeTaskCount() == 0);

    // ---- a foreign coroutine awaiting a Task outside poll() must not hang ----
    g_last_error = -1;
    g_child_runs = 0;
    g_foreign_after = 0;
    foreignParent(); // runs eagerly to completion; must resume, not strand
    AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
    AA_CHECK(g_foreign_after == 1); // caller resumed (not stranded forever)
    AA_CHECK(g_child_runs == 0);    // child never ran; its frame was released
    AA_CHECK(sch.activeTaskCount() == 0); // nothing left scheduled

    AA_RUN_TESTS();
}

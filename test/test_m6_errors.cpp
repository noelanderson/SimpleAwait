// M6 child-await error test — a second await of a consumed Task fails
// deterministically (task_awaited_twice) without suspending or hanging.

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

    AA_RUN_TESTS();
}

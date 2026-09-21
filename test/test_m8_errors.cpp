// M8 ThreadSafeFlag error test.
//
// A second simultaneous waiter is a deterministic programming error
// (multiple_flag_waiters); a foreign coroutine waiting outside a scheduler pass is
// rejected (invalid_task) rather than stranded; and destroying a flag that still
// has a parked waiter raises object_destroyed_with_waiters.

#include <coroutine>
#include <cstdint>

namespace { int g_last_error = -1; unsigned long long g_now = 0; }
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::TaskHandle;
using arduinoawait::ThreadSafeFlag;

namespace {
int g_a_phase = 0;
int g_b_after = 0;
int g_foreign_after = 0;

Task<void> waiterA(ThreadSafeFlag* f) {
    ++g_a_phase;
    co_await f->wait();
    ++g_a_phase;
}
Task<void> waiterB(ThreadSafeFlag* f) {
    co_await f->wait(); // second waiter -> multiple_flag_waiters, must not suspend
    ++g_b_after;
}
Task<void> parkOn(ThreadSafeFlag* f) {
    co_await f->wait();
}

struct EagerTask {
    struct promise_type {
        EagerTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
};

ThreadSafeFlag* g_foreign_flag = nullptr;
EagerTask foreignWaiter() {
    co_await g_foreign_flag->wait(); // outside poll(): no running task -> invalid_task
    ++g_foreign_after;
}

int g_nested_after = 0;
int g_outer_after = 0;
ThreadSafeFlag* g_nested_flag = nullptr;
EagerTask nestedForeign() {
    co_await g_nested_flag->wait(); // awaiting handle != the running (outer) task
    ++g_nested_after;
}
Task<void> outerLaunch() {
    nestedForeign(); // a foreign nested await, launched from within a running task
    ++g_outer_after;
    co_return;
}
} // namespace

int main() {
    auto& sch = scheduler();

    // ---- a second simultaneous waiter is multiple_flag_waiters (and does not hang) ----
    {
        ThreadSafeFlag flag;
        g_a_phase = 0;
        g_b_after = 0;
        g_last_error = -1;
        spawn(waiterA(&flag));
        poll(); // A parks as the single waiter
        AA_CHECK(g_a_phase == 1);
        spawn(waiterB(&flag)); // B attempts to wait on the same flag
        poll(); // B is rejected without suspending
        AA_CHECK(g_last_error == static_cast<int>(Error::multiple_flag_waiters));
        AA_CHECK(g_b_after == 1); // B continued (did not suspend or hang)
        AA_CHECK(g_a_phase == 1); // A is still the sole waiter
        flag.set();
        poll();
        AA_CHECK(g_a_phase == 2); // A woken by the signal
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- a foreign coroutine waiting outside poll() is rejected, not stranded ----
    {
        ThreadSafeFlag flag;
        g_foreign_flag = &flag;
        g_last_error = -1;
        g_foreign_after = 0;
        foreignWaiter(); // eager, outside poll()
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(g_foreign_after == 1); // caller resumed, not stranded
        AA_CHECK(!flag.isSet());
        // flag has no waiter (rejected before arming) -> its destructor is clean
    }

    // ---- foreign/nested await takes precedence over the single-waiter check ----
    // A real waiter is parked, THEN a foreign nested coroutine awaits the SAME flag
    // from inside a running task. The foreign await must be reported as invalid_task
    // (never masked as multiple_flag_waiters), must not suspend, and must leave the
    // existing waiter untouched.
    {
        ThreadSafeFlag flag;
        g_a_phase = 0;
        g_nested_after = 0;
        g_outer_after = 0;
        g_nested_flag = &flag;
        spawn(waiterA(&flag));
        poll(); // A parks as the single waiter
        AA_CHECK(g_a_phase == 1);
        g_last_error = -1;
        spawn(outerLaunch()); // foreign nested await while A is parked + a task runs
        poll();
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task)); // not multiple_flag_waiters
        AA_CHECK(g_nested_after == 1); // foreign continuation ran (not stranded)
        AA_CHECK(g_outer_after == 1);  // outer task completed
        AA_CHECK(g_a_phase == 1);      // A still the sole waiter, untouched
        flag.set();
        poll();
        AA_CHECK(g_a_phase == 2); // A still wakes normally afterward
        AA_CHECK(sch.activeTaskCount() == 0);
    }

    // ---- destroying a flag with a parked waiter is a deterministic error ----
    g_last_error = -1;
    {
        ThreadSafeFlag doomed;
        spawn(parkOn(&doomed));
        poll(); // the task parks on `doomed`
        // `doomed` is destroyed here WITH a waiter still parked on it
    }
    AA_CHECK(g_last_error == static_cast<int>(Error::object_destroyed_with_waiters));

    AA_RUN_TESTS();
}

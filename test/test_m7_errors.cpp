// M7 Event error test.
//
// Verifies that a foreign coroutine waiting on an Event outside a scheduler pass
// is rejected deterministically (invalid_task) rather than stranded, and that
// destroying an Event that still has parked waiters is a deterministic
// programming error (object_destroyed_with_waiters).

#include <coroutine>
#include <cstdint>

namespace { int g_last_error = -1; unsigned long long g_now = 0; }
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::create_task;
using arduinoawait::Error;
using arduinoawait::Event;
using arduinoawait::poll;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::TaskHandle;

namespace {
int g_after = 0;
int g_foreign_after = 0;

Task<void> parkOn(Event* e) {
    co_await e->wait();
    ++g_after;
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

EagerTask foreignWaiter(Event* e) {
    co_await e->wait(); // outside poll(): no running task -> invalid_task, no strand
    ++g_foreign_after;
}

int g_nested_after = 0;
int g_outer_after = 0;
Event* g_nested_ev = nullptr;

// A foreign coroutine invoked synchronously from inside a running task. current_
// is the outer task (non-null), but the awaiting coroutine is THIS foreign one,
// so the wait must be rejected (not attached to the outer task's slot).
EagerTask nestedForeignWaiter() {
    co_await g_nested_ev->wait();
    ++g_nested_after;
}

Task<void> outerTask() {
    nestedForeignWaiter(); // runs eagerly while outerTask is the running task
    ++g_outer_after;
    co_return;
}
} // namespace

int main() {
    // ---- a foreign coroutine waiting on an Event outside poll() must not hang ----
    {
        Event ev; // clear
        g_last_error = -1;
        g_foreign_after = 0;
        foreignWaiter(&ev); // runs eagerly; the clear-Event wait is rejected
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(g_foreign_after == 1); // caller resumed, not stranded
        AA_CHECK(!ev.isSet());
        // ev has no waiters (the wait was rejected) -> its destructor raises no error
    }

    // ---- a foreign wait NESTED inside a running task is rejected, not misattached ----
    {
        Event ev; // clear
        g_nested_ev = &ev;
        g_last_error = -1;
        g_nested_after = 0;
        g_outer_after = 0;
        TaskHandle ho = create_task(outerTask());
        int g = 0;
        while (!ho.done() && g++ < 20) {
            poll();
        }
        AA_CHECK(ho.done());
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(g_nested_after == 1); // foreign continuation ran (not stranded)
        AA_CHECK(g_outer_after == 1);  // outer task unaffected, completed normally
        ev.set(); // ev must have no waiters -> this wakes nothing
        poll();
        AA_CHECK(scheduler().activeTaskCount() == 0); // no stray slot queued on ev
    }

    // ---- destroying an Event with active waiters is a deterministic error ----
    g_last_error = -1;
    g_after = 0;
    {
        Event doomed; // clear
        spawn(parkOn(&doomed));
        poll(); // the task parks on `doomed`
        AA_CHECK(g_after == 0);
        AA_CHECK(!doomed.isSet());
        // `doomed` is destroyed here WITH a waiter still parked on it
    }
    AA_CHECK(g_last_error == static_cast<int>(Error::object_destroyed_with_waiters));

    AA_RUN_TESTS();
}

// M9 Queue error test.
//
// Destroying a queue that still has a parked sender or receiver is a deterministic
// programming error (object_destroyed_with_waiters). A foreign coroutine that must
// suspend on send/receive outside a scheduler pass (the awaiting coroutine is not
// the running task) is rejected with invalid_task rather than stranded, and it
// mutates no queue state.

#include <coroutine>
#include <cstddef>
#include <cstdint>

namespace { int g_last_error = -1; unsigned long long g_now = 0; }
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::Error;
using arduinoawait::poll;
using arduinoawait::Queue;
using arduinoawait::scheduler;
using arduinoawait::spawn;
using arduinoawait::Task;

namespace {

template <size_t N>
Task<void> parkReceiver(Queue<int, N>* q) {
    (void)(co_await q->receive());
}
template <size_t N>
Task<void> parkSender(Queue<int, N>* q, int v) {
    co_await q->send(v);
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

Queue<int, 2>* g_foreign_recv_q = nullptr;
int g_foreign_recv_after = 0;
EagerTask foreignReceiver() {
    (void)(co_await g_foreign_recv_q->receive()); // outside poll() -> invalid_task
    ++g_foreign_recv_after;
}

Queue<int, 1>* g_foreign_send_q = nullptr;
int g_foreign_send_after = 0;
EagerTask foreignSender() {
    co_await g_foreign_send_q->send(99); // full + foreign -> invalid_task
    ++g_foreign_send_after;
}

} // namespace

int main() {
    auto& sch = scheduler();
    (void)sch;

    // ---- destroying a queue with a parked RECEIVER is a deterministic error ----
    g_last_error = -1;
    {
        Queue<int, 2> q;
        spawn(parkReceiver<2>(&q));
        poll(); // receiver parks on the empty queue
        // q is destroyed here WITH a parked receiver
    }
    AA_CHECK(g_last_error == static_cast<int>(Error::object_destroyed_with_waiters));

    // ---- destroying a queue with a parked SENDER is a deterministic error ----
    g_last_error = -1;
    {
        Queue<int, 1> q;
        (void)q.trySend(1); // full (capacity 1)
        spawn(parkSender<1>(&q, 2));
        poll(); // sender parks on the full queue
        // q is destroyed here WITH a parked sender
    }
    AA_CHECK(g_last_error == static_cast<int>(Error::object_destroyed_with_waiters));

    // ---- a foreign coroutine receiving outside poll() is rejected, not stranded ----
    {
        Queue<int, 2> q;
        g_foreign_recv_q = &q;
        g_last_error = -1;
        g_foreign_recv_after = 0;
        foreignReceiver(); // eager, outside poll(): empty -> would suspend, but foreign
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(g_foreign_recv_after == 1); // caller resumed (not stranded)
        AA_CHECK(q.empty());
        // q has no waiter (rejected before parking) -> clean destructor
    }

    // ---- a foreign coroutine sending on a full queue is rejected, not stranded ----
    {
        Queue<int, 1> q;
        (void)q.trySend(7); // full
        g_foreign_send_q = &q;
        g_last_error = -1;
        g_foreign_send_after = 0;
        foreignSender(); // eager, outside poll(): full -> would suspend, but foreign
        AA_CHECK(g_last_error == static_cast<int>(Error::invalid_task));
        AA_CHECK(g_foreign_send_after == 1); // caller resumed (not stranded)
        int v = 0;
        AA_CHECK(q.tryReceive(v) && v == 7); // only the original value present
        AA_CHECK(q.empty());                 // the foreign send did not buffer 99
    }

    AA_RUN_TESTS();
}

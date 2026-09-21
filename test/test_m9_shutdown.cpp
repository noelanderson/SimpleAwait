// M9 scheduler-shutdown unlink test (BLOCKER 1 regression).
//
// A parked Queue awaiter is the intrusive list node, and it lives on the waiting
// coroutine's frame, which the scheduler owns and destroys at teardown. If the
// Queue OUTLIVES the scheduler — as a namespace-scope Queue does relative to the
// function-local scheduler singleton — the awaiter's frame is destroyed FIRST. The
// awaiter must unlink itself from the still-alive Queue, so the Queue's later
// destructor sees no waiter and does NOT spuriously report
// object_destroyed_with_waiters. Under the DEFAULT [[noreturn]] hook that spurious
// error would abort the process, so this test simply must exit cleanly (0): a
// regression reintroducing the dangling link aborts during static teardown.

namespace { unsigned long long g_now = 0; }
#define ARDUINOAWAIT_CLOCK_NOW_US() (g_now)

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::poll;
using arduinoawait::Queue;
using arduinoawait::spawn;
using arduinoawait::Task;

namespace {
// Namespace-scope queues: constructed at static init, BEFORE the scheduler
// singleton (constructed on first use in main), hence destroyed AFTER it.
Queue<int, 2> g_recv_q;
Queue<int, 1> g_send_q;

Task<void> parkReceiver() { (void)(co_await g_recv_q.receive()); }
Task<void> parkSender() { co_await g_send_q.send(2); }
} // namespace

int main() {
    (void)g_send_q.trySend(1); // fill so the sender must park
    spawn(parkReceiver());
    spawn(parkSender());
    poll(); // both tasks park on their queues

    // main() returns with both queues still holding a parked waiter. At static
    // teardown the scheduler is destroyed first and tears down both parked frames;
    // each awaiter unlinks from its still-alive queue, so ~Queue sees no waiter and
    // the process exits cleanly. AA_RUN_TESTS reports no in-test failures; the real
    // assertion is the clean process exit that follows.
    AA_RUN_TESTS();
}

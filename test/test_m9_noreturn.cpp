// M9 no-return regression (review BLOCKER): the no-value await_resume path — a
// foreign/nested receive() on an empty Queue for a NON-default-constructible payload
// under a RETURNING error hook — must NEVER return, even at the C++20 floor under
// optimization. A bare infinite loop with no observable side effect could be
// assumed to terminate ([intro.progress]) and optimized into a fallthrough that
// moves a never-constructed object; the fix performs a volatile access each
// iteration so the loop is a real, guaranteed non-return.
//
// This program drives exactly that path. It must hang; run_hang_test.cmake builds it
// at -O2 and requires the process to be killed by a timeout rather than exit on its
// own.

#include <coroutine>
#include <cstddef>

// A returning, NON-volatile hook (the style the forward-progress rule can otherwise
// let the optimizer defeat).
namespace { int g_last_error = -1; unsigned long long g_now = 0; }
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_now)

#include <SimpleAwait.h>

using simpleawait::Queue;

namespace {
struct NoDefault {
    int x;
    explicit NoDefault(int v) noexcept : x(v) {}
};

Queue<NoDefault, 1>* g_q = nullptr;

struct EagerTask {
    struct promise_type {
        EagerTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
};

EagerTask foreignReceive() {
    // Empty queue + foreign await (no running task) -> await_suspend rejects with
    // invalid_task and returns false -> await_resume hits the no-value path, which
    // must never return for a non-default-constructible T.
    NoDefault v = co_await g_q->receive();
    (void)v.x; // unreachable if the invariant holds
}
} // namespace

int main() {
    Queue<NoDefault, 1> q; // empty
    g_q = &q;
    foreignReceive();
    // Reaching here means the deterministic no-return invariant was violated.
    return 0;
}

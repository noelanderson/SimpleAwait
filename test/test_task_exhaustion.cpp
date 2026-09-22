// Task<void> frame-pool exhaustion test.
//
// Uses a small frame pool and the recording error hook to verify the coroutine
// frame allocation path: creating tasks eventually exhausts the pool, which
// routes to Error::frame_pool_exhausted and yields an empty Task (via
// get_return_object_on_allocation_failure) rather than throwing or touching the
// heap. Destroying the live tasks fully recovers the pool.

#include <cstdint>
#include <utility>

#define SIMPLEAWAIT_FRAME_POOL_BYTES 1024

namespace {
int g_last_error = -1;
}
#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::Error;
using simpleawait::Task;
using simpleawait::detail::frame_pool;

namespace {
Task<void> make_task() { co_return; }
} // namespace

int main() {
    SA_CHECK(frame_pool().bytesUsed() == 0);

    Task<void> live[128];
    int created = 0;
    bool saw_exhaustion = false;

    for (int i = 0; i < 128; ++i) {
        g_last_error = -1;
        Task<void> t = make_task();
        if (static_cast<bool>(t)) {
            live[created++] = std::move(t);
        } else {
            // Exhausted: empty Task returned and frame_pool_exhausted recorded.
            SA_CHECK(g_last_error == static_cast<int>(Error::frame_pool_exhausted));
            saw_exhaustion = true;
            break;
        }
    }

    SA_CHECK(created >= 1);     // at least one frame fit in the pool
    SA_CHECK(saw_exhaustion);   // and exhaustion was reached within the cap
    SA_CHECK(frame_pool().allocationFailures() >= 1);

    // Destroy all live tasks -> full recovery.
    for (int i = 0; i < created; ++i) {
        live[i] = Task<void>{}; // move-assign empty destroys the held frame
    }
    SA_CHECK(frame_pool().bytesUsed() == 0);

    // After recovery, allocation succeeds again with no error.
    g_last_error = -1;
    Task<void> again = make_task();
    SA_CHECK(static_cast<bool>(again));
    SA_CHECK(g_last_error == -1);

    SA_RUN_TESTS();
}

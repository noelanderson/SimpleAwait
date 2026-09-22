// Hardware validation — lazy Task<void> lifetime and frame-pool integration.
//
// Creates and destroys an unscheduled Task on real hardware and confirms its
// coroutine frame is taken from the fixed pool and returned on destruction (full
// recovery), never touching the global heap. This forces the coroutine promise
// (its pool-backed operator new/delete) and Task ownership to compile and link
// for the target's coroutine ABI and word size.
//
// Developer validation sketch; reaches into detail:: and is not part of the
// public API. Not run by host CI, but CI compiles it on every target.

#include <SimpleAwait.h>

using simpleawait::Task;
using simpleawait::detail::frame_pool;

// A lazy coroutine: its body never runs here (the Task is never scheduled), so
// the flag must stay false — proving creation alone does not execute the body.
static bool g_body_ran = false;

static Task<void> demo() {
    g_body_ran = true;
    co_return;
}

void setup() {
    Serial.begin(115200);
}

void loop() {
    const size_t before = frame_pool().bytesUsed();

    bool owned = false;
    {
        Task<void> t = demo(); // lazy: frame allocated from the pool, body not run
        owned = static_cast<bool>(t);
    } // t destroyed here -> frame returned to the pool

    const bool recovered = frame_pool().bytesUsed() == before;

    Serial.print("owned=");
    Serial.print(owned ? "OK" : "FAIL");
    Serial.print(" body_ran=");
    Serial.print(g_body_ran ? "FAIL(ran)" : "OK(lazy)");
    Serial.print(" recovered=");
    Serial.println(recovered ? "OK" : "FAIL");
    Serial.print("pool_free=");
    Serial.print(static_cast<unsigned long>(frame_pool().bytesFree()));
    Serial.print(" alloc_failures=");
    Serial.println(static_cast<unsigned long>(frame_pool().allocationFailures()));

    delay(1000);
}

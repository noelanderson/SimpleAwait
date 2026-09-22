// Configuration-override test.
//
// The configuration surface must be overridable by defining the macros BEFORE
// including <SimpleAwait.h>. This test overrides task capacity, frame pool
// size, and the deterministic error hook, then verifies the library honors all
// three. Overriding SIMPLEAWAIT_ON_ERROR also lets us exercise the hook without
// terminating the process (the default hook is [[noreturn]]).

#include <cstdint>

#define SIMPLEAWAIT_MAX_TASKS 8
#define SIMPLEAWAIT_FRAME_POOL_BYTES 1024

namespace {
int g_last_error = -1;
}

#define SIMPLEAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <SimpleAwait.h>

#include "sa_test.h"

static_assert(SIMPLEAWAIT_MAX_TASKS == 8, "task capacity override must be honored");
static_assert(SIMPLEAWAIT_FRAME_POOL_BYTES == 1024, "frame pool override must be honored");

int main() {
    // The overridden error hook must expand to our recording macro rather than
    // the default halt handler.
    SIMPLEAWAIT_ON_ERROR(42);
    SA_CHECK(g_last_error == 42);

    SIMPLEAWAIT_ON_ERROR(7);
    SA_CHECK(g_last_error == 7);

    SA_RUN_TESTS();
}

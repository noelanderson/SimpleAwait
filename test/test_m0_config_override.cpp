// M0 configuration-override test.
//
// The configuration surface must be overridable by defining the macros BEFORE
// including <ArduinoAwait.h>. This test overrides task capacity, frame pool
// size, and the deterministic error hook, then verifies the library honors all
// three. Overriding ARDUINOAWAIT_ON_ERROR also lets us exercise the hook without
// terminating the process (the default hook is [[noreturn]]).

#include <cstdint>

#define ARDUINOAWAIT_MAX_TASKS 8
#define ARDUINOAWAIT_FRAME_POOL_BYTES 1024

namespace {
int g_last_error = -1;
}

#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <ArduinoAwait.h>

#include "aa_test.h"

static_assert(ARDUINOAWAIT_MAX_TASKS == 8, "task capacity override must be honored");
static_assert(ARDUINOAWAIT_FRAME_POOL_BYTES == 1024, "frame pool override must be honored");

int main() {
    // The overridden error hook must expand to our recording macro rather than
    // the default halt handler.
    ARDUINOAWAIT_ON_ERROR(42);
    AA_CHECK(g_last_error == 42);

    ARDUINOAWAIT_ON_ERROR(7);
    AA_CHECK(g_last_error == 7);

    AA_RUN_TESTS();
}

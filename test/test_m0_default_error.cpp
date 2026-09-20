// Default error-hook death test.
//
// The default (non-overridden) ARDUINOAWAIT_ON_ERROR hook must terminate the
// program deterministically: on hosted builds it calls std::abort(). This test
// prints a marker (so the wrapper can confirm the process actually reached the
// hook) and then invokes the hook. It is driven by test/run_death_test.cmake,
// which requires both the marker and a non-zero/abnormal termination — a
// portable replacement for CTest WILL_FAIL, which does not reliably invert
// signal-based termination (SIGABRT) on POSIX. If the handler ever wrongly
// returned, control would fall off the end of main, return 0, and the wrapper
// would flag the regression.

#include <ArduinoAwait.h>

#include <cstdio>

#if defined(_MSC_VER)
#  include <cstdlib>
#endif

int main() {
#if defined(_MSC_VER)
    // Run non-interactively: suppress the CRT abort message and Windows Error
    // Reporting fault dialog so the death test cannot block on UI.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    // Marker proving the process launched and reached the hook, so the test
    // wrapper can distinguish a genuine abort from an unrelated launch failure.
    std::fputs("AA_DEATH_TEST_REACHED\n", stderr);
    std::fflush(stderr);

    // Default hook is [[noreturn]]; control must not return past this point.
    ARDUINOAWAIT_ON_ERROR(0);
}

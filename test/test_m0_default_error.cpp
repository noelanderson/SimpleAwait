// Default error-hook death test.
//
// The default (non-overridden) ARDUINOAWAIT_ON_ERROR hook must terminate the
// program deterministically: on hosted builds it calls std::abort(). This test
// invokes it and is registered in CTest with WILL_FAIL, so a normal (zero) exit
// is treated as a FAILURE and the abnormal termination is treated as success.
// If the handler ever wrongly returned, control would fall off the end of main,
// return 0, and CTest would flag the regression.

#include <ArduinoAwait.h>

#if defined(_MSC_VER)
#  include <cstdlib>
#endif

int main() {
#if defined(_MSC_VER)
    // Run non-interactively: suppress the CRT abort message and Windows Error
    // Reporting fault dialog so the death test cannot block on UI.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    // Default hook is [[noreturn]]; control must not return past this point.
    ARDUINOAWAIT_ON_ERROR(0);
}

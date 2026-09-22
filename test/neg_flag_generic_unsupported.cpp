// Negative-compilation probe for the generic-Arduino umbrella header.
//
// On a generic Arduino target (ARDUINO defined, but not a first-class RP2040/
// RP2350/ESP32 core and no critical-section override) there is no PORTABLE way to
// save and restore the interrupt-enable state, and the scheduler's external-signal
// poll step (detail::poll_external_signals, run by poll() every pass) needs a
// usable critical section. So the umbrella <SimpleAwait.h> must fail to compile
// deterministically on include with a directive to define
// SIMPLEAWAIT_CRITICAL_SECTION_OVERRIDE or use a first-class target — rather than
// emitting ISR-unsafe or silently-incorrect code.
//
// This probe defines ARDUINO (with a fake clock override so the clock backend does
// not pull in <Arduino.h>) and merely includes the umbrella header — it does NOT
// reference ThreadSafeFlag — proving the failure is at include, not only at use.
// Built as an EXCLUDE_FROM_ALL target and asserted to fail by
// run_negcompile_test.cmake.

#define ARDUINO 100
#define SIMPLEAWAIT_CLOCK_NOW_US() (0ull)

#include <SimpleAwait.h>

int main() {
    return 0;
}

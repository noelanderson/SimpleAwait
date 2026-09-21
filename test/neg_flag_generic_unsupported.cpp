// Negative-compilation probe for the generic-Arduino ThreadSafeFlag backend (M8 H1).
//
// On a generic Arduino target (ARDUINO defined, but not a first-class RP2040/
// RP2350/ESP32 core and no critical-section override) there is no PORTABLE way to
// save and restore the interrupt-enable state: noInterrupts()/interrupts() would
// unconditionally re-enable interrupts on exit, which is unsafe inside an ISR. The
// short critical section used by ThreadSafeFlag::set() must therefore be a COMPILE
// ERROR when constructed, steering the user to a state-preserving override or a
// first-class target — rather than silently emitting ISR-unsafe code.
//
// This probe defines ARDUINO (with a fake clock override so the clock backend does
// not pull in <Arduino.h>) and constructs a ThreadSafeFlag critical section via
// set(). It must fail to compile WITH the sync diagnostic. Built as an
// EXCLUDE_FROM_ALL target and asserted to fail by run_negcompile_test.cmake.

#define ARDUINO 100
#define ARDUINOAWAIT_CLOCK_NOW_US() (0ull)

#include <ArduinoAwait.h>

int main() {
    arduinoawait::ThreadSafeFlag flag;
    flag.set(); // constructs detail::CriticalSection -> generic-Arduino static_assert
    return 0;
}

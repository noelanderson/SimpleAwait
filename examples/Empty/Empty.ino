// ArduinoAwait — Empty build-skeleton example.
//
// This sketch exists to validate the M0 build matrix: it includes the public
// ArduinoAwait header (exercising the compile-time coroutine-support checks and
// configuration surface) and compiles cleanly on every first-class target with
// no functional scheduling yet.
//
// Later milestones introduce the golden examples (01_Blink, 02_TwoTasks, ...).

#include <ArduinoAwait.h>

void setup() {
    // Nothing to initialize yet. ArduinoAwait requires no begin() call.
}

void loop() {
    // Future milestones will call arduinoawait::poll() here.
}

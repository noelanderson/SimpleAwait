// SimpleAwait — Empty build-skeleton example.
//
// This sketch exists to validate the M0 build matrix: it includes the public
// SimpleAwait header (exercising the compile-time coroutine-support checks and
// configuration surface) and compiles cleanly on every first-class target with
// no functional scheduling yet.
//
// Later milestones introduce the golden examples (01_Blink, 02_TwoTasks, ...).

#include <SimpleAwait.h>

void setup() {
    // Nothing to initialize yet. SimpleAwait requires no begin() call.
}

void loop() {
    // Future milestones will call simpleawait::poll() here.
}

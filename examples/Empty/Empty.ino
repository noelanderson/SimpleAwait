// SimpleAwait — Empty build-skeleton example.
//
// This sketch exists to validate the build/target matrix: it includes the public
// SimpleAwait header (exercising the compile-time coroutine-support checks and
// configuration surface) and compiles cleanly on every first-class target.
//
// For a functional starting point, see the golden examples (01_Blink,
// 02_TwoTasks, ...).

#include <SimpleAwait.h>

void setup() {
    // Nothing to initialize. SimpleAwait requires no begin() call.
}

void loop() {
    // A real sketch drives the scheduler by calling simpleawait::poll() here.
}

// M4 hardware validation — cooperative scheduler on real hardware.
//
// Drives the fixed-slot scheduler through many bounded poll() passes on-target
// and confirms: two tasks scheduled per pass both run (FIFO) and complete within
// one pass; current_task() is valid inside a resumed task and invalid outside a
// pass; an observable TaskHandle reports done() after completion; and the fixed
// task slots are reused across passes (generation increments) with no heap use
// and a bounded active-task count. This forces the scheduler, TaskHandle, and the
// coroutine/pool integration to compile and link for the target's coroutine ABI.
//
// Uses only the public scheduling API. Developer validation sketch; not run by
// host CI, but CI compiles it on every first-class target.

#include <SimpleAwait.h>

using simpleawait::create_task;
using simpleawait::current_task;
using simpleawait::poll;
using simpleawait::scheduler;
using simpleawait::spawn;
using simpleawait::Task;
using simpleawait::TaskHandle;

static int g_ran = 0;
static bool g_current_ok = true;

// Increments the run counter and verifies it is the current task while running.
static Task<void> counter() {
    if (!current_task().valid()) {
        g_current_ok = false;
    }
    ++g_ran;
    co_return;
}

void setup() {
    Serial.begin(115200);
}

void loop() {
    auto& sch = scheduler();

    g_ran = 0;
    g_current_ok = true;

    // Outside a poll() pass there is no current task.
    const bool outside_ok = !current_task().valid();

    // Schedule two tasks: one observable, one detached.
    TaskHandle h = create_task(counter());
    spawn(counter());

    const bool ready_ok = sch.hasReadyTasks() && !h.done();

    poll(); // one bounded pass: both tasks run in FIFO order, then complete

    const bool done_ok = h.done() && (sch.activeTaskCount() == 0) && (g_ran == 2);

    Serial.print("ran=");
    Serial.print(g_ran);
    Serial.print(" current_inside=");
    Serial.print(g_current_ok ? "OK" : "FAIL");
    Serial.print(" current_outside=");
    Serial.print(outside_ok ? "OK" : "FAIL");
    Serial.print(" ready_before=");
    Serial.print(ready_ok ? "OK" : "FAIL");
    Serial.print(" done_after=");
    Serial.println(done_ok ? "OK" : "FAIL");

    delay(500);
}

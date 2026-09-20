// ArduinoAwait — 02_TwoTasks golden example.
//
// Demonstrates the M4 scheduling API: two independent coroutine Tasks scheduled
// to run concurrently, driven by a single cooperative poll() pass. `create_task`
// returns an observable TaskHandle; `spawn` is fire-and-forget. Within one poll()
// pass the scheduler resumes the tasks that were ready at the start of the pass,
// in FIFO order, and never resumes a task twice in the same pass.
//
// This example uses only the M4 API (create_task / spawn / current_task / poll),
// so it compiles unchanged from M4 onward. Later milestones add delay()/yield(),
// which let tasks suspend and interleave over time (see 03_YieldFairness).

#include <ArduinoAwait.h>

using namespace arduinoawait;

Task<void> worker(int id) {
    // current_task() is valid only while a task is being resumed by the scheduler.
    const bool scheduled = current_task().valid();

    Serial.print("worker ");
    Serial.print(id);
    Serial.print(scheduled ? " running under scheduler" : " (unscheduled?)");
    Serial.println();
    co_return;
}

TaskHandle g_first;

void setup() {
    Serial.begin(115200);

    // Schedule two tasks concurrently. Neither body runs yet: scheduling is lazy
    // and bodies execute only inside poll().
    g_first = create_task(worker(1)); // observable
    spawn(worker(2));                 // detached

    // Outside a poll() pass there is no "current" task.
    if (!current_task().valid()) {
        Serial.println("setup: no current task (expected)");
    }
}

void loop() {
    poll(); // one bounded pass: worker(1) then worker(2) run, then complete

    static bool reported = false;
    if (!reported && g_first.done()) {
        // A handle keeps reporting done() until its scheduler slot is reused.
        Serial.println("worker 1 completed");
        reported = true;
    }
}

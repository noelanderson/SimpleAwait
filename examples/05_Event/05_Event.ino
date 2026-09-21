// ArduinoAwait — 05_Event golden example.
//
// A manual-reset Event as a one-shot "start line": several runner tasks all
// co_await the same Event and suspend; a starter task releases them together with
// a single set(), which wakes every waiter in FIFO order (they then run on the
// next poll pass). Event is scheduler-local and cooperative — no locks, no ISR.

#include <ArduinoAwait.h>

using namespace arduinoawait;

Event startLine;

Task<void> runner(int id) {
    co_await startLine.wait(); // wait at the start line until released
    Serial.print("runner ");
    Serial.print(id);
    Serial.println(" GO");
    // A real runner would continue its work here (e.g. co_await delay_ms(...)).
}

Task<void> starter() {
    co_await delay_ms(1000);
    Serial.println("On your marks...");
    co_await delay_ms(1000);
    Serial.println("Get set... GO!");
    startLine.set(); // release every waiting runner at once (FIFO wake order)
}

void setup() {
    Serial.begin(115200);
    spawn(runner(1));
    spawn(runner(2));
    spawn(runner(3));
    spawn(starter());
}

void loop() {
    poll();
}

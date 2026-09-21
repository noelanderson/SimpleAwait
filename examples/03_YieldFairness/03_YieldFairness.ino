// ArduinoAwait — 03_YieldFairness golden example.
//
// Demonstrates cooperative fairness. Two tasks never block: each does a unit of
// work and then co_await yield(). Because yield() always defers to a LATER poll()
// pass and the ready queue is FIFO, the two counters advance 1:1 — a continuously
// running task cannot starve another ready task. A third task uses delay_ms() to
// show timed work coexisting with the pure yielders in the same loop().

#include <ArduinoAwait.h>

using namespace arduinoawait;

unsigned long g_a = 0;
unsigned long g_b = 0;

// Increments the counter pointed to, yielding after each step. The pointer is
// copied into the coroutine frame and refers to a global, so it stays valid.
// yield() is qualified: the Arduino core defines a global ::yield(), so a bare
// yield() would be ambiguous under `using namespace arduinoawait`.
Task<void> counter(unsigned long* ticks) {
    while (true) {
        ++(*ticks);
        co_await arduinoawait::yield();
    }
}

Task<void> reporter() {
    while (true) {
        co_await delay_ms(1000);
        Serial.print("A=");
        Serial.print(g_a);
        Serial.print(" B=");
        Serial.print(g_b);
        Serial.print(" (skew=");
        Serial.print(static_cast<long>(g_a) - static_cast<long>(g_b));
        Serial.println(")");
    }
}

void setup() {
    Serial.begin(115200);
    spawn(counter(&g_a));
    spawn(counter(&g_b));
    spawn(reporter());
}

void loop() {
    poll();
}

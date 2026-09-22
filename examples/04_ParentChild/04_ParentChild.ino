// SimpleAwait — 04_ParentChild golden example.
//
// Sequential child await. A parent Task runs its sub-steps by co_await-ing child
// Tasks: each `co_await step(n)` runs that child to completion (on a later poll
// pass) before the parent continues. This is structured, sequential composition
// on top of the cooperative scheduler — no blocking, no callbacks, and the child
// frames live in the fixed pool. A separate heartbeat task keeps running to show
// the parent only waits for its own child, not the whole scheduler.

#include <SimpleAwait.h>

// Some cores (e.g. the generic "ESP32 Dev Module" board variant) don't define
// LED_BUILTIN because there is no single fixed board; fall back to the GPIO most
// ESP32 dev boards use for an onboard LED.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

using namespace simpleawait;

Task<void> step(int n) {
    Serial.print("  step ");
    Serial.print(n);
    Serial.println(" begin");
    co_await delay_ms(200);
    Serial.print("  step ");
    Serial.print(n);
    Serial.println(" end");
}

Task<void> sequence() {
    while (true) {
        Serial.println("sequence begin");
        co_await step(1);
        co_await step(2);
        co_await step(3);
        Serial.println("sequence end");
        co_await delay_ms(1000);
    }
}

Task<void> heartbeat() {
    while (true) {
        digitalWrite(LED_BUILTIN, HIGH);
        co_await delay_ms(100);
        digitalWrite(LED_BUILTIN, LOW);
        co_await delay_ms(100);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    spawn(sequence());
    spawn(heartbeat());
}

void loop() {
    poll();
}

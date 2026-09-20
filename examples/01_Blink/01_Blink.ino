// ArduinoAwait — 01_Blink (M4 skeleton).
//
// This is the structural skeleton of the canonical blink example. At M4 the
// scheduler can run coroutine Tasks but has no timing primitives yet, so the
// skeleton wires a Task through the scheduler using only the M4 API (spawn +
// poll). The timing-driven body is completed at M5, when delay()/delay_ms()
// land, turning this into a real cooperative blink:
//
//     Task<void> blink() {
//         pinMode(kLedPin, OUTPUT);
//         while (true) {
//             digitalWrite(kLedPin, HIGH);
//             co_await delay_ms(500);
//             digitalWrite(kLedPin, LOW);
//             co_await delay_ms(500);
//         }
//     }

#include <ArduinoAwait.h>

using namespace arduinoawait;

constexpr int kLedPin = LED_BUILTIN;

Task<void> blink() {
    pinMode(kLedPin, OUTPUT);
    digitalWrite(kLedPin, HIGH);
    co_return; // M5 replaces this with the delay()-driven blink loop above.
}

void setup() {
    spawn(blink());
}

void loop() {
    poll();
}

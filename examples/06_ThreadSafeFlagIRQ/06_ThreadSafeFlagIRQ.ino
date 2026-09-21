// ArduinoAwait — 06_ThreadSafeFlagIRQ golden example.
//
// The canonical IRQ -> coroutine bridge. A hardware interrupt handler calls
// ThreadSafeFlag::set() from ISR context; a coroutine simply `co_await`s the flag
// and is woken (in scheduler context, on the next poll) each time the interrupt
// fires. set() is safe to call from the ISR — it never touches scheduler lists and
// never resumes coroutine code directly; the scheduler resolves the signal at the
// start of poll(). This is the ONLY primitive whose set() may be called from an
// external/IRQ context (ordinary Event is scheduler-only).

#include <ArduinoAwait.h>

using namespace arduinoawait;

ThreadSafeFlag buttonPressed;
constexpr int kButtonPin = 2; // wire a button from this pin to GND (INPUT_PULLUP)

void onButtonIsr() {
    buttonPressed.set(); // ISR context: signal the waiting coroutine, safely
}

Task<void> handler() {
    unsigned long count = 0;
    while (true) {
        co_await buttonPressed.wait(); // suspend until the ISR signals
        ++count;
        Serial.print("button interrupt #");
        Serial.println(count);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(kButtonPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(kButtonPin), onButtonIsr, FALLING);
    spawn(handler());
}

void loop() {
    poll();
}

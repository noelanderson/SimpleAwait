// SimpleAwait — 01_Blink golden example.
//
// The canonical cooperative blink: a single Task toggles the LED and yields the
// core back to the scheduler across each interval via co_await delay_ms(). No
// busy-waiting and no RTOS — loop() just pumps poll(), and the coroutine's frame
// lives in the fixed pool. Other tasks (see 02_TwoTasks) can run concurrently in
// the same loop.

#include <SimpleAwait.h>

// Some cores (e.g. the generic "ESP32 Dev Module" board variant) don't define
// LED_BUILTIN because there is no single fixed board; fall back to the GPIO most
// ESP32 dev boards use for an onboard LED.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

using namespace simpleawait;

constexpr int kLedPin = LED_BUILTIN;

Task<void> blink() {
    pinMode(kLedPin, OUTPUT);
    while (true) {
        digitalWrite(kLedPin, HIGH);
        co_await delay_ms(500);
        digitalWrite(kLedPin, LOW);
        co_await delay_ms(500);
    }
}

void setup() {
    spawn(blink());
}

void loop() {
    poll();
}

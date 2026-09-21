// ArduinoAwait — 01_Blink golden example.
//
// The canonical cooperative blink: a single Task toggles the LED and yields the
// core back to the scheduler across each interval via co_await delay_ms(). No
// busy-waiting and no RTOS — loop() just pumps poll(), and the coroutine's frame
// lives in the fixed pool. Other tasks (see 02_TwoTasks) can run concurrently in
// the same loop.

#include <ArduinoAwait.h>

using namespace arduinoawait;

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

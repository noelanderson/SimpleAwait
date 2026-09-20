// M1 hardware validation — native monotonic microsecond clock.
//
// Reads detail::platform_now_us() repeatedly and confirms the 64-bit
// microsecond timebase is monotonic on real hardware. The same source runs on
// every first-class target; the platform layer selects the backend
// (time_us_64() on RP2040/RP2350, esp_timer_get_time() on ESP32).
//
// This is a developer validation sketch (it reaches into detail:: to exercise
// the internal clock); it is not part of the public application API. It is not
// run by host CI.

#include <ArduinoAwait.h>

using arduinoawait::detail::platform_now_us;
using arduinoawait::detail::tick_t;

void setup() {
    Serial.begin(115200);
    const tick_t start = platform_now_us();
    (void)start;
}

void loop() {
    tick_t previous = platform_now_us();
    bool monotonic = true;
    tick_t max_step = 0;

    for (uint32_t i = 0; i < 200000; ++i) {
        const tick_t now = platform_now_us();
        if (now < previous) {
            monotonic = false;
            break;
        }
        const tick_t step = now - previous;
        if (step > max_step) {
            max_step = step;
        }
        previous = now;
    }

    Serial.print("platform_now_us monotonic: ");
    Serial.println(monotonic ? "OK" : "FAIL");
    Serial.print("now_us(low32)=");
    Serial.println(static_cast<unsigned long>(previous & 0xFFFFFFFFULL));
    Serial.print("max_step_us=");
    Serial.println(static_cast<unsigned long>(max_step));

    delay(1000);
}

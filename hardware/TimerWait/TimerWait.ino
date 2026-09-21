// M5 hardware validation — timer waits on the native clock.
//
// Spawns a task that repeatedly co_await delay_ms(500) and reports the interval
// actually measured from the native 64-bit microsecond clock (time_us_64() on
// RP2040/RP2350, esp_timer_get_time() on ESP32). This forces the timer path
// (delay awaitable -> scheduler timer wait -> platform clock) to compile and link
// for the target and confirms delays elapse on real hardware. A second task
// yields continuously to confirm it is not starved while the first one sleeps.
//
// Developer validation sketch; not run by host CI, but CI compiles it on every
// first-class target.

#include <ArduinoAwait.h>

using arduinoawait::delay_ms;
using arduinoawait::poll;
using arduinoawait::spawn;
using arduinoawait::Task;
// Note: arduinoawait::yield() is qualified at the call site rather than brought in
// with a using-declaration, because the Arduino core defines a global ::yield().

static unsigned long g_yield_ticks = 0;

static Task<void> blinker() {
    pinMode(LED_BUILTIN, OUTPUT);
    bool on = false;
    // Baseline from the clock before the first wait, so the first reported
    // interval measures one actual delay rather than uptime-at-first-wake.
    unsigned long long last = arduinoawait::detail::platform_now_us();
    while (true) {
        co_await delay_ms(500);
        on = !on;
        digitalWrite(LED_BUILTIN, on ? HIGH : LOW);

        const unsigned long long now = arduinoawait::detail::platform_now_us();
        const unsigned long long elapsed_ms = (now - last) / 1000ULL;
        last = now;
        Serial.print("interval_ms=");
        Serial.print(static_cast<unsigned long>(elapsed_ms));
        Serial.print(" yield_ticks_between=");
        Serial.println(g_yield_ticks);
        g_yield_ticks = 0;
    }
}

// Never blocks: proves the sleeping timer task does not starve ready work.
static Task<void> busy() {
    while (true) {
        ++g_yield_ticks;
        co_await arduinoawait::yield();
    }
}

void setup() {
    Serial.begin(115200);
    spawn(blinker());
    spawn(busy());
}

void loop() {
    poll();
}

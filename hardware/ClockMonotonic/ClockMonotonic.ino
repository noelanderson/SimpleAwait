// M1 hardware validation — native monotonic microsecond clock.
//
// Confirms the 64-bit microsecond timebase is monotonic AND actually advances on
// real hardware. The same source runs on every first-class target; the platform
// layer selects the backend (time_us_64() on RP2040/RP2350, esp_timer_get_time()
// on ESP32).
//
// It reaches into detail:: to exercise the internal clock, so it is a developer
// validation sketch, not part of the public application API. It is not run by
// host CI, but CI compiles it on every target to confirm the native backend
// links.
//
// Validation across successive loop() batches (the previous observation persists
// between batches, so a backward jump or a frozen clock during the ~1s gap is
// visible — a per-batch reset would hide both):
//   * monotonic: no read is less than the previous read, within or across batches;
//   * advanced:  the clock strictly advances over the ~1s inter-batch interval,
//                which fails for a permanently frozen clock.

#include <ArduinoAwait.h>

using arduinoawait::detail::platform_now_us;
using arduinoawait::detail::tick_t;

static tick_t s_last = 0;
static bool s_have_last = false;

void setup() {
    Serial.begin(115200);
}

void loop() {
    const tick_t batch_start = platform_now_us();

    bool monotonic = true;
    bool advanced_across_gap = true;

    if (s_have_last) {
        if (batch_start < s_last) {
            monotonic = false; // went backward across the inter-batch gap
        }
        if (batch_start <= s_last) {
            advanced_across_gap = false; // frozen: no progress over ~1s
        }
    }

    tick_t previous = batch_start;
    for (uint32_t i = 0; i < 200000; ++i) {
        const tick_t now = platform_now_us();
        if (now < previous) {
            monotonic = false; // went backward within the batch
            break;
        }
        previous = now;
    }

    s_last = previous;
    s_have_last = true;

    Serial.print("monotonic=");
    Serial.println(monotonic ? "OK" : "FAIL");
    Serial.print("advanced_across_gap=");
    Serial.println(advanced_across_gap ? "OK" : "FAIL(frozen?)");
    Serial.print("now_us_low32=");
    Serial.println(static_cast<unsigned long>(previous & 0xFFFFFFFFULL));

    delay(1000);
}

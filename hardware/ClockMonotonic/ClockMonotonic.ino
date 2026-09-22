// Hardware validation — native monotonic microsecond clock.
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
// Design note (why progress is checked WITHIN a batch, before any delay): on
// Arduino-Pico, delay() ultimately waits on the same native timer. If that timer
// were frozen, a validator that only compared reads across loop() iterations
// would print "OK" and then block forever inside delay() before ever reaching a
// freeze check. So each loop() first samples a bounded batch and requires the
// clock to advance within it; only a healthy, advancing clock is then allowed to
// enter the clock-dependent delay.

#include <SimpleAwait.h>

using simpleawait::detail::platform_now_us;
using simpleawait::detail::tick_t;

static tick_t s_last = 0;
static bool s_have_last = false;

// Bounded busy-wait that does NOT depend on the clock under test, used to idle
// between batches when the clock is stalled (so the sketch keeps reporting
// instead of hanging in a clock-dependent delay()).
static void busy_idle() {
    // Non-volatile loop counter with a per-iteration volatile access: keeps the
    // loop from being optimized away without the C++20-deprecated volatile
    // compound increment.
    for (uint32_t i = 0; i < 2000000u; ++i) {
        volatile uint32_t sink = i;
        (void)sink;
    }
}

void loop() {
    const tick_t batch_start = platform_now_us();

    // Within-batch monotonicity and progress. A frozen clock is detected HERE,
    // before any clock-dependent wait.
    bool monotonic = true;
    tick_t previous = batch_start;
    for (uint32_t i = 0; i < 200000; ++i) {
        const tick_t now = platform_now_us();
        if (now < previous) {
            monotonic = false; // went backward within the batch
            break;
        }
        previous = now;
    }
    const bool progressed_in_batch = previous > batch_start;

    // Cross-batch: the clock must not roll back since the previous batch. This is
    // unmeasured on the very first batch (nothing to compare against yet).
    const bool cross_batch_measured = s_have_last;
    const bool cross_batch_ok = !s_have_last || batch_start >= s_last;

    s_last = previous;
    s_have_last = true;

    Serial.print("monotonic=");
    Serial.println(monotonic ? "OK" : "FAIL");
    Serial.print("progressed_in_batch=");
    Serial.println(progressed_in_batch ? "OK" : "FAIL(frozen?)");
    Serial.print("cross_batch=");
    if (!cross_batch_measured) {
        Serial.println("n/a(first batch)");
    } else {
        Serial.println(cross_batch_ok ? "OK" : "FAIL(rollback)");
    }
    Serial.print("now_us_low32=");
    Serial.println(static_cast<unsigned long>(previous & 0xFFFFFFFFULL));

    // Only enter the clock-dependent delay if the clock is healthy; otherwise a
    // frozen clock would block delay() indefinitely before the next report.
    if (monotonic && progressed_in_batch && cross_batch_ok) {
        delay(1000);
    } else {
        Serial.println("clock unhealthy: skipping clock-dependent delay");
        busy_idle();
    }
}

void setup() {
    Serial.begin(115200);
}

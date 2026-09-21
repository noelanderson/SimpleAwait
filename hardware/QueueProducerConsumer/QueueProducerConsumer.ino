// M9 hardware validation — Queue<T,N> producer/consumer FIFO + back-pressure.
//
// A producer sends a monotonically increasing counter into a small bounded Queue;
// a consumer receives and verifies strict FIFO order (each value == the previous
// + 1). The small capacity forces both full-send and empty-receive suspension, and
// the ever-increasing counter wraps the ring buffer many times. A periodic reporter
// prints the running count and an IN-ORDER / OUT-OF-ORDER verdict, forcing the Queue
// path (aligned ring storage, sender/receiver wait sets, direct hand-off) to
// compile, link, and run correctly on device.
//
// Uses only the public API. Developer validation sketch; not run by host CI, but
// CI compiles it on every first-class target.

#include <ArduinoAwait.h>

using arduinoawait::delay_ms;
using arduinoawait::poll;
using arduinoawait::Queue;
using arduinoawait::spawn;
using arduinoawait::Task;

static Queue<uint32_t, 4> g_q;
static uint32_t g_received = 0;
static uint32_t g_next_expected = 0;
static bool g_order_ok = true;

static Task<void> producer() {
    uint32_t v = 0;
    while (true) {
        co_await g_q.send(v); // suspends while the queue is full (back-pressure)
        ++v;
    }
}

static Task<void> consumer() {
    while (true) {
        const uint32_t v = co_await g_q.receive(); // suspends while empty
        if (v != g_next_expected) {
            g_order_ok = false; // a gap or reordering would be a defect
        }
        g_next_expected = v + 1;
        ++g_received;
    }
}

static Task<void> reporter() {
    while (true) {
        co_await delay_ms(1000);
        Serial.print("received=");
        Serial.print(g_received);
        Serial.print(" next_expected=");
        Serial.print(g_next_expected);
        Serial.println(g_order_ok ? " IN-ORDER" : " OUT-OF-ORDER");
    }
}

void setup() {
    Serial.begin(115200);
    spawn(producer());
    spawn(consumer());
    spawn(reporter());
}

void loop() {
    poll();
}

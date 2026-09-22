// SimpleAwait — 08_WaitUntil golden example.
//
// waitUntil(predicate) suspends a task at fair yield points until a condition holds
// — a readable, cooperative alternative to a hand-written polling loop. Here a
// "sensor" value (advanced by a background task) must cross a threshold; a worker
// waits for it with waitUntil, while a heartbeat task keeps running to show the wait
// never blocks the scheduler. waitUntil is a pure composition over yield(), not a
// new primitive.

#include <SimpleAwait.h>

using namespace simpleawait;

static int g_sensor = 0;

Task<void> sensor() {
    while (true) {
        ++g_sensor;
        co_await delay_ms(200);
    }
}

Task<void> worker() {
    co_await waitUntil([] { return g_sensor >= 10; });
    Serial.print("threshold reached at sensor=");
    Serial.println(g_sensor);
}

Task<void> heartbeat() {
    while (true) {
        Serial.println("tick");
        co_await delay_ms(500);
    }
}

void setup() {
    Serial.begin(115200);
    spawn(sensor());
    spawn(worker());
    spawn(heartbeat());
}

void loop() {
    poll();
}

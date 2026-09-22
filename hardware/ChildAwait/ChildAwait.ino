// M6 hardware validation — parent/child await on real hardware.
//
// A parent task awaits a sequence of child tasks; each child does a timed unit of
// work. The sketch confirms on-target that co_await of a child Task compiles and
// links (the TaskAwaiter -> scheduler start_child path and the coroutine ABI) and
// that the parent resumes only after each child completes, in order. A heartbeat
// task runs concurrently to confirm the waiting parent does not stall the loop.
//
// Uses only the public API. Developer validation sketch; not run by host CI, but
// CI compiles it on every first-class target.

#include <SimpleAwait.h>

using simpleawait::delay_ms;
using simpleawait::poll;
using simpleawait::spawn;
using simpleawait::Task;

static int g_order = 0;
static unsigned long g_heartbeats = 0;

static Task<void> childStep(int expected) {
    co_await delay_ms(50);
    const bool ok = (g_order == expected);
    g_order = expected + 1;
    Serial.print("child ");
    Serial.print(expected);
    Serial.println(ok ? " OK" : " OUT-OF-ORDER");
}

static Task<void> parentSeq() {
    while (true) {
        g_order = 0;
        co_await childStep(0);
        co_await childStep(1);
        co_await childStep(2);
        Serial.print("sequence done; heartbeats_this_round=");
        Serial.println(g_heartbeats);
        g_heartbeats = 0;
        co_await delay_ms(1000);
    }
}

static Task<void> heartbeat() {
    while (true) {
        ++g_heartbeats;
        co_await delay_ms(25);
    }
}

void setup() {
    Serial.begin(115200);
    spawn(parentSeq());
    spawn(heartbeat());
}

void loop() {
    poll();
}

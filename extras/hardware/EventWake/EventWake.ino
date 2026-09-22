// Hardware validation — Event wake ordering on real hardware.
//
// Three waiter tasks co_await the same manual-reset Event and suspend; a
// controller sets the Event to release them, then clears it for the next round.
// Each waiter records the order in which it was released; the controller reports
// whether the wake order was FIFO (1,2,3). This forces the Event / WaitQueue
// path (park in waiting_local, wake_all to the ready FIFO) to compile and link
// for the target and confirms multi-waiter FIFO wake on device.
//
// Uses only the public API. Developer validation sketch; not run by host CI, but
// CI compiles it on every first-class target.

#include <SimpleAwait.h>

using simpleawait::delay_ms;
using simpleawait::Event;
using simpleawait::poll;
using simpleawait::spawn;
using simpleawait::Task;

static Event g_go;
static int g_order[3];
static int g_count = 0;

static Task<void> waiter(int id) {
    while (true) {
        co_await g_go.wait();
        if (g_count < 3) {
            g_order[g_count++] = id;
        }
        co_await delay_ms(1); // let the controller clear before waiting again
    }
}

static Task<void> controller() {
    while (true) {
        co_await delay_ms(1000);
        g_count = 0;
        g_go.set();           // release all three waiters
        co_await delay_ms(1); // give them a pass to record their order
        g_go.clear();         // reset for the next round

        const bool fifo = (g_count == 3) && (g_order[0] == 1) &&
                          (g_order[1] == 2) && (g_order[2] == 3);
        Serial.print("release order=");
        Serial.print(g_order[0]);
        Serial.print(g_order[1]);
        Serial.print(g_order[2]);
        Serial.println(fifo ? " FIFO-OK" : " OUT-OF-ORDER");
    }
}

void setup() {
    Serial.begin(115200);
    spawn(waiter(1));
    spawn(waiter(2));
    spawn(waiter(3));
    spawn(controller());
}

void loop() {
    poll();
}

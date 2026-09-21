// M8 hardware validation — IRQ -> ThreadSafeFlag -> coroutine wake latency.
//
// A pin-change interrupt handler timestamps the event from the native 64-bit
// clock and calls ThreadSafeFlag::set() from ISR context. A coroutine `co_await`s
// the flag and, on wake (scheduler context), reads the clock again and reports the
// IRQ -> coroutine latency (bounded by the poll() loop period). This forces the
// external-signal path (platform CriticalSection, set() from an ISR, the scheduler
// external-pending resolution) to compile and link for the target, and confirms on
// device that an ISR signal reliably wakes a waiting coroutine without the ISR ever
// resuming coroutine code.
//
// Trigger by momentarily connecting the pin to GND (INPUT_PULLUP). A concurrent
// heartbeat task proves the waiting coroutine does not stall the loop.
//
// Developer validation sketch; not run by host CI, but CI compiles it on every
// first-class target.

#include <ArduinoAwait.h>

using arduinoawait::delay_ms;
using arduinoawait::poll;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::ThreadSafeFlag;

static ThreadSafeFlag g_irq;
static volatile unsigned long long g_isr_time_us = 0;
static unsigned long g_heartbeats = 0;

constexpr int kIrqPin = 2;

static void onIsr() {
    g_isr_time_us = arduinoawait::detail::platform_now_us(); // ISR-safe native clock
    g_irq.set();                                             // ISR-safe signal
}

static Task<void> irqHandler() {
    while (true) {
        co_await g_irq.wait();
        const unsigned long long now = arduinoawait::detail::platform_now_us();
        const unsigned long latency_us =
            static_cast<unsigned long>(now - g_isr_time_us);
        Serial.print("irq wake latency_us=");
        Serial.print(latency_us);
        Serial.print(" heartbeats_since=");
        Serial.println(g_heartbeats);
        g_heartbeats = 0;
    }
}

static Task<void> heartbeat() {
    while (true) {
        ++g_heartbeats;
        co_await delay_ms(1);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(kIrqPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(kIrqPin), onIsr, FALLING);
    spawn(irqHandler());
    spawn(heartbeat());
}

void loop() {
    poll();
}

// ArduinoAwait — 07_QueueProducerConsumer golden example.
//
// A bounded, scheduler-local Queue<T,N> connects a producer and a consumer
// coroutine. The producer co_awaits send() and suspends when the queue is full;
// the consumer co_awaits receive() and suspends when it is empty. Back-pressure is
// automatic and cooperative — values flow in FIFO order with no locks, no ISR, and
// no heap. Because the consumer here is slower than the producer, the queue fills
// and the producer naturally throttles to the consumer's rate.

#include <ArduinoAwait.h>

using namespace arduinoawait;

Queue<int, 8> samples;

Task<void> producer() {
    int value = 0;
    while (true) {
        co_await samples.send(value); // suspends while the queue is full
        Serial.print("produced ");
        Serial.println(value);
        ++value;
        co_await delay_ms(250);
    }
}

Task<void> consumer() {
    while (true) {
        const int value = co_await samples.receive(); // suspends while empty
        Serial.print("            consumed ");
        Serial.println(value);
        co_await delay_ms(1000); // slower than the producer -> back-pressure
    }
}

void setup() {
    Serial.begin(115200);
    spawn(producer());
    spawn(consumer());
}

void loop() {
    poll();
}

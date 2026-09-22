// Hardware validation — fixed coroutine frame allocator.
//
// Exercises detail::FramePool on real hardware: allocate a couple of frames
// (including an over-aligned one), free them, and confirm full recovery
// (bytesUsed() == 0). This also forces the allocator template to compile and
// link for the target's word size and alignment (e.g. 32-bit ARM/RISC-V, where
// size_t and alignof(max_align_t) differ from the 64-bit host).
//
// Developer validation sketch; reaches into detail:: and is not part of the
// public API. Not run by host CI, but CI compiles it on every target.

#include <SimpleAwait.h>

using simpleawait::detail::FramePool;

static FramePool<2048> pool;

void setup() {
    Serial.begin(115200);
}

void loop() {
    void* a = pool.allocate(64);
    void* b = pool.allocate(200, 32); // over-aligned request
    void* c = pool.allocate(16);

    const bool allocated = a != nullptr && b != nullptr && c != nullptr;
    const bool bAligned =
        (reinterpret_cast<uintptr_t>(b) & (uintptr_t{32} - 1)) == 0;

    // Free in a different order than allocated to exercise coalescing.
    pool.deallocate(b);
    pool.deallocate(a);
    pool.deallocate(c);

    const bool recovered = pool.bytesUsed() == 0 &&
                           pool.bytesFree() == FramePool<2048>::capacity();

    Serial.print("alloc=");
    Serial.print(allocated ? "OK" : "FAIL");
    Serial.print(" over_aligned=");
    Serial.print(bAligned ? "OK" : "FAIL");
    Serial.print(" recovered=");
    Serial.println(recovered ? "OK" : "FAIL");
    Serial.print("capacity=");
    Serial.print(static_cast<unsigned long>(FramePool<2048>::capacity()));
    Serial.print(" peak_used=");
    Serial.println(static_cast<unsigned long>(pool.peakBytesUsed()));

    delay(1000);
}

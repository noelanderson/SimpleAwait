// M2 fixed frame allocator test.
//
// Exercises the coalescing free-list FramePool: exact-capacity allocation, many
// small frames, mixed sizes with non-overlap, arbitrary destruction order,
// fragmentation and coalescing with full recovery, over-aligned requests,
// deterministic exhaustion, and peak tracking. A global operator new/delete
// override acts as a canary: the pool must never touch the global heap.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(_MSC_VER)
#  include <malloc.h>
#endif

namespace {
unsigned long long g_global_new_calls = 0;

void* raw_aligned(std::size_t n, std::size_t align) {
    if (align < sizeof(void*)) {
        align = sizeof(void*);
    }
#if defined(_MSC_VER)
    return _aligned_malloc(n ? n : 1, align);
#else
    void* p = nullptr;
    if (posix_memalign(&p, align, n ? n : 1) != 0) {
        p = nullptr;
    }
    return p;
#endif
}

void raw_aligned_free(void* p) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
} // namespace

// Instrument every replaceable global allocation function (plain, array, sized,
// and over-aligned) so the canary catches ANY global heap use by the pool.
void* operator new(std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { ++g_global_new_calls; return std::malloc(n ? n : 1); }
void* operator new(std::size_t n, std::align_val_t a) {
    ++g_global_new_calls;
    return raw_aligned(n, static_cast<std::size_t>(a));
}
void* operator new[](std::size_t n, std::align_val_t a) {
    ++g_global_new_calls;
    return raw_aligned(n, static_cast<std::size_t>(a));
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { raw_aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { raw_aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { raw_aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { raw_aligned_free(p); }

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::detail::FramePool;

namespace {

bool is_aligned(const void* p, std::size_t align) {
    return (reinterpret_cast<std::uintptr_t>(p) & (align - 1)) == 0;
}

constexpr std::size_t kMaxAlign = alignof(std::max_align_t);

} // namespace

int main() {
    const unsigned long long newAtStart = g_global_new_calls;

    // ---- basic alloc/free with full recovery and no global allocation ----
    {
        FramePool<4096> pool;
        AA_CHECK(pool.bytesUsed() == 0);
        AA_CHECK(pool.bytesFree() == pool.capacity());

        const unsigned long long before = g_global_new_calls;
        void* a = pool.allocate(100);
        AA_CHECK(a != nullptr);
        AA_CHECK(is_aligned(a, kMaxAlign));
        AA_CHECK(pool.bytesUsed() > 0);

        pool.deallocate(a);
        AA_CHECK(pool.bytesUsed() == 0);
        AA_CHECK(pool.bytesFree() == pool.capacity());
        AA_CHECK(g_global_new_calls == before); // canary: no global heap use
    }

    // ---- many small frames freed in a scrambled order ----
    {
        FramePool<4096> pool;
        void* ptrs[16];
        for (int i = 0; i < 16; ++i) {
            ptrs[i] = pool.allocate(32);
            AA_CHECK(ptrs[i] != nullptr);
        }
        const int order[16] = {3, 1, 0, 2, 7, 5, 4, 6, 11, 9, 8, 10, 15, 13, 12, 14};
        for (int i = 0; i < 16; ++i) {
            pool.deallocate(ptrs[order[i]]);
        }
        AA_CHECK(pool.bytesUsed() == 0);
        AA_CHECK(pool.bytesFree() == pool.capacity());
    }

    // ---- mixed sizes must not overlap ----
    {
        FramePool<4096> pool;
        auto* a = static_cast<unsigned char*>(pool.allocate(16));
        auto* b = static_cast<unsigned char*>(pool.allocate(240));
        auto* c = static_cast<unsigned char*>(pool.allocate(96));
        AA_CHECK(a && b && c);

        std::memset(a, 0xAA, 16);
        std::memset(b, 0xBB, 240);
        std::memset(c, 0xCC, 96);
        AA_CHECK(a[0] == 0xAA && a[15] == 0xAA);
        AA_CHECK(b[0] == 0xBB && b[239] == 0xBB);
        AA_CHECK(c[0] == 0xCC && c[95] == 0xCC);

        pool.deallocate(b);
        pool.deallocate(a);
        pool.deallocate(c);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // ---- fragmentation then coalescing recovers full capacity ----
    {
        FramePool<4096> pool;
        void* a = pool.allocate(64);
        void* b = pool.allocate(64);
        void* c = pool.allocate(64);
        AA_CHECK(a && b && c);

        pool.deallocate(a); // free ends, leaving b allocated in the middle
        pool.deallocate(c);
        pool.deallocate(b); // frees the middle -> everything coalesces
        AA_CHECK(pool.bytesUsed() == 0);

        // A single allocation can again use essentially the whole arena.
        void* big = pool.allocate(pool.capacity() - pool.blockOverhead());
        AA_CHECK(big != nullptr);
        AA_CHECK(pool.bytesUsed() == pool.capacity()); // exact maximum payload
        pool.deallocate(big);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // ---- over-aligned requests ----
    {
        FramePool<4096> pool;
        const std::size_t aligns[] = {std::size_t(16), std::size_t(32),
                                      std::size_t(64), std::size_t(128)};
        for (std::size_t align : aligns) {
            void* p = pool.allocate(50, align);
            AA_CHECK(p != nullptr);
            AA_CHECK(is_aligned(p, align));
            std::memset(p, 0x5A, 50); // write to prove the region is usable
            pool.deallocate(p);
            AA_CHECK(pool.bytesUsed() == 0);
        }
    }

    // ---- exact capacity, then deterministic exhaustion ----
    {
        FramePool<256> pool;
        void* whole = pool.allocate(pool.capacity() - pool.blockOverhead());
        AA_CHECK(whole != nullptr);
        AA_CHECK(pool.bytesUsed() == pool.capacity());

        void* none = pool.allocate(16);
        AA_CHECK(none == nullptr);
        AA_CHECK(pool.allocationFailures() == 1);

        pool.deallocate(whole);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // ---- exhaustion under many allocations; peak retained after recovery ----
    {
        FramePool<512> pool;
        void* ptrs[128];
        int n = 0;
        for (;;) {
            void* p = pool.allocate(16);
            if (p == nullptr) {
                break;
            }
            AA_CHECK(n < 128);
            ptrs[n++] = p;
        }
        AA_CHECK(pool.allocationFailures() >= 1);

        const std::size_t peak = pool.peakBytesUsed();
        AA_CHECK(peak > 0);
        AA_CHECK(peak <= pool.capacity());

        for (int i = 0; i < n; ++i) {
            pool.deallocate(ptrs[i]);
        }
        AA_CHECK(pool.bytesUsed() == 0);
        AA_CHECK(pool.bytesFree() == pool.capacity());
        AA_CHECK(pool.peakBytesUsed() == peak); // peak is a high-water mark
    }

    // ---- oversized / overflow-inducing requests fail cleanly (no corruption) ----
    {
        FramePool<256> pool;
        const std::size_t before = pool.allocationFailures();

        AA_CHECK(pool.allocate(SIZE_MAX) == nullptr);
        AA_CHECK(pool.allocate(SIZE_MAX - (pool.blockOverhead() - 1)) == nullptr);
        AA_CHECK(pool.allocate(pool.capacity()) == nullptr); // no room for a header
        AA_CHECK(pool.bytesUsed() == 0);                     // arena untouched
        AA_CHECK(pool.allocationFailures() == before + 3);

        // The pool is still fully intact: an exact-capacity cycle still works.
        void* whole = pool.allocate(pool.capacity() - pool.blockOverhead());
        AA_CHECK(whole != nullptr);
        AA_CHECK(pool.bytesUsed() == pool.capacity());
        pool.deallocate(whole);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // ---- over-aligned request in a tiny pool never forms an out-of-arena ptr ----
    // Whether a 128/256-aligned block fits a 64-byte pool depends on the pool's
    // runtime address, so we assert the INVARIANT (aligned and usable, or null;
    // pool always intact) rather than a fixed outcome. ASan/UBSan (CI) confirms no
    // out-of-bounds pointer is ever formed on the miss path.
    {
        FramePool<64> pool;
        const std::size_t aligns[] = {std::size_t(128), std::size_t(256)};
        for (std::size_t align : aligns) {
            void* p = pool.allocate(1, align);
            if (p != nullptr) {
                AA_CHECK(is_aligned(p, align));
                *static_cast<unsigned char*>(p) = 0x11; // usable region
                pool.deallocate(p);
            }
            AA_CHECK(pool.bytesUsed() == 0);
        }

        // A request whose size alone cannot fit is a deterministic miss.
        const std::size_t before = pool.allocationFailures();
        AA_CHECK(pool.allocate(pool.capacity(), 128) == nullptr);
        AA_CHECK(pool.allocationFailures() == before + 1);
        AA_CHECK(pool.bytesUsed() == 0);

        void* p = pool.allocate(1); // a default-aligned small allocation still works
        AA_CHECK(p != nullptr);
        pool.deallocate(p);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // ---- invalid (non-power-of-two) alignment is rejected; 0 means default ----
    {
        FramePool<256> pool;
        const std::size_t before = pool.allocationFailures();
        AA_CHECK(pool.allocate(16, 48) == nullptr); // 48 is not a power of two
        AA_CHECK(pool.allocate(16, 24) == nullptr);
        AA_CHECK(pool.allocationFailures() == before + 2);

        void* z = pool.allocate(16, 0); // 0 -> default alignment
        AA_CHECK(z != nullptr);
        AA_CHECK(is_aligned(z, kMaxAlign));
        pool.deallocate(z);
        AA_CHECK(pool.bytesUsed() == 0);
    }

    // The pool never touched the global heap across ANY operation above.
    AA_CHECK(g_global_new_calls == newAtStart);

    AA_RUN_TESTS();
}

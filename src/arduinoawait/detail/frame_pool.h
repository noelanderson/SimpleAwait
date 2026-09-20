#pragma once

// ArduinoAwait — fixed coroutine frame allocator.
//
// A statically sized byte arena that hands out variable-size, aligned blocks for
// coroutine frames without ever touching the global heap. It is a coalescing
// first-fit free list:
//
//   * fixed capacity (no heap fallback — allocate() returns nullptr on exhaustion
//     and records a failure; the caller applies the deterministic error policy);
//   * variable-size blocks with arbitrary free order;
//   * alignment honored up to and beyond alignof(std::max_align_t) (coroutine
//     frames allocated via operator new(size_t) need max_align; an over-aligned
//     request is satisfied by padding within the block);
//   * adjacent free blocks coalesce, so all capacity is recovered once every
//     block is freed (bytesUsed() == 0).
//
// Block headers live inside the arena. They are created with placement new and
// re-accessed through std::launder so the implementation is well-defined under
// strict aliasing / object-lifetime rules (validated under ASan/UBSan in CI).
//
// Task counts are intentionally small, so the O(n) walk for allocate/deallocate/
// coalesce is acceptable (see AGENTS.md §8, §18).

#include <cstddef>
#include <cstdint>
#include <new>

#include "../config.h"
#include "../error.h"

namespace arduinoawait {
namespace detail {

template <size_t Bytes>
class FramePool {
    struct BlockHeader {
        uint32_t size;          // total block size incl header, multiple of kAlign
        uint32_t payloadOffset; // offset from block start to the returned pointer
        bool allocated;
    };

    static constexpr size_t kAlign = alignof(std::max_align_t);
    // Effective header slot: the block header occupies the first sizeof(BlockHeader)
    // bytes, rounded up to kAlign so the payload that follows is kAlign-aligned.
    static constexpr size_t kHeader = (sizeof(BlockHeader) + kAlign - 1) & ~(kAlign - 1);
    static constexpr size_t kUsable = Bytes - (Bytes % kAlign);

    static_assert(kHeader % kAlign == 0, "header slot must be a multiple of kAlign");
    static_assert(Bytes >= kHeader + kAlign, "FramePool capacity too small");

public:
    FramePool() noexcept { reset(); }

    FramePool(const FramePool&) = delete;
    FramePool& operator=(const FramePool&) = delete;

    // Re-initialize to a single free block. Intended for test isolation and
    // startup; not for reclaiming live frames.
    void reset() noexcept {
        ::new (static_cast<void*>(storage_))
            BlockHeader{static_cast<uint32_t>(kUsable),
                        static_cast<uint32_t>(kHeader), false};
        bytesUsed_ = 0;
        peakBytesUsed_ = 0;
        allocationFailures_ = 0;
    }

    // Allocate `size` bytes aligned to at least `align` (rounded up to kAlign).
    // Returns a suitably aligned pointer, or nullptr on exhaustion (recording a
    // failure). Never allocates from the global heap.
    [[nodiscard]] void* allocate(size_t size, size_t align = kAlign) noexcept {
        if (align < kAlign) {
            align = kAlign;
        }

        size_t off = 0;
        while (off < kUsable) {
            BlockHeader* blk = header_at(off);
            const size_t blkSize = blk->size;

            if (!blk->allocated) {
                std::byte* const start = storage_ + off;
                std::byte* const payload = align_up_ptr(start + kHeader, align);
                const size_t payloadOffset =
                    static_cast<size_t>(payload - start);
                const size_t needed = round_up(payloadOffset + size, kAlign);

                if (needed <= blkSize) {
                    const size_t remainder = blkSize - needed;
                    if (remainder >= kHeader + kAlign) {
                        ::new (static_cast<void*>(storage_ + off + needed))
                            BlockHeader{static_cast<uint32_t>(remainder),
                                        static_cast<uint32_t>(kHeader), false};
                        blk->size = static_cast<uint32_t>(needed);
                    }
                    blk->allocated = true;
                    blk->payloadOffset = static_cast<uint32_t>(payloadOffset);
                    bytesUsed_ += blk->size;
                    if (bytesUsed_ > peakBytesUsed_) {
                        peakBytesUsed_ = bytesUsed_;
                    }
                    return static_cast<void*>(payload);
                }
            }

            off += blkSize;
        }

        ++allocationFailures_;
        return nullptr;
    }

    // Free a block previously returned by allocate(). Coalesces adjacent free
    // blocks. Passing a pointer not currently allocated by this pool (double free
    // or foreign pointer) is a deterministic programming error.
    void deallocate(void* p) noexcept {
        if (p == nullptr) {
            return;
        }

        size_t off = 0;
        while (off < kUsable) {
            BlockHeader* blk = header_at(off);
            const size_t blkSize = blk->size;

            if (blk->allocated &&
                static_cast<void*>(storage_ + off + blk->payloadOffset) == p) {
                blk->allocated = false;
                bytesUsed_ -= blkSize;
                coalesce();
                return;
            }

            off += blkSize;
        }

        ARDUINOAWAIT_ON_ERROR(Error::internal_error);
    }

    size_t bytesUsed() const noexcept { return bytesUsed_; }
    size_t bytesFree() const noexcept { return kUsable - bytesUsed_; }
    size_t peakBytesUsed() const noexcept { return peakBytesUsed_; }
    size_t allocationFailures() const noexcept { return allocationFailures_; }

    static constexpr size_t capacity() noexcept { return kUsable; }

    // Per-allocation header overhead in bytes: each live block consumes this plus
    // its (aligned) payload. Useful for sizing a pool to a known frame count.
    static constexpr size_t blockOverhead() noexcept { return kHeader; }

private:
    BlockHeader* header_at(size_t off) noexcept {
        return std::launder(reinterpret_cast<BlockHeader*>(storage_ + off));
    }

    static std::byte* align_up_ptr(std::byte* p, size_t align) noexcept {
        const uintptr_t v = reinterpret_cast<uintptr_t>(p);
        const uintptr_t aligned =
            (v + (align - 1)) & ~static_cast<uintptr_t>(align - 1);
        return p + (aligned - v);
    }

    static constexpr size_t round_up(size_t v, size_t a) noexcept {
        return (v + (a - 1)) & ~(a - 1);
    }

    // Merge every run of adjacent free blocks into one.
    void coalesce() noexcept {
        size_t off = 0;
        while (off < kUsable) {
            BlockHeader* blk = header_at(off);
            const size_t nextOff = off + blk->size;
            if (!blk->allocated && nextOff < kUsable) {
                BlockHeader* nxt = header_at(nextOff);
                if (!nxt->allocated) {
                    blk->size = static_cast<uint32_t>(blk->size + nxt->size);
                    continue; // re-examine the merged block without advancing
                }
            }
            off = nextOff;
        }
    }

    alignas(alignof(std::max_align_t)) std::byte storage_[Bytes];
    size_t bytesUsed_ = 0;
    size_t peakBytesUsed_ = 0;
    size_t allocationFailures_ = 0;
};

} // namespace detail
} // namespace arduinoawait

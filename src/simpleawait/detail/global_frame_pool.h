#pragma once

// SimpleAwait — the process-wide coroutine frame pool instance.
//
// Coroutine frames are allocated from this single fixed-capacity pool (sized by
// SIMPLEAWAIT_FRAME_POOL_BYTES) via the Task promise's operator new/delete, so no
// coroutine frame ever touches the global heap. A function-local static gives one
// instance across translation units with lazy, ordered initialization.

#include "../config.h"
#include "frame_pool.h"

namespace simpleawait {
namespace detail {

using DefaultFramePool = FramePool<SIMPLEAWAIT_FRAME_POOL_BYTES>;

inline DefaultFramePool& frame_pool() noexcept {
    static DefaultFramePool pool;
    return pool;
}

} // namespace detail
} // namespace simpleawait

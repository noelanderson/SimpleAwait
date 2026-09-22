#pragma once

// ArduinoAwait — waitUntil: header-defined coroutine composition (V1_API_CONTRACT
// §12, ARCHITECTURE §18).
//
// waitUntil(predicate) suspends the calling task at a fair yield point until the
// predicate returns true. It is a pure composition over yield() and Task<void> —
// NOT a scheduler primitive: the scheduler never stores, type-erases, or polls the
// predicate. The predicate is evaluated in scheduler context, once per poll() pass
// at a fair yield point, so other tasks keep running while a task waits. A predicate
// that is already true completes after a single evaluation (one poll pass) without
// an additional suspension.
//
// The predicate is taken by value and lives in the coroutine frame for the wait's
// duration. Typical use is either `co_await waitUntil(pred);` (as a child await) or
// `spawn(waitUntil(pred));`.

#include "delay.h" // yield()
#include "task.h"

namespace arduinoawait {

// GCC emits -Wsubobject-linkage (part of -Wall) because the compiler-generated
// coroutine frame for waitUntil<Predicate> holds the predicate, and the natural
// argument is a block-scope lambda whose closure type has no linkage. The frame is
// never named across translation units, so this is benign; suppress it here so the
// frozen lambda-friendly API stays warning-clean under -Werror (Clang/MSVC do not
// warn).
#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif

template <class Predicate>
Task<void> waitUntil(Predicate predicate) {
    while (!predicate()) {
        co_await yield();
    }
}

#if defined(__GNUC__) && !defined(__clang__)
#  pragma GCC diagnostic pop
#endif

} // namespace arduinoawait

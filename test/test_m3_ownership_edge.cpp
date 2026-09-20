// M3 Task<void> ownership edge cases (regressions for the move-ordering bugs).
//
// Both bugs only manifest for coroutines whose frames hold nontrivial by-value
// objects (a Task, or a guard whose destructor re-enters the owning Task), so
// the basic ownership test could not see them. The recording error hook lets a
// double-free surface as Error::internal_error instead of halting.

#include <cstddef>
#include <cstdint>
#include <utility>

namespace {
int g_last_error = -1;
}
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::Error;
using arduinoawait::Task;
using arduinoawait::detail::frame_pool;

namespace {

// ---- B1: move-assign whose SOURCE lives inside the destination's frame -------
Task<void>* g_recorded_child = nullptr;

struct ChildHolder {
    Task<void> child;
    explicit ChildHolder(Task<void>&& c) noexcept : child(std::move(c)) {}
    ChildHolder(ChildHolder&& o) noexcept : child(std::move(o.child)) {
        g_recorded_child = &child; // address of the child once it lives in the frame
    }
};

Task<void> leaf() { co_return; }
Task<void> holder_coro(ChildHolder) { co_return; }

// ---- B2: a by-value parameter whose destructor re-enters the owning Task -----
int g_owned_guard_dtors = 0;
bool g_owner_empty_at_dtor = false;

struct ReentrantGuard {
    Task<void>* owner = nullptr;
    ReentrantGuard() = default;
    ReentrantGuard(ReentrantGuard&& o) noexcept : owner(o.owner) { o.owner = nullptr; }
    ReentrantGuard& operator=(ReentrantGuard&&) = delete;
    ~ReentrantGuard() {
        if (owner != nullptr) {
            ++g_owned_guard_dtors;
            // With correct detach-before-destroy, the owner's handle is already
            // cleared by the time its frame (holding us) is destroyed, so the
            // owner already reports empty. The buggy order destroys first and
            // still reports non-empty here.
            g_owner_empty_at_dtor = !static_cast<bool>(*owner);
            *owner = Task<void>{}; // re-enter: must be a safe no-op, not a double destroy
        }
    }
};

Task<void> guarded_coro(ReentrantGuard) { co_return; }

} // namespace

int main() {
    const std::size_t base = frame_pool().bytesUsed();

    // ---- B1: outer = std::move(child-inside-outer's-frame) ----
    {
        g_recorded_child = nullptr;
        Task<void> outer = holder_coro(ChildHolder{leaf()});
        AA_CHECK(static_cast<bool>(outer));
        AA_CHECK(g_recorded_child != nullptr);
        AA_CHECK(static_cast<bool>(*g_recorded_child)); // in-frame child owns leaf's frame
        AA_CHECK(frame_pool().bytesUsed() > base);      // outer's frame + leaf's frame

        outer = std::move(*g_recorded_child);           // the dangerous move-assign

        // Correct behavior: outer takes leaf's frame; its old frame (holding the
        // now-empty child) is freed. Buggy behavior silently empties outer.
        AA_CHECK(static_cast<bool>(outer));
        // g_recorded_child now dangles (old frame freed); do not dereference it.
    }
    AA_CHECK(frame_pool().bytesUsed() == base); // full recovery

    // ---- B2: destroying a frame whose parameter destructor re-enters the Task ----
    {
        g_owned_guard_dtors = 0;
        g_last_error = -1;

        Task<void> t;
        {
            ReentrantGuard g;
            g.owner = &t;
            t = guarded_coro(std::move(g)); // frame holds the guard with owner=&t
            AA_CHECK(static_cast<bool>(t));
        } // the stack guard (owner nulled by the move) destructs harmlessly

        t = Task<void>{}; // destroy t's frame -> guard dtor re-enters -> empties t

        AA_CHECK(g_owned_guard_dtors == 1);   // exactly once: no double destruction
        AA_CHECK(g_owner_empty_at_dtor);      // detached before destroy (ordering)
        AA_CHECK(g_last_error != static_cast<int>(Error::internal_error)); // no double free
        AA_CHECK(!static_cast<bool>(t));
    }
    AA_CHECK(frame_pool().bytesUsed() == base);

    AA_RUN_TESTS();
}

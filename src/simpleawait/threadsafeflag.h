#pragma once

// SimpleAwait — ThreadSafeFlag: single-waiter, auto-reset, coalescing external
// signal (V1_API_CONTRACT §10, ARCHITECTURE §16).
//
// set() is the ONLY method callable from a supported external/IRQ/callback/other-
// core context: under a short platform critical section it marks the flag signaled
// and the scheduler externally-pending, then returns — it never manipulates
// scheduler lists and never resumes coroutine code. poll() later resolves signaled
// flags in scheduler context and wakes the single waiter (auto-reset on consume;
// repeated set() while signaled coalesces). wait()/clear()/isSet() are
// scheduler-context operations. A second simultaneous waiter is a deterministic
// programming error (multiple_flag_waiters).

#include <coroutine>

#include "config.h"
#include "error.h"
#include "detail/platform_sync.h"
#include "scheduler.h"

namespace simpleawait {

class ThreadSafeFlag {
public:
    ThreadSafeFlag() noexcept = default;
    ThreadSafeFlag(const ThreadSafeFlag&) = delete;
    ThreadSafeFlag& operator=(const ThreadSafeFlag&) = delete;

    ~ThreadSafeFlag() {
        // Destroying a flag with a parked waiter is a programming error. Unarm
        // FIRST so poll() can never dereference this freed flag, then report it.
        if (waiter_ != nullptr) {
            unarm();
            SIMPLEAWAIT_ON_ERROR(Error::object_destroyed_with_waiters);
        }
    }

    class Awaiter {
    public:
        explicit Awaiter(ThreadSafeFlag* flag) noexcept : flag_(flag) {}

        bool await_ready() const noexcept { return flag_->consume(); }
        bool await_suspend(std::coroutine_handle<> awaiting) const noexcept {
            return flag_->park(awaiting);
        }
        void await_resume() const noexcept {}

    private:
        ThreadSafeFlag* flag_;
    };

    [[nodiscard]] Awaiter wait() noexcept { return Awaiter{this}; }

    // External-context safe: mark signaled + scheduler externally pending, then
    // return. No scheduler list manipulation, no coroutine resumption.
    void set() noexcept {
        [[maybe_unused]] detail::CriticalSection cs;
        signaled_ = true;
        s_pending_ = true;
    }

    void clear() noexcept {
        [[maybe_unused]] detail::CriticalSection cs;
        signaled_ = false;
    }
    bool isSet() const noexcept {
        [[maybe_unused]] detail::CriticalSection cs;
        return signaled_;
    }

private:
    friend void detail::poll_external_signals() noexcept;

    // If signaled, consume the signal (auto-reset) and report true so wait() does
    // not suspend; otherwise false. Protected against a concurrent set().
    bool consume() noexcept {
        [[maybe_unused]] detail::CriticalSection cs;
        if (signaled_) {
            signaled_ = false;
            return true;
        }
        return false;
    }

    // Park the running task as this flag's single waiter. Returns true if the
    // caller must stay suspended; false otherwise. The awaiting-handle validation
    // takes PRECEDENCE over the single-waiter check: a foreign/nested await is
    // always invalid_task (never masked by multiple_flag_waiters), and it neither
    // suspends the caller nor mutates the flag.
    bool park(std::coroutine_handle<> awaiting) noexcept {
        bool suspend = false;
        if (!scheduler().running_is(awaiting)) {
            // foreign/nested await: invalid_task reported; do not suspend or arm
        } else if (waiter_ != nullptr) {
            SIMPLEAWAIT_ON_ERROR(Error::multiple_flag_waiters);
        } else {
            waiter_ = scheduler().park_running();
            arm();
            suspend = true;
        }
        return suspend; // reachable via the success path; no code after a hook
    }

    // Scheduler-context armed-list membership (flags with a waiting task). Only
    // touched in scheduler context, so it needs no critical section.
    void arm() noexcept {
        armed_next_ = s_armed_head_;
        s_armed_head_ = this;
    }
    void unarm() noexcept {
        for (ThreadSafeFlag** link = &s_armed_head_; *link != nullptr;
             link = &(*link)->armed_next_) {
            if (*link == this) {
                *link = armed_next_;
                break;
            }
        }
        armed_next_ = nullptr;
        waiter_ = nullptr;
    }

    bool signaled_ = false;                // cross-context (CriticalSection-protected)
    Scheduler::Slot* waiter_ = nullptr;     // scheduler-context single waiter
    ThreadSafeFlag* armed_next_ = nullptr;  // scheduler-context armed-list link

    inline static ThreadSafeFlag* s_armed_head_ = nullptr; // scheduler-context
    inline static bool s_pending_ = false;                 // cross-context
};

namespace detail {
inline void poll_external_signals() noexcept {
    // Fast exit unless an external set() is pending (checked under protection).
    {
        [[maybe_unused]] detail::CriticalSection cs;
        if (!ThreadSafeFlag::s_pending_) {
            return;
        }
        ThreadSafeFlag::s_pending_ = false;
    }
    // Resolve in scheduler context: wake the waiter of each signaled armed flag
    // and remove it from the armed list. wake_slot() runs outside the critical
    // section (it is scheduler-context ready-queue work, never called from an ISR).
    ThreadSafeFlag* prev = nullptr;
    ThreadSafeFlag* f = ThreadSafeFlag::s_armed_head_;
    while (f != nullptr) {
        bool wake = false;
        {
            [[maybe_unused]] detail::CriticalSection cs;
            if (f->signaled_ && f->waiter_ != nullptr) {
                f->signaled_ = false; // consume (auto-reset)
                wake = true;
            }
        }
        ThreadSafeFlag* const next = f->armed_next_;
        if (wake) {
            scheduler().wake_slot(f->waiter_);
            f->waiter_ = nullptr;
            f->armed_next_ = nullptr;
            if (prev == nullptr) {
                ThreadSafeFlag::s_armed_head_ = next;
            } else {
                prev->armed_next_ = next;
            }
        } else {
            prev = f;
        }
        f = next;
    }
}
} // namespace detail

} // namespace simpleawait

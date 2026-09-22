#pragma once

// SimpleAwait — Event: scheduler-local, manual-reset, multi-waiter (V1 §9).
//
// wait() on a set Event completes without suspension; wait() on a clear Event
// appends the task to the Event's FIFO waiter queue. set() latches the Event and
// wakes all current waiters in FIFO order (they run on a LATER poll() pass);
// clear() resets and affects only future waits. Event is scheduler-context only:
// there is no ISR set() (use ThreadSafeFlag for external contexts). Destroying an
// Event that still has waiters is a deterministic
// programming error (Error::object_destroyed_with_waiters).

#include <coroutine>

#include "config.h"
#include "error.h"
#include "scheduler.h"

namespace simpleawait {

class Event {
public:
    Event() noexcept = default;
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    ~Event() {
        if (!waiters_.empty()) {
            SIMPLEAWAIT_ON_ERROR(Error::object_destroyed_with_waiters);
        }
    }

    class Awaiter {
    public:
        explicit Awaiter(Event* ev) noexcept : ev_(ev) {}

        bool await_ready() const noexcept { return ev_->set_; }
        bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
            return ev_->park(awaiting);
        }
        void await_resume() const noexcept {}

    private:
        Event* ev_;
    };

    [[nodiscard]] Awaiter wait() noexcept { return Awaiter{this}; }

    void set() {
        set_ = true;
        scheduler().wake_all(waiters_);
    }
    void clear() noexcept { set_ = false; }
    bool isSet() const noexcept { return set_; }

private:
    // Park the running task on this Event's waiter queue. Returns false (do not
    // suspend) when the awaiting coroutine is not the running task, so a foreign
    // caller is not stranded (mirrors the child-await validation).
    bool park(std::coroutine_handle<> awaiting) noexcept {
        return scheduler().wait_on(waiters_, awaiting);
    }

    bool set_ = false;
    Scheduler::WaitQueue waiters_;
};

} // namespace simpleawait

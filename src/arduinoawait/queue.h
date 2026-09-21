#pragma once

// ArduinoAwait — Queue<T, Capacity>: bounded, scheduler-local FIFO with blocking
// send/receive (V1_API_CONTRACT §11, ARCHITECTURE §17).
//
// Data order and waiter order are both FIFO. `send()` completes immediately when a
// receiver is waiting (direct hand-off) or capacity exists; otherwise the sender
// suspends in FIFO order carrying its value on its own coroutine frame. `receive()`
// completes immediately when data is buffered; otherwise the receiver suspends in
// FIFO order. A receive that frees a slot admits the oldest waiting sender (its
// value moves to the tail, preserving send order); a send that finds a waiting
// receiver hands the value straight to it. Wakeups ENQUEUE tasks (via
// `wake_slot()`); they never inline-resume, matching the §9 poll() model.
//
// Storage is a fixed ring of aligned raw cells with placement construction/
// destruction, so T need not be default-constructible and no heap is used. There
// are no ISR methods in V1 (external contexts use ThreadSafeFlag). Destroying a
// Queue that still has parked senders or receivers is a deterministic programming
// error (Error::object_destroyed_with_waiters).

#include <coroutine>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "config.h"
#include "error.h"
#include "scheduler.h"

namespace arduinoawait {

template <class T, size_t Capacity>
class Queue {
    static_assert(Capacity > 0, "Queue capacity must be greater than 0");

public:
    Queue() noexcept = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    ~Queue() {
        // Detach any still-parked awaiter nodes so their destructors — which may run
        // later, e.g. when the scheduler tears down the parked frames at shutdown,
        // after this queue is already gone — do not touch this destroyed queue.
        // Destroying a queue that still has live waiters remains a deterministic
        // programming error.
        for (SendAwaiter* s = send_head_; s != nullptr; s = s->next_) {
            s->linked_ = false;
        }
        for (ReceiveAwaiter* r = recv_head_; r != nullptr; r = r->next_) {
            r->linked_ = false;
        }
        if (send_head_ != nullptr || recv_head_ != nullptr) {
            ARDUINOAWAIT_ON_ERROR(Error::object_destroyed_with_waiters);
        }
        // Destroy any buffered payloads exactly once.
        while (count_ > 0) {
            pop_front_storage();
        }
    }

    // Suspends until the value is buffered or handed to a waiting receiver. The
    // value lives in the awaiter (on the sending coroutine's frame) while parked,
    // so no default-construction or heap is required.
    class SendAwaiter {
    public:
        SendAwaiter(Queue* q, const T& value) : q_(q), value_(value) {}
        SendAwaiter(Queue* q, T&& value) : q_(q), value_(std::move_if_noexcept(value)) {}
        SendAwaiter(const SendAwaiter&) = delete;
        SendAwaiter& operator=(const SendAwaiter&) = delete;
        ~SendAwaiter() {
            if (linked_) {
                q_->unlink_sender(this); // frame destroyed while still parked
            }
        }

        // Deliver by move when T is nothrow-movable, else by copy: send(const T&)
        // must accept a copy-only payload (frozen §11) that has no move constructor.
        bool await_ready() { return q_->do_send(std::move_if_noexcept(value_)); }
        bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
            return q_->park_sender(this, awaiting);
        }
        void await_resume() const noexcept {}

    private:
        friend class Queue;
        Queue* q_;
        T value_;
        SendAwaiter* next_ = nullptr;
        Scheduler::Slot* slot_ = nullptr;
        bool linked_ = false;
    };

    // Suspends until a value is available. The result is placement-constructed into
    // the awaiter's aligned storage (either here on an immediate receive, or by a
    // sender's direct hand-off while parked) and moved out in await_resume().
    class ReceiveAwaiter {
    public:
        explicit ReceiveAwaiter(Queue* q) noexcept : q_(q) {}
        ReceiveAwaiter(const ReceiveAwaiter&) = delete;
        ReceiveAwaiter& operator=(const ReceiveAwaiter&) = delete;
        ~ReceiveAwaiter() {
            if (linked_) {
                q_->unlink_receiver(this); // frame destroyed while still parked
            }
            if (has_result_) {
                result_ptr()->~T(); // constructed but never consumed (e.g. shutdown)
            }
        }

        bool await_ready() {
            if (q_->count_ == 0) {
                return false;
            }
            construct_result(std::move_if_noexcept(*q_->storage_ptr(q_->head_)));
            q_->pop_front_storage();
            q_->admit_oldest_sender();
            return true;
        }
        bool await_suspend(std::coroutine_handle<> awaiting) noexcept {
            return q_->park_receiver(this, awaiting);
        }
        T await_resume() {
            // The result is present on the normal path (constructed in await_ready,
            // or handed off by a sender while parked). It is absent only when a
            // foreign/nested await could not park (invalid_task was reported in
            // await_suspend and await_suspend returned false). In that case, for a
            // default-constructible T return a value-initialized T; otherwise report
            // and halt deterministically rather than fabricate a T.
            if (!has_result_) {
                if constexpr (std::is_default_constructible_v<T>) {
                    return T{};
                } else {
                    // No value exists and none can be synthesized for this T. Report
                    // and never fall through: a configured hook that RETURNS must not
                    // reach the move of a non-existent object (lifetime UB). Looping
                    // the report is unconditionally non-returning yet leaves no
                    // unreachable statement after a [[noreturn]] hook. Only reachable
                    // via a foreign/nested await.
                    for (;;) {
                        ARDUINOAWAIT_ON_ERROR(Error::invalid_task);
                    }
                }
            }
            // Move/copy the stored result into the return object, then destroy the
            // stored copy. Returning the move_if_noexcept EXPRESSION (not a named
            // local) avoids selecting a deleted move constructor for a copy-only T,
            // and the guard destroys the stored object AFTER the return value has
            // been constructed. Reached only via the has_result_ path; no C4702.
            struct ResultGuard {
                T* p;
                bool* consumed;
                ~ResultGuard() {
                    p->~T();
                    *consumed = false;
                }
            } guard{result_ptr(), &has_result_};
            return std::move_if_noexcept(*result_ptr());
        }

    private:
        friend class Queue;
        template <class U>
        void construct_result(U&& value) {
            ::new (static_cast<void*>(&result_storage_)) T(std::forward<U>(value));
            has_result_ = true;
        }
        T* result_ptr() noexcept {
            return std::launder(reinterpret_cast<T*>(&result_storage_));
        }

        Queue* q_;
        alignas(T) unsigned char result_storage_[sizeof(T)];
        bool has_result_ = false;
        ReceiveAwaiter* next_ = nullptr;
        Scheduler::Slot* slot_ = nullptr;
        bool linked_ = false;
    };

    [[nodiscard]] SendAwaiter send(const T& value) { return SendAwaiter{this, value}; }
    [[nodiscard]] SendAwaiter send(T&& value) { return SendAwaiter{this, std::move(value)}; }
    [[nodiscard]] ReceiveAwaiter receive() noexcept { return ReceiveAwaiter{this}; }

    bool trySend(const T& value) { return do_send(value); }
    bool trySend(T&& value) { return do_send(std::move_if_noexcept(value)); }
    bool tryReceive(T& out) {
        if (count_ == 0) {
            return false;
        }
        out = std::move_if_noexcept(*storage_ptr(head_));
        pop_front_storage();
        admit_oldest_sender();
        return true;
    }

    bool empty() const noexcept { return count_ == 0; }
    bool full() const noexcept { return count_ == Capacity; }
    size_t size() const noexcept { return count_; }
    static constexpr size_t capacity() noexcept { return Capacity; }

private:
    static size_t next_index(size_t i) noexcept { return (i + 1 == Capacity) ? 0 : i + 1; }
    size_t tail_index() const noexcept {
        size_t t = head_ + count_;
        if (t >= Capacity) {
            t -= Capacity;
        }
        return t;
    }
    T* storage_ptr(size_t i) noexcept {
        return std::launder(reinterpret_cast<T*>(&cells_[i]));
    }
    template <class U>
    void store_back(U&& value) {
        ::new (static_cast<void*>(&cells_[tail_index()])) T(std::forward<U>(value));
        ++count_;
    }
    void pop_front_storage() noexcept {
        storage_ptr(head_)->~T();
        head_ = next_index(head_);
        --count_;
    }

    // Non-suspending delivery: hand to the oldest waiting receiver (queue is empty
    // in that case) or buffer if capacity exists; otherwise report failure.
    template <class U>
    bool do_send(U&& value) {
        if (recv_head_ != nullptr) {
            ReceiveAwaiter* r = recv_pop_front();
            r->construct_result(std::forward<U>(value));
            scheduler().wake_slot(r->slot_);
            return true;
        }
        if (count_ < Capacity) {
            store_back(std::forward<U>(value));
            return true;
        }
        return false;
    }

    // After a receive frees a slot, admit the oldest waiting sender (queue was full
    // in that case): buffer its value at the tail — preserving send order — and
    // wake it.
    void admit_oldest_sender() {
        if (send_head_ != nullptr) {
            SendAwaiter* s = send_pop_front();
            store_back(std::move_if_noexcept(s->value_));
            scheduler().wake_slot(s->slot_);
        }
    }

    bool park_sender(SendAwaiter* a, std::coroutine_handle<> awaiting) noexcept {
        bool suspend = false;
        if (scheduler().running_is(awaiting)) {
            a->slot_ = scheduler().park_running();
            a->next_ = nullptr;
            if (send_tail_ == nullptr) {
                send_head_ = a;
            } else {
                send_tail_->next_ = a;
            }
            send_tail_ = a;
            a->linked_ = true;
            suspend = true;
        }
        return suspend; // foreign/nested await -> invalid_task, do not strand/park
    }
    SendAwaiter* send_pop_front() noexcept {
        SendAwaiter* a = send_head_;
        send_head_ = a->next_;
        if (send_head_ == nullptr) {
            send_tail_ = nullptr;
        }
        a->next_ = nullptr;
        a->linked_ = false;
        return a;
    }
    // O(n) removal of a specific parked sender whose frame is being destroyed while
    // still linked (e.g. scheduler shutdown). Permitted per the WaitQueue rules.
    void unlink_sender(SendAwaiter* a) noexcept {
        SendAwaiter* prev = nullptr;
        for (SendAwaiter* s = send_head_; s != nullptr; prev = s, s = s->next_) {
            if (s == a) {
                if (prev == nullptr) {
                    send_head_ = s->next_;
                } else {
                    prev->next_ = s->next_;
                }
                if (send_tail_ == s) {
                    send_tail_ = prev;
                }
                break;
            }
        }
        a->next_ = nullptr;
        a->linked_ = false;
    }

    bool park_receiver(ReceiveAwaiter* a, std::coroutine_handle<> awaiting) noexcept {
        bool suspend = false;
        if (scheduler().running_is(awaiting)) {
            a->slot_ = scheduler().park_running();
            a->next_ = nullptr;
            if (recv_tail_ == nullptr) {
                recv_head_ = a;
            } else {
                recv_tail_->next_ = a;
            }
            recv_tail_ = a;
            a->linked_ = true;
            suspend = true;
        }
        return suspend;
    }
    ReceiveAwaiter* recv_pop_front() noexcept {
        ReceiveAwaiter* a = recv_head_;
        recv_head_ = a->next_;
        if (recv_head_ == nullptr) {
            recv_tail_ = nullptr;
        }
        a->next_ = nullptr;
        a->linked_ = false;
        return a;
    }
    void unlink_receiver(ReceiveAwaiter* a) noexcept {
        ReceiveAwaiter* prev = nullptr;
        for (ReceiveAwaiter* r = recv_head_; r != nullptr; prev = r, r = r->next_) {
            if (r == a) {
                if (prev == nullptr) {
                    recv_head_ = r->next_;
                } else {
                    prev->next_ = r->next_;
                }
                if (recv_tail_ == r) {
                    recv_tail_ = prev;
                }
                break;
            }
        }
        a->next_ = nullptr;
        a->linked_ = false;
    }

    struct alignas(T) Cell {
        unsigned char bytes[sizeof(T)];
    };

    Cell cells_[Capacity];
    size_t head_ = 0;
    size_t count_ = 0;
    SendAwaiter* send_head_ = nullptr;
    SendAwaiter* send_tail_ = nullptr;
    ReceiveAwaiter* recv_head_ = nullptr;
    ReceiveAwaiter* recv_tail_ = nullptr;
};

} // namespace arduinoawait

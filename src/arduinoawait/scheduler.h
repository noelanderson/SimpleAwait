#pragma once

// ArduinoAwait — cooperative scheduler: fixed task slots, generation-safe
// handles, a FIFO ready queue, and a bounded, reentry-guarded poll() pass.
//
// The scheduler owns each scheduled coroutine frame (transferred from the Task).
// It stores fixed metadata per task slot (no heap), identifies tasks by
// slot+generation so a stale handle cannot alias a reused slot, and resumes each
// ready task through poll(). Per the frozen V1 model (ARCHITECTURE §9), one
// poll() pass resumes only the tasks that were ready at the START of the pass;
// tasks made ready during the pass run in a later pass, and no task runs twice
// in one pass.

#include <coroutine>
#include <cstddef>
#include <cstdint>

#include "config.h"
#include "error.h"
#include "detail/platform_clock.h"
#include "detail/time_math.h"
#include "task.h"

namespace arduinoawait {

using TaskSlot = uint16_t;
using TaskGeneration = uint32_t;

struct TaskId {
    TaskSlot slot = 0;
    TaskGeneration generation = 0;

    friend constexpr bool operator==(TaskId, TaskId) = default;
};

class Scheduler;

// Copyable, observational identity for a scheduled task. It never owns the
// coroutine frame. Generation-checked: a handle to a completed task stays valid
// (done() == true) until the slot is reused, after which the older handle
// becomes invalid.
class TaskHandle {
public:
    constexpr TaskHandle() noexcept = default;

    bool valid() const noexcept;
    bool done() const noexcept;
    TaskId id() const noexcept { return id_; }

    explicit operator bool() const noexcept { return valid(); }

private:
    friend class Scheduler;
    constexpr TaskHandle(const Scheduler* sched, TaskId id) noexcept
        : sched_(sched), id_(id) {}

    const Scheduler* sched_ = nullptr;
    TaskId id_{};
};

// Free-function scheduling API (V1_API_CONTRACT §6). Declared before Scheduler
// so the class can grant friendship to the scheduling entry points; the frozen
// Scheduler surface (§7) therefore does not expose create_task()/spawn(). The
// exception specifications match the frozen contract exactly (create_task/spawn
// are not noexcept; current_task() is).
[[nodiscard]] TaskHandle create_task(Task<void>&& task);
void spawn(Task<void>&& task);
TaskHandle current_task() noexcept;
void poll();

namespace detail {
// Test-only seam: force a scheduler slot's generation, to exercise generation
// retirement near the uint32 boundary without 2^32 real reuses. NOT public API.
void force_slot_generation(Scheduler& sched, TaskSlot slot, TaskGeneration generation) noexcept;
} // namespace detail

// Timer/yield awaitables (V1_API_CONTRACT §8). Forward-declared so the Scheduler
// can grant them access to the running task's suspend/timer hooks.
class YieldAwaitable;
class DelayAwaitable;

class Scheduler {
public:
    Scheduler() noexcept = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    ~Scheduler() {
        // Teardown: destroying a suspended coroutine frame runs its by-value
        // parameter destructors, which may reenter the scheduler (poll() or
        // create_task()). Guard against that: mark shutdown first (so poll() is a
        // no-op and schedule() is refused), and detach each slot BEFORE destroying
        // its frame so a reentrant call can never observe it as still owning a
        // frame or still linked. Each owned frame is thus destroyed exactly once.
        shutting_down_ = true;
        for (Slot& s : slots_) {
            if (s.handle && s.state != State::completed) {
                const std::coroutine_handle<> frame = s.handle;
                s.handle = {};
                s.state = State::completed;
                frame.destroy();
            }
        }
    }

    // The task currently being resumed, or an invalid handle outside a resume.
    // Scheduling uses the free functions create_task()/spawn() (see §6).
    TaskHandle currentTask() const noexcept {
        if (current_ == nullptr) {
            return TaskHandle{};
        }
        return TaskHandle{this, TaskId{index_of(current_), current_->generation}};
    }

    // One bounded scheduler pass (V1 frozen semantics, ARCHITECTURE §9).
    void poll() {
        if (shutting_down_) {
            return; // teardown in progress: never run a pass
        }
        if (in_poll_) {
            ARDUINOAWAIT_ON_ERROR(Error::scheduler_reentry);
        } else {
            in_poll_ = true;
            now_ = detail::platform_now_us(); // §9 step 1: sample the 64-bit clock
            process_due_timers();             // §9 step 5: enqueue due timers (deterministic)
            run_pass();
            in_poll_ = false;
        }
    }

    bool hasReadyTasks() const noexcept { return ready_count_ > 0; }
    bool hasPendingTasks() const noexcept { return active_count_ > 0; }
    size_t activeTaskCount() const noexcept { return active_count_; }

private:
    friend class TaskHandle;
    friend TaskHandle create_task(Task<void>&& task);
    friend void spawn(Task<void>&& task);
    friend void detail::force_slot_generation(Scheduler&, TaskSlot, TaskGeneration) noexcept;
    friend class YieldAwaitable;
    friend class DelayAwaitable;

    static constexpr size_t kMaxTasks = ARDUINOAWAIT_MAX_TASKS;

    // Every slot index (0..kMaxTasks-1) must be representable in TaskSlot, or a
    // handle's identity could be aliased by narrowing in index_of() (M1).
    static_assert(kMaxTasks <= static_cast<size_t>(UINT16_MAX) + 1u,
                  "ARDUINOAWAIT_MAX_TASKS exceeds the representable TaskSlot range");

    // A slot whose generation reaches this value is retired (never reused) so an
    // incremented generation can never wrap back onto an earlier live handle (H2).
    static constexpr TaskGeneration kMaxGeneration = UINT32_MAX;

    // Generation-checked identity queries used by TaskHandle.
    bool handle_valid(TaskId id) const noexcept {
        if (id.slot >= kMaxTasks) {
            return false;
        }
        const Slot& s = slots_[id.slot];
        return s.generation == id.generation && s.state != State::free;
    }
    bool handle_done(TaskId id) const noexcept {
        if (id.slot >= kMaxTasks) {
            return false;
        }
        const Slot& s = slots_[id.slot];
        return s.generation == id.generation && s.state == State::completed;
    }

    enum class State : uint8_t { free, ready, running, waiting_timer, suspended, completed };

    struct Slot {
        std::coroutine_handle<> handle{};
        Slot* next = nullptr;           // intrusive ready-FIFO link
        detail::tick_t deadline_us = 0; // absolute wake deadline when waiting_timer
        TaskGeneration generation = 0;
        State state = State::free;
    };

    TaskSlot index_of(const Slot* s) const noexcept {
        return static_cast<TaskSlot>(s - slots_);
    }

    // Acquire a free slot, or reuse the oldest completed tombstone (which bumps
    // its generation, invalidating older handles). A slot at kMaxGeneration is
    // retired rather than reused, so generations never wrap (H2). Returns nullptr
    // when full or when every candidate slot's generation is exhausted.
    Slot* acquire_slot() noexcept {
        for (Slot& s : slots_) {
            if (s.state == State::free && s.generation != kMaxGeneration) {
                ++s.generation;
                return &s;
            }
        }
        for (Slot& s : slots_) {
            if (s.state == State::completed && s.generation != kMaxGeneration) {
                ++s.generation;
                return &s;
            }
        }
        return nullptr;
    }

    // On rejection (teardown, empty Task, or slot exhaustion) the passed Task is
    // left untouched: the caller retains frame ownership and it is released
    // normally when that Task is destroyed (a temporary at the end of the full
    // expression). Only success transfers the frame into a slot.
    TaskHandle schedule(Task<void>&& task) noexcept {
        TaskHandle result; // invalid unless scheduling succeeds
        if (shutting_down_) {
            // Teardown in progress: refuse silently; the caller retains the Task.
        } else if (!task) {
            ARDUINOAWAIT_ON_ERROR(Error::invalid_task);
        } else if (Slot* slot = acquire_slot(); slot == nullptr) {
            ARDUINOAWAIT_ON_ERROR(Error::task_limit);
        } else {
            slot->handle = detail::take_frame(task); // transfer frame ownership
            slot->state = State::ready;
            slot->next = nullptr;
            ready_push(slot);
            ++active_count_;
            result = TaskHandle{this, TaskId{index_of(slot), slot->generation}};
        }
        return result; // reachable via the shutdown/success paths; no code after a hook
    }

    void ready_push(Slot* slot) noexcept {
        slot->next = nullptr;
        if (ready_tail_ == nullptr) {
            ready_head_ = slot;
        } else {
            ready_tail_->next = slot;
        }
        ready_tail_ = slot;
        ++ready_count_;
    }

    Slot* ready_pop() noexcept {
        Slot* slot = ready_head_;
        if (slot != nullptr) {
            ready_head_ = slot->next;
            if (ready_head_ == nullptr) {
                ready_tail_ = nullptr;
            }
            slot->next = nullptr;
            --ready_count_;
        }
        return slot;
    }

    void run_pass() {
        // Snapshot the pass budget: only tasks ready NOW run this pass.
        const size_t budget = ready_count_;
        for (size_t i = 0; i < budget; ++i) {
            Slot* slot = ready_pop();
            if (slot == nullptr) {
                break;
            }
            if (slot->state != State::ready) {
                continue;
            }
            slot->state = State::running;
            current_ = slot;
            slot->handle.resume();
            current_ = nullptr;

            if (slot->handle.done()) {
                slot->handle.destroy();
                slot->handle = {};
                slot->state = State::completed;
                --active_count_;
            } else if (slot->state == State::running) {
                // Suspended without registering a wake source. No V1 awaitable does
                // this (yield -> ready, delay -> waiting_timer); park it so it does
                // not run again until something readies it.
                slot->state = State::suspended;
            }
            // Otherwise an awaitable already moved it to ready (yield/delay(0)) or
            // waiting_timer (positive delay); leave that state intact.
        }
    }

    // Re-queue the currently running task to the ready FIFO tail for a LATER pass
    // (yield() / delay(0)). The pass budget was already snapshotted, so it will
    // not run again this pass.
    void yield_current() noexcept {
        if (current_ != nullptr) {
            current_->state = State::ready;
            ready_push(current_);
        }
    }

    // Suspend the currently running task on a timer `duration_us` from the sampled
    // clock. Zero duration is a fair yield (delay(0) == yield()). Deadline overflow
    // routes to the deterministic error hook (Error::deadline_overflow); under a
    // non-halting hook the task is requeued so it is never lost.
    void arm_current_timer(detail::tick_t duration_us) noexcept {
        if (current_ == nullptr) {
            return;
        }
        if (duration_us == 0) {
            yield_current();
            return;
        }
        detail::tick_t deadline = 0;
        if (detail::compute_deadline(now_, duration_us, deadline)) {
            current_->deadline_us = deadline;
            current_->state = State::waiting_timer;
            if (deadline < nearest_deadline_) {
                nearest_deadline_ = deadline;
            }
        } else {
            yield_current(); // overflow: hook fired; do not lose the task
        }
    }

    // §9 step 5: move every timer whose deadline is due to the ready FIFO tail in
    // deterministic slot order. O(1) when nothing is due (cached nearest deadline).
    void process_due_timers() noexcept {
        if (now_ < nearest_deadline_) {
            return; // fast path: the soonest deadline is still in the future
        }
        for (Slot& s : slots_) {
            if (s.state == State::waiting_timer && s.deadline_us <= now_) {
                s.state = State::ready;
                ready_push(&s);
            }
        }
        recompute_nearest_deadline();
    }

    void recompute_nearest_deadline() noexcept {
        detail::tick_t nearest = UINT64_MAX;
        for (const Slot& s : slots_) {
            if (s.state == State::waiting_timer && s.deadline_us < nearest) {
                nearest = s.deadline_us;
            }
        }
        nearest_deadline_ = nearest;
    }

    Slot slots_[kMaxTasks]{};
    Slot* ready_head_ = nullptr;
    Slot* ready_tail_ = nullptr;
    size_t ready_count_ = 0;
    size_t active_count_ = 0;
    Slot* current_ = nullptr;
    bool in_poll_ = false;
    bool shutting_down_ = false;
    detail::tick_t now_ = 0;                       // §9 step-1 clock sample
    detail::tick_t nearest_deadline_ = UINT64_MAX; // cached soonest timer deadline
};

inline bool TaskHandle::valid() const noexcept {
    return sched_ != nullptr && sched_->handle_valid(id_);
}
inline bool TaskHandle::done() const noexcept {
    return sched_ != nullptr && sched_->handle_done(id_);
}

// Process-wide scheduler singleton and the common free-function API.
inline Scheduler& scheduler() noexcept {
    static Scheduler instance;
    return instance;
}

inline TaskHandle create_task(Task<void>&& task) {
    return scheduler().schedule(static_cast<Task<void>&&>(task));
}
inline void spawn(Task<void>&& task) {
    (void)scheduler().schedule(static_cast<Task<void>&&>(task));
}
inline TaskHandle current_task() noexcept { return scheduler().currentTask(); }
inline void poll() { scheduler().poll(); }

namespace detail {
inline void force_slot_generation(Scheduler& sched, TaskSlot slot, TaskGeneration generation) noexcept {
    sched.slots_[slot].generation = generation;
}
} // namespace detail

} // namespace arduinoawait

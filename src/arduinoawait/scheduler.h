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
// Scheduler surface (§7) therefore does not expose create_task()/spawn().
[[nodiscard]] TaskHandle create_task(Task<void>&& task) noexcept;
void spawn(Task<void>&& task) noexcept;
TaskHandle current_task() noexcept;
void poll();

class Scheduler {
public:
    Scheduler() noexcept = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    ~Scheduler() {
        // Release any still-owned frames (ready/running/suspended) at shutdown.
        for (Slot& s : slots_) {
            if (s.handle && s.state != State::completed) {
                s.handle.destroy();
                s.handle = {};
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

    // One bounded scheduler pass (V1 frozen semantics).
    void poll() {
        if (in_poll_) {
            ARDUINOAWAIT_ON_ERROR(Error::scheduler_reentry);
        } else {
            in_poll_ = true;
            run_pass();
            in_poll_ = false;
        }
    }

    bool hasReadyTasks() const noexcept { return ready_count_ > 0; }
    bool hasPendingTasks() const noexcept { return active_count_ > 0; }
    size_t activeTaskCount() const noexcept { return active_count_; }

private:
    friend class TaskHandle;
    friend TaskHandle create_task(Task<void>&& task) noexcept;
    friend void spawn(Task<void>&& task) noexcept;

    static constexpr size_t kMaxTasks = ARDUINOAWAIT_MAX_TASKS;

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

    enum class State : uint8_t { free, ready, running, suspended, completed };

    struct Slot {
        std::coroutine_handle<> handle{};
        Slot* next = nullptr; // intrusive ready-FIFO link
        TaskGeneration generation = 0;
        State state = State::free;
    };

    TaskSlot index_of(const Slot* s) const noexcept {
        return static_cast<TaskSlot>(s - slots_);
    }

    // Acquire a free slot, or reuse the oldest completed tombstone (which bumps
    // its generation, invalidating older handles). Returns nullptr when full.
    Slot* acquire_slot() noexcept {
        for (Slot& s : slots_) {
            if (s.state == State::free) {
                ++s.generation;
                return &s;
            }
        }
        for (Slot& s : slots_) {
            if (s.state == State::completed) {
                ++s.generation;
                return &s;
            }
        }
        return nullptr;
    }

    TaskHandle schedule(Task<void>&& task) noexcept {
        TaskHandle result; // invalid unless scheduling succeeds
        if (!task) {
            ARDUINOAWAIT_ON_ERROR(Error::invalid_task);
        } else if (Slot* slot = acquire_slot(); slot == nullptr) {
            ARDUINOAWAIT_ON_ERROR(Error::task_limit);
            // The passed Task's frame is released normally by its destructor.
        } else {
            slot->handle = detail::take_frame(task); // transfer frame ownership
            slot->state = State::ready;
            slot->next = nullptr;
            ready_push(slot);
            ++active_count_;
            result = TaskHandle{this, TaskId{index_of(slot), slot->generation}};
        }
        return result; // reachable via the success path; no unreachable code after a hook
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
            } else {
                // Suspended without re-queueing. No M4 public awaitable does this;
                // later milestones move such tasks between wait sets. Park it off
                // the ready queue so it does not run again this pass.
                slot->state = State::suspended;
            }
        }
    }

    Slot slots_[kMaxTasks]{};
    Slot* ready_head_ = nullptr;
    Slot* ready_tail_ = nullptr;
    size_t ready_count_ = 0;
    size_t active_count_ = 0;
    Slot* current_ = nullptr;
    bool in_poll_ = false;
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

inline TaskHandle create_task(Task<void>&& task) noexcept {
    return scheduler().schedule(static_cast<Task<void>&&>(task));
}
inline void spawn(Task<void>&& task) noexcept {
    (void)scheduler().schedule(static_cast<Task<void>&&>(task));
}
inline TaskHandle current_task() noexcept { return scheduler().currentTask(); }
inline void poll() { scheduler().poll(); }

} // namespace arduinoawait

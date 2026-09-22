#pragma once

// ArduinoAwait — diagnostics (V1_API_CONTRACT §14).
//
// Compiled ONLY when ARDUINOAWAIT_ENABLE_DIAGNOSTICS is set to 1. In a default
// build this header expands to nothing, so the diagnostics surface adds no code and
// no data. When enabled, `stats()` returns an allocation-free snapshot of the
// scheduler and coroutine-frame-pool counters. The underlying high-water marks
// (peak tasks, peak frame bytes) are always tracked at negligible, deterministic
// cost; only the accessor is gated.

#include "config.h"

#if ARDUINOAWAIT_ENABLE_DIAGNOSTICS

#include <cstddef>

#include "detail/global_frame_pool.h"
#include "scheduler.h"

namespace arduinoawait {

struct Stats {
    size_t activeTasks;         // tasks currently owned by the scheduler
    size_t peakTasks;           // high-water mark of activeTasks
    size_t readyTasks;          // tasks in the ready FIFO
    size_t waitingTimers;       // tasks suspended on a delay/timer
    size_t frameBytesUsed;      // coroutine-frame pool bytes in use
    size_t peakFrameBytesUsed;  // high-water mark of frameBytesUsed
    size_t frameBytesFree;      // coroutine-frame pool bytes available
    size_t allocationFailures;  // frame-pool allocation failures since start
};

namespace detail {
inline SchedulerCounters scheduler_counters() noexcept {
    Scheduler& sched = scheduler();
    SchedulerCounters counters{};
    counters.activeTasks = sched.active_count_;
    counters.peakTasks = sched.peak_count_;
    counters.readyTasks = sched.ready_count_;
    counters.waitingTimers = 0;
    for (const Scheduler::Slot& slot : sched.slots_) {
        if (slot.state == Scheduler::State::waiting_timer) {
            ++counters.waitingTimers;
        }
    }
    return counters;
}
} // namespace detail

[[nodiscard]] inline Stats stats() noexcept {
    const detail::SchedulerCounters counters = detail::scheduler_counters();
    const auto& pool = detail::frame_pool();
    Stats s{};
    s.activeTasks = counters.activeTasks;
    s.peakTasks = counters.peakTasks;
    s.readyTasks = counters.readyTasks;
    s.waitingTimers = counters.waitingTimers;
    s.frameBytesUsed = pool.bytesUsed();
    s.peakFrameBytesUsed = pool.peakBytesUsed();
    s.frameBytesFree = pool.bytesFree();
    s.allocationFailures = pool.allocationFailures();
    return s;
}

} // namespace arduinoawait

#endif // ARDUINOAWAIT_ENABLE_DIAGNOSTICS

#pragma once

// ArduinoAwait — deterministic error codes.
//
// Core failures are reported deterministically through the configured error
// hook (ARDUINOAWAIT_ON_ERROR); ArduinoAwait does not use exceptions for control
// flow. This enum is the frozen V1 error surface (see
// docs/arduinoawait/V1_API_CONTRACT.md §3). Values are appended, never
// reordered, to keep the underlying integer meanings stable.

#include <cstdint>

namespace arduinoawait {

enum class Error : uint8_t {
    none,
    task_limit,
    frame_pool_exhausted,
    scheduler_reentry,
    invalid_task,
    task_already_scheduled,
    task_awaited_twice,
    multiple_flag_waiters,
    object_destroyed_with_waiters,
    deadline_overflow,
    unhandled_exception,
    internal_error
};

} // namespace arduinoawait

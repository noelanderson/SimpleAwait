#pragma once

// ============================================================================
//  ArduinoAwait
//  A small, deterministic, fixed-memory cooperative coroutine library for
//  Arduino using native standard C++20 coroutines.
//
//  Primary targets: RP2040, RP2350 (Arm and RISC-V), ESP32 family (ESP32-S3).
//  Language floor:  C++20 (C++23 and later are supported but not required).
//
//  This is the single public include for applications:
//
//      #include <ArduinoAwait.h>
//
//  All public declarations live in namespace `arduinoawait`.
//
//  See docs/arduinoawait/V1_API_CONTRACT.md for the frozen public API and
//  docs/arduinoawait/ARCHITECTURE.md for the normative design.
//
//  Milestone status: M9 (Queue<T, Capacity>). Adds the bounded, scheduler-local
//  FIFO with blocking send/receive on top of M8: FIFO values and FIFO sender/
//  receiver waiters over fixed aligned ring storage (no heap, no default-
//  constructibility requirement), with trySend/tryReceive and automatic
//  back-pressure. Wakeups enqueue tasks (never inline-resume) per the §9 poll()
//  model, and there are no ISR methods in V1. waitUntil and final V1 integration
//  arrive in later milestones without changing this include path.
// ============================================================================

// Compile-time coroutine support verification. Must come first so an
// unsupported toolchain fails with a clear, early diagnostic.
#include "arduinoawait/detail/coroutine_support.h"

// Compile-time configuration (task capacity, frame pool size, error hook, ...).
#include "arduinoawait/config.h"

// Library version constants.
#include "arduinoawait/version.h"

// Deterministic error codes (frozen V1 surface).
#include "arduinoawait/error.h"

// Platform 64-bit monotonic microsecond clock and deadline arithmetic.
#include "arduinoawait/detail/platform_clock.h"
#include "arduinoawait/detail/time_math.h"

// Fixed coroutine frame allocator (no global heap fallback).
#include "arduinoawait/detail/frame_pool.h"

// Lazy, move-only Task<void> coroutine handle.
#include "arduinoawait/task.h"

// Cooperative scheduler: create_task/spawn/current_task/poll, TaskHandle.
#include "arduinoawait/scheduler.h"

// yield() and delay()/delay_ms()/delay_us() timer awaitables.
#include "arduinoawait/delay.h"

// Event: scheduler-local, manual-reset, multi-waiter synchronization.
#include "arduinoawait/event.h"

// ThreadSafeFlag: single-waiter external/IRQ-context signal bridge.
#include "arduinoawait/threadsafeflag.h"

// Queue<T, Capacity>: bounded, scheduler-local FIFO with blocking send/receive.
#include "arduinoawait/queue.h"

namespace arduinoawait {

// Public API entities are added here by subsequent milestones (Task, Scheduler,
// create_task/spawn, delay/yield, Event, ThreadSafeFlag, Queue, waitUntil).

} // namespace arduinoawait

// Per docs/arduinoawait/V1_API_CONTRACT.md §1, ArduinoAwait provides no required
// global namespace alias. Applications that prefer a shorter name may opt in in
// their own code, e.g.:
//
//     namespace aa = arduinoawait;

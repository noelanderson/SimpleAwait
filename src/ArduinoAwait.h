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
//  Milestone status: M11 (V1 hardening) — the V1 feature set is complete and
//  frozen. This single include exposes the whole V1 surface: Task<void>, the
//  scheduler (create_task/spawn/current_task/poll, TaskHandle), yield()/delay*(),
//  parent/child await, Event, ThreadSafeFlag, Queue<T,Capacity>, waitUntil(), and —
//  under ARDUINOAWAIT_ENABLE_DIAGNOSTICS — Stats/stats(). M11 adds stress suites,
//  optimization/sanitizer build-mode coverage, and on-device validation without any
//  new API or change to this include path.
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

// waitUntil(predicate): header-defined coroutine composition over yield().
#include "arduinoawait/waituntil.h"

// Diagnostics (opt-in via ARDUINOAWAIT_ENABLE_DIAGNOSTICS): Stats/stats() snapshot.
#include "arduinoawait/diagnostics.h"

namespace arduinoawait {

// The V1 public surface is declared by the headers included above: Task<void>,
// Scheduler with create_task/spawn/current_task/poll and TaskHandle, yield() and
// delay*(), parent/child await, Event, ThreadSafeFlag, Queue<T,Capacity>,
// waitUntil(), and (under ARDUINOAWAIT_ENABLE_DIAGNOSTICS) Stats/stats(). This
// aggregation header intentionally declares nothing of its own.

} // namespace arduinoawait

// Per docs/arduinoawait/V1_API_CONTRACT.md §1, ArduinoAwait provides no required
// global namespace alias. Applications that prefer a shorter name may opt in in
// their own code, e.g.:
//
//     namespace aa = arduinoawait;

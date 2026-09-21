#pragma once

// ArduinoAwait — platform short critical section for external-context signaling.
//
// ThreadSafeFlag::set() may run in an external/IRQ/callback/other-core context and
// touches a tiny amount of shared metadata (the flag's signaled bit and the
// scheduler's external-pending marker). That metadata is protected by a short,
// cross-context-safe critical section owned by the platform layer (ARCHITECTURE
// §16.2). Exactly one backend is selected at compile time:
//
//   ARDUINOAWAIT_CRITICAL_SECTION_* override  advanced/testing (user supplied)
//   ARDUINO_ARCH_RP2040   RP2040/RP2350: save_and_disable_interrupts() /
//                         restore_interrupts() — STATE-PRESERVING, so it is safe
//                         inside an ISR. This guarantees SAME-CORE IRQ exclusion
//                         only; cross-core (core1 -> core0) set() additionally
//                         needs a spinlock — supply the override to add one until a
//                         first-class multicore backend lands.
//   ARDUINO_ARCH_ESP32    ESP32 family: a portMUX spinlock (IRQ + multicore safe)
//   ARDUINO (generic)     UNSUPPORTED: there is no portable way to restore the
//                         prior interrupt state, and the scheduler's external-
//                         signal poll step needs a usable critical section, so the
//                         umbrella header fails to compile (#error) unless the
//                         override is provided.
//   host (none)           no-op: host tests are single-threaded; the state machine
//                         is exercised deterministically without real concurrency.
//
// Do NOT assume std::atomic is ISR-safe on every target (some RP2040 atomics use
// locks); the critical section, not a bare atomic, owns the cross-context ordering.

#include "../config.h"

#if defined(ARDUINOAWAIT_CRITICAL_SECTION_OVERRIDE)
// The application provides ARDUINOAWAIT_CRITICAL_SECTION_ENTER() and _EXIT().

namespace arduinoawait {
namespace detail {
class CriticalSection {
public:
    CriticalSection() noexcept { ARDUINOAWAIT_CRITICAL_SECTION_ENTER(); }
    ~CriticalSection() noexcept { ARDUINOAWAIT_CRITICAL_SECTION_EXIT(); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};
} // namespace detail
} // namespace arduinoawait

#elif defined(ARDUINO_ARCH_RP2040)

#include <hardware/sync.h>
namespace arduinoawait {
namespace detail {
class CriticalSection {
public:
    CriticalSection() noexcept : saved_(save_and_disable_interrupts()) {}
    ~CriticalSection() noexcept { restore_interrupts(saved_); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;

private:
    uint32_t saved_;
};
} // namespace detail
} // namespace arduinoawait

#elif defined(ARDUINO_ARCH_ESP32)

#include <Arduino.h> // pulls in the FreeRTOS portMUX spinlock API on ESP32
namespace arduinoawait {
namespace detail {
inline portMUX_TYPE& aa_flag_mux() noexcept {
    static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    return mux;
}
class CriticalSection {
public:
    CriticalSection() noexcept { portENTER_CRITICAL_SAFE(&aa_flag_mux()); }
    ~CriticalSection() noexcept { portEXIT_CRITICAL_SAFE(&aa_flag_mux()); }
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};
} // namespace detail
} // namespace arduinoawait

#elif defined(ARDUINO)

// Generic Arduino target with no first-class backend and no override. There is no
// PORTABLE way to save and restore the interrupt-enable state here: noInterrupts()/
// interrupts() cannot restore the prior state, which is unsafe inside an ISR. This
// is not merely a ThreadSafeFlag concern — the scheduler's external-signal poll
// step (detail::poll_external_signals, run by poll() every pass) also needs a
// usable critical section — so the umbrella <ArduinoAwait.h> cannot be compiled
// safely on such a target. Fail loudly and deterministically on include with a
// directive rather than emitting unsafe or silently-incorrect code.
#error "ArduinoAwait: generic Arduino targets require ARDUINOAWAIT_CRITICAL_SECTION_OVERRIDE (there is no portable ISR-safe critical section); use a first-class target (RP2040/RP2350/ESP32) or supply the override."

#else

namespace arduinoawait {
namespace detail {
// Host: deterministic single-threaded tests need no real protection. The platform
// layer owns cross-context correctness on target; this no-op keeps the state
// machine testable without introducing nondeterministic concurrency.
class CriticalSection {
public:
    CriticalSection() noexcept = default;
    ~CriticalSection() noexcept = default;
    CriticalSection(const CriticalSection&) = delete;
    CriticalSection& operator=(const CriticalSection&) = delete;
};
} // namespace detail
} // namespace arduinoawait

#endif

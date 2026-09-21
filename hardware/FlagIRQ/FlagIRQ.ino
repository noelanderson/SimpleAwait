// M8 hardware validation — self-driving ThreadSafeFlag IRQ-storm stress with a
// deterministic PASS/FAIL verdict.
//
// A repeating hardware-timer interrupt (RP2040/RP2350 repeating timer; ESP32
// hw_timer) calls ThreadSafeFlag::set() from ISR context at a fixed rate — an "IRQ
// storm" that need not be triggered by a human. A coroutine co_awaits the flag and
// on each scheduler-context wake records the IRQ->coroutine latency and counts the
// wake. A heartbeat task proves the waiting coroutine never stalls the loop. After
// a fixed number of ISR signals the sketch stops the storm, quiesces (drains the
// coalesced backlog), and prints a PASS/FAIL verdict over Serial, evaluating four
// executable invariants:
//
//   * liveness   — the coroutine woke at least once and the heartbeat advanced;
//   * coalescing — storm wakes never exceeded storm signals (auto-reset/coalescing
//                  holds); evaluated on storm totals only;
//   * no ISR body — the coroutine never observed itself running in ISR context (a
//                   per-core hardware/RTOS check: __get_current_exception() on RP,
//                   xPortInIsrContext() on ESP32 — not a "was I inside poll()"
//                   proxy, so it catches an inline resume even when an ISR preempts
//                   a scheduler pass, and is immune to another core's ISR); and
//   * watchdog   — one INDEPENDENT drain signal (excluded from the coalescing
//                  denominator) produces exactly one wake within a deadline (no
//                  lost signal and no deadlock under stress).
//
// The shared 64-bit ISR timestamp is read and written under the platform
// CriticalSection so it cannot tear across the ISR/task boundary on 32-bit cores.
//
// This exercises the whole external-signal path (platform CriticalSection, set()
// from a real ISR, the scheduler external-pending resolution) and produces an
// on-device pass/fail result. Host CI COMPILES this on every first-class target;
// the on-device RUN is a pre-V1-release gate (AGENTS.md §16) and its result is the
// "RESULT=PASS" line below. ThreadSafeFlag::set() is ISR-safe only on the
// first-class targets, so this sketch is unsupported elsewhere by construction.

#include <ArduinoAwait.h>

using arduinoawait::delay_ms;
using arduinoawait::poll;
using arduinoawait::spawn;
using arduinoawait::Task;
using arduinoawait::ThreadSafeFlag;
using arduinoawait::detail::CriticalSection;
using arduinoawait::detail::platform_now_us;

static ThreadSafeFlag g_irq;

// ---- ISR-owned state ------------------------------------------------------
// g_isr_time_us is 64-bit; on a 32-bit core a task read racing the ISR write could
// tear, so every access is wrapped in a short CriticalSection (which masks the
// timer IRQ for its duration). g_isr_sets is a 32-bit counter with a single writer
// (the non-reentrant timer ISR), so its plain read on the task side is atomic.
static unsigned long long g_isr_time_us = 0;  // guarded by CriticalSection
static volatile unsigned long g_isr_sets = 0; // ISR writes, task reads

// ---- task-owned state -----------------------------------------------------
static unsigned long g_wakes = 0;
static unsigned long long g_last_latency_us = 0;
static unsigned long g_heartbeats = 0;

// Coroutine bodies must only ever execute in normal (thread) context, never inside
// an interrupt. g_ran_in_isr latches true if the waiter ever observes itself
// running in ISR context — the exact forbidden behavior (an external signal path
// resuming coroutine code from an ISR). The check queries a per-core hardware/RTOS
// ISR-context indicator, so a timer ISR that merely PREEMPTS a scheduler pass (the
// coroutine is suspended, not running) does not trip it, and an ISR active on
// another core does not create a false positive.
static bool g_ran_in_isr = false;

constexpr unsigned long kStormSignals = 2000;          // storm length
constexpr unsigned long long kWatchdogUs = 100000ull;  // 100 ms drain deadline
constexpr unsigned long kQuiesceStable = 3;            // consecutive idle polls
constexpr unsigned long kQuiesceMaxPolls = 1000;       // safety cap

static inline void aa_isr_fire() {
    {
        CriticalSection cs; // race-free 64-bit timestamp store
        g_isr_time_us = platform_now_us();
    }
    ++g_isr_sets;
    g_irq.set(); // ISR-safe: marks signaled + pending, never resumes coroutine code
}

// ---- platform storm driver + ISR-context probe (first-class targets only) -
#if defined(ARDUINO_ARCH_RP2040)
#include <pico/platform.h> // __get_current_exception()
#include <pico/time.h>
static inline bool aa_in_isr_context() {
    // Nonzero exception number == this core is in an exception/IRQ handler
    // (Arm IPSR / RISC-V equivalent). Zero == thread mode.
    return __get_current_exception() != 0u;
}
static repeating_timer_t g_timer;
static bool aa_on_timer(repeating_timer_t*) {
    aa_isr_fire();
    return true; // keep repeating
}
static void aa_start_storm() {
    add_repeating_timer_us(-250, aa_on_timer, nullptr, &g_timer); // 250 us period
}
static void aa_stop_storm() { cancel_repeating_timer(&g_timer); }

#elif defined(ARDUINO_ARCH_ESP32)
static inline bool aa_in_isr_context() {
    return xPortInIsrContext() != 0; // per-core FreeRTOS ISR-context check
}
static hw_timer_t* g_timer = nullptr;
static void ARDUINO_ISR_ATTR aa_on_timer() { aa_isr_fire(); }
static void aa_start_storm() {
    g_timer = timerBegin(1000000);           // 1 MHz tick
    timerAttachInterrupt(g_timer, &aa_on_timer);
    timerAlarm(g_timer, 250, true, 0);       // every 250 ticks = 250 us, autoreload
}
static void aa_stop_storm() {
    if (g_timer != nullptr) {
        timerEnd(g_timer);
        g_timer = nullptr;
    }
}

#else
#error "FlagIRQ requires a first-class target (RP2040/RP2350 or ESP32): ThreadSafeFlag::set() is ISR-safe only there."
#endif

static Task<void> irqWaiter() {
    while (true) {
        co_await g_irq.wait();
        if (aa_in_isr_context()) {
            g_ran_in_isr = true; // forbidden: coroutine body running in ISR context
        }
        unsigned long long isrTime;
        {
            CriticalSection cs; // race-free 64-bit read
            isrTime = g_isr_time_us;
        }
        g_last_latency_us = platform_now_us() - isrTime;
        ++g_wakes;
    }
}

static Task<void> heartbeat() {
    while (true) {
        ++g_heartbeats;
        co_await delay_ms(1);
    }
}

static bool g_reported = false;

void setup() {
    Serial.begin(115200);
    spawn(irqWaiter());
    spawn(heartbeat());
    aa_start_storm();
}

void loop() {
    poll();

    if (g_reported || g_isr_sets < kStormSignals) {
        return; // storm still running, or verdict already printed
    }

    // Storm target reached: stop generating signals, then QUIESCE — poll until the
    // waiter's wake count stops advancing — so every coalesced storm signal is
    // consumed before the independent watchdog signal. This keeps the coalescing
    // invariant (evaluated on storm totals) separate from the watchdog drain (N3).
    aa_stop_storm();
    unsigned long stable = 0;
    unsigned long polls = 0;
    unsigned long lastWakes = g_wakes;
    while (stable < kQuiesceStable && polls < kQuiesceMaxPolls) {
        poll();
        ++polls;
        if (g_wakes == lastWakes) {
            ++stable;
        } else {
            stable = 0;
            lastWakes = g_wakes;
        }
    }

    const unsigned long signalsInStorm = g_isr_sets;  // every signal the ISR raised
    const unsigned long wakesInStorm = g_wakes;       // every wake they produced
    const unsigned long heartbeatsAtStop = g_heartbeats;

    // Watchdog: one independent signal must produce exactly one wake within the
    // deadline (no lost signal, no deadlock). This signal is NOT in the storm
    // coalescing denominator.
    const unsigned long wakesBeforeDrain = g_wakes;
    g_irq.set();
    const unsigned long long deadline = platform_now_us() + kWatchdogUs;
    bool drained = false;
    while (platform_now_us() < deadline) {
        poll();
        if (g_wakes > wakesBeforeDrain) {
            drained = true;
            break;
        }
    }

    const bool liveness = (wakesInStorm >= 1) && (heartbeatsAtStop >= 1);
    const bool coalescing = (wakesInStorm <= signalsInStorm); // storm signals only
    const bool noIsrBody = !g_ran_in_isr;
    const bool drainedOne = drained && (g_wakes == wakesBeforeDrain + 1);
    const bool pass = liveness && coalescing && noIsrBody && drainedOne;

    Serial.println();
    Serial.println("== M8 FlagIRQ stress verdict ==");
    Serial.print("storm_signals=");
    Serial.println(signalsInStorm);
    Serial.print("storm_wakes=");
    Serial.println(wakesInStorm);
    Serial.print("heartbeats=");
    Serial.println(heartbeatsAtStop);
    Serial.print("last_latency_us=");
    Serial.println(static_cast<unsigned long>(g_last_latency_us));
    Serial.print("liveness=");
    Serial.println(liveness ? "ok" : "FAIL");
    Serial.print("coalescing(storm_wakes<=storm_signals)=");
    Serial.println(coalescing ? "ok" : "FAIL");
    Serial.print("no_coroutine_body_in_isr=");
    Serial.println(noIsrBody ? "ok" : "FAIL");
    Serial.print("watchdog_drain(exactly_one_wake)=");
    Serial.println(drainedOne ? "ok" : "FAIL");
    Serial.print("RESULT=");
    Serial.println(pass ? "PASS" : "FAIL");

    g_reported = true;
}

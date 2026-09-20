// M1 deadline-arithmetic test.
//
// Verifies millisecond->microsecond widening, the overflow predicate, and the
// deadline-overflow policy (Error::deadline_overflow routed through the error
// hook). The hook is overridden to record rather than halt so the overflow path
// is observable without terminating the test.

#include <cstdint>

namespace {
int g_last_error = -1;
}
#define ARDUINOAWAIT_ON_ERROR(error) (g_last_error = static_cast<int>(error))

#include <ArduinoAwait.h>

#include "aa_test.h"

using arduinoawait::Error;
using arduinoawait::detail::add_overflows;
using arduinoawait::detail::compute_deadline;
using arduinoawait::detail::ms_to_us;
using arduinoawait::detail::tick_t;

// Widening cannot overflow: even UINT32_MAX ms fits in uint64 us.
static_assert(ms_to_us(0) == 0, "0 ms");
static_assert(ms_to_us(1) == 1000ULL, "1 ms");
static_assert(ms_to_us(500) == 500000ULL, "500 ms");
static_assert(ms_to_us(UINT32_MAX) == static_cast<tick_t>(UINT32_MAX) * 1000ULL,
              "max ms widened before multiply");

static_assert(!add_overflows(0, 0), "0+0");
static_assert(!add_overflows(100, 200), "small");
static_assert(!add_overflows(UINT64_MAX, 0), "max+0");
static_assert(!add_overflows(UINT64_MAX - 10, 10), "exactly max");
static_assert(add_overflows(UINT64_MAX, 1), "max+1");
static_assert(add_overflows(UINT64_MAX - 10, 11), "just over");

int main() {
    tick_t out = 0;

    // Success writes the deadline, returns true, and does not touch the hook.
    g_last_error = -1;
    out = 0;
    AA_CHECK(compute_deadline(1000, 500, out));
    AA_CHECK(out == 1500);
    AA_CHECK(g_last_error == -1);

    // Exactly the maximum representable deadline is valid (not an overflow).
    out = 0;
    AA_CHECK(compute_deadline(UINT64_MAX - 10, 10, out));
    AA_CHECK(out == UINT64_MAX);
    AA_CHECK(g_last_error == -1);

    // ms widening feeding a deadline.
    out = 0;
    AA_CHECK(compute_deadline(0, ms_to_us(500), out));
    AA_CHECK(out == 500000ULL);

    // Overflow returns false, leaves out untouched, and routes to the hook. The
    // bool is the unambiguous failure signal (a valid max deadline is not
    // conflated with failure).
    g_last_error = -1;
    out = 0xABCDEFu;
    AA_CHECK(!compute_deadline(UINT64_MAX - 10, 11, out));
    AA_CHECK(out == 0xABCDEFu); // unchanged on failure
    AA_CHECK(g_last_error == static_cast<int>(Error::deadline_overflow));

    g_last_error = -1;
    out = 0xABCDEFu;
    AA_CHECK(!compute_deadline(UINT64_MAX, 1, out));
    AA_CHECK(out == 0xABCDEFu);
    AA_CHECK(g_last_error == static_cast<int>(Error::deadline_overflow));

    // A non-overflowing maximum duration succeeds without the hook.
    g_last_error = -1;
    out = 0;
    AA_CHECK(compute_deadline(0, UINT64_MAX, out));
    AA_CHECK(out == UINT64_MAX);
    AA_CHECK(g_last_error == -1);

    AA_RUN_TESTS();
}

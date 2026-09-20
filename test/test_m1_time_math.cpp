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
    // Normal deadline computation does not touch the error hook.
    g_last_error = -1;
    AA_CHECK(compute_deadline(1000, 500) == 1500);
    AA_CHECK(compute_deadline(UINT64_MAX - 10, 10) == UINT64_MAX);
    AA_CHECK(g_last_error == -1);

    // ms widening feeding a deadline.
    AA_CHECK(compute_deadline(0, ms_to_us(500)) == 500000ULL);

    // Overflow routes to the error hook with deadline_overflow.
    g_last_error = -1;
    (void)compute_deadline(UINT64_MAX - 10, 11);
    AA_CHECK(g_last_error == static_cast<int>(Error::deadline_overflow));

    g_last_error = -1;
    (void)compute_deadline(UINT64_MAX, 1);
    AA_CHECK(g_last_error == static_cast<int>(Error::deadline_overflow));

    // A non-overflowing large duration does not trigger the hook.
    g_last_error = -1;
    (void)compute_deadline(0, UINT64_MAX);
    AA_CHECK(g_last_error == -1);

    AA_RUN_TESTS();
}

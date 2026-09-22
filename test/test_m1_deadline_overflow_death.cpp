// M1 deadline overflow death test.
//
// Under the default [[noreturn]] error hook, a deadline that would overflow the
// 64-bit microsecond timebase must halt the program (Error::deadline_overflow);
// V1 forbids silent wrap/saturation. This test does NOT override the hook, prints
// the death-test marker, then triggers an overflow. It is driven by
// run_death_test.cmake, which requires the marker and abnormal termination.

#include <SimpleAwait.h>

#include <cstdio>

#if defined(_MSC_VER)
#  include <cstdlib>
#endif

using simpleawait::detail::compute_deadline;
using simpleawait::detail::tick_t;

int main() {
#if defined(_MSC_VER)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    std::fputs("SA_DEATH_TEST_REACHED\n", stderr);
    std::fflush(stderr);

    tick_t out = 0;
    // Overflows -> default hook halts. Control must not return past this call.
    (void)compute_deadline(UINT64_MAX, 1, out);
}

// Negative-compile regression: with diagnostics at the default (disabled), the
// Stats/stats() surface — and, under the same SIMPLEAWAIT_ENABLE_DIAGNOSTICS guard,
// the scheduler's peak-task counter member and its update path — must be entirely
// absent, so the opt-in feature costs nothing in a default build (M10 H1). This
// probe references simpleawait::stats() WITHOUT enabling diagnostics; it must fail
// to compile because the name does not exist. Built as an EXCLUDE_FROM_ALL target
// and asserted to fail by run_negcompile_test.cmake.

#include <SimpleAwait.h>

int main() {
    auto s = simpleawait::stats(); // undeclared when diagnostics is disabled
    (void)s;
    return 0;
}

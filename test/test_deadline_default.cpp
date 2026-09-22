// Deadline default-hook regression test.
//
// compute_deadline() must compile cleanly under the DEFAULT (unoverridden)
// [[noreturn]] error hook and the repository's strict warnings — a prior version
// placed an unreachable statement after the hook, which MSVC /W4 /WX rejected
// (C4702) for any default-config consumer. This translation unit does NOT
// override SIMPLEAWAIT_ON_ERROR, so it exercises the default policy. It calls
// only the non-overflow (success) path; the halting overflow path with the
// default hook is covered by the companion death test.

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::detail::compute_deadline;
using simpleawait::detail::tick_t;

int main() {
    tick_t out = 0;

    SA_CHECK(compute_deadline(1000, 500, out));
    SA_CHECK(out == 1500);

    out = 0;
    SA_CHECK(compute_deadline(0, 1000000, out));
    SA_CHECK(out == 1000000);

    // Exactly the maximum deadline is valid under the default policy too.
    out = 0;
    SA_CHECK(compute_deadline(UINT64_MAX - 1, 1, out));
    SA_CHECK(out == UINT64_MAX);

    SA_RUN_TESTS();
}

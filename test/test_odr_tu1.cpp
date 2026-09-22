// ODR / multi-translation-unit test (main).
//
// A header-only library must not accidentally give each translation unit its own
// copy of a supposedly-shared entity. This test links two TUs that both include
// <SimpleAwait.h> and checks that:
//   * the inline version constant `simpleawait::version_major` is one object
//     (same address in both TUs) — regressing `inline` to internal linkage would
//     break this;
//   * the inline `default_error_handler<int>` instantiation is one function
//     (same address in both TUs);
//   * configuration macros are consistent across TUs.

#include "test_odr.h"

#include "sa_test.h"

int main() {
    SA_CHECK(&simpleawait::version_major == sa_tu2_version_major_addr());
    SA_CHECK(&simpleawait::detail::default_error_handler<int> == sa_tu2_error_handler_addr());
    SA_CHECK(SIMPLEAWAIT_MAX_TASKS == sa_tu2_max_tasks());

    SA_RUN_TESTS();
}

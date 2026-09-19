// ODR / multi-translation-unit test (main).
//
// A header-only library must not accidentally give each translation unit its own
// copy of a supposedly-shared entity. This test links two TUs that both include
// <ArduinoAwait.h> and checks that:
//   * the inline version constant `arduinoawait::version_major` is one object
//     (same address in both TUs) — regressing `inline` to internal linkage would
//     break this;
//   * the inline `default_error_handler<int>` instantiation is one function
//     (same address in both TUs);
//   * configuration macros are consistent across TUs.

#include "test_m0_odr.h"

#include "aa_test.h"

int main() {
    AA_CHECK(&arduinoawait::version_major == aa_tu2_version_major_addr());
    AA_CHECK(&arduinoawait::detail::default_error_handler<int> == aa_tu2_error_handler_addr());
    AA_CHECK(ARDUINOAWAIT_MAX_TASKS == aa_tu2_max_tasks());

    AA_RUN_TESTS();
}

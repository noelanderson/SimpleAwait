// ODR test, translation unit 2. See test_m0_odr_tu1.cpp for the assertions.
//
// Exposes the addresses/values that must be identical to those observed in the
// other translation unit, proving the header's inline variable and inline
// function template each have exactly one definition program-wide.

#include "test_m0_odr.h"

const int* aa_tu2_version_major_addr() {
    return &arduinoawait::version_major;
}

aa_handler_ptr aa_tu2_error_handler_addr() {
    return &arduinoawait::detail::default_error_handler<int>;
}

int aa_tu2_max_tasks() {
    return ARDUINOAWAIT_MAX_TASKS;
}

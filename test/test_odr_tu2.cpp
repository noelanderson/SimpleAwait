// ODR test, translation unit 2. See test_odr_tu1.cpp for the assertions.
//
// Exposes the addresses/values that must be identical to those observed in the
// other translation unit, proving the header's inline variable and inline
// function template each have exactly one definition program-wide.

#include "test_odr.h"

const int* sa_tu2_version_major_addr() {
    return &simpleawait::version_major;
}

sa_handler_ptr sa_tu2_error_handler_addr() {
    return &simpleawait::detail::default_error_handler<int>;
}

int sa_tu2_max_tasks() {
    return SIMPLEAWAIT_MAX_TASKS;
}

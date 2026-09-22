// Negative-compilation probe for the scheduler task-capacity limit.
//
// SIMPLEAWAIT_MAX_TASKS greater than the representable TaskSlot range would let
// index_of() narrow a slot index and alias a handle's identity. A static_assert
// in Scheduler must reject it at compile time rather than silently narrow. Built
// as an EXCLUDE_FROM_ALL target and asserted to fail (with the capacity
// diagnostic) by run_negcompile_test.cmake.
//
// TaskSlot is uint16_t, so indices 0..65535 are representable and 65537 slots is
// one past the limit.

#define SIMPLEAWAIT_MAX_TASKS 65537

#include <SimpleAwait.h>

static_assert(sizeof(simpleawait::Scheduler) > 0, "force instantiation of the capacity check");

int main() {
    return 0;
}

// Negative-compilation probe for the FramePool capacity limit (M2 H1).
//
// A pool capacity that does not fit the 32-bit block header fields must be
// rejected by a static_assert rather than silently truncated. On the 64-bit host
// this instantiates a >4 GiB capacity, which must fail to compile. Built as an
// EXCLUDE_FROM_ALL target and asserted to fail (with the capacity diagnostic) by
// run_negcompile_test.cmake.

#include <ArduinoAwait.h>

using TooBig = arduinoawait::detail::FramePool<static_cast<std::size_t>(0xFFFFFFFFULL) + 1>;

static_assert(sizeof(TooBig) > 0, "force instantiation of the capacity check");

int main() {
    return 0;
}

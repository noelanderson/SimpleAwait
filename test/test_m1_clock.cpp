// M1 platform clock test.
//
// Injects a deterministic 64-bit microsecond clock via SIMPLEAWAIT_CLOCK_NOW_US
// and verifies platform_now_us() returns exactly the injected value and observes
// monotonic advances. Also exercises the generic 32-bit micros() extender's
// wrap handling directly (the extender backs the secondary generic-Arduino
// clock; here it is tested in isolation).

#include <cstdint>

// External linkage so the header's inline platform_now_us() may reference it.
std::uint64_t g_sa_fake_now = 0;
#define SIMPLEAWAIT_CLOCK_NOW_US() (g_sa_fake_now)

#include <SimpleAwait.h>

#include "sa_test.h"

using simpleawait::detail::Micros32Extender;
using simpleawait::detail::platform_now_us;
using simpleawait::detail::tick_t;

static_assert(sizeof(tick_t) == 8, "tick_t is a 64-bit microsecond type");

// Compile-time extender wrap check: after 0xFFFFFFF0 then 0x00000010 the 64-bit
// value must cross into the second 2^32 window.
namespace {
constexpr std::uint64_t extender_after_wrap() {
    Micros32Extender e;
    e.extend(0xFFFFFFF0u);
    return e.extend(0x00000010u);
}
} // namespace
static_assert(extender_after_wrap() == ((std::uint64_t{1} << 32) | 0x10u),
              "extender must count a wrap into the high 32 bits");

int main() {
    // Exact injected values.
    g_sa_fake_now = 0;
    SA_CHECK(platform_now_us() == 0);

    g_sa_fake_now = 1234567;
    SA_CHECK(platform_now_us() == 1234567);

    g_sa_fake_now = 0xFFFFFFFFFFULL; // well beyond 32 bits
    SA_CHECK(platform_now_us() == 0xFFFFFFFFFFULL);

    // Monotonic advance is observed.
    g_sa_fake_now = 1000;
    const tick_t t0 = platform_now_us();
    g_sa_fake_now = 1000 + 500;
    const tick_t t1 = platform_now_us();
    SA_CHECK(t1 > t0);
    SA_CHECK(t1 - t0 == 500);

    // Extender at runtime: no wrap, then a wrap, then continue.
    Micros32Extender ext;
    SA_CHECK(ext.extend(0) == 0);
    SA_CHECK(ext.extend(1000) == 1000);
    SA_CHECK(ext.extend(0xFFFFFF00u) == 0xFFFFFF00u);
    // Wrap: 0x00000005 < previous -> high word increments.
    SA_CHECK(ext.extend(0x00000005u) == ((std::uint64_t{1} << 32) | 0x5u));
    // Still ascending within the new window.
    SA_CHECK(ext.extend(0x00000006u) == ((std::uint64_t{1} << 32) | 0x6u));

    SA_RUN_TESTS();
}

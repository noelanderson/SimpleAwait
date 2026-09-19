#pragma once

// Minimal deterministic test harness for ArduinoAwait host tests.
//
// Intentionally tiny and dependency-free: each test is a standalone executable
// whose main() returns 0 on success and 1 on failure. AA_CHECK records failures
// (and keeps going so a run reports every problem), AA_RUN_TESTS returns the
// aggregate result.

#include <cstdio>

namespace aatest {

inline int g_failures = 0;

inline void record_failure(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "CHECK FAILED: %s  (%s:%d)\n", expr, file, line);
    ++g_failures;
}

inline int result() noexcept {
    if (g_failures == 0) {
        std::printf("OK\n");
        return 0;
    }
    std::fprintf(stderr, "%d check(s) failed\n", g_failures);
    return 1;
}

} // namespace aatest

#define AA_CHECK(cond)                                                         \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::aatest::record_failure(#cond, __FILE__, __LINE__);               \
        }                                                                      \
    } while (0)

#define AA_RUN_TESTS() return ::aatest::result()

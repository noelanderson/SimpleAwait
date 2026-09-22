// Negative-compilation probe.
//
// This translation unit is EXPECTED to fail to compile: it includes the public
// header while the compiler is forced to a pre-C++20 standard (see the target's
// CXX_STANDARD in test/CMakeLists.txt), where no coroutine support is
// advertised. The compile-time guard in coroutine_support.h must reject it with
// a clear #error. test/run_negcompile_test.cmake builds this EXCLUDE_FROM_ALL
// target and requires BOTH that the build fails AND that the failure carries the
// coroutine-support diagnostic (so an unrelated/infrastructure build failure is
// not mistaken for a passing test); if the guard ever regresses and this
// compiles, the test fails, flagging the regression.

#include <SimpleAwait.h>

int main() {
    return 0;
}

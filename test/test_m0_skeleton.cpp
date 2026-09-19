// M0 skeleton test.
//
// Validates the M0 build-skeleton contract:
//   * the public header includes cleanly and pulls in configuration + version;
//   * default configuration macros are present and sane;
//   * native C++20 coroutine support is actually available end-to-end through
//     the library include path (a trivial local coroutine is created lazily,
//     resumed once, completes, and is destroyed exactly once).
//
// No ArduinoAwait scheduling exists yet at M0; this test deliberately uses only
// the standard coroutine primitives to prove the toolchain + header cooperate.

#include <ArduinoAwait.h>

#include <coroutine>

#include "aa_test.h"

// ---- Compile-time contract -------------------------------------------------

static_assert(__cpp_impl_coroutine, "coroutine language support must be present");

static_assert(ARDUINOAWAIT_MAX_TASKS >= 1, "default task capacity must be >= 1");
static_assert(ARDUINOAWAIT_FRAME_POOL_BYTES >= 1, "default frame pool must be positive");
static_assert(ARDUINOAWAIT_ENABLE_DIAGNOSTICS == 0, "diagnostics default off");
static_assert(ARDUINOAWAIT_ENABLE_ISR == 0, "ISR bridge default off");

static_assert(ARDUINOAWAIT_VERSION_MAJOR == 0, "version major");
static_assert(ARDUINOAWAIT_VERSION_MINOR == 1, "version minor");
static_assert(ARDUINOAWAIT_VERSION_PATCH == 0, "version patch");
static_assert(arduinoawait::version_major == ARDUINOAWAIT_VERSION_MAJOR, "version constant");
static_assert(arduinoawait::version_minor == ARDUINOAWAIT_VERSION_MINOR, "version constant");
static_assert(arduinoawait::version_patch == ARDUINOAWAIT_VERSION_PATCH, "version constant");

// ---- End-to-end coroutine smoke ------------------------------------------

namespace {

struct SmokeCoro {
    struct promise_type {
        SmokeCoro get_return_object() {
            return SmokeCoro{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };

    explicit SmokeCoro(std::coroutine_handle<promise_type> h) noexcept : handle(h) {}
    SmokeCoro(const SmokeCoro&) = delete;
    SmokeCoro& operator=(const SmokeCoro&) = delete;
    SmokeCoro(SmokeCoro&& other) noexcept : handle(other.handle) { other.handle = {}; }
    SmokeCoro& operator=(SmokeCoro&&) = delete;
    ~SmokeCoro() {
        if (handle) {
            handle.destroy();
        }
    }

    std::coroutine_handle<promise_type> handle;
};

bool g_body_ran = false;

SmokeCoro smoke() {
    g_body_ran = true;
    co_return;
}

} // namespace

int main() {
    // Lazy creation: initial_suspend is suspend_always, so the body has not run
    // just from calling the coroutine function.
    SmokeCoro c = smoke();
    AA_CHECK(static_cast<bool>(c.handle));
    AA_CHECK(!c.handle.done());
    AA_CHECK(!g_body_ran);

    // One resume runs the body to co_return and lands on final_suspend.
    c.handle.resume();
    AA_CHECK(g_body_ran);
    AA_CHECK(c.handle.done());

    AA_RUN_TESTS();
}

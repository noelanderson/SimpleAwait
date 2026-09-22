# ArduinoAwait host tests.
#
# Each test is compiled at the C++20 language floor and, where the toolchain
# advertises it, also at C++23 as a forward-compatibility check. Tests are
# deterministic and never sleep on wall-clock time. Optimization-level coverage
# (-O0/-O2/-Os) and sanitizers are driven from CI via CMAKE_CXX_FLAGS and
# ARDUINOAWAIT_ENABLE_SANITIZERS respectively.

option(ARDUINOAWAIT_ENABLE_SANITIZERS
    "Build ArduinoAwait host tests with ASan/UBSan (GCC/Clang)" OFF)

find_package(Python3 COMPONENTS Interpreter)

# Language standards to build every test under.
set(_aa_standards 20)
if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    list(APPEND _aa_standards 23)
endif()

# Common per-target setup: link the header-only library, pin the C++ standard,
# apply strict warnings-as-errors, and optionally sanitizers.
function(_aa_setup_test_target target std)
    target_link_libraries("${target}" PRIVATE arduinoawait::arduinoawait)
    set_target_properties("${target}" PROPERTIES
        CXX_STANDARD ${std}
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options("${target}" PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    if(ARDUINOAWAIT_ENABLE_SANITIZERS AND NOT MSVC)
        target_compile_options("${target}" PRIVATE
            -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options("${target}" PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# arduinoawait_add_host_test(<name> <source>...)
#   One ctest entry per available language standard.
function(arduinoawait_add_host_test test_name)
    foreach(_std IN LISTS _aa_standards)
        set(_target "${test_name}_cpp${_std}")
        add_executable("${_target}" ${ARGN})
        _aa_setup_test_target("${_target}" ${_std})
        add_test(NAME "${_target}" COMMAND "${_target}")
    endforeach()
endfunction()

# arduinoawait_add_death_test(<name> <source>)
#   Builds a program expected to terminate abnormally, driven through a CMake
#   wrapper that recognizes an abort/non-zero exit AND requires the program to
#   have reached the error hook. This is portable across POSIX (SIGABRT) and
#   Windows (exit 3), unlike CTest WILL_FAIL which does not reliably invert
#   signal-based termination.
function(arduinoawait_add_death_test test_name source)
    foreach(_std IN LISTS _aa_standards)
        set(_target "${test_name}_cpp${_std}")
        add_executable("${_target}" "${source}")
        _aa_setup_test_target("${_target}" ${_std})
        add_test(NAME "${_target}"
            COMMAND "${CMAKE_COMMAND}"
                "-DAA_EXE=$<TARGET_FILE:${_target}>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/run_death_test.cmake")
    endforeach()
endfunction()

# Positive tests.
arduinoawait_add_host_test(test_m0_skeleton test_m0_skeleton.cpp)
arduinoawait_add_host_test(test_m0_config_override test_m0_config_override.cpp)
arduinoawait_add_host_test(test_m0_odr test_m0_odr_tu1.cpp test_m0_odr_tu2.cpp)
arduinoawait_add_host_test(test_m1_clock test_m1_clock.cpp)
arduinoawait_add_host_test(test_m1_time_math test_m1_time_math.cpp)
arduinoawait_add_host_test(test_m1_deadline_default test_m1_deadline_default.cpp)
arduinoawait_add_host_test(test_m2_frame_pool test_m2_frame_pool.cpp)
arduinoawait_add_host_test(test_m3_task test_m3_task.cpp)
arduinoawait_add_host_test(test_m3_task_exhaustion test_m3_task_exhaustion.cpp)
arduinoawait_add_host_test(test_m3_ownership_edge test_m3_ownership_edge.cpp)
arduinoawait_add_host_test(test_m4_scheduler test_m4_scheduler.cpp)
arduinoawait_add_host_test(test_m4_errors test_m4_errors.cpp)
arduinoawait_add_host_test(test_m4_poll_clock test_m4_poll_clock.cpp)
arduinoawait_add_host_test(test_m4_shutdown test_m4_shutdown.cpp)
arduinoawait_add_host_test(test_m5_timers test_m5_timers.cpp)
arduinoawait_add_host_test(test_m5_overflow test_m5_overflow.cpp)
arduinoawait_add_host_test(test_m6_childawait test_m6_childawait.cpp)
arduinoawait_add_host_test(test_m6_errors test_m6_errors.cpp)
arduinoawait_add_host_test(test_m6_exhaust test_m6_exhaust.cpp)
arduinoawait_add_host_test(test_m7_event test_m7_event.cpp)
arduinoawait_add_host_test(test_m7_errors test_m7_errors.cpp)
arduinoawait_add_host_test(test_m8_flag test_m8_flag.cpp)
arduinoawait_add_host_test(test_m8_errors test_m8_errors.cpp)
arduinoawait_add_host_test(test_m9_queue test_m9_queue.cpp)
arduinoawait_add_host_test(test_m9_errors test_m9_errors.cpp)
arduinoawait_add_host_test(test_m9_shutdown test_m9_shutdown.cpp)
arduinoawait_add_host_test(test_m10_waituntil test_m10_waituntil.cpp)
arduinoawait_add_host_test(test_m10_diagnostics test_m10_diagnostics.cpp)

# The default error hook must terminate deterministically.
arduinoawait_add_death_test(test_m0_default_error test_m0_default_error.cpp)

# The default deadline-overflow policy must halt (Error::deadline_overflow).
arduinoawait_add_death_test(test_m1_deadline_overflow test_m1_deadline_overflow_death.cpp)

# Persistent negative-compile regression: including the public header under a
# pre-C++20 standard must be rejected by the coroutine-support guard. The probe
# does NOT link the arduinoawait interface target (which would raise the standard
# floor to C++20); it only adds the include path and is pinned to C++17. The
# wrapper requires the build to fail WITH the coroutine-support diagnostic (not
# merely to fail) and forwards the build configuration for multi-config
# generators.
add_executable(neg_coroutine_probe EXCLUDE_FROM_ALL neg_coroutine_probe.cpp)
target_include_directories(neg_coroutine_probe PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_target_properties(neg_coroutine_probe PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_test(NAME neg_coroutine_support_rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DAA_CMAKE=${CMAKE_COMMAND}"
        "-DAA_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DAA_TARGET=neg_coroutine_probe"
        "-DAA_DIAGNOSTIC=ArduinoAwait requires C++20 or later with standard coroutine support"
        "-DAA_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_negcompile_test.cmake")

# Persistent negative-compile regression: a FramePool capacity that does not fit
# the 32-bit block header fields must be rejected by a static_assert. On the
# 64-bit host this instantiates a >4 GiB capacity.
add_executable(neg_frame_pool_capacity EXCLUDE_FROM_ALL neg_frame_pool_capacity.cpp)
target_link_libraries(neg_frame_pool_capacity PRIVATE arduinoawait::arduinoawait)
set_target_properties(neg_frame_pool_capacity PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_test(NAME neg_frame_pool_capacity_rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DAA_CMAKE=${CMAKE_COMMAND}"
        "-DAA_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DAA_TARGET=neg_frame_pool_capacity"
        "-DAA_DIAGNOSTIC=FramePool capacity must fit"
        "-DAA_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_negcompile_test.cmake")

# Persistent negative-compile regression: a task capacity that exceeds the
# representable TaskSlot range must be rejected by a static_assert rather than
# silently narrowing a slot index (M4 M1).
add_executable(neg_scheduler_capacity EXCLUDE_FROM_ALL neg_scheduler_capacity.cpp)
target_link_libraries(neg_scheduler_capacity PRIVATE arduinoawait::arduinoawait)
set_target_properties(neg_scheduler_capacity PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_test(NAME neg_scheduler_capacity_rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DAA_CMAKE=${CMAKE_COMMAND}"
        "-DAA_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DAA_TARGET=neg_scheduler_capacity"
        "-DAA_DIAGNOSTIC=ARDUINOAWAIT_MAX_TASKS exceeds"
        "-DAA_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_negcompile_test.cmake")

# Persistent negative-compile regression: on a generic Arduino target (ARDUINO
# defined, no first-class core, no override) there is no portable ISR-safe critical
# section and the scheduler's external-signal poll step needs one, so the umbrella
# <ArduinoAwait.h> must fail to compile on include rather than emit unsafe code
# (M8 H1/N1).
add_executable(neg_flag_generic_unsupported EXCLUDE_FROM_ALL neg_flag_generic_unsupported.cpp)
target_link_libraries(neg_flag_generic_unsupported PRIVATE arduinoawait::arduinoawait)
set_target_properties(neg_flag_generic_unsupported PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_test(NAME neg_flag_generic_unsupported_rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DAA_CMAKE=${CMAKE_COMMAND}"
        "-DAA_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DAA_TARGET=neg_flag_generic_unsupported"
        "-DAA_DIAGNOSTIC=generic Arduino targets require ARDUINOAWAIT_CRITICAL_SECTION_OVERRIDE"
        "-DAA_CONFIG=$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_negcompile_test.cmake")

# Optimized no-return regression (M9 review): the no-value await_resume path must
# spin (a guaranteed C++20 forward-progress operation) rather than fall through to
# move a never-constructed object, even at -O2 with a RETURNING error hook. Built at
# -O2 on GCC/Clang (which apply the [intro.progress] rule the reviewer reproduced;
# MSVC /O2 conflicts with the harness debug /RTC1). The wrapper requires the process
# to be killed by a timeout rather than exit on its own.
if(NOT MSVC)
    add_executable(test_m9_noreturn test_m9_noreturn.cpp)
    target_link_libraries(test_m9_noreturn PRIVATE arduinoawait::arduinoawait)
    set_target_properties(test_m9_noreturn PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    target_compile_options(test_m9_noreturn PRIVATE -O2 -Wall -Wextra -Wpedantic -Werror)
    add_test(NAME test_m9_noreturn_hangs
        COMMAND "${CMAKE_COMMAND}"
            "-DAA_EXE=$<TARGET_FILE:test_m9_noreturn>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/run_hang_test.cmake")
endif()

# Metadata gate self-test (fail-closed behavior of lint_metadata_gate.py).
if(Python3_FOUND)
    add_test(NAME test_lint_gate
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_lint_gate.py")
else()
    message(STATUS "Python3 not found; skipping metadata gate self-test")
endif()

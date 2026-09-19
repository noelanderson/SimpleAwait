# ArduinoAwait host tests.
#
# Each test is compiled at the C++20 language floor and, where the toolchain
# advertises it, also at C++23 as a forward-compatibility check. Tests are
# deterministic and never sleep on wall-clock time. Optimization-level coverage
# (-O0/-O2/-Os) and sanitizers are driven from CI via CMAKE_CXX_FLAGS and
# ARDUINOAWAIT_ENABLE_SANITIZERS respectively.

option(ARDUINOAWAIT_ENABLE_SANITIZERS
    "Build ArduinoAwait host tests with ASan/UBSan (GCC/Clang)" OFF)

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
#   Like a host test, but the program is expected to terminate abnormally
#   (nonzero exit); registered WILL_FAIL so a clean exit reports a regression.
function(arduinoawait_add_death_test test_name source)
    foreach(_std IN LISTS _aa_standards)
        set(_target "${test_name}_cpp${_std}")
        add_executable("${_target}" "${source}")
        _aa_setup_test_target("${_target}" ${_std})
        add_test(NAME "${_target}" COMMAND "${_target}")
        set_tests_properties("${_target}" PROPERTIES WILL_FAIL TRUE)
    endforeach()
endfunction()

# Positive tests.
arduinoawait_add_host_test(test_m0_skeleton test_m0_skeleton.cpp)
arduinoawait_add_host_test(test_m0_config_override test_m0_config_override.cpp)
arduinoawait_add_host_test(test_m0_odr test_m0_odr_tu1.cpp test_m0_odr_tu2.cpp)

# The default error hook must terminate deterministically.
arduinoawait_add_death_test(test_m0_default_error test_m0_default_error.cpp)

# Persistent negative-compile regression: including the public header under a
# pre-C++20 standard must be rejected by the coroutine-support guard. The probe
# does NOT link the arduinoawait interface target (which would raise the standard
# floor to C++20); it only adds the include path and is pinned to C++17.
add_executable(neg_coroutine_probe EXCLUDE_FROM_ALL neg_coroutine_probe.cpp)
target_include_directories(neg_coroutine_probe PRIVATE "${CMAKE_SOURCE_DIR}/src")
set_target_properties(neg_coroutine_probe PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
add_test(NAME neg_coroutine_support_rejected
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target neg_coroutine_probe)
set_tests_properties(neg_coroutine_support_rejected PROPERTIES WILL_FAIL TRUE)

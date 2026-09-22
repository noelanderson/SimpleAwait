cmake_minimum_required(VERSION 3.20)

project(SimpleAwait
    VERSION 0.1.0
    DESCRIPTION "Deterministic fixed-memory cooperative coroutine library for Arduino"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# SimpleAwait is a header-only library. Expose it as an INTERFACE target so
# host tests (and downstream host consumers) can link against it and pick up
# the public include directory.
# ---------------------------------------------------------------------------
add_library(simpleawait INTERFACE)
add_library(simpleawait::simpleawait ALIAS simpleawait)

target_include_directories(simpleawait INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The library never *requires* more than C++20. Consumers may compile at a newer
# standard; we only advertise the floor here.
target_compile_features(simpleawait INTERFACE cxx_std_20)

# MSVC reports an inaccurate __cplusplus unless asked not to. SimpleAwait's own
# checks do not depend on __cplusplus, but making it accurate avoids surprising
# downstream code that (incorrectly) does.
if(MSVC)
    target_compile_options(simpleawait INTERFACE /Zc:__cplusplus)
endif()

# ---------------------------------------------------------------------------
# Host tests. Enabled by default when SimpleAwait is the top-level project.
# ---------------------------------------------------------------------------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(SIMPLEAWAIT_BUILD_TESTS "Build SimpleAwait host tests" ON)
else()
    option(SIMPLEAWAIT_BUILD_TESTS "Build SimpleAwait host tests" OFF)
endif()

if(SIMPLEAWAIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

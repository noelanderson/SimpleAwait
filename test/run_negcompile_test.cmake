# CTest wrapper: assert the negative-compile probe is rejected by the
# ArduinoAwait coroutine-support guard (and not by some unrelated failure).
#
# Building neg_coroutine_probe (a C++17 translation unit that includes the
# public header) must FAIL, and the failure must contain the library's
# coroutine-support diagnostic. A bare "build failed" check (e.g. CTest
# WILL_FAIL) would also accept infrastructure failures such as a broken
# toolchain; requiring the specific diagnostic rejects those. The build config
# is forwarded so multi-config generators build the intended configuration.
#
# Invoked as:
#   cmake -DAA_CMAKE=<cmake> -DAA_BUILD_DIR=<dir> -DAA_CONFIG=<cfg> \
#         -P run_negcompile_test.cmake

if(NOT DEFINED AA_CMAKE OR NOT DEFINED AA_BUILD_DIR)
    message(FATAL_ERROR "AA_CMAKE and AA_BUILD_DIR must be set")
endif()

set(_cmd "${AA_CMAKE}" --build "${AA_BUILD_DIR}" --target neg_coroutine_probe)
if(DEFINED AA_CONFIG AND NOT "${AA_CONFIG}" STREQUAL "")
    list(APPEND _cmd --config "${AA_CONFIG}")
endif()

execute_process(
    COMMAND ${_cmd}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

set(_all "${_out}${_err}")

if("${_rc}" STREQUAL "0")
    message(FATAL_ERROR
        "neg_coroutine_probe compiled successfully; the coroutine-support guard "
        "did not reject a pre-C++20 build:\n${_all}")
endif()

string(FIND "${_all}"
    "ArduinoAwait requires C++20 or later with standard coroutine support" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "neg_coroutine_probe failed to build, but WITHOUT the expected "
        "coroutine-support diagnostic (unrelated/infrastructure failure?):\n${_all}")
endif()

message(STATUS
    "neg_coroutine_probe correctly rejected with the coroutine-support diagnostic")

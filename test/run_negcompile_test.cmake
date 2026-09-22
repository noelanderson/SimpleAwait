# CTest wrapper: assert a target fails to compile with a specific diagnostic.
#
# Building SA_TARGET must FAIL, and the failure output must contain SA_DIAGNOSTIC.
# A bare "build failed" check (e.g. CTest WILL_FAIL) would also accept unrelated
# infrastructure failures such as a broken toolchain; requiring the specific
# diagnostic rejects those. The build config is forwarded so multi-config
# generators build the intended configuration.
#
# Invoked as:
#   cmake -DSA_CMAKE=<cmake> -DSA_BUILD_DIR=<dir> -DSA_TARGET=<target> \
#         -DSA_DIAGNOSTIC=<substring> -DSA_CONFIG=<cfg> \
#         -P run_negcompile_test.cmake

if(NOT DEFINED SA_CMAKE OR NOT DEFINED SA_BUILD_DIR OR
   NOT DEFINED SA_TARGET OR NOT DEFINED SA_DIAGNOSTIC)
    message(FATAL_ERROR
        "SA_CMAKE, SA_BUILD_DIR, SA_TARGET, and SA_DIAGNOSTIC must be set")
endif()

set(_cmd "${SA_CMAKE}" --build "${SA_BUILD_DIR}" --target "${SA_TARGET}")
if(DEFINED SA_CONFIG AND NOT "${SA_CONFIG}" STREQUAL "")
    list(APPEND _cmd --config "${SA_CONFIG}")
endif()

execute_process(
    COMMAND ${_cmd}
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

set(_all "${_out}${_err}")

if("${_rc}" STREQUAL "0")
    message(FATAL_ERROR
        "${SA_TARGET} compiled successfully; expected it to be rejected:\n${_all}")
endif()

string(FIND "${_all}" "${SA_DIAGNOSTIC}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "${SA_TARGET} failed to build, but WITHOUT the expected diagnostic "
        "(\"${SA_DIAGNOSTIC}\") - unrelated/infrastructure failure?:\n${_all}")
endif()

message(STATUS "${SA_TARGET} correctly rejected with: ${SA_DIAGNOSTIC}")

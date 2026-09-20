# CTest wrapper: assert a target fails to compile with a specific diagnostic.
#
# Building AA_TARGET must FAIL, and the failure output must contain AA_DIAGNOSTIC.
# A bare "build failed" check (e.g. CTest WILL_FAIL) would also accept unrelated
# infrastructure failures such as a broken toolchain; requiring the specific
# diagnostic rejects those. The build config is forwarded so multi-config
# generators build the intended configuration.
#
# Invoked as:
#   cmake -DAA_CMAKE=<cmake> -DAA_BUILD_DIR=<dir> -DAA_TARGET=<target> \
#         -DAA_DIAGNOSTIC=<substring> -DAA_CONFIG=<cfg> \
#         -P run_negcompile_test.cmake

if(NOT DEFINED AA_CMAKE OR NOT DEFINED AA_BUILD_DIR OR
   NOT DEFINED AA_TARGET OR NOT DEFINED AA_DIAGNOSTIC)
    message(FATAL_ERROR
        "AA_CMAKE, AA_BUILD_DIR, AA_TARGET, and AA_DIAGNOSTIC must be set")
endif()

set(_cmd "${AA_CMAKE}" --build "${AA_BUILD_DIR}" --target "${AA_TARGET}")
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
        "${AA_TARGET} compiled successfully; expected it to be rejected:\n${_all}")
endif()

string(FIND "${_all}" "${AA_DIAGNOSTIC}" _pos)
if(_pos EQUAL -1)
    message(FATAL_ERROR
        "${AA_TARGET} failed to build, but WITHOUT the expected diagnostic "
        "(\"${AA_DIAGNOSTIC}\") - unrelated/infrastructure failure?:\n${_all}")
endif()

message(STATUS "${AA_TARGET} correctly rejected with: ${AA_DIAGNOSTIC}")

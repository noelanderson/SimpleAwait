# CTest wrapper: assert a death-test executable terminated abnormally.
#
# The default ArduinoAwait error hook is [[noreturn]] and calls std::abort() on
# hosted builds. abort() terminates via SIGABRT on POSIX and exit code 3 on
# Windows. CTest's WILL_FAIL does not reliably invert signal-based termination,
# so instead of WILL_FAIL we run the executable here and require:
#   * it launched and reached the hook (emitting the AA_DEATH_TEST_REACHED
#     marker) — rejecting unrelated launch/infrastructure failures; and
#   * it did NOT return 0 (a clean exit would mean the hook wrongly returned).
#
# Invoked as: cmake -DAA_EXE=<path> -P run_death_test.cmake

if(NOT DEFINED AA_EXE)
    message(FATAL_ERROR "AA_EXE not set")
endif()
if(NOT EXISTS "${AA_EXE}")
    message(FATAL_ERROR "death-test executable not found: ${AA_EXE}")
endif()

execute_process(
    COMMAND "${AA_EXE}"
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

set(_all "${_out}${_err}")

string(FIND "${_all}" "AA_DEATH_TEST_REACHED" _marker)
if(_marker EQUAL -1)
    message(FATAL_ERROR
        "death test did not reach the error hook (launch/other failure); "
        "result='${_rc}':\n${_all}")
endif()

if("${_rc}" STREQUAL "0")
    message(FATAL_ERROR
        "default error hook returned normally; it must be [[noreturn]]")
endif()

message(STATUS
    "default error hook terminated abnormally (result='${_rc}') after reaching the hook")

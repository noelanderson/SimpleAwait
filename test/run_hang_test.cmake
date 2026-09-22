# CTest wrapper: assert SA_EXE never returns on its own.
#
# The deterministic no-value error path in Queue::ReceiveAwaiter::await_resume must
# spin forever (a guaranteed C++20 forward-progress operation) rather than fall
# through, even at -O2 with a RETURNING error hook. This wrapper runs SA_EXE with a
# timeout and PASSES only if the timeout has to kill it; a process that exits on its
# own — the regression the optimizer would introduce — fails the test.
#
# Invoked as:
#   cmake -DSA_EXE=<exe> -P run_hang_test.cmake

if(NOT DEFINED SA_EXE)
    message(FATAL_ERROR "SA_EXE must be set")
endif()

execute_process(
    COMMAND "${SA_EXE}"
    TIMEOUT 8
    RESULT_VARIABLE _rc
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)

if("${_rc}" MATCHES "[Tt]imeout")
    message(STATUS "${SA_EXE} correctly never returned (killed by timeout).")
else()
    message(FATAL_ERROR
        "${SA_EXE} returned on its own (result: '${_rc}') - the deterministic "
        "no-return invariant was violated (optimized fallthrough?).\n${_out}${_err}")
endif()

# ArduinoAwait host tests.
#
# Each test is compiled at the C++20 language floor and, where the toolchain
# advertises it, also at C++23 as a forward-compatibility check. Tests are
# deterministic and never sleep on wall-clock time.

option(ARDUINOAWAIT_ENABLE_SANITIZERS
    "Build ArduinoAwait host tests with ASan/UBSan (GCC/Clang)" OFF)

set(_aa_have_cxx23 FALSE)
if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(_aa_have_cxx23 TRUE)
endif()

# arduinoawait_add_host_test(<name> <source>)
#   Adds one ctest entry per available language standard (20, and 23 if
#   supported), each linking the header-only library with strict warnings.
function(arduinoawait_add_host_test test_name source)
    set(_standards 20)
    if(_aa_have_cxx23)
        list(APPEND _standards 23)
    endif()

    foreach(_std IN LISTS _standards)
        set(_target "${test_name}_cpp${_std}")
        add_executable("${_target}" "${source}")
        target_link_libraries("${_target}" PRIVATE arduinoawait::arduinoawait)

        set_target_properties("${_target}" PROPERTIES
            CXX_STANDARD ${_std}
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF)

        if(MSVC)
            target_compile_options("${_target}" PRIVATE /W4 /WX /permissive-)
        else()
            target_compile_options("${_target}" PRIVATE
                -Wall -Wextra -Wpedantic -Werror)
        endif()

        if(ARDUINOAWAIT_ENABLE_SANITIZERS AND NOT MSVC)
            target_compile_options("${_target}" PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options("${_target}" PRIVATE
                -fsanitize=address,undefined)
        endif()

        add_test(NAME "${_target}" COMMAND "${_target}")
    endforeach()
endfunction()

arduinoawait_add_host_test(test_m0_skeleton test_m0_skeleton.cpp)
arduinoawait_add_host_test(test_m0_config_override test_m0_config_override.cpp)

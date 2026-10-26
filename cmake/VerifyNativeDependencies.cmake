# Ninja compares /showIncludes prefixes byte for byte. CMake's compiler
# identification decodes that output using the console code page, which can
# differ from cl's redirected output. Probe without decoding, then require
# valid UTF-8: Ninja's generated rules must use CMake's internal encoding.
# This prevents a localized cached prefix silently discarding header edges.
function(lgaed_verify_native_dependencies)
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        return()
    endif()
    if(CMAKE_SCRIPT_MODE_FILE AND NOT EXISTS "${CMAKE_BINARY_DIR}/CMakeCache.txt")
        message(FATAL_ERROR "Standalone native dependency repair requires an existing CMake cache.")
    endif()
    if(EXISTS "${CMAKE_BINARY_DIR}/CMakeCache.txt")
        file(STRINGS "${CMAKE_BINARY_DIR}/CMakeCache.txt" _cache_locations
            REGEX "^CMAKE_(HOME_DIRECTORY|CACHEFILE_DIR):INTERNAL=")
        foreach(_location IN LISTS _cache_locations)
            if(_location MATCHES "^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$")
                set(_cached_source "${CMAKE_MATCH_1}")
            elseif(_location MATCHES "^CMAKE_CACHEFILE_DIR:INTERNAL=(.*)$")
                set(_cached_binary "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        foreach(_kind source binary)
            if(NOT DEFINED _cached_${_kind})
                message(FATAL_ERROR "Native dependency repair requires cache directory metadata.")
            endif()
            string(TOUPPER "${_kind}" _upper_kind)
            file(REAL_PATH "${CMAKE_${_upper_kind}_DIR}" _expected_location)
            file(REAL_PATH "${_cached_${_kind}}" _cached_location)
            string(TOLOWER "${_expected_location}" _expected_location)
            string(TOLOWER "${_cached_location}" _cached_location)
            if(NOT _expected_location STREQUAL _cached_location)
                message(FATAL_ERROR "Native dependency repair ${_kind} directory does not match its CMake cache.")
            endif()
        endforeach()
    endif()
    set(_probe_dir "${CMAKE_BINARY_DIR}/CMakeFiles/NativeDependencies")
    file(MAKE_DIRECTORY "${_probe_dir}")
    file(WRITE "${_probe_dir}/lgaed_dependency_probe.hpp" "#pragma once\n")
    file(WRITE "${_probe_dir}/probe.cpp" "#include \"lgaed_dependency_probe.hpp\"\nint lgaed_dependency_probe;\n")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /c probe.cpp /Foprobe.obj
        WORKING_DIRECTORY "${_probe_dir}"
        OUTPUT_VARIABLE _output ERROR_VARIABLE _error RESULT_VARIABLE _result
        ENCODING NONE)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Cannot verify native header dependencies: ${_output}${_error}")
    endif()
    string(REPLACE "\r\n" "\n" _output "${_output}")
    string(REPLACE "\n" ";" _lines "${_output}")
    set(_prefix "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "[A-Za-z]:[\\\\/][^\r\n]*lgaed_dependency_probe\\.hpp$")
            string(FIND "${_line}" "${CMAKE_MATCH_0}" _path_start)
            string(SUBSTRING "${_line}" 0 ${_path_start} _prefix)
            break()
        endif()
    endforeach()
    if(_prefix STREQUAL "")
        message(FATAL_ERROR "MSVC did not report the native dependency probe header.")
    endif()
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_prefix}" PARENT_SCOPE)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_prefix}" PARENT_SCOPE)

    # Migrate precisely the compiler metadata for this CMake version as well.
    # This also lets the standalone repair work with an unchanged checkout's
    # build button, without resetting its cache or replacing any source file.
    set(_compiler_info "${CMAKE_BINARY_DIR}/CMakeFiles/${CMAKE_VERSION}/CMakeCXXCompiler.cmake")
    file(WRITE "${_probe_dir}/prefix.bin" "${_prefix}")
    set(_compiler_arguments -PrefixFile "${_probe_dir}/prefix.bin")
    if(EXISTS "${_compiler_info}")
        list(APPEND _compiler_arguments -CompilerInfo "${_compiler_info}")
    elseif(CMAKE_SCRIPT_MODE_FILE)
        message(FATAL_ERROR "No compiler metadata for CMake ${CMAKE_VERSION} in ${CMAKE_BINARY_DIR}.")
    endif()

    # Fix legacy objects whose missing header edges survive a correct configure.
    # Only generated object timestamps are changed; sources and data stay intact.
    execute_process(
        COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
            -NoProfile -NonInteractive -ExecutionPolicy Bypass -File
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/windows/Repair-NativeDependencies.ps1"
            -SourceRoot "${CMAKE_SOURCE_DIR}" -BinaryRoot "${CMAKE_BINARY_DIR}"
            -Ninja "${CMAKE_MAKE_PROGRAM}"
            ${_compiler_arguments}
        OUTPUT_VARIABLE _repair_output ERROR_VARIABLE _repair_error RESULT_VARIABLE _repair_result
        ENCODING UTF-8)
    if(NOT _repair_result EQUAL 0)
        message(FATAL_ERROR "Cannot repair native header dependencies: ${_repair_output}${_repair_error}")
    endif()
    string(STRIP "${_repair_output}" _repair_output)
    if(NOT _repair_output STREQUAL "")
        message(STATUS "${_repair_output}")
    endif()
endfunction()

# Repair only a caller-selected build tree from another existing checkout.
# SourceRoot is read-only; all generated writes remain in BinaryRoot.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    foreach(_required SOURCE_ROOT BINARY_ROOT CXX_COMPILER NINJA)
        if(NOT DEFINED LGAED_DEPENDENCY_${_required} OR LGAED_DEPENDENCY_${_required} STREQUAL "")
            message(FATAL_ERROR "Standalone dependency repair requires LGAED_DEPENDENCY_${_required}.")
        endif()
    endforeach()
    set(CMAKE_SOURCE_DIR "${LGAED_DEPENDENCY_SOURCE_ROOT}")
    set(CMAKE_BINARY_DIR "${LGAED_DEPENDENCY_BINARY_ROOT}")
    set(CMAKE_CXX_COMPILER "${LGAED_DEPENDENCY_CXX_COMPILER}")
    set(CMAKE_MAKE_PROGRAM "${LGAED_DEPENDENCY_NINJA}")
    set(CMAKE_GENERATOR Ninja)
    lgaed_verify_native_dependencies()
endif()

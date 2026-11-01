function(lgaed_verify_native_toolchain)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "The host must be an MSVC Windows x64 binary.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
        message(FATAL_ERROR "Unexpected compiler target: ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
    endif()
    if(NOT CMAKE_GENERATOR STREQUAL "Ninja")
        message(FATAL_ERROR "Use the Ninja presets for this native base.")
    endif()
    if(NOT CMAKE_VERSION STREQUAL "4.4.5")
        message(FATAL_ERROR "Use stable CMake 4.4.5 selected by the Windows tools manifest; selected ${CMAKE_VERSION}.")
    endif()
    execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
        RESULT_VARIABLE _lgaed_ninja_result OUTPUT_VARIABLE _lgaed_ninja_version
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _lgaed_ninja_result EQUAL 0 OR NOT _lgaed_ninja_version STREQUAL "1.13.2")
        message(FATAL_ERROR "Ninja 1.13.2 is required; selected '${_lgaed_ninja_version}'.")
    endif()
    # The family folder and uninstall bundle labels do not prove which payload
    # is installed. Check actual tool resources, including direct IDE builds.
    file(READ "${CMAKE_SOURCE_DIR}/toolchains/windows.versions.json" _windows_tools)
    string(JSON _sdk_minimum GET "${_windows_tools}" visualStudio windowsSdkServicing)
    set(_sdk_probe "${CMAKE_BINARY_DIR}/CMakeFiles/VerifySdkFileVersion.ps1")
    file(WRITE "${_sdk_probe}" [=[
$ErrorActionPreference = 'Stop'
foreach ($toolPath in $args) {
    $version = (Get-Item -LiteralPath $toolPath).VersionInfo.FileVersion
    if ($version -notmatch '^(\d+\.\d+\.\d+\.\d+)') { throw "Cannot read Windows SDK file version: $toolPath" }
    [Console]::WriteLine($Matches[1])
}
]=])
    execute_process(COMMAND "$ENV{SystemRoot}/System32/WindowsPowerShell/v1.0/powershell.exe"
        -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "${_sdk_probe}"
        "${CMAKE_RC_COMPILER}" "${CMAKE_MT}"
        RESULT_VARIABLE _sdk_probe_result OUTPUT_VARIABLE _sdk_versions ERROR_VARIABLE _sdk_error
        OUTPUT_STRIP_TRAILING_WHITESPACE ENCODING UTF-8)
    if(NOT _sdk_probe_result EQUAL 0)
        message(FATAL_ERROR "Cannot verify Windows SDK servicing: ${_sdk_error}")
    endif()
    string(REPLACE "\r" "" _sdk_versions "${_sdk_versions}")
    string(REPLACE "\n" ";" _sdk_versions "${_sdk_versions}")
    list(LENGTH _sdk_versions _sdk_version_count)
    if(NOT _sdk_version_count EQUAL 2)
        message(FATAL_ERROR "Cannot establish both Windows SDK tool file versions: ${_sdk_versions}")
    endif()
    foreach(_sdk_version IN LISTS _sdk_versions)
        if(_sdk_version VERSION_LESS _sdk_minimum)
            message(FATAL_ERROR "Windows SDK servicing ${_sdk_minimum} is required; actual tool version ${_sdk_version}.")
        endif()
    endforeach()
    message(STATUS "Windows SDK servicing verified from rc.exe/mt.exe: ${_sdk_versions}")
    message(STATUS "MSVC package: $ENV{VCToolsVersion}; compiler: ${CMAKE_CXX_COMPILER_VERSION}; x64 host -> x64 target")
    message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Windows SDK/UCRT: 10.0.28000.0; resource compiler: ${CMAKE_RC_COMPILER}")
    message(STATUS "CMake: ${CMAKE_VERSION}; Ninja: ${_lgaed_ninja_version}")
endfunction()

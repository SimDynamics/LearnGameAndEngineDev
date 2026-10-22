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
    if(NOT CMAKE_VERSION STREQUAL "4.3.1-msvc1")
        message(FATAL_ERROR "Use the installed Visual Studio CMake 4.3.1-msvc1; selected ${CMAKE_VERSION}.")
    endif()
    execute_process(COMMAND "${CMAKE_MAKE_PROGRAM}" --version
        RESULT_VARIABLE _lgaed_ninja_result OUTPUT_VARIABLE _lgaed_ninja_version
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _lgaed_ninja_result EQUAL 0 OR NOT _lgaed_ninja_version STREQUAL "1.13.2")
        message(FATAL_ERROR "Ninja 1.13.2 is required; selected '${_lgaed_ninja_version}'.")
    endif()
    # The toolkit directory identifies SDK/UCRT 10.0.28000.0, not its servicing QFE.
    # Windows SDK standalone/Visual Studio installers can expose different
    # uninstall registry records. Do not reject a valid MSVC toolchain solely
    # because the 10.0.28000.2957 product name or DisplayVersion is absent.
    # The mandatory SDK/UCRT and actual include/library/tool paths are already
    # verified by WindowsToolchain.cmake.
    message(STATUS "Windows SDK servicing update: not established by the 10.0.28000.0 directory; check Installed apps for 10.0.28000.2957")
    message(STATUS "MSVC package: $ENV{VCToolsVersion}; compiler: ${CMAKE_CXX_COMPILER_VERSION}; x64 host -> x64 target")
    message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Windows SDK/UCRT: 10.0.28000.0; resource compiler: ${CMAKE_RC_COMPILER}")
    message(STATUS "CMake: ${CMAKE_VERSION}; Ninja: ${_lgaed_ninja_version}")
endfunction()

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
    # Servicing updates keep the Windows Kits include/lib folder at 10.0.28000.0.
    # Confirm the complete installed SDK release, not just the directory name.
    set(_lgaed_required_sdk_release "10.0.28000.2957")
    set(_lgaed_uninstall_root "HKLM/SOFTWARE/Microsoft/Windows/CurrentVersion/Uninstall")
    set(_lgaed_sdk_release_found FALSE)
    foreach(_lgaed_registry_view 32 64)
        cmake_host_system_information(RESULT _lgaed_installed_entries
            QUERY WINDOWS_REGISTRY "${_lgaed_uninstall_root}"
            SUBKEYS VIEW ${_lgaed_registry_view})
        foreach(_lgaed_entry IN LISTS _lgaed_installed_entries)
            set(_lgaed_product_key "${_lgaed_uninstall_root}/${_lgaed_entry}")
            cmake_host_system_information(RESULT _lgaed_product_name
                QUERY WINDOWS_REGISTRY "${_lgaed_product_key}"
                VALUE DisplayName VIEW ${_lgaed_registry_view})
            if(NOT _lgaed_product_name MATCHES
                    "^(Windows Software Development Kit|Windows SDK for Windows|Microsoft Windows SDK)")
                continue()
            endif()
            cmake_host_system_information(RESULT _lgaed_product_version
                QUERY WINDOWS_REGISTRY "${_lgaed_product_key}"
                VALUE DisplayVersion VIEW ${_lgaed_registry_view})
            # Windows SDK installers may register DisplayVersion as 10.1.<build>.<QFE>.
            if(_lgaed_product_name MATCHES "10[.]0[.]28000[.]2957"
                    OR _lgaed_product_version MATCHES "^10[.][01][.]28000[.]2957$")
                set(_lgaed_sdk_release_found TRUE)
                break()
            endif()
        endforeach()
        if(_lgaed_sdk_release_found)
            break()
        endif()
    endforeach()
    if(NOT _lgaed_sdk_release_found)
        message(FATAL_ERROR
            "Windows SDK 10.0.28000.2957 (September 2026 servicing release) is required. "
            "The 10.0.28000.0 SDK/UCRT directory alone does not prove this release. "
            "Check Windows Settings > Apps > Installed apps for the full SDK version. "
            "Install the official latest SDK from https://learn.microsoft.com/windows/apps/windows-sdk/downloads "
            "and reconfigure. No changes to Visual Studio, SDKs or user files were made.")
    endif()
    message(STATUS "Windows SDK servicing release: ${_lgaed_required_sdk_release} (registered installation)")
    message(STATUS "MSVC package: $ENV{VCToolsVersion}; compiler: ${CMAKE_CXX_COMPILER_VERSION}; x64 host -> x64 target")
    message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Windows SDK/UCRT: 10.0.28000.0; resource compiler: ${CMAKE_RC_COMPILER}")
    message(STATUS "CMake: ${CMAKE_VERSION}; Ninja: ${_lgaed_ninja_version}")
endfunction()

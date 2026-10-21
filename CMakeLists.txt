cmake_minimum_required(VERSION 4.3.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use a build preset; in-source builds would mix generated files with imported content.")
endif()

project(LearnGameAndEngineDev VERSION 0.1.0 LANGUAGES CXX RC)

include(cmake/VerifyNativeToolchain.cmake)
lgaed_verify_native_toolchain()

# These settings apply to the host, not to future student compilation activities.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_filename_component(_lgaed_checkout_name "${CMAKE_SOURCE_DIR}" NAME)
if(_lgaed_checkout_name MATCHES "^LearnGameAndEngineDev-(Work|Codex)$")
    set(_lgaed_initial_profile "${CMAKE_MATCH_1}")
else()
    set(_lgaed_initial_profile "")
endif()
set(LGAED_DEFAULT_PROFILE "${_lgaed_initial_profile}" CACHE STRING "Default development data profile (Work or Codex)")
set_property(CACHE LGAED_DEFAULT_PROFILE PROPERTY STRINGS Work Codex)
if(NOT LGAED_DEFAULT_PROFILE MATCHES "^(Work|Codex)$")
    message(FATAL_ERROR "Select -DLGAED_DEFAULT_PROFILE=Work or Codex for this checkout.")
endif()

configure_file(cmake/native_build_config.hpp.in generated/native_build_config.hpp @ONLY)

add_library(lgaed_host_options INTERFACE)
target_compile_definitions(lgaed_host_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_host_options INTERFACE /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
target_include_directories(lgaed_host_options INTERFACE
    "${CMAKE_SOURCE_DIR}/native"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_library(lgaed_core STATIC native/core/profile.cpp)
target_link_libraries(lgaed_core PUBLIC lgaed_host_options)

add_library(lgaed_platform_windows STATIC native/platform/windows/environment.cpp)
target_link_libraries(lgaed_platform_windows PUBLIC lgaed_core lgaed_host_options)
target_link_libraries(lgaed_platform_windows PRIVATE shell32 ole32 advapi32)

add_executable(LearnGameAndEngineDev WIN32
    native/presentation/windows/main.cpp
    native/presentation/windows/app.manifest)
target_link_libraries(LearnGameAndEngineDev PRIVATE
    lgaed_core lgaed_platform_windows lgaed_host_options
    user32 gdi32 shell32 ole32 advapi32)
set_target_properties(LearnGameAndEngineDev PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

include(CTest)
if(BUILD_TESTING)
    add_executable(lgaed_profile_tests native/core/profile_tests.cpp)
    target_link_libraries(lgaed_profile_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.profile COMMAND lgaed_profile_tests)
    add_test(NAME native.window_smoke
        COMMAND LearnGameAndEngineDev --profile "${LGAED_DEFAULT_PROFILE}"
        --qa-instance "ctest-${CMAKE_BUILD_TYPE}" --smoke-test)
    set_tests_properties(native.window_smoke PROPERTIES TIMEOUT 20)
endif()

install(TARGETS LearnGameAndEngineDev RUNTIME DESTINATION bin)
message(STATUS "Development profile: ${LGAED_DEFAULT_PROFILE}; build and install trees belong to this checkout.")

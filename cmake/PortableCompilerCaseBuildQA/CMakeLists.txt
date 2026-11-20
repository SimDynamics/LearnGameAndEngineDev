cmake_minimum_required(VERSION 3.25)
project(LGAEDPortableCompilerCaseBuildQA LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The native compiler-case history must be compiled with Windows MSVC.")
endif()

# Compile the actual application sources on disposable hosted Windows without
# changing the pinned full application toolchain, dependencies or learner data.
get_filename_component(LGAED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LGAED_CPP_STANDARD 23)
set(LGAED_CPP_STANDARD_OPTION "/std:c++latest")
set(LGAED_DEFAULT_PROFILE "Codex")
set(LGAED_DEP_ROOT "")
set(LGAED_EDUCATIONAL_VC_ROOT "")
set(LGAED_EDUCATIONAL_SDK_ROOT "")
configure_file("${LGAED_ROOT}/cmake/native_build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/native_build_config.hpp" @ONLY)

add_library(lgaed_compiler_case_native_compile OBJECT
    "${LGAED_ROOT}/native/platform/windows/compiler_completion.cpp"
    "${LGAED_ROOT}/native/platform/windows/compiler_case_revision_state_tests.cpp")
target_include_directories(lgaed_compiler_case_native_compile PRIVATE
    "${LGAED_ROOT}/native"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(lgaed_compiler_case_native_compile PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_compiler_case_native_compile PRIVATE
    /std:c++latest /EHsc /W4 /permissive- /Zc:__cplusplus /utf-8 /bigobj)

# An OBJECT library compiles actual files but never links or runs the pinned
# complete Windows reader. The portable receipt and evaluator tests execute
# separately in the invoking workflow on GCC, Clang and hosted MSVC.

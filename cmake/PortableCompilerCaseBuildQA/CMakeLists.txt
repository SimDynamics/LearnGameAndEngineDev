cmake_minimum_required(VERSION 3.25)
project(LGAEDPortableCompilerCaseBuildQA LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Native compiler-case receipts require Windows MSVC.")
endif()

# Reuse the existing isolated Windows libraries rather than inventing test
# stand-ins for the real WinRT loader, StateStore or compiler completion logic.
# This never configures the pinned full application or a learner profile.
get_filename_component(LGAED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_subdirectory("${LGAED_ROOT}/cmake/PortableCompilerGuidanceQA"
    "${CMAKE_CURRENT_BINARY_DIR}/guidance")

add_executable(lgaed_compiler_case_revision_state_tests
    "${LGAED_ROOT}/native/platform/windows/compiler_case_revision_state_tests.cpp")
target_link_libraries(lgaed_compiler_case_revision_state_tests PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_compile_options(lgaed_compiler_case_revision_state_tests PRIVATE /bigobj)
target_link_options(lgaed_compiler_case_revision_state_tests PRIVATE /OPT:REF)

enable_testing()
add_test(NAME native.compiler_case_return_state
    COMMAND lgaed_compiler_case_revision_state_tests
        "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/compiler-case-revision"
        "${CMAKE_CURRENT_BINARY_DIR}/qa" --return-only)
set_tests_properties(native.compiler_case_return_state PROPERTIES
    TIMEOUT 240 RUN_SERIAL TRUE)

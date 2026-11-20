cmake_minimum_required(VERSION 3.25)
project(LGAEDPortableCompilerGuidanceQA LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The actual state-store and WinRT loader QA requires Windows MSVC.")
endif()

# Isolated hosted-Windows checks of the real native reader's state and
# compiler-guidance implementation. The application's pinned toolchain,
# dependencies and build tree are not changed or bypassed.
get_filename_component(LGAED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LGAED_CPP_STANDARD 23)
set(LGAED_CPP_STANDARD_OPTION "/std:c++latest")
set(LGAED_DEFAULT_PROFILE "Codex")
set(LGAED_DEP_ROOT "")
set(LGAED_EDUCATIONAL_VC_ROOT "")
set(LGAED_EDUCATIONAL_SDK_ROOT "")
# The root application config embeds CMAKE_SOURCE_DIR as its lesson-data
# locator. In this standalone subproject CMAKE_SOURCE_DIR points at the QA
# script directory, so substitute only that placeholder with the actual
# checkout root; keep every other original build-config field unchanged.
file(READ "${LGAED_ROOT}/cmake/native_build_config.hpp.in" _lgaed_original_config)
string(REPLACE "@CMAKE_SOURCE_DIR@" "@LGAED_ROOT@"
    _lgaed_qa_config "${_lgaed_original_config}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/native_build_config_qa.hpp.in"
    "${_lgaed_qa_config}")
configure_file("${CMAKE_CURRENT_BINARY_DIR}/native_build_config_qa.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/native_build_config.hpp" @ONLY)

add_library(lgaed_guidance_qa_options INTERFACE)
target_compile_definitions(lgaed_guidance_qa_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_guidance_qa_options INTERFACE
    /std:c++latest /EHsc /W4 /permissive- /Zc:__cplusplus /utf-8 /Gy)
target_include_directories(lgaed_guidance_qa_options INTERFACE
    "${LGAED_ROOT}/native" "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Keep one authentic implementation, with no test stand-ins. MSVC's /Gy
# and the linker's /OPT:REF discard platform functions this fixture does
# not call, but the state store, authored loader and receipt logic remain real.
add_library(lgaed_guidance_core STATIC
    "${LGAED_ROOT}/native/core/profile.cpp"
    "${LGAED_ROOT}/native/core/compile_check.cpp"
    "${LGAED_ROOT}/native/core/lesson.cpp"
    "${LGAED_ROOT}/native/core/lesson_state.cpp"
    "${LGAED_ROOT}/native/core/lesson_page_reach.cpp"
    "${LGAED_ROOT}/native/core/text_semantics.cpp"
    "${LGAED_ROOT}/native/core/cpp_project.cpp"
    "${LGAED_ROOT}/native/core/image_resource.cpp"
    "${LGAED_ROOT}/native/core/simulation.cpp"
    "${LGAED_ROOT}/native/core/catalog.cpp"
    "${LGAED_ROOT}/native/core/audio_schedule.cpp"
    "${LGAED_ROOT}/native/core/learning_flow.cpp"
    "${LGAED_ROOT}/native/core/view_preferences.cpp"
    "${LGAED_ROOT}/native/core/narration_state.cpp"
    "${LGAED_ROOT}/native/core/scientific_calculator.cpp")
target_link_libraries(lgaed_guidance_core PUBLIC lgaed_guidance_qa_options)

add_library(lgaed_guidance_platform STATIC
    "${LGAED_ROOT}/native/platform/windows/environment.cpp"
    "${LGAED_ROOT}/native/platform/windows/content_loader.cpp"
    "${LGAED_ROOT}/native/platform/windows/bibliography_revision.cpp"
    "${LGAED_ROOT}/native/platform/windows/state_store.cpp"
    "${LGAED_ROOT}/native/platform/windows/compiler_completion.cpp"
    "${LGAED_ROOT}/native/platform/windows/cpp_runner.cpp"
    "${LGAED_ROOT}/native/platform/windows/cpp_project_history.cpp"
    "${LGAED_ROOT}/native/platform/windows/profile_migration.cpp"
    "${LGAED_ROOT}/native/platform/windows/android_import.cpp")
# The production content loader has many authored validation paths. Its
# large translation unit needs MSVC's extended object section table.
target_compile_options(lgaed_guidance_platform PRIVATE /bigobj)
target_link_libraries(lgaed_guidance_platform PUBLIC
    lgaed_guidance_core lgaed_guidance_qa_options
    shell32 shlwapi sapi ole32 oleaut32 advapi32 windowsapp runtimeobject
    bcrypt xmllite user32)

add_executable(lgaed_compiler_guidance_portable_qa
    "${LGAED_ROOT}/native/platform/windows/compiler_guidance_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/math_matching_text_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/indexed_unique_transfer_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/defaulted_move_policy_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/independent_copy_assignment_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/move_assignment_invariant_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/array_owner_read_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/array_copy_move_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/array_return_take_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/polymorphic_destruction_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/virtual_lifetime_dispatch_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/derived_preparation_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/abstract_interface_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/interface_collection_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/polymorphic_clone_state_tests.cpp"
    "${LGAED_ROOT}/native/platform/windows/dual_interface_state_tests.cpp")
target_link_libraries(lgaed_compiler_guidance_portable_qa PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_compiler_guidance_portable_qa PRIVATE /OPT:REF)
enable_testing()
add_test(NAME native.compiler_guidance_state
    COMMAND lgaed_compiler_guidance_portable_qa)
add_test(NAME native.compiler_review_guidance_priority
    COMMAND lgaed_compiler_guidance_portable_qa --review-priority)
add_test(NAME native.u12l24_authored_compiler_guides
    COMMAND lgaed_compiler_guidance_portable_qa --authored-l24-guides)
add_test(NAME native.u9l13_general_choice_editorial
    COMMAND lgaed_compiler_guidance_portable_qa --general-choice-live)
add_test(NAME native.u5l13_identifier_rename_historical
    COMMAND lgaed_compiler_guidance_portable_qa --identifier-rename-live)
add_test(NAME native.u5l13_math_matching_text_historical
    COMMAND lgaed_compiler_guidance_portable_qa --math-matching-text-live)
add_test(NAME native.u6l5_indexed_unique_transfer_state
    COMMAND lgaed_compiler_guidance_portable_qa --indexed-unique-transfer-state-live)
add_test(NAME native.u6l6_defaulted_move_policy_state
    COMMAND lgaed_compiler_guidance_portable_qa --defaulted-move-policy-state-live)
add_test(NAME native.u6l7_independent_copy_assignment_state
    COMMAND lgaed_compiler_guidance_portable_qa --independent-copy-assignment-state-live)
add_test(NAME native.u6l8_move_assignment_invariant_state
    COMMAND lgaed_compiler_guidance_portable_qa --move-assignment-invariant-state-live)
add_test(NAME native.u6l10_array_owner_read_state
    COMMAND lgaed_compiler_guidance_portable_qa --array-owner-read-state-live)
add_test(NAME native.u6l11_array_copy_move_state
    COMMAND lgaed_compiler_guidance_portable_qa --array-copy-move-state-live)
add_test(NAME native.u6l12_array_return_take_state
    COMMAND lgaed_compiler_guidance_portable_qa --array-return-take-state-live)
add_test(NAME native.u8l5_polymorphic_destruction_state
    COMMAND lgaed_compiler_guidance_portable_qa --polymorphic-destruction-state-live)
add_test(NAME native.u8l6_virtual_lifetime_state
    COMMAND lgaed_compiler_guidance_portable_qa --virtual-lifetime-state-live)
add_test(NAME native.u8l7_derived_preparation_state
    COMMAND lgaed_compiler_guidance_portable_qa --derived-preparation-state-live)
add_test(NAME native.u8l8_abstract_interface_state
    COMMAND lgaed_compiler_guidance_portable_qa --abstract-interface-state-live)
add_test(NAME native.u8l9_interface_collection_state
    COMMAND lgaed_compiler_guidance_portable_qa --interface-collection-state-live)
add_test(NAME native.u8l10_polymorphic_clone_state
    COMMAND lgaed_compiler_guidance_portable_qa --polymorphic-clone-state-live)
add_test(NAME native.u10l1_dual_interface_state
    COMMAND lgaed_compiler_guidance_portable_qa --dual-interface-state-live)
set_tests_properties(native.compiler_guidance_state
    native.compiler_review_guidance_priority
    native.u12l24_authored_compiler_guides
    native.u9l13_general_choice_editorial
    native.u5l13_identifier_rename_historical
    native.u5l13_math_matching_text_historical
    native.u6l5_indexed_unique_transfer_state
    native.u6l6_defaulted_move_policy_state
    native.u6l7_independent_copy_assignment_state
    native.u6l8_move_assignment_invariant_state
    native.u6l10_array_owner_read_state
    native.u6l11_array_copy_move_state
    native.u6l12_array_return_take_state
    native.u8l5_polymorphic_destruction_state
    native.u8l6_virtual_lifetime_state
    native.u8l7_derived_preparation_state
    native.u8l8_abstract_interface_state
    native.u8l9_interface_collection_state
    native.u8l10_polymorphic_clone_state
    native.u10l1_dual_interface_state PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# Read-only authoring reference for Work: validate each proposed property
# with the actual WinRT full and compact loaders without writing lesson JSON.
add_executable(lgaed_p3_authoring_state_tests
    "${LGAED_ROOT}/native/platform/windows/p3_ownership_authoring_state_tests.cpp")
target_link_libraries(lgaed_p3_authoring_state_tests PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_p3_authoring_state_tests PRIVATE /OPT:REF)
add_test(NAME native.p3_fourteen_ownership_authoring
    COMMAND lgaed_p3_authoring_state_tests)
set_tests_properties(native.p3_fourteen_ownership_authoring PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)

# Local-int contracts use the real WinRT loader and StateStore codec without
# running student binaries or changing the pinned Windows toolchain.
add_executable(lgaed_local_int_state_tests
    "${LGAED_ROOT}/native/platform/windows/local_int_state_tests.cpp")
target_link_libraries(lgaed_local_int_state_tests PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_local_int_state_tests PRIVATE /OPT:REF)
add_test(NAME native.compiler_local_int_state
    COMMAND lgaed_local_int_state_tests)
set_tests_properties(native.compiler_local_int_state PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)

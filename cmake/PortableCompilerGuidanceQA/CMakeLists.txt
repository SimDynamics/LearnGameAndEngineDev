cmake_minimum_required(VERSION 3.25)
project(LGAEDPortableCompilerGuidanceQA LANGUAGES CXX)

option(LGAED_HOSTED_FEEDBACK_FONT_QA
    "Run the real native LessonView font-parity checks in disposable hosted Windows" OFF)

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
# Hosted Win32 execution needs the *disposable runner's* compiler and
# SDK paths, not the separately pinned MSVC 14.51/SDK 28000 installation.
# These values are confined to this standalone QA build; the root product
# manifest and toolchain checks remain unchanged.
if("$ENV{VCToolsInstallDir}" STREQUAL "" OR
   "$ENV{WindowsSdkDir}" STREQUAL "" OR
   "$ENV{WindowsSDKVersion}" STREQUAL "")
    message(FATAL_ERROR "Hosted Win32 source QA needs activated MSVC/SDK tools.")
endif()
file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" LGAED_EDUCATIONAL_VC_ROOT)
file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" LGAED_EDUCATIONAL_SDK_ROOT)
string(REGEX MATCH "[0-9]+[.][0-9]+[.][0-9]+[.][0-9]+"
    LGAED_HOSTED_WINDOWS_SDK_VERSION "$ENV{WindowsSDKVersion}")
if(NOT LGAED_HOSTED_WINDOWS_SDK_VERSION)
    message(FATAL_ERROR "Cannot resolve the hosted Windows SDK version.")
endif()
# The production config template deliberately pins SDK 28000. Substitute
# ONLY its generated copy for this independently labeled hosted QA.
# The original template remains immutable.
# The root application config embeds CMAKE_SOURCE_DIR as its lesson-data
# locator. In this standalone subproject CMAKE_SOURCE_DIR points at the QA
# script directory, so substitute only that placeholder with the actual
# checkout root; keep every other original build-config field unchanged.
file(READ "${LGAED_ROOT}/cmake/native_build_config.hpp.in" _lgaed_original_config)
string(REPLACE "@CMAKE_SOURCE_DIR@" "@LGAED_ROOT@"
    _lgaed_qa_config "${_lgaed_original_config}")
string(REPLACE "10.0.28000.0" "${LGAED_HOSTED_WINDOWS_SDK_VERSION}"
    _lgaed_qa_config "${_lgaed_qa_config}")
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
# Run the production source probe's own adversarial regressions in the same
# hosted MSVC CTest job that checks sealed progress and native StateStore.
add_executable(lgaed_local_int_probe_source_qa
    "${LGAED_ROOT}/native/core/cpp_local_int_probe_tests.cpp")
target_link_libraries(lgaed_local_int_probe_source_qa PRIVATE
    lgaed_guidance_qa_options)
add_test(NAME native.local_int_probe_neutral_state
    COMMAND lgaed_local_int_probe_source_qa)
set_tests_properties(native.local_int_probe_neutral_state
    PROPERTIES TIMEOUT 60)
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

# Three original U3L9 assertion activities, run in the SAME bounded Win32
# child process worker used by the actual app. Unlike the lightweight Python
# MSVC baseline, this test checks the authentic request and grading path.
add_executable(lgaed_native_assertion_runner_tests
    "${LGAED_ROOT}/native/platform/windows/native_assertion_runner_tests.cpp")
target_link_libraries(lgaed_native_assertion_runner_tests PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_native_assertion_runner_tests PRIVATE /OPT:REF)
# The real worker launches this UTF-8 console companion for every MSVC
# compilation. Keep it beside the isolated QA executable, as in production.
add_dependencies(lgaed_native_assertion_runner_tests lgaed_cpp_tool_host)
add_test(NAME native.u3l9_assertion_contracts
    COMMAND lgaed_native_assertion_runner_tests)
set_tests_properties(native.u3l9_assertion_contracts PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)

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

# Verify explicitly selected historical/new resets remain valid, while
# malformed new-generation and cross-parent scopes reject before mutation.
add_executable(lgaed_course_edition_scope_qa
    "${LGAED_ROOT}/native/core/course_edition_scope_tests.cpp")
target_link_libraries(lgaed_course_edition_scope_qa PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_course_edition_scope_qa PRIVATE /OPT:REF)
add_test(NAME native.course_edition_reset_scope
    COMMAND lgaed_course_edition_scope_qa)
set_tests_properties(native.course_edition_reset_scope PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE)

# Host the original UTF-8 MSVC compiler runner in a private console.
# The test executable finds this companion beside itself, not in a personal
# installation. It is only built for the hosted U2L7 end-to-end regression.
add_executable(lgaed_cpp_tool_host
    "${LGAED_ROOT}/native/platform/windows/cpp_tool_host.cpp")
target_link_libraries(lgaed_cpp_tool_host PRIVATE
    lgaed_guidance_qa_options user32)

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

# Execute the actual U2L7 local-string lesson and private probes with hosted
# MSVC; preserve its own isolated QA profile and independent StateStore reopen.
# This is NOT a production-pinned Windows application acceptance test.
add_executable(lgaed_local_string_state_tests
    "${LGAED_ROOT}/native/platform/windows/local_string_state_tests.cpp")
target_link_libraries(lgaed_local_string_state_tests PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_local_string_state_tests PRIVATE /OPT:REF)
add_dependencies(lgaed_local_string_state_tests lgaed_cpp_tool_host)
add_test(NAME native.compiler_local_string_state
    COMMAND lgaed_local_string_state_tests)
set_tests_properties(native.compiler_local_string_state PROPERTIES
    TIMEOUT 360 RUN_SERIAL TRUE)

# Independent current-U2L5 reader gate and bookmark regressions on hosted
# Windows. This uses the real published loader and narration-state codec.
# It does not synthesize PCM or claim installed-application playback.
add_executable(lgaed_reading_gate_portable_qa
    "${LGAED_ROOT}/native/presentation/windows/reading_gate_policy_tests.cpp")
target_link_libraries(lgaed_reading_gate_portable_qa PRIVATE
    lgaed_guidance_platform lgaed_guidance_qa_options)
target_link_options(lgaed_reading_gate_portable_qa PRIVATE /OPT:REF)
add_test(NAME native.reading_gate_u2l5_bookmarks
    COMMAND lgaed_reading_gate_portable_qa)
set_tests_properties(native.reading_gate_u2l5_bookmarks PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE)


# Optional authentic Win32 UI regression. Hosted MSVC is allowed here ONLY
# for this isolated source/UI check; it is not the pinned production toolchain.
# The complete application's CMake graph and dependencies remain unchanged.
if(LGAED_HOSTED_FEEDBACK_FONT_QA)
    target_sources(lgaed_guidance_platform PRIVATE
        "${LGAED_ROOT}/native/platform/windows/narration_preferences.cpp"
        "${LGAED_ROOT}/native/platform/windows/catalog_loader.cpp"
        "${LGAED_ROOT}/native/platform/windows/audio_generation.cpp"
        "${LGAED_ROOT}/native/platform/windows/narration_worker.cpp"
        "${LGAED_ROOT}/native/platform/windows/narration_cpu_policy.cpp"
        "${LGAED_ROOT}/native/platform/windows/narration_player.cpp"
        "${LGAED_ROOT}/native/platform/windows/system_speech.cpp"
        "${LGAED_ROOT}/native/platform/windows/system_speech_process.cpp")
    add_library(lgaed_platform_windows ALIAS lgaed_guidance_platform)
    add_library(lgaed_host_options ALIAS lgaed_guidance_qa_options)
    include("${LGAED_ROOT}/cmake/CurricularWindows.cmake")
    # The real LessonView is a large Windows translation unit. Hosted Debug
    # MSVC otherwise exhausts COFF's classic section count (C1128).
    # Keep this source-scoped; application build flags remain unchanged.
    set_source_files_properties(
        "${LGAED_ROOT}/native/presentation/windows/lesson_view.cpp"
        PROPERTIES COMPILE_OPTIONS "/bigobj")
    add_executable(lgaed_feedback_font_portable_qa
        "${LGAED_ROOT}/native/presentation/windows/feedback_font_portable_tests.cpp")
    target_link_libraries(lgaed_feedback_font_portable_qa PRIVATE
        lgaed_curricular_windows lgaed_guidance_qa_options)
    target_link_options(lgaed_feedback_font_portable_qa PRIVATE /OPT:REF)
    add_test(NAME native.feedback_font_portable
        COMMAND lgaed_feedback_font_portable_qa)
    set_tests_properties(native.feedback_font_portable PROPERTIES
        TIMEOUT 120 RUN_SERIAL TRUE)
    # Real authored U2L3 semantics: verify ranges, visual colors, existing
    # feedback and isolated StateStore reopen using original Win32 controls.
    # This excludes PCM/audio acceptance and does not relax pinned app tools.
    add_executable(lgaed_u2_semantic_portable_qa
        "${LGAED_ROOT}/native/presentation/windows/technical_context_view_tests.cpp")
    target_link_libraries(lgaed_u2_semantic_portable_qa PRIVATE
        lgaed_curricular_windows lgaed_guidance_qa_options)
    target_compile_options(lgaed_u2_semantic_portable_qa PRIVATE /bigobj)
    target_link_options(lgaed_u2_semantic_portable_qa PRIVATE /OPT:REF)
    add_test(NAME native.u2_semantic_view_portable
        COMMAND lgaed_u2_semantic_portable_qa --current-u2-roles)
    set_tests_properties(native.u2_semantic_view_portable PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE)
    # Exercise the two current U2L5 REQUIRED compiler gates with production
    # LessonView and StateStore, without pretending model receipts are runs
    # or generating TTS audio on hosted Windows.
    add_test(NAME native.u2l5_native_reader_gates
        COMMAND lgaed_u2_semantic_portable_qa --current-u2l5-native-gates)
    set_tests_properties(native.u2l5_native_reader_gates PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE)
    # U2L6 native reader regression: two source-owned independent compiler
    # gates, restored drafts, attempted manual advance and exact references.
    add_test(NAME native.u2l6_native_reader_gates
        COMMAND lgaed_u2_semantic_portable_qa --current-u2l6-native-gates)
    set_tests_properties(native.u2l6_native_reader_gates PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE)
    # Original U1L2's five code lines and the optional blank on real Win32
    # controls; this does not alter the pinned product toolchain or curriculum.
    add_executable(lgaed_u1l2_order_whitespace_portable_qa
        "${LGAED_ROOT}/native/presentation/windows/order_code_view_tests.cpp")
    target_link_libraries(lgaed_u1l2_order_whitespace_portable_qa PRIVATE
        lgaed_curricular_windows lgaed_guidance_qa_options)
    target_link_options(lgaed_u1l2_order_whitespace_portable_qa PRIVATE /OPT:REF)
    add_test(NAME native.u1l2_order_blank_portable
        COMMAND lgaed_u1l2_order_whitespace_portable_qa --whitespace)
    set_tests_properties(native.u1l2_order_blank_portable PROPERTIES
        TIMEOUT 180 RUN_SERIAL TRUE)
    # Reuse the exact original nine-inline regression (including active U2L5)
    # with real production LessonView, CppFillView and StateStore. This
    # standalone hosted fixture does not run student code or synthesize audio;
    # its compiler prerequisite receipts are explicitly synthetic QA data.
    add_executable(lgaed_inline_feedback_portable_qa
        "${LGAED_ROOT}/native/presentation/windows/lesson_view_tests.cpp")
    target_compile_definitions(lgaed_inline_feedback_portable_qa PRIVATE
        LGAED_INLINE_FEEDBACK_ONLY)
    target_compile_options(lgaed_inline_feedback_portable_qa PRIVATE /bigobj)
    target_link_libraries(lgaed_inline_feedback_portable_qa PRIVATE
        lgaed_curricular_windows lgaed_guidance_qa_options)
    target_link_options(lgaed_inline_feedback_portable_qa PRIVATE /OPT:REF)
    add_test(NAME native.inline_feedback_u2l5_portable
        COMMAND lgaed_inline_feedback_portable_qa --inline-feedback)
    set_tests_properties(native.inline_feedback_u2l5_portable PROPERTIES
        TIMEOUT 300 RUN_SERIAL TRUE)
endif()

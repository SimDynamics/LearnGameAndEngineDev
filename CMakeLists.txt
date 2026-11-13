cmake_minimum_required(VERSION 4.4.5)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use a build preset; in-source builds would mix generated files with imported content.")
endif()

include(cmake/WindowsCxxStandard.cmake)
set(CMAKE_USER_MAKE_RULES_OVERRIDE_CXX "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindowsCxxStandard.cmake")
project(LearnGameAndEngineDev VERSION 0.1.0 LANGUAGES CXX RC)

option(LGAED_STATE_IMPORT_QA_ONLY
    "Build only the four published native state-import regressions with the official Windows toolchain" OFF)
option(LGAED_NARRATION_PLAYER_QA_ONLY
    "Build only the three real Windows narration-player probes with the official Windows toolchain" OFF)
option(LGAED_READER_BUILD_QA_ONLY
    "Compile and link the three original curricular reader/diagnostic test executables without running them" OFF)
option(LGAED_WASI_RUNNER_QA_ONLY
    "Build and run actual Windows WASI SDK / Wasmtime single-file compiler and output regressions" OFF)
option(LGAED_NARRATION_WORKER_QA_ONLY
    "Build and test real Spanish Kokoro synthesis and playback failure diagnostics without a reader or audio endpoint" OFF)
option(LGAED_U12_READER_QA_ONLY
    "Build and run the three original U12L4 published-seventeen reader regressions with real Kokoro playback" OFF)
option(LGAED_U11_EDITORIAL_READER_QA_ONLY
    "Build and run the ten original published U11 editorial reader regressions with real Kokoro playback" OFF)
option(LGAED_COMPILER_HISTORICAL_READER_QA_ONLY
    "Build and run the fourteen original historical compiler reader regressions with real Kokoro playback" OFF)
option(LGAED_U6_DIAGNOSTIC_READER_QA_ONLY
    "Build and run the three original U6 diagnostic reader flows with real Kokoro playback" OFF)
set(_lgaed_qa_mode_count 0)
foreach(_mode LGAED_STATE_IMPORT_QA_ONLY LGAED_NARRATION_PLAYER_QA_ONLY
        LGAED_READER_BUILD_QA_ONLY LGAED_WASI_RUNNER_QA_ONLY LGAED_NARRATION_WORKER_QA_ONLY LGAED_U12_READER_QA_ONLY
        LGAED_U11_EDITORIAL_READER_QA_ONLY LGAED_COMPILER_HISTORICAL_READER_QA_ONLY
        LGAED_U6_DIAGNOSTIC_READER_QA_ONLY)
    if(${_mode})
        math(EXPR _lgaed_qa_mode_count "${_lgaed_qa_mode_count} + 1")
    endif()
endforeach()
if(_lgaed_qa_mode_count GREATER 1)
    message(FATAL_ERROR "Select only one focused Windows QA mode in each build directory.")
endif()

include(cmake/VerifyNativeToolchain.cmake)
lgaed_verify_native_toolchain()
include(cmake/VerifyNativeDependencies.cmake)
lgaed_verify_native_dependencies()
if(_lgaed_qa_mode_count GREATER 0)
    include(cmake/WindowsStateImportQaTools.cmake)
    lgaed_select_windows_state_import_qa_tools()
else()
    include(cmake/WindowsRuntimeTools.cmake)
    lgaed_select_windows_runtime_tools()
endif()

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "/Zc:__cplusplus")
unset(LGAED_CPP23_MODE_VERIFIED CACHE)
check_cxx_source_compiles([=[
#include <expected>
#include <utility>
static_assert(__cplusplus == 202302L && _MSVC_LANG == 202302L);
constexpr int language_probe() { if consteval { return 23; } else { return 0; } }
static_assert(language_probe() == 23);
int main() { std::expected<int,int> value = 23; return value.value() == 23 ? 0 : 1; }
]=] LGAED_CPP23_MODE_VERIFIED)
unset(CMAKE_REQUIRED_FLAGS)
if(NOT LGAED_CPP23_MODE_VERIFIED)
    message(FATAL_ERROR "The selected Windows compiler must provide the exact C++23 language profile and library.")
endif()
message(STATUS "Windows C++${CMAKE_CXX_STANDARD}: ${LGAED_CPP_STANDARD_OPTION}")
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

# The developer explicitly authorized C:/LGAED-Dep for this Windows
# project's dependencies. Replace any cached E:-drive path on local builds;
# only a disposable, GitHub-hosted CI runner may select its own temp root.
set(_lgaed_authorized_dependencies "C:/LGAED-Dep")
if("$ENV{GITHUB_ACTIONS}" STREQUAL "true" AND "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
    set(LGAED_DEP_ROOT "${_lgaed_authorized_dependencies}" CACHE PATH
        "LearnGameAndEngineDev dependency root (isolated CI may override)")
else()
    set(LGAED_DEP_ROOT "${_lgaed_authorized_dependencies}" CACHE PATH
        "User-authorized LearnGameAndEngineDev dependencies" FORCE)
endif()
file(TO_CMAKE_PATH "${LGAED_DEP_ROOT}" LGAED_DEP_ROOT)
file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" LGAED_EDUCATIONAL_VC_ROOT)
file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" LGAED_EDUCATIONAL_SDK_ROOT)
configure_file(cmake/native_build_config.hpp.in generated/native_build_config.hpp @ONLY)

add_library(lgaed_host_options INTERFACE)
target_compile_definitions(lgaed_host_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_host_options INTERFACE /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
target_include_directories(lgaed_host_options INTERFACE
    "${CMAKE_SOURCE_DIR}/native"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

add_executable(lgaed_cpp_tool_host native/platform/windows/cpp_tool_host.cpp)
target_link_libraries(lgaed_cpp_tool_host PRIVATE lgaed_host_options user32)
set_target_properties(lgaed_cpp_tool_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_library(lgaed_core STATIC native/core/profile.cpp native/core/compile_check.cpp native/core/lesson.cpp native/core/lesson_state.cpp native/core/lesson_page_reach.cpp native/core/text_semantics.cpp native/core/cpp_project.cpp native/core/image_resource.cpp
    native/core/catalog.cpp native/core/audio_schedule.cpp native/core/learning_flow.cpp native/core/view_preferences.cpp native/core/narration_state.cpp native/core/scientific_calculator.cpp native/core/smo.cpp native/core/smo_native.cpp)
target_link_libraries(lgaed_core PUBLIC lgaed_host_options)

add_library(lgaed_platform_windows STATIC
    native/platform/windows/environment.cpp
    native/platform/windows/profile_migration.cpp
    native/platform/windows/content_loader.cpp
    native/platform/windows/bibliography_revision.cpp
    native/platform/windows/narration_preferences.cpp
    native/platform/windows/catalog_loader.cpp
    native/platform/windows/audio_generation.cpp
    native/platform/windows/narration_worker.cpp
    native/platform/windows/narration_cpu_policy.cpp
    native/platform/windows/narration_player.cpp
    native/platform/windows/system_speech.cpp
    native/platform/windows/system_speech_process.cpp
    native/platform/windows/state_store.cpp
    native/platform/windows/android_import.cpp
    native/platform/windows/cpp_project_history.cpp
    native/platform/windows/cpp_runner.cpp
    native/platform/windows/compiler_completion.cpp)
target_link_libraries(lgaed_platform_windows PUBLIC lgaed_core lgaed_host_options)
add_dependencies(lgaed_platform_windows lgaed_cpp_tool_host)
target_link_libraries(lgaed_platform_windows PRIVATE shell32 shlwapi sapi ole32 oleaut32 advapi32 windowsapp runtimeobject bcrypt xmllite)

function(lgaed_add_named_rule_source_tests)
    if(TARGET lgaed_named_rule_check_tests)
        return()
    endif()
    add_executable(lgaed_named_rule_check_tests native/core/named_rule_check_tests.cpp)
    target_link_libraries(lgaed_named_rule_check_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.named_rule_source COMMAND lgaed_named_rule_check_tests)
    set_tests_properties(native.named_rule_source PROPERTIES TIMEOUT 30)
endfunction()

if(LGAED_WASI_RUNNER_QA_ONLY)
    include(CTest)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "WASI runner QA requires BUILD_TESTING=ON.")
    endif()
    add_executable(lgaed_cpp_runner_tests native/platform/windows/cpp_runner_tests.cpp)
    target_link_libraries(lgaed_cpp_runner_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.wasi_runner COMMAND lgaed_cpp_runner_tests --wasi)
    message(STATUS "Focused real Windows WASI runner QA: C++23 compilation, four inputs and output rejection; no app or narration.")
    return()
endif()
# The import/player modes still build the complete production core/platform
# libraries and their original test translation units. Neither configures
# presentation, shader assets or the synthesis runtime, which these probes do
# not use. All native toolchain/dependency/C++23 checks remain mandatory.
if(LGAED_STATE_IMPORT_QA_ONLY OR LGAED_NARRATION_PLAYER_QA_ONLY)
    include(CTest)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Focused Windows QA requires BUILD_TESTING=ON.")
    endif()
    if(LGAED_STATE_IMPORT_QA_ONLY)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        include(cmake/StateImportTests.cmake)
        lgaed_add_state_import_tests("${CMAKE_CURRENT_BINARY_DIR}/qa/state-import")
        # Keep the historical import suite focused on its seven self-contained
        # tests. Compiler guidance is exercised by the full native configuration;
        # this isolated profile lacks all resources those fixtures reference.
        message(STATUS "Focused native import QA: historical suites and exact runtime stderr; no application, reader or audio verification.")
    else()
        include(cmake/NarrationPlayerTests.cmake)
        lgaed_add_narration_player_tests("${CMAKE_CURRENT_BINARY_DIR}/qa/narration-player")
        message(STATUS "Focused native narration-player QA: two real Windows playback probes and one failure diagnostic; no synthesis, application or reader verification.")
    endif()
    return()
endif()

if(LGAED_NARRATION_WORKER_QA_ONLY)
    include(CTest)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Focused narration-worker QA requires BUILD_TESTING=ON.")
    endif()
    if(NOT IS_DIRECTORY "${LGAED_NARRATION_RESOURCES}" OR NOT IS_DIRECTORY "${LGAED_ENGLISH_RESOURCES}")
        message(FATAL_ERROR "Focused narration-worker QA requires existing authenticated Kokoro and English resource directories.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include(cmake/NarrationRuntime.cmake)
    lgaed_add_narration_runtime(NarrationHost)
    include(cmake/NarrationWorkerTests.cmake)
    lgaed_add_narration_worker_tests()
    set_tests_properties(native.narration_spanish_pcm PROPERTIES
        ENVIRONMENT "LGAED_REQUIRE_NEW_SPANISH_SYNTHESIS=1")
    include(cmake/NarrationPlayerTests.cmake)
    lgaed_add_narration_player_failure_test()
    message(STATUS "Focused narration-worker QA: native worker, Spanish PCM and real player failure diagnostic; no successful playback or reader verification.")
    return()
endif()

include(cmake/CurricularWindows.cmake)
if(LGAED_U12_READER_QA_ONLY OR LGAED_U11_EDITORIAL_READER_QA_ONLY
        OR LGAED_COMPILER_HISTORICAL_READER_QA_ONLY OR LGAED_U6_DIAGNOSTIC_READER_QA_ONLY)
    include(CTest)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "Focused curricular reader QA requires BUILD_TESTING=ON.")
    endif()
    if(NOT IS_DIRECTORY "${LGAED_NARRATION_RESOURCES}" OR NOT IS_DIRECTORY "${LGAED_ENGLISH_RESOURCES}")
        message(FATAL_ERROR "Focused curricular reader QA requires existing authenticated Kokoro and English resource directories.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include(cmake/NarrationRuntime.cmake)
    lgaed_add_narration_runtime(NarrationHost)
    lgaed_add_curricular_reader_test_targets()
    if(LGAED_U12_READER_QA_ONLY)
        add_dependencies(lgaed_lesson_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
        include(cmake/U12PublishedReaderTests.cmake)
        lgaed_add_u12_published_reader_tests()
        message(STATUS "Focused U12 reader QA: three original published-seventeen reader phases with real Kokoro playback.")
    elseif(LGAED_U11_EDITORIAL_READER_QA_ONLY)
        add_dependencies(lgaed_lesson_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
        include(cmake/StateImportTests.cmake)
        include(cmake/U11EditorialReaderTests.cmake)
        lgaed_add_u11_editorial_reader_tests()
        message(STATUS "Focused U11 editorial reader QA: ten original published assignment/remainder phases with real Kokoro playback.")
    elseif(LGAED_COMPILER_HISTORICAL_READER_QA_ONLY)
        add_dependencies(lgaed_u11_declarations_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
        include(cmake/StateImportTests.cmake)
        include(cmake/CompilerHistoricalReaderTests.cmake)
        lgaed_add_compiler_historical_reader_tests()
        message(STATUS "Focused compiler historical reader QA: fourteen original U11/U12 phases with real compilation and Kokoro playback.")
    else()
        include(cmake/CompilerContractFixtures.cmake)
        include(cmake/U6DiagnosticReaderTests.cmake)
        lgaed_add_u6_diagnostic_reader_tests()
        message(STATUS "Focused U6 diagnostic reader QA: three original flows covering twelve diagnostics with real compilation and Kokoro playback.")
    endif()
    message(STATUS "Focused curricular reader QA requires an active Windows render endpoint; no application, shaders or numerical backends are configured.")
    return()
endif()

if(LGAED_READER_BUILD_QA_ONLY)
    include(CTest)
    if(NOT BUILD_TESTING)
        message(FATAL_ERROR "LGAED_READER_BUILD_QA_ONLY requires BUILD_TESTING=ON.")
    endif()
    lgaed_add_curricular_reader_test_targets()
    # Run the real Win32 compiler view against thirteen authored inputs.
    # Exactly the first must unlock navigation; all others remain optional.
    add_executable(lgaed_compiler_first_case_view_tests
        native/presentation/windows/compiler_first_case_view_tests.cpp)
    target_link_libraries(lgaed_compiler_first_case_view_tests PRIVATE
        lgaed_curricular_windows)
    add_test(NAME native.compiler_puzzles COMMAND lgaed_compiler_first_case_view_tests)
    set_tests_properties(native.compiler_puzzles PROPERTIES TIMEOUT 210 RUN_SERIAL TRUE)
    # Preserve authored code indentation and optional decorative cards without
    # modifying any historical JSON, reference or saved response.
    add_executable(lgaed_code_format_compat_tests native/core/code_format_compat_tests.cpp)
    target_link_libraries(lgaed_code_format_compat_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.code_format_compat COMMAND lgaed_code_format_compat_tests)
    set_tests_properties(native.code_format_compat PROPERTIES TIMEOUT 30)
    lgaed_add_named_rule_source_tests()
    # Read-only CMake File API graph: a duplicated library source and a
    # real target_link_libraries() can print identical stdout but differ in
    # generated target types, source ownership and linker dependencies.
    add_executable(lgaed_cpp_cmake_runner_tests
        native/platform/windows/cpp_cmake_runner_tests.cpp)
    target_link_libraries(lgaed_cpp_cmake_runner_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.cpp_cmake_graph
        COMMAND lgaed_cpp_cmake_runner_tests --graph)
    set_tests_properties(native.cpp_cmake_graph PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    # Exercise the source-evidence prerequisite on four real, historically
    # mapped U3 editors, without changing their authored JSON contracts.
    add_executable(lgaed_named_rule_runtime_tests
        native/platform/windows/named_rule_runtime_tests.cpp)
    target_link_libraries(lgaed_named_rule_runtime_tests PRIVATE lgaed_curricular_windows)
    add_test(NAME native.named_rule_runtime COMMAND lgaed_named_rule_runtime_tests
        "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json")
    set_tests_properties(native.named_rule_runtime PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    # Compile and execute four real U3L6/U3L8 original student editors
    # against native MSVC with independent stdout/stderr and semantic probes.
    # The original JSON remains untouched: the reviewed root declaration is
    # injected only into the isolated QA lesson in memory.
    add_executable(lgaed_u3_runtime_semantics_tests
        native/platform/windows/u3_runtime_semantics_tests.cpp)
    target_link_libraries(lgaed_u3_runtime_semantics_tests PRIVATE lgaed_curricular_windows)
    add_test(NAME native.u3_runtime_semantics COMMAND lgaed_u3_runtime_semantics_tests)
    set_tests_properties(native.u3_runtime_semantics PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    # A personal Windows development build does not require Google Play
    # or a saved entitlement to keep checking exercises with zero lives.
    add_executable(lgaed_premium_windows_tests native/core/premium_windows_tests.cpp)
    target_link_libraries(lgaed_premium_windows_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.premium_windows COMMAND lgaed_premium_windows_tests)
    set_tests_properties(native.premium_windows PROPERTIES TIMEOUT 30)
    add_executable(lgaed_order_spacing_contract_tests native/core/order_spacing_contract_tests.cpp)
    target_link_libraries(lgaed_order_spacing_contract_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.order_spacing_contract COMMAND lgaed_order_spacing_contract_tests)
    set_tests_properties(native.order_spacing_contract PROPERTIES TIMEOUT 30)
    add_executable(lgaed_order_code_whitespace_view_tests
        native/presentation/windows/order_code_view_tests.cpp native/presentation/windows/app.manifest)
    target_link_libraries(lgaed_order_code_whitespace_view_tests PRIVATE lgaed_curricular_windows)
    add_test(NAME native.order_code_whitespace_view
        COMMAND lgaed_order_code_whitespace_view_tests --whitespace)
    set_tests_properties(native.order_code_whitespace_view PROPERTIES TIMEOUT 60)
    # Compile the full RichEdit test translation unit in Windows CI as well.
    # This catches obsolete presentation APIs before a local Release install,
    # without running desktop-color assertions on a noninteractive runner.
    add_executable(lgaed_rich_text_tests native/presentation/windows/rich_text_tests.cpp
        native/presentation/windows/rich_text.cpp native/presentation/windows/reader_theme.cpp)
    target_link_libraries(lgaed_rich_text_tests PRIVATE lgaed_platform_windows lgaed_host_options
        user32 gdi32 uxtheme ole32 oleaut32 comctl32 uuid)
    # The original reader tests are still only compiled. The history UI test
    # exercises the actual Win32 record/compare/restore dialogs, without
    # creating an app profile or evaluating learner completion.
    add_executable(lgaed_cpp_project_view_tests native/presentation/windows/cpp_project_view_tests.cpp)
    target_link_libraries(lgaed_cpp_project_view_tests PRIVATE lgaed_curricular_windows)
    add_test(NAME native.cpp_project_internal_dialogs
        COMMAND lgaed_cpp_project_view_tests "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history" --internal-dialogs)
    set_tests_properties(native.cpp_project_internal_dialogs PROPERTIES TIMEOUT 90)
    # Load the published U3 editor through the production Win32 lesson view.
    # This focused QA uses its own profile; unrelated prerequisite pages are
    # not artificially marked complete or silently granted to the student.
    add_test(NAME native.cpp_project_u3_selective_lesson
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-selective
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_selective_lesson PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # The original full thirteen-page lesson must still block a fresh user
    # from jumping over prior multiple-choice and compiler requirements.
    add_test(NAME native.cpp_project_u3_original_gates
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-gates
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_original_gates PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    # U2L3 has a closed runtime diagnostic: exact stderr and normal exit 2.
    # Inject only a root declaration into an isolated in-memory original.
    add_test(NAME native.cpp_project_u2_exact_eof
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u2-exact-eof
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/construccion_reproducible/lesson_opciones_y_revision_de_la_configuracion.json")
    set_tests_properties(native.cpp_project_u2_exact_eof PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    # The curricular declaration must preserve the original owner's stored keys.
    # The root field is injected only into memory until Work publishes it.
    add_test(NAME native.cpp_project_u3_declaration
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-declaration
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_declaration PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    # Exercise the full native History interaction with the root declaration
    # active in an in-memory curriculum, preserving the original JSON.
    add_test(NAME native.cpp_project_u3_declared_selective
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-selective-declared
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_declared_selective PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # An isolated student must solve actual early U3 questions before
    # reaching the first CMake editor; later U3 pages remain gated.
    add_test(NAME native.cpp_project_u3_original_prerequisites
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-prerequisites
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_original_prerequisites PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    # Original pages 1-5: genuine checked questions and an edited CMake
    # project with two targets executed before any later page is permitted.
    add_test(NAME native.cpp_project_u3_first_cmake
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-first-cmake
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_first_cmake PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # Continue without synthetic progress: the original next ordering, question,
    # failing source and repaired CMake test are all exercised in one reader.
    add_test(NAME native.cpp_project_u3_first_cmake_repair
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-first-cmake-repair
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_first_cmake_repair PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # Verify the real page-seven failure/repair and page-nine nested lotes
    # CMake projects before permitting the published History editor.
    add_test(NAME native.cpp_project_u3_real_history_gate
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-real-history-gate
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_real_history_gate PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # Full original thirteen-page student path, no synthetic completions:
    # the same editor records/compares/restores History, then checks final pages.
    add_test(NAME native.cpp_project_u3_entire_original
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history"
            --u3-entire-original
            "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_reorganizar_la_descripcion_conservando_el_comprobador.json")
    set_tests_properties(native.cpp_project_u3_entire_original PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    # Regression for other C++ activities with multiple independent inputs:
    # correct-but-partial answers must not display U3 History instructions.
    add_test(NAME native.cpp_project_other_cpp_multicase
        COMMAND lgaed_cpp_project_view_tests
            "${CMAKE_CURRENT_BINARY_DIR}/qa/cpp-project-history" --other-cases)
    set_tests_properties(native.cpp_project_other_cpp_multicase PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    message(STATUS "Focused reader build plus one real Win32 history dialog regression; no reader/audio test, fixture, synthesis runtime or application build.")
    return()
endif()

# Author-time shader compiler: deliberately independent of presentation, which
# consumes its outputs. Shader optimization/IEEE flags are identical in every
# host build configuration; Debug does not turn these kernels into /Od shaders.
add_executable(lgaed_smo_shader_compile native/tools/smo_shader_compile.cpp)
target_link_libraries(lgaed_smo_shader_compile PRIVATE lgaed_host_options d3dcompiler bcrypt)
set_target_properties(lgaed_smo_shader_compile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
# Optimized HLSL compilation can be expensive. Use this Windows host's CPU and
# available memory, keeping a 4 GiB budget per compiler and at most eight jobs.
# Small hosts retain a conservative limit; shader flags remain IEEE-strict/O3.
cmake_host_system_information(RESULT _lgaed_threads QUERY NUMBER_OF_LOGICAL_CORES)
cmake_host_system_information(RESULT _lgaed_available_memory QUERY AVAILABLE_PHYSICAL_MEMORY)
math(EXPR _lgaed_cpu_shader_jobs "${_lgaed_threads} / 2")
math(EXPR _lgaed_memory_shader_jobs "${_lgaed_available_memory} / 4096")
set(_lgaed_shader_jobs 8)
foreach(_limit _lgaed_cpu_shader_jobs _lgaed_memory_shader_jobs)
    if(${_limit} LESS _lgaed_shader_jobs)
        set(_lgaed_shader_jobs "${${_limit}}")
    endif()
endforeach()
if(_lgaed_shader_jobs LESS 1)
    set(_lgaed_shader_jobs 1)
endif()
set(LGAED_SHADER_COMPILE_JOBS "${_lgaed_shader_jobs}" CACHE STRING "Concurrent optimized HLSL compiler processes")
if(NOT LGAED_SHADER_COMPILE_JOBS MATCHES "^[1-9][0-9]*$" OR LGAED_SHADER_COMPILE_JOBS GREATER 32)
    message(FATAL_ERROR "LGAED_SHADER_COMPILE_JOBS must be an integer from 1 to 32.")
endif()
set_property(GLOBAL APPEND PROPERTY JOB_POOLS "smo_hlsl_compile=${LGAED_SHADER_COMPILE_JOBS}")
message(STATUS "Optimized HLSL compiler jobs: ${LGAED_SHADER_COMPILE_JOBS}")

# Pure numerical references can be verified without preparing graphics assets
# or creating a GPU device. The application links these same implementations.
add_library(lgaed_smo_arithmetic STATIC
    native/presentation/windows/smo_rule_contract.cpp
    native/presentation/windows/smo_duodecimal.cpp
    native/presentation/windows/smo_float12.cpp
    native/presentation/windows/smo_numeric_scene_reference.cpp)
target_link_libraries(lgaed_smo_arithmetic PUBLIC lgaed_core lgaed_host_options)

add_executable(lgaed_system_speech_host native/platform/windows/system_speech_host_main.cpp)
target_link_libraries(lgaed_system_speech_host PRIVATE lgaed_platform_windows)
set_target_properties(lgaed_system_speech_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_library(lgaed_presentation_windows STATIC
    native/presentation/windows/window_frame.cpp
    native/presentation/windows/catalog_view.cpp
    native/presentation/windows/tools_view.cpp
    native/presentation/windows/graphics_view.cpp
    native/presentation/windows/smo_view.cpp
    native/presentation/windows/smo_benchmark.cpp
    native/presentation/windows/smo_compute_d3d11.cpp
    native/presentation/windows/smo_compute_d3d12.cpp
    native/presentation/windows/smo_compute_vulkan.cpp
    native/presentation/windows/smo_compute_opengl.cpp
    native/presentation/windows/smo_scene.cpp
    native/presentation/windows/smo_scene_reference.cpp
    native/presentation/windows/smo_rule_gpu.cpp
    native/presentation/windows/smo_duodecimal_scene.cpp
    native/presentation/windows/smo_duodecimal_gpu.cpp
    native/presentation/windows/smo_program_gpu.cpp
    native/presentation/windows/smo_native_gpu.cpp
    native/presentation/windows/smo_native_render.cpp
    native/presentation/windows/smo_program_render.cpp
    native/presentation/windows/smo_program_shaders.cpp
    native/presentation/windows/smo_program_session.cpp
    native/presentation/windows/smo_radix_ablation.cpp
    native/presentation/windows/smo_lazy_task.cpp
    native/presentation/windows/smo_subdivision_task.cpp
    native/presentation/windows/audio_course_view.cpp
    native/presentation/windows/narration_licenses.cpp)
target_link_libraries(lgaed_presentation_windows PUBLIC
    lgaed_curricular_windows lgaed_platform_windows lgaed_smo_arithmetic lgaed_host_options user32 gdi32 gdiplus d3d11 d3d12 dxgi d3dcompiler shell32 ole32 oleacc advapi32 comctl32 comdlg32 uuid uxtheme dwmapi)
add_executable(lgaed_smo_compile native/tools/smo_compile.cpp)
target_link_libraries(lgaed_smo_compile PRIVATE lgaed_presentation_windows)
set_target_properties(lgaed_smo_compile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
include(cmake/SmoSceneShaders.cmake)
lgaed_prepare_smo_shaders(lgaed_presentation_windows)
# Integrated laboratory tasks use the same existing compute backends and
# author-time shader verification as the rest of the SMO laboratory.
lgaed_compile_smo_hlsl_family(lgaed_presentation_windows smo_lazy_task SMO_LAZY_MODE lazy_mode 11 LGAED_SMO_LAZY_HLSL_SHADER_ROOT
    SHARED_CS50 smo_lazy_task.inc)
lgaed_compile_smo_hlsl_family(lgaed_presentation_windows smo_subdivision_task SMO_SUBDIVISION_STAGE subdivision_stage 7 LGAED_SMO_SUBDIVISION_HLSL_SHADER_ROOT
    SHARED_CS50 smo_float12.inc smo_subdivision_task.inc)
get_target_property(_lgaed_task_glslc lgaed_presentation_windows LGAED_SMO_GLSLC)
if(_lgaed_task_glslc)
    lgaed_compile_smo_family(lgaed_presentation_windows "${_lgaed_task_glslc}" smo_lazy_task SMO_LAZY_MODE lazy_mode 11 LGAED_SMO_LAZY_SHADER_ROOT
        smo_lazy_task.inc)
    lgaed_compile_smo_family(lgaed_presentation_windows "${_lgaed_task_glslc}" smo_subdivision_task SMO_SUBDIVISION_STAGE subdivision_stage 7 LGAED_SMO_SUBDIVISION_SHADER_ROOT
        smo_float12.inc smo_subdivision_task.inc)
endif()
add_executable(LearnGameAndEngineDev WIN32
    native/presentation/windows/main.cpp
    native/presentation/windows/app.manifest)
target_link_libraries(LearnGameAndEngineDev PRIVATE
    lgaed_presentation_windows)
# The normal native build prepares the shared linguistic runtime too. Its
# source/hash check is idempotent and never synthesizes or replaces recordings.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(cmake/NarrationRuntime.cmake)
lgaed_add_narration_runtime(Application)
add_dependencies(LearnGameAndEngineDev lgaed_narration_runtime)
add_dependencies(LearnGameAndEngineDev lgaed_system_speech_host)
set_target_properties(LearnGameAndEngineDev PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

include(CTest)
option(LGAED_BUILD_SMO_RENDER_REFERENCE_TESTS
    "Build and register the optional cached/uncached SMO render benchmarks by default" OFF)
if(BUILD_TESTING)
    lgaed_add_named_rule_source_tests()
    include(cmake/StateImportTests.cmake)
    lgaed_add_curricular_reader_test_targets()
    add_executable(lgaed_text_semantics_tests native/core/text_semantics_tests.cpp)
    target_link_libraries(lgaed_text_semantics_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.text_semantics COMMAND lgaed_text_semantics_tests)
    add_executable(lgaed_text_semantics_loader_tests native/platform/windows/text_semantics_loader_tests.cpp)
    target_link_libraries(lgaed_text_semantics_loader_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.text_semantics_loader COMMAND lgaed_text_semantics_loader_tests)
    add_executable(lgaed_curricular_image_tests native/presentation/windows/curricular_image_view_tests.cpp)
    target_link_libraries(lgaed_curricular_image_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.curricular_images COMMAND lgaed_curricular_image_tests)
    set_tests_properties(native.curricular_images PROPERTIES TIMEOUT 60)
    add_test(NAME native.curricular_image_contract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_image_resources.py)
    set_tests_properties(native.curricular_image_contract PROPERTIES TIMEOUT 30)
    add_test(NAME native.initial_v2_family_contract COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_initial_v2_family.py)
    set_tests_properties(native.initial_v2_family_contract PROPERTIES TIMEOUT 30)
    add_test(NAME curriculum.bibliography_archive COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_bibliography_archive.py)
    set_tests_properties(curriculum.bibliography_archive PROPERTIES TIMEOUT 30)
    add_test(NAME curriculum.bibliography_revision COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_bibliography_revision.py)
    set_tests_properties(curriculum.bibliography_revision PROPERTIES TIMEOUT 30)
    set(LGAED_BIBLIOGRAPHY_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/bibliography-06d31ba-v2")
    set(LGAED_BIBLIOGRAPHY_FIXTURES "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}/manifest.json"
        "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}/predicate-cases.json")
    foreach(_lesson IN ITEMS caracteres_y_valores_logicos conversiones_y_deduccion enteros_y_aproximaciones)
        list(APPEND LGAED_BIBLIOGRAPHY_FIXTURES
            "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}/before-lesson_${_lesson}.json"
            "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}/after-lesson_${_lesson}.json")
    endforeach()
    add_custom_command(OUTPUT ${LGAED_BIBLIOGRAPHY_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/bibliography_fixture_source.py"
            --output-dir "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/bibliography_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/bibliography_revision.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/test_bibliography_revision.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Authenticating isolated bibliography revision proposals from local Git"
        VERBATIM)
    add_custom_target(lgaed_bibliography_fixtures DEPENDS ${LGAED_BIBLIOGRAPHY_FIXTURES})
    add_executable(lgaed_bibliography_revision_tests native/platform/windows/bibliography_revision_tests.cpp)
    target_link_libraries(lgaed_bibliography_revision_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_bibliography_revision_tests lgaed_bibliography_fixtures)
    add_test(NAME native.bibliography_revision COMMAND lgaed_bibliography_revision_tests "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}")
    set_tests_properties(native.bibliography_revision PROPERTIES TIMEOUT 30)
    add_executable(lgaed_bibliography_view_tests native/presentation/windows/bibliography_view_tests.cpp)
    target_link_libraries(lgaed_bibliography_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_bibliography_view_tests lgaed_bibliography_fixtures)
    add_test(NAME native.bibliography_view COMMAND lgaed_bibliography_view_tests "${LGAED_BIBLIOGRAPHY_FIXTURE_DIR}")
    set_tests_properties(native.bibliography_view PROPERTIES TIMEOUT 60)
    add_executable(lgaed_compiler_feedback_source_tests native/platform/windows/compiler_feedback_source_tests.cpp)
    target_link_libraries(lgaed_compiler_feedback_source_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.compiler_feedback_source COMMAND lgaed_compiler_feedback_source_tests)
    add_executable(lgaed_compiler_guidance_state_tests native/platform/windows/compiler_guidance_state_tests.cpp)
    target_link_libraries(lgaed_compiler_guidance_state_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.compiler_guidance_state COMMAND lgaed_compiler_guidance_state_tests)
    set_tests_properties(native.compiler_guidance_state PROPERTIES TIMEOUT 30)
    add_test(NAME native.compiler_review_guidance_priority COMMAND lgaed_compiler_guidance_state_tests --review-priority)
    set_tests_properties(native.compiler_review_guidance_priority PROPERTIES TIMEOUT 30)
    add_test(NAME native.u12l24_authored_compiler_guides COMMAND lgaed_compiler_guidance_state_tests --authored-l24-guides)
    set_tests_properties(native.u12l24_authored_compiler_guides PROPERTIES TIMEOUT 30)
    add_test(NAME curriculum.compiler_guidance_projection COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_compiler_guidance_projection.py)
    add_executable(lgaed_text_semantic_projection_tests native/presentation/windows/text_semantic_projection_tests.cpp)
    target_link_libraries(lgaed_text_semantic_projection_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.text_semantic_projection COMMAND lgaed_text_semantic_projection_tests)
    add_executable(lgaed_text_semantic_feedback_tests native/presentation/windows/text_semantic_feedback_tests.cpp)
    target_link_libraries(lgaed_text_semantic_feedback_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.text_semantic_feedback COMMAND lgaed_text_semantic_feedback_tests)
    add_executable(lgaed_narration_semantic_state_tests native/core/narration_semantic_state_tests.cpp)
    target_link_libraries(lgaed_narration_semantic_state_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.narration_semantic_state COMMAND lgaed_narration_semantic_state_tests)
    add_executable(lgaed_narration_recording_identity_tests native/presentation/windows/narration_recording_identity_tests.cpp)
    target_link_libraries(lgaed_narration_recording_identity_tests PRIVATE lgaed_host_options)
    add_test(NAME native.narration_recording_identity COMMAND lgaed_narration_recording_identity_tests)
    add_executable(lgaed_positional_reading_archive_tests native/core/positional_reading_archive_tests.cpp)
    target_link_libraries(lgaed_positional_reading_archive_tests PRIVATE lgaed_host_options)
    add_test(NAME native.positional_reading_archive COMMAND lgaed_positional_reading_archive_tests)
    add_executable(lgaed_narration_file_access_tests native/platform/windows/narration_file_access_tests.cpp)
    target_link_libraries(lgaed_narration_file_access_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.narration_file_access COMMAND lgaed_narration_file_access_tests)
    set_tests_properties(native.narration_file_access PROPERTIES TIMEOUT 30)
    add_test(NAME native.text_semantics_plan COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_text_semantics.py)
    add_test(NAME native.character_speech COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_character_speech.py)
    add_test(NAME native.curriculum_visible_semantics COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_curriculum_visible_semantics.py)
    add_test(NAME native.math_feedback_sources COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_math_feedback_sources.py)
    add_test(NAME native.curriculum_region_identity COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_curriculum_region_identity.py)
    add_test(NAME curriculum.text_semantics COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_text_semantics.py)
    # These focused contracts never create windows or access user input/audio.
    add_executable(lgaed_lesson_entry_policy_tests native/core/lesson_entry_policy_tests.cpp)
    target_link_libraries(lgaed_lesson_entry_policy_tests PRIVATE lgaed_host_options)
    add_test(NAME native.lesson_entry_policy COMMAND lgaed_lesson_entry_policy_tests)
    set_tests_properties(native.lesson_entry_policy PROPERTIES TIMEOUT 30)
    add_executable(lgaed_lesson_page_reach_tests native/core/lesson_page_reach_tests.cpp)
    target_link_libraries(lgaed_lesson_page_reach_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.lesson_page_reach COMMAND lgaed_lesson_page_reach_tests)
    set_tests_properties(native.lesson_page_reach PROPERTIES TIMEOUT 30)
    include(cmake/CompilerContractFixtures.cmake)
    add_executable(lgaed_u6_diagnostic_source_tests native/core/u6_diagnostic_source_tests.cpp)
    target_link_libraries(lgaed_u6_diagnostic_source_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_u6_diagnostic_source_tests lgaed_compiler_contract_fixtures)
    add_test(NAME native.u6_diagnostic_sources COMMAND lgaed_u6_diagnostic_source_tests)
    set_tests_properties(native.u6_diagnostic_sources PROPERTIES TIMEOUT 30)
    add_executable(lgaed_u6_diagnostic_loader_tests native/core/u6_diagnostic_loader_tests.cpp)
    target_link_libraries(lgaed_u6_diagnostic_loader_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_u6_diagnostic_loader_tests lgaed_compiler_contract_fixtures)
    add_test(NAME native.u6_diagnostic_loader COMMAND lgaed_u6_diagnostic_loader_tests)
    set_tests_properties(native.u6_diagnostic_loader PROPERTIES TIMEOUT 60)
    add_test(NAME native.u6_diagnostic_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u6_project_diagnostic.py)
    set_tests_properties(native.u6_diagnostic_authoring PROPERTIES TIMEOUT 60)
    add_executable(lgaed_u6_diagnostic_runner_tests native/platform/windows/u6_diagnostic_runner_tests.cpp)
    target_link_libraries(lgaed_u6_diagnostic_runner_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_u6_diagnostic_runner_tests lgaed_cpp_tool_host lgaed_compiler_contract_fixtures)
    foreach(u6_diagnostic IN ITEMS
        u6-difference-unselected-definition u6-difference-conflicting-return u6-difference-missing-header
        u6-envelopes-missing-header u6-envelopes-missing-declaration u6-envelopes-unselected-definitions u6-envelopes-duplicate-definition
        u6-series-unknown-call u6-series-unselected-definitions u6-series-duplicate-definition
        u6-shelf-unselected-definition u6-shelf-conflicting-return)
        add_test(NAME native.${u6_diagnostic} COMMAND lgaed_u6_diagnostic_runner_tests ${u6_diagnostic})
        set_tests_properties(native.${u6_diagnostic} PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    endforeach()
    include(cmake/U6DiagnosticReaderTests.cmake)
    lgaed_add_u6_diagnostic_reader_tests()
    add_executable(lgaed_u12l26_exit_tests native/platform/windows/u12l26_exit_tests.cpp)
    target_link_libraries(lgaed_u12l26_exit_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_u12l26_exit_tests lgaed_cpp_tool_host lgaed_compiler_contract_fixtures)
    add_test(NAME native.u12l26_intentional_exit COMMAND lgaed_u12l26_exit_tests)
    set_tests_properties(native.u12l26_intentional_exit PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.u12l26_exit_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u12l26_intentional_exit.py)
    set_tests_properties(native.u12l26_exit_authoring PROPERTIES TIMEOUT 60)
    add_executable(lgaed_crt_output_variant_tests native/platform/windows/crt_output_variant_tests.cpp)
    target_link_libraries(lgaed_crt_output_variant_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_crt_output_variant_tests lgaed_cpp_tool_host lgaed_compiler_contract_fixtures)
    add_test(NAME native.crt_output_variant COMMAND lgaed_crt_output_variant_tests)
    add_executable(lgaed_crt_feedback_state_tests native/platform/windows/crt_feedback_state_tests.cpp)
    target_link_libraries(lgaed_crt_feedback_state_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_crt_feedback_state_tests lgaed_compiler_contract_fixtures)
    add_test(NAME native.crt_feedback_state COMMAND lgaed_crt_feedback_state_tests)
    add_test(NAME native.crt_output_contracts COMMAND lgaed_crt_output_variant_tests --contracts-only)
    add_executable(lgaed_compiler_diagnostic_review_tests native/platform/windows/compiler_diagnostic_review_tests.cpp)
    target_link_libraries(lgaed_compiler_diagnostic_review_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_compiler_diagnostic_review_tests lgaed_cpp_tool_host)
    add_test(NAME native.compiler_diagnostic_review COMMAND lgaed_compiler_diagnostic_review_tests)
    add_test(NAME native.compiler_diagnostic_review_real COMMAND lgaed_compiler_diagnostic_review_tests --real)
    add_test(NAME native.compiler_diagnostic_rejection_budget COMMAND lgaed_compiler_diagnostic_review_tests --rejection-model)
    add_test(NAME native.compiler_interrupted_review COMMAND lgaed_compiler_diagnostic_review_tests --interrupted)
    add_test(NAME native.compiler_edited_feedback COMMAND lgaed_compiler_diagnostic_review_tests --edited-feedback)
    set_tests_properties(native.compiler_edited_feedback PROPERTIES TIMEOUT 30)
    set_tests_properties(native.compiler_diagnostic_review_real PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    set_tests_properties(native.crt_output_variant PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.crt_output_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_crt_output_variant.py)
    set_tests_properties(native.crt_output_authoring PROPERTIES TIMEOUT 60)
    add_executable(lgaed_reading_gate_policy_tests native/presentation/windows/reading_gate_policy_tests.cpp)
    target_link_libraries(lgaed_reading_gate_policy_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.reading_gate_policy COMMAND lgaed_reading_gate_policy_tests)
    add_executable(lgaed_narration_legacy_policy_tests native/presentation/windows/narration_legacy_policy_tests.cpp)
    target_link_libraries(lgaed_narration_legacy_policy_tests PRIVATE lgaed_core)
    add_test(NAME native.narration_legacy_policy COMMAND lgaed_narration_legacy_policy_tests)
    set_tests_properties(native.narration_legacy_policy PROPERTIES TIMEOUT 30)
    add_executable(lgaed_narration_policy_bridge_tests native/platform/windows/narration_policy_bridge_tests.cpp)
    target_link_libraries(lgaed_narration_policy_bridge_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_narration_policy_bridge_tests lgaed_narration_runtime)
    add_test(NAME native.narration_policy_bridge COMMAND lgaed_narration_policy_bridge_tests)
    set_tests_properties(native.narration_policy_bridge PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME native.narration_prose_inference_bridge COMMAND lgaed_narration_policy_bridge_tests --prose-inference)
    set_tests_properties(native.narration_prose_inference_bridge PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_executable(lgaed_curricular_output_roles_tests native/presentation/windows/curricular_output_roles_tests.cpp)
    target_link_libraries(lgaed_curricular_output_roles_tests PRIVATE lgaed_presentation_windows lgaed_host_options)
    add_test(NAME native.curricular_output_roles COMMAND lgaed_curricular_output_roles_tests)
    add_executable(lgaed_intentional_exit_tests native/platform/windows/intentional_exit_tests.cpp)
    target_link_libraries(lgaed_intentional_exit_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_intentional_exit_tests lgaed_cpp_tool_host)
    add_test(NAME native.intentional_exit_execution COMMAND lgaed_intentional_exit_tests)
    add_test(NAME native.intentional_exit_contract COMMAND lgaed_lesson_tests --intentional-exit-code)
    add_test(NAME native.compiler_recheck_contract COMMAND lgaed_lesson_tests --compiler-recheck)
    add_executable(lgaed_u12_compiler_batch_tests native/platform/windows/u12_compiler_batch_tests.cpp)
    target_link_libraries(lgaed_u12_compiler_batch_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_u12_compiler_batch_tests lgaed_cpp_tool_host)
    add_test(NAME native.u12_compiler_batch COMMAND lgaed_u12_compiler_batch_tests)
    set_tests_properties(native.u12_compiler_batch PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME native.u12_ordinary_guides COMMAND lgaed_u12_compiler_batch_tests --ordinary-guides)
    set_tests_properties(native.u12_ordinary_guides PROPERTIES TIMEOUT 45)
    add_test(NAME native.u12_late_ordinary_guides COMMAND lgaed_u12_compiler_batch_tests --late-ordinary-guides)
    set_tests_properties(native.u12_late_ordinary_guides PROPERTIES TIMEOUT 45)
    add_test(NAME native.u12_final_ordinary_guides COMMAND lgaed_u12_compiler_batch_tests --final-ordinary-guides)
    set_tests_properties(native.u12_final_ordinary_guides PROPERTIES TIMEOUT 45)
    add_test(NAME native.u12_loading_compiler_batch COMMAND lgaed_u12_compiler_batch_tests --loading-state)
    set_tests_properties(native.u12_loading_compiler_batch PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native.u12_final_compiler_batch COMMAND lgaed_u12_compiler_batch_tests --final-compiler-batch)
    set_tests_properties(native.u12_final_compiler_batch PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native.u12_output_speech COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_u12_output_speech.py)
    add_test(NAME native.output_data_policy_versions COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_output_data_policy_versions.py)
    set_tests_properties(native.output_data_policy_versions PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME native.u12_insertion_extraction_math COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_programming_math_bridge.py
        -k ProgrammingInsertionExtractionMathBridgeTest)
    add_test(NAME native.u12_loading_state_math COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_programming_math_bridge.py
        -k ProgrammingLoadingStateMathBridgeTest)
    set_tests_properties(native.u12_loading_state_math PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME native.u12_final_integrator_matching COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_programming_math_bridge.py
        -k ProgrammingFinalIntegratorMatchingBridgeTest)
    set_tests_properties(native.u12_final_integrator_matching PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME native.u12_current_compiler_guide_plans COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_text_semantics.py
        -k test_current_l25_l26_compiler_guides_keep_native_prepared_identity)
    set_tests_properties(native.u12_current_compiler_guide_plans PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    set_tests_properties(native.u12_output_speech native.u12_insertion_extraction_math PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
    add_test(NAME native.intentional_exit_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_output_exit_code_contract.py)
    add_test(NAME native.programming_math_contracts COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_programming_math_bridge.py)
    set_tests_properties(native.intentional_exit_execution PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    set_tests_properties(native.reading_gate_policy native.curricular_output_roles
        native.intentional_exit_contract native.compiler_recheck_contract native.intentional_exit_authoring native.programming_math_contracts
        PROPERTIES TIMEOUT 60)
    add_test(NAME native.narration_english_pack COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/narration" -p test_prepare_english_ai_pack.py)
    add_test(NAME native.narration_distribution_contract COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/narration" -p test_verify_windows_distribution.py)
    add_test(NAME native.narration_distribution COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/scripts/narration/verify_windows_distribution.py"
        --runtime "${CMAKE_SOURCE_DIR}/out/narration/runtime.json")
    add_test(NAME native.first_unit_manifest_export COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/narration" -p test_export_first_unit_windows.py)
    add_test(NAME native.first_unit_dora_pack COMMAND "${Python3_EXECUTABLE}" -m unittest
        test_worker.HistoricalDoraPackTest)
    set_tests_properties(native.first_unit_dora_pack PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/native/narration/worker" TIMEOUT 180)
    set_tests_properties(native.narration_english_pack native.narration_distribution_contract
        native.narration_distribution native.first_unit_manifest_export PROPERTIES TIMEOUT 60)
    add_executable(lgaed_technical_context_view_tests native/presentation/windows/technical_context_view_tests.cpp)
    target_link_libraries(lgaed_technical_context_view_tests PRIVATE lgaed_presentation_windows)
    add_executable(lgaed_remainder_context_view_tests native/presentation/windows/remainder_context_view_tests.cpp)
    target_link_libraries(lgaed_remainder_context_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.remainder_technical_context COMMAND lgaed_remainder_context_view_tests)
    set_tests_properties(native.remainder_technical_context PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_dependencies(lgaed_technical_context_view_tests lgaed_work_exact_editorial_fixtures)
    add_test(NAME native.technical_context_view COMMAND lgaed_technical_context_view_tests)
    set_tests_properties(native.technical_context_view PROPERTIES TIMEOUT 120)
    add_test(NAME native.console_string_context_view COMMAND lgaed_technical_context_view_tests --console-string-references)
    set_tests_properties(native.console_string_context_view PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.duplication_code_mention COMMAND lgaed_technical_context_view_tests --duplication-revision
        "${LGAED_WORK_EXACT_EDITORIAL_DIR}/duplication-before.json" "${LGAED_WORK_EXACT_EDITORIAL_DIR}/duplication-after.json")
    add_test(NAME native.remainder_exception_editorial COMMAND lgaed_technical_context_view_tests --remainder-revision
        "${LGAED_WORK_EXACT_EDITORIAL_DIR}/remainder-before.json" "${LGAED_WORK_EXACT_EDITORIAL_DIR}/remainder-after.json")
    set_tests_properties(native.duplication_code_mention native.remainder_exception_editorial PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.u11_remainder_editorial_fixtures COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u11_remainder_editorial_fixtures.py)
    set_tests_properties(native.u11_remainder_editorial_fixtures PROPERTIES TIMEOUT 60)
    set(LGAED_U11_ASSIGNMENT_EDITORIAL_DIR "${CMAKE_CURRENT_BINARY_DIR}/u11-assignment-editorials")
    set(LGAED_U11_ASSIGNMENT_EDITORIAL_FIXTURES
        "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-before.json"
        "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-after.json"
        "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/manifest.json")
    add_custom_command(OUTPUT ${LGAED_U11_ASSIGNMENT_EDITORIAL_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_assignment_editorial_fixture_source.py"
            --output-dir "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_assignment_editorial_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying immutable U11L20 editorial fixtures from local Git publications"
        VERBATIM)
    add_custom_target(lgaed_u11_assignment_editorial_fixtures DEPENDS ${LGAED_U11_ASSIGNMENT_EDITORIAL_FIXTURES})
    add_test(NAME native.u11_assignment_import_fixtures COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u11_assignment_import_fixtures.py)
    set_tests_properties(native.u11_assignment_import_fixtures PROPERTIES TIMEOUT 60)
    add_executable(lgaed_u11_assignment_editorial_view_tests native/presentation/windows/u11_assignment_editorial_view_tests.cpp)
    target_link_libraries(lgaed_u11_assignment_editorial_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_u11_assignment_editorial_view_tests lgaed_u11_assignment_editorial_fixtures)
    add_test(NAME native.u11_assignment_editorial_view COMMAND lgaed_u11_assignment_editorial_view_tests
        "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-before.json" "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-after.json")
    set_tests_properties(native.u11_assignment_editorial_view PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME native.u11_assignment_editorial_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u11_assignment_editorials.py)
    set_tests_properties(native.u11_assignment_editorial_authoring PROPERTIES TIMEOUT 120)
    set(LGAED_U11_SESSION_EDITORIAL_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u11-session-editorial")
    set(LGAED_U11_SESSION_EDITORIAL_FIXTURES
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-before.json"
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-after.json"
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/manifest.json")
    add_custom_command(OUTPUT ${LGAED_U11_SESSION_EDITORIAL_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_session_editorial_fixture_source.py"
            --output-dir "${LGAED_U11_SESSION_EDITORIAL_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_session_editorial_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the immutable U11L22 literal-spacing editorial pair from local Git"
        VERBATIM)
    set(LGAED_U11_SESSION_CURRENT_EDITORIAL_FIXTURES
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-before.json"
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-after.json"
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/manifest-current.json")
    add_custom_command(OUTPUT ${LGAED_U11_SESSION_CURRENT_EDITORIAL_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_session_editorial_fixture_source.py"
            --output-dir "${LGAED_U11_SESSION_EDITORIAL_DIR}" --current
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u11_session_editorial_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the current U11L22 literal-spacing pair without replacing historical fixtures"
        VERBATIM)
    add_custom_target(lgaed_u11_session_editorial_fixtures DEPENDS
        ${LGAED_U11_SESSION_EDITORIAL_FIXTURES} ${LGAED_U11_SESSION_CURRENT_EDITORIAL_FIXTURES})
    add_dependencies(lgaed_technical_context_view_tests lgaed_u11_session_editorial_fixtures)
    add_test(NAME native.u11_session_literal_view COMMAND lgaed_technical_context_view_tests --literal-spacing-revision
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-before.json" "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-after.json")
    set_tests_properties(native.u11_session_literal_view PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.u11_session_literal_current_view COMMAND lgaed_technical_context_view_tests --literal-spacing-revision
        "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-before.json" "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-after.json")
    set_tests_properties(native.u11_session_literal_current_view PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.u11_session_literal_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u11_session_editorials.py)
    set_tests_properties(native.u11_session_literal_authoring PROPERTIES TIMEOUT 120)
    set(LGAED_U12_APPEND_HYPOTHESIS_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u12-append-hypothesis")
    set(LGAED_U12_APPEND_HYPOTHESIS_FIXTURES
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/before.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/after.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-before.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-after.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-before.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-after.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-before.json"
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-after.json")
    add_custom_command(OUTPUT ${LGAED_U12_APPEND_HYPOTHESIS_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_append_hypothesis_fixture_source.py"
            --output-dir "${LGAED_U12_APPEND_HYPOTHESIS_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_append_hypothesis_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying historical and integrated U12L3 question fixtures without editing curriculum"
        VERBATIM)
    add_custom_target(lgaed_u12_append_hypothesis_fixtures DEPENDS ${LGAED_U12_APPEND_HYPOTHESIS_FIXTURES})
    add_test(NAME native.u12_append_hypothesis_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u12_append_hypothesis.py)
    set_tests_properties(native.u12_append_hypothesis_authoring PROPERTIES TIMEOUT 90)
    set(LGAED_U12_COMPOSED_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u12-composed-output")
    set(LGAED_U12_COMPOSED_OUTPUT_FIXTURES
        "${LGAED_U12_COMPOSED_OUTPUT_DIR}/before.json"
        "${LGAED_U12_COMPOSED_OUTPUT_DIR}/after.json"
        "${LGAED_U12_COMPOSED_OUTPUT_DIR}/manifest.json")
    add_custom_command(OUTPUT ${LGAED_U12_COMPOSED_OUTPUT_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            --output-dir "${LGAED_U12_COMPOSED_OUTPUT_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_feedback_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the published U12L4 positional lesson and its first stable-layout adoption"
        VERBATIM)
    add_custom_target(lgaed_u12_composed_output_fixtures DEPENDS ${LGAED_U12_COMPOSED_OUTPUT_FIXTURES})
    set(LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u12-composed-output-current")
    set(LGAED_U12_COMPOSED_OUTPUT_CURRENT_FIXTURES
        "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/before.json"
        "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/after.json"
        "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/manifest.json")
    add_custom_command(OUTPUT ${LGAED_U12_COMPOSED_OUTPUT_CURRENT_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            --current-output-dir "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_output_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_feedback_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/text_semantics.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the original positional U12L4 lesson against its complete fifteen-revision publication"
        VERBATIM)
    add_custom_target(lgaed_u12_composed_output_current_fixtures DEPENDS ${LGAED_U12_COMPOSED_OUTPUT_CURRENT_FIXTURES})
    add_test(NAME native.u12_composed_output_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u12_composed_output_fixtures.py)
    set_tests_properties(native.u12_composed_output_authoring PROPERTIES TIMEOUT 90)
    set(LGAED_U12_COMPOSED_FEEDBACK_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u12-composed-feedback")
    set(LGAED_U12_COMPOSED_FEEDBACK_FIXTURES
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/before.json"
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/after.json"
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/manifest.json")
    add_custom_command(OUTPUT ${LGAED_U12_COMPOSED_FEEDBACK_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_feedback_fixture_source.py"
            --output-dir "${LGAED_U12_COMPOSED_FEEDBACK_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/u12_composed_feedback_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/validar_json_programacion.py"
        COMMENT "Verifying the closed U12L4 feedback and hypothesis proposal without editing active curriculum"
        VERBATIM)
    add_custom_target(lgaed_u12_composed_feedback_fixtures DEPENDS ${LGAED_U12_COMPOSED_FEEDBACK_FIXTURES})
    add_test(NAME native.u12_composed_feedback_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_u12_composed_feedback.py)
    set_tests_properties(native.u12_composed_feedback_authoring PROPERTIES TIMEOUT 90)
    add_executable(lgaed_window_frame_tests native/presentation/windows/window_frame_tests.cpp)
    target_link_libraries(lgaed_window_frame_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.window_frame COMMAND lgaed_window_frame_tests)
    set_tests_properties(native.window_frame PROPERTIES TIMEOUT 20)
    add_executable(lgaed_window_fullscreen_tests native/presentation/windows/window_fullscreen_tests.cpp
        native/presentation/windows/window_fullscreen.cpp)
    target_include_directories(lgaed_window_fullscreen_tests PRIVATE native)
    target_link_libraries(lgaed_window_fullscreen_tests PRIVATE lgaed_host_options user32 comctl32)
    add_test(NAME native.window_fullscreen COMMAND lgaed_window_fullscreen_tests)
    set_tests_properties(native.window_fullscreen PROPERTIES TIMEOUT 20)
    add_executable(lgaed_exact_prediction_view_tests native/presentation/windows/exact_prediction_view_tests.cpp)
    target_link_libraries(lgaed_exact_prediction_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.exact_prediction_view COMMAND lgaed_exact_prediction_view_tests)
    set_tests_properties(native.exact_prediction_view PROPERTIES TIMEOUT 90)
    add_test(NAME native.u11_precision_prediction COMMAND lgaed_exact_prediction_view_tests --u11-precision)
    set_tests_properties(native.u11_precision_prediction PROPERTIES TIMEOUT 60)
    add_test(NAME native.prepared_input_predictions COMMAND lgaed_exact_prediction_view_tests --prepared-input)
    set_tests_properties(native.prepared_input_predictions PROPERTIES TIMEOUT 90)
    add_executable(lgaed_u11_names_integration_view_tests native/presentation/windows/u11_names_integration_view_tests.cpp)
    target_link_libraries(lgaed_u11_names_integration_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_u11_names_integration_view_tests lgaed_cpp_tool_host)
    add_test(NAME native.u11_names_integration_view COMMAND lgaed_u11_names_integration_view_tests)
    set_tests_properties(native.u11_names_integration_view PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_executable(lgaed_u11_names_reader_tests native/presentation/windows/u11_names_reader_tests.cpp)
    target_link_libraries(lgaed_u11_names_reader_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_u11_names_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_test(NAME native.u11_names_reader COMMAND lgaed_u11_names_reader_tests)
    set_tests_properties(native.u11_names_reader PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_dependencies(lgaed_u11_declarations_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_test(NAME native.u11_declarations_reader COMMAND lgaed_u11_declarations_reader_tests)
    set_tests_properties(native.u11_declarations_reader PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native.u11_assignment_compiler_reader COMMAND lgaed_u11_declarations_reader_tests --assignments)
    set_tests_properties(native.u11_assignment_compiler_reader PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_test(NAME native.u11_compiler_reader_entry_setup COMMAND lgaed_u11_declarations_reader_tests --entry-setup)
    set_tests_properties(native.u11_compiler_reader_entry_setup PROPERTIES TIMEOUT 30)
    add_test(NAME native.u11_constants_compiler_reader COMMAND lgaed_u11_declarations_reader_tests --constants)
    set_tests_properties(native.u11_constants_compiler_reader PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    add_executable(lgaed_legacy_response_view_tests native/presentation/windows/legacy_response_view_tests.cpp)
    target_link_libraries(lgaed_legacy_response_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.legacy_response_view COMMAND lgaed_legacy_response_view_tests)
    set_tests_properties(native.legacy_response_view PROPERTIES TIMEOUT 60)
    add_executable(lgaed_curricular_emphasis_view_tests native/presentation/windows/curricular_emphasis_view_tests.cpp)
    target_link_libraries(lgaed_curricular_emphasis_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.curricular_emphasis COMMAND lgaed_curricular_emphasis_view_tests)
    set_tests_properties(native.curricular_emphasis PROPERTIES TIMEOUT 60)
    add_executable(lgaed_editorial_matching_view_tests native/presentation/windows/editorial_matching_view_tests.cpp)
    target_link_libraries(lgaed_editorial_matching_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.editorial_matching_view COMMAND lgaed_editorial_matching_view_tests)
    set_tests_properties(native.editorial_matching_view PROPERTIES TIMEOUT 120)
    add_test(NAME native.u11_domain_matching_revision COMMAND lgaed_editorial_matching_view_tests --u11-domain)
    set_tests_properties(native.u11_domain_matching_revision PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME native.editorial_matching_fixture_source COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_editorial_matching_fixture_source.py)
    set_tests_properties(native.editorial_matching_fixture_source PROPERTIES TIMEOUT 30)
    set(LGAED_EDITORIAL_MATCHING_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u9-cbd9a38")
    set(LGAED_EDITORIAL_MATCHING_FIXTURE "${LGAED_EDITORIAL_MATCHING_FIXTURE_DIR}/lesson_construir_y_comprobar_un_modelo_de_pedidos.json")
    add_custom_command(OUTPUT "${LGAED_EDITORIAL_MATCHING_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/curriculum/editorial_matching_fixture_source.py"
            --output-dir "${LGAED_EDITORIAL_MATCHING_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/editorial_matching_fixture_source.py"
        COMMENT "Verifying immutable U9 editorial matching fixture from local cbd9a38"
        VERBATIM)
    add_custom_target(lgaed_editorial_matching_fixture DEPENDS "${LGAED_EDITORIAL_MATCHING_FIXTURE}")
    add_dependencies(lgaed_editorial_matching_view_tests lgaed_editorial_matching_fixture lgaed_narration_runtime)
    add_executable(lgaed_curricular_order_view_tests native/presentation/windows/curricular_order_view_tests.cpp)
    target_link_libraries(lgaed_curricular_order_view_tests PRIVATE lgaed_presentation_windows)
    foreach(order_case RANGE 0 13)
        add_test(NAME native.curricular_order_view.${order_case}
            COMMAND lgaed_curricular_order_view_tests --case ${order_case})
        set_tests_properties(native.curricular_order_view.${order_case}
            PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
    set(LGAED_RETIREMENT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u1-d1933ed")
    set(LGAED_RETIREMENT_FIXTURES
        "${LGAED_RETIREMENT_FIXTURE_DIR}/lesson_primer_programa_y_salida.json"
        "${LGAED_RETIREMENT_FIXTURE_DIR}/lesson_texto_secuencia_y_formato.json"
        "${LGAED_RETIREMENT_FIXTURE_DIR}/lesson_fuente_compilacion_y_diagnostico.json")
    add_custom_command(OUTPUT ${LGAED_RETIREMENT_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/curriculum/retirement_fixture_source.py"
            --output-dir "${LGAED_RETIREMENT_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/retirement_fixture_source.py"
        COMMENT "Verifying immutable pre-retirement U1 test fixtures from local d1933ed"
        VERBATIM)
    add_custom_target(lgaed_retirement_fixtures DEPENDS ${LGAED_RETIREMENT_FIXTURES})
    add_test(NAME native.diagnostic_fixture_source COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_diagnostic_fixture_source.py)
    set_tests_properties(native.diagnostic_fixture_source PROPERTIES TIMEOUT 30)
    set(LGAED_DIAGNOSTIC_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/diagnostics-7862f10")
    set(LGAED_DIAGNOSTIC_FIXTURES
        "${LGAED_DIAGNOSTIC_FIXTURE_DIR}/lesson_consultas_const_y_objetos_constantes.json"
        "${LGAED_DIAGNOSTIC_FIXTURE_DIR}/lesson_valores_por_defecto_con_significado.json"
        "${LGAED_DIAGNOSTIC_FIXTURE_DIR}/lesson_argumentos_con_tipos_que_expresan_su_funcion.json")
    add_custom_command(OUTPUT ${LGAED_DIAGNOSTIC_FIXTURES}
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/diagnostic_fixture_source.py"
            --output-dir "${LGAED_DIAGNOSTIC_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/diagnostic_fixture_source.py"
        COMMENT "Verifying immutable pre-diagnostic fixtures from local 7862f10"
        VERBATIM)
    add_custom_target(lgaed_diagnostic_fixtures DEPENDS ${LGAED_DIAGNOSTIC_FIXTURES})
    set(LGAED_EDITORIAL_RETURN_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/editorial-return-1fe0975")
    set(LGAED_EDITORIAL_RETURN_FIXTURE "${LGAED_EDITORIAL_RETURN_FIXTURE_DIR}/lesson_estado_asignacion_y_copia.json")
    set(LGAED_EDITORIAL_RETURN_INTERMEDIATE_FIXTURE "${LGAED_EDITORIAL_RETURN_FIXTURE_DIR}/lesson_estado_asignacion_y_copia-df2f9fa.json")
    add_custom_command(OUTPUT "${LGAED_EDITORIAL_RETURN_FIXTURE}" "${LGAED_EDITORIAL_RETURN_INTERMEDIATE_FIXTURE}"
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_SOURCE_DIR}/scripts/curriculum/editorial_return_fixture_source.py"
            --output-dir "${LGAED_EDITORIAL_RETURN_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/editorial_return_fixture_source.py"
        COMMENT "Verifying immutable U2L3 editorial return fixtures from local 1fe0975 and df2f9fa"
        VERBATIM)
    add_custom_target(lgaed_editorial_return_fixture DEPENDS "${LGAED_EDITORIAL_RETURN_FIXTURE}" "${LGAED_EDITORIAL_RETURN_INTERMEDIATE_FIXTURE}")
    add_test(NAME native.editorial_return_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_editorial_return.py)
    set_tests_properties(native.editorial_return_authoring PROPERTIES TIMEOUT 120)
    add_test(NAME native.editorial_return_fixture_source COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_editorial_return_fixture_source.py)
    set_tests_properties(native.editorial_return_fixture_source PROPERTIES TIMEOUT 60)
    add_test(NAME native.work_exact_editorials COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_work_exact_editorials.py)
    set_tests_properties(native.work_exact_editorials PROPERTIES TIMEOUT 120)
    add_test(NAME native.ordinary_explanation_authoring COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_ordinary_explanation_revision.py)
    set_tests_properties(native.ordinary_explanation_authoring PROPERTIES TIMEOUT 120)
    add_executable(lgaed_editorial_return_tests native/core/editorial_return_tests.cpp)
    target_link_libraries(lgaed_editorial_return_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_editorial_return_tests lgaed_editorial_return_fixture)
    add_test(NAME native.editorial_return COMMAND lgaed_editorial_return_tests)
    set_tests_properties(native.editorial_return PROPERTIES TIMEOUT 120)
    add_executable(lgaed_editorial_return_view_tests native/presentation/windows/editorial_return_view_tests.cpp)
    target_link_libraries(lgaed_editorial_return_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_editorial_return_view_tests lgaed_editorial_return_fixture lgaed_narration_runtime)
    add_test(NAME native.editorial_return_view COMMAND lgaed_editorial_return_view_tests)
    set_tests_properties(native.editorial_return_view PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    set(LGAED_ORDER_LINE_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/order-lines")
    add_custom_command(OUTPUT "${LGAED_ORDER_LINE_FIXTURE_DIR}/manifest.json"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/curriculum/order_line_fixture_source.py"
            --output-dir "${LGAED_ORDER_LINE_FIXTURE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/curriculum/order_line_fixture_source.py"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        COMMENT "Verifying immutable order-line migration fixtures from local aa230c"
        VERBATIM)
    add_custom_target(lgaed_order_line_fixtures DEPENDS "${LGAED_ORDER_LINE_FIXTURE_DIR}/manifest.json")
    add_executable(lgaed_smo_lazy_task_tests native/presentation/windows/smo_lazy_task_tests.cpp)
    target_link_libraries(lgaed_smo_lazy_task_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_lazy_task COMMAND lgaed_smo_lazy_task_tests)
    add_executable(lgaed_smo_subdivision_task_tests native/presentation/windows/smo_subdivision_task_tests.cpp)
    target_link_libraries(lgaed_smo_subdivision_task_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_subdivision_task COMMAND lgaed_smo_subdivision_task_tests)
    set_tests_properties(native.smo_lazy_task native.smo_subdivision_task PROPERTIES TIMEOUT 90)
    add_executable(lgaed_smo_task_view_tests native/presentation/windows/smo_task_view_tests.cpp)
    target_link_libraries(lgaed_smo_task_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_integrated_task_view COMMAND lgaed_smo_task_view_tests)
    set_tests_properties(native.smo_integrated_task_view PROPERTIES TIMEOUT 60)
    add_custom_target(lgaed_smo_integrated_tasks DEPENDS lgaed_smo_lazy_task_tests lgaed_smo_subdivision_task_tests lgaed_smo_task_view_tests lgaed_smo_view_tests)
    add_executable(lgaed_audio_schedule_tests native/core/audio_schedule_tests.cpp)
    target_link_libraries(lgaed_audio_schedule_tests PRIVATE lgaed_core)
    add_test(NAME native.audio_schedule COMMAND lgaed_audio_schedule_tests)
    add_test(NAME native.background_narration_plan COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_background_plan.py")
    set_tests_properties(native.background_narration_plan PROPERTIES TIMEOUT 240)
    add_executable(lgaed_narration_cpu_policy_tests native/platform/windows/narration_cpu_policy_tests.cpp)
    target_link_libraries(lgaed_narration_cpu_policy_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.narration_cpu_policy COMMAND lgaed_narration_cpu_policy_tests)
    set_tests_properties(native.narration_cpu_policy PROPERTIES TIMEOUT 30)
    add_executable(lgaed_audio_generation_tests native/platform/windows/audio_generation_tests.cpp)
    target_link_libraries(lgaed_audio_generation_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_audio_generation_tests lgaed_narration_runtime lgaed_system_speech_host)
    add_test(NAME native.audio_generation COMMAND lgaed_audio_generation_tests "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_executable(lgaed_system_speech_background_tests native/platform/windows/system_speech_background_tests.cpp)
    target_link_libraries(lgaed_system_speech_background_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_system_speech_background_tests lgaed_system_speech_host)
    add_test(NAME native.system_speech_background COMMAND lgaed_system_speech_background_tests)
    set_tests_properties(native.system_speech_background PROPERTIES TIMEOUT 90)
    add_executable(lgaed_audio_generation_real_tests native/platform/windows/audio_generation_real_tests.cpp)
    target_link_libraries(lgaed_audio_generation_real_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_audio_generation_real_tests lgaed_system_speech_host lgaed_narration_runtime)
    add_test(NAME native.audio_generation_real COMMAND lgaed_audio_generation_real_tests)
    set_tests_properties(native.audio_generation_real PROPERTIES TIMEOUT 240)
    set_tests_properties(native.audio_generation PROPERTIES TIMEOUT 90)
    add_executable(lgaed_narration_shutdown_tests native/platform/windows/narration_shutdown_tests.cpp)
    target_link_libraries(lgaed_narration_shutdown_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.narration_shutdown COMMAND lgaed_narration_shutdown_tests "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.narration_shutdown PROPERTIES TIMEOUT 20)
    add_executable(lgaed_narration_preferences_tests native/platform/windows/narration_preferences_tests.cpp)
    target_link_libraries(lgaed_narration_preferences_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_narration_preferences_tests lgaed_narration_runtime)
    add_test(NAME native.narration_preferences COMMAND lgaed_narration_preferences_tests)
    add_executable(lgaed_scrollbar_tests native/presentation/windows/scrollbar_tests.cpp)
    target_link_libraries(lgaed_scrollbar_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.scrollbars COMMAND lgaed_scrollbar_tests)
    add_test(NAME native.custom_viewport_scrollbar_policy COMMAND lgaed_scrollbar_tests --custom-viewport-policy)
    set_tests_properties(native.custom_viewport_scrollbar_policy PROPERTIES TIMEOUT 30)
    # Includes visible CodeEditor formatting frames and real H/V thumb tracking.
    set_tests_properties(native.scrollbars PROPERTIES TIMEOUT 120)
    add_executable(lgaed_native_menu_tests native/presentation/windows/native_menu_tests.cpp)
    target_link_libraries(lgaed_native_menu_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.native_menu COMMAND lgaed_native_menu_tests)
    set_tests_properties(native.native_menu PROPERTIES TIMEOUT 30)
    add_executable(lgaed_view_preferences_tests native/core/view_preferences_tests.cpp)
    target_link_libraries(lgaed_view_preferences_tests PRIVATE lgaed_core)
    add_test(NAME native.view_preferences COMMAND lgaed_view_preferences_tests)
    add_executable(lgaed_attempt_ledger_tests native/core/attempt_ledger_tests.cpp)
    target_link_libraries(lgaed_attempt_ledger_tests PRIVATE lgaed_core)
    add_test(NAME native.attempt_ledger COMMAND lgaed_attempt_ledger_tests)
    add_executable(lgaed_learning_flow_tests native/core/learning_flow_tests.cpp)
    target_link_libraries(lgaed_learning_flow_tests PRIVATE lgaed_core)
    add_test(NAME native.learning_flow COMMAND lgaed_learning_flow_tests)
    add_executable(lgaed_attempt_indicator_tests native/presentation/windows/attempt_indicator_tests.cpp
        native/presentation/windows/icons.cpp)
    target_link_libraries(lgaed_attempt_indicator_tests PRIVATE lgaed_host_options user32 gdi32 gdiplus)
    add_test(NAME native.attempt_indicator COMMAND lgaed_attempt_indicator_tests)
    set_tests_properties(native.attempt_indicator PROPERTIES TIMEOUT 30)
    add_executable(lgaed_card_list_tests native/presentation/windows/card_list_tests.cpp)
    target_link_libraries(lgaed_card_list_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.card_list COMMAND lgaed_card_list_tests)
    set_tests_properties(native.card_list PROPERTIES TIMEOUT 30)
    add_executable(lgaed_reader_shortcut_focus_tests native/presentation/windows/reader_shortcut_focus_tests.cpp)
    target_link_libraries(lgaed_reader_shortcut_focus_tests PRIVATE lgaed_host_options user32 comctl32)
    add_test(NAME native.reader_shortcut_focus COMMAND lgaed_reader_shortcut_focus_tests)
    set_tests_properties(native.reader_shortcut_focus PROPERTIES TIMEOUT 30)
    add_executable(lgaed_code_editor_viewport_tests native/presentation/windows/code_editor_viewport_tests.cpp)
    target_link_libraries(lgaed_code_editor_viewport_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.code_editor_viewport COMMAND lgaed_code_editor_viewport_tests)
    set_tests_properties(native.code_editor_viewport PROPERTIES TIMEOUT 120)
    add_executable(lgaed_code_gutter_tests native/presentation/windows/code_gutter_tests.cpp)
    target_link_libraries(lgaed_code_gutter_tests PRIVATE lgaed_presentation_windows dwmapi)
    add_test(NAME native.code_gutter COMMAND lgaed_code_gutter_tests)
    set_tests_properties(native.code_gutter PROPERTIES TIMEOUT 30)
    add_executable(lgaed_code_gutter_lifecycle_tests native/presentation/windows/code_gutter_lifecycle_tests.cpp)
    target_link_libraries(lgaed_code_gutter_lifecycle_tests PRIVATE lgaed_presentation_windows dwmapi)
    add_test(NAME native.code_gutter_lifecycle COMMAND lgaed_code_gutter_lifecycle_tests)
    set_tests_properties(native.code_gutter_lifecycle PROPERTIES TIMEOUT 30)
    add_executable(lgaed_audio_course_view_tests native/presentation/windows/audio_course_view_tests.cpp)
    target_link_libraries(lgaed_audio_course_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.audio_course_view COMMAND lgaed_audio_course_view_tests "${CMAKE_SOURCE_DIR}/out/qa")
    set_tests_properties(native.audio_course_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_settings_view_tests native/presentation/windows/settings_view_tests.cpp)
    target_link_libraries(lgaed_settings_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_settings_view_tests lgaed_narration_runtime)
    add_test(NAME native.settings_view COMMAND lgaed_settings_view_tests --case controls)
    add_test(NAME native.settings_profile_location COMMAND lgaed_settings_view_tests --case profile-location)
    add_test(NAME native.settings_scrollbar_paint COMMAND lgaed_settings_view_tests --case scrollbar-paint)
    set_tests_properties(native.settings_scrollbar_paint PROPERTIES TIMEOUT 40 RUN_SERIAL TRUE)
    set_tests_properties(native.settings_profile_location PROPERTIES TIMEOUT 45)
    add_test(NAME native.settings_save_failure COMMAND lgaed_settings_view_tests --case save-failure)
    set_tests_properties(native.settings_save_failure PROPERTIES TIMEOUT 30)
    add_test(NAME native.narration_licenses COMMAND lgaed_settings_view_tests --case licenses)
    set_tests_properties(native.settings_view native.narration_licenses PROPERTIES TIMEOUT 40)
    add_executable(lgaed_internal_window_tests native/presentation/windows/internal_window_tests.cpp)
    target_link_libraries(lgaed_internal_window_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.internal_navigation COMMAND lgaed_internal_window_tests)
    set_tests_properties(native.internal_navigation PROPERTIES TIMEOUT 40)
    add_executable(lgaed_lesson_composition_tests native/presentation/windows/lesson_composition_tests.cpp)
    target_link_libraries(lgaed_lesson_composition_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_lesson_composition_tests lgaed_narration_runtime)
    add_test(NAME native.lesson_composition COMMAND lgaed_lesson_composition_tests)
    set_tests_properties(native.lesson_composition PROPERTIES TIMEOUT 120)
    add_executable(lgaed_lesson_editor_mount_tests native/presentation/windows/lesson_editor_mount_tests.cpp)
    target_link_libraries(lgaed_lesson_editor_mount_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.lesson_editor_mount COMMAND lgaed_lesson_editor_mount_tests)
    set_tests_properties(native.lesson_editor_mount PROPERTIES TIMEOUT 180)
    add_test(NAME native.lesson_caption_initial COMMAND lgaed_lesson_editor_mount_tests --caption-first)
    set_tests_properties(native.lesson_caption_initial PROPERTIES TIMEOUT 45)
    add_test(NAME native.prediction_newline_visible COMMAND lgaed_lesson_editor_mount_tests --prediction-newline)
    set_tests_properties(native.prediction_newline_visible PROPERTIES TIMEOUT 45)
    add_executable(lgaed_curricular_input_lab_tests native/presentation/windows/curricular_input_lab_tests.cpp)
    target_link_libraries(lgaed_curricular_input_lab_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.curricular_input_lab COMMAND lgaed_curricular_input_lab_tests)
    set_tests_properties(native.curricular_input_lab PROPERTIES TIMEOUT 180)
    add_test(NAME native.curricular_stdout_transport COMMAND lgaed_curricular_input_lab_tests --u3-stdout)
    set_tests_properties(native.curricular_stdout_transport PROPERTIES TIMEOUT 180)
    add_test(NAME native.curricular_u3_combinations COMMAND lgaed_curricular_input_lab_tests --u3-combinations)
    set_tests_properties(native.curricular_u3_combinations PROPERTIES TIMEOUT 240)
    add_executable(lgaed_choice_revision_view_tests native/presentation/windows/choice_revision_view_tests.cpp)
    target_link_libraries(lgaed_choice_revision_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.choice_revision_view COMMAND lgaed_choice_revision_view_tests)
    set_tests_properties(native.choice_revision_view PROPERTIES TIMEOUT 180)
    add_test(NAME native.u11_consult_choice_revision_view COMMAND lgaed_choice_revision_view_tests --u11-consult)
    set_tests_properties(native.u11_consult_choice_revision_view PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    add_test(NAME native.general_choice_editorial_authoring
        COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
            -s "${CMAKE_SOURCE_DIR}/scripts/curriculum"
            -p test_general_choice_editorial.py)
    set_tests_properties(native.general_choice_editorial_authoring PROPERTIES TIMEOUT 45)
    set(LGAED_GENERAL_CHOICE_EDITORIAL_QA_SOURCE
        "${CMAKE_SOURCE_DIR}/scripts/curriculum/test_general_choice_editorial.py")
    set(LGAED_GENERAL_CHOICE_EDITORIAL_QA_ORIGINAL
        "${CMAKE_SOURCE_DIR}/app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json")
    set(LGAED_GENERAL_CHOICE_EDITORIAL_QA_REVISED
        "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/general-choice-editorial/u9-reviewed.json")
    add_custom_command(OUTPUT "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_REVISED}"
        COMMAND "${Python3_EXECUTABLE}" -B "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_SOURCE}"
            --write-reviewed "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_REVISED}"
        DEPENDS "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_SOURCE}"
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_ORIGINAL}"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        COMMENT "Authenticating U9 editorial choice review without changing published lessons"
        VERBATIM)
    set(LGAED_GENERAL_CHOICE_EDITORIAL_QA_MARKED
        "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/general-choice-editorial/u9-reviewed-marked.json")
    add_custom_command(OUTPUT "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_MARKED}"
        COMMAND "${Python3_EXECUTABLE}" -B "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_SOURCE}"
            --write-reviewed-marked "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_MARKED}"
        DEPENDS "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_SOURCE}"
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_ORIGINAL}"
            "${CMAKE_SOURCE_DIR}/scripts/curriculum/stable_state_layout.py"
        COMMENT "Verifying a genuine UTF-16 highlight in archived U9 choice reading"
        VERBATIM)
    add_custom_target(lgaed_general_choice_editorial_fixture
        DEPENDS "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_REVISED}"
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_MARKED}")
    add_executable(lgaed_general_choice_editorial_tests
        native/platform/windows/general_choice_editorial_tests.cpp)
    target_link_libraries(lgaed_general_choice_editorial_tests PRIVATE lgaed_curricular_windows)
    add_dependencies(lgaed_general_choice_editorial_tests
        lgaed_general_choice_editorial_fixture)
    add_test(NAME native.general_choice_editorial
        COMMAND lgaed_general_choice_editorial_tests
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_ORIGINAL}"
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_REVISED}")
    set_tests_properties(native.general_choice_editorial PROPERTIES TIMEOUT 100)
    add_executable(lgaed_general_choice_editorial_view_tests
        native/presentation/windows/general_choice_editorial_view_tests.cpp)
    target_link_libraries(lgaed_general_choice_editorial_view_tests
        PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_general_choice_editorial_view_tests
        lgaed_general_choice_editorial_fixture)
    add_test(NAME native.general_choice_editorial_reader
        COMMAND lgaed_general_choice_editorial_view_tests
            "${LGAED_GENERAL_CHOICE_EDITORIAL_QA_MARKED}")
    set_tests_properties(native.general_choice_editorial_reader
        PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_executable(lgaed_compile_diagnostic_view_tests native/presentation/windows/compile_diagnostic_view_tests.cpp)
    target_link_libraries(lgaed_compile_diagnostic_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.compile_diagnostic_view COMMAND lgaed_compile_diagnostic_view_tests)
    set_tests_properties(native.compile_diagnostic_view PROPERTIES TIMEOUT 240)
    add_executable(lgaed_cpp_diagnostic_runner_tests native/platform/windows/cpp_diagnostic_runner_tests.cpp)
    target_link_libraries(lgaed_cpp_diagnostic_runner_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.cpp_diagnostic_runner COMMAND lgaed_cpp_diagnostic_runner_tests)
    set_tests_properties(native.cpp_diagnostic_runner PROPERTIES TIMEOUT 240)
    add_executable(lgaed_compiler_revision_view_tests native/presentation/windows/compiler_revision_view_tests.cpp)
    target_link_libraries(lgaed_compiler_revision_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.compiler_revision_view COMMAND lgaed_compiler_revision_view_tests)
    set_tests_properties(native.compiler_revision_view PROPERTIES TIMEOUT 120)
    add_test(NAME native.u11_late_compiler_revision_view COMMAND lgaed_compiler_revision_view_tests --u11-late)
    set_tests_properties(native.u11_late_compiler_revision_view PROPERTIES TIMEOUT 120)
    add_test(NAME native.u11_starter_compiler_revision_view COMMAND lgaed_compiler_revision_view_tests --u11-starter)
    set_tests_properties(native.u11_starter_compiler_revision_view PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_executable(lgaed_cpp_fill_view_tests native/presentation/windows/cpp_fill_view_tests.cpp)
    target_link_libraries(lgaed_cpp_fill_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.cpp_fill_view COMMAND lgaed_cpp_fill_view_tests)
    set_tests_properties(native.cpp_fill_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_order_code_view_tests native/presentation/windows/order_code_view_tests.cpp
        native/presentation/windows/app.manifest)
    target_link_libraries(lgaed_order_code_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.order_code_view COMMAND lgaed_order_code_view_tests)
    set_tests_properties(native.order_code_view PROPERTIES TIMEOUT 40)
    add_dependencies(lgaed_lesson_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u11_assignment_editorial_fixtures)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u11_session_editorial_fixtures)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u12_append_hypothesis_fixtures)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u12_composed_output_fixtures)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u12_composed_output_current_fixtures)
    add_dependencies(lgaed_lesson_reader_tests lgaed_u12_composed_feedback_fixtures)
    add_test(NAME native.u12_composed_feedback_reader COMMAND lgaed_lesson_reader_tests --u12-composed-feedback
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/before.json" "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/after.json")
    set_tests_properties(native.u12_composed_feedback_reader PROPERTIES TIMEOUT 480 RUN_SERIAL TRUE)
    foreach(composed_question_phase RANGE 0 1)
        add_test(NAME native.u12_composed_question_reader_${composed_question_phase}
            COMMAND lgaed_lesson_reader_tests --u12-composed-question
                "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/before.json" "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/after.json"
                "${composed_question_phase}")
        set_tests_properties(native.u12_composed_question_reader_${composed_question_phase} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
    foreach(composed_output_phase RANGE 0 2)
        add_test(NAME native.u12_composed_output_reader_${composed_output_phase}
            COMMAND lgaed_lesson_reader_tests --u12-composed-output
                "${LGAED_U12_COMPOSED_OUTPUT_DIR}/before.json" "${LGAED_U12_COMPOSED_OUTPUT_DIR}/after.json"
                "${composed_output_phase}")
        set_tests_properties(native.u12_composed_output_reader_${composed_output_phase} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME native.u12_composed_output_current_reader_${composed_output_phase}
            COMMAND lgaed_lesson_reader_tests --u12-composed-output-current
                "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/before.json" "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/after.json"
                "${composed_output_phase}")
        set_tests_properties(native.u12_composed_output_current_reader_${composed_output_phase} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
    include(cmake/U12PublishedReaderTests.cmake)
    lgaed_add_u12_published_reader_tests()
    foreach(hypothesis_reader_phase RANGE 0 1)
        add_test(NAME native.u12_append_hypothesis_reader_${hypothesis_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u12-append-hypothesis
                "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/after.json"
                "${hypothesis_reader_phase}")
        set_tests_properties(native.u12_append_hypothesis_reader_${hypothesis_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        add_test(NAME native.u12_append_hypothesis_current_reader_${hypothesis_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u12-append-hypothesis-current
                "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-after.json"
                "${hypothesis_reader_phase}")
        set_tests_properties(native.u12_append_hypothesis_current_reader_${hypothesis_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        add_test(NAME native.u12_append_hypothesis_suffix_reader_${hypothesis_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u12-append-hypothesis-suffix
                "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-after.json"
                "${hypothesis_reader_phase}")
        set_tests_properties(native.u12_append_hypothesis_suffix_reader_${hypothesis_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
        add_test(NAME native.u12_append_hypothesis_final_reader_${hypothesis_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u12-append-hypothesis-final
                "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-after.json"
                "${hypothesis_reader_phase}")
        set_tests_properties(native.u12_append_hypothesis_final_reader_${hypothesis_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.u12_file_roundtrip_reader COMMAND lgaed_lesson_reader_tests --u12-file-roundtrip-reader)
    set_tests_properties(native.u12_file_roundtrip_reader PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.u12_file_append_reader COMMAND lgaed_lesson_reader_tests --u12-file-append-reader)
    set_tests_properties(native.u12_file_append_reader PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.u12_file_repairs_reader COMMAND lgaed_lesson_reader_tests --u12-file-repairs-reader)
    set_tests_properties(native.u12_file_repairs_reader PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
    foreach(file_format_page RANGE 0 2)
        add_test(NAME native.u12_file_format_reader_${file_format_page}
            COMMAND lgaed_lesson_reader_tests --u12-file-format-reader ${file_format_page})
        set_tests_properties(native.u12_file_format_reader_${file_format_page} PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)
    endforeach()
    foreach(session_reader_phase RANGE 0 1)
        add_test(NAME native.u11_session_literal_reader_${session_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u11-literal-spacing-revision
                "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-before.json"
                "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-after.json" ${session_reader_phase})
        set_tests_properties(native.u11_session_literal_reader_${session_reader_phase} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
        add_test(NAME native.u11_session_literal_current_reader_${session_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u11-literal-spacing-revision
                "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-before.json"
                "${LGAED_U11_SESSION_EDITORIAL_DIR}/session-current-after.json" ${session_reader_phase})
        set_tests_properties(native.u11_session_literal_current_reader_${session_reader_phase} PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    endforeach()
    foreach(assignment_reader_phase RANGE 0 1)
        add_test(NAME native.u11_assignment_editorial_reader_${assignment_reader_phase}
            COMMAND lgaed_lesson_reader_tests --u11-assignment-revision
                "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-before.json"
                "${LGAED_U11_ASSIGNMENT_EDITORIAL_DIR}/assignment-after.json" ${assignment_reader_phase})
        set_tests_properties(native.u11_assignment_editorial_reader_${assignment_reader_phase} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    include(cmake/U11EditorialReaderTests.cmake)
    lgaed_add_u11_editorial_reader_tests()
    add_test(NAME native.curricular_color_reader COMMAND lgaed_lesson_reader_tests --curricular-colors)
    set_tests_properties(native.curricular_color_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_executable(lgaed_current_reference_reader_tests native/presentation/windows/current_reference_reader_tests.cpp)
    target_link_libraries(lgaed_current_reference_reader_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_current_reference_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_test(NAME native.current_reference_reader COMMAND lgaed_current_reference_reader_tests)
    set_tests_properties(native.current_reference_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_executable(lgaed_equality_count_reader_tests native/presentation/windows/equality_count_reader_tests.cpp)
    target_link_libraries(lgaed_equality_count_reader_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_equality_count_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_test(NAME native.equality_count_reader COMMAND lgaed_equality_count_reader_tests)
    set_tests_properties(native.equality_count_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    foreach(inline_case RANGE 1 9)
        add_test(NAME native.current_inline_feedback_${inline_case}
            COMMAND lgaed_lesson_reader_tests --current-inline-feedback ${inline_case})
        set_tests_properties(native.current_inline_feedback_${inline_case} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.lesson_reader COMMAND lgaed_lesson_reader_tests)
    # Includes full real playback for the added prerequisite/bookmark cases.
    # Its foreground-focus scenario also owns the desktop during this test.
    set_tests_properties(native.lesson_reader PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.reader_state_compatibility COMMAND lgaed_lesson_reader_tests --state-compatibility)
    set_tests_properties(native.reader_state_compatibility PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.reader_missing_recording COMMAND lgaed_lesson_reader_tests --missing-recording)
    set_tests_properties(native.reader_missing_recording PROPERTIES TIMEOUT 90)
    add_test(NAME native.reader_changed_recording COMMAND lgaed_lesson_reader_tests --changed-recording)
    set_tests_properties(native.reader_changed_recording PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)
    add_test(NAME native.narration_speed_shortcuts COMMAND lgaed_lesson_reader_tests --rates)
    set_tests_properties(native.narration_speed_shortcuts PROPERTIES TIMEOUT 90)
    add_test(NAME native.narration_reader_suspend COMMAND lgaed_lesson_reader_tests --suspend "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.narration_reader_suspend PROPERTIES TIMEOUT 90)
    add_test(NAME native.narration_seek_shortcuts COMMAND lgaed_lesson_reader_tests --seek-keys)
    set_tests_properties(native.narration_seek_shortcuts PROPERTIES TIMEOUT 90)
    add_test(NAME native.compiler_reader_focus COMMAND lgaed_lesson_reader_tests --compiler-focus)
    set_tests_properties(native.compiler_reader_focus PROPERTIES TIMEOUT 90)
    add_test(NAME native.required_compiler_reader COMMAND lgaed_lesson_reader_tests --required-compiler)
    set_tests_properties(native.required_compiler_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME native.cpp_numeric_reader COMMAND lgaed_lesson_reader_tests --cpp-numeric-context)
    set_tests_properties(native.cpp_numeric_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME native.cpp_parenthesis_reader COMMAND lgaed_lesson_reader_tests --cpp-parenthesis-context)
    set_tests_properties(native.cpp_parenthesis_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME native.u11_domain_revision_reader COMMAND lgaed_lesson_reader_tests --u11-domain-revision)
    add_test(NAME native.u11_domain_race_reader COMMAND lgaed_lesson_reader_tests --u11-domain-race)
    add_test(NAME native.u11_consult_revision_reader COMMAND lgaed_lesson_reader_tests --u11-consult-revision)
    set_tests_properties(native.u11_consult_revision_reader PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
    set_tests_properties(native.u11_domain_revision_reader native.u11_domain_race_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    include(cmake/NarrationPlayerTests.cmake)
    lgaed_add_narration_player_tests("${CMAKE_SOURCE_DIR}/out/qa/narration-playback-rate")
    add_test(NAME native.dependent_puzzle_reader COMMAND lgaed_lesson_reader_tests --dependent-puzzles)
    set_tests_properties(native.dependent_puzzle_reader PROPERTIES TIMEOUT 150)
    add_test(NAME native.integrated_u1_reader COMMAND lgaed_lesson_reader_tests --integrated-u1)
    set_tests_properties(native.integrated_u1_reader PROPERTIES TIMEOUT 150)
    add_test(NAME native.integrated_order_reader COMMAND lgaed_lesson_reader_tests --integrated-order)
    set_tests_properties(native.integrated_order_reader PROPERTIES TIMEOUT 180)
    add_test(NAME native.integrated_matching_reader COMMAND lgaed_lesson_reader_tests --integrated-matching)
    set_tests_properties(native.integrated_matching_reader PROPERTIES TIMEOUT 180)
    add_executable(lgaed_curricular_fraction_view_tests native/presentation/windows/curricular_fraction_view_tests.cpp)
    target_link_libraries(lgaed_curricular_fraction_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_curricular_fraction_view_tests lgaed_narration_runtime)
    add_test(NAME native.curricular_fraction COMMAND lgaed_curricular_fraction_view_tests)
    set_tests_properties(native.curricular_fraction PROPERTIES TIMEOUT 120)
    add_test(NAME native.math_fraction_worker COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_math_bridge.py"
        WindowsMathBridgeTest.test_11_original_auto_typesetting_preserves_utf16_source_and_operators
        WindowsMathBridgeTest.test_12_current_curricular_fractions_keep_complete_utf16_units
        WindowsMathBridgeTest.test_13_windows_fraction_repair_preserves_protected_sources
        WindowsMathBridgeTest.test_14_fraction_repair_keeps_binary_minus_outside_and_unary_minus_inside)
    set_tests_properties(native.math_fraction_worker PROPERTIES TIMEOUT 30)
    add_executable(lgaed_math_puzzle_view_tests native/presentation/windows/math_puzzle_view_tests.cpp)
    target_link_libraries(lgaed_math_puzzle_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_puzzle_view_tests lgaed_narration_runtime)
    add_executable(lgaed_math_rigid_angle_view_tests native/presentation/windows/math_rigid_angle_view_tests.cpp)
    target_link_libraries(lgaed_math_rigid_angle_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_rigid_angle_view_tests lgaed_narration_runtime)
    add_test(NAME native.math_rigid_angle_view COMMAND lgaed_math_rigid_angle_view_tests)
    set_tests_properties(native.math_rigid_angle_view PROPERTIES TIMEOUT 120)
    add_executable(lgaed_math_narration_plan_tests native/presentation/windows/math_narration_plan_tests.cpp)
    target_link_libraries(lgaed_math_narration_plan_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_narration_plan_tests lgaed_narration_runtime)
    add_test(NAME native.math_narration_plan COMMAND lgaed_math_narration_plan_tests)
    set_tests_properties(native.math_narration_plan PROPERTIES TIMEOUT 120)
    add_test(NAME native.math_puzzle_view COMMAND lgaed_math_puzzle_view_tests)
    add_test(NAME native.math_internal_viewport COMMAND lgaed_math_puzzle_view_tests --internal-viewport)
    set_tests_properties(native.math_internal_viewport PROPERTIES TIMEOUT 120)
    set_tests_properties(native.math_puzzle_view PROPERTIES TIMEOUT 180)
    add_test(NAME native.math_technical_locale COMMAND lgaed_math_puzzle_view_tests --locale-technical)
    set_tests_properties(native.math_technical_locale PROPERTIES TIMEOUT 45)
    add_executable(lgaed_segment_transport_view_tests native/presentation/windows/segment_transport_view_tests.cpp)
    target_link_libraries(lgaed_segment_transport_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_segment_transport_view_tests lgaed_narration_runtime)
    add_test(NAME native.segment_transport_view COMMAND lgaed_segment_transport_view_tests)
    set_tests_properties(native.segment_transport_view PROPERTIES TIMEOUT 120)
    add_test(NAME native.segment_transport_worker COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_segment_transport.py")
    set_tests_properties(native.segment_transport_worker PROPERTIES TIMEOUT 60)
    add_executable(lgaed_math_expression_editor_tests native/presentation/windows/math_expression_editor_tests.cpp)
    target_link_libraries(lgaed_math_expression_editor_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_expression_editor_tests lgaed_narration_runtime)
    add_test(NAME native.math_expression_editor COMMAND lgaed_math_expression_editor_tests)
    set_tests_properties(native.math_expression_editor PROPERTIES TIMEOUT 90)
    include(cmake/NarrationWorkerTests.cmake)
    lgaed_add_narration_worker_tests()
    add_test(NAME native.windows_symbol_speech COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_windows_symbol_speech.py")
    set_tests_properties(native.windows_symbol_speech PROPERTIES TIMEOUT 180)
    add_test(NAME native.spanish_prose_boundaries COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_spanish_prose_boundaries.py")
    set_tests_properties(native.spanish_prose_boundaries PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.spanish_prose_g2p COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_spanish_prose_g2p.py")
    set_tests_properties(native.spanish_prose_g2p PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.semantic_source_envelopes COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_semantic_source_envelopes.py")
    set_tests_properties(native.semantic_source_envelopes PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.cpp_numeric_context COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_cpp_numeric_context.py")
    set_tests_properties(native.cpp_numeric_context PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.cpp_parenthesis_context COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_cpp_parenthesis_context.py")
    set_tests_properties(native.cpp_parenthesis_context PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.math_editor_worker COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_math_editor.py")
    add_test(NAME native.math_calculator_task COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_calculator_task.py")
    add_test(NAME native.worker_dependencies COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_worker_dependencies.py)
    add_executable(lgaed_system_speech_tests native/platform/windows/system_speech_tests.cpp)
    target_link_libraries(lgaed_system_speech_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.system_speech COMMAND lgaed_system_speech_tests "${CMAKE_SOURCE_DIR}/out/qa/system-speech")
    set_tests_properties(native.system_speech PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
    add_executable(lgaed_reading_shade_tests native/presentation/windows/reading_shade_tests.cpp)
    target_link_libraries(lgaed_reading_shade_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.reading_shade COMMAND lgaed_reading_shade_tests)
    set_tests_properties(native.reading_shade PROPERTIES TIMEOUT 30)
    add_executable(lgaed_calculator_tests native/core/scientific_calculator_tests.cpp)
    target_link_libraries(lgaed_calculator_tests PRIVATE lgaed_core)
    add_test(NAME native.calculator COMMAND lgaed_calculator_tests)
    add_executable(lgaed_tools_view_tests native/presentation/windows/tools_view_tests.cpp)
    target_link_libraries(lgaed_tools_view_tests PRIVATE lgaed_presentation_windows)
    add_executable(lgaed_graphics_view_tests native/presentation/windows/graphics_view_tests.cpp)
    target_link_libraries(lgaed_graphics_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.graphics_view COMMAND lgaed_graphics_view_tests)
    set_tests_properties(native.graphics_view PROPERTIES TIMEOUT 60)
    add_executable(lgaed_smo_tests native/core/smo_tests.cpp)
    target_link_libraries(lgaed_smo_tests PRIVATE lgaed_core)
    add_test(NAME native.smo COMMAND lgaed_smo_tests)
    add_executable(lgaed_smo_native_tests native/core/smo_native_tests.cpp)
    target_link_libraries(lgaed_smo_native_tests PRIVATE lgaed_core)
    add_test(NAME native.smo_native_compiler COMMAND lgaed_smo_native_tests)
    set_tests_properties(native.smo_native_compiler PROPERTIES TIMEOUT 30)
    add_test(NAME native.smo_compiler_cli COMMAND lgaed_smo_compile "${CMAKE_SOURCE_DIR}/native/smo_native_integer.smo")
    add_test(NAME native.smo_compiler_graphics_cli COMMAND lgaed_smo_compile "${CMAKE_SOURCE_DIR}/native/smo_triangle.smo" --profile graphics)
    set_tests_properties(native.smo_compiler_cli PROPERTIES TIMEOUT 30)
    set_tests_properties(native.smo_compiler_graphics_cli PROPERTIES TIMEOUT 30)
    add_executable(lgaed_smo_native_gpu_tests native/presentation/windows/smo_native_gpu_tests.cpp)
    target_link_libraries(lgaed_smo_native_gpu_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_native_gpu COMMAND lgaed_smo_native_gpu_tests)
    set_tests_properties(native.smo_native_gpu PROPERTIES TIMEOUT 90)
    add_executable(lgaed_smo_native_real_tests native/core/smo_native_real_tests.cpp)
    target_link_libraries(lgaed_smo_native_real_tests PRIVATE lgaed_core)
    add_test(NAME native.smo_native_real COMMAND lgaed_smo_native_real_tests)
    set_tests_properties(native.smo_native_real PROPERTIES TIMEOUT 30)
    add_executable(lgaed_smo_native_real_gpu_tests native/presentation/windows/smo_native_real_gpu_tests.cpp)
    target_link_libraries(lgaed_smo_native_real_gpu_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_native_real_gpu COMMAND lgaed_smo_native_real_gpu_tests)
    set_tests_properties(native.smo_native_real_gpu PROPERTIES TIMEOUT 120)
    add_executable(lgaed_smo_native_radix_tests native/presentation/windows/smo_native_radix_tests.cpp)
    target_link_libraries(lgaed_smo_native_radix_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_native_radix COMMAND lgaed_smo_native_radix_tests)
    set_tests_properties(native.smo_native_radix PROPERTIES TIMEOUT 90)
    add_executable(lgaed_smo_native_performance_tests native/presentation/windows/smo_native_performance_tests.cpp)
    target_link_libraries(lgaed_smo_native_performance_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_native_performance COMMAND lgaed_smo_native_performance_tests)
    set_tests_properties(native.smo_native_performance PROPERTIES TIMEOUT 90)
    add_executable(lgaed_smo_native_render_performance_tests EXCLUDE_FROM_ALL native/presentation/windows/smo_native_render_performance_tests.cpp)
    target_link_libraries(lgaed_smo_native_render_performance_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_smo_native_render_performance_tests smo_native_render_dxbc_reference)
    if(TARGET smo_native_render_spirv_reference)
        add_dependencies(lgaed_smo_native_render_performance_tests smo_native_render_spirv_reference)
    endif()
    add_executable(lgaed_smo_f12_render_performance_tests EXCLUDE_FROM_ALL native/presentation/windows/smo_f12_render_performance_tests.cpp)
    target_link_libraries(lgaed_smo_f12_render_performance_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_smo_f12_render_performance_tests smo_program_render_dxbc_reference)
    if(TARGET smo_program_render_spirv_reference)
        add_dependencies(lgaed_smo_f12_render_performance_tests smo_program_render_spirv_reference)
    endif()
    if(LGAED_BUILD_SMO_RENDER_REFERENCE_TESTS)
        set_target_properties(lgaed_smo_native_render_performance_tests lgaed_smo_f12_render_performance_tests
            PROPERTIES EXCLUDE_FROM_ALL FALSE)
        add_test(NAME native.smo_native_render_performance COMMAND lgaed_smo_native_render_performance_tests)
        add_test(NAME native.smo_f12_render_performance COMMAND lgaed_smo_f12_render_performance_tests)
        set_tests_properties(native.smo_native_render_performance native.smo_f12_render_performance PROPERTIES TIMEOUT 90)
    endif()
    add_custom_target(lgaed_smo_performance_tests DEPENDS
        lgaed_smo_native_performance_tests lgaed_smo_native_render_performance_tests lgaed_smo_f12_render_performance_tests lgaed_smo_native_radix_tests)
    add_executable(lgaed_smo_view_tests native/presentation/windows/smo_view_tests.cpp)
    target_link_libraries(lgaed_smo_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_internal_info COMMAND lgaed_smo_view_tests --internal-info)
    set_tests_properties(native.smo_internal_info PROPERTIES TIMEOUT 60)
    add_test(NAME native.smo_triangle COMMAND lgaed_smo_view_tests triangle)
    add_test(NAME native.smo_cube COMMAND lgaed_smo_view_tests cube)
    add_test(NAME native.smo_triangle_f12 COMMAND lgaed_smo_view_tests triangle f12)
    add_test(NAME native.smo_cube_f12 COMMAND lgaed_smo_view_tests cube f12)
    set_tests_properties(native.smo native.smo_triangle native.smo_cube PROPERTIES TIMEOUT 60)
    set_tests_properties(native.smo_triangle_f12 native.smo_cube_f12 PROPERTIES TIMEOUT 60)
    add_executable(lgaed_smo_benchmark_tests native/presentation/windows/smo_benchmark_tests.cpp)
    target_link_libraries(lgaed_smo_benchmark_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_benchmark COMMAND lgaed_smo_benchmark_tests)
    set_tests_properties(native.smo_benchmark PROPERTIES TIMEOUT 120)
    add_executable(lgaed_smo_sustained_tests native/presentation/windows/smo_sustained_tests.cpp)
    target_link_libraries(lgaed_smo_sustained_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_sustained COMMAND lgaed_smo_sustained_tests)
    set_tests_properties(native.smo_sustained PROPERTIES TIMEOUT 60)
    add_executable(lgaed_smo_rule_gpu_tests native/presentation/windows/smo_rule_gpu_tests.cpp)
    target_link_libraries(lgaed_smo_rule_gpu_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_rules_gpu COMMAND lgaed_smo_rule_gpu_tests)
    set_tests_properties(native.smo_rules_gpu PROPERTIES TIMEOUT 60)
    add_executable(lgaed_smo_rule_laws_tests native/core/smo_rule_laws_tests.cpp)
    target_link_libraries(lgaed_smo_rule_laws_tests PRIVATE lgaed_smo_arithmetic)
    add_test(NAME native.smo_rule_laws COMMAND lgaed_smo_rule_laws_tests)
    set_tests_properties(native.smo_rule_laws PROPERTIES TIMEOUT 30)
    add_executable(lgaed_smo_power_laws_tests native/core/smo_power_laws_tests.cpp)
    target_link_libraries(lgaed_smo_power_laws_tests PRIVATE lgaed_smo_arithmetic)
    add_test(NAME native.smo_power_laws COMMAND lgaed_smo_power_laws_tests)
    set_tests_properties(native.smo_power_laws PROPERTIES TIMEOUT 30)
    # Research kernels are explicit QA targets, never extra application startup
    # work. Enable this option only to include their brief WARP checks in CTest.
    option(LGAED_BUILD_SMO_EXPLORATION_TESTS "Include isolated SMO exploration GPU tests in the normal build/CTest" OFF)
    add_executable(lgaed_smo_scale12_experiment_tests EXCLUDE_FROM_ALL
        native/presentation/windows/smo_scale12_experiment.cpp
        native/presentation/windows/smo_scale12_experiment_tests.cpp)
    target_link_libraries(lgaed_smo_scale12_experiment_tests PRIVATE lgaed_presentation_windows)
    lgaed_compile_smo_hlsl_family(lgaed_smo_scale12_experiment_tests smo_scale12 SMO_SCALE12_STAGE scale12_stage 4 LGAED_SMO_SCALE12_HLSL_SHADER_ROOT
        SHARED_CS50 smo_float12.inc smo_scale12_experiment.inc)
    add_executable(lgaed_smo_rule_application_tests EXCLUDE_FROM_ALL
        native/presentation/windows/smo_rule_application_tests.cpp)
    target_link_libraries(lgaed_smo_rule_application_tests PRIVATE lgaed_presentation_windows)
    lgaed_compile_smo_hlsl_family(lgaed_smo_rule_application_tests smo_rule_application APP_MODE application_mode 3 LGAED_SMO_APPLICATION_HLSL_SHADER_ROOT
        SHARED_CS50 smo_arithmetic.inc smo_rule_application.inc)
    get_target_property(_lgaed_experiment_glslc lgaed_presentation_windows LGAED_SMO_GLSLC)
    if(_lgaed_experiment_glslc)
        target_compile_definitions(lgaed_smo_scale12_experiment_tests PRIVATE LGAED_HAS_VULKAN=1)
        target_compile_definitions(lgaed_smo_rule_application_tests PRIVATE LGAED_HAS_VULKAN=1)
        lgaed_compile_smo_family(lgaed_smo_scale12_experiment_tests "${_lgaed_experiment_glslc}" smo_scale12 SMO_SCALE12_STAGE scale12_stage 4 LGAED_SMO_SCALE12_SHADER_ROOT
            smo_float12.inc smo_scale12_experiment.inc)
        lgaed_compile_smo_family(lgaed_smo_rule_application_tests "${_lgaed_experiment_glslc}" smo_rule_application APP_MODE application_mode 3 LGAED_SMO_APPLICATION_SHADER_ROOT
            smo_arithmetic.inc smo_rule_application.inc)
    endif()
    add_custom_target(lgaed_smo_exploration_tests DEPENDS lgaed_smo_rule_laws_tests lgaed_smo_power_laws_tests
        lgaed_smo_scale12_experiment_tests lgaed_smo_rule_application_tests)
    if(LGAED_BUILD_SMO_EXPLORATION_TESTS)
        set_target_properties(lgaed_smo_scale12_experiment_tests lgaed_smo_rule_application_tests PROPERTIES EXCLUDE_FROM_ALL FALSE)
        add_test(NAME native.smo_scale12_experiment COMMAND lgaed_smo_scale12_experiment_tests)
        add_test(NAME native.smo_rule_applications COMMAND lgaed_smo_rule_application_tests)
        set_tests_properties(native.smo_scale12_experiment native.smo_rule_applications PROPERTIES TIMEOUT 90)
    endif()
    add_executable(lgaed_smo_duodecimal_tests native/presentation/windows/smo_duodecimal_gpu_tests.cpp)
    target_link_libraries(lgaed_smo_duodecimal_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_duodecimal COMMAND lgaed_smo_duodecimal_tests)
    set_tests_properties(native.smo_duodecimal PROPERTIES TIMEOUT 60)
    add_executable(lgaed_smo_float12_cpu_tests native/presentation/windows/smo_float12_cpu_tests.cpp)
    target_link_libraries(lgaed_smo_float12_cpu_tests PRIVATE lgaed_smo_arithmetic)
    add_test(NAME native.smo_float12_cpu COMMAND lgaed_smo_float12_cpu_tests)
    set_tests_properties(native.smo_float12_cpu PROPERTIES TIMEOUT 30)
    add_test(NAME native.smo_float12_gpu COMMAND lgaed_smo_duodecimal_tests float12)
    add_executable(lgaed_smo_program_gpu_tests native/presentation/windows/smo_program_gpu_tests.cpp)
    target_link_libraries(lgaed_smo_program_gpu_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_program_gpu COMMAND lgaed_smo_program_gpu_tests)
    set_tests_properties(native.smo_program_gpu PROPERTIES TIMEOUT 120)
    add_executable(lgaed_smo_radix_ablation_tests native/presentation/windows/smo_radix_ablation_tests.cpp)
    target_link_libraries(lgaed_smo_radix_ablation_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.smo_radix_ablation COMMAND lgaed_smo_radix_ablation_tests)
    set_tests_properties(native.smo_radix_ablation PROPERTIES TIMEOUT 90)
    set_tests_properties(native.smo_float12_gpu PROPERTIES TIMEOUT 60)
    add_dependencies(lgaed_tools_view_tests lgaed_narration_runtime)
    add_test(NAME native.tools_view COMMAND lgaed_tools_view_tests)
    add_test(NAME native.calculator_internal_window COMMAND lgaed_tools_view_tests --calculator-window)
    set_tests_properties(native.calculator_internal_window PROPERTIES TIMEOUT 120)
    set_tests_properties(native.tools_view PROPERTIES TIMEOUT 90)
    add_executable(lgaed_catalog_tests native/core/catalog_tests.cpp)
    target_link_libraries(lgaed_catalog_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.catalog COMMAND lgaed_catalog_tests)
    set_tests_properties(native.catalog PROPERTIES TIMEOUT 30)
    add_executable(lgaed_catalog_view_tests native/presentation/windows/catalog_view_tests.cpp)
    target_link_libraries(lgaed_catalog_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.catalog_view COMMAND lgaed_catalog_view_tests)
    # Traverses complete catalogues/progress in three languages and a live
    # lesson; allow headroom alongside the Debug playback/math integrations.
    set_tests_properties(native.catalog_view PROPERTIES TIMEOUT 90)
    add_executable(lgaed_narration_state_tests native/core/narration_state_tests.cpp)
    target_link_libraries(lgaed_narration_state_tests PRIVATE lgaed_core)
    add_test(NAME native.narration_state COMMAND lgaed_narration_state_tests)
    add_executable(lgaed_profile_tests native/core/profile_tests.cpp)
    target_link_libraries(lgaed_profile_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.profile COMMAND lgaed_profile_tests)
    add_executable(lgaed_profile_migration_tests native/platform/windows/profile_migration_tests.cpp)
    target_link_libraries(lgaed_profile_migration_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.profile_migration COMMAND lgaed_profile_migration_tests "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.profile_migration PROPERTIES TIMEOUT 45)
    add_executable(lgaed_profile_session_tests native/platform/windows/profile_session_tests.cpp)
    target_link_libraries(lgaed_profile_session_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.profile_session COMMAND lgaed_profile_session_tests "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.profile_session PROPERTIES TIMEOUT 30)
    add_test(NAME native.window_smoke
        COMMAND LearnGameAndEngineDev --profile "${LGAED_DEFAULT_PROFILE}"
        --qa-instance "ctest-${CMAKE_BUILD_TYPE}" --smoke-test)
    set_tests_properties(native.window_smoke PROPERTIES TIMEOUT 20)
    add_executable(lgaed_lesson_tests native/core/lesson_tests.cpp)
    target_link_libraries(lgaed_lesson_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_dependencies(lgaed_lesson_tests lgaed_compiler_contract_fixtures)
    add_dependencies(lgaed_lesson_tests lgaed_u11_session_editorial_fixtures)
    add_dependencies(lgaed_lesson_tests lgaed_u12_append_hypothesis_fixtures)
    add_dependencies(lgaed_lesson_tests lgaed_u12_composed_output_fixtures)
    add_dependencies(lgaed_lesson_tests lgaed_u12_composed_output_current_fixtures)
    add_dependencies(lgaed_lesson_tests lgaed_u12_composed_feedback_fixtures)
    add_test(NAME native.u12_composed_feedback_contract COMMAND lgaed_lesson_tests --u12-composed-feedback
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/before.json" "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/after.json")
    set_tests_properties(native.u12_composed_feedback_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_composed_output_contract COMMAND lgaed_lesson_tests --u12-composed-output
        "${LGAED_U12_COMPOSED_OUTPUT_DIR}/before.json" "${LGAED_U12_COMPOSED_OUTPUT_DIR}/after.json")
    set_tests_properties(native.u12_composed_output_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_composed_output_current_contract COMMAND lgaed_lesson_tests --u12-composed-output-current
        "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/before.json" "${LGAED_U12_COMPOSED_OUTPUT_CURRENT_DIR}/after.json")
    set_tests_properties(native.u12_composed_output_current_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_append_hypothesis_contract COMMAND lgaed_lesson_tests --u12-append-hypothesis
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/after.json")
    set_tests_properties(native.u12_append_hypothesis_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_append_hypothesis_current_contract COMMAND lgaed_lesson_tests --u12-append-hypothesis-current
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/current-after.json")
    set_tests_properties(native.u12_append_hypothesis_current_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_append_hypothesis_suffix_contract COMMAND lgaed_lesson_tests --u12-append-hypothesis-suffix
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/suffix-after.json")
    set_tests_properties(native.u12_append_hypothesis_suffix_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.u12_append_hypothesis_final_contract COMMAND lgaed_lesson_tests --u12-append-hypothesis-final
        "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-before.json" "${LGAED_U12_APPEND_HYPOTHESIS_DIR}/final-after.json")
    set_tests_properties(native.u12_append_hypothesis_final_contract PROPERTIES TIMEOUT 60)
    add_test(NAME native.lesson_contract COMMAND lgaed_lesson_tests)
    add_executable(lgaed_cpp_runner_tests native/platform/windows/cpp_runner_tests.cpp)
    target_link_libraries(lgaed_cpp_runner_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.cpp_runner COMMAND lgaed_cpp_runner_tests)
    set_tests_properties(native.cpp_runner PROPERTIES TIMEOUT 180)
    add_executable(lgaed_cpp_project_tests native/core/cpp_project_tests.cpp)
    target_link_libraries(lgaed_cpp_project_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.cpp_project COMMAND lgaed_cpp_project_tests)
    set_tests_properties(native.cpp_project PROPERTIES TIMEOUT 30)
    add_executable(lgaed_cpp_cmake_runner_tests native/platform/windows/cpp_cmake_runner_tests.cpp)
    target_link_libraries(lgaed_cpp_cmake_runner_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.cpp_cmake_runner COMMAND lgaed_cpp_cmake_runner_tests)
    set_tests_properties(native.cpp_cmake_runner PROPERTIES TIMEOUT 180)
    add_executable(lgaed_cpp_project_history_tests native/platform/windows/cpp_project_history_tests.cpp)
    target_link_libraries(lgaed_cpp_project_history_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.cpp_project_history COMMAND lgaed_cpp_project_history_tests "${CMAKE_SOURCE_DIR}/out/qa/cpp-project-history")
    set_tests_properties(native.cpp_project_history PROPERTIES TIMEOUT 40)
    add_executable(lgaed_cpp_project_view_tests native/presentation/windows/cpp_project_view_tests.cpp)
    target_link_libraries(lgaed_cpp_project_view_tests PRIVATE lgaed_presentation_windows)
    add_executable(lgaed_cmake_curricular_targets_tests native/presentation/windows/cmake_curricular_targets_tests.cpp)
    target_link_libraries(lgaed_cmake_curricular_targets_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_cmake_curricular_targets_tests lgaed_cpp_tool_host)
    foreach(cmake_case RANGE 1 7)
        add_test(NAME native.cmake_curricular_targets_${cmake_case}
            COMMAND lgaed_cmake_curricular_targets_tests ${cmake_case})
        set_tests_properties(native.cmake_curricular_targets_${cmake_case} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.cpp_project_view COMMAND lgaed_cpp_project_view_tests "${CMAKE_SOURCE_DIR}/out/qa/cpp-project-history")
    add_test(NAME native.cpp_project_internal_dialogs COMMAND lgaed_cpp_project_view_tests "${CMAKE_SOURCE_DIR}/out/qa/cpp-project-history" --internal-dialogs)
    set_tests_properties(native.cpp_project_internal_dialogs PROPERTIES TIMEOUT 90)
    set_tests_properties(native.cpp_project_view PROPERTIES TIMEOUT 120)
    include(cmake/CompilerHistoricalReaderTests.cmake)
    lgaed_add_compiler_historical_reader_tests()
    add_test(NAME native.compiler_import_fixtures COMMAND "${Python3_EXECUTABLE}" -B -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/scripts/curriculum" -p test_compiler_import_fixtures.py)
    set_tests_properties(native.compiler_import_fixtures PROPERTIES TIMEOUT 120)
    lgaed_add_state_import_tests("${CMAKE_SOURCE_DIR}/out/qa/state")
    add_test(NAME native.u11_names_state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state"
        --u11-names "${LGAED_U11_NAMES_BEFORE_FILE}")
    set_tests_properties(native.u11_names_state_import PROPERTIES TIMEOUT 120)
    add_test(NAME native.state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state")
    add_test(NAME native.compiler_review_state COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state" --compiler-reviews)
    set_tests_properties(native.compiler_review_state PROPERTIES TIMEOUT 60)
    add_test(NAME native.u11_starter_state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state" --u11-starter)
    add_test(NAME native.u11_consult_choice_state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state" --u11-consult)
    set_tests_properties(native.u11_consult_choice_state_import PROPERTIES TIMEOUT 120)
    add_test(NAME native.remainder_editorial_state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state"
        --remainder-editorial "${LGAED_WORK_EXACT_EDITORIAL_DIR}/remainder-before.json")
    set_tests_properties(native.remainder_editorial_state_import PROPERTIES TIMEOUT 120)
    add_executable(lgaed_code_format_compat_tests native/core/code_format_compat_tests.cpp)
    target_link_libraries(lgaed_code_format_compat_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.code_format_compat COMMAND lgaed_code_format_compat_tests)
    set_tests_properties(native.code_format_compat PROPERTIES TIMEOUT 30)
    add_executable(lgaed_rich_text_tests native/presentation/windows/rich_text_tests.cpp
        native/presentation/windows/rich_text.cpp native/presentation/windows/reader_theme.cpp)
    target_link_libraries(lgaed_rich_text_tests PRIVATE lgaed_platform_windows lgaed_host_options user32 gdi32 uxtheme ole32 oleaut32 comctl32 uuid)
    add_test(NAME native.rich_text COMMAND lgaed_rich_text_tests)
    add_test(NAME native.curricular_text_roles COMMAND lgaed_rich_text_tests --curricular-roles)
    set_tests_properties(native.curricular_text_roles PROPERTIES TIMEOUT 60)
    add_test(NAME native.unmarked_prose_roles COMMAND lgaed_rich_text_tests --unmarked-prose)
    set_tests_properties(native.unmarked_prose_roles PROPERTIES TIMEOUT 30)
    add_test(NAME native.technical_background COMMAND lgaed_rich_text_tests --technical-background
        "${CMAKE_SOURCE_DIR}/out/qa/technical-background.bmp")
    set_tests_properties(native.technical_background PROPERTIES TIMEOUT 45)
    add_test(NAME native.technical_text_stress COMMAND lgaed_rich_text_tests --technical-stress)
    set_tests_properties(native.technical_text_stress PROPERTIES TIMEOUT 45)
    add_test(NAME native.technical_text_android_parity COMMAND lgaed_rich_text_tests --android-parity
        "${CMAKE_SOURCE_DIR}/native/presentation/windows/fixtures/technical_ranges_android.tsv"
        "${CMAKE_SOURCE_DIR}/out/qa/jvm-technical/native-mismatches.tsv")
    set_tests_properties(native.technical_text_android_parity PROPERTIES TIMEOUT 45)
    add_test(NAME native.technical_text_reference_sources COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_SOURCE_DIR}/native/presentation/windows/test_support/generate_technical_ranges_fixture.py" --verify)
    add_executable(lgaed_code_editor_tests native/presentation/windows/code_editor_tests.cpp)
    target_link_libraries(lgaed_code_editor_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.code_editor COMMAND lgaed_code_editor_tests)
    add_executable(lgaed_editing_bar_tests native/presentation/windows/editing_bar_tests.cpp)
    target_link_libraries(lgaed_editing_bar_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.editing_bar COMMAND lgaed_editing_bar_tests)
    set_tests_properties(native.editing_bar PROPERTIES TIMEOUT 20)
    add_executable(lgaed_lesson_view_tests native/presentation/windows/lesson_view_tests.cpp)
    target_link_libraries(lgaed_lesson_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_lesson_view_tests lgaed_narration_runtime)
    add_dependencies(lgaed_lesson_view_tests LearnGameAndEngineDev)
    # The focused hidden-control check needs neither the app executable nor
    # the narration runtime used by other lesson-view scenarios.
    add_executable(lgaed_curricular_inline_feedback_tests native/presentation/windows/lesson_view_tests.cpp)
    target_compile_definitions(lgaed_curricular_inline_feedback_tests PRIVATE LGAED_INLINE_FEEDBACK_ONLY=1)
    target_link_libraries(lgaed_curricular_inline_feedback_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_curricular_inline_feedback_tests lgaed_retirement_fixtures)
    add_test(NAME native.lesson_view COMMAND lgaed_lesson_view_tests)
    set_tests_properties(native.lesson_view PROPERTIES TIMEOUT 180)
    add_test(NAME native.compiler_pointer_scroll COMMAND lgaed_lesson_view_tests --compiler-scroll)
    set_tests_properties(native.compiler_pointer_scroll PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
    add_test(NAME native.u12_file_roundtrip_view COMMAND lgaed_lesson_view_tests --u12-file-roundtrip)
    set_tests_properties(native.u12_file_roundtrip_view PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME native.u12_file_append_view COMMAND lgaed_lesson_view_tests --u12-file-append)
    set_tests_properties(native.u12_file_append_view PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    add_test(NAME native.u12_file_repairs_view COMMAND lgaed_lesson_view_tests --u12-file-repairs)
    set_tests_properties(native.u12_file_repairs_view PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_dependencies(lgaed_lesson_view_tests lgaed_u12_composed_feedback_fixtures)
    add_test(NAME native.u12_composed_feedback_view COMMAND lgaed_lesson_view_tests --u12-composed-feedback
        "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/before.json" "${LGAED_U12_COMPOSED_FEEDBACK_DIR}/after.json")
    set_tests_properties(native.u12_composed_feedback_view PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_test(NAME native.lesson_caret COMMAND lgaed_lesson_view_tests --caret)
    set_tests_properties(native.lesson_caret PROPERTIES TIMEOUT 180)
    add_test(NAME native.lesson_scroll_render COMMAND lgaed_lesson_view_tests --scroll-repaint)
    add_test(NAME native.lesson_scroll_motion COMMAND lgaed_lesson_view_tests --scroll-motion)
    set_tests_properties(native.lesson_scroll_motion PROPERTIES TIMEOUT 45)
    set_tests_properties(native.lesson_scroll_render PROPERTIES TIMEOUT 45)
    add_test(NAME native.lesson_texto_secuencia COMMAND lgaed_lesson_view_tests --texto-secuencia)
    set_tests_properties(native.lesson_texto_secuencia PROPERTIES TIMEOUT 150)
    add_test(NAME native.lesson_texto_restauracion COMMAND lgaed_lesson_view_tests --texto-restauracion)
    set_tests_properties(native.lesson_texto_restauracion PROPERTIES TIMEOUT 150)
    add_test(NAME native.lesson_navigation_geometry COMMAND lgaed_lesson_view_tests --navigation-geometry)
    set_tests_properties(native.lesson_navigation_geometry PROPERTIES TIMEOUT 180)
    add_test(NAME native.compiler_puzzles COMMAND lgaed_lesson_view_tests --compiler-puzzles)
    set_tests_properties(native.compiler_puzzles PROPERTIES TIMEOUT 150)
    add_test(NAME native.stderr_objective COMMAND lgaed_lesson_view_tests --stderr-objective)
    set_tests_properties(native.stderr_objective PROPERTIES TIMEOUT 90)
    add_test(NAME native.lesson_menu COMMAND lgaed_lesson_view_tests --lesson-menu)
    set_tests_properties(native.lesson_menu PROPERTIES TIMEOUT 45)
    add_test(NAME native.u11_editorial COMMAND lgaed_lesson_view_tests --u11-editorial)
    set_tests_properties(native.u11_editorial PROPERTIES TIMEOUT 120)
    add_test(NAME native.narration_repaint COMMAND lgaed_lesson_view_tests --narration-repaint)
    set_tests_properties(native.narration_repaint PROPERTIES TIMEOUT 60)
    add_test(NAME native.curricular_inline_feedback COMMAND lgaed_curricular_inline_feedback_tests --inline-feedback)
    set_tests_properties(native.curricular_inline_feedback PROPERTIES TIMEOUT 120)
    add_test(NAME native.compiler_editor_height COMMAND lgaed_lesson_view_tests --compiler-height)
    set_tests_properties(native.compiler_editor_height PROPERTIES TIMEOUT 60)
    add_test(NAME native.dependent_puzzles COMMAND lgaed_lesson_view_tests --dependent-puzzles)
    set_tests_properties(native.dependent_puzzles PROPERTIES TIMEOUT 150)
    foreach(partition RANGE 0 3)
        add_test(NAME native.current_dependent_puzzles.${partition} COMMAND lgaed_lesson_view_tests --current-dependent-puzzles ${partition} 4)
        set_tests_properties(native.current_dependent_puzzles.${partition} PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
    endforeach()
    add_executable(lgaed_programming_composition_tests native/presentation/windows/programming_composition_tests.cpp)
    target_link_libraries(lgaed_programming_composition_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_programming_composition_tests lgaed_narration_runtime)
    foreach(locale IN ITEMS es en fr)
        foreach(partition RANGE 0 15)
            add_test(NAME native.programming_composition_${locale}_${partition} COMMAND lgaed_programming_composition_tests ${locale} ${partition} 16)
            set_tests_properties(native.programming_composition_${locale}_${partition} PROPERTIES TIMEOUT 180)
        endforeach()
    endforeach()
    add_executable(lgaed_import_view_tests native/presentation/windows/import_view_tests.cpp)
    target_link_libraries(lgaed_import_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.import_view COMMAND lgaed_import_view_tests)
    set_tests_properties(native.import_view PROPERTIES TIMEOUT 180)
    foreach(fixture_consumer IN ITEMS lgaed_lesson_tests lgaed_state_tests lgaed_catalog_tests
            lgaed_lesson_reader_tests lgaed_lesson_view_tests lgaed_import_view_tests)
        add_dependencies(${fixture_consumer} lgaed_retirement_fixtures)
    endforeach()
    foreach(fixture_consumer IN ITEMS lgaed_lesson_tests lgaed_state_tests lgaed_catalog_tests
            lgaed_compile_diagnostic_view_tests)
        add_dependencies(${fixture_consumer} lgaed_diagnostic_fixtures)
    endforeach()
    foreach(fixture_consumer IN ITEMS lgaed_lesson_tests lgaed_lesson_state_tests lgaed_state_tests
            lgaed_lesson_composition_tests)
        if(TARGET ${fixture_consumer})
            add_dependencies(${fixture_consumer} lgaed_order_line_fixtures)
        endif()
    endforeach()
    add_custom_target(lgaed_curricular_color_checks DEPENDS LearnGameAndEngineDev
        lgaed_rich_text_tests lgaed_reading_shade_tests lgaed_technical_context_view_tests lgaed_remainder_context_view_tests
        lgaed_lesson_reader_tests lgaed_current_reference_reader_tests lgaed_cpp_fill_view_tests
        lgaed_math_puzzle_view_tests lgaed_window_frame_tests lgaed_window_fullscreen_tests lgaed_catalog_view_tests)
    add_custom_target(lgaed_compiler_revision_checks DEPENDS LearnGameAndEngineDev
        lgaed_lesson_tests lgaed_state_tests lgaed_compiler_revision_view_tests
        lgaed_lesson_reader_tests lgaed_choice_revision_view_tests lgaed_exact_prediction_view_tests)
    add_custom_target(lgaed_work_revision_checks DEPENDS LearnGameAndEngineDev
        lgaed_technical_context_view_tests lgaed_remainder_context_view_tests lgaed_editorial_matching_view_tests lgaed_lesson_tests lgaed_state_tests
        lgaed_rich_text_tests lgaed_reading_shade_tests lgaed_lesson_reader_tests)
    add_custom_target(lgaed_cpp_narration_checks DEPENDS LearnGameAndEngineDev
        lgaed_lesson_reader_tests lgaed_narration_worker_tests lgaed_math_narration_plan_tests
        lgaed_audio_course_view_tests lgaed_audio_generation_tests lgaed_audio_generation_real_tests)
    add_custom_target(lgaed_math_label_reader_checks DEPENDS LearnGameAndEngineDev
        lgaed_lesson_reader_tests lgaed_lesson_tests lgaed_state_tests
        lgaed_editorial_matching_view_tests lgaed_editorial_return_view_tests)
    add_custom_target(lgaed_u11_names_checks DEPENDS LearnGameAndEngineDev
        lgaed_u11_names_integration_view_tests lgaed_u11_names_reader_tests
        lgaed_u11_declarations_reader_tests lgaed_lesson_tests lgaed_state_tests)
    add_custom_target(lgaed_u11_assignment_checks DEPENDS LearnGameAndEngineDev
        lgaed_u11_assignment_editorial_view_tests lgaed_lesson_reader_tests lgaed_u11_declarations_reader_tests
        lgaed_lesson_tests lgaed_state_tests)
    add_custom_target(lgaed_u11_session_literal_checks DEPENDS LearnGameAndEngineDev
        lgaed_technical_context_view_tests lgaed_lesson_reader_tests lgaed_lesson_tests)
    add_custom_target(lgaed_cpp23_checks DEPENDS LearnGameAndEngineDev
        lgaed_cpp_runner_tests lgaed_cpp_project_tests lgaed_cpp_cmake_runner_tests
        lgaed_cpp_diagnostic_runner_tests lgaed_lesson_tests lgaed_lesson_view_tests
        lgaed_smo_tests lgaed_smo_native_tests lgaed_smo_native_real_tests lgaed_smo_native_real_gpu_tests
        lgaed_narration_worker_tests lgaed_math_narration_plan_tests)
    add_custom_target(lgaed_curricular_reader_checks DEPENDS LearnGameAndEngineDev
        lgaed_equality_count_reader_tests lgaed_editorial_return_tests lgaed_editorial_return_view_tests)
    add_custom_target(lgaed_editorial_return_checks DEPENDS
        LearnGameAndEngineDev lgaed_editorial_return_tests lgaed_editorial_return_view_tests
        lgaed_lesson_tests lgaed_state_tests lgaed_catalog_tests
        lgaed_lesson_reader_tests lgaed_current_reference_reader_tests lgaed_narration_worker_tests)
    add_custom_target(lgaed_windows_settings_checks DEPENDS
        lgaed_current_reference_reader_tests
        lgaed_math_narration_plan_tests lgaed_cmake_curricular_targets_tests
        lgaed_scrollbar_tests lgaed_math_rigid_angle_view_tests lgaed_lesson_composition_tests
        lgaed_narration_state_tests
        LearnGameAndEngineDev lgaed_internal_window_tests lgaed_settings_view_tests
        lgaed_lesson_reader_tests lgaed_lesson_view_tests lgaed_reader_shortcut_focus_tests
        lgaed_audio_course_view_tests lgaed_tools_view_tests lgaed_cpp_project_view_tests
        lgaed_math_puzzle_view_tests lgaed_math_expression_editor_tests lgaed_smo_view_tests
        lgaed_audio_schedule_tests lgaed_audio_generation_tests lgaed_audio_generation_real_tests
        lgaed_system_speech_tests lgaed_system_speech_background_tests
        lgaed_narration_cpu_policy_tests lgaed_narration_shutdown_tests
        lgaed_narration_preferences_tests lgaed_profile_tests lgaed_profile_migration_tests lgaed_profile_session_tests)
endif()

install(TARGETS LearnGameAndEngineDev lgaed_cpp_tool_host lgaed_system_speech_host RUNTIME DESTINATION bin)
install(FILES native/assets/fonts/NotoSerif-Regular.ttf native/assets/fonts/NotoSerif-Italic.ttf
    native/assets/fonts/NOTICE.txt native/assets/fonts/DroidSansMono.ttf
    native/assets/fonts/DroidSansMono-NOTICE.txt DESTINATION bin/fonts)
message(STATUS "Development profile: ${LGAED_DEFAULT_PROFILE}; build and install trees belong to this checkout.")

if(BUILD_TESTING)
    add_executable(lgaed_inline_presentation_tests native/presentation/windows/inline_presentation_tests.cpp)
    target_link_libraries(lgaed_inline_presentation_tests PRIVATE lgaed_presentation_windows)
    set_target_properties(lgaed_inline_presentation_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_test(NAME native.inline_presentation COMMAND lgaed_inline_presentation_tests)
endif()

if(BUILD_TESTING)
    add_executable(lgaed_lesson_text_display_tests native/presentation/windows/lesson_text_display_tests.cpp)
    target_link_libraries(lgaed_lesson_text_display_tests PRIVATE lgaed_presentation_windows)
    set_target_properties(lgaed_lesson_text_display_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
    add_test(NAME native.lesson_text_color COMMAND lgaed_lesson_text_display_tests color)
    add_test(NAME native.order_text_bounds COMMAND lgaed_lesson_text_display_tests geometry)
endif()

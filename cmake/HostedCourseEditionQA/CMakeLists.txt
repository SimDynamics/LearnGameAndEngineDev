cmake_minimum_required(VERSION 3.25)
project(LgaedHostedCourseEditionQA LANGUAGES CXX)

# Separate hosted Windows/MSVC validation, not a build with the pinned tools.
# The user's data, production presets and dependency versions remain untouched.
if(NOT WIN32 OR NOT MSVC OR
   NOT "$ENV{GITHUB_ACTIONS}" STREQUAL "true" OR
   NOT "$ENV{RUNNER_ENVIRONMENT}" STREQUAL "github-hosted")
    message(FATAL_ERROR "Only isolated GitHub-hosted Windows MSVC QA is supported.")
endif()
get_filename_component(_lgaed_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Reuse the original native build header template. This nested QA target
# substitutes the ORIGINAL source root, not its own subdirectory.
set(LGAED_CPP_STANDARD 23)
set(LGAED_CPP_STANDARD_OPTION "/std:c++latest")
set(LGAED_DEFAULT_PROFILE "Codex")
set(LGAED_DEP_ROOT "${CMAKE_BINARY_DIR}/dependencies")
set(LGAED_EDUCATIONAL_VC_ROOT "$ENV{VCToolsInstallDir}")
set(LGAED_EDUCATIONAL_SDK_ROOT "$ENV{WindowsSdkDir}")
# The isolated hosted runner may have an SDK version different from the
# product's pinned Windows SDK. Use its activated SDK only for student-code
# execution in this QA target; never rewrite the production template or pin.
file(TO_CMAKE_PATH "$ENV{WindowsSdkDir}" _lgaed_qa_sdk_root)
set(_lgaed_qa_sdk "$ENV{WindowsSDKVersion}")
string(REGEX REPLACE "[/\\\\]+$" "" _lgaed_qa_sdk "${_lgaed_qa_sdk}")
if(NOT _lgaed_qa_sdk MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$" OR
   NOT IS_DIRECTORY "${_lgaed_qa_sdk_root}/Include/${_lgaed_qa_sdk}/ucrt" OR
   NOT IS_DIRECTORY "${_lgaed_qa_sdk_root}/Lib/${_lgaed_qa_sdk}/um/x64")
    message(FATAL_ERROR "Hosted SDK is not a complete Windows x64 SDK: '${_lgaed_qa_sdk}'")
endif()
message(STATUS "Isolated hosted QA SDK: ${_lgaed_qa_sdk} (product pin unchanged)")
file(READ "${_lgaed_root}/cmake/native_build_config.hpp.in" _header)
string(REPLACE "@CMAKE_SOURCE_DIR@" "${_lgaed_root}" _header "${_header}")
string(REPLACE "educational_sdk_version[] = L\"10.0.28000.0\""
    "educational_sdk_version[] = L\"${_lgaed_qa_sdk}\"" _header "${_header}")
string(CONFIGURE "${_header}" _header @ONLY)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/native_build_config.hpp" "${_header}")

add_library(lgaed_host_options INTERFACE)
target_compile_definitions(lgaed_host_options INTERFACE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_host_options INTERFACE
    /std:c++latest /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
target_include_directories(lgaed_host_options INTERFACE
    "${_lgaed_root}/native" "${CMAKE_CURRENT_BINARY_DIR}/generated")

# Only QA's SOURCE inventory is repeated; production C++ is linked unchanged.
function(_lgaed_absolute_sources output)
    set(_result)
    foreach(_path IN LISTS ARGN)
        list(APPEND _result "${_lgaed_root}/${_path}")
    endforeach()
    set(${output} "${_result}" PARENT_SCOPE)
endfunction()

_lgaed_absolute_sources(_core
    native/core/profile.cpp
    native/core/compile_check.cpp
    native/core/lesson.cpp
    native/core/lesson_state.cpp
    native/core/lesson_page_reach.cpp
    native/core/text_semantics.cpp
    native/core/cpp_project.cpp
    native/core/image_resource.cpp
    native/core/simulation.cpp
    native/core/catalog.cpp
    native/core/audio_schedule.cpp
    native/core/learning_flow.cpp
    native/core/view_preferences.cpp
    native/core/narration_state.cpp
    native/core/scientific_calculator.cpp
    native/core/smo.cpp
    native/core/smo_native.cpp)
add_library(lgaed_core STATIC ${_core})
target_link_libraries(lgaed_core PUBLIC lgaed_host_options)

add_executable(lgaed_cpp_tool_host
    "${_lgaed_root}/native/platform/windows/cpp_tool_host.cpp")
target_link_libraries(lgaed_cpp_tool_host PRIVATE lgaed_host_options user32)
set_target_properties(lgaed_cpp_tool_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

_lgaed_absolute_sources(_platform
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
add_library(lgaed_platform_windows STATIC ${_platform})
set_source_files_properties(
    "${_lgaed_root}/native/platform/windows/content_loader.cpp"
    "${_lgaed_root}/native/platform/windows/cpp_runner.cpp"
    PROPERTIES COMPILE_OPTIONS "/bigobj")
target_link_libraries(lgaed_platform_windows PUBLIC lgaed_core lgaed_host_options)
target_link_libraries(lgaed_platform_windows PRIVATE
    shell32 shlwapi sapi ole32 oleaut32 advapi32 windowsapp runtimeobject bcrypt xmllite)
add_dependencies(lgaed_platform_windows lgaed_cpp_tool_host)

# Link the exact presentation source target shared with the real app.
include("${_lgaed_root}/cmake/CurricularWindows.cmake")
find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
set(_edition_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/course-edition")
set(_manifest "${_edition_dir}/manifest.json")
add_custom_command(OUTPUT "${_manifest}"
    COMMAND "${Python3_EXECUTABLE}" -B
        "${_lgaed_root}/scripts/curriculum/course_edition_fixture_source.py"
        --output-dir "${_edition_dir}"
    DEPENDS "${_lgaed_root}/scripts/curriculum/course_edition_fixture_source.py"
        "${_lgaed_root}/scripts/curriculum/course_edition.py"
        "${_lgaed_root}/scripts/curriculum/stable_state_layout.py"
    VERBATIM)
add_custom_target(lgaed_course_edition_fixtures DEPENDS "${_manifest}")
add_executable(lgaed_course_edition_tests
    "${_lgaed_root}/native/platform/windows/course_edition_tests.cpp")
target_link_libraries(lgaed_course_edition_tests PRIVATE lgaed_curricular_windows)
add_dependencies(lgaed_course_edition_tests lgaed_course_edition_fixtures)

enable_testing()
add_test(NAME native.course_edition
    COMMAND lgaed_course_edition_tests "${_edition_dir}")
set_tests_properties(native.course_edition PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Mount both authored U2L10 predictions through their stable identities and
# reopen the same isolated profile after a real multiline edit.
add_executable(lgaed_lesson_editor_mount_tests
    "${_lgaed_root}/native/presentation/windows/lesson_editor_mount_tests.cpp")
target_link_libraries(lgaed_lesson_editor_mount_tests PRIVATE lgaed_curricular_windows)
add_test(NAME native.prediction_newline_visible
    COMMAND lgaed_lesson_editor_mount_tests --prediction-newline)
set_tests_properties(native.prediction_newline_visible
    PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)

# A published retirement can preserve prose older than an option review.
# Validate both Windows loaders and retained responses against exact Git bytes.
set(_choice_retirement_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/choice-retirement")
set(_choice_retirement_outputs)
foreach(_fixture manifest.json synthetic-before.json synthetic-after.json
        published-before.json published-after.json
        rejected-unknown-option.json rejected-another-option.json
        rejected-correct-index.json rejected-option-count.json
        rejected-option-type.json rejected-unpublished-prose.json)
    list(APPEND _choice_retirement_outputs "${_choice_retirement_dir}/${_fixture}")
endforeach()
add_custom_command(OUTPUT ${_choice_retirement_outputs}
    COMMAND "${Python3_EXECUTABLE}" -B
        "${_lgaed_root}/scripts/curriculum/choice_retirement_fixture_source.py"
        --output-dir "${_choice_retirement_dir}"
    DEPENDS "${_lgaed_root}/scripts/curriculum/choice_retirement_fixture_source.py"
        "${_lgaed_root}/scripts/curriculum/diagnostic_fixture_source.py"
        "${_lgaed_root}/scripts/curriculum/stable_state_layout.py"
        "${_lgaed_root}/scripts/curriculum/text_semantics.py"
    VERBATIM)
add_custom_target(lgaed_choice_retirement_fixtures
    DEPENDS ${_choice_retirement_outputs})
add_executable(lgaed_general_choice_editorial_tests
    "${_lgaed_root}/native/platform/windows/general_choice_editorial_tests.cpp")
target_link_libraries(lgaed_general_choice_editorial_tests PRIVATE lgaed_platform_windows)
add_dependencies(lgaed_general_choice_editorial_tests lgaed_choice_retirement_fixtures)
add_test(NAME native.general_choice_retirement_history
    COMMAND lgaed_general_choice_editorial_tests
        --retirement-history "${_choice_retirement_dir}")
set_tests_properties(native.general_choice_retirement_history
    PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)
include("${_lgaed_root}/cmake/GeneralChoiceProseTests.cmake")
lgaed_add_general_choice_prose_tests("${_lgaed_root}")
include("${_lgaed_root}/cmake/ReaderRevisionTests.cmake")
lgaed_add_reader_revision_tests("${_lgaed_root}")

# Windows-only integration: the original simulation model, its real RichEdit
# controls and its authored example in the production LessonView.
add_executable(lgaed_simulation_tests
    "${_lgaed_root}/native/core/simulation_tests.cpp")
target_link_libraries(lgaed_simulation_tests PRIVATE lgaed_core)
add_executable(lgaed_simulation_view_tests
    "${_lgaed_root}/native/presentation/windows/simulation_view_tests.cpp")
target_link_libraries(lgaed_simulation_view_tests PRIVATE lgaed_curricular_windows)
target_compile_options(lgaed_simulation_view_tests PRIVATE /bigobj)
add_executable(lgaed_simulation_lesson_tests
    "${_lgaed_root}/native/presentation/windows/simulation_lesson_tests.cpp")
target_link_libraries(lgaed_simulation_lesson_tests PRIVATE lgaed_curricular_windows)
add_test(NAME native.simulation_model COMMAND lgaed_simulation_tests)
add_test(NAME native.simulation_empty_text_controls
    COMMAND lgaed_simulation_view_tests --blank-label)
add_test(NAME native.simulation_empty_text_lesson
    COMMAND lgaed_simulation_lesson_tests
    "${_lgaed_root}/docs/examples/array_runs_simulation.json"
    "${_lgaed_root}/native/presentation/windows/fixtures/simulation_state_layout.json"
    "${_lgaed_root}/docs/examples/array_runs_empty_text_simulation.json")
set_tests_properties(native.simulation_model PROPERTIES TIMEOUT 40)
set_tests_properties(native.simulation_empty_text_controls
    native.simulation_empty_text_lesson PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Actual Windows student C++ compilation through the production LessonView.
# The first of thirteen declared cases is required; the other twelve are
# independently executable, never mandatory for progress.
add_executable(lgaed_compiler_first_case_view_tests
    "${_lgaed_root}/native/presentation/windows/compiler_first_case_view_tests.cpp")
target_link_libraries(lgaed_compiler_first_case_view_tests
    PRIVATE lgaed_curricular_windows)
add_test(NAME native.compiler_puzzles COMMAND lgaed_compiler_first_case_view_tests)
set_tests_properties(native.compiler_puzzles PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)

# The active U2 visual-semantic regressions use real Win32 RichEdit and the
# production LessonView. This independent hosted profile does not replace the
# exact pinned MSVC reader and narration suites.
add_executable(lgaed_technical_context_view_tests
    "${_lgaed_root}/native/presentation/windows/technical_context_view_tests.cpp")
target_link_libraries(lgaed_technical_context_view_tests
    PRIVATE lgaed_curricular_windows)
target_compile_options(lgaed_technical_context_view_tests PRIVATE /bigobj)
add_test(NAME native.current_u2_roles
    COMMAND lgaed_technical_context_view_tests --current-u2-roles)
add_test(NAME native.current_u2l5_gates
    COMMAND lgaed_technical_context_view_tests --current-u2l5-native-gates)
# U2L6 has two required C++ editors on one page. Exercise their independent
# real LessonView navigation, persistence, read barriers and references.
add_test(NAME native.current_u2l6_native_gates
    COMMAND lgaed_technical_context_view_tests --current-u2l6-native-gates)
set_tests_properties(native.current_u2_roles native.current_u2l5_gates
    native.current_u2l6_native_gates PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# Check actual U2L6 edition and retained history in isolated hosted Win32 QA.
add_executable(lgaed_reading_gate_policy_tests
    "${_lgaed_root}/native/presentation/windows/reading_gate_policy_tests.cpp")
target_link_libraries(lgaed_reading_gate_policy_tests PRIVATE lgaed_curricular_windows)
add_test(NAME native.current_u2l6_reading_gate COMMAND lgaed_reading_gate_policy_tests)
set_tests_properties(native.current_u2l6_reading_gate PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE)

# Run the existing production StateStore/local-int integration against the
# six current U2 editors using only in-memory authoring proposals. This tests
# real Windows full/progress loaders, sealed receipts and reopen; it neither
# changes the curriculum nor pretends to compile the learner's program.
add_executable(lgaed_local_int_state_tests
    "${_lgaed_root}/native/platform/windows/local_int_state_tests.cpp")
target_link_libraries(lgaed_local_int_state_tests
    PRIVATE lgaed_platform_windows)
add_test(NAME native.current_u2_local_int_state
    COMMAND lgaed_local_int_state_tests)
set_tests_properties(native.current_u2_local_int_state
    PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# The original U2L8 publication is immutable QA input; the authoring proposal
# adds only independent char/bool observations to its existing editor.
set(_u2l8_scalar_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u2l8-scalars")
add_custom_command(OUTPUT "${_u2l8_scalar_dir}/before.json"
        "${_u2l8_scalar_dir}/after.json" "${_u2l8_scalar_dir}/manifest.json"
    COMMAND "${Python3_EXECUTABLE}" -B
        "${_lgaed_root}/scripts/curriculum/local_scalar_fixture_source.py"
        --output-dir "${_u2l8_scalar_dir}"
    DEPENDS "${_lgaed_root}/scripts/curriculum/local_scalar_fixture_source.py"
        "${_lgaed_root}/scripts/curriculum/diagnostic_fixture_source.py"
        "${_lgaed_root}/scripts/curriculum/stable_state_layout.py"
    VERBATIM)
add_custom_target(lgaed_u2l8_scalar_fixtures
    DEPENDS "${_u2l8_scalar_dir}/before.json"
        "${_u2l8_scalar_dir}/after.json" "${_u2l8_scalar_dir}/manifest.json")
add_dependencies(lgaed_local_int_state_tests lgaed_u2l8_scalar_fixtures)
add_test(NAME native.current_u2l8_local_scalar_state
    COMMAND lgaed_local_int_state_tests --u2l8-scalars "${_u2l8_scalar_dir}")
set_tests_properties(native.current_u2l8_local_scalar_state
    PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)

# Test the original U2L7 owners with the unchanged production MSVC runner.
# Keep all prospective curriculum authoring and stored progress in isolated QA.
add_executable(lgaed_local_string_state_tests
    "${_lgaed_root}/native/platform/windows/local_string_state_tests.cpp")
target_link_libraries(lgaed_local_string_state_tests PRIVATE lgaed_platform_windows)
add_test(NAME native.current_u2_local_string_state COMMAND lgaed_local_string_state_tests)
set_tests_properties(native.current_u2_local_string_state PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)

include("${_lgaed_root}/cmake/InputDependencyTests.cmake")
lgaed_add_input_dependency_state_tests("${_lgaed_root}")
lgaed_add_local_role_state_tests("${_lgaed_root}")
lgaed_add_local_dependency_receipt_tests("${_lgaed_root}")
lgaed_add_local_dependency_view_tests("${_lgaed_root}" lgaed_curricular_windows)

add_executable(lgaed_curricular_input_lab_tests
    "${_lgaed_root}/native/presentation/windows/curricular_input_lab_tests.cpp")
target_link_libraries(lgaed_curricular_input_lab_tests PRIVATE lgaed_curricular_windows)
add_test(NAME native.curricular_input_lab COMMAND lgaed_curricular_input_lab_tests)
set_tests_properties(native.curricular_input_lab PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
add_test(NAME native.named_rule_feedback_view
    COMMAND lgaed_curricular_input_lab_tests --named-rule-feedback)
set_tests_properties(native.named_rule_feedback_view
    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
add_test(NAME native.curricular_stdout_transport
    COMMAND lgaed_curricular_input_lab_tests --u3-stdout)
set_tests_properties(native.curricular_stdout_transport
    PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)
add_test(NAME native.curricular_u3_combinations
    COMMAND lgaed_curricular_input_lab_tests --u3-combinations)
set_tests_properties(native.curricular_u3_combinations
    PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)

# Reuse the production loader's real-pilot assertions after the published
# U3L8 editor was retired. These fixtures retain the entire current history;
# archived contracts are resolved by stable identity inside the native test.
set(_current_format_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/current-code-format")
set(_current_format_outputs)
foreach(_fixture real.before.json real.after.json
        after-editorial/real.before.json after-editorial/real.after.json)
    list(APPEND _current_format_outputs "${_current_format_dir}/${_fixture}")
endforeach()
add_custom_command(OUTPUT ${_current_format_outputs}
    COMMAND "${Python3_EXECUTABLE}" -B
        "${_lgaed_root}/scripts/curriculum/test_real_code_format_pilot.py"
        --output-dir "${_current_format_dir}"
    DEPENDS "${_lgaed_root}/scripts/curriculum/test_real_code_format_pilot.py"
        "${_lgaed_root}/scripts/curriculum/code_format_compat.py"
        "${_lgaed_root}/scripts/curriculum/stable_state_layout.py"
        "${_lgaed_root}/app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json"
        "${_lgaed_root}/app/src/main/assets/content/en/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json"
        "${_lgaed_root}/app/src/main/assets/content/fr/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_construir_decisiones_desde_un_problema.json"
    WORKING_DIRECTORY "${_lgaed_root}"
    VERBATIM)
add_custom_target(lgaed_current_code_format_fixtures DEPENDS ${_current_format_outputs})
add_executable(lgaed_code_format_loader_tests
    "${_lgaed_root}/native/platform/windows/code_format_loader_tests.cpp")
target_link_libraries(lgaed_code_format_loader_tests PRIVATE lgaed_platform_windows)
add_dependencies(lgaed_code_format_loader_tests lgaed_current_code_format_fixtures)
add_test(NAME native.code_format_current_pilot
    COMMAND lgaed_code_format_loader_tests --real-pilot "${_current_format_dir}")
set_tests_properties(native.code_format_current_pilot
    PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE)

add_executable(lgaed_exact_prediction_view_tests
    "${_lgaed_root}/native/presentation/windows/exact_prediction_view_tests.cpp")
target_link_libraries(lgaed_exact_prediction_view_tests PRIVATE lgaed_curricular_windows)
add_test(NAME native.prepared_input_predictions
    COMMAND lgaed_exact_prediction_view_tests --prepared-input)
set_tests_properties(native.prepared_input_predictions PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
lgaed_add_u2l9_input_view_history_tests("${_lgaed_root}")

# Test the nine current inline-answer Win32 controls through the original
# production LessonView translation unit (no copied mock implementation).
# The fixture's prior completions are modelled, NOT student executions or
# proof of narration playback. This is hosted UI/StateStore QA only.
add_executable(lgaed_inline_feedback_view_tests
    "${_lgaed_root}/native/presentation/windows/lesson_view_tests.cpp")
target_compile_definitions(lgaed_inline_feedback_view_tests
    PRIVATE LGAED_INLINE_FEEDBACK_ONLY=1)
target_link_libraries(lgaed_inline_feedback_view_tests
    PRIVATE lgaed_curricular_windows)
target_compile_options(lgaed_inline_feedback_view_tests PRIVATE /bigobj)
add_test(NAME native.current_inline_feedback
    COMMAND lgaed_inline_feedback_view_tests --inline-feedback)
set_tests_properties(native.current_inline_feedback
    PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)

# Native Windows case revision StateStore and preserved aggregate QA.
set(_u6l19_dir "${CMAKE_CURRENT_BINARY_DIR}/test-fixtures/u6l19-case-split")
add_custom_command(OUTPUT "${_u6l19_dir}/before.json" "${_u6l19_dir}/after.json"
    COMMAND "${Python3_EXECUTABLE}" -B
        "${_lgaed_root}/scripts/curriculum/test_current_batched_client_split.py"
        --export-native-qa "${_u6l19_dir}"
    DEPENDS "${_lgaed_root}/scripts/curriculum/test_current_batched_client_split.py"
        "${_lgaed_root}/scripts/curriculum/stable_state_layout.py"
        "${_lgaed_root}/app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json"
    VERBATIM)
add_custom_target(lgaed_u6l19_case_split_fixtures
    DEPENDS "${_u6l19_dir}/before.json" "${_u6l19_dir}/after.json")
add_executable(lgaed_u6l19_case_split_state_tests
    "${_lgaed_root}/native/platform/windows/u6l19_case_split_state_tests.cpp")
target_link_libraries(lgaed_u6l19_case_split_state_tests PRIVATE lgaed_platform_windows)
add_dependencies(lgaed_u6l19_case_split_state_tests lgaed_u6l19_case_split_fixtures)
add_test(NAME native.u6l19_case_split_state
    COMMAND lgaed_u6l19_case_split_state_tests
       "${_u6l19_dir}/before.json" "${_u6l19_dir}/after.json"
       "${_u6l19_dir}/isolated-profile")
set_tests_properties(native.u6l19_case_split_state PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE)

# U3 diagnostics and reports: compile and execute the original MSVC runner
# with source-authenticated, in-memory root objectives. This is the full,
# unchanged Windows production worker, not a modeled compiler receipt.
add_executable(lgaed_u3_runtime_semantics_tests
    "${_lgaed_root}/native/platform/windows/u3_runtime_semantics_tests.cpp")
target_link_libraries(lgaed_u3_runtime_semantics_tests PRIVATE lgaed_platform_windows)
add_test(NAME native.u3_runtime_semantics
    COMMAND lgaed_u3_runtime_semantics_tests)
set_tests_properties(native.u3_runtime_semantics
    PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)

# Reuse the genuine U3 source and counterfactual runner on hosted Windows.
# Keep the full pinned CMake/battery QA entrypoint unchanged: this focused
# mode exercises local-constant provenance with actual MSVC compilations.
add_executable(lgaed_u3_named_rule_runtime_tests
    "${_lgaed_root}/native/platform/windows/named_rule_runtime_tests.cpp")
target_link_libraries(lgaed_u3_named_rule_runtime_tests PRIVATE lgaed_platform_windows)
target_compile_options(lgaed_u3_named_rule_runtime_tests PRIVATE /bigobj)
add_test(NAME native.u3_named_rules_single_file
    COMMAND lgaed_u3_named_rule_runtime_tests --single-file
        "${_lgaed_root}/app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles/lesson_nombrar_reglas_y_separar_acuerdos.json")
set_tests_properties(native.u3_named_rules_single_file PROPERTIES TIMEOUT 420 RUN_SERIAL TRUE)




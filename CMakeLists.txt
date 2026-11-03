cmake_minimum_required(VERSION 4.4.5)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use a build preset; in-source builds would mix generated files with imported content.")
endif()

project(LearnGameAndEngineDev VERSION 0.1.0 LANGUAGES CXX RC)

include(cmake/VerifyNativeToolchain.cmake)
lgaed_verify_native_toolchain()
include(cmake/VerifyNativeDependencies.cmake)
lgaed_verify_native_dependencies()
include(cmake/WindowsRuntimeTools.cmake)
lgaed_select_windows_runtime_tools()

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

add_library(lgaed_core STATIC native/core/profile.cpp native/core/compile_check.cpp native/core/lesson.cpp native/core/lesson_state.cpp native/core/cpp_project.cpp
    native/core/catalog.cpp native/core/audio_schedule.cpp native/core/learning_flow.cpp native/core/view_preferences.cpp native/core/narration_state.cpp native/core/scientific_calculator.cpp native/core/smo.cpp native/core/smo_native.cpp)
target_link_libraries(lgaed_core PUBLIC lgaed_host_options)

# Pure numerical references can be verified without preparing graphics assets
# or creating a GPU device. The application links these same implementations.
add_library(lgaed_smo_arithmetic STATIC
    native/presentation/windows/smo_rule_contract.cpp
    native/presentation/windows/smo_duodecimal.cpp
    native/presentation/windows/smo_float12.cpp
    native/presentation/windows/smo_numeric_scene_reference.cpp)
target_link_libraries(lgaed_smo_arithmetic PUBLIC lgaed_core lgaed_host_options)

add_library(lgaed_platform_windows STATIC
    native/platform/windows/environment.cpp
    native/platform/windows/profile_migration.cpp
    native/platform/windows/content_loader.cpp
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
add_executable(lgaed_system_speech_host native/platform/windows/system_speech_host_main.cpp)
target_link_libraries(lgaed_system_speech_host PRIVATE lgaed_platform_windows)
set_target_properties(lgaed_system_speech_host PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

add_library(lgaed_presentation_windows STATIC
    native/presentation/windows/window_fullscreen.cpp
    native/presentation/windows/window_frame.cpp
    native/presentation/windows/internal_window.cpp
    native/presentation/windows/lesson_view.cpp
    native/presentation/windows/cpp_project_view.cpp
    native/presentation/windows/cpp_fill_view.cpp
    native/presentation/windows/order_code_view.cpp
    native/presentation/windows/math_puzzle_view.cpp
    native/presentation/windows/math_expression_editor.cpp
    native/presentation/windows/native_math_text.cpp
    native/presentation/windows/catalog_view.cpp
    native/presentation/windows/card_list.cpp
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
    native/presentation/windows/settings_view.cpp
    native/presentation/windows/audio_course_view.cpp
    native/presentation/windows/narration_licenses.cpp
    native/presentation/windows/reading_shade.cpp
    native/presentation/windows/lesson_reader.cpp
    native/presentation/windows/icons.cpp
    native/presentation/windows/native_menu.cpp
    native/presentation/windows/rich_text.cpp
    native/presentation/windows/reader_theme.cpp
    native/presentation/windows/editing_bar.cpp
    native/presentation/windows/code_gutter.cpp
    native/presentation/windows/code_editor.cpp)
target_link_libraries(lgaed_presentation_windows PUBLIC
    lgaed_platform_windows lgaed_smo_arithmetic lgaed_host_options user32 gdi32 gdiplus d3d11 d3d12 dxgi d3dcompiler shell32 ole32 oleacc advapi32 comctl32 comdlg32 uuid uxtheme dwmapi)
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
# Standard controls and trackbars require the official v6 activation context.
# Propagate it to every consumer, including the actual UI regression hosts.
target_link_options(lgaed_presentation_windows INTERFACE
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'")
add_executable(LearnGameAndEngineDev WIN32
    native/presentation/windows/main.cpp
    native/presentation/windows/app.manifest)
target_link_libraries(LearnGameAndEngineDev PRIVATE
    lgaed_presentation_windows)
# The normal native build prepares the shared linguistic runtime too. Its
# source/hash check is idempotent and never synthesizes or replaces recordings.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(lgaed_narration_runtime
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/build_worker.py"
        --java-home "${LGAED_JAVA_HOME}" --ffmpeg "${LGAED_FFMPEG_EXECUTABLE}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Preparing the shared Kokoro and mathematical runtime"
    VERBATIM)
add_dependencies(LearnGameAndEngineDev lgaed_narration_runtime)
add_dependencies(LearnGameAndEngineDev lgaed_system_speech_host)
set_target_properties(LearnGameAndEngineDev PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

include(CTest)
option(LGAED_BUILD_SMO_RENDER_REFERENCE_TESTS
    "Build and register the optional cached/uncached SMO render benchmarks by default" OFF)
if(BUILD_TESTING)
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
    add_test(NAME native.technical_context_view COMMAND lgaed_technical_context_view_tests)
    set_tests_properties(native.technical_context_view PROPERTIES TIMEOUT 120)
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
    add_test(NAME native.prepared_input_predictions COMMAND lgaed_exact_prediction_view_tests --prepared-input)
    set_tests_properties(native.prepared_input_predictions PROPERTIES TIMEOUT 90)
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
    add_executable(lgaed_cpp_fill_view_tests native/presentation/windows/cpp_fill_view_tests.cpp)
    target_link_libraries(lgaed_cpp_fill_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.cpp_fill_view COMMAND lgaed_cpp_fill_view_tests)
    set_tests_properties(native.cpp_fill_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_order_code_view_tests native/presentation/windows/order_code_view_tests.cpp
        native/presentation/windows/app.manifest)
    target_link_libraries(lgaed_order_code_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.order_code_view COMMAND lgaed_order_code_view_tests)
    set_tests_properties(native.order_code_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_lesson_reader_tests native/presentation/windows/lesson_reader_tests.cpp)
    target_link_libraries(lgaed_lesson_reader_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_lesson_reader_tests lgaed_narration_runtime)
    add_test(NAME native.curricular_color_reader COMMAND lgaed_lesson_reader_tests --curricular-colors)
    set_tests_properties(native.curricular_color_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    add_executable(lgaed_current_reference_reader_tests native/presentation/windows/current_reference_reader_tests.cpp)
    target_link_libraries(lgaed_current_reference_reader_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_current_reference_reader_tests lgaed_narration_runtime lgaed_cpp_tool_host)
    add_test(NAME native.current_reference_reader COMMAND lgaed_current_reference_reader_tests)
    set_tests_properties(native.current_reference_reader PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    foreach(inline_case RANGE 1 9)
        add_test(NAME native.current_inline_feedback_${inline_case}
            COMMAND lgaed_lesson_reader_tests --current-inline-feedback ${inline_case})
        set_tests_properties(native.current_inline_feedback_${inline_case} PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME native.lesson_reader COMMAND lgaed_lesson_reader_tests)
    # Includes full real playback for the added prerequisite/bookmark cases.
    set_tests_properties(native.lesson_reader PROPERTIES TIMEOUT 360)
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
    add_executable(lgaed_narration_player_tests native/platform/windows/narration_player_tests.cpp)
    target_link_libraries(lgaed_narration_player_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.narration_player_rates COMMAND lgaed_narration_player_tests "${CMAKE_SOURCE_DIR}/out/qa/narration-playback-rate")
    set_tests_properties(native.narration_player_rates PROPERTIES TIMEOUT 45)
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
    add_executable(lgaed_narration_worker_tests native/platform/windows/narration_worker_tests.cpp)
    target_link_libraries(lgaed_narration_worker_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_narration_worker_tests lgaed_narration_runtime)
    add_test(NAME native.narration_worker COMMAND lgaed_narration_worker_tests "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME native.windows_symbol_speech COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_windows_symbol_speech.py")
    set_tests_properties(native.windows_symbol_speech PROPERTIES TIMEOUT 180)
    add_test(NAME native.spanish_prose_boundaries COMMAND "${Python3_EXECUTABLE}" -B
        "${CMAKE_SOURCE_DIR}/native/narration/worker/test_spanish_prose_boundaries.py")
    set_tests_properties(native.spanish_prose_boundaries PROPERTIES TIMEOUT 360 RUN_SERIAL TRUE)
    add_test(NAME native.math_editor_worker COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_math_editor.py")
    add_test(NAME native.math_calculator_task COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_calculator_task.py")
    add_test(NAME native.worker_dependencies COMMAND "${Python3_EXECUTABLE}" -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/native/narration/worker" -p test_worker_dependencies.py)
    set_tests_properties(native.narration_worker PROPERTIES TIMEOUT 180)
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
    add_executable(lgaed_state_tests native/core/lesson_state_tests.cpp)
    target_link_libraries(lgaed_state_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state")
    add_executable(lgaed_rich_text_tests native/presentation/windows/rich_text_tests.cpp
        native/presentation/windows/rich_text.cpp native/presentation/windows/reader_theme.cpp)
    target_link_libraries(lgaed_rich_text_tests PRIVATE lgaed_platform_windows lgaed_host_options user32 gdi32 uxtheme ole32 oleaut32 comctl32 uuid)
    add_test(NAME native.rich_text COMMAND lgaed_rich_text_tests)
    add_test(NAME native.curricular_text_roles COMMAND lgaed_rich_text_tests --curricular-roles)
    set_tests_properties(native.curricular_text_roles PROPERTIES TIMEOUT 60)
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
    add_test(NAME native.lesson_view COMMAND lgaed_lesson_view_tests)
    set_tests_properties(native.lesson_view PROPERTIES TIMEOUT 180)
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
    add_test(NAME native.curricular_inline_feedback COMMAND lgaed_lesson_view_tests --inline-feedback)
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
        lgaed_rich_text_tests lgaed_reading_shade_tests lgaed_technical_context_view_tests
        lgaed_lesson_reader_tests lgaed_current_reference_reader_tests lgaed_cpp_fill_view_tests
        lgaed_math_puzzle_view_tests lgaed_window_frame_tests lgaed_window_fullscreen_tests lgaed_catalog_view_tests)
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

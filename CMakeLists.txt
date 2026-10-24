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
set_property(GLOBAL APPEND PROPERTY JOB_POOLS smo_hlsl_compile=2)

add_library(lgaed_core STATIC native/core/profile.cpp native/core/lesson.cpp native/core/lesson_state.cpp native/core/cpp_project.cpp
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
    native/platform/windows/content_loader.cpp
    native/platform/windows/narration_preferences.cpp
    native/platform/windows/catalog_loader.cpp
    native/platform/windows/audio_generation.cpp
    native/platform/windows/narration_worker.cpp
    native/platform/windows/narration_cpu_policy.cpp
    native/platform/windows/narration_player.cpp
    native/platform/windows/system_speech.cpp
    native/platform/windows/state_store.cpp
    native/platform/windows/android_import.cpp
    native/platform/windows/cpp_project_history.cpp
    native/platform/windows/cpp_runner.cpp)
target_link_libraries(lgaed_platform_windows PUBLIC lgaed_core lgaed_host_options)
add_dependencies(lgaed_platform_windows lgaed_cpp_tool_host)
target_link_libraries(lgaed_platform_windows PRIVATE shell32 shlwapi sapi ole32 oleaut32 advapi32 windowsapp runtimeobject bcrypt xmllite)

add_library(lgaed_presentation_windows STATIC
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
    lgaed_platform_windows lgaed_smo_arithmetic lgaed_host_options user32 gdi32 gdiplus d3d11 d3d12 dxgi d3dcompiler shell32 ole32 oleacc advapi32 comctl32 comdlg32 uuid)
add_executable(lgaed_smo_compile native/tools/smo_compile.cpp)
target_link_libraries(lgaed_smo_compile PRIVATE lgaed_presentation_windows)
set_target_properties(lgaed_smo_compile PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
include(cmake/SmoSceneShaders.cmake)
lgaed_prepare_smo_shaders(lgaed_presentation_windows)
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
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Preparing the shared Kokoro and mathematical runtime"
    VERBATIM)
add_dependencies(LearnGameAndEngineDev lgaed_narration_runtime)
set_target_properties(LearnGameAndEngineDev PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")

include(CTest)
option(LGAED_BUILD_SMO_RENDER_REFERENCE_TESTS
    "Build and register the optional cached/uncached SMO render benchmarks by default" OFF)
if(BUILD_TESTING)
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
    add_dependencies(lgaed_audio_generation_tests lgaed_narration_runtime)
    add_test(NAME native.audio_generation COMMAND lgaed_audio_generation_tests "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(native.audio_generation PROPERTIES TIMEOUT 90)
    add_executable(lgaed_narration_preferences_tests native/platform/windows/narration_preferences_tests.cpp)
    target_link_libraries(lgaed_narration_preferences_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_narration_preferences_tests lgaed_narration_runtime)
    add_test(NAME native.narration_preferences COMMAND lgaed_narration_preferences_tests)
    add_executable(lgaed_scrollbar_tests native/presentation/windows/scrollbar_tests.cpp)
    target_link_libraries(lgaed_scrollbar_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.scrollbars COMMAND lgaed_scrollbar_tests)
    set_tests_properties(native.scrollbars PROPERTIES TIMEOUT 30)
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
    add_executable(lgaed_card_list_tests native/presentation/windows/card_list_tests.cpp)
    target_link_libraries(lgaed_card_list_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.card_list COMMAND lgaed_card_list_tests)
    set_tests_properties(native.card_list PROPERTIES TIMEOUT 30)
    add_executable(lgaed_code_gutter_tests native/presentation/windows/code_gutter_tests.cpp)
    target_link_libraries(lgaed_code_gutter_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.code_gutter COMMAND lgaed_code_gutter_tests)
    set_tests_properties(native.code_gutter PROPERTIES TIMEOUT 30)
    add_executable(lgaed_audio_course_view_tests native/presentation/windows/audio_course_view_tests.cpp)
    target_link_libraries(lgaed_audio_course_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.audio_course_view COMMAND lgaed_audio_course_view_tests "${CMAKE_SOURCE_DIR}/out/qa")
    set_tests_properties(native.audio_course_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_settings_view_tests native/presentation/windows/settings_view_tests.cpp)
    target_link_libraries(lgaed_settings_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_settings_view_tests lgaed_narration_runtime)
    add_test(NAME native.settings_view COMMAND lgaed_settings_view_tests --case controls)
    add_test(NAME native.narration_licenses COMMAND lgaed_settings_view_tests --case licenses)
    set_tests_properties(native.settings_view native.narration_licenses PROPERTIES TIMEOUT 40)
    add_executable(lgaed_lesson_composition_tests native/presentation/windows/lesson_composition_tests.cpp)
    target_link_libraries(lgaed_lesson_composition_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_lesson_composition_tests lgaed_narration_runtime)
    add_test(NAME native.lesson_composition COMMAND lgaed_lesson_composition_tests)
    set_tests_properties(native.lesson_composition PROPERTIES TIMEOUT 120)
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
    add_test(NAME native.lesson_reader COMMAND lgaed_lesson_reader_tests)
    set_tests_properties(native.lesson_reader PROPERTIES TIMEOUT 240)
    add_executable(lgaed_math_puzzle_view_tests native/presentation/windows/math_puzzle_view_tests.cpp)
    target_link_libraries(lgaed_math_puzzle_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_puzzle_view_tests lgaed_narration_runtime)
    add_test(NAME native.math_puzzle_view COMMAND lgaed_math_puzzle_view_tests)
    set_tests_properties(native.math_puzzle_view PROPERTIES TIMEOUT 180)
    add_executable(lgaed_math_expression_editor_tests native/presentation/windows/math_expression_editor_tests.cpp)
    target_link_libraries(lgaed_math_expression_editor_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_math_expression_editor_tests lgaed_narration_runtime)
    add_test(NAME native.math_expression_editor COMMAND lgaed_math_expression_editor_tests)
    set_tests_properties(native.math_expression_editor PROPERTIES TIMEOUT 90)
    add_executable(lgaed_narration_worker_tests native/platform/windows/narration_worker_tests.cpp)
    target_link_libraries(lgaed_narration_worker_tests PRIVATE lgaed_platform_windows)
    add_dependencies(lgaed_narration_worker_tests lgaed_narration_runtime)
    add_test(NAME native.narration_worker COMMAND lgaed_narration_worker_tests "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME native.math_editor_worker COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_math_editor.py")
    add_test(NAME native.math_calculator_task COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/native/narration/worker/test_calculator_task.py")
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
        lgaed_smo_native_performance_tests lgaed_smo_native_render_performance_tests lgaed_smo_f12_render_performance_tests)
    add_executable(lgaed_smo_view_tests native/presentation/windows/smo_view_tests.cpp)
    target_link_libraries(lgaed_smo_view_tests PRIVATE lgaed_presentation_windows)
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
    set_tests_properties(native.tools_view PROPERTIES TIMEOUT 90)
    add_executable(lgaed_catalog_tests native/core/catalog_tests.cpp)
    target_link_libraries(lgaed_catalog_tests PRIVATE lgaed_platform_windows)
    add_test(NAME native.catalog COMMAND lgaed_catalog_tests)
    set_tests_properties(native.catalog PROPERTIES TIMEOUT 30)
    add_executable(lgaed_catalog_view_tests native/presentation/windows/catalog_view_tests.cpp)
    target_link_libraries(lgaed_catalog_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.catalog_view COMMAND lgaed_catalog_view_tests)
    set_tests_properties(native.catalog_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_narration_state_tests native/core/narration_state_tests.cpp)
    target_link_libraries(lgaed_narration_state_tests PRIVATE lgaed_core)
    add_test(NAME native.narration_state COMMAND lgaed_narration_state_tests)
    add_executable(lgaed_profile_tests native/core/profile_tests.cpp)
    target_link_libraries(lgaed_profile_tests PRIVATE lgaed_core lgaed_host_options)
    add_test(NAME native.profile COMMAND lgaed_profile_tests)
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
    add_test(NAME native.cpp_project_view COMMAND lgaed_cpp_project_view_tests "${CMAKE_SOURCE_DIR}/out/qa/cpp-project-history")
    set_tests_properties(native.cpp_project_view PROPERTIES TIMEOUT 120)
    add_executable(lgaed_state_tests native/core/lesson_state_tests.cpp)
    target_link_libraries(lgaed_state_tests PRIVATE lgaed_platform_windows lgaed_host_options)
    add_test(NAME native.state_import COMMAND lgaed_state_tests "${CMAKE_SOURCE_DIR}/out/qa/state")
    add_executable(lgaed_rich_text_tests native/presentation/windows/rich_text_tests.cpp
        native/presentation/windows/rich_text.cpp native/presentation/windows/reader_theme.cpp)
    target_link_libraries(lgaed_rich_text_tests PRIVATE lgaed_platform_windows lgaed_host_options user32 gdi32 ole32 oleaut32 comctl32 uuid)
    add_test(NAME native.rich_text COMMAND lgaed_rich_text_tests)
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
    add_test(NAME native.lesson_view COMMAND lgaed_lesson_view_tests)
    set_tests_properties(native.lesson_view PROPERTIES TIMEOUT 180)
    add_test(NAME native.lesson_caret COMMAND lgaed_lesson_view_tests --caret)
    set_tests_properties(native.lesson_caret PROPERTIES TIMEOUT 180)
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
endif()

install(TARGETS LearnGameAndEngineDev lgaed_cpp_tool_host RUNTIME DESTINATION bin)
install(FILES native/assets/fonts/NotoSerif-Regular.ttf native/assets/fonts/NotoSerif-Italic.ttf
    native/assets/fonts/NOTICE.txt native/assets/fonts/DroidSansMono.ttf
    native/assets/fonts/DroidSansMono-NOTICE.txt DESTINATION bin/fonts)
message(STATUS "Development profile: ${LGAED_DEFAULT_PROFILE}; build and install trees belong to this checkout.")

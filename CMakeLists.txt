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

add_library(lgaed_core STATIC native/core/profile.cpp native/core/lesson.cpp native/core/lesson_state.cpp
    native/core/catalog.cpp native/core/learning_flow.cpp native/core/view_preferences.cpp native/core/narration_state.cpp native/core/scientific_calculator.cpp)
target_link_libraries(lgaed_core PUBLIC lgaed_host_options)

add_library(lgaed_platform_windows STATIC
    native/platform/windows/environment.cpp
    native/platform/windows/content_loader.cpp
    native/platform/windows/catalog_loader.cpp
    native/platform/windows/narration_worker.cpp
    native/platform/windows/narration_player.cpp
    native/platform/windows/system_speech.cpp
    native/platform/windows/state_store.cpp
    native/platform/windows/android_import.cpp
    native/platform/windows/cpp_runner.cpp)
target_link_libraries(lgaed_platform_windows PUBLIC lgaed_core lgaed_host_options)
target_link_libraries(lgaed_platform_windows PRIVATE shell32 shlwapi sapi ole32 oleaut32 advapi32 windowsapp runtimeobject bcrypt xmllite)

add_library(lgaed_presentation_windows STATIC
    native/presentation/windows/lesson_view.cpp
    native/presentation/windows/cpp_fill_view.cpp
    native/presentation/windows/math_puzzle_view.cpp
    native/presentation/windows/math_expression_editor.cpp
    native/presentation/windows/native_math_text.cpp
    native/presentation/windows/catalog_view.cpp
    native/presentation/windows/card_list.cpp
    native/presentation/windows/tools_view.cpp
    native/presentation/windows/settings_view.cpp
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
    lgaed_platform_windows lgaed_host_options user32 gdi32 gdiplus d3d11 dxgi d3dcompiler shell32 ole32 oleacc advapi32 comctl32 comdlg32 uuid)
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
if(BUILD_TESTING)
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
    add_executable(lgaed_settings_view_tests native/presentation/windows/settings_view_tests.cpp)
    target_link_libraries(lgaed_settings_view_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_settings_view_tests lgaed_narration_runtime)
    add_test(NAME native.settings_view COMMAND lgaed_settings_view_tests)
    set_tests_properties(native.settings_view PROPERTIES TIMEOUT 40)
    add_executable(lgaed_lesson_composition_tests native/presentation/windows/lesson_composition_tests.cpp)
    target_link_libraries(lgaed_lesson_composition_tests PRIVATE lgaed_presentation_windows)
    add_dependencies(lgaed_lesson_composition_tests lgaed_narration_runtime)
    add_test(NAME native.lesson_composition COMMAND lgaed_lesson_composition_tests)
    set_tests_properties(native.lesson_composition PROPERTIES TIMEOUT 120)
    add_executable(lgaed_cpp_fill_view_tests native/presentation/windows/cpp_fill_view_tests.cpp)
    target_link_libraries(lgaed_cpp_fill_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.cpp_fill_view COMMAND lgaed_cpp_fill_view_tests)
    set_tests_properties(native.cpp_fill_view PROPERTIES TIMEOUT 40)
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
    add_executable(lgaed_import_view_tests native/presentation/windows/import_view_tests.cpp)
    target_link_libraries(lgaed_import_view_tests PRIVATE lgaed_presentation_windows)
    add_test(NAME native.import_view COMMAND lgaed_import_view_tests)
    set_tests_properties(native.import_view PROPERTIES TIMEOUT 180)
endif()

install(TARGETS LearnGameAndEngineDev RUNTIME DESTINATION bin)
install(FILES native/assets/fonts/NotoSerif-Regular.ttf native/assets/fonts/NotoSerif-Italic.ttf
    native/assets/fonts/NOTICE.txt native/assets/fonts/DroidSansMono.ttf
    native/assets/fonts/DroidSansMono-NOTICE.txt DESTINATION bin/fonts)
message(STATUS "Development profile: ${LGAED_DEFAULT_PROFILE}; build and install trees belong to this checkout.")

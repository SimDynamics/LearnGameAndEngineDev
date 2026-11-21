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
file(READ "${_lgaed_root}/cmake/native_build_config.hpp.in" _header)
string(REPLACE "@CMAKE_SOURCE_DIR@" "${_lgaed_root}" _header "${_header}")
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

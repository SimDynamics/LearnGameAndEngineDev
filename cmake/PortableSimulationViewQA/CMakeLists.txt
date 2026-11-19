cmake_minimum_required(VERSION 3.25)
project(LGAEDPortableSimulationViewQA LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "Focused native GUI test requires Windows and MSVC.")
endif()

# This checks the REAL simulation model, view and reading primitives.
# The pinned full-application toolchain and installation are left intact.
get_filename_component(LGAED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LGAED_CPP_STANDARD 23)
set(LGAED_CPP_STANDARD_OPTION "/std:c++latest")
set(LGAED_DEFAULT_PROFILE "Codex")
set(LGAED_DEP_ROOT "")
set(LGAED_EDUCATIONAL_VC_ROOT "")
set(LGAED_EDUCATIONAL_SDK_ROOT "")
configure_file("${LGAED_ROOT}/cmake/native_build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/native_build_config.hpp" @ONLY)

add_executable(lgaed_simulation_view_portable_qa
    "${LGAED_ROOT}/native/core/simulation.cpp"
    "${LGAED_ROOT}/native/core/lesson_state.cpp"
    "${LGAED_ROOT}/native/core/text_semantics.cpp"
    "${LGAED_ROOT}/native/presentation/windows/reader_theme.cpp"
    "${LGAED_ROOT}/native/presentation/windows/rich_text.cpp"
    "${LGAED_ROOT}/native/presentation/windows/simulation_view.cpp"
    "${LGAED_ROOT}/native/presentation/windows/simulation_view_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/accessibility_guids.cpp")
target_include_directories(lgaed_simulation_view_portable_qa PRIVATE
    "${LGAED_ROOT}/native"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_definitions(lgaed_simulation_view_portable_qa PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_compile_options(lgaed_simulation_view_portable_qa PRIVATE
    /std:c++latest /EHsc /W4 /permissive- /Zc:__cplusplus /utf-8 /Gy)
target_link_libraries(lgaed_simulation_view_portable_qa PRIVATE
    user32 gdi32 gdiplus shell32 ole32 oleaut32 oleacc advapi32
    comctl32 comdlg32 uuid uxtheme dwmapi)
target_link_options(lgaed_simulation_view_portable_qa PRIVATE
    /OPT:REF
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'")
enable_testing()
add_test(NAME native.simulation_blank_button
    COMMAND lgaed_simulation_view_portable_qa --blank-label)
set_tests_properties(native.simulation_blank_button PROPERTIES TIMEOUT 60)

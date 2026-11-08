# Shared production controls used by the application and the original reader
# test executables. They do not consume the SMO shaders or numerical backends.
include_guard(GLOBAL)

add_library(lgaed_curricular_windows STATIC
    native/presentation/windows/window_fullscreen.cpp
    native/presentation/windows/internal_window.cpp
    native/presentation/windows/lesson_view.cpp
    native/presentation/windows/cpp_project_view.cpp
    native/presentation/windows/cpp_fill_view.cpp
    native/presentation/windows/order_code_view.cpp
    native/presentation/windows/curricular_image_view.cpp
    native/presentation/windows/math_puzzle_view.cpp
    native/presentation/windows/math_expression_editor.cpp
    native/presentation/windows/native_math_text.cpp
    native/presentation/windows/card_list.cpp
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
target_link_libraries(lgaed_curricular_windows PUBLIC
    lgaed_platform_windows lgaed_host_options user32 gdi32 gdiplus shell32 ole32 oleacc advapi32 comctl32 comdlg32 uuid uxtheme dwmapi)

# Standard controls and trackbars require the official v6 activation context.
# Propagate the existing manifest dependency to the application and every
# curricular consumer, including the actual UI regression hosts.
target_link_options(lgaed_curricular_windows INTERFACE
    "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'")

# Keep the complete original test translation units and output locations.
# The normal build attaches its unchanged runtime/fixture dependencies and
# CTest commands. ReaderBuild calls only this compilation/link definition.
function(lgaed_add_curricular_reader_test_targets)
    add_executable(lgaed_lesson_reader_tests native/presentation/windows/lesson_reader_tests.cpp)
    target_link_libraries(lgaed_lesson_reader_tests PRIVATE lgaed_curricular_windows)
    add_executable(lgaed_u11_declarations_reader_tests native/presentation/windows/u11_declarations_reader_tests.cpp)
    target_link_libraries(lgaed_u11_declarations_reader_tests PRIVATE lgaed_curricular_windows)
    add_executable(lgaed_u6_diagnostic_flow_tests native/presentation/windows/u6_diagnostic_flow_tests.cpp)
    target_link_libraries(lgaed_u6_diagnostic_flow_tests PRIVATE lgaed_curricular_windows)
endfunction()

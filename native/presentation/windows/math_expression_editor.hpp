#pragma once

#include <windows.h>
#include <functional>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::presentation {
struct MathExpressionEditorAccess {
    // One shared canonical Kotlin MathExpressionModel session per control.
    std::function<std::string(std::string_view)> request;
    std::function<void()> changed, submit;
    std::function<void(bool)> focus_changed;
    std::string expected_template;
    std::string language="es";
    bool accent_border=false;
    std::function<void()> cancel;
};
struct MathExpressionCaret {
    std::string path_json;
    std::size_t offset=0;
    int leaf_text_offset=-1;
    RECT bounds{};
};
struct MathExpressionTextPosition {std::string text;float baseline=0,size=0;};
struct MathExpressionEditorSnapshot {
    std::string latex,draft_json,expression_json,cursor_json,presentation_json,last_error;
    std::uint64_t revision=0;
    bool complete=false,can_previous=false,can_next=false;
    int measured_height=0,measured_width=0;
    std::vector<MathExpressionCaret> carets;
    std::vector<MathExpressionTextPosition> text_positions;
    std::vector<float> horizontal_rules;
};
[[nodiscard]] HWND create_math_expression_editor(HWND parent,int id,MathExpressionEditorAccess);
bool set_math_expression_latex(HWND,std::string_view latex);
bool restore_math_expression_draft(HWND,std::string_view draft_json);
bool perform_math_expression_key(HWND,std::string_view key,std::string_view name={});
bool insert_math_expression_text(HWND,std::string_view text);
[[nodiscard]] MathExpressionEditorSnapshot inspect_math_expression_editor(HWND);
[[nodiscard]] int math_expression_editor_height(HWND);
// Read-only results use the same recursive AST layout and canonical import,
// without introducing a separate plain-text formula representation.
void set_math_expression_read_only(HWND,bool);
// One nonactivating overlay per native root; never reserves a lesson footer.
// request dispatches math_keyboard to the same worker's lightweight lane.
void show_math_keyboard(HWND target,std::function<std::string(std::string_view)> request,
    std::string_view configuration_json,std::string_view language="es");
void hide_math_keyboard(HWND target=nullptr);
[[nodiscard]] HWND math_keyboard_window(HWND root);
} // namespace lgaed::presentation

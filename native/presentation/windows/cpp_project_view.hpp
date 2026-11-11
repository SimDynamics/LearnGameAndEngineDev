#pragma once
#include "core/lesson.hpp"
#include "presentation/windows/code_editor.hpp"
#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/cpp_runner.hpp"
#include <windows.h>
#include <functional>
#include <memory>
#include <string>

namespace lgaed::presentation {
struct CppProjectCallbacks {
    std::function<void(std::string project_json,std::string cmake_json)> save;
    // Selecting another file/cursor does not change the compiled snapshot.
    std::function<void(bool source_changed)> changed;
    std::function<void(windows::CppRunRequest::Action)> run;
    // Fired only after the existing UI actually applies a verified recovery.
    // This is one observation, never a complete curricular achievement.
    std::function<void(const windows::CppHistoryRecoveryEvidence&)> restored_file;
    // Emitted when the app actually displays a comparison from the selected
    // persisted revision to the captured draft. Viewing it is not grading.
    std::function<void(const windows::CppHistoryComparisonEvidence&)> compared_file;
    // Emitted only after the revision has been written atomically and
    // reread from disk. It never marks a curriculum objective completed.
    std::function<void(const windows::CppHistoryRecordEvidence&)> recorded_revision;
};
class CppProjectView {
public:
    CppProjectView(HWND parent,const curriculum::Block&,HWND source,windows::CodeEditor&,
        std::string_view project_json,std::string_view cmake_json,
        std::filesystem::path history_root,std::string identity,std::string language,
        CppProjectCallbacks);
    ~CppProjectView();
    [[nodiscard]] HWND window()const;
    [[nodiscard]] HWND configuration_window()const;
    [[nodiscard]] int height_for_width(int);
    [[nodiscard]] int configuration_height_for_width(int);
    [[nodiscard]] bool loading()const;
    // The existing editor forwards its actual changes here; no second editor.
    bool capture();
    void snapshot(windows::CppRunRequest&);
    void reset();
    void set_language(std::string);
    void set_text_scale(int);
    void set_availability(bool busy,bool configured,bool built);
    [[nodiscard]] std::string selected_file()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}

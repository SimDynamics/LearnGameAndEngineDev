#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "core/attempts.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;

constexpr std::string_view filename = "lesson_construir_y_comprobar_un_modelo_de_pedidos.json";
constexpr std::string_view identity = "block_0007_0001";
constexpr std::string_view old_fingerprint = "978c6526c69d056658f4ee8455eaa4c37f5e87d97d9feebd5ecf93bac352ed13";
constexpr std::string_view revised_label = "Construcción desde código y unidades que termina normalmente";

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

template<class Predicate> void wait_for(Predicate predicate, std::string_view message) {
    const auto deadline = GetTickCount64() + 30000;
    do {
        pump();
        if (predicate()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(std::string(message));
}

HWND descendant(HWND parent, int id) {
    struct Search { int id; HWND found = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND item, LPARAM raw) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(item) == value.id) { value.found = item; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

RECT bounds(HWND item) {
    RECT result{};
    require(item && GetWindowRect(item, &result), "A restored matching label has no native bounds");
    return result;
}

const curriculum::Block& matching_block(const curriculum::Lesson& lesson) {
    const auto position = curriculum::stable_block_position(lesson, identity);
    require(position && position->page == 7 && position->block == 1,
        "The historical matching activity moved from its original stable location");
    return lesson.pages[position->page].blocks[position->block];
}

curriculum::Lesson load_historical() {
    std::array<wchar_t, 32768> executable{};
    const auto count = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    require(count && count < executable.size(), "Cannot resolve the immutable matching fixture directory");
    const auto path = std::filesystem::path(std::wstring_view(executable.data(), count)).parent_path()
        / "test-fixtures/u9-cbd9a38" / filename;
    require(state::sha256(state::read_bytes(path)) ==
        "caf6601b9eb17bc71fa067bc334f0e20196ebe2604848fd40dc455cbeef5d836",
        "The historical matching fixture differs from its pinned cbd9a38 Git bytes");
    return windows::load_lesson(path);
}

JsonObject saved_answers(const curriculum::Block& block, std::string_view mode) {
    const auto authored = JsonObject::Parse(wide(block.raw_json));
    const auto expected = authored.GetNamedArray(L"responses").GetAt(0).GetObject()
        .GetNamedObject(L"validator").GetNamedObject(L"expected");
    auto pairs = JsonObject::Parse(expected.Stringify());
    if (mode == "incorrect") {
        const auto construction = pairs.GetNamedString(L"construccion");
        pairs.Insert(L"construccion", pairs.GetNamedValue(L"transicion"));
        pairs.Insert(L"transicion", JsonValue::CreateStringValue(construction));
    } else if (mode == "partial") pairs.Remove(L"construccion");
    JsonObject response;
    response.Insert(L"type", JsonValue::CreateStringValue(L"matching"));
    response.Insert(L"pairs", pairs);
    JsonObject answers;
    answers.Insert(L"contratos", response);
    return answers;
}

void qa_compiler_prerequisite(state::StateData& data, const curriculum::Lesson& lesson,
    const curriculum::Block& block) {
    require(block.type == curriculum::BlockType::cpp_compiler && block.output_check,
        "The matching QA prerequisite must retain its original compiler objective");
    windows::CppRunRequest request;
    request.revision = 1;
    request.source_utf8 = block.initial_code;
    for (const auto& goal : block.output_check->cases) {
        curriculum::FreshExecution execution{1, true, true, 0, goal.standard_input, goal.outputs.front()};
        execution.executable = goal.executable;
        if (goal.standard_error == curriculum::StandardErrorExpectation::non_blank)
            execution.standard_error = "QA prerequisite fixture only";
        (void)windows::record_compiler_puzzle_case(data, lesson, block, request, execution,
            "Synthetic prerequisite receipt for matching restoration QA; no compiler execution is claimed");
    }
    require(windows::compiler_puzzle_completed(data, lesson, block),
        "The explicit QA prerequisite receipt did not satisfy its original objective");
}

class MountedLesson {
public:
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host = nullptr;
    HWND view = nullptr;
    std::atomic<int> validations{0};
    int new_feedback_events = 0;

    MountedLesson(curriculum::Lesson current, const state::StateData& initial,
        windows::NarrationWorker& worker, std::string_view mode)
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"editorial-matching-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(GetTickCount64()) + L"-" + wide(mode), false})),
          lesson(std::move(current)), worker_(worker) {
        require(!std::filesystem::exists(environment.paths.root), "Matching QA requires a new isolated profile");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);
        (void)store.load();
        store.save(initial);
        host = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Hidden matching editorial QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host, "Cannot create the isolated matching QA host");
        open();
    }

    ~MountedLesson() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }

    void open() {
        LessonViewHooks hooks;
        hooks.validate_math = [this](std::string_view block, std::string_view responses,
            std::string_view language, std::stop_token stop) {
            auto result = worker_.validate_math(block, responses, language, stop);
            ++validations;
            return result;
        };
        hooks.feedback_changed = [this](HWND, std::size_t, bool) { ++new_feedback_events; };
        view = create_lesson_view(host, environment, lesson, std::move(hooks));
        require(view, "Cannot mount the current matching lesson");
        MoveWindow(view, 0, 0, 1100, 850, TRUE);
        pump();
        require(!IsWindowVisible(host), "Matching QA must never take over the user's desktop");
    }

    void close() {
        if (!view) return;
        require(flush_lesson_view(view), "Cannot flush the isolated matching QA state");
        DestroyWindow(view);
        view = nullptr;
        pump();
    }

    void reopen() { close(); open(); }

    state::StateData saved() const { return read_lesson_state(view); }

    HWND feedback() const { return descendant(view, 1104); }

private:
    windows::NarrationWorker& worker_;
};

void run_case(const curriculum::Lesson& legacy, const curriculum::Lesson& current,
    windows::NarrationWorker& worker, std::string_view mode) {
    const auto& old_block = matching_block(legacy);
    const auto& block = matching_block(current);
    require(curriculum::exercise_key(legacy, old_block) == curriculum::exercise_key(current, block),
        "The reviewed label changed its historical answer address");
    auto answers = saved_answers(old_block, mode);
    const auto old_reply = JsonObject::Parse(wide(worker.validate_math(old_block.raw_json,
        utf8(answers.Stringify()), "es")));
    require(utf8(old_reply.GetNamedString(L"contentFingerprint")) == old_fingerprint,
        "The historical matching answer did not use the original Android fingerprint");
    JsonObject payload;
    payload.Insert(L"responses", answers);
    payload.Insert(L"correct", JsonValue::CreateBooleanValue(mode == "correct"));
    payload.Insert(L"contentFingerprint", old_reply.GetNamedValue(L"contentFingerprint"));
    payload.Insert(L"incorrectAttempts", JsonValue::CreateNumberValue(4));
    payload.Insert(L"opaque", JsonObject::Parse(LR"({"selection":23,"source":"historical QA fixture"})"));
    state::Response response;
    response.raw_json = utf8(payload.Stringify());
    response.correct = mode == "correct";
    if (mode == "incorrect") state::mark_response_reviewed(response, old_block);

    state::StateData initial;
    state::prepare_lesson_state(initial, legacy);
    state::put_response(initial, legacy, old_block, response);
    state::save_current_page(initial, legacy, 7);
    initial.preferences["native_windows"]["ui_language"] = std::string("es");
    auto& reserve = initial.preferences["attempt_reserve_seconds_test"];
    reserve["recovery_interval_programming"] = attempts::recovery_millis;
    reserve["reserve_programming"] = 6.0;
    reserve["updated_at_programming"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    narration::History bookmark;
    // A source-bound synthetic bookmark checks storage preservation only. This
    // fixture does not synthesize audio or claim timed playback restoration.
    bookmark.entries.push_back({state::sha256(old_block.raw_json), old_block.stable_id,
        {2, 730, 1, false}, legacy.pages[7].stable_id});
    narration::write_history(initial, bookmark);
    const auto original_response = state::get_response(initial, legacy, old_block)->raw_json;
    const auto original_reserve = initial.preferences.at("attempt_reserve_seconds_test");
    const auto original_history = narration::read_history(initial);
    const std::set<std::string> required{"block_0006_0006", "block_0006_0009"};
    const auto prerequisites = curriculum::activity_prerequisites(current, block);
    std::set<std::string> actual_required;
    for (const auto position : prerequisites)
        actual_required.insert(current.pages[position.page].blocks[position.block].stable_id);
    require(actual_required == required, "The matching activity lost its two original prerequisites");
    // Other prerequisite receipts only permit reaching the tested page. They
    // never certify real compilation and do not answer the matching activity.
    for (const auto& page : current.pages) for (const auto& candidate : page.blocks)
        if (candidate.type == curriculum::BlockType::cpp_compiler && candidate.output_check
            && !required.contains(candidate.stable_id)) qa_compiler_prerequisite(initial, current, candidate);

    MountedLesson mounted(current, initial, worker, mode);
    const auto unchanged = [&] {
        const auto data = mounted.saved();
        const auto restored = state::get_response(data, current, block);
        require(restored && restored->raw_json == original_response && restored->correct == response.correct
            && state::response_reviewed(*restored, block) == (mode == "incorrect"),
            "Restoring the revised label rewrote an answer, verdict, attempt count or reviewed identity");
        require(data.preferences.at("attempt_reserve_seconds_test") == original_reserve,
            "Restoring matching feedback charged or reset the original attempt reserve");
        require(narration::read_history(data) == original_history,
            "Opening or restoring matching feedback moved the historical reading bookmark");
        require(mounted.new_feedback_events == 0,
            "Reopening an existing matching answer emitted a fresh submission/narration event");
    };
    for (const auto& region : lesson_reading_regions(mounted.view))
        require(region.source_text.find(revised_label) == std::string::npos,
            "A prerequisite-locked matching label leaked into the reader");
    require(lesson_page_state(mounted.view).index < 7 || !descendant(mounted.view, 1105),
        "The saved matching answer bypassed its unresolved compiler prerequisites");
    unchanged();
    mounted.close();
    {
        state::StateStore store(mounted.environment.paths.root);
        auto unlocked = store.load();
        for (const auto position : prerequisites)
            qa_compiler_prerequisite(unlocked, current, current.pages[position.page].blocks[position.block]);
        state::save_current_page(unlocked, current, 7);
        store.save(unlocked);
    }
    mounted.open();
    const auto current_reply = JsonObject::Parse(wide(worker.validate_math(block.raw_json, utf8(answers.Stringify()), "es")));
    const auto expected_feedback = mode == "partial" ? std::string{} : utf8(current_reply.GetNamedString(L"feedback"));
    const auto verify = [&] {
        wait_for([&] {
            const auto feedback = mounted.feedback();
            return feedback && edit_text(feedback) == expected_feedback;
        }, "The current matching guide/success did not restore after fingerprint verification");
        require(lesson_page_state(mounted.view).index == 7, "QA prerequisites did not restore the original saved page");
        const auto regions = lesson_reading_regions(mounted.view);
        const auto find_region = [&](std::string_view suffix) -> const LessonReadingRegion& {
            const auto found = std::ranges::find_if(regions, [&](const auto& region) { return region.key.ends_with(suffix); });
            require(found != regions.end(), "A restored source/target matching card is missing from the reader");
            return *found;
        };
        const auto& construction = find_region("contratos/source/construccion");
        require(construction.source_text == revised_label && edit_text(construction.target) == revised_label,
            "A restored matching card or narration region retained the obsolete construction label");
        const auto raw = JsonObject::Parse(wide(block.raw_json));
        const auto targets = raw.GetNamedArray(L"responses").GetAt(0).GetObject().GetNamedArray(L"targets");
        const auto pairs = answers.GetNamedObject(L"contratos").GetNamedObject(L"pairs");
        for (const auto& pair : pairs) {
            const auto& source = find_region("contratos/source/" + utf8(pair.Key()));
            const auto target_id = utf8(pair.Value().GetString());
            const auto& target = find_region("contratos/target/" + target_id);
            const auto source_y = bounds(source.target).top, target_y = bounds(target.target).top;
            require(source_y > target_y, "A saved matching source did not follow its assigned target card");
            for (std::uint32_t index = 0; index + 1 < targets.Size(); ++index)
                if (utf8(targets.GetAt(index).GetObject().GetNamedString(L"id")) == target_id)
                    require(source_y < bounds(find_region("contratos/target/" + utf8(targets.GetAt(index + 1).GetObject().GetNamedString(L"id"))).target).top,
                        "A restored source card was associated with a different target");
        }
        if (mode == "partial")
            require(bounds(construction.target).top < bounds(find_region("contratos/target/sin_cambios").target).top,
                "An unmatched historical source was silently assigned to a target");
        const auto feedback_region = std::ranges::find_if(regions, [](const auto& region) { return region.feedback && region.block_index == 1; });
        if (expected_feedback.empty()) require(feedback_region == regions.end(), "An old partial draft acquired evaluated feedback");
        else require(feedback_region != regions.end() && feedback_region->source_text == expected_feedback
            && feedback_region->correct == (mode == "correct"), "The reader lost the exact current guide/success or its correct state");
        unchanged();
    };
    verify();
    mounted.reopen();
    verify();
    std::cout << "PASS actual matching editorial " << mode
        << ": prerequisite gate, original payload/attempts/bookmark, current cards/feedback/reader and silent reopen\n";
}
}

int main() {
    const auto ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    const auto rich_edit = LoadLibraryW(L"Msftedit.dll");
    int result = 0;
    try {
        require(rich_edit, "Cannot load native RichEdit for matching QA");
        reader_theme::prepare_fonts();
        const auto source = std::filesystem::path(build::source_root)
            / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido" / filename;
        const auto source_hash = state::sha256(state::read_bytes(source));
        const auto legacy = load_historical();
        const auto current = windows::load_lesson(source);
        windows::NarrationWorker worker(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        for (const auto mode : {"correct", "incorrect", "partial"}) run_case(legacy, current, worker, mode);
        require(state::sha256(state::read_bytes(source)) == source_hash, "Editorial matching QA modified the live curricular asset");
    } catch (const std::exception& error) {
        std::cerr << "FAIL editorial matching: " << error.what() << '\n';
        result = 1;
    }
    if (rich_edit) FreeLibrary(rich_edit);
    OleUninitialize();
    return result;
}

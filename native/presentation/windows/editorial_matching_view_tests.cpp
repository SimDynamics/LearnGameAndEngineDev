#include "core/lesson.hpp"
#include "core/profile.hpp"
#include "core/narration_state.hpp"
#include "core/attempts.hpp"
#include "core/math_label_test_fixture.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/android_import.hpp"
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
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <map>
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

void prepare_visual_profile(std::wstring_view token) {
    require(is_valid_qa_instance(token), "Unsafe matching visual QA instance token");
    const auto source = std::filesystem::path(build::source_root)
        / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido" / filename;
    const auto asset_hash = state::sha256(state::read_bytes(source));
    const auto lesson = windows::load_lesson(source);
    const auto& target = matching_block(lesson);
    require(target.type == curriculum::BlockType::math_puzzle && lesson.compiler_puzzles_required,
        "The matching visual target lost its current type or published global compiler policy");
    const std::set<std::string> global_required{
        "block_0001_0002", "block_0001_0007", "block_0004_0002", "block_0006_0006", "block_0006_0009"};
    const std::set<std::string> direct_required{"block_0006_0006", "block_0006_0009"};
    std::set<std::string> actual_global, actual_direct;
    for (std::size_t page = 0; page < 7; ++page) for (const auto& block : lesson.pages[page].blocks)
        if (curriculum::has_compiler_objective(block)) actual_global.insert(block.stable_id);
    for (const auto at : curriculum::activity_prerequisites(lesson, target))
        actual_direct.insert(lesson.pages[at.page].blocks[at.block].stable_id);
    require(actual_global == global_required && actual_direct == direct_required,
        "The matching visual QA requires exactly five prior global gates and its two original direct owners");
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    require(environment.paths.root.parent_path().filename() == L"qa"
        && environment.paths.root.parent_path().parent_path().filename() == L"Codex"
        && environment.paths.root.filename() == token,
        "Matching visual QA resolved outside the selected isolated Codex QA directory");
    state::require_safe_path(environment.paths.root);
    require(!std::filesystem::exists(environment.paths.root), "Matching visual QA refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    require(std::filesystem::create_directory(environment.paths.root), "Another process already created this matching QA profile");
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    const auto empty = store.load();
    require(empty.preferences.empty(), "The newly reserved matching QA profile unexpectedly contains prior data");
    state::StateData data;
    state::prepare_lesson_state(data, lesson);
    // The five prior global compiler gates are explicit synthetic QA receipts,
    // including the matching activity's two direct owners. No run or answer is
    // claimed; only this later page's presentation is being isolated.
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks)
        if (global_required.contains(block.stable_id)) qa_compiler_prerequisite(data, lesson, block);
    state::save_current_page(data, lesson, 7);
    auto& preferences = data.preferences["native_windows"];
    preferences["ui_language"] = std::string("es");
    preferences["content_language"] = std::string("es");
    preferences["catalog/locale"] = std::string("es");
    preferences["catalog/route"] = std::string("programming");
    preferences["catalog/course"] = std::string("cpp_fundamentals");
    const auto unit_end = lesson.id.rfind("__");
    require(unit_end != std::string::npos, "The matching lesson has no catalog unit identity");
    preferences["catalog/unit"] = lesson.id.substr(0, unit_end);
    preferences["navigation/lesson"] = lesson.id;
    // Prepare only the current sentence, avoiding unrelated long reference-code
    // recordings during this visual-only QA. This is an isolated preference.
    data.preferences["lesson_narration_settings"]["kokoro_advance_generation"] = false;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        require(!state::get_response(data, lesson, block), "Matching visual preparation invented a quiz response or achievement");
        if (block.type == curriculum::BlockType::cpp_compiler) {
            const bool seeded = global_required.contains(block.stable_id);
            require(windows::compiler_puzzle_completed(data, lesson, block) == seeded,
                "Matching visual preparation credited an unrelated compiler objective");
            if (!seeded) require(windows::compiler_puzzle_case_count(data, lesson, block) == 0,
                "An unrelated compiler acquired a partial QA receipt");
        }
    }
    require(narration::read_history(data).entries.empty(), "Matching visual preparation fabricated reading history");
    state::validate_lesson_state(data, lesson);
    store.save(data);
    const auto saved = store.load();
    require(saved == data && state::current_page(saved, lesson) == 7 && !state::get_response(saved, lesson, target),
        "Matching visual QA did not preserve its unanswered page exactly");
    require(state::sha256(state::read_bytes(source)) == asset_hash, "Matching visual preparation changed the authored asset");
    std::cout << "profile=Codex\nqa_instance=" << utf8(token)
        << "\ndata_root=" << utf8(environment.paths.root.native()) << "\nlesson=" << lesson.id
        << "\ntarget=" << target.stable_id << "\npage=8\npage_index=7\nblock=2"
        << "\nsynthetic_prerequisites=block_0001_0002,block_0001_0007,block_0004_0002,block_0006_0006,block_0006_0009"
        << "\ndirect_owners=block_0006_0006,block_0006_0009\ncompiler_executions=none"
        << "\nquiz_responses=none\ntarget_completed=0\nreading_history=none\nadvance_generation=0\nwindows_created=0\n";
}

void inspect_visual_profile(std::wstring_view token) {
    require(is_valid_qa_instance(token), "Unsafe matching inspection QA token");
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    const auto& root = environment.paths.root;
    require(root.parent_path().filename() == L"qa" && root.parent_path().parent_path().filename() == L"Codex"
        && root.filename() == token, "Matching inspection resolved outside the selected Codex QA profile");
    const auto state_root = root / L"state", active = state_root / L"active/state.bin";
    state::require_safe_path(state_root);
    require(std::filesystem::is_directory(state_root) && std::filesystem::is_regular_file(active),
        "Matching inspection requires an existing QA profile and complete active state");
    const auto tree = [&] {
        std::map<std::string, std::string> entries;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(state_root)) {
            state::require_safe_path(entry.path());
            const auto name = utf8(entry.path().lexically_relative(state_root).generic_wstring());
            if (entry.is_directory()) entries.emplace(name, "directory");
            else {
                require(entry.is_regular_file(), "Matching inspection refuses a special state-tree entry");
                entries.emplace(name, "file:" + state::sha256(state::read_bytes(entry.path())));
            }
        }
        return entries;
    };
    const auto tree_hash = [](const auto& entries) {
        std::string framed;
        for (const auto& [name, value] : entries)
            framed += std::to_string(name.size()) + ":" + name + std::to_string(value.size()) + ":" + value;
        return state::sha256(framed);
    };
    const auto before = tree();
    const auto source = std::filesystem::path(build::source_root)
        / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido" / filename;
    const auto asset_hash = state::sha256(state::read_bytes(source));
    const auto lesson = windows::load_lesson(source);
    const auto& target = matching_block(lesson);
    // Constructor/load are read-only. Never initialize directories, prepare
    // compatibility metadata, save, validate through a worker, or create HWNDs.
    state::StateStore store(root);
    const auto data = store.load();
    state::validate_lesson_state(data, lesson);
    const auto response = state::get_response(data, lesson, target);
    const auto history = narration::read_history(data);
    require(history.entries.size() <= 3, "Matching inspection found an unbounded reading history");
    JsonObject result;
    const auto string_field = [&](const wchar_t* name, const std::string& value) {
        result.Insert(name, JsonValue::CreateStringValue(wide(value)));
    };
    string_field(L"qa_instance", utf8(token));
    string_field(L"asset_sha256", asset_hash);
    string_field(L"state_sha256", before.at("active/state.bin").substr(5));
    result.Insert(L"current_page", JsonValue::CreateNumberValue(static_cast<double>(state::current_page(data, lesson) + 1)));
    result.Insert(L"response_present", JsonValue::CreateBooleanValue(response.has_value()));
    string_field(L"response_payload_sha256", response ? state::sha256(response->raw_json) : "");
    string_field(L"answer", response ? response->answer : "");
    result.Insert(L"correct", JsonValue::CreateBooleanValue(response && response->correct));
    string_field(L"active_key", history.active_key);
    string_field(L"active_owner", history.active_owner);
    JsonArray entries;
    for (const auto& bookmark : history.entries) {
        JsonObject entry;
        entry.Insert(L"key", JsonValue::CreateStringValue(wide(bookmark.key)));
        entry.Insert(L"page", JsonValue::CreateStringValue(wide(bookmark.page)));
        entry.Insert(L"segment", JsonValue::CreateNumberValue(static_cast<double>(bookmark.position.segment)));
        entry.Insert(L"offset_ms", JsonValue::CreateNumberValue(static_cast<double>(bookmark.position.milliseconds)));
        entry.Insert(L"heard_through", JsonValue::CreateNumberValue(static_cast<double>(bookmark.position.heard_through)));
        entry.Insert(L"finished", JsonValue::CreateBooleanValue(bookmark.position.finished));
        // Ownership belongs to the active key, not independently to every slot.
        entry.Insert(L"owner", JsonValue::CreateStringValue(wide(bookmark.key == history.active_key ? history.active_owner : "")));
        entries.Append(entry);
    }
    result.Insert(L"history", entries);
    const auto after = tree();
    require(before == after && state::sha256(state::read_bytes(source)) == asset_hash,
        "QA state tree or asset changed during inspection; no stable snapshot is claimed");
    string_field(L"state_tree_sha256_before", tree_hash(before));
    string_field(L"state_tree_sha256_after", tree_hash(after));
    result.Insert(L"read_only", JsonValue::CreateBooleanValue(true));
    std::cout << utf8(result.Stringify()) << '\n';
}

class MountedLesson {
public:
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host = nullptr;
    HWND view = nullptr;
    std::atomic<int> validations{0};
    int new_feedback_events = 0;
    int openings = 0;

    MountedLesson(curriculum::Lesson current, const state::StateData& initial,
        windows::NarrationWorker& worker, std::string_view mode,
        const std::filesystem::path& imported_source = {})
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"editorial-matching-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(GetTickCount64()) + L"-" + wide(mode), false})),
          lesson(std::move(current)), worker_(worker) {
        require(!std::filesystem::exists(environment.paths.root), "Matching QA requires a new isolated profile");
        if (imported_source.empty()) {
            initialize_profile_directories(environment.paths);
            state::StateStore store(environment.paths.root);
            (void)store.load();
            store.save(initial);
        } else {
            const auto preview = state::inspect_android_export(imported_source);
            require(preview.data == initial, "The math prompt export changed the exact previous state");
            state::validate_lesson_state(preview.data, lesson);
            state::commit_android_import(preview, environment.paths.root);
            initialize_profile_directories(environment.paths);
        }
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
        ++openings;
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
void run_u11_domain(windows::NarrationWorker& worker){
    const auto source=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto bytes=state::read_bytes(source);const auto fixture=test::u11_domain_label_fixture(windows::load_lesson(source));
    const auto& old=fixture.before.pages[8].blocks[2];const auto& revised=fixture.after.pages[8].blocks[2];
    const auto authored=JsonObject::Parse(wide(old.raw_json));
    const auto expected=authored.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"validator").GetNamedObject(L"expected");
    const auto answers=[&](std::string_view mode){
        const auto pairs=JsonObject::Parse(expected.Stringify());
        if(mode=="incorrect"){pairs.Insert(L"dominio",JsonValue::CreateStringValue(L"completa"));pairs.Insert(L"valida",JsonValue::CreateStringValue(L"limite"));}
        else if(mode=="partial")pairs.Remove(L"dominio");
        JsonObject value;value.Insert(L"type",JsonValue::CreateStringValue(L"matching"));value.Insert(L"pairs",pairs);
        JsonObject result;result.Insert(L"casos",value);return result;
    };
    for(const auto mode:{"empty","partial","incorrect","correct"}){
        state::StateData initial;state::prepare_lesson_state(initial,fixture.before);state::save_current_page(initial,fixture.before,8);
        initial.preferences["native_windows"]["ui_language"]=std::string("es");
        initial.preferences["u11_domain_qa"]["opaque"]=std::string(" \t unchanged\r\n");
        for(const auto& page:fixture.before.pages)for(const auto& block:page.blocks)
            if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check)qa_compiler_prerequisite(initial,fixture.before,block);
        narration::History bookmark;bookmark.entries.push_back({state::sha256(old.raw_json),old.stable_id,{2,730,1,false},fixture.before.pages[8].stable_id});
        narration::write_history(initial,bookmark); // Storage only, no audio playback is claimed.
        auto original_answers=answers(mode);
        if(std::string_view(mode)!="empty"){
            const auto reply=JsonObject::Parse(wide(worker.validate_math(old.raw_json,utf8(original_answers.Stringify()),"es")));
            require(utf8(reply.GetNamedString(L"contentFingerprint"))==test::u11_domain_android_before,
                "The original U11 response did not use its independently pinned Android insertion-order fingerprint");
            JsonObject payload;payload.Insert(L"responses",original_answers);payload.Insert(L"correct",JsonValue::CreateBooleanValue(std::string_view(mode)=="correct"));
            payload.Insert(L"contentFingerprint",reply.GetNamedValue(L"contentFingerprint"));payload.Insert(L"incorrectAttempts",JsonValue::CreateNumberValue(3));
            payload.Insert(L"opaque",JsonObject::Parse(LR"({"origin":"original U11 matching QA","selected":17})"));
            state::Response response;response.raw_json=utf8(payload.Stringify());response.correct=std::string_view(mode)=="correct";
            if(std::string_view(mode)=="incorrect")state::mark_response_reviewed(response,old);
            state::put_response(initial,fixture.before,old,response);
        }
        const auto prior=state::get_response(initial,fixture.before,old);
        const auto history=narration::read_history(initial);const auto opaque=initial.preferences.at("u11_domain_qa");
        MountedLesson mounted(fixture.after,initial,worker,std::string("u11-")+mode);
        const auto regions=[&]{return lesson_reading_regions(mounted.view);};
        const auto find_region=[&](std::string_view suffix){const auto all=regions();
            const auto found=std::ranges::find_if(all,[&](const auto& value){return value.key.ends_with(suffix);});
            require(found!=all.end(),"An actual U11 matching source/target lost its reader region");return *found;};
        const auto preserved=[&]{const auto data=mounted.saved();const auto saved=state::get_response(data,fixture.after,revised);
            require(saved.has_value()==prior.has_value(),"U11 label adoption invented or removed a matching response");
            if(prior)require(saved->raw_json==prior->raw_json&&saved->correct==prior->correct
                &&state::response_reviewed(*saved,revised)==(std::string_view(mode)=="incorrect"),
                "U11 label adoption rewrote historical pairs, attempts, metadata or reviewed state");
            require(data.preferences.at("u11_domain_qa")==opaque&&narration::read_history(data)==history&&mounted.new_feedback_events==0,
                "U11 label restoration altered unrelated data/bookmark or emitted a fresh feedback event");
            require(lesson_page_state(mounted.view).index==8,"The explicit compiler QA prerequisites did not retain U11 page nine");
            const auto label=find_region("casos/source/dominio");
            require(label.source_text=="99+2;"&&edit_text(label.target)=="99+2;","U11 restored card or reader retained the literal-domain example");
            std::size_t sources=0,targets=0;for(const auto& region:regions()){
                sources+=region.key.find("casos/source/")!=std::string::npos;targets+=region.key.find("casos/target/")!=std::string::npos;}
            require(sources==6&&targets==6,"U11 label adoption removed a source or target identity");
        };
        const auto historical_feedback=std::string_view(mode)=="correct"||std::string_view(mode)=="incorrect";
        const auto revised_reply=JsonObject::Parse(wide(worker.validate_math(revised.raw_json,utf8(original_answers.Stringify()),"es")));
        require(utf8(revised_reply.GetNamedString(L"contentFingerprint"))==test::u11_domain_android_before,
            "The reviewed U11 label did not retain the authentic historical response fingerprint");
        const auto expected_feedback=historical_feedback?utf8(JsonObject::Parse(wide(worker.validate_math(revised.raw_json,utf8(original_answers.Stringify()),"es"))).GetNamedString(L"feedback")):std::string{};
        const auto verify=[&]{wait_for([&]{const auto feedback=descendant(mounted.view,1204);return feedback&&edit_text(feedback)==expected_feedback;},
            "The U11 matching historical guide/success did not finish its compatible restoration");preserved();};
        verify();mounted.reopen();verify();
        const auto tap=[&](std::string_view key){const auto item=find_region(key).target;
            SendMessageW(item,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(2,2));SendMessageW(item,WM_LBUTTONUP,0,MAKELPARAM(2,2));pump();};
        const auto reset_matching=[&]{
            const auto math=descendant(mounted.view,1205);require(math,"The U11 matching toolbar container is absent");
            HWND button=nullptr;
            EnumChildWindows(math,[](HWND item,LPARAM data)->BOOL{
                wchar_t kind[32]{},caption[80]{};GetClassNameW(item,kind,32);GetWindowTextW(item,caption,80);
                if(lstrcmpiW(kind,L"BUTTON")==0&&std::wstring_view(caption)==L"Reiniciar"){
                    *reinterpret_cast<HWND*>(data)=item;return FALSE;
                }return TRUE;
            },reinterpret_cast<LPARAM>(&button));
            require(button&&IsWindowEnabled(button),"The actual U11 mathematical Restart button is absent or disabled");
            SendMessageW(button,BM_CLICK,0,0);pump();
        };
        if(prior)reset_matching();
        // Real card controls create a new rejected attempt and then correct
        // only the two swapped pairs. Original answers above were restored,
        // never silently reinterpreted as these current responses.
        const auto wrong=answers("incorrect").GetNamedObject(L"casos").GetNamedObject(L"pairs");
        for(const auto& pair:wrong){tap("casos/source/"+utf8(pair.Key()));tap("casos/target/"+utf8(pair.Value().GetString()));}
        SendMessageW(descendant(mounted.view,1203),BM_CLICK,0,0);
        wait_for([&]{const auto value=state::get_response(mounted.saved(),fixture.after,revised);return value&&!value->correct&&state::response_reviewed(*value,revised);},
            "The current U11 matching controls did not record their rejected swapped pairs");
        const auto wrong_feedback=utf8(JsonObject::Parse(wide(worker.validate_math(revised.raw_json,utf8(answers("incorrect").Stringify()),"es"))).GetNamedString(L"feedback"));
        require(edit_text(descendant(mounted.view,1204))==wrong_feedback,"U11 current rejection lost its exact guide");
        tap("casos/source/valida");tap("casos/target/completa");tap("casos/source/dominio");tap("casos/target/limite");
        SendMessageW(descendant(mounted.view,1203),BM_CLICK,0,0);
        wait_for([&]{const auto value=state::get_response(mounted.saved(),fixture.after,revised);return value&&value->correct;},
            "Correcting the two current U11 matching cards did not produce success");
        const auto fresh=state::get_response(mounted.saved(),fixture.after,revised)->raw_json;
        const auto success=utf8(JsonObject::Parse(wide(worker.validate_math(revised.raw_json,utf8(answers("correct").Stringify()),"es"))).GetNamedString(L"feedback"));
        require(edit_text(descendant(mounted.view,1204))==success,"U11 current success lost its explanation");
        const auto current_regions=regions();
        require(std::ranges::any_of(current_regions,[&](const auto& region){return region.feedback&&region.block_index==2&&region.correct&&region.source_text==success;}),
            "U11 current reader did not expose the exact successful feedback");
        mounted.reopen();wait_for([&]{const auto value=state::get_response(mounted.saved(),fixture.after,revised);return value&&value->correct&&value->raw_json==fresh
            &&edit_text(descendant(mounted.view,1204))==success;},"U11 current matching success did not reopen exactly");
        reset_matching();
        const auto reset=state::get_response(mounted.saved(),fixture.after,revised);
        require(reset.has_value(),"U11 scoped Reset did not save its ordinary empty response record");
        require(!reset->correct,"U11 scoped Reset retained the current success verdict");
        require(!state::response_reviewed(*reset,revised),"U11 scoped Reset retained reviewed-answer metadata");
        require(!JsonObject::Parse(wide(reset->raw_json)).HasKey(L"responses"),"U11 scoped Reset retained matching pairs in its response payload");
        require(edit_text(descendant(mounted.view,1204)).empty(),"U11 scoped Reset retained current feedback");
        require(mounted.saved().preferences.at("u11_domain_qa")==opaque,"U11 scoped Reset changed unrelated opaque preferences");
        require(narration::read_history(mounted.saved())==history,"U11 scoped Reset changed the stored historical reading bookmark");
        for(const auto& page:fixture.after.pages)for(const auto& compiler:page.blocks)if(compiler.type==curriculum::BlockType::cpp_compiler&&compiler.output_check)
            require(windows::compiler_puzzle_completed(mounted.saved(),fixture.after,compiler),"Resetting U11 matching changed an independent compiler prerequisite");
    }
    require(state::read_bytes(source)==bytes,"U11 matching revision QA changed the curricular source");
    std::cout<<"PASS current U11 domain matching label: original/revised full-block pins, empty/partial/rejected/successful historical state, exact current cards and reader sources, new real rejection/correction/success, silent reopen and scoped Reset. Compiler receipts/bookmark are synthetic QA, no execution/audio playback/human listening claimed.\n";
}
constexpr std::string_view assetcheck_identity = "block_0002_0005";

const curriculum::Block& assetcheck_block(const curriculum::Lesson& lesson) {
    const auto position = curriculum::stable_block_position(lesson, assetcheck_identity);
    require(position && position->page == 2 && position->block == 5,
        "AssetCheck prompt revision changed its original stable address");
    const auto& block = lesson.pages[2].blocks[5];
    require(block.type == curriculum::BlockType::math_puzzle,
        "The AssetCheck prompt no longer belongs to its original math puzzle");
    return block;
}

JsonObject assetcheck_answers(const curriculum::Block& block, std::string_view mode) {
    const auto raw = JsonObject::Parse(wide(block.raw_json));
    const auto pairs = JsonObject::Parse(raw.GetNamedArray(L"responses").GetObjectAt(0)
        .GetNamedObject(L"validator").GetNamedObject(L"expected").Stringify());
    if (mode == "incorrect") {
        pairs.Insert(L"a", JsonValue::CreateStringValue(L"uno"));
        pairs.Insert(L"b", JsonValue::CreateStringValue(L"cero"));
    } else if (mode == "partial") pairs.Remove(L"a");
    JsonObject response;
    response.Insert(L"type", JsonValue::CreateStringValue(L"matching"));
    response.Insert(L"pairs", pairs);
    JsonObject answers;
    answers.Insert(L"relaciones", response);
    return answers;
}

void assetcheck_write_new(const std::filesystem::path& path, std::string_view bytes) {
    state::require_safe_path(path);
    require(!std::filesystem::exists(path), "Refusing to overwrite an existing math QA export");
    std::filesystem::create_directories(state::native_io_path(path.parent_path()));
    std::ofstream output(state::native_io_path(path), std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(output), "Cannot write an isolated math QA export");
}

std::string assetcheck_xml(std::string_view text) {
    std::string result;
    for (const char character : text) switch (character) {
    case '&': result += "&amp;"; break;
    case '<': result += "&lt;"; break;
    case '>': result += "&gt;"; break;
    case '"': result += "&quot;"; break;
    case '\r': result += "&#13;"; break;
    case '\n': result += "&#10;"; break;
    case '\t': result += "&#9;"; break;
    default: result += character;
    }
    return result;
}

void assetcheck_export(const std::filesystem::path& root, const state::StateData& data) {
    for (const auto& [domain, entries] : data.preferences) {
        std::string document = "<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for (const auto& [key, value] : entries) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto name = " name=\"" + assetcheck_xml(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>)
                document += "<string" + name + ">" + assetcheck_xml(item) + "</string>";
            else if constexpr (std::is_same_v<T, bool>)
                document += "<boolean" + name + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>)
                document += "<long" + name + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream number;
                number << std::setprecision(17) << item;
                document += "<float" + name + " value=\"" + number.str() + "\"/>";
            } else {
                document += "<set" + name + ">";
                for (const auto& word : item) document += "<string>" + assetcheck_xml(word) + "</string>";
                document += "</set>";
            }
        }, value);
        assetcheck_write_new(root / "shared_prefs" / (domain + ".xml"), document + "</map>");
    }
}

void assetcheck_loader_guards(const curriculum::Lesson& revised) {
    const auto reject = [](auto operation, std::string_view message) {
        bool failed = false;
        try { operation(); } catch (const winrt::hresult_error&) { failed = true; }
        catch (const std::exception&) { failed = true; }
        require(failed, message);
    };
    // WinRT may serialize object order/number spelling differently. Preserve
    // the exact current mathematical block, whose raw contract is protected,
    // and use the identical transport for the positive and negative cases.
    const auto transport = [&](const JsonObject& root, std::string_view math_source) {
        const auto block = root.GetNamedArray(L"pages").GetObjectAt(2)
            .GetNamedArray(L"blocks").GetObjectAt(5);
        const auto encoded_block = utf8(block.Stringify());
        auto encoded_document = utf8(root.Stringify());
        const auto at = encoded_document.find(encoded_block);
        require(at != std::string::npos && encoded_document.find(encoded_block, at + encoded_block.size()) == std::string::npos,
            "The math negative-test transport did not identify one exact block");
        encoded_document.replace(at, encoded_block.size(), math_source);
        return encoded_document;
    };
    const auto& original = assetcheck_block(revised);
    const auto positive = transport(JsonObject::Parse(wide(revised.raw_json)), original.raw_json);
    const auto full = windows::parse_lesson_json(positive, "es");
    const auto progress = windows::parse_lesson_progress_metadata(positive, "es");
    for (const auto* loaded : {&full, &progress}) {
        const auto& actual = assetcheck_block(*loaded);
        require(loaded->id == revised.id && loaded->state_layout.signatures == revised.state_layout.signatures
            && actual.raw_json == original.raw_json
            && curriculum::math_evaluation_source(actual) == curriculum::math_evaluation_source(original)
            && actual.text_semantics.at("/prompt") == original.text_semantics.at("/prompt"),
            "The positive math negative-test transport changed ownership, raw evaluation or explicit semantics");
    }
    const auto fail = [&](auto mutate) {
        auto root = JsonObject::Parse(wide(revised.raw_json));
        auto math_source = original.raw_json;
        mutate(root, math_source);
        const auto bytes = transport(root, math_source);
        reject([&] { (void)windows::parse_lesson_json(bytes, "es"); },
            "Full loader accepted an unauthenticated math prompt revision");
        reject([&] { (void)windows::parse_lesson_progress_metadata(bytes, "es"); },
            "Progress loader accepted an unauthenticated math prompt revision");
    };
    fail([](JsonObject& root, std::string&) { root.GetNamedObject(L"stateLayout").Remove(L"mathTextRevisions"); });
    fail([](JsonObject& root, std::string&) {
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"mathTextRevisions")
            .GetObjectAt(0).Insert(L"rawJson", JsonObject{});
    });
    fail([](JsonObject& root, std::string&) {
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"mathTextRevisions")
            .GetObjectAt(0).Insert(L"rawJson", JsonValue::CreateStringValue(L"{}"));
    });
    fail([](JsonObject& root, std::string&) {
        const auto descriptors = root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
        bool removed = false;
        for (unsigned index = 0; index < descriptors.Size(); ++index) {
            const auto descriptor = descriptors.GetObjectAt(index);
            if (descriptor.GetNamedString(L"block") == wide(assetcheck_identity)
                && descriptor.GetNamedString(L"path") == L"/prompt") {
                descriptors.RemoveAt(index); removed = true; break;
            }
        }
        require(removed, "The current math prompt fixture lacks its explicit descriptor");
    });
    fail([](JsonObject& root, std::string& math_source) {
        const auto block = root.GetNamedArray(L"pages").GetObjectAt(2)
            .GetNamedArray(L"blocks").GetObjectAt(5);
        block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"validator")
            .GetNamedObject(L"expected").Insert(L"a", JsonValue::CreateStringValue(L"uno"));
        // Change only this protected answer token; retain every other raw
        // property, numeric token and insertion order from the positive case.
        const auto expected = math_source.find("\"expected\"");
        require(expected != std::string::npos && math_source.find("\"expected\"", expected + 1) == std::string::npos,
            "The protected math mutation did not identify one expected mapping");
        const auto target = math_source.find("\"cero\"", expected);
        require(target != std::string::npos, "The protected math mutation lost its original target token");
        math_source.replace(target, std::string_view("\"cero\"").size(), "\"uno\"");
        const auto proofs = root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
        proofs.GetObjectAt(proofs.Size() - 1).Insert(L"afterSha256",
            JsonValue::CreateStringValue(wide(test::compile_check_test_fingerprint(block))));
    });
    std::cout << "PASS AssetCheck full/progress raw-preserving transport control and five protected/archive/descriptor rejections\n";
}

void assetcheck_reading(MountedLesson& mounted, const curriculum::Block& original,
    const curriculum::Block& current) {
    const auto original_prompt = utf8(JsonObject::Parse(wide(original.raw_json)).GetNamedString(L"prompt"));
    const auto current_prompt = utf8(JsonObject::Parse(wide(current.raw_json)).GetNamedString(L"prompt"));
    const auto& marked = current.text_semantics.at("/prompt");
    require(marked.source == current_prompt && marked.spans.size() == 2
        && marked.spans[0].text == "LF" && marked.spans[1].text == "CR"
        && std::ranges::all_of(marked.spans, [](const auto& span) {
            return span.role == curriculum::SemanticRole::data && span.reading == curriculum::SemanticReading::natural; }),
        "The current AssetCheck prompt lost its explicit QA semantic marks");
    const auto regions = lesson_reading_regions(mounted.view);
    const auto visible = std::ranges::find_if(regions, [&](const auto& region) {
        return region.block_index == 5 && !region.feedback && region.source_text == current_prompt;
    });
    require(visible != regions.end() && visible->text_semantics
        && visible->text_semantics->source == current_prompt
        && visible->text_semantics->spans == marked.spans && visible->target
        && edit_text(visible->target) == current_prompt,
        "The actual AssetCheck reader lost the current prompt or its authored mark boundaries");
    const auto before = mounted.saved();
    const auto archives = editorial_lesson_reading_archives(mounted.view);
    require(mounted.saved() == before, "Reconstructing the math reading archive modified saved progress");
    const auto previous = original.text_semantics.find("/prompt");
    bool found = false;
    for (const auto& archive : archives) for (const auto& region : archive.regions) {
        if (region.block_index != 5 || region.feedback || region.source_text != original_prompt) continue;
        require(region.key == visible->key, "The historical math prompt changed its stable reading key");
        require(region.text_semantics.has_value() == (previous != original.text_semantics.end()),
            "The historical math prompt invented or dropped its previous semantic descriptor");
        if (previous != original.text_semantics.end())
            require(region.text_semantics->source == previous->second.source
                && region.text_semantics->spans == previous->second.spans,
                "The historical math prompt inherited the revised LF/CR semantic marks");
        require(std::ranges::find(archive.revised_regions, region.key) != archive.revised_regions.end(),
            "The changed math prompt was not identified as a revised narration region");
        found = true;
    }
    require(found, "The original AssetCheck prompt is missing from the authenticated reading archive");
}

void run_assetcheck_prompt(windows::NarrationWorker& worker,
    const std::filesystem::path& before_path, const std::filesystem::path& after_path) {
    const auto before_bytes = state::read_bytes(before_path), after_bytes = state::read_bytes(after_path);
    const auto before = windows::load_lesson(before_path, "es"), after = windows::load_lesson(after_path, "es");
    const auto& old = assetcheck_block(before);
    const auto& revised = assetcheck_block(after);
    require(before.id == after.id && before.state_layout.signatures == after.state_layout.signatures
        && curriculum::exercise_key(before, old) == curriculum::exercise_key(after, revised)
        && state::response_storage_key(before, old) == state::response_storage_key(after, revised),
        "The prompt revision changed the lesson, historical signatures or saved answer key");
    const auto progress = windows::parse_lesson_progress_metadata(after.raw_json, "es");
    require(progress.id == after.id && progress.state_layout.signatures == before.state_layout.signatures
        && curriculum::math_evaluation_source(assetcheck_block(progress)) == curriculum::math_evaluation_source(revised),
        "The progress loader did not preserve the same math prompt ownership/evaluation source");
    require(!revised.math_evaluation_raw_json.empty()
        && curriculum::math_evaluation_source(old) == old.raw_json,
        "Math evaluation did not retain the required original block source");
    assetcheck_loader_guards(after);
    const auto qa = std::filesystem::path(build::source_root) / "out/qa"
        / ("assetcheck-prompt-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    state::require_safe_path(qa);
    require(!std::filesystem::exists(qa), "Math prompt QA requires a new isolated export directory");
    for (const auto mode : {"correct", "incorrect", "partial"}) {
        const auto answers = assetcheck_answers(old, mode);
        const auto prior_reply = JsonObject::Parse(wide(worker.validate_math(old.raw_json,
            utf8(answers.Stringify()), "es")));
        const auto fingerprint = utf8(prior_reply.GetNamedString(L"contentFingerprint"));
        require(!fingerprint.empty() && prior_reply.GetNamedBoolean(L"correct") == (std::string_view(mode) == "correct"),
            "The actual worker did not validate the original AssetCheck pairs as expected");
        const auto current_reply = JsonObject::Parse(wide(worker.validate_math(curriculum::math_evaluation_source(revised),
            utf8(answers.Stringify()), "es")));
        require(utf8(current_reply.GetNamedString(L"contentFingerprint")) == fingerprint
            && current_reply.GetNamedBoolean(L"correct") == prior_reply.GetNamedBoolean(L"correct")
            && current_reply.GetNamedString(L"feedback") == prior_reply.GetNamedString(L"feedback"),
            "The revised math prompt changed the actual worker fingerprint, verdict or feedback");
        state::StateData initial;
        state::prepare_lesson_state(initial, before);
        // These ordinary QA selections only open page three; the target's math
        // receipt below comes from the real worker, never a fabricated verdict.
        for (std::size_t page = 0; page <= 2; ++page) for (const auto& block : before.pages[page].blocks)
            if (block.type == curriculum::BlockType::multiple_choice) {
                require(block.correct_index.has_value(), "An AssetCheck page prerequisite lost its correct option");
                state::Response selected;
                selected.selected = static_cast<std::int64_t>(*block.correct_index); selected.correct = true;
                state::put_response(initial, before, block, selected);
            }
        JsonObject payload;
        payload.Insert(L"responses", answers);
        payload.Insert(L"correct", prior_reply.GetNamedValue(L"correct"));
        payload.Insert(L"contentFingerprint", prior_reply.GetNamedValue(L"contentFingerprint"));
        payload.Insert(L"incorrectAttempts", JsonValue::CreateNumberValue(4));
        payload.Insert(L"opaque", JsonObject::Parse(LR"({"selection":23,"source":"AssetCheck original QA"})"));
        state::Response response;
        response.raw_json = utf8(payload.Stringify()); response.correct = std::string_view(mode) == "correct";
        if (std::string_view(mode) == "incorrect") state::mark_response_reviewed(response, old);
        state::put_response(initial, before, old, response);
        state::save_current_page(initial, before, 2);
        initial.preferences["assetcheck_prompt_qa"]["opaque"] = std::string(" original\t\r\n datos ");
        const auto read_key = "lesson_" + before.id + "_read_blocks_v2_es";
        initial.preferences["learning_progress"][read_key] = std::vector<std::string>{"block_0000_0001", "block_0002_0004"};
        narration::History bookmark;
        // Storage-only source-bound marker. No synthesis or audio playback is claimed.
        bookmark.entries.push_back({state::sha256(old.raw_json), old.stable_id, {2, 730, 1, false}, before.pages[2].stable_id});
        narration::write_history(initial, bookmark);
        const auto old_response = state::get_response(initial, before, old)->raw_json;
        const auto old_history = narration::read_history(initial);
        const auto old_read = initial.preferences.at("learning_progress").at(read_key);
        const auto export_path = qa / mode / "export";
        assetcheck_export(export_path, initial);
        std::map<std::filesystem::path, std::string> export_hashes;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(export_path))
            if (entry.is_regular_file()) export_hashes.emplace(entry.path(), state::sha256(state::read_bytes(entry.path())));
        for (const bool imported : {false, true}) {
            MountedLesson mounted(after, initial, worker, std::string("assetcheck-") + mode + (imported ? "-import" : "-native"),
                imported ? export_path : std::filesystem::path{});
            const auto expected_feedback = std::string_view(mode) == "partial" ? std::string{} : utf8(prior_reply.GetNamedString(L"feedback"));
            const auto preserved = [&] {
                const auto data = mounted.saved();
                const auto retained = state::get_response(data, after, revised);
                require(retained && retained->raw_json == old_response && retained->correct == response.correct
                    && state::response_reviewed(*retained, revised) == (std::string_view(mode) == "incorrect"),
                    "Math prompt adoption rewrote pairs, verdict, attempts, fingerprint or reviewed state");
                require(data.preferences.at("assetcheck_prompt_qa") == initial.preferences.at("assetcheck_prompt_qa")
                    && data.preferences.at("learning_progress").at(read_key) == old_read
                    && narration::read_history(data) == old_history && mounted.new_feedback_events == 0,
                    "Math prompt restoration altered opaque data/reading markers or emitted new feedback");
                const bool correct = std::string_view(mode) == "correct";
                const auto page = lesson_page_state(mounted.view);
                const auto options = lesson_options_snapshot(mounted.view);
                require(page.index == 2 && page.can_previous && page.can_next == correct
                    && (std::ranges::find(options.available_pages, std::size_t{3}) != options.available_pages.end()) == correct,
                    "The effective math view/navigation did not retain the restored verdict");
                const auto check = descendant(mounted.view, 1503);
                require(check && static_cast<bool>(IsWindowEnabled(check)) == !correct,
                    "The real matching Check control did not adopt the restored verdict");
                const auto regions = lesson_reading_regions(mounted.view);
                const auto card = [&](std::string_view suffix) -> const LessonReadingRegion& {
                    const auto found = std::ranges::find_if(regions, [&](const auto& region) { return region.key.ends_with(suffix); });
                    require(found != regions.end() && found->target, "A restored AssetCheck matching card is missing");
                    return *found;
                };
                const auto targets = JsonObject::Parse(wide(old.raw_json)).GetNamedArray(L"responses")
                    .GetObjectAt(0).GetNamedArray(L"targets");
                const auto pairs = answers.GetNamedObject(L"relaciones").GetNamedObject(L"pairs");
                for (const auto& pair : pairs) {
                    const auto source_y = bounds(card("relaciones/source/" + utf8(pair.Key())).target).top;
                    const auto target_id = utf8(pair.Value().GetString());
                    require(source_y > bounds(card("relaciones/target/" + target_id).target).top,
                        "The visible saved math source did not follow its assigned target");
                    for (unsigned index = 0; index + 1 < targets.Size(); ++index)
                        if (utf8(targets.GetObjectAt(index).GetNamedString(L"id")) == target_id)
                            require(source_y < bounds(card("relaciones/target/" + utf8(targets.GetObjectAt(index + 1).GetNamedString(L"id"))).target).top,
                                "The visible saved math source was restored under another target");
                }
                if (std::string_view(mode) == "partial")
                    require(bounds(card("relaciones/source/a").target).top < bounds(card("relaciones/target/cero").target).top,
                        "The unassigned partial AssetCheck card was silently assigned to a target");
                bool exercise_seen = false, feedback_seen = false;
                for (const auto& region : regions) if (region.block_index == 5) {
                    require(region.is_exercise && region.solved == correct,
                        "The effective AssetCheck reading regions disagree with the restored solved state");
                    exercise_seen = true;
                    if (region.feedback) {
                        require(region.correct == correct && region.source_text == expected_feedback,
                            "The effective AssetCheck feedback lost its original verdict or text");
                        feedback_seen = true;
                    }
                }
                require(exercise_seen && feedback_seen == (std::string_view(mode) != "partial"),
                    "The restored math view invented partial feedback or omitted evaluated feedback");
                assetcheck_reading(mounted, old, revised);
            };
            const auto restored = [&] {
                const bool partial = std::string_view(mode) == "partial";
                // Unreviewed partial drafts deliberately skip verify_saved_math;
                // they finish entry synchronously. Correct/reviewed receipts
                // must complete one real hook validation on every new view.
                const int expected_validations = partial ? 0 : mounted.openings;
                wait_for([&] {
                    const auto page = lesson_page_state(mounted.view);
                    const auto feedback = descendant(mounted.view, 1504);
                    const auto check = descendant(mounted.view, 1503);
                    const auto regions = lesson_reading_regions(mounted.view);
                    const auto sources = std::ranges::count_if(regions, [](const auto& region) {
                        return region.block_index == 5 && region.key.find("relaciones/source/") != std::string::npos;
                    });
                    const auto targets = std::ranges::count_if(regions, [](const auto& region) {
                        return region.block_index == 5 && region.key.find("relaciones/target/") != std::string::npos;
                    });
                    return mounted.validations.load() == expected_validations
                        && page.index == 2 && page.can_previous
                        && page.can_next == (std::string_view(mode) == "correct")
                        && check && static_cast<bool>(IsWindowEnabled(check)) == (std::string_view(mode) != "correct")
                        && sources == 4 && targets == 4
                        && feedback && edit_text(feedback) == expected_feedback;
                }, "The AssetCheck view did not finish its real receipt/draft restoration");
                preserved();
            };
            restored(); mounted.reopen(); restored();
            mounted.close();
            state::StateStore store(mounted.environment.paths.root);
            const auto physical = store.load();
            require(state::get_response(physical, after, revised)->raw_json == old_response
                && narration::read_history(physical) == old_history,
                "Physical math prompt state lost the original receipt or reading bookmark");
            mounted.open(); restored();
            if (!imported && std::string_view(mode) == "partial") {
                const auto tap = [&](std::string_view suffix) {
                    const auto regions = lesson_reading_regions(mounted.view);
                    const auto card = std::ranges::find_if(regions, [&](const auto& region) { return region.key.ends_with(suffix); });
                    require(card != regions.end() && card->target, "An AssetCheck matching card is missing from the current reader");
                    SendMessageW(card->target, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(2, 2));
                    SendMessageW(card->target, WM_LBUTTONUP, 0, MAKELPARAM(2, 2)); pump();
                };
                // Complete the partial draft with two wrong pairs, then repair
                // precisely those cards using the actual native interaction.
                tap("relaciones/source/a"); tap("relaciones/target/uno");
                tap("relaciones/source/b"); tap("relaciones/target/cero");
                SendMessageW(descendant(mounted.view, 1503), BM_CLICK, 0, 0);
                wait_for([&] { const auto value = state::get_response(mounted.saved(), after, revised);
                    return value && !value->correct && state::response_reviewed(*value, revised); },
                    "The revised prompt failed to record a real rejected matching attempt");
                require(edit_text(descendant(mounted.view, 1504)) == utf8(JsonObject::Parse(wide(worker.validate_math(
                    curriculum::math_evaluation_source(revised), utf8(assetcheck_answers(old, "incorrect").Stringify()), "es"))).GetNamedString(L"feedback")),
                    "The revised prompt changed the current rejected feedback");
                tap("relaciones/source/a"); tap("relaciones/target/cero");
                tap("relaciones/source/b"); tap("relaciones/target/uno");
                SendMessageW(descendant(mounted.view, 1503), BM_CLICK, 0, 0);
                wait_for([&] { const auto value = state::get_response(mounted.saved(), after, revised); return value && value->correct; },
                    "Correcting the AssetCheck cards did not produce a real current success");
                const auto success = utf8(JsonObject::Parse(wide(worker.validate_math(
                    curriculum::math_evaluation_source(revised), utf8(assetcheck_answers(old, "correct").Stringify()), "es"))).GetNamedString(L"feedback"));
                require(edit_text(descendant(mounted.view, 1504)) == success, "The current AssetCheck success lost its exact feedback");
                const auto fresh = state::get_response(mounted.saved(), after, revised)->raw_json;
                require(utf8(JsonObject::Parse(wide(fresh)).GetNamedString(L"contentFingerprint")) == fingerprint,
                    "A newly submitted math response did not use the authenticated original fingerprint");
                const auto events = mounted.new_feedback_events;
                mounted.reopen();
                wait_for([&] { const auto value = state::get_response(mounted.saved(), after, revised);
                    return value && value->correct && value->raw_json == fresh && edit_text(descendant(mounted.view, 1504)) == success; },
                    "The new successful AssetCheck response did not reopen exactly");
                require(mounted.new_feedback_events == events, "Reopening a new math success emitted another feedback event");
                require(lesson_page_state(mounted.view).can_next
                    && !IsWindowEnabled(descendant(mounted.view, 1503)),
                    "The reopened current math success did not unlock the real next-page gate");
            }
            for (const auto& [path, hash] : export_hashes)
                require(state::sha256(state::read_bytes(path)) == hash, "Importing math prompt state changed an original export");
            std::cout << "PASS AssetCheck prompt state=" << mode << " route=" << (imported ? "import" : "native")
                << " fingerprint=" << fingerprint << " preserved_reopen_feedback_events=0\n";
        }
    }
    require(state::read_bytes(before_path) == before_bytes && state::read_bytes(after_path) == after_bytes,
        "The native math prompt regression changed its authenticated fixtures");
    std::cout << "PASS six AssetCheck prompt native/import cases, real worker/current matching controls, exact reading archives; no audio playback or human review claimed\n";
}

}

int main(int argc, char** argv) {
    const bool u11_domain=argc==2&&std::string_view(argv[1])=="--u11-domain";
    const bool assetcheck_prompt=argc==4&&std::string_view(argv[1])=="--assetcheck-prompt";
    if (argc != 1&&!u11_domain&&!assetcheck_prompt) {
        const bool prepare = argc == 3 && std::string_view(argv[1]) == "--prepare-visual";
        const bool inspect = argc == 3 && std::string_view(argv[1]) == "--inspect-visual";
        const bool shape = prepare || inspect;
        const auto raw = shape ? std::string_view(argv[2]) : std::string_view{};
        const std::wstring token(raw.begin(), raw.end());
        if (!shape || !is_valid_qa_instance(token)) {
            std::cerr << "Usage: editorial_matching_view_tests [--assetcheck-prompt <before.json> <after.json> | --u11-domain | --prepare-visual <safe-new-QA-token> | --inspect-visual <safe-existing-QA-token>]\n";
            return 2;
        }
        // This branch only performs JSON/state/file operations. Native windows
        // and worker validation remain owned by the unchanged normal regression.
        try { if (inspect) inspect_visual_profile(token); else prepare_visual_profile(token); return 0; }
        catch (const std::exception& error) { std::cerr << "Visual QA operation failed: " << error.what() << '\n'; return 1; }
    }
    const auto ole = OleInitialize(nullptr);
    if (FAILED(ole)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&common);
    const auto rich_edit = LoadLibraryW(L"Msftedit.dll");
    int result = 0;
    try {
        require(rich_edit, "Cannot load native RichEdit for matching QA");
        reader_theme::prepare_fonts();
        if(assetcheck_prompt){windows::NarrationWorker worker(std::filesystem::path(build::source_root)/"out/narration/runtime.json");run_assetcheck_prompt(worker,argv[2],argv[3]);}
        else if(u11_domain){windows::NarrationWorker worker(std::filesystem::path(build::source_root)/"out/narration/runtime.json");run_u11_domain(worker);}
        else{
        const auto source = std::filesystem::path(build::source_root)
            / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido" / filename;
        const auto source_hash = state::sha256(state::read_bytes(source));
        const auto legacy = load_historical();
        const auto current = windows::load_lesson(source);
        windows::NarrationWorker worker(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        for (const auto mode : {"correct", "incorrect", "partial"}) run_case(legacy, current, worker, mode);
        require(state::sha256(state::read_bytes(source)) == source_hash, "Editorial matching QA modified the live curricular asset");
        }
    } catch (const winrt::hresult_error& error) {
        std::cerr << "FAIL editorial matching: " << winrt::to_string(error.message()) << '\n';
        result = 1;
    } catch (const std::exception& error) {
        std::cerr << "FAIL editorial matching: " << error.what() << '\n';
        result = 1;
    }
    if (rich_edit) FreeLibrary(rich_edit);
    OleUninitialize();
    return result;
}

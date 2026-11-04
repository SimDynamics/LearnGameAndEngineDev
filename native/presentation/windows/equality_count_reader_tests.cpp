#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <oleacc.h>
#include <richedit.h>
#include <wrl/client.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using winrt::Windows::Data::Json::JsonObject;
std::string phase = "initialization";
constexpr std::string_view owner_id = "block_0005_0002";
constexpr std::string_view target_id = "conteos_igualdad_en_cuatro_vectores";
constexpr std::string_view reference_id = "block_0006_0005";
constexpr std::string_view next_id = "block_0006_0003";
constexpr std::string_view expected_stdout = "1:0\n0:1\n0:1\nCoincidencias:2";
constexpr std::array<std::string_view, 4> labels{{"Vector vacío", "Un elemento: (R, 7)",
    "Un elemento: (R, 8)", "Cinco elementos: (R, 7), (R, 8), (R, 7), (S, 7), (R, 7)"}};
constexpr std::array<std::string_view, 4> field_ids{{"vacio", "coincide", "no_coincide", "repetidos"}};
void require(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(phase + ": " + std::string(reason));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void wait(const std::function<bool()>& complete, std::string_view reason) {
    const auto until = GetTickCount64() + 90000;
    do { pump(); if (complete()) return; MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT); }
    while (GetTickCount64() < until);
    throw std::runtime_error(phase + ": " + std::string(reason));
}
HWND find(HWND parent, int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND window, LPARAM raw) -> BOOL {
        auto& query = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(window) != query.id) return TRUE;
        query.result = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
const curriculum::Block& at(const curriculum::Lesson& lesson, std::string_view id) {
    const auto position = curriculum::stable_block_position(lesson, id);
    require(position.has_value(), "The published focal identity disappeared");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
std::string bytes(const std::filesystem::path& path) {
    const auto value = state::read_bytes(path); return {value.begin(), value.end()};
}
std::string crt_text(std::string_view value) {
    std::string result; for (const auto c : value) { if (c == '\n') result += '\r'; result += c; } return result;
}
bool same_response(const state::Response& left, const state::Response& right) {
    return left.answer == right.answer && left.correct == right.correct && left.raw_json == right.raw_json
        && left.selected == right.selected && left.order == right.order
        && left.reviewed_answer_identity == right.reviewed_answer_identity;
}

WindowsEnvironment visual_environment(std::wstring_view token) {
    require(is_valid_qa_instance(token) && token.starts_with(L"visual-equality-"), "Unsafe equality visual QA token");
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    const auto& root = environment.paths.root;
    require(root.filename() == token && root.parent_path().filename() == L"qa"
        && root.parent_path().parent_path().filename() == L"Codex", "Visual QA resolved outside its selected Codex instance");
    state::require_safe_path(root); return environment;
}
std::filesystem::path visual_asset() {
    return std::filesystem::path(build::source_root)
        / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json";
}
void prepare_visual_profile(std::wstring_view token) {
    phase = "preparing a new unanswered visual profile";
    const auto environment = visual_environment(token); const auto source = visual_asset();
    const auto original_hash = state::sha256(state::read_bytes(source)); const auto lesson = windows::load_lesson(source);
    require(curriculum::stable_block_position(lesson, owner_id) == curriculum::StatePosition{5, 2}
        && curriculum::stable_block_position(lesson, target_id) == curriculum::StatePosition{6, 3}
        && lesson.compiler_puzzles_required, "The actual visual owner/target contract changed");
    require(!std::filesystem::exists(environment.paths.root), "Visual preparation refuses to reuse an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    require(std::filesystem::create_directory(environment.paths.root), "Another process reserved this visual profile");
    initialize_profile_directories(environment.paths); state::StateStore store(environment.paths.root);
    require(store.load().preferences.empty(), "The new visual profile already contains state");
    state::StateData data; state::prepare_lesson_state(data, lesson); state::save_current_page(data, lesson, 5);
    auto& preferences = data.preferences["native_windows"];
    preferences["ui_language"] = std::string("es"); preferences["content_language"] = std::string("es");
    preferences["catalog/locale"] = std::string("es"); preferences["catalog/route"] = std::string("programming");
    preferences["catalog/course"] = std::string("cpp_fundamentals");
    const auto separator = lesson.id.rfind("__"); require(separator != std::string::npos, "The actual lesson has no catalog unit");
    preferences["catalog/unit"] = lesson.id.substr(0, separator); preferences["navigation/lesson"] = lesson.id;
    auto& speech = data.preferences["lesson_narration_settings"];
    speech["enabled"] = true; speech["controls_visible"] = true; speech["engine"] = std::string("KOKORO");
    speech["language"] = std::string("es"); speech["kokoro_voice:es"] = std::string("ef_dora");
    speech["rate"] = 1.; speech["kokoro_advance_generation"] = false;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        require(!state::get_response(data, lesson, block), "Preparation invented a quiz response");
        if (block.type == curriculum::BlockType::cpp_compiler)
            require(!windows::compiler_puzzle_completed(data, lesson, block)
                && windows::compiler_puzzle_case_count(data, lesson, block) == 0
                && !state::get_draft(data, curriculum::draft_key("programming", lesson, block), block.initial_code).has_saved,
                "Preparation invented a compiler achievement or saved source");
    }
    require(narration::read_history(data).entries.empty(), "Preparation invented played audio");
    state::validate_lesson_state(data, lesson); store.save(data); require(store.load() == data, "Prepared state did not round-trip exactly");
    require(state::sha256(state::read_bytes(source)) == original_hash, "Visual preparation modified the actual asset");
    std::cout << "qa_instance=" << utf8(token) << "\ndata_root=" << utf8(environment.paths.root.native())
        << "\nlesson=" << lesson.id << "\npage_index=5\ncompiler_receipts=0\ncompiler_executions=0\nsaved_source=0"
        << "\ntarget_response=none\nreading_history=none\nvoice=ef_dora\nrate=1\nwindows_created=0\n";
}
void inspect_visual_profile(std::wstring_view token) {
    using namespace winrt::Windows::Data::Json;
    phase = "read-only visual profile inspection";
    const auto environment = visual_environment(token); const auto root = environment.paths.root / "state";
    const auto active = root / "active/state.bin";
    require(std::filesystem::is_directory(root) && std::filesystem::is_regular_file(active), "Inspection requires an existing complete visual QA state");
    const auto tree = [&] {
        std::map<std::string, std::string> files;
        for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
            state::require_safe_path(item.path()); const auto key = utf8(item.path().lexically_relative(root).generic_wstring());
            require(item.is_directory() || item.is_regular_file(), "Inspection refuses special state-tree entries");
            files.emplace(key, item.is_directory() ? "directory" : "file:" + state::sha256(state::read_bytes(item.path())));
        }
        return files;
    };
    const auto tree_hash = [](const auto& files) {
        std::string framed; for (const auto& [key, value] : files)
            framed += std::to_string(key.size()) + ':' + key + std::to_string(value.size()) + ':' + value;
        return state::sha256(framed);
    };
    const auto before = tree(); const auto source = visual_asset(); const auto source_hash = state::sha256(state::read_bytes(source));
    const auto lesson = windows::load_lesson(source); const auto& owner = at(lesson, owner_id); const auto& target = at(lesson, target_id);
    // StateStore construction/load only read. Do not initialize directories,
    // prepare compatibility metadata, save, run a worker or create native UI.
    state::StateStore store(environment.paths.root); const auto data = store.load(); state::validate_lesson_state(data, lesson);
    const auto response = state::get_response(data, lesson, target); const auto history = narration::read_history(data);
    const auto page = state::current_page(data, lesson); const auto completed = state::find(data, "learning_progress", "lesson_" + lesson.id + "_completed");
    require(history.entries.size() <= 3 && (!completed || (std::get_if<bool>(completed) && !std::get<bool>(*completed))),
        "Inspection found an unbounded history or completed focal lesson");
    const auto draft = state::get_draft(data, curriculum::draft_key("programming", lesson, owner), owner.initial_code);
    JsonObject result;
    const auto text = [&](const wchar_t* key, const std::string& value) { result.Insert(key, JsonValue::CreateStringValue(wide(value))); };
    const auto number = [&](const wchar_t* key, double value) { result.Insert(key, JsonValue::CreateNumberValue(value)); };
    const auto flag = [&](const wchar_t* key, bool value) { result.Insert(key, JsonValue::CreateBooleanValue(value)); };
    text(L"qa_instance", utf8(token)); text(L"data_root", utf8(environment.paths.root.native())); text(L"asset_sha256", source_hash);
    text(L"state_file", utf8(active.native())); text(L"state_sha256_before", before.at("active/state.bin").substr(5));
    number(L"current_page_index", static_cast<double>(page));
    number(L"scroll", static_cast<double>(state::integer_value(data, "native_windows", "scroll/" + lesson.id + '/' + lesson.pages.at(page).stable_id)));
    number(L"owner_receipts", static_cast<double>(windows::compiler_puzzle_case_count(data, lesson, owner)));
    flag(L"owner_completed", windows::compiler_puzzle_completed(data, lesson, owner)); flag(L"source_has_saved", draft.has_saved);
    text(L"source_sha256", state::sha256(draft.source)); flag(L"source_matches_reference", draft.source == at(lesson, reference_id).code);
    std::size_t run_directories = 0, executions = 0; const auto runs = environment.paths.projects / "cpp-runs";
    if (std::filesystem::exists(runs)) for (const auto& item : std::filesystem::directory_iterator(runs)) {
        state::require_safe_path(item.path()); if (!item.is_directory()) continue; ++run_directories;
        if (std::filesystem::is_regular_file(item.path() / "execution-stdout.bin")
            && std::filesystem::is_regular_file(item.path() / "execution-stderr.bin")) ++executions;
    }
    number(L"native_run_directories", static_cast<double>(run_directories)); number(L"captured_execution_count", static_cast<double>(executions));
    flag(L"target_response_present", response.has_value()); flag(L"target_correct", response && response->correct);
    flag(L"target_reviewed", response && state::response_reviewed(*response, target));
    text(L"target_payload_sha256", response ? state::sha256(response->raw_json) : "");
    JsonObject values;
    if (response) {
        const auto raw = JsonObject::Parse(wide(response->raw_json));
        const auto responses = raw.GetNamedObject(L"responses", JsonObject());
        values = responses.GetNamedObject(L"conteos", JsonObject()).GetNamedObject(L"values", JsonObject());
    }
    result.Insert(L"target_fields", values); flag(L"lesson_completed", false);
    text(L"active_key", history.active_key); text(L"active_owner", history.active_owner); JsonArray slots;
    for (const auto& mark : history.entries) {
        JsonObject slot; slot.Insert(L"key", JsonValue::CreateStringValue(wide(mark.key))); slot.Insert(L"page", JsonValue::CreateStringValue(wide(mark.page)));
        slot.Insert(L"segment", JsonValue::CreateNumberValue(static_cast<double>(mark.position.segment)));
        slot.Insert(L"offset_ms", JsonValue::CreateNumberValue(static_cast<double>(mark.position.milliseconds)));
        slot.Insert(L"heard_through", JsonValue::CreateNumberValue(static_cast<double>(mark.position.heard_through)));
        slot.Insert(L"finished", JsonValue::CreateBooleanValue(mark.position.finished)); slots.Append(slot);
    }
    result.Insert(L"history", slots); const auto after = tree();
    require(before == after && state::sha256(state::read_bytes(source)) == source_hash,
        "QA state or asset changed during inspection; no stable read-only snapshot is claimed");
    text(L"state_sha256_after", after.at("active/state.bin").substr(5));
    text(L"state_tree_sha256_before", tree_hash(before)); text(L"state_tree_sha256_after", tree_hash(after)); flag(L"read_only", true);
    std::cout << utf8(result.Stringify()) << '\n';
}

struct Harness {
    std::shared_ptr<windows::NarrationWorker> backend;
    WindowsEnvironment environment;
    curriculum::Lesson lesson, control_lesson;
    std::filesystem::path asset, control_asset;
    std::vector<std::uint8_t> original, control_original;
    HWND host = nullptr, view = nullptr;
    std::unique_ptr<LessonReader> reader;
    state::Preferences unrelated;
    std::vector<narration::Bookmark> other_slots;
    struct Recording { std::string hash; std::filesystem::file_time_type modified; };
    std::map<std::filesystem::path, Recording> recordings;
    explicit Harness(std::shared_ptr<windows::NarrationWorker> worker);
    ~Harness() { if (std::uncaught_exceptions()) backend->shutdown(); close(); if (host) DestroyWindow(host); }
    void close() { reader.reset(); if (view) { (void)flush_lesson_view(view); DestroyWindow(view); view = nullptr; } }
    void open(const curriculum::Lesson& model, std::size_t page);
    LessonReaderSnapshot snapshot() const {
        const auto value = reader->snapshot(); require(value.error.empty(), "Actual reader error: " + value.error); return value;
    }
    void ready() const {
        wait([&] { const auto value = snapshot(); return value.planned && !value.segments.empty()
            && value.index < value.segments.size() && value.segments[value.index].available; }, "Current actual sentence did not become available");
    }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot flush the isolated learner state");
        state::StateStore store(environment.paths.root); return store.load();
    }
    HWND control(std::size_t block, int part) const {
        const auto result = find(view, 1000 + static_cast<int>(block) * 100 + part);
        require(result != nullptr, "An actual lesson control is missing"); return result;
    }
    void edit(HWND field, std::string_view value) const {
        require(IsWindowEnabled(field), "The actual input is unexpectedly disabled");
        CHARRANGE all{0, -1}; SendMessageW(field, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto text = wide(value); SendMessageW(field, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
        require(edit_text(field) == value, "The actual input changed supplied characters");
    }
    // All methods below exercise the actual controls/backend. No response,
    // receipt, audio certificate or narration slot is manufactured by this QA.
    std::array<HWND, 4> fields() const;
    void values(const std::array<std::string_view, 4>& expected, bool write = false) const;
    void locked() const;
    void compile(std::string_view source, bool accepted);
    void verify_recording(const LessonReaderSegmentSnapshot& part);
    narration::Bookmark bookmark(std::string_view key) const;
    LessonReaderSnapshot real_pause(std::string_view source_contains = {});
    void preserved(std::size_t receipts) const;
    void submit(bool correct);
};

Harness::Harness(std::shared_ptr<windows::NarrationWorker> worker) : backend(std::move(worker)) {
    const auto content = std::filesystem::path(build::source_root) / "app/src/main/assets/content/es/programming/cpp_fundamentals";
    asset = content / "datos_propios_y_estado_valido/lesson_operaciones_auxiliares_y_significado_de_igualdad.json";
    control_asset = content / "programas_y_ejecucion/lesson_texto_secuencia_y_formato.json";
    original = state::read_bytes(asset); control_original = state::read_bytes(control_asset);
    lesson = windows::load_lesson(asset); control_lesson = windows::load_lesson(control_asset);
    require(lesson.pages.size() == 7 && lesson.compiler_puzzles_required
        && curriculum::stable_block_position(lesson, owner_id) == curriculum::StatePosition{5, 2}
        && curriculum::stable_block_position(lesson, target_id) == curriculum::StatePosition{6, 3}
        && curriculum::stable_block_position(lesson, next_id) == curriculum::StatePosition{6, 4}
        && at(lesson, target_id).type == curriculum::BlockType::math_puzzle
        && at(lesson, next_id).type == curriculum::BlockType::output_prediction,
        "The current published U9L12 owner/target/next contract changed");
    const auto& owner = at(lesson, owner_id); const auto dependencies = curriculum::activity_prerequisites(lesson, at(lesson, target_id));
    require(owner.type == curriculum::BlockType::cpp_compiler && owner.output_check && owner.output_check->cases.size() == 1
        && owner.output_check->cases[0].standard_input.empty()
        && owner.output_check->cases[0].outputs == std::vector<std::string>{std::string(expected_stdout)}
        && dependencies == std::vector<curriculum::StatePosition>{{5, 2}}
        && !at(lesson, reference_id).code.empty(), "The independent four-line owner oracle changed");
    const auto spec = JsonObject::Parse(wide(at(lesson, target_id).raw_json));
    const auto response = spec.GetNamedArray(L"responses").GetObjectAt(0);
    require(response.GetNamedString(L"type") == L"math_multi_input" && response.GetNamedString(L"id") == L"conteos"
        && response.GetNamedArray(L"fields").Size() == 4, "The actual four-field composition changed");
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto item = response.GetNamedArray(L"fields").GetObjectAt(static_cast<std::uint32_t>(i));
        require(item.GetNamedString(L"label") == wide(labels[i]) && item.GetNamedString(L"id") == wide(field_ids[i]),
            "The semantic input label/identity changed");
    }
    const auto token = L"equality-reader-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto base = std::filesystem::weakly_canonical(std::filesystem::path(build::source_root)) / L"out/qa/equality-count-reader";
    require(base.is_absolute(), "QA state requires an absolute checkout location"); state::require_safe_path(base);
    environment.options = {ProfileId::Codex, token, false}; environment.paths = make_profile_paths(base, ProfileId::Codex, token);
    environment.paths.cache = std::filesystem::path(build::source_root) / "out/qa/lesson-reader-audio";
    const std::filesystem::path build_base = L"C:/LGP-ToolchainBuilds/LGAED-EqualityReader-QA";
    environment.paths.projects = build_base / token;
    state::require_safe_path(environment.paths.root); state::require_safe_path(environment.paths.cache); state::require_safe_path(environment.paths.projects);
    require(!std::filesystem::exists(environment.paths.root) && !std::filesystem::exists(environment.paths.projects)
        && std::filesystem::weakly_canonical(environment.paths.projects).parent_path() == std::filesystem::weakly_canonical(build_base),
        "Refusing to reuse another profile or compiler directory");
    initialize_profile_directories(environment.paths);
    state::StateData initial; state::prepare_lesson_state(initial, lesson); state::prepare_lesson_state(initial, control_lesson);
    // A late saved address is the gate/redirect input; it credits no answer.
    state::save_current_page(initial, lesson, 6);
    initial.preferences["native_windows"]["ui_language"] = std::string("es");
    initial.preferences["equality_reader_unrelated"]["opaque"] = std::string(" \t preserve\r\n");
    unrelated = initial.preferences.at("equality_reader_unrelated");
    auto& settings = initial.preferences["lesson_narration_settings"];
    settings["engine"] = std::string("KOKORO"); settings["language"] = std::string("es");
    settings["kokoro_voice:es"] = std::string("ef_dora"); settings["rate"] = 3.; settings["kokoro_advance_generation"] = false;
    require(windows::compiler_puzzle_case_count(initial, lesson, owner) == 0
        && !windows::compiler_puzzle_completed(initial, lesson, owner)
        && !state::get_response(initial, lesson, at(lesson, target_id)) && narration::read_history(initial).entries.empty(),
        "The focal profile began with invented credit or bookmarks");
    state::StateStore store(environment.paths.root); (void)store.load(); store.save(initial);
    host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Actual equality count reader QA",
        WS_OVERLAPPEDWINDOW, 0, 0, 1000, 780, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "Cannot create the isolated hidden QA host");
}
void Harness::open(const curriculum::Lesson& model, std::size_t page) {
    require(!view && !reader, "Close the previous QA view before mounting another lesson");
    LessonViewHooks hooks;
    hooks.before_page_change = [&](HWND) { if (reader) reader->pause(); };
    hooks.before_destroy = [&](HWND) { reader.reset(); };
    hooks.content_changed = [&](HWND) { if (reader) reader->content_changed(); };
    hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (reader) reader->feedback(block, correct); };
    hooks.priority_work = [worker = backend](std::stop_token stop) -> std::shared_ptr<void> {
        return std::make_shared<windows::NarrationPriorityLease>(worker->heavy_work_lease(stop));
    };
    hooks.validate_math = [worker = backend](std::string_view raw, std::string_view answers, std::string_view locale, std::stop_token stop) {
        return worker->validate_math(raw, answers, locale, stop);
    };
    hooks.math_editor_request = [worker = backend](std::string_view request) { return worker->math_editor(request); };
    view = create_lesson_view(host, environment, model, std::move(hooks));
    require(view != nullptr, "Cannot create the actual lesson view"); MoveWindow(view, 0, 0, 980, 720, TRUE); pump();
    require(lesson_page_state(view).index == page && !IsWindowVisible(host), "The actual QA page resolved incorrectly or exposed the desktop");
    reader = std::make_unique<LessonReader>(view, environment, backend, [] {}); ready();
}
std::array<HWND, 4> Harness::fields() const {
    struct Probe { std::array<HWND, 4> found{}; std::size_t count = 0; std::exception_ptr error; } probe;
    EnumChildWindows(control(3, 5), [](HWND window, LPARAM raw) -> BOOL {
        auto& probe = *reinterpret_cast<Probe*>(raw);
        try {
            if (math_field_keyboard_context(window).empty()) return TRUE;
            Microsoft::WRL::ComPtr<IAccessible> accessible;
            require(SUCCEEDED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(accessible.GetAddressOf()))),
                "An actual mathematical input has no MSAA provider");
            VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF; BSTR native = nullptr;
            const auto result = accessible->get_accName(self, &native);
            const auto name = native ? utf8(std::wstring_view(native, SysStringLen(native))) : std::string{}; SysFreeString(native);
            require(result == S_OK, "An actual mathematical input has no accessibility name");
            const auto match = std::ranges::find(labels, name);
            require(match != labels.end(), "An actual input is not identified by its published semantic label");
            const auto index = static_cast<std::size_t>(match - labels.begin());
            require(probe.found[index] == nullptr, "A semantic input accessibility name is duplicated");
            probe.found[index] = window; ++probe.count;
        } catch (...) { probe.error = std::current_exception(); return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&probe));
    if (probe.error) std::rethrow_exception(probe.error);
    require(probe.count == 4, "The actual table does not expose exactly its four semantic inputs"); return probe.found;
}
void Harness::values(const std::array<std::string_view, 4>& expected, bool write) const {
    const auto inputs = fields();
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (write) edit(inputs[i], expected[i]);
        require(edit_text(inputs[i]) == expected[i], "A real table field did not preserve its exact draft value");
    }
    const auto response = state::get_response(saved(), lesson, at(lesson, target_id));
    require(response.has_value(), "A composed four-field draft was not persisted");
    const auto actual = JsonObject::Parse(wide(response->raw_json)).GetNamedObject(L"responses").GetNamedObject(L"conteos").GetNamedObject(L"values");
    require(actual.Size() <= 4, "The saved composition added another semantic input");
    for (const auto& item : actual)
        require(std::ranges::find(field_ids, winrt::to_string(item.Key())) != field_ids.end(), "The saved composition added an unknown semantic input");
    for (std::size_t i = 0; i < field_ids.size(); ++i)
        require(actual.GetNamedString(wide(field_ids[i]), L"") == wide(expected[i]), "The persisted semantic field differs from the visible actual draft");
}
void Harness::locked() const {
    require(lesson_page_state(view).index == 5 && !navigate_lesson_view(view, 6), "The dependent page bypassed its unresolved real compiler");
    const auto secret = at(lesson, target_id).prompt;
    for (const auto& region : lesson_reading_regions(view)) require(region.source_text.find(secret) == std::string::npos,
        "The locked target leaked into narration");
    struct Probe { std::string secret; bool leaked = false; } probe{secret};
    EnumChildWindows(view, [](HWND window, LPARAM raw) -> BOOL {
        auto& probe = *reinterpret_cast<Probe*>(raw);
        Microsoft::WRL::ComPtr<IAccessible> accessible;
        if (FAILED(AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(accessible.GetAddressOf())))) return TRUE;
        VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF;
        for (const bool value : {false, true}) {
            BSTR native = nullptr; if (value) accessible->get_accValue(self, &native); else accessible->get_accName(self, &native);
            if (native) { probe.leaked |= utf8(std::wstring_view(native, SysStringLen(native))).find(probe.secret) != std::string::npos; SysFreeString(native); }
        }
        return !probe.leaked;
    }, reinterpret_cast<LPARAM>(&probe));
    require(!probe.leaked && !state::get_response(saved(), lesson, at(lesson, target_id)), "The locked target exposed its prompt or invented a response");
}
void Harness::compile(std::string_view source, bool accepted) {
    reader->pause(); edit(control(2, 10), source);
    const auto directories = [&] { std::set<std::filesystem::path> result; const auto base = environment.paths.projects / "cpp-runs";
        if (std::filesystem::exists(base)) for (const auto& entry : std::filesystem::directory_iterator(base)) if (entry.is_directory()) result.insert(entry.path());
        return result; };
    const auto before = directories(); const auto button = control(2, 11); require(IsWindowEnabled(button), "Actual compiler action is disabled");
    SendMessageW(button, BM_CLICK, 0, 0); require(!IsWindowEnabled(button), "The actual compiler did not start");
    wait([&] { return IsWindowEnabled(button); }, "The real MSVC run did not finish"); reader->pause(); ready();
    std::vector<std::filesystem::path> created; for (const auto& item : directories()) if (!before.contains(item)) created.push_back(item);
    require(created.size() == 1, "An actual execution did not produce exactly one fresh native directory");
    const auto directory = created.front(); DWORD kind = 0;
    require(bytes(directory / "main.cpp") == source && bytes(directory / "stdin.bin").empty()
        && bytes(directory / "execution-stdout.bin") == (accepted ? crt_text(expected_stdout) : std::string{})
        && bytes(directory / "execution-stderr.bin").empty()
        && GetBinaryTypeW((directory / "program.exe").c_str(), &kind) && kind == SCS_64BIT_BINARY
        && bytes(directory / "toolchain.txt").find("compiler=" + utf8((std::filesystem::path(build::educational_vc_root) / L"bin/Hostx64/x64/cl.exe").wstring()) + '\n') != std::string::npos
        && edit_text(control(2, 34)) == "Ejecución terminada (código 0).",
        "The actual MSVC source/input/stdout/stderr/exit/toolchain differs from the independent oracle");
    preserved(accepted ? 1 : 0);
    std::cout << "actual_msvc=" << (accepted ? "reference four exact lines" : "unanswered initial stub, empty stdout") << " exit=0 stderr_bytes=0\n";
}

void Harness::verify_recording(const LessonReaderSegmentSnapshot& part) {
    require(part.available && !part.recording.empty(), "The played sentence has no actual recording");
    const auto data = state::read_bytes(part.recording);
    const auto u16 = [&](std::size_t at) { return unsigned(data[at]) | (unsigned(data[at + 1]) << 8); };
    const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
    const auto tag = [&](std::size_t at, std::string_view text) { return std::equal(text.begin(), text.end(), data.begin() + at); };
    require(data.size() > 44 && tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data")
        && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000 && u16(34) == 16
        && u32(40) == data.size() - 44 && (data.size() - 44) % 2 == 0
        && std::ranges::any_of(data.begin() + 44, data.end(), [](auto sample) { return sample != 0; })
        && std::isfinite(part.duration_ms) && std::abs(part.duration_ms - (data.size() - 44) / 48.) <= 1
        && !part.timings.empty(), "The actual sentence lacks non-silent mono 24 kHz PCM16 with word timings");
    double previous_start = 0;
    for (const auto& word : part.timings) {
        require(word.start < word.end && word.end <= wide(part.source).size()
            && std::isfinite(word.start_ms) && std::isfinite(word.end_ms) && word.start_ms >= previous_start
            && word.end_ms >= word.start_ms && word.end_ms <= part.duration_ms + 1,
            "The actual word alignment does not address valid UTF-16 source and PCM bounds");
        previous_start = word.start_ms;
    }
    const Recording evidence{state::sha256(data), std::filesystem::last_write_time(part.recording)};
    if (const auto found = recordings.find(part.recording); found != recordings.end())
        require(found->second.hash == evidence.hash && found->second.modified == evidence.modified,
            "An already observed compatible recording was rewritten");
    else recordings.emplace(part.recording, evidence);
}
narration::Bookmark Harness::bookmark(std::string_view key) const {
    const auto history = narration::read_history(saved());
    const auto found = std::ranges::find(history.entries, key, &narration::Bookmark::key);
    require(found != history.entries.end(), "The actually played page has no saved bookmark"); return *found;
}
LessonReaderSnapshot Harness::real_pause(std::string_view source_contains) {
    ready(); if (!snapshot().playing) reader->toggle();
    wait([&] { const auto value = snapshot(); const auto& part = value.segments[value.index];
        return value.playing && value.allocated && value.playback_rate == 3. && part.available
            && (source_contains.empty() || part.source.find(source_contains) != std::string::npos)
            && value.offset > 150 && value.offset + 200 < part.duration_ms;
    }, "The requested actual source did not play far enough for a real released pause");
    reader->pause(); const auto paused = snapshot();
    require(!paused.playing && !paused.allocated && !paused.finished && paused.offset > 150,
        "The actual pause did not release the player and preserve its partial position");
    verify_recording(paused.segments[paused.index]);
    const auto stored = bookmark(paused.key);
    require(stored.position.segment == static_cast<std::int64_t>(paused.index)
        && stored.position.milliseconds == paused.offset && stored.position.heard_through == paused.heard_through
        && stored.position.heard_prefix && stored.position.heard_prefix->microseconds > 0,
        "The saved pause lacks the exact position and a prefix derived from actual played PCM");
    return paused;
}
void Harness::preserved(std::size_t receipts) const {
    const auto data = saved(); const auto& owner = at(lesson, owner_id);
    require(data.preferences.at("equality_reader_unrelated") == unrelated
        && windows::compiler_puzzle_case_count(data, lesson, owner) == receipts
        && windows::compiler_puzzle_completed(data, lesson, owner) == (receipts == 1),
        "A focal action changed unrelated data or the real compiler achievement");
    require(state::read_bytes(asset) == original && state::read_bytes(control_asset) == control_original,
        "Technical QA changed an authentic curricular asset");
    const auto history = narration::read_history(data);
    require(history.entries.size() <= 3, "The real reader exceeded its three saved positions");
    for (const auto& expected : other_slots) {
        const auto found = std::ranges::find(history.entries, expected.key, &narration::Bookmark::key);
        require(found != history.entries.end() && *found == expected, "A focal operation changed another real played page's exact saved position");
    }
    const auto next = state::get_response(data, lesson, at(lesson, next_id));
    require(!next || !next->correct, "The focal flow invented a response to the following pending puzzle");
    const auto completed = state::find(data, "learning_progress", "lesson_" + lesson.id + "_completed");
    require(!completed || !std::get_if<bool>(completed) || !std::get<bool>(*completed),
        "The focal flow completed a lesson with another unanswered puzzle");
    for (const auto& [path, evidence] : recordings)
        require(state::sha256(state::read_bytes(path)) == evidence.hash && std::filesystem::last_write_time(path) == evidence.modified,
            "An actual compatible WAV was removed, rewritten or replaced");
}
void Harness::submit(bool correct) {
    reader->pause(); const auto button = control(3, 3);
    require(IsWindowEnabled(button), "The actual four-field Check action is unavailable");
    SendMessageW(button, BM_CLICK, 0, 0);
    const auto body = winrt::to_string(JsonObject::Parse(wide(at(lesson, target_id).raw_json))
        .GetNamedObject(L"feedback").GetNamedString(correct ? L"correct" : L"incorrect"));
    wait([&] {
        const auto data = read_lesson_state(view); const auto response = state::get_response(data, lesson, at(lesson, target_id));
        if (!response || (!response->correct && !state::response_reviewed(*response, at(lesson, target_id)))) return false;
        const auto regions = lesson_reading_regions(view);
        return std::ranges::any_of(regions, [&](const auto& region) {
            return region.block_index == 3 && region.feedback && region.correct == correct
                && region.source_text.find(body) != std::string::npos;
        });
    }, "The canonical validator did not expose the exact authored feedback for the actual four fields");
    reader->pause(); ready();
    const auto paused = snapshot(); require(!paused.playing && !paused.allocated, "A manually paused submission started playback automatically");
    for (const auto& region : lesson_reading_regions(view)) if (region.block_index == 3)
        require(region.reading_context == "auto", "A mathematical composition inherited the C++ code-only reading context");
    preserved(1);
}

void run(const std::shared_ptr<windows::NarrationWorker>& backend) {
    Harness h(backend);
    phase = "real control-page bookmark";
    h.open(h.control_lesson, 0);
    const auto control_pause = h.real_pause();
    h.other_slots.push_back(h.bookmark(control_pause.key)); h.close();

    phase = "unanswered real compiler gate";
    h.open(h.lesson, 5); h.locked();
    h.compile(at(h.lesson, owner_id).initial_code, false); h.locked();
    phase = "actual four-line MSVC construction";
    h.compile(at(h.lesson, reference_id).code, true);
    const auto owner_pause = h.real_pause("Igualdad completa");
    h.other_slots.push_back(h.bookmark(owner_pause.key));
    require(h.other_slots.size() == 2 && h.other_slots[0].key != h.other_slots[1].key,
        "The two other slots were not earned on distinct actual pages");
    require(navigate_lesson_view(h.view, 6), "The real accepted construction did not unlock its following page");
    h.reader->content_changed(); h.ready(); h.preserved(1);

    const auto& goal = at(h.lesson, target_id);
    const auto feedback = JsonObject::Parse(wide(goal.raw_json)).GetNamedObject(L"feedback");
    const auto guide = winrt::to_string(feedback.GetNamedString(L"incorrect"));
    const auto success = winrt::to_string(feedback.GetNamedString(L"correct"));
    const auto feedback_text = [&](std::string_view body, std::string_view forbidden) {
        const auto regions = lesson_reading_regions(h.view);
        const auto found = std::ranges::find_if(regions, [](const auto& region) { return region.block_index == 3 && region.feedback; });
        require(found != regions.end() && found->source_text.find(body) != std::string::npos
            && found->source_text.find(forbidden) == std::string::npos
            && edit_text(found->target).find(body) != std::string::npos,
            "The visible and narrated feedback leaked another verdict or lost its complete authored explanation");
    };
    const auto response = [&] {
        const auto result = state::get_response(h.saved(), h.lesson, goal);
        require(result.has_value(), "The actual composed answer disappeared"); return *result;
    };
    const auto settled_feedback = [&](bool correct) {
        wait([&] { const auto current = h.snapshot();
            return current.planned && std::ranges::any_of(current.segments, [&](const auto& part) {
                return part.block == 3 && part.feedback && part.correct == correct;
            });
        }, "Reopening did not finish revalidating and planning the stored mathematical feedback");
        h.ready();
    };
    const auto feedback_recordings = [&](bool correct) {
        const auto current = h.snapshot(); std::size_t count = 0, last = 0;
        for (std::size_t i = 0; i < current.segments.size(); ++i) {
            const auto& part = current.segments[i];
            if (part.block != 3 || !part.feedback || part.correct != correct) continue;
            require(part.available, "A played feedback sentence has no real recording");
            h.verify_recording(part); ++count; last = i;
        }
        require(count > 1 && (!correct || current.heard_through > static_cast<std::int64_t>(last)),
            "The complete authored feedback lost sentences or genuine correct-feedback listening credit");
    };
    const auto restore = [&](const LessonReaderSnapshot& paused, const state::Response& answer,
        const std::array<std::string_view, 4>& values) {
        const auto history = narration::read_history(h.saved());
        const auto recording = paused.segments.at(paused.index).recording;
        h.close(); h.open(h.lesson, 6);
        if (answer.correct || state::response_reviewed(answer, goal)) settled_feedback(answer.correct);
        h.values(values);
        const auto restored = h.snapshot();
        require(restored.key == paused.key && restored.index == paused.index && restored.offset == paused.offset
            && restored.heard_through == paused.heard_through && !restored.playing && !restored.allocated && !restored.finished
            && restored.segments.at(restored.index).recording == recording
            && same_response(response(), answer) && narration::read_history(h.saved()) == history,
            "Full-view reopening changed the real released pause, composed answer or bounded history");
        h.verify_recording(restored.segments.at(restored.index)); h.preserved(1);
    };
    const auto resume = [&](const LessonReaderSnapshot& paused) {
        h.reader->toggle();
        wait([&] { const auto current = h.snapshot();
            require(current.key == paused.key && current.index == paused.index,
                "Explicit resume selected a different actual recording");
            return current.playing && current.allocated && current.offset > paused.offset + 20;
        }, "The released pause did not resume its real PCM at the saved offset");
    };

    phase = "actual four-field prompt and unsubmitted draft";
    for (const auto input : h.fields()) require(edit_text(input).empty(), "The fresh table began with a supplied answer");
    const auto regions = lesson_reading_regions(h.view);
    for (const auto label : labels) require(std::ranges::any_of(regions, [&](const auto& region) {
        return region.block_index == 3 && region.source_text == label;
    }), "An actual table field lost its authored narration label");
    require(std::ranges::none_of(regions, [](const auto& region) { return region.block_index == 3 && region.feedback; }),
        "The unanswered table revealed feedback");
    const auto prompt_pause = h.real_pause("La medida buscada");
    constexpr std::array<std::string_view, 4> partial{{"0", "1", "", ""}};
    h.values(partial, true);
    const auto draft = response();
    require(!draft.correct && !state::response_reviewed(draft, goal), "A partial unsubmitted draft became a checked answer");
    restore(prompt_pause, draft, partial);

    phase = "real rejected four-field attempt and guide";
    constexpr std::array<std::string_view, 4> wrong{{"0", "1", "0", "2"}};
    h.values(wrong, true); h.submit(false); feedback_text(guide, success);
    const auto rejected = response();
    require(!rejected.correct && state::response_reviewed(rejected, goal), "The real incorrect submission lost its review identity");
    const auto wrong_pause = h.real_pause("Compara cada elemento");
    restore(wrong_pause, rejected, wrong); feedback_text(guide, success); resume(wrong_pause);
    wait([&] { const auto current = h.snapshot(); const auto& part = current.segments.at(current.index);
        return !current.playing && !current.allocated && !current.preparing && !current.finished
            && part.block == 3 && part.feedback && !part.correct && part.duration_ms > 0
            && current.offset >= part.duration_ms - 40;
    }, "The complete incorrect guide did not finish and stop for a retry");
    feedback_recordings(false);
    require(same_response(response(), rejected) && !state::get_response(h.saved(), h.lesson, at(h.lesson, next_id)),
        "Reading the guide changed the rejected answer or answered the later puzzle");
    h.preserved(1);

    phase = "correcting only the repeated-elements count";
    constexpr std::array<std::string_view, 4> correct{{"0", "1", "0", "3"}};
    h.edit(h.fields()[3], correct[3]); h.values(correct); h.submit(true); feedback_text(success, guide);
    const auto accepted = response();
    require(accepted.correct, "The public 0/1/0/3 answers were not accepted by the actual math backend");
    for (const auto input : h.fields()) require(!IsWindowEnabled(input) || (GetWindowLongPtrW(input, GWL_STYLE) & ES_READONLY),
        "An accepted answer remained editable");
    const auto correct_pause = h.real_pause("El vector vacío");
    restore(correct_pause, accepted, correct); feedback_text(success, guide); resume(correct_pause);
    phase = "heard correct feedback stops at the next unanswered puzzle";
    wait([&] { const auto current = h.snapshot(); const auto& part = current.segments.at(current.index);
        return current.waiting_gate && !current.playing && !current.allocated && !current.finished
            && part.block == curriculum::stable_block_position(h.lesson, next_id)->block;
    }, "Correct feedback did not continue normally to the next real pending puzzle");
    feedback_recordings(true);
    h.reader->pause(); const auto stopped = h.snapshot();
    h.close(); h.open(h.lesson, 6); settled_feedback(true); h.values(correct);
    const auto reopened = h.snapshot();
    require(reopened.key == stopped.key && reopened.index == stopped.index && reopened.offset == stopped.offset
        && !reopened.playing && !reopened.allocated && !reopened.finished && same_response(response(), accepted)
        && !state::get_response(h.saved(), h.lesson, at(h.lesson, next_id))
        && narration::read_history(h.saved()).entries.size() == 3,
        "The final reopen lost the accepted composition or fabricated completion beyond the pending puzzle");
    h.preserved(1);
    std::cout << "PASS current U9L12 four-field count: two actual MSVC runs and source/stdout/stderr oracle, protected owner gate, "
        "native 0/1/0/3 inputs with unsubmitted/rejected/accepted restore, full current guide and success PCM/UTF-16 timings, "
        "released exact pause/reopen/resume, three genuinely played page slots, unchanged WAVs/assets/compiler proof and no later completion. "
        "Human listening and curricular visual acceptance remain separate.\n";
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    if (argc != 1) {
        const bool prepare = argc == 3 && std::string_view(argv[1]) == "--prepare-visual";
        const bool inspect = argc == 3 && std::string_view(argv[1]) == "--inspect-visual";
        if (!prepare && !inspect) {
            std::cerr << "Usage: equality_count_reader_tests [--prepare-visual visual-equality-<new-token> | --inspect-visual visual-equality-<existing-token>]\n"; return 2;
        }
        try { const auto token = wide(argv[2]); if (prepare) prepare_visual_profile(token); else inspect_visual_profile(token); return 0; }
        catch (const std::exception& error) { std::cerr << "Visual equality QA operation failed: " << error.what() << '\n'; return 1; }
    }
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try {
        require(rich != nullptr, "Cannot load the actual RichEdit controls"); reader_theme::prepare_fonts();
        auto backend = std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        struct Shutdown { std::shared_ptr<windows::NarrationWorker> worker; ~Shutdown() { worker->shutdown(); } } cleanup{backend};
        run(backend);
    } catch (const std::exception& error) { std::cerr << "FAIL equality-count reader: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

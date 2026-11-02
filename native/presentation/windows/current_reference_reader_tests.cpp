#include "core/attempts.hpp"
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <oleacc.h>
#include <richedit.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
std::string phase = "initialization";
void require(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(phase + ": " + std::string(reason));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void wait(const std::function<bool()>& complete, std::string_view description) {
    const auto until = GetTickCount64() + 90000;
    do {
        pump(); if (complete()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < until);
    throw std::runtime_error(phase + ": " + std::string(description));
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
std::string bytes(const std::filesystem::path& file) {
    const auto data = state::read_bytes(file); return {data.begin(), data.end()};
}
std::string crt_text(std::string_view value) {
    std::string result; for (const auto character : value) { if (character == '\n') result += '\r'; result += character; }
    return result;
}
bool same_response(const state::Response& left, const state::Response& right) {
    return left.answer == right.answer && left.correct == right.correct && left.raw_json == right.raw_json
        && left.selected == right.selected && left.order == right.order
        && left.reviewed_answer_identity == right.reviewed_answer_identity;
}
std::string msaa_text(HWND window, bool require_provider = false) {
    Microsoft::WRL::ComPtr<IAccessible> accessible;
    const auto provider = AccessibleObjectFromWindow(window, static_cast<DWORD>(OBJID_CLIENT), IID_PPV_ARGS(accessible.GetAddressOf()));
    require(!require_provider || SUCCEEDED(provider), "An unlocked actual source has no MSAA provider");
    if (FAILED(provider)) return {};
    VARIANT self{}; self.vt = VT_I4; self.lVal = CHILDID_SELF; std::string text; bool supplied = false;
    for (const bool value : {false, true}) {
        BSTR result = nullptr; const auto status = value ? accessible->get_accValue(self, &result) : accessible->get_accName(self, &result);
        if (status == S_OK) { supplied = true; if (result) { text += '\n'; text += utf8(std::wstring_view(result, SysStringLen(result))); } }
        SysFreeString(result);
    }
    require(!require_provider || supplied, "An unlocked actual source has no successful MSAA name/value");
    for (std::size_t offset = 0; (offset = text.find("\r\n", offset)) != std::string::npos;) text.replace(offset, 2, "\n");
    std::ranges::replace(text, '\r', '\n'); return text;
}
curriculum::StatePosition position(const curriculum::Lesson& lesson, std::string_view identity) {
    const auto result = curriculum::stable_block_position(lesson, identity);
    require(result.has_value(), "A current independent owner or prediction identity disappeared"); return *result;
}
const curriculum::Block& at(const curriculum::Lesson& lesson, std::string_view identity) {
    const auto result = position(lesson, identity); return lesson.pages.at(result.page).blocks.at(result.block);
}
constexpr std::string_view owner_id = "block_0014_0007", old_owner_id = "block_0012_0003", target_id = "block_0015_0002";
struct Case { std::string_view input, output; };
constexpr std::array<Case, 4> cases{{
    {"7 2", "Una: 2 7\nCompartido: 2 7\nDos: 7 2"},
    {"4 4", "Una: 4 4\nCompartido: 4 4\nDos: 4 4"},
    {"0 6", "Una: 6 0\nCompartido: 6 0\nDos: 0 6"},
    {"-12 12", "Una: 12 -12\nCompartido: 12 -12\nDos: -12 12"}
}};
void synthetic_previous_owner(state::StateData& data, const curriculum::Lesson& lesson, std::string_view identity) {
    // Explicit QA isolation, not execution evidence. Neither of these owners
    // is the independent four-input constructor being tested below.
    require(identity == "block_0007_0004" || identity == old_owner_id, "Refusing a synthetic focal or undeclared owner");
    const auto& block = at(lesson, identity); require(block.output_check.has_value(), "An earlier owner lost its authored cases");
    windows::CppRunRequest request; request.revision = 1; request.source_utf8 = block.initial_code;
    for (const auto& item : block.output_check->cases) {
        request.stdin_utf8 = item.standard_input;
        curriculum::FreshExecution synthetic{1, true, true, 0, item.standard_input, item.outputs.front()};
        synthetic.executable = item.executable;
        (void)windows::record_compiler_puzzle_case(data, lesson, block, request, synthetic,
            "Synthetic QA earlier owner only; no execution claimed");
    }
    require(windows::compiler_puzzle_completed(data, lesson, block), "Cannot isolate the expressly synthetic earlier owner");
}
state::Preferences receipts(const state::StateData& data, const curriculum::Lesson& lesson, std::string_view identity) {
    state::Preferences result;
    const auto prefix = "compiler-puzzle/v1/" + lesson.locale + '/' + lesson.id + '/'
        + curriculum::exercise_key(lesson, at(lesson, identity)) + '/';
    if (const auto domain = data.preferences.find("native_windows"); domain != data.preferences.end())
        for (const auto& item : domain->second) if (item.first.starts_with(prefix)) result.insert(item);
    return result;
}

struct Harness {
    std::shared_ptr<windows::NarrationWorker> backend;
    curriculum::Lesson current, legacy_qa;
    WindowsEnvironment environment;
    std::filesystem::path asset;
    std::vector<std::uint8_t> asset_before;
    HWND host = nullptr, view = nullptr;
    std::unique_ptr<LessonReader> reader;
    state::Preferences reserve, unrelated, previous_first, previous_second;
    std::optional<state::Response> accepted, page_choice;
    std::optional<narration::Bookmark> third_slot;
    struct Recording { std::string hash; std::filesystem::file_time_type modified; };
    std::map<std::filesystem::path, Recording> recordings;
    std::size_t executions = 0;

    explicit Harness(std::shared_ptr<windows::NarrationWorker> worker) : backend(std::move(worker)) {
        asset = std::filesystem::path(build::source_root) /
            "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos/lesson_referencias_para_modificar.json";
        asset_before = state::read_bytes(asset); current = windows::load_lesson(asset);
        const auto owner = position(current, owner_id), target = position(current, target_id);
        const auto& block = at(current, owner_id); const auto& puzzle = at(current, target_id);
        require(owner.page == 14 && owner.block == 7 && target.page == 15 && target.block == 2
            && block.type == curriculum::BlockType::cpp_compiler && block.standard_input == "7 2"
            && block.output_check && block.output_check->input_comparison == curriculum::TextComparison::tokens
            && block.output_check->cases.size() == cases.size() && !current.compiler_puzzles_required
            && puzzle.type == curriculum::BlockType::output_prediction
            && puzzle.answers == std::vector<std::string>{"9 0 0 9"}, "The current independent U6 contract changed");
        const auto prerequisites = curriculum::activity_prerequisites(current, puzzle);
        require(prerequisites.size() == 1 && prerequisites.front().page == owner.page && prerequisites.front().block == owner.block,
            "The current prediction no longer depends on exactly its independent constructor");
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const auto& item = block.output_check->cases[index];
            require(item.standard_input == cases[index].input && item.outputs == std::vector<std::string>{
                std::string(cases[index].output), std::string(cases[index].output) + '\n'},
                "A published input or literal exact output differs from the independent oracle");
        }
        require(position(current, old_owner_id).page == 12 && position(current, old_owner_id).block == 4,
            "The earlier owner display address changed independently of its historical ID");
        legacy_qa = current;
        const auto relation = std::ranges::find(legacy_qa.activity_after, target_id, &curriculum::ActivityAfter::activity);
        require(relation != legacy_qa.activity_after.end(), "The current focal relation is missing");
        relation->prerequisites = {std::string(old_owner_id)};
        curriculum::validate_activity_relationships(legacy_qa);
        // This is a simulated QA antecedent, not a claim that a historical
        // published JSON or a real learner profile was imported. Only this
        // in-memory owner link differs; the current model stays untouched.
        const auto token = L"reference-reader-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        const auto base = std::filesystem::path(build::source_root) / L"out/qa/current-reference-reader";
        require(base.is_absolute() && (base.root_name() == L"E:" || base.root_name() == L"e:"), "QA state requires its physical E: checkout");
        environment.options = {ProfileId::Codex, token, false};
        environment.paths = make_profile_paths(base, ProfileId::Codex, token);
        environment.paths.cache = std::filesystem::path(build::source_root) / "out/qa/lesson-reader-audio";
        const std::filesystem::path build_base = L"C:/LGP-ToolchainBuilds/LGAED-ReferenceReader-QA";
        environment.paths.projects = build_base / token;
        state::require_safe_path(environment.paths.root); state::require_safe_path(environment.paths.cache);
        state::require_safe_path(environment.paths.projects);
        require(!std::filesystem::exists(environment.paths.root) && !std::filesystem::exists(environment.paths.projects)
            && std::filesystem::weakly_canonical(environment.paths.projects).parent_path() == std::filesystem::weakly_canonical(build_base),
            "Refusing to reuse another fixture's state or compiler directory");
        initialize_profile_directories(environment.paths);
        state::StateData initial; state::prepare_lesson_state(initial, current);
        synthetic_previous_owner(initial, current, "block_0007_0004"); synthetic_previous_owner(initial, current, old_owner_id);
        state::save_current_page(initial, current, 15);
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences["reference_reader_unrelated"]["opaque"] = std::string(" \t preserve\r\n");
        auto& settings = initial.preferences["lesson_narration_settings"];
        settings["engine"] = std::string("KOKORO"); settings["language"] = std::string("es");
        settings["kokoro_voice:es"] = std::string("ef_dora"); settings["rate"] = 3.; settings["kokoro_advance_generation"] = false;
        auto& finite = initial.preferences["attempt_reserve_seconds_test"];
        finite["reserve_programming"] = 3.; finite["recovery_interval_programming"] = attempts::recovery_millis;
        finite["updated_at_programming"] = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        reserve = finite; unrelated = initial.preferences.at("reference_reader_unrelated");
        previous_first = receipts(initial, current, "block_0007_0004"); previous_second = receipts(initial, current, old_owner_id);
        require(windows::compiler_puzzle_case_count(initial, current, block) == 0
            && !windows::compiler_puzzle_completed(initial, current, block) && !state::get_response(initial, current, puzzle),
            "The focal constructor or prediction began with invented credit");
        state::StateStore store(environment.paths.root); (void)store.load(); store.save(initial);
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Current independent reference reader QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1000, 780, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create the isolated hidden reference-reader host");
    }
    ~Harness() {
        if (std::uncaught_exceptions()) backend->shutdown();
        close(); if (host) DestroyWindow(host);
    }
    void close() {
        reader.reset(); if (view) { (void)flush_lesson_view(view); DestroyWindow(view); view = nullptr; }
    }
    HWND control(std::size_t block, int part) const {
        const auto window = find(view, 1000 + static_cast<int>(block) * 100 + part);
        require(window != nullptr, "An actual focal lesson control is missing"); return window;
    }
    LessonReaderSnapshot snapshot() const {
        const auto result = reader->snapshot(); require(result.error.empty(), "Real current-reference reader error: " + result.error); return result;
    }
    void ready() const {
        wait([&] { const auto value = snapshot(); return value.planned && !value.segments.empty()
            && value.index < value.segments.size() && value.segments[value.index].available; }, "The actual current sentence was not synthesized");
    }
    void open(const curriculum::Lesson& model, std::size_t expected_page) {
        require(!view && !reader, "Close the previous fixture view before opening another");
        LessonViewHooks hooks;
        hooks.before_page_change = [&](HWND) { if (reader) reader->pause(); };
        hooks.before_destroy = [&](HWND) { reader.reset(); };
        hooks.content_changed = [&](HWND) { if (reader) reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (reader) reader->feedback(block, correct); };
        hooks.priority_work = [worker = backend](std::stop_token stop) -> std::shared_ptr<void> {
            return std::make_shared<windows::NarrationPriorityLease>(worker->heavy_work_lease(stop));
        };
        view = create_lesson_view(host, environment, model, std::move(hooks));
        require(view != nullptr, "Cannot create the actual current-reference lesson view"); MoveWindow(view, 0, 0, 980, 720, TRUE); pump();
        require(lesson_page_state(view).index == expected_page && !IsWindowVisible(host), "The focal QA page resolved incorrectly or exposed the desktop");
        reader = std::make_unique<LessonReader>(view, environment, backend, [] {}); ready();
    }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot preserve isolated reference-reader state");
        state::StateStore store(environment.paths.root); return store.load();
    }
    void edit(HWND window, std::string_view value) const {
        require(IsWindowEnabled(window), "A real focal editor is unexpectedly disabled");
        CHARRANGE all{0, -1}; SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto native = wide(value); SendMessageW(window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(native.c_str()));
        require(edit_text(window) == value, "The actual editor changed supplied characters");
    }
    void navigate(std::size_t page) {
        reader->pause(); require(navigate_lesson_view(view, page), "The normal focal page navigation was blocked"); ready();
    }
    void page_while_closed(std::size_t page) {
        require(!view && !reader, "QA preparation must not overwrite a live view's state");
        state::StateStore store(environment.paths.root); auto data = store.load(); state::save_current_page(data, current, page); store.save(data);
    }
    void preserved(std::size_t count, bool completed) const {
        const auto data = saved();
        require(data.preferences.at("reference_reader_unrelated") == unrelated
            && data.preferences.at("attempt_reserve_seconds_test") == reserve
            && receipts(data, current, "block_0007_0004") == previous_first && receipts(data, current, old_owner_id) == previous_second,
            "A focal action changed unrelated state, finite hearts or either explicitly synthetic earlier receipt");
        require(windows::compiler_puzzle_case_count(data, current, at(current, owner_id)) == count
            && windows::compiler_puzzle_completed(data, current, at(current, owner_id)) == completed,
            "The independent owner has the wrong number of actual captured-program cases");
        for (const auto& page : current.pages) for (const auto& block : page.blocks)
            if (block.type == curriculum::BlockType::cpp_compiler && block.stable_id != owner_id
                && block.stable_id != old_owner_id && block.stable_id != "block_0007_0004")
                require(windows::compiler_puzzle_case_count(data, current, block) == 0
                    && !windows::compiler_puzzle_completed(data, current, block), "The focused run credited another compiler");
        if (accepted) {
            const auto response = state::get_response(data, current, at(current, target_id));
            require(response && same_response(*response, *accepted), "Hiding, reopening or resetting erased or reevaluated the historical actual prediction response");
        }
        if (page_choice) {
            const auto response = state::get_response(data, current, current.pages[14].blocks[1]);
            require(response && same_response(*response, *page_choice), "The adjacent actual choice changed during owner/bookmark operations");
        }
        if (third_slot) {
            const auto history = narration::read_history(data);
            const auto found = std::ranges::find(history.entries, third_slot->key, &narration::Bookmark::key);
            require(history.entries.size() == 3 && found != history.entries.end() && *found == *third_slot,
                "A focal restore, execution or reset evicted/rewrote the unrelated third bookmark");
        }
        require(state::read_bytes(asset) == asset_before, "The curricular JSON changed during technical QA");
        for (const auto& [path, recording] : recordings)
            require(state::sha256(state::read_bytes(path)) == recording.hash && std::filesystem::last_write_time(path) == recording.modified,
                "The exact compatible recorded audio was rewritten or replaced");
    }
    void verify_recording(const LessonReaderSegmentSnapshot& part) {
        const auto data = state::read_bytes(part.recording);
        const auto u16 = [&](std::size_t offset) { return unsigned(data[offset]) | (unsigned(data[offset + 1]) << 8); };
        const auto u32 = [&](std::size_t offset) { return u16(offset) | (u16(offset + 2) << 16); };
        const auto tag = [&](std::size_t offset, std::string_view value) { return std::equal(value.begin(), value.end(), data.begin() + offset); };
        require(data.size() > 44 && tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data")
            && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000 && u16(34) == 16 && u32(40) == data.size() - 44
            && (data.size() - 44) % 2 == 0 && std::ranges::any_of(data.begin() + 44, data.end(), [](auto sample) { return sample != 0; })
            && std::abs(part.duration_ms - (data.size() - 44) / 48.) <= 1 && !part.timings.empty(),
            "The accepted prediction lacks real non-silent mono 24 kHz PCM16 and word timings");
        double previous = 0;
        for (const auto& timing : part.timings) {
            require(timing.start < timing.end && timing.end <= wide(part.source).size()
                && std::isfinite(timing.start_ms) && std::isfinite(timing.end_ms) && timing.start_ms >= previous
                && timing.end_ms >= timing.start_ms && timing.end_ms <= part.duration_ms + 1,
                "A current prediction word has invalid source/audio alignment"); previous = timing.start_ms;
        }
        const Recording value{state::sha256(data), std::filesystem::last_write_time(part.recording)};
        if (const auto found = recordings.find(part.recording); found != recordings.end())
            require(found->second.hash == value.hash && found->second.modified == value.modified, "An existing compatible recording was rewritten");
        else recordings.emplace(part.recording, value);
    }
    narration::Bookmark bookmark(std::string_view key) const {
        const auto history = narration::read_history(saved()); const auto found = std::ranges::find(history.entries, key, &narration::Bookmark::key);
        require(found != history.entries.end(), "The real page bookmark is missing"); return *found;
    }
    void exact_pause(const LessonReaderSnapshot& before) const {
        const auto after = snapshot();
        require(after.key == before.key && after.index == before.index && after.offset == before.offset
            && after.heard_through == before.heard_through && !after.playing && !after.allocated && !after.finished
            && after.segments[after.index].recording == before.segments[before.index].recording,
            "Reopening lost the exact real paused recording, offset or listening boundary");
    }
    void resume(const LessonReaderSnapshot& before) {
        reader->toggle(); const auto request = snapshot();
        require(request.key == before.key && request.index == before.index && request.offset == before.offset,
            "An explicit valid-certificate resume changed the exact requested bookmark");
        wait([&] { const auto value = snapshot(); require(value.key == before.key && value.index == before.index,
                "The actual resumed player selected a different sentence");
            return value.playing && value.allocated && value.playback_rate == 3. && value.offset > before.offset; },
            "The same actual recording did not resume monotonically at the selected supported speed");
        reader->pause(); verify_recording(snapshot().segments[snapshot().index]);
    }
    void locked_sources() const {
        const auto& puzzle = at(current, target_id);
        const std::array<std::string, 4> secrets{puzzle.question, "const int copia = primera;", "copia conserva el 9", "9 0 0 9"};
        const auto check_text = [&](std::string_view text) { for (const auto& secret : secrets)
            require(text.find(secret) == std::string_view::npos, "The locked prediction leaked through a source or native accessibility provider"); };
        for (const auto& region : lesson_reading_regions(view)) check_text(region.source_text);
        const auto value = snapshot();
        require(lesson_page_state(view).index == 14 && !value.playing && !value.allocated && !value.finished,
            "A locked old bookmark bypassed its owner or started an audio player");
        for (const auto& segment : value.segments) { check_text(segment.source); check_text(segment.spoken); }
        // HWND descendants are real MSAA providers, including RichEdit values.
        // Catch inside the enumeration callback; never unwind through Win32.
        const std::function<void(std::string_view)> inspect = check_text;
        struct Probe { const std::function<void(std::string_view)>* inspect; std::exception_ptr error; } probe{&inspect, {}};
        EnumChildWindows(view, [](HWND window, LPARAM raw) -> BOOL {
            auto& data = *reinterpret_cast<Probe*>(raw);
            try { (*data.inspect)(msaa_text(window)); }
            catch (...) { data.error = std::current_exception(); return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&probe));
        if (probe.error) std::rethrow_exception(probe.error);
    }
    void unlocked_accessibility() const {
        const auto code = control(2, 1), answer = control(2, 2);
        require(!IsWindowVisible(host) && edit_text(code) == at(current, target_id).code
            && msaa_text(code, true).find("const int copia = primera;") != std::string::npos
            && msaa_text(answer, true).find("9 0 0 9") != std::string::npos,
            "The same hidden-host MSAA probe did not expose the actual unlocked source and answer positively");
    }
    std::set<std::filesystem::path> runs() const {
        std::set<std::filesystem::path> result; const auto parent = environment.paths.projects / L"cpp-runs";
        if (std::filesystem::exists(parent)) for (const auto& entry : std::filesystem::directory_iterator(parent))
            if (entry.is_directory()) result.insert(entry.path());
        return result;
    }
    void compile_case(std::size_t index, std::string_view program) {
        reader->pause(); edit(control(7, 15), cases[index].input); const auto before = runs(); const auto button = control(7, 11);
        require(IsWindowEnabled(button), "The independent compiler's actual combined action is unavailable");
        SendMessageW(button, BM_CLICK, 0, 0); wait([&] { return IsWindowEnabled(button); }, "The independent real MSVC execution did not finish");
        reader->pause(); ready();
        const auto after = runs(); std::vector<std::filesystem::path> created;
        for (const auto& directory : after) if (!before.contains(directory)) created.push_back(directory);
        require(created.size() == 1, "The independent compiler did not create exactly one fresh native run");
        const auto directory = created.front();
        require(bytes(directory / L"main.cpp") == program && bytes(directory / L"stdin.bin") == cases[index].input
            && bytes(directory / L"execution-stdout.bin") == crt_text(std::string(cases[index].output) + '\n')
            && bytes(directory / L"execution-stderr.bin").empty(), "The actual source/input or raw native stdout/stderr differs from its independent case");
        DWORD kind = 0;
        require(GetBinaryTypeW((directory / L"program.exe").c_str(), &kind) && kind == SCS_64BIT_BINARY
            && bytes(directory / L"toolchain.txt").find("compiler=" + utf8((std::filesystem::path(build::educational_vc_root) / L"bin/Hostx64/x64/cl.exe").wstring()) + '\n') != std::string::npos,
            "The independent case did not use the configured MSVC and a real x64 executable");
        const auto status = edit_text(control(7, 34));
        require(index + 1 == cases.size() ? status == "Ejecución terminada (código 0)."
            : status == "Caso correcto. Casos comprobados: " + std::to_string(index + 1) + " de 4.",
            "The fresh native run did not record its own successful termination/case");
        const auto destination = environment.paths.logs / L"native-runs" / directory.filename(); state::require_safe_path(destination);
        std::filesystem::create_directories(state::native_io_path(destination.parent_path()));
        require(std::filesystem::create_directory(state::native_io_path(destination)), "Refusing to overwrite a previous case capture");
        for (const auto* file : {L"main.cpp", L"stdin.bin", L"toolchain.txt", L"compilation-stdout.bin", L"compilation-stderr.bin", L"execution-stdout.bin", L"execution-stderr.bin"}) {
            const auto source = directory / file, output = destination / file;
            require(std::filesystem::copy_file(state::native_io_path(source), state::native_io_path(output))
                && state::read_bytes(source) == state::read_bytes(output), "The durable E: native capture is not byte exact");
        }
        ++executions; preserved(index + 1, index + 1 == cases.size());
        const auto draft = state::get_draft(saved(), curriculum::draft_key("programming", current, at(current, owner_id)), "");
        require(draft.source == program && draft.input == cases[index].input, "The independent program/input draft changed between case executions");
        std::cout << "actual_msvc_case=" << index + 1 << " stdin=" << cases[index].input << " exit=0 stderr_bytes=0\n";
    }
};

std::string qa_program(const curriculum::Lesson& lesson) {
    // Published p14b8 is a complete stdin/alias example, not a solution to the
    // independent p15 editor. This QA main is derived from it only by changing
    // the two report labels required by the p15b7 construction prompt.
    const auto& example = at(lesson, "block_0013_0007");
    require(example.type == curriculum::BlockType::cpp_compiler && !example.initial_code.empty(), "The published stdin/alias example is absent");
    auto program = example.initial_code;
    for (const auto& [old_label, new_label] : std::array<std::pair<std::string_view, std::string_view>, 2>{{
        {"\"Distintos: \"", "\"Una: \""}, {"\"Restaurado: \"", "\"Dos: \""}}}) {
        const auto offset = program.find(old_label);
        require(offset != std::string::npos && program.find(old_label, offset + 1) == std::string::npos,
            "The explicit QA report-label derivation is no longer unique"); program.replace(offset, old_label.size(), new_label);
    }
    const std::string function = "void intercambiar(int& primero, int& segundo) {\n"
        "    const int anterior = primero;\n    primero = segundo;\n    segundo = anterior;\n}\n";
    require(program.find(function) != std::string::npos && at(lesson, "block_0013_0002").code.find(function) != std::string::npos,
        "The QA program changed the exact published interchange function");
    return "// QA main derived from the published stdin example and the independent p15 construction prompt.\n" + program;
}

void run(const std::shared_ptr<windows::NarrationWorker>& backend) {
    Harness fixture(backend);
    phase = "simulated legacy QA: actual native prediction answer and actual paused PCM";
    fixture.open(fixture.legacy_qa, 15); fixture.preserved(0, false);
    fixture.edit(fixture.control(2, 2), "9 0 0 9"); SendMessageW(fixture.control(2, 3), BM_CLICK, 0, 0);
    fixture.reader->pause(); fixture.ready();
    fixture.accepted = state::get_response(fixture.saved(), fixture.current, at(fixture.current, target_id));
    require(fixture.accepted && fixture.accepted->correct && fixture.accepted->answer == "9 0 0 9",
        "The prior prediction was not accepted through its actual editable answer and Check button");
    fixture.reader->toggle();
    wait([&] { const auto value = fixture.snapshot(); const auto& part = value.segments[value.index];
        return value.playing && part.feedback && part.correct && part.block == 2
            && part.source.find("copia conserva el 9") != std::string::npos && value.offset > 150 && value.offset < part.duration_ms / 2; },
        "The actual accepted prediction explanation did not play before its real pause");
    fixture.reader->pause(); auto paused = fixture.snapshot(); fixture.verify_recording(paused.segments[paused.index]);
    const auto initial_history = narration::read_history(fixture.saved()); fixture.close(); fixture.open(fixture.legacy_qa, 15);
    fixture.exact_pause(paused);
    require(narration::read_history(fixture.saved()) == initial_history, "Whole-view reopen rewrote the real prediction bookmark");
    fixture.resume(paused); paused = fixture.snapshot();
    auto real_bookmark = fixture.bookmark(paused.key);
    require(real_bookmark.position.segment == static_cast<std::int64_t>(paused.index)
        && real_bookmark.position.milliseconds == paused.offset && real_bookmark.position.heard_prefix
        && real_bookmark.position.heard_prefix->microseconds > 0,
        "The actual pause did not save an exact certificate derived from real listened PCM");

    phase = "simulated legacy QA: real p15 choice and positional-only late bookmark";
    fixture.close(); fixture.page_while_closed(14); fixture.open(fixture.legacy_qa, 14);
    const auto& choice = fixture.current.pages[14].blocks[1];
    require(choice.type == curriculum::BlockType::multiple_choice && choice.correct_index == 0, "The adjacent p15 choice changed");
    SendMessageW(fixture.control(1, 20), BM_CLICK, 0, 0); SendMessageW(fixture.control(1, 3), BM_CLICK, 0, 0);
    fixture.reader->pause(); fixture.ready();
    fixture.page_choice = state::get_response(fixture.saved(), fixture.current, choice);
    require(fixture.page_choice && fixture.page_choice->correct && fixture.page_choice->selected == 0,
        "The adjacent navigation gate was not answered through its real choice and Check controls");
    const auto old_page = fixture.snapshot(); const auto owner_first = std::ranges::find_if(old_page.segments, [](const auto& part) { return part.block == 7; });
    require(owner_first != old_page.segments.end(), "The p15 script has no independent constructor segment");
    const auto first_owner = static_cast<std::size_t>(owner_first - old_page.segments.begin());
    const auto page_identity = lesson_page_state(fixture.view).narration_page_identity;
    fixture.close();
    narration::History preserved;
    preserved.entries = {real_bookmark,
        {old_page.key, {}, {static_cast<std::int64_t>(old_page.segments.size() - 1), 0, 0, true}, page_identity},
        {"qa_reference_unrelated_pause", {}, {0, 37, 0, false}, "qa_reference_unrelated_page"}};
    fixture.third_slot = preserved.entries.back();
    // The second/third positions are explicit preservation fixtures, without
    // PCM certificates or listening credit. Only the p16 marker above is real.
    { state::StateStore store(fixture.environment.paths.root); auto data = store.load();
        narration::write_history(data, preserved); state::save_current_page(data, fixture.current, 15); store.save(data); }

    phase = "current untouched model: the wrong old owner cannot release the independent gate";
    fixture.open(fixture.current, 14); fixture.preserved(0, false); fixture.locked_sources();
    const auto redirected = fixture.snapshot();
    require(redirected.key == old_page.key && redirected.index == first_owner && redirected.offset == 0
        && !redirected.finished && narration::read_history(fixture.saved()) == preserved,
        "A same-script late/finished p15 bookmark bypassed its new owner or rewrote any of the three saved slots");
    require(!navigate_lesson_view(fixture.view, 15), "An earlier completed owner released the current independent prediction");

    phase = "current model: four fresh inputs of one actual MSVC program";
    const auto program = qa_program(fixture.current); fixture.edit(fixture.control(7, 10), program);
    for (std::size_t index = 0; index < cases.size(); ++index) fixture.compile_case(index, program);
    require(fixture.executions == 4 && lesson_page_state(fixture.view).can_next, "Four actual cases did not release the normal focal page gate");

    phase = "current model: exact historical real bookmark returns silently after its own four cases";
    fixture.navigate(15); fixture.exact_pause(paused); fixture.preserved(4, true); fixture.unlocked_accessibility();
    require(fixture.bookmark(paused.key) == real_bookmark, "Unlocking changed the exact historical response-bound bookmark");
    const auto before_reopen = narration::read_history(fixture.saved()); fixture.close(); fixture.open(fixture.current, 15);
    fixture.exact_pause(paused);
    require(narration::read_history(fixture.saved()) == before_reopen, "Reopening the unlocked current page rewrote the three-slot history");
    fixture.resume(paused); paused = fixture.snapshot(); real_bookmark = fixture.bookmark(paused.key);

    phase = "current model: completed achievement is sticky across source edits";
    fixture.navigate(14); fixture.reader->pause(); fixture.edit(fixture.control(7, 10), program + "\n// QA draft edited after completed achievement\n");
    fixture.preserved(4, true); fixture.navigate(15); fixture.exact_pause(paused);
    require(fixture.bookmark(paused.key) == real_bookmark, "A later draft edit revoked the prior achievement or changed the dependent bookmark");

    phase = "current model: explicit Reset relocks while conserving the actual prediction and its recording";
    fixture.navigate(14); fixture.reader->pause(); SendMessageW(fixture.control(7, 12), BM_CLICK, 0, 0);
    fixture.reader->pause(); fixture.ready(); fixture.preserved(0, false); fixture.locked_sources();
    require(edit_text(fixture.control(7, 10)) == at(fixture.current, owner_id).initial_code
        && !navigate_lesson_view(fixture.view, 15) && fixture.bookmark(paused.key) == real_bookmark,
        "Reset retained the owner achievement, erased its original source or lost the protected real bookmark");
    fixture.close(); fixture.page_while_closed(15);
    state::StateStore store(fixture.environment.paths.root); const auto after_reset = narration::read_history(store.load());
    fixture.open(fixture.current, 14); fixture.preserved(0, false); fixture.locked_sources();
    require(narration::read_history(fixture.saved()) == after_reset && fixture.bookmark(paused.key) == real_bookmark,
        "Reopening a protected saved page after Reset changed its historical bookmark or another saved slot");
    std::cout << "PASS current U6 independent reference owner: four real MSVC cases; native p15 choice; real accepted p16 explanation PCM/timings, pause/reopen/resume; current gate/redirect and native MSAA privacy; same-key late bookmark constrained without changing three slots; sticky edits and scoped Reset preserve response/audio/bookmark. Two earlier owners are explicitly synthetic QA receipts; legacyQA is simulated; no full curricular traversal or human listening acceptance claimed.\n";
}
} // namespace

int main() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try {
        require(rich != nullptr, "Cannot load actual RichEdit controls"); reader_theme::prepare_fonts();
        auto backend = std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        struct Shutdown { std::shared_ptr<windows::NarrationWorker> worker; ~Shutdown() { worker->shutdown(); } } cleanup{backend};
        run(backend);
    } catch (const std::exception& error) { std::cerr << "FAIL current reference reader: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

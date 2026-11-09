#include "core/attempts.hpp"
#include "core/compiler_flow.hpp"
#include "core/narration_state.hpp"
#include "core/u6_diagnostic_test_fixture.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void wait(const std::function<bool()>& ready, std::string_view message, ULONGLONG timeout = 120000) {
    const auto until = GetTickCount64() + timeout;
    do { pump(); if (ready()) return; MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT); }
    while (GetTickCount64() < until);
    throw std::runtime_error("Timeout: " + std::string(message));
}
HWND find(HWND parent, int id) {
    struct Search { int id; HWND result = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND item, LPARAM raw) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(item) != value.id) return TRUE;
        value.result = item; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
int control_id(std::size_t block, int part) { return 1000 + static_cast<int>(block) * 100 + part; }
curriculum::StatePosition position(const curriculum::Lesson& lesson, std::string_view id) {
    const auto value = curriculum::stable_block_position(lesson, id);
    require(value.has_value(), "A published U6 diagnostic lost its stable owner"); return *value;
}
const curriculum::Block& owner(const curriculum::Lesson& lesson, std::string_view id) {
    const auto at = position(lesson, id); return lesson.pages[at.page].blocks[at.block];
}
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string xml_text(std::string_view value) {
    std::string result;
    for (const auto c : value) {
        switch (c) {
        case '&': result += "&amp;"; break; case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break; case '"': result += "&quot;"; break;
        case '\r': result += "&#13;"; break; case '\n': result += "&#10;"; break;
        case '\t': result += "&#9;"; break; default: result += c;
        }
    }
    return result;
}
void export_preferences(const std::filesystem::path& root, const state::StateData& data) {
    require(!std::filesystem::exists(root), "U6 import QA requires a new isolated export");
    std::filesystem::create_directories(state::native_io_path(root / "shared_prefs"));
    for (const auto& [domain, values] : data.preferences) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for (const auto& [key, value] : values) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto name = " name=\"" + xml_text(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>) xml += "<string" + name + ">" + xml_text(item) + "</string>";
            else if constexpr (std::is_same_v<T, bool>) xml += "<boolean" + name + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>) xml += "<long" + name + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, double>) {
                std::ostringstream text; text.imbue(std::locale::classic()); text << std::setprecision(17) << item;
                xml += "<float" + name + " value=\"" + text.str() + "\"/>";
            } else {
                xml += "<set" + name + ">";
                for (const auto& entry : item) xml += "<string>" + xml_text(entry) + "</string>";
                xml += "</set>";
            }
        }, value);
        xml += "</map>";
        std::ofstream file(state::native_io_path(root / "shared_prefs" / (domain + ".xml")), std::ios::binary);
        file.write(xml.data(), static_cast<std::streamsize>(xml.size())); file.close();
        require(static_cast<bool>(file), "Cannot write the isolated U6 XML fixture");
    }
}
std::string project_json(const curriculum::Block& block) {
    JsonObject result, cursors, indentation; JsonArray files, sources;
    for (const auto& [path, code] : block.project_files) {
        JsonObject file; file.Insert(L"path", JsonValue::CreateStringValue(winrt::to_hstring(path)));
        file.Insert(L"code", JsonValue::CreateStringValue(winrt::to_hstring(code))); files.Append(file);
        cursors.Insert(winrt::to_hstring(path), JsonValue::CreateNumberValue(2));
        indentation.Insert(winrt::to_hstring(path), JsonValue::CreateStringValue(winrt::to_hstring(state::make_indentation_metadata(code, {}))));
    }
    for (const auto& path : block.source_files) sources.Append(JsonValue::CreateStringValue(winrt::to_hstring(path)));
    result.Insert(L"files", files); result.Insert(L"sources", sources);
    result.Insert(L"selected", JsonValue::CreateStringValue(L"main.cpp"));
    result.Insert(L"cursors", cursors); result.Insert(L"indentationV2", indentation);
    return winrt::to_string(result.Stringify());
}
bool ordinary(curriculum::BlockType type) {
    using B = curriculum::BlockType;
    return type == B::multiple_choice || type == B::output_prediction || type == B::fill_code || type == B::order_code;
}
void prepare_ordinary(state::StateData& data, const curriculum::Lesson& lesson,
                      const std::shared_ptr<windows::NarrationWorker>& backend) {
    // QA prerequisite responses, not student answers or an assessment of the
    // curriculum. The real evaluators still check every prepared response.
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        if (!ordinary(block.type) && block.type != curriculum::BlockType::math_puzzle) continue;
        require(!state::get_response(data, lesson, block), "U6 setup refuses to replace an existing ordinary response");
        state::Response response; curriculum::AnswerEvaluation evaluated;
        if (block.type == curriculum::BlockType::multiple_choice) {
            require(block.correct_index.has_value(), "Missing prerequisite choice contract");
            response.selected = static_cast<std::int64_t>(*block.correct_index);
            response.answer = block.options.at(*block.correct_index);
            evaluated = curriculum::evaluate_choice(block, *block.correct_index);
        } else if (block.type == curriculum::BlockType::order_code) {
            response.order.assign(block.correct_order.begin(), block.correct_order.end());
            evaluated = curriculum::evaluate_order(block, block.correct_order);
        } else if (ordinary(block.type)) {
            require(!block.answers.empty(), "Missing prerequisite text contract");
            response.answer = block.answers.front(); evaluated = curriculum::evaluate_text_answer(block, response.answer);
        } else {
            // Only the actual U6 matching prerequisite is supported. Do not
            // silently turn a new mathematical interaction into solved state.
            const auto authored = JsonObject::Parse(winrt::to_hstring(block.raw_json));
            const auto specs = authored.GetNamedArray(L"responses");
            require(specs.Size() == 1, "U6 prerequisite math schema needs explicit review");
            const auto spec = specs.GetObjectAt(0);
            require(spec.GetNamedString(L"type") == L"math_matching_drag", "Unexpected U6 prerequisite response type");
            JsonObject answer, answers;
            answer.Insert(L"type", JsonValue::CreateStringValue(L"matching"));
            answer.Insert(L"pairs", spec.GetNamedObject(L"validator").GetNamedObject(L"expected"));
            answers.Insert(spec.GetNamedString(L"id"), answer);
            const auto reply = JsonObject::Parse(winrt::to_hstring(backend->validate_math(
                block.raw_json, winrt::to_string(answers.Stringify()), lesson.locale)));
            require(reply.GetNamedBoolean(L"evaluable", false) && reply.GetNamedBoolean(L"correct", false),
                "The real mathematical validator rejected the QA prerequisite");
            JsonObject stored; stored.Insert(L"responses", answers);
            stored.Insert(L"contentFingerprint", reply.GetNamedValue(L"contentFingerprint"));
            stored.Insert(L"correct", JsonValue::CreateBooleanValue(true)); response.raw_json = winrt::to_string(stored.Stringify());
            evaluated.correct = true;
        }
        require(evaluated.correct, "The actual U6 evaluator rejected a QA prerequisite answer");
        response.correct = true; state::mark_response_reviewed(response, block); state::put_response(data, lesson, block, response);
    }
}
std::string cpp_bytes(std::string_view value) {
    std::string result = "\"";
    for (const unsigned char byte : value) {
        result += '\\'; result += static_cast<char>('0' + ((byte >> 6) & 7));
        result += static_cast<char>('0' + ((byte >> 3) & 7)); result += static_cast<char>('0' + (byte & 7));
    }
    return result + '"';
}
std::string prerequisite_program(const curriculum::OutputCheck& check) {
    // Deliberately small QA input/output adapter. It is compiled and executed,
    // never a fabricated FreshExecution, and does not claim to solve the
    // educational programming task or validate its reference implementation.
    std::string code = "#include <cstdio>\n#include <iostream>\n#include <iterator>\n#include <string>\n#include <fcntl.h>\n#include <io.h>\nint main(){\n"
        "_setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);\n"
        "const std::string input{std::istreambuf_iterator<char>{std::cin},std::istreambuf_iterator<char>{}};\n";
    std::set<std::string> inputs;
    for (const auto& item : check.cases) {
        require(!item.executable && !item.outputs.empty() && inputs.insert(item.standard_input).second,
            "U6 QA prerequisite requires unique stdin cases without CMake targets");
        code += "if(input==" + cpp_bytes(item.standard_input) + "){std::cout.write(" + cpp_bytes(item.outputs.front())
            + "," + std::to_string(item.outputs.front().size()) + ");";
        if (item.standard_error == curriculum::StandardErrorExpectation::non_blank) code += "std::cerr<<\"QA prerequisite stderr\";";
        code += "return " + std::to_string(item.exit_code) + ";}\n";
    }
    return code + "return 83;}\n";
}
void execute_stdout_prerequisites(state::StateData& data, const curriculum::Lesson& lesson, const WindowsEnvironment& environment) {
    std::uint64_t sequence = 0; std::size_t cases = 0, objectives = 0;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) if (block.output_check) {
        windows::CppRunRequest request; request.source_utf8 = prerequisite_program(*block.output_check);
        request.projects_directory = environment.paths.projects / L"QA prerequisite executions";
        request.compile_timeout = 45s; request.execution_timeout = 5s; request.revision = ++sequence;
        for (const auto& item : block.output_check->cases) {
            request.request_id = ++sequence; request.stdin_utf8 = item.standard_input;
            const auto result = windows::run_cpp(request);
            const auto execution = windows::capture_cpp_execution(request, result);
            require(result.error_utf8.empty() && result.compilation.succeeded() && execution.execution_completed,
                "A real U6 QA prerequisite program failed: " + result.error_utf8 + result.compilation.stdout_bytes + result.compilation.stderr_bytes);
            require(windows::evaluate_cpp_output_check(*block.output_check, execution, request.revision).verdict == curriculum::OutputVerdict::correct,
                "A real U6 QA prerequisite did not produce the required input/output result");
            (void)windows::record_compiler_puzzle_case(data, lesson, block, request, execution,
                "QA prerequisite adapter: actual execution, not curricular program verification"); ++cases;
        }
        require(windows::compiler_puzzle_completed(data, lesson, block), "Real U6 prerequisite executions left an incomplete objective"); ++objectives;
    }
    std::cout << "SETUP " << objectives << " other stdout objectives / " << cases
        << " actual QA adapter executions; no diagnostic receipts, student work or curricular-solution claim\n";
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::shared_ptr<windows::NarrationWorker> backend;
    HWND host = nullptr, view = nullptr;
    std::unique_ptr<LessonReader> reader;
    std::size_t finish_callbacks = 0;
    View(WindowsEnvironment configured, curriculum::Lesson model, std::shared_ptr<windows::NarrationWorker> worker)
        : environment(std::move(configured)), lesson(std::move(model)), backend(std::move(worker)) {
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Hidden U6 diagnostic flow QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create hidden U6 flow host"); open();
    }
    ~View() { reader.reset(); if (view) { (void)flush_lesson_view(view); DestroyWindow(view); } if (host) DestroyWindow(host); }
    void open() {
        LessonViewHooks hooks;
        hooks.before_page_change = [this](HWND) { if (reader) reader->pause(); };
        hooks.content_changed = [this](HWND) { if (reader) reader->content_changed(); };
        hooks.feedback_changed = [this](HWND, std::size_t block, bool correct) { if (reader) reader->feedback(block, correct); };
        hooks.lesson_completed = [this](HWND) { ++finish_callbacks; };
        hooks.validate_math = [worker = backend](std::string_view block, std::string_view answer, std::string_view locale, std::stop_token stop) {
            return worker->validate_math(block, answer, locale, stop);
        };
        view = create_lesson_view(host, environment, lesson, std::move(hooks));
        require(view != nullptr, "Cannot create real U6 lesson view"); MoveWindow(view, 0, 0, 1100, 850, TRUE);
        reader = std::make_unique<LessonReader>(view, environment, backend, [] {}); ready();
        require(!IsWindowVisible(host), "U6 flow exposed its private QA window");
    }
    void reopen() { reader.reset(); require(flush_lesson_view(view), "Cannot save U6 flow before reopening"); DestroyWindow(view); view = nullptr; open(); }
    LessonReaderSnapshot snapshot() {
        auto result = reader->snapshot(); require(result.error.empty(), "Real U6 reader: " + result.error); return result;
    }
    void ready() {
        wait([&] { const auto current = snapshot(); return current.planned && !current.segments.empty()
            && std::ranges::all_of(current.segments, [](const auto& part) { return part.available; }); }, "real U6 plan, PCM and word alignment");
    }
    state::StateData saved() { require(flush_lesson_view(view), "Cannot flush U6 lesson state"); return read_lesson_state(view); }
    HWND control(std::size_t block, int part) {
        const auto result = find(view, control_id(block, part)); require(result != nullptr, "Missing real U6 lesson control"); return result;
    }
    std::string text(std::size_t block, int part) { return edit_text(control(block, part)); }
    void go(std::size_t page) {
        reader->pause();
        for (auto current = lesson_page_state(view).index; current < page; ++current) {
            require(navigate_lesson_view(view, current + 1), "An actual preceding U6 requirement blocked sequential navigation"); pump();
        }
        require(lesson_page_state(view).index == page, "The U6 flow opened a different page"); ready();
    }
    void select_file(std::size_t block, std::size_t file) {
        const auto project = control(block, 5); const auto tab = find(project, 1000 + static_cast<int>(file));
        require(tab != nullptr && IsWindowEnabled(tab), "Missing enabled native project tab"); SendMessageW(tab, BM_CLICK, 0, 0); pump();
    }
    void replace_source(std::size_t block, std::string_view source) {
        const auto edit = control(block, 10); require(IsWindowEnabled(edit), "U6 project editor unexpectedly disabled");
        CHARRANGE all{0, -1}; SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto value = wide(source); SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str())); pump();
        require(text(block, 10) == source, "Editing the native U6 project changed supplied source bytes");
    }
    void project(std::size_t block, const curriculum::CppProjectFiles& files, const std::vector<std::string>& sources) {
        reader->pause(); const auto& goal = lesson.pages[lesson_page_state(view).index].blocks[block];
        require(files.size() == goal.project_files.size(), "U6 repair changed the project file inventory");
        for (std::size_t index = 0; index < files.size(); ++index) {
            require(files[index].first == goal.project_files[index].first, "U6 repair reordered an authored file"); select_file(block, index);
            if (text(block, 10) != files[index].second) replace_source(block, files[index].second);
            if (curriculum::is_cpp_source_file(files[index].first)) {
                const auto toggle = find(control(block, 5), 303); require(toggle != nullptr, "Missing real source-selection checkbox");
                const bool selected = SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_CHECKED;
                if (selected) SendMessageW(toggle, BM_CLICK, 0, 0);
                pump();
            }
        }
        // A native checkbox appends a newly selected source. Rebuild the
        // sequence through those controls so main.cpp does not silently move
        // before the newly included implementation in a repaired link case.
        for (const auto& source : sources) {
            const auto file = std::ranges::find(files, source, [](const auto& item) { return item.first; });
            require(file != files.end() && curriculum::is_cpp_source_file(source), "Unknown U6 selected source");
            select_file(block, static_cast<std::size_t>(file - files.begin()));
            const auto toggle = find(control(block, 5), 303);
            require(toggle && SendMessageW(toggle, BM_GETCHECK, 0, 0) == BST_UNCHECKED, "U6 source selection was not cleared");
            SendMessageW(toggle, BM_CLICK, 0, 0); pump();
        }
        const auto main = std::ranges::find(files, std::string("main.cpp"), [](const auto& file) { return file.first; });
        require(main != files.end(), "U6 project lost main.cpp"); select_file(block, static_cast<std::size_t>(main - files.begin()));
        const auto base = curriculum::draft_key("programming", lesson, goal);
        const auto stored = JsonObject::Parse(winrt::to_hstring(state::string_value(saved(), "cpp_lesson_drafts", base + "/project")));
        curriculum::CppProjectFiles actual; std::vector<std::string> actual_sources;
        for (const auto& value : stored.GetNamedArray(L"files")) { const auto file = value.GetObject();
            actual.emplace_back(winrt::to_string(file.GetNamedString(L"path")), winrt::to_string(file.GetNamedString(L"code"))); }
        for (const auto& value : stored.GetNamedArray(L"sources")) actual_sources.push_back(winrt::to_string(value.GetString()));
        require(actual == files && actual_sources == sources, "Real project controls lost file bytes or selected source order");
    }
    void run(std::size_t block) {
        reader->pause(); const auto button = control(block, 11);
        require(IsWindowEnabled(button), "U6 diagnostic action was already running");
        SendMessageW(button, BM_CLICK, 0, 0); pump();
        wait([&] { return IsWindowEnabled(button); }, "real U6 diagnostic compilation", 150000);
        reader->pause(); ready(); require(!snapshot().playing && !snapshot().allocated, "U6 completion ignored the manual pause");
        require(text(block, 34).find("El programa no se ejecutó.") != std::string::npos, "U6 diagnostic status no longer reports compilation only");
    }
};

void pending(View& f, const curriculum::StatePosition at, const curriculum::Block& block) {
    const auto data = f.saved(); require(!windows::compiler_puzzle_completed(data, f.lesson, block), "Opaque historical success became diagnostic evidence");
    const auto options = lesson_options_snapshot(f.view);
    require(options.finish_visible == (at.page + 1 == f.lesson.pages.size())
        && !lesson_page_state(f.view).can_next && !options.finish_enabled,
        "Pending U6 diagnostic enabled page completion or lost its real Finish action");
    require(!invoke_lesson_option(f.view, LessonOptionAction::finish) && f.finish_callbacks == 0,
        "The real Finish action bypassed a pending U6 diagnostic");
    for (const auto page : options.available_pages) require(page <= at.page, "The Pages menu bypassed a pending U6 diagnostic");
    if (at.page + 1 < f.lesson.pages.size()) require(!navigate_lesson_view(f.view, at.page + 1), "Navigation bypassed a pending U6 diagnostic");
    const auto regions = lesson_reading_regions(f.view);
    require(std::ranges::any_of(regions, [&](const auto& region) { return region.block_index == at.block && !region.feedback
        && region.is_exercise && region.required_compiler && !region.solved; }), "Questionless U6 compiler has no reader gate");
    require(std::ranges::none_of(regions, [&](const auto& region) { return region.block_index != std::numeric_limits<std::size_t>::max()
        && region.block_index > at.block; }), "Pending U6 compiler exposed later questions or reference prose");
    require(std::ranges::none_of(regions, [&](const auto& region) { return region.source_text.find(block.compile_check->explanation) != std::string::npos; }),
        "U6 success explanation leaked before a real expected rejection");
}
std::map<std::filesystem::path, std::string> feedback_audio(const LessonReaderSnapshot& snapshot, std::size_t block, bool correct) {
    std::map<std::filesystem::path, std::string> result;
    for (const auto& part : snapshot.segments) if (part.block == block && part.feedback && part.correct == correct) {
        const auto bytes = state::read_bytes(part.recording);
        const auto u16 = [&](std::size_t at) { return unsigned(bytes[at]) | (unsigned(bytes[at + 1]) << 8); };
        const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
        const auto tag = [&](std::size_t at, std::string_view value) { return std::equal(value.begin(), value.end(), bytes.begin() + at); };
        require(part.available && !part.timings.empty() && std::isfinite(part.duration_ms) && part.duration_ms > 0
            && bytes.size() > 44 && tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data")
            && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000 && u16(34) == 16
            && u32(40) == bytes.size() - 44 && (bytes.size() - 44) % 2 == 0
            && std::ranges::any_of(bytes.begin() + 44, bytes.end(), [](auto byte) { return byte != 0; })
            && std::abs(part.duration_ms - (bytes.size() - 44) / 48.) <= 1,
            "U6 feedback lacks actual non-silent mono 24 kHz PCM16 and aligned words");
        double previous = 0;
        for (const auto& word : part.timings) {
            require(word.start < word.end && word.end <= wide(part.source).size()
                && std::isfinite(word.start_ms) && std::isfinite(word.end_ms) && word.start_ms >= previous
                && word.end_ms >= word.start_ms && word.end_ms <= part.duration_ms + 1, "Invalid real U6 feedback word alignment");
            previous = word.start_ms;
        }
        result.emplace(part.recording, state::sha256(bytes));
    }
    require(!result.empty(), "The real reader omitted U6 diagnostic feedback audio"); return result;
}
void unchanged_audio(const std::map<std::filesystem::path, std::string>& recordings) {
    for (const auto& [path, sha] : recordings) require(state::sha256(state::read_bytes(path)) == sha, "U6 reopening rewrote a compatible recording");
}
void run_goal(View& f, const test::U6DiagnosticCase& spec) {
    const auto at = position(f.lesson, spec.identity); const auto& block = owner(f.lesson, spec.identity); f.go(at.page);
    pending(f, at, block); require(find(f.view, control_id(at.block, 15)) == nullptr, "Diagnostic-only project exposed stdin");
    f.reader->restart(); f.reader->pause(); f.reader->seek_seconds(100000);
    auto boundary = f.snapshot(); require(!boundary.finished && !boundary.playing && !boundary.allocated
        && boundary.segments.at(boundary.index).block == at.block && boundary.heard_through == 0, "Seek bypassed a pending U6 reader boundary");
    f.reader->seek_seconds(-1); f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); return value.waiting_gate && !value.playing && !value.allocated; }, "U6 reader waits at its real compiler");
    require(!f.snapshot().finished, "A pending U6 compiler falsely finished its page");

    const auto repair = curriculum::prepare_project_compile_diagnostic_source(block, block.project_files, block.source_files);
    require(repair && repair->project, "Missing existing minimum U6 compiler control");
    f.project(at.block, repair->project->control_files, repair->project->control_sources); f.run(at.block);
    require(f.text(at.block, 4).starts_with("✗ Incorrecto") && f.text(at.block, 4).find("aún falta obtener el rechazo solicitado.") != std::string::npos,
        "A valid repaired project lost the requested-rejection retry guide");
    pending(f, at, block); const auto wrong_audio = feedback_audio(f.snapshot(), at.block, false);
    f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); const auto& part = value.segments.at(value.index);
        return !value.playing && !value.allocated && !value.preparing && part.block == at.block && part.feedback && !part.correct
            && value.offset >= part.duration_ms - 1; }, "complete U6 retry guide stops without advancing");
    require(!f.snapshot().finished, "The incorrect U6 guide completed a pending page");

    auto unrelated = repair->project->control_files;
    const auto first = std::ranges::find(unrelated, repair->project->control_sources.front(), [](const auto& file) { return file.first; });
    require(first != unrelated.end(), "Missing control translation unit"); first->second.insert(0, "#error QA_U6_UNRELATED\n");
    f.project(at.block, unrelated, repair->project->control_sources); f.run(at.block);
    require(f.text(at.block, 14).find("QA_U6_UNRELATED") != std::string::npos && f.text(at.block, 4).empty(),
        "Unrelated U6 build failure lost its technical diagnostic or became an educational verdict"); pending(f, at, block);

    f.project(at.block, block.project_files, block.source_files); f.run(at.block);
    require(windows::compiler_puzzle_completed(f.saved(), f.lesson, block) && f.text(at.block, 4).starts_with("✓ Correcto"),
        "The actual requested U6 rejection did not earn its objective");
    require(f.text(at.block, 34).starts_with(spec.link ? "Enlace rechazado." : "Compilación rechazada."),
        "U6 success reports the wrong compilation/link phase");
    const auto regions = lesson_reading_regions(f.view);
    require(std::ranges::any_of(regions, [&](const auto& region) { return region.block_index == at.block && region.feedback
        && region.correct && region.solved && region.required_compiler
        && region.source_text.find(block.compile_check->explanation) != std::string::npos; }),
        "The integrated reader omitted the complete current authored U6 explanation");
    const auto correct_audio = feedback_audio(f.snapshot(), at.block, true);
    // A fresh answer permits an explicit preview beyond its own feedback, but
    // that temporary priority grants no listening credit or page completion.
    const auto before_priority_seek = f.snapshot(); f.reader->seek_seconds(100000); const auto prioritized = f.snapshot();
    require(prioritized.heard_through == before_priority_seek.heard_through,
        "Prioritized U6 forward seek credited unheard success feedback");
    require(!prioritized.finished, "Prioritized U6 forward seek falsely finished the page");
    require(!prioritized.playing && !prioritized.allocated,
        "Prioritized U6 forward seek started or allocated paused audio");
    // Restart removes that explicit answer priority and resets listening. The
    // ordinary unread-feedback barrier must now retain the compiler's block.
    f.reader->restart(); f.reader->pause();
    const auto restarted = f.snapshot();
    require(restarted.heard_through == 0 && !restarted.finished && !restarted.playing && !restarted.allocated,
        "U6 reader restart retained listening credit, completion or active audio");
    const auto before_seek = f.snapshot(); f.reader->seek_seconds(100000); const auto sought = f.snapshot();
    require(sought.heard_through == before_seek.heard_through,
        "Ordinary U6 forward seek credited unheard success feedback");
    require(!sought.finished && !sought.playing && !sought.allocated,
        "Ordinary U6 forward seek finished the page or activated paused audio");
    require(sought.segments.at(sought.index).block == at.block && sought.segments.at(sought.index).feedback,
        "Ordinary U6 forward seek crossed its unread-feedback barrier: expected block " + std::to_string(at.block)
            + ", actual block " + std::to_string(sought.segments.at(sought.index).block)
            + ", segment " + std::to_string(sought.index));
    // Resume from the beginning of the success, not from the end reached by a
    // manual seek: the following explanation must be physically played.
    f.reader->seek_seconds(-100000); f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); const auto& part = value.segments.at(value.index);
        return value.playing && part.block == at.block && part.feedback && part.correct && value.offset > 120; }, "real U6 success before an exact pause");
    f.reader->pause(); const auto paused = f.snapshot(); const auto saved = f.saved();
    const auto draft_key = curriculum::draft_key("programming", f.lesson, block);
    const auto project = state::string_value(saved, "cpp_lesson_drafts", draft_key + "/project");
    f.reopen(); const auto resumed = f.snapshot();
    require(resumed.key == paused.key && resumed.index == paused.index && resumed.offset == paused.offset
        && resumed.heard_through == paused.heard_through && !resumed.playing && !resumed.allocated
        && windows::compiler_puzzle_completed(f.saved(), f.lesson, block)
        && state::string_value(f.saved(), "cpp_lesson_drafts", draft_key + "/project") == project,
        "U6 reopening changed its exact real pause, project draft or earned achievement");
    unchanged_audio(wrong_audio); unchanged_audio(correct_audio);
    f.reader->toggle(); wait([&] { return f.snapshot().finished; }, "complete real U6 success explanation before page completion", 180000);
    const auto complete = f.snapshot();
    for (const auto& part : complete.segments) if (part.block == at.block && part.feedback && part.correct)
        require(part.feedback_fully_played, "U6 continued before the whole correct explanation was actually played");
    require(narration::read_history(f.saved()).entries.size() <= 3, "U6 flow created a parallel or unbounded narration history");
    if (at.page + 1 < f.lesson.pages.size()) require(lesson_page_state(f.view).can_next, "The earned U6 objective still blocks its completed page");
    std::cout << "PASS " << spec.diagnostic << ": native project edits, accepted-control retry, unrelated error, authenticated expected rejection, gates, full real feedback and exact reopened pause\n";
}

void run_group(std::string_view group, std::shared_ptr<windows::NarrationWorker> backend) {
    const std::string filename = group == "declarations" ? "lesson_declaraciones_y_firmas.json"
        : group == "interfaces" ? "lesson_interfaces_cabeceras_y_programas_con_varios_archivos.json"
        : "lesson_construir_un_proyecto_desde_su_interfaz.json";
    const auto path = std::filesystem::path(build::source_root) / "app/src/main/assets/content/es/programming/cpp_fundamentals/funciones_y_contratos" / filename;
    const auto asset = state::sha256(state::read_bytes(path)); const auto lesson = windows::load_lesson(path);
    const auto before = test::load_pre_u6_diagnostic_lesson(filename);
    std::vector<test::U6DiagnosticCase> cases;
    for (const auto& spec : test::u6_diagnostic_cases) if (spec.file == filename) cases.push_back(spec);
    std::ranges::sort(cases, [&](const auto& a, const auto& b) { return position(lesson, a.identity).page < position(lesson, b.identity).page; });
    require(!cases.empty(), "Unknown U6 flow group");
    state::StateData historical; state::prepare_lesson_state(historical, before);
    historical.preferences["u6_flow_qa"]["retained"] = std::string(" exact\topaque\r\n保存 ");
    historical.preferences["u6_flow_qa"]["count"] = std::int64_t{7}; historical.preferences["u6_flow_qa"]["enabled"] = true;
    historical.preferences["u6_flow_qa"]["set"] = std::vector<std::string>{"one", "two"};
    std::map<std::string, std::string> old_responses;
    for (const auto& spec : cases) {
        const auto& old = owner(before, spec.identity); const auto& current = owner(lesson, spec.identity);
        require(!old.compile_check && !old.output_check && current.compile_check && current.compile_check->diagnostic == spec.diagnostic
            && !current.compile_check->explanation.empty() && current.initial_code == old.initial_code
            && current.project_files == old.project_files && current.source_files == old.source_files
            && curriculum::exercise_key(before, old) == curriculum::exercise_key(lesson, current)
            && curriculum::draft_key("programming", before, old) == curriculum::draft_key("programming", lesson, current),
            "Current U6 adoption changed historical program bytes, goal owner or draft address");
        state::Response opaque; opaque.correct = true; opaque.answer = "QA historical opaque flag; no diagnostic execution";
        opaque.raw_json = R"({"opaque":{"source":"isolated historical QA"},"incorrectAttempts":2})";
        state::put_response(historical, before, old, opaque);
        const auto response_key = state::response_storage_key(before, old);
        old_responses.emplace(response_key, state::string_value(historical, "learning_progress", response_key));
        state::Draft draft; draft.source = old.initial_code; draft.input = " \t dormant historical stdin\r\n";
        draft.indentation_json = state::make_indentation_metadata(draft.source, {}); draft.selection_start = draft.selection_end = 2;
        const auto key = curriculum::draft_key("programming", before, old); state::put_draft(historical, key, draft);
        historical.preferences["cpp_lesson_drafts"][key + "/project"] = project_json(old);
    }
    const auto environment = resolve_windows_environment({ProfileId::Codex,
        L"u6-flow-" + wide(group) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()), false});
    require(!std::filesystem::exists(environment.paths.root), "U6 flow requires a new private profile");
    initialize_profile_directories(environment.paths);
    const auto exported = environment.paths.root.parent_path() / (environment.paths.root.filename().wstring() + L"-export");
    export_preferences(exported, historical); const auto preview = state::inspect_android_export(exported);
    require(preview.data == historical, "U6 import inspection changed the historical typed values");
    state::validate_lesson_state(preview.data, lesson);
    require(preview.data == historical, "U6 compatibility inspection mutated its import source");
    state::commit_android_import(preview, environment.paths.root);
    state::StateStore store(environment.paths.root); auto data = store.load();
    require(data == historical, "U6 import transaction changed the original QA state");
    state::prepare_lesson_state(data, lesson); state::validate_lesson_state(data, lesson);
    for (const auto& spec : cases) require(!windows::compiler_puzzle_completed(data, lesson, owner(lesson, spec.identity)),
        "U6 import manufactured a diagnostic receipt from historical correctness");
    require(narration::read_history(data).entries.empty(), "U6 import invented a listening bookmark");
    prepare_ordinary(data, lesson, backend); execute_stdout_prerequisites(data, lesson, environment);
    const auto first_page = position(lesson, cases.front().identity).page;
    for (std::size_t page = 0; page <= first_page; ++page) (void)state::mark_page_reached(data, lesson, page);
    state::save_current_page(data, lesson, first_page);
    data.preferences["native_windows"]["ui_language"] = std::string("es");
    auto& settings = data.preferences["lesson_narration_settings"];
    settings["engine"] = std::string("KOKORO"); settings["language"] = std::string("es");
    settings["kokoro_voice:es"] = std::string("ef_dora"); settings["rate"] = 2.; settings["kokoro_advance_generation"] = true;
    store.save(data); const auto hearts = attempts::balance(data, "programming", now_ms()).available;
    View view(environment, lesson, backend);
    require(lesson_page_state(view.view).index == first_page, "The real U6 entry gates rejected its prepared prerequisites");
    const auto preserved = [&] {
        const auto saved = view.saved();
        require(saved.preferences.at("u6_flow_qa") == historical.preferences.at("u6_flow_qa"), "U6 flow changed unrelated historical preferences");
        require(attempts::balance(saved, "programming", now_ms()).available == hearts, "A U6 diagnostic operation consumed a heart");
        for (const auto& [key, value] : old_responses) require(state::string_value(saved, "learning_progress", key) == value,
            "U6 flow rewrote a historical compiler response or opaque metadata");
        for (const auto& spec : cases) {
            const auto key = curriculum::draft_key("programming", lesson, owner(lesson, spec.identity));
            require(state::get_draft(saved, key, "").input == state::get_draft(historical, key, "").input,
                "Compilation-only U6 UI rewrote a dormant historical stdin draft");
        }
        for (const auto& page : lesson.pages) for (const auto& block : page.blocks) if (block.output_check)
            require(windows::compiler_puzzle_completed(saved, lesson, block), "U6 flow revoked an independent real stdout prerequisite");
        for (const auto& file : preview.files) require(state::sha256(state::read_bytes(exported / file.relative_path)) == file.sha256
            && state::sha256(state::read_bytes(store.root() / "active/android" / file.relative_path)) == file.sha256,
            "U6 flow changed the source export or its archived import bytes");
        require(state::sha256(state::read_bytes(path)) == asset, "U6 flow changed authored curriculum bytes");
    };
    preserved();
    for (const auto& spec : cases) { run_goal(view, spec); preserved(); }
    view.go(lesson.pages.size() - 1);
    require(state::all_pages_reached(view.saved(), lesson) && lesson_options_snapshot(view.view).finish_enabled
        && view.finish_callbacks == 0, "The complete real U6 sequence did not enable its previously blocked Finish action");
    // Reset only the last objective, after the full sequence has been traversed.
    // Earlier actual achievements and unrelated drafts remain available.
    const auto& last = cases.back(); const auto at = position(lesson, last.identity); view.reader->pause();
    require(navigate_lesson_view(view.view, at.page), "Cannot return to the final completed U6 diagnostic"); view.ready();
    SendMessageW(view.control(at.block, 12), BM_CLICK, 0, 0); pump(); view.ready(); pending(view, at, owner(lesson, last.identity));
    for (std::size_t index = 0; index + 1 < cases.size(); ++index)
        require(windows::compiler_puzzle_completed(view.saved(), lesson, owner(lesson, cases[index].identity)), "Reset revoked another completed U6 diagnostic");
    preserved();
    std::cout << "PASS U6 " << group << ": " << cases.size()
        << " actual diagnostic editors; authentic e8389c8 import, native gates, real Kokoro/PCM playback, bounded pauses and scoped Reset."
        << " Other prerequisite adapters/answers are isolated QA setup, not student work; no visual or human listening acceptance claimed.\n";
}
}

int main(int argc, char** argv) {
    if (argc != 2 || (std::string_view(argv[1]) != "declarations" && std::string_view(argv[1]) != "interfaces"
        && std::string_view(argv[1]) != "project")) return 2;
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    const auto rich = LoadLibraryW(L"Msftedit.dll"); if (!rich) { OleUninitialize(); return 2; }
    int result = 0; std::cout << std::unitbuf;
    try {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
        auto backend = std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        run_group(argv[1], std::move(backend));
    } catch (const std::exception& error) { std::cerr << "FAIL U6 diagnostic flow: " << error.what() << '\n'; result = 1; }
    catch (const winrt::hresult_error& error) { std::cerr << "FAIL U6 Windows setup: " << error.code().value << '\n'; result = 1; }
    FreeLibrary(rich); OleUninitialize(); return result;
}

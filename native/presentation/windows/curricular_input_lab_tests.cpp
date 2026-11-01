#include "core/lesson.hpp"
#include "core/attempts.hpp"
#include "core/profile.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
std::string phase = "initialization";

void check(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(phase + ": " + std::string(message));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
std::string bytes(const std::filesystem::path& path) {
    const auto content = state::read_bytes(path);
    return std::string(content.begin(), content.end());
}
HWND descendant(HWND parent, int id) {
    struct Search { int id; HWND found = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND item, LPARAM raw) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(item) != search.id) return TRUE;
        search.found = item; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
std::string window_text(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(window, text.data(), static_cast<int>(text.size()))));
    return utf8(text);
}
curriculum::StatePosition position(const curriculum::Lesson& lesson, std::string_view identity) {
    const auto found = curriculum::stable_block_position(lesson, identity);
    check(found.has_value(), "A published curricular compiler identity is missing");
    return *found;
}

std::filesystem::path combinations_asset() {
    return std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_combinar_condiciones.json";
}
void combinations_contract(const curriculum::Lesson& lesson) {
    const auto target = position(lesson, "combinaciones_faltantes_editor");
    const auto original = position(lesson, "block_0003_0007");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& previous = lesson.pages[original.page].blocks[original.block];
    check(target.page == 5 && target.block == 6 && original.page == 3 && original.block == 7
        && compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == 3 && compiler.standard_input == "0 0 0"
        && !compiler.initial_code.empty() && previous.type == curriculum::BlockType::cpp_compiler
        && previous.initial_code == compiler.initial_code && previous.output_check
        && previous.output_check->cases.size() == 5 && !lesson.compiler_puzzles_required
        && curriculum::activity_prerequisites(lesson, compiler).empty()
        && curriculum::draft_key("programming", lesson, compiler) != curriculum::draft_key("programming", lesson, previous),
        "The U3 three-case editor lost its published contract or independent historical identity");
    constexpr std::array<std::string_view, 3> inputs{"0 0 0", "1 0 0", "1 0 1"};
    constexpr std::array<std::string_view, 3> outputs{"Entrada denegada", "Entrada denegada", "Entrada permitida"};
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& item = compiler.output_check->cases[index];
        check(item.standard_input == inputs[index] && !item.explanation.empty()
            && item.outputs == std::vector<std::string>{std::string(outputs[index]), std::string(outputs[index]) + '\n'},
            "The U3 missing-combinations cases, exact output alternatives or explanations changed");
    }
}

void prepare_visual_profile(std::wstring_view token, bool u3 = false, bool combinations = false) {
    phase = combinations ? "prepare new isolated U3 combinations visual QA"
        : u3 ? "prepare new isolated U3 stdout visual QA" : "prepare new isolated U2L9 visual QA";
    check(is_valid_qa_instance(token), "Unsafe visual QA instance token");
    const auto path = combinations ? combinations_asset() : std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals" /
        (u3 ? "expresiones_y_decisiones/lesson_validez_y_cortocircuito.json"
            : "objetos_y_estado/lesson_leer_una_entrada_conocida.json");
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    if (combinations) combinations_contract(lesson);
    const auto target = position(lesson, combinations ? "combinaciones_faltantes_editor"
        : u3 ? "cero_y_totales_validos_editor" : "block_0009_0003");
    const auto reference = (u3 || combinations) ? target : position(lesson, "block_0010_0001");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& authored_reference = lesson.pages[reference.page].blocks[reference.block];
    check(compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == (combinations ? 3 : 4) && !lesson.compiler_puzzles_required
        && curriculum::activity_prerequisites(lesson, compiler).empty(),
        "The visual QA target lost its current independent optional case contract");
    check(combinations ? target.page == 5 && target.block == 6 && compiler.standard_input == "0 0 0"
        : u3 ? target.page == 6 && target.block == 6 && compiler.standard_input == "0 4" && !compiler.initial_code.empty()
        : target.page == 9 && target.block == 3 && reference.page == 10 && reference.block == 1
            && compiler.standard_input == "Iris 4 2" && authored_reference.type == curriculum::BlockType::code
            && authored_reference.language == "cpp" && !authored_reference.code.empty(),
        "The selected visual QA source, input or audited position changed");
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    check(environment.paths.root.parent_path().filename() == L"qa"
        && environment.paths.root.parent_path().parent_path().filename() == L"Codex"
        && environment.paths.root.filename() == token,
        "Visual QA resolved outside the isolated Codex QA directory");
    state::require_safe_path(environment.paths.root);
    check(!std::filesystem::exists(environment.paths.root), "Visual QA refuses to reuse or overwrite an existing profile");
    std::filesystem::create_directories(environment.paths.root.parent_path());
    check(std::filesystem::create_directory(environment.paths.root), "Another process already created this QA profile");
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    const auto empty = store.load();
    check(empty.preferences.empty(), "The newly reserved QA profile unexpectedly contains previous state");
    state::StateData data;
    state::prepare_lesson_state(data, lesson);
    state::save_current_page(data, lesson, target.page);
    auto& preferences = data.preferences["native_windows"];
    preferences["ui_language"] = std::string("es");
    preferences["content_language"] = std::string("es");
    preferences["catalog/locale"] = std::string("es");
    preferences["catalog/route"] = std::string("programming");
    preferences["catalog/course"] = std::string("cpp_fundamentals");
    const auto unit_end = lesson.id.rfind("__");
    check(unit_end != std::string::npos, "The selected laboratory has no catalog unit identity");
    preferences["catalog/unit"] = lesson.id.substr(0, unit_end);
    preferences["navigation/lesson"] = lesson.id;
    // U2 uses its authored later reference as an explicit QA-only draft;
    // U3 keeps its original initialCode. Neither setup executes a program or
    // credits any prerequisite, response or goal.
    state::Draft draft;
    draft.source = (u3 || combinations) ? compiler.initial_code : authored_reference.code;
    draft.input = *compiler.standard_input;
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    state::put_draft(data, draft_key, draft);
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        check(!state::get_response(data, lesson, block), "Preparing visual QA invented a quiz response");
        if (block.type == curriculum::BlockType::cpp_compiler)
            check(windows::compiler_puzzle_case_count(data, lesson, block) == 0
                && !windows::compiler_puzzle_completed(data, lesson, block),
                "Preparing visual QA invented a compiler receipt or achievement");
    }
    state::validate_lesson_state(data, lesson);
    store.save(data);
    const auto saved = store.load();
    const auto restored = state::get_draft(saved, draft_key, "");
    check(saved == data && state::current_page(saved, lesson) == target.page
        && restored.has_saved && restored.source == draft.source && restored.input == *compiler.standard_input,
        "The prepared QA profile did not preserve its uncredited reference draft and input exactly");
    check(state::sha256(state::read_bytes(path)) == asset_hash, "Preparing visual QA changed the authored asset");
    std::cout << "profile=Codex\nqa_instance=" << utf8(token)
        << "\ndata_root=" << utf8(environment.paths.root.native()) << "\nlesson=" << lesson.id
        << "\ntarget=" << compiler.stable_id << "\npage=" << target.page + 1 << "\npage_index=" << target.page
        << "\nblock=" << target.block + 1 << "\nsource_reference=" << ((u3 || combinations) ? "none" : authored_reference.stable_id)
        << "\nsource_seed=" << ((u3 || combinations) ? "authored-initialCode-not-an-execution" : "authored-reference-draft-only-not-an-execution")
        << "\ninput=" << *compiler.standard_input
        << "\nsynthetic_prerequisites=none\nquiz_responses=none\ncompiler_receipts=none"
        << "\ntarget_completed=0\ncompiler_puzzles_required=0\nwindows_created=0\n";
}

state::Preferences compiler_receipts(const state::StateData& data, const curriculum::Lesson& lesson,
    const curriculum::Block& block) {
    state::Preferences result;
    const auto prefix = "compiler-puzzle/v1/" + lesson.locale + '/' + lesson.id + '/'
        + curriculum::exercise_key(lesson, block) + '/';
    if (const auto domain = data.preferences.find("native_windows"); domain != data.preferences.end())
        for (const auto& entry : domain->second) if (entry.first.starts_with(prefix)) result.insert(entry);
    return result;
}

void inspect_combinations_visual(std::wstring_view token) {
    phase = "read-only inspection of isolated U3 combinations visual QA";
    check(is_valid_qa_instance(token), "Unsafe visual QA instance token");
    const auto path = combinations_asset();
    const auto asset_before = state::read_bytes(path);
    const auto lesson = windows::load_lesson(path);
    combinations_contract(lesson);
    const auto environment = resolve_windows_environment({ProfileId::Codex, std::wstring(token), false});
    check(environment.paths.root.parent_path().filename() == L"qa"
        && environment.paths.root.parent_path().parent_path().filename() == L"Codex"
        && environment.paths.root.filename() == token, "Inspection resolved outside isolated Codex QA");
    const auto active = environment.paths.root / L"state/active/state.bin";
    state::require_safe_path(active);
    const auto before = state::read_bytes(active);
    check(before.size() >= 64, "The active QA state is incomplete; inspection preserved the file");
    const std::string checksum(before.end() - 64, before.end());
    const std::span<const std::uint8_t> payload(before.data(), before.size() - 64);
    check(state::sha256(payload) == checksum, "The active QA state checksum does not match; inspection preserved the file");
    const auto data = state::decode(payload); // No StateStore load, migration or write.
    state::validate_lesson_state(data, lesson);
    using namespace winrt::Windows::Data::Json;
    JsonObject result;
    const auto string = [&](const wchar_t* name, std::string_view value) {
        result.Insert(name, JsonValue::CreateStringValue(winrt::to_hstring(value)));
    };
    string(L"qa_instance", utf8(token)); string(L"lesson", lesson.id);
    string(L"asset_sha256", state::sha256(asset_before)); string(L"active_state_sha256", state::sha256(before));
    result.Insert(L"active_state_checksum_verified", JsonValue::CreateBooleanValue(true));
    result.Insert(L"page", JsonValue::CreateNumberValue(static_cast<double>(state::current_page(data, lesson) + 1)));
    JsonArray editors;
    for (const auto identity : {"combinaciones_faltantes_editor", "block_0003_0007"}) {
        const auto at = position(lesson, identity);
        const auto& block = lesson.pages[at.page].blocks[at.block];
        const auto draft = state::get_draft(data, curriculum::draft_key("programming", lesson, block),
            block.initial_code, block.standard_input.value_or(""));
        JsonObject item;
        item.Insert(L"identity", JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        item.Insert(L"case_count", JsonValue::CreateNumberValue(static_cast<double>(windows::compiler_puzzle_case_count(data, lesson, block))));
        item.Insert(L"required_cases", JsonValue::CreateNumberValue(static_cast<double>(block.output_check->cases.size())));
        item.Insert(L"completed", JsonValue::CreateBooleanValue(windows::compiler_puzzle_completed(data, lesson, block)));
        item.Insert(L"first_achievement_feedback", JsonValue::CreateStringValue(winrt::to_hstring(windows::compiler_puzzle_feedback(data, lesson, block))));
        state::StateData receipts; receipts.preferences["native_windows"] = compiler_receipts(data, lesson, block);
        item.Insert(L"receipt_sha256", JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(state::encode(receipts)))));
        item.Insert(L"draft_saved", JsonValue::CreateBooleanValue(draft.has_saved));
        item.Insert(L"draft_source_sha256", JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(draft.source))));
        item.Insert(L"draft_input", JsonValue::CreateStringValue(winrt::to_hstring(draft.input)));
        item.Insert(L"draft_input_sha256", JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(draft.input))));
        item.Insert(L"draft_is_initial_code", JsonValue::CreateBooleanValue(draft.source == block.initial_code));
        editors.Append(item);
    }
    result.Insert(L"editors", editors);
    std::size_t quiz_responses = 0;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks)
        if (state::get_response(data, lesson, block)) ++quiz_responses;
    result.Insert(L"quiz_responses", JsonValue::CreateNumberValue(static_cast<double>(quiz_responses)));
    check(state::read_bytes(active) == before && state::read_bytes(path) == asset_before,
        "The active QA state or authored asset changed during read-only inspection; retry after the app saves");
    result.Insert(L"read_only_state_and_asset_unchanged", JsonValue::CreateBooleanValue(true));
    std::cout << winrt::to_string(result.Stringify()) << '\n';
}

struct View {
    WindowsEnvironment environment;
    const curriculum::Lesson& lesson;
    std::size_t block;
    HWND host = nullptr, view = nullptr;
    std::size_t executions = 0;

    View(const curriculum::Lesson& model, curriculum::StatePosition target, bool finite_reserve = false)
        : environment(resolve_windows_environment({ProfileId::Codex,
            L"curricular-input-lab-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                + std::to_wstring(GetTickCount64()), false})), lesson(model), block(target.block) {
        check(!std::filesystem::exists(environment.paths.root), "Input laboratory QA requires a new private profile");
        initialize_profile_directories(environment.paths);
        state::StateData initial;
        state::prepare_lesson_state(initial, lesson);
        state::save_current_page(initial, lesson, target.page);
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences["input_lab_qa_unrelated"]["keep"] = std::string(" \t unchanged\n");
        if (finite_reserve) {
            auto& reserve = initial.preferences["attempt_reserve_seconds_test"];
            reserve["reserve_programming"] = 3.;
            reserve["updated_at_programming"] = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            reserve["recovery_interval_programming"] = attempts::recovery_millis;
        }
        state::StateStore store(environment.paths.root);
        (void)store.load(); store.save(initial);
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Hidden current U2L9 input laboratory QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(host != nullptr, "Cannot create hidden input laboratory host");
        open();
    }
    ~View() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
    }
    void open() {
        view = create_lesson_view(host, environment, lesson);
        check(view != nullptr, "Cannot create the actual input laboratory lesson view");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        check(!IsWindowVisible(host), "Input laboratory QA exposed a desktop window");
    }
    void reopen() {
        check(flush_lesson_view(view), "Cannot preserve the input laboratory before closing");
        DestroyWindow(view); view = nullptr; open();
    }
    void reopen_at_for_qa(std::size_t page) {
        check(flush_lesson_view(view), "Cannot preserve historical edits before reserving the focal QA position");
        DestroyWindow(view); view = nullptr;
        state::StateStore store(environment.paths.root);
        auto saved = store.load();
        // This fixture mounts a focal page without answering intervening
        // quizzes. It is preparation, not evidence that curricular navigation
        // or its legitimate completion gates were exercised.
        state::save_current_page(saved, lesson, page);
        store.save(saved); open();
    }
    state::StateData saved() {
        check(flush_lesson_view(view), "Cannot flush the isolated input laboratory state");
        state::StateStore store(environment.paths.root);
        return store.load();
    }
    HWND control(std::size_t index, int part) {
        const auto id = 1000 + static_cast<int>(index) * 100 + part;
        const auto result = descendant(view, id);
        check(result != nullptr, "Missing actual control id=" + std::to_string(id)
            + " block=" + std::to_string(index) + " part=" + std::to_string(part)
            + " page=" + std::to_string(lesson_page_state(view).index + 1)
            + " completed executions=" + std::to_string(executions));
        return result;
    }
    std::string text(int part) { return edit_text(control(block, part)); }
    void replace(int part, std::string_view value) {
        const auto edit = control(block, part);
        check(IsWindowEnabled(edit), "The actual source/input editor is unexpectedly locked");
        CHARRANGE all{0, -1};
        SendMessageW(edit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto native = wide(value);
        SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(native.c_str())); pump();
        check(edit_text(edit) == value, "A real source/input edit changed supplied characters");
    }
    std::set<std::filesystem::path> run_directories() const {
        std::set<std::filesystem::path> result;
        const auto parent = environment.paths.projects / L"cpp-runs";
        if (std::filesystem::exists(parent))
            for (const auto& entry : std::filesystem::directory_iterator(parent))
                if (entry.is_directory()) result.insert(entry.path());
        return result;
    }
    std::filesystem::path run(std::string_view source, std::string_view input, std::string_view expected,
        std::string_view expected_raw = {}) {
        const auto before = run_directories();
        const auto button = control(block, 11);
        check(IsWindowEnabled(button) && window_text(button) == "Compilar, ejecutar y comprobar",
            "The actual input laboratory is busy or has no combined execution action");
        SendMessageW(button, BM_CLICK, 0, 0);
        const auto deadline = GetTickCount64() + 70000;
        do { pump(); if (IsWindowEnabled(button)) break;
            MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
        } while (GetTickCount64() < deadline);
        check(IsWindowEnabled(button), "The real input laboratory compilation/execution timed out");
        const auto status = text(34);
        check(status == "Ejecución terminada (código 0)." || status.starts_with("Caso correcto. Casos comprobados: "),
            "The real input laboratory did not complete compilation and execution successfully");
        const auto after = run_directories();
        std::vector<std::filesystem::path> created;
        for (const auto& directory : after) if (!before.contains(directory)) created.push_back(directory);
        check(created.size() == 1, "A laboratory action did not create exactly one fresh execution");
        const auto& directory = created.front();
        const auto compiler = std::filesystem::path(build::educational_vc_root) / L"bin/Hostx64/x64/cl.exe";
        const auto toolchain = bytes(directory / L"toolchain.txt");
        check(toolchain.starts_with("MSVC=") && toolchain.find("compiler=" + utf8(compiler.wstring()) + '\n') != std::string::npos
            && std::filesystem::is_regular_file(directory / L"program.exe")
            && std::filesystem::file_size(directory / L"program.exe") > 0,
            "The laboratory did not use the configured MSVC compiler and a real executable");
        check(bytes(directory / L"main.cpp") == source && bytes(directory / L"stdin.bin") == input,
            "The compiler did not capture the exact authored source and current prepared input");
        check(bytes(directory / L"execution-stdout.bin") == (expected_raw.empty() ? expected : expected_raw) && text(14) == expected
            && bytes(directory / L"execution-stderr.bin").empty(),
            "The actual program's binary output or visible console differs from the exact requested output");
        check(!IsWindowVisible(host), "A laboratory execution exposed the QA host");
        ++executions;
        return directory;
    }
};

void current_three_input_laboratory() {
    phase = "load current U2L9 asset and contracts";
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json";
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    const auto target = position(lesson, "block_0009_0003");
    const auto reference = position(lesson, "block_0010_0001");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& authored_reference = lesson.pages[reference.page].blocks[reference.block];
    check(target.page == 9 && target.block == 3 && reference.page == 10 && reference.block == 1
        && compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == 4 && compiler.standard_input == "Iris 4 2"
        && authored_reference.type == curriculum::BlockType::code && authored_reference.language == "cpp"
        && !authored_reference.code.empty() && !lesson.compiler_puzzles_required,
        "The current voluntary U2L9 laboratory/reference no longer has its published four-case contract");
    struct Case { std::string_view canonical, input, output; };
    constexpr std::array<Case, 4> cases{{
        {"Iris 4 2", "Iris 4 2", "Iris: 22"},
        {"Leo 0 4", "Leo\n0\n4\n", "Leo: 4"},
        {"Vera 3 1", "  Vera\t3 1 \n", "Vera: 16"},
        {"Sol 20 4", "Sol 20 4", "Sol: 104"}
    }};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& published = compiler.output_check->cases[index];
        check(published.standard_input == cases[index].canonical
            && published.outputs == std::vector<std::string>{std::string(cases[index].output), std::string(cases[index].output) + '\n'},
            "The actual input cases lost their exact output or optional-final-LF alternatives");
    }
    phase = "mount actual p10 laboratory in a new QA profile";
    View fixture(lesson, target);
    const auto initial = fixture.saved();
    const auto unrelated = initial.preferences.at("input_lab_qa_unrelated");
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == compiler.initial_code
        && fixture.text(15) == *compiler.standard_input
        && windows::compiler_puzzle_case_count(initial, lesson, compiler) == 0
        && !windows::compiler_puzzle_completed(initial, lesson, compiler),
        "The isolated actual input laboratory began on another page or with invented credit");
    phase = "enter the authored reference through the real source editor";
    fixture.replace(10, authored_reference.code);
    const auto retained = [&](std::size_t count) {
        const auto saved = fixture.saved();
        check(saved.preferences.at("input_lab_qa_unrelated") == unrelated,
            "Input laboratory operations changed an unrelated QA preference");
        check(windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == (count == cases.size()),
            "Real input executions did not preserve the exact number of independently checked cases");
        for (const auto& page : lesson.pages) for (const auto& other : page.blocks)
            if (other.type == curriculum::BlockType::cpp_compiler && other.stable_id != compiler.stable_id)
                check(windows::compiler_puzzle_case_count(saved, lesson, other) == 0
                    && !windows::compiler_puzzle_completed(saved, lesson, other),
                    "The focal laboratory invented progress for another compiler");
        check(!lesson.compiler_puzzles_required, "QA changed the published optional compiler policy");
    };
    for (std::size_t index = 0; index < cases.size(); ++index) {
        phase = "case " + std::to_string(index + 1) + ": " + std::string(cases[index].canonical);
        fixture.replace(15, cases[index].input);
        fixture.run(authored_reference.code, cases[index].input, cases[index].output);
        retained(index + 1);
        if (index + 1 < cases.size())
            check(fixture.text(4).empty() && fixture.text(34) == "Caso correcto. Casos comprobados: "
                + std::to_string(index + 1) + " de 4.",
                "A partial multi-case laboratory lost its checked-case status or falsely displayed final achievement");
    }
    phase = "repeat the actual Sol execution without editing";
    check(fixture.text(4) == "✓ Correcto", "All four real input cases did not complete their actual objective");
    fixture.run(authored_reference.code, cases.back().input, cases.back().output); // No edit between these two Sol executions.
    retained(cases.size());
    const std::string mixed_input = "Iris\t4\n2";
    phase = "Iris input with tab and LF before reopening";
    fixture.replace(15, mixed_input);
    fixture.run(authored_reference.code, mixed_input, cases.front().output);
    retained(cases.size());
    const auto before_reopen = state::get_draft(fixture.saved(), draft_key, "");
    check(before_reopen.has_saved && before_reopen.source == authored_reference.code && before_reopen.input == mixed_input,
        "The real editor/input controls did not persist their exact current draft");
    phase = "destroy and reopen the actual p10 laboratory";
    fixture.reopen();
    const auto restored = state::get_draft(fixture.saved(), draft_key, "");
    check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == authored_reference.code
        && fixture.text(15) == mixed_input && restored.source == before_reopen.source && restored.input == before_reopen.input
        && fixture.text(4) == "✓ Correcto", "Destroying/reopening lost the actual source, prepared input, page or achievement");
    retained(cases.size());
    phase = "fresh Iris execution after reopening";
    fixture.run(authored_reference.code, mixed_input, cases.front().output);
    retained(cases.size());
    phase = "open and verify the actual p11 reference";
    check(navigate_lesson_view(fixture.view, reference.page), "Cannot reach the published reference after completing the laboratory"); pump();
    check(edit_text(fixture.control(reference.block, 1)) == authored_reference.code,
        "The actual unlocked reference differs from the source exercised through the editor");
    phase = "return to p10 and verify retained state and asset";
    check(navigate_lesson_view(fixture.view, target.page), "Cannot return to the completed input laboratory"); pump();
    check(fixture.text(10) == authored_reference.code && fixture.text(15) == mixed_input,
        "Reference navigation changed the laboratory's independent source/input draft");
    retained(cases.size());
    check(fixture.executions == 7 && state::sha256(state::read_bytes(path)) == asset_hash && !IsWindowVisible(fixture.host),
        "The focal input QA lost a fresh execution, changed authored bytes or exposed the desktop");
    std::cout << "PASS current U2L9 p10b4: seven fresh native MSVC runs, Iris/Leo/Vera/Sol exact binary outputs, token whitespace variants, four-case progress, repeated input, exact source/input/achievement reopen and p11 reference; no required-policy activation or other objective credit\n";
}

void current_u3_stdout_transport() {
    phase = "load current U3 zero-and-totals stdout objective";
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_validez_y_cortocircuito.json";
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    const auto target = position(lesson, "cero_y_totales_validos_editor");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    check(target.page == 6 && target.block == 6 && compiler.type == curriculum::BlockType::cpp_compiler
        && compiler.output_check && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == 4 && compiler.standard_input == "0 4"
        && !compiler.initial_code.empty() && !lesson.compiler_puzzles_required
        && curriculum::activity_prerequisites(lesson, compiler).empty(),
        "The current U3 optional stdout objective lost its exact position, initial source, input or four-case contract");
    struct Case { std::string_view input, output, raw; };
    constexpr std::array<Case, 4> cases{{
        {"0 4", "1\nGrupos: 0", "1\r\nGrupos: 0"},
        {"1 4", "0\nGrupos: 0", "0\r\nGrupos: 0"},
        {"28 6", "0\nGrupos: 4", "0\r\nGrupos: 4"},
        {"120 12", "1\nGrupos: 10", "1\r\nGrupos: 10"}
    }};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& published = compiler.output_check->cases[index];
        check(published.standard_input == cases[index].input && !published.explanation.empty()
            && published.outputs == std::vector<std::string>{std::string(cases[index].output), std::string(cases[index].output) + '\n'},
            "The U3 authored LF outputs, optional final break or current success explanations changed");
    }
    View fixture(lesson, target);
    const auto initial = fixture.saved();
    const auto unrelated = initial.preferences.at("input_lab_qa_unrelated");
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    const auto source = compiler.initial_code;
    const auto retained = [&](std::size_t count, std::string_view input) {
        const auto saved = fixture.saved();
        const auto draft = state::get_draft(saved, draft_key, "");
        check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == source
            && fixture.text(15) == input && saved.preferences.at("input_lab_qa_unrelated") == unrelated
            && windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == (count == cases.size()),
            "The U3 current page, unchanged source/input, independent case progress or preference was lost");
        if (count) check(draft.has_saved && draft.source == source && draft.input == input,
            "The U3 actual source/input editors lost their exact persisted draft");
        for (const auto& page : lesson.pages) for (const auto& other : page.blocks)
            if (other.type == curriculum::BlockType::cpp_compiler && other.stable_id != compiler.stable_id)
                check(windows::compiler_puzzle_case_count(saved, lesson, other) == 0
                    && !windows::compiler_puzzle_completed(saved, lesson, other),
                    "The focal U3 regression manufactured another compiler's achievement");
    };
    retained(0, *compiler.standard_input);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& item = cases[index];
        phase = "U3 native MSVC execution " + std::to_string(index + 1) + ": " + std::string(item.input);
        fixture.replace(15, item.input);
        const auto run_directory = fixture.run(source, item.input, item.output, item.raw);
        const auto raw = bytes(run_directory / L"execution-stdout.bin");
        curriculum::FreshExecution execution;
        execution.source_revision = 1;
        execution.compilation_succeeded = execution.execution_succeeded = true;
        execution.exit_code = 0;
        execution.standard_input = bytes(run_directory / L"stdin.bin");
        execution.standard_output = raw;
        const auto exact = curriculum::evaluate_output_check(*compiler.output_check, execution, 1);
        const auto native = windows::evaluate_cpp_output_check(*compiler.output_check, execution, 1);
        check(exact.verdict == curriculum::OutputVerdict::incorrect
            && native.verdict == curriculum::OutputVerdict::correct && native.stdout_correct
            && native.feedback == compiler.output_check->cases[index].explanation
            && execution.standard_output == raw && bytes(run_directory / L"execution-stdout.bin") == item.raw,
            "The current U3 CRT CRLF capture was rejected, rewritten or credited by a weakened shared LF contract");
        if (!index) {
            // One curricular negative checks that accepting CRT separators
            // does not flatten this lesson's two required output lines.
            const auto separator = execution.standard_output.find("\r\n");
            check(separator != std::string::npos, "The actual first U3 program emitted no native line separator");
            execution.standard_output.erase(separator, 2);
            check(windows::evaluate_cpp_output_check(*compiler.output_check, execution, 1).verdict
                == curriculum::OutputVerdict::incorrect,
                "The current U3 objective accepted its two required lines concatenated");
        }
        retained(index + 1, item.input);
        if (index + 1 < cases.size())
            check(fixture.text(4).empty() && fixture.text(34) == "Caso correcto. Casos comprobados: "
                + std::to_string(index + 1) + " de 4.",
                "A native U3 execution lost its partial case count or invented complete feedback");
        if (index == 1) {
            phase = "restore the two genuine U3 partial receipts";
            fixture.reopen(); retained(2, item.input);
            check(fixture.text(4).empty(), "Reopening two U3 cases invented the final achievement");
        }
    }
    phase = "restore the four-case U3 achievement and exact original source/input";
    const auto feedback = "✓ Correcto\n\n" + compiler.output_check->cases.back().explanation;
    check(fixture.text(4) == feedback, "The completed U3 objective lost its exact final case explanation");
    fixture.reopen(); retained(cases.size(), cases.back().input);
    check(fixture.text(4) == feedback, "The U3 final verdict/explanation did not survive reopening");
    bool reading = false;
    for (const auto& region : lesson_reading_regions(fixture.view))
        if (region.target == fixture.control(target.block, 4)) {
            reading = true;
            check(region.feedback && region.correct && region.source_text == feedback,
                "The restored U3 feedback's reader source differs from its completed visible result");
        }
    check(reading && fixture.executions == 4 && !IsWindowVisible(fixture.host)
        && state::sha256(state::read_bytes(path)) == asset_hash,
        "The U3 QA lost a fresh run, changed the authored asset or exposed the desktop");
    std::cout << "PASS current U3 p7b7 zero/totals: four real native MSVC runs of unchanged initialCode, exact raw CRLF stdout and stdin/source captures, shared LF contract unchanged, Windows verdicts and current feedback, four-case partial/full reopen, source/input/other state preserved; no synthetic receipts or desktop/listening acceptance\n";
}

void current_u3_combinations() {
    phase = "load current U3 missing-combinations objective";
    const auto path = combinations_asset();
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    combinations_contract(lesson);
    const auto target = position(lesson, "combinaciones_faltantes_editor");
    const auto original = position(lesson, "block_0003_0007");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& historical = lesson.pages[original.page].blocks[original.block];
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    const auto historical_key = curriculum::draft_key("programming", lesson, historical);
    const auto source = compiler.initial_code;
    auto wrong = source;
    const std::string expression = "(tiene_pase || tiene_invitacion) && sala_abierta";
    const auto expression_at = wrong.find(expression);
    check(expression_at != std::string::npos && wrong.find(expression, expression_at + 1) == std::string::npos,
        "The published source changed; the intentionally wrong grouping must be reviewed");
    wrong.replace(expression_at, expression.size(), "tiene_pase || (tiene_invitacion && sala_abierta)");
    View fixture(lesson, target, true);
    check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == source
        && fixture.text(15) == *compiler.standard_input, "The actual new U3 editor lost its exact initialCode/input");
    phase = "edit the independent historical five-case editor through its actual controls";
    check(navigate_lesson_view(fixture.view, original.page), "Cannot open the historical compiler's actual page"); pump();
    fixture.block = original.block;
    const auto historical_source = historical.initial_code + "// Independent historical QA draft.\n";
    const std::string historical_input = " 0\t1\n1 ";
    fixture.replace(10, historical_source); fixture.replace(15, historical_input);
    fixture.reopen_at_for_qa(target.page);
    fixture.block = target.block;
    const auto initial = fixture.saved();
    const auto unrelated = initial.preferences.at("input_lab_qa_unrelated");
    const auto reserve = initial.preferences.at("attempt_reserve_seconds_test");
    const auto historical_draft = state::get_draft(initial, historical_key, "");
    const auto original_receipts = compiler_receipts(initial, lesson, historical);
    check(historical_draft.has_saved && historical_draft.source == historical_source
        && historical_draft.input == historical_input && original_receipts.empty() && !attempts::premium(initial),
        "The historical QA draft was not independent and uncredited, or QA bypassed the finite reserve");
    const auto retained = [&](std::size_t count, std::string_view current_source, std::string_view input) {
        const auto saved = fixture.saved();
        const auto draft = state::get_draft(saved, draft_key, source, *compiler.standard_input);
        const auto previous = state::get_draft(saved, historical_key, "");
        check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == current_source
            && fixture.text(15) == input && draft.source == current_source && draft.input == input
            && windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == (count == 3),
            "The new U3 editor lost its exact draft, current page or genuine three-case progress");
        check(saved.preferences.at("input_lab_qa_unrelated") == unrelated
            && saved.preferences.at("attempt_reserve_seconds_test") == reserve
            && previous.has_saved && previous.source == historical_draft.source && previous.input == historical_draft.input
            && previous.indentation_json == historical_draft.indentation_json
            && previous.selection_start == historical_draft.selection_start && previous.selection_end == historical_draft.selection_end
            && compiler_receipts(saved, lesson, historical) == original_receipts,
            "The new U3 operations consumed reserve or changed unrelated/historical editor state");
        for (const auto& page : lesson.pages) for (const auto& other : page.blocks) {
            check(!state::get_response(saved, lesson, other), "Compiler QA invented an unrelated quiz response");
            if (other.type == curriculum::BlockType::cpp_compiler && other.stable_id != compiler.stable_id)
                check(windows::compiler_puzzle_case_count(saved, lesson, other) == 0
                    && !windows::compiler_puzzle_completed(saved, lesson, other),
                    "The three-case editor credited the historical editor or another objective");
        }
    };
    retained(0, source, *compiler.standard_input);
    phase = "first genuine 000 receipt before changing the program";
    fixture.run(source, "0 0 0", "Entrada denegada"); retained(1, source, "0 0 0");
    check(fixture.text(4).empty() && fixture.text(34) == "Caso correcto. Casos comprobados: 1 de 3.",
        "One successful input was presented as the complete three-case achievement");
    phase = "edit invalidates the incomplete program and run an actual wrong grouping at 100";
    fixture.replace(10, wrong); retained(0, wrong, "0 0 0");
    fixture.replace(15, "1 0 0");
    fixture.run(wrong, "1 0 0", "Entrada permitida"); retained(0, wrong, "1 0 0");
    check(fixture.text(4) == "✗ Incorrecto", "The real wrong grouping was not rejected for the 100 case");
    fixture.reopen(); retained(0, wrong, "1 0 0");
    phase = "retry the original program without resurrecting its discarded partial receipt";
    fixture.replace(10, source); retained(0, source, "1 0 0");
    constexpr std::array<std::string_view, 3> inputs{"0 0 0", "1 0 0", "1 0 1"};
    constexpr std::array<std::string_view, 3> outputs{"Entrada denegada", "Entrada denegada", "Entrada permitida"};
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        phase = "U3 missing-combinations retry " + std::to_string(index + 1) + ": " + std::string(inputs[index]);
        fixture.replace(15, inputs[index]);
        fixture.run(source, inputs[index], outputs[index]); retained(index + 1, source, inputs[index]);
        if (index + 1 < inputs.size())
            check(fixture.text(4).empty() && fixture.text(34) == "Caso correcto. Casos comprobados: "
                + std::to_string(index + 1) + " de 3.", "A partial retry lost its counter or invented final feedback");
        if (index == 1) {
            fixture.reopen(); retained(2, source, inputs[index]);
            check(fixture.text(4).empty(), "Reopening two genuine receipts invented final achievement");
        }
    }
    const auto feedback = "✓ Correcto\n\n" + compiler.output_check->cases.back().explanation;
    check(fixture.text(4) == feedback, "The completed U3 editor lost its actual final case explanation");
    const auto first_achievement = compiler_receipts(fixture.saved(), lesson, compiler);
    check(!first_achievement.empty(), "Three actual correct executions produced no achievement provenance");
    fixture.reopen(); retained(3, source, inputs.back());
    check(fixture.text(4) == feedback, "The three-case achievement and final explanation did not survive reopening");
    phase = "a later wrong experiment preserves the first genuine achievement";
    fixture.replace(10, wrong); fixture.replace(15, "1 0 0");
    fixture.run(wrong, "1 0 0", "Entrada permitida"); retained(3, wrong, "1 0 0");
    check(compiler_receipts(fixture.saved(), lesson, compiler) == first_achievement && fixture.text(4) == feedback,
        "A later wrong experiment rewrote the first genuine program, receipts or success feedback");
    fixture.reopen(); retained(3, wrong, "1 0 0");
    check(fixture.text(4) == feedback && compiler_receipts(fixture.saved(), lesson, compiler) == first_achievement,
        "Reopening replaced the first achievement with the later incorrect draft");
    phase = "explicit actual editor reset clears only its own achievement";
    const auto reset = fixture.control(target.block, 12);
    wchar_t reset_class[32]{};
    check(GetClassNameW(reset, reset_class, 32) && lstrcmpiW(reset_class, L"BUTTON") == 0 && IsWindowEnabled(reset),
        "The actual icon reset action is not an enabled native button");
    SendMessageW(reset, BM_CLICK, 0, 0); pump(); retained(0, source, "1 0 0");
    check(fixture.text(4).empty() && fixture.text(14).empty()
        && compiler_receipts(fixture.saved(), lesson, compiler).empty(), "Explicit reset retained stale output, feedback or receipts");
    fixture.reopen(); retained(0, source, "1 0 0");
    check(fixture.text(4).empty() && compiler_receipts(fixture.saved(), lesson, compiler).empty()
        && fixture.executions == 6 && state::sha256(state::read_bytes(path)) == asset_hash,
        "Reset/reopening resurrected achievement, lost a real execution or changed the curricular asset");
    std::cout << "PASS current U3 p6b7 missing combinations: six fresh native MSVC executions with exact source/stdin/stdout/stderr captures and successful exit status; 000/100/101 receipts, wrong grouping, partial-program invalidation, retry/reopen, retained first achievement and explicit reset; finite reserve and independent historical editor preserved. This checks the published three-input contract, not all eight Boolean combinations or desktop/listening acceptance.\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const bool u3_stdout = argc == 2 && std::string_view(argv[1]) == "--u3-stdout";
    const bool u3_combinations = argc == 2 && std::string_view(argv[1]) == "--u3-combinations";
    if (argc != 1 && !u3_stdout && !u3_combinations) {
        const bool shape = argc == 3 && (std::string_view(argv[1]) == "--prepare-visual"
            || std::string_view(argv[1]) == "--prepare-u3-visual"
            || std::string_view(argv[1]) == "--prepare-u3-combinations-visual"
            || std::string_view(argv[1]) == "--inspect-u3-combinations-visual");
        const auto raw = shape ? std::string_view(argv[2]) : std::string_view{};
        const std::wstring token(raw.begin(), raw.end());
        if (!shape || !is_valid_qa_instance(token)) {
            std::cerr << "Usage: curricular_input_lab_tests [--u3-stdout | --u3-combinations | --prepare-visual <safe-new-QA-token> | --prepare-u3-visual <safe-new-QA-token> | --prepare-u3-combinations-visual <safe-new-QA-token> | --inspect-u3-combinations-visual <safe-QA-token>]\n";
            return 2;
        }
        // Preparation performs only JSON/state/file operations. The original
        // regression remains responsible for all native control initialization.
        try {
            if (std::string_view(argv[1]) == "--inspect-u3-combinations-visual") inspect_combinations_visual(token);
            else prepare_visual_profile(token, std::string_view(argv[1]) == "--prepare-u3-visual",
                std::string_view(argv[1]) == "--prepare-u3-combinations-visual");
            return 0;
        }
        catch (const std::exception& error) { std::cerr << "Visual QA preparation failed: " << error.what() << '\n'; return 1; }
    }
    const auto apartment = OleInitialize(nullptr);
    if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll");
    int result = 0;
    try {
        check(rich != nullptr, "Cannot load RichEdit for the current input laboratory");
        reader_theme::prepare_fonts();
        if (u3_combinations) current_u3_combinations();
        else if (u3_stdout) current_u3_stdout_transport(); else current_three_input_laboratory();
    } catch (const std::exception& error) { std::cerr << "FAIL current input laboratory: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich);
    OleUninitialize(); return result;
}

#include "core/lesson.hpp"
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
#include <algorithm>
#include <array>
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
    check(found.has_value(), "A published U2L9 laboratory identity is missing");
    return *found;
}

struct View {
    WindowsEnvironment environment;
    const curriculum::Lesson& lesson;
    std::size_t block;
    HWND host = nullptr, view = nullptr;
    std::size_t executions = 0;

    View(const curriculum::Lesson& model, curriculum::StatePosition target)
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
    void run(std::string_view source, std::string_view input, std::string_view expected) {
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
        check(bytes(directory / L"execution-stdout.bin") == expected && text(14) == expected
            && bytes(directory / L"execution-stderr.bin").empty(),
            "The actual program's binary output or visible console differs from the exact requested output");
        check(!IsWindowVisible(host), "A laboratory execution exposed the QA host");
        ++executions;
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
}

int main() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto apartment = OleInitialize(nullptr);
    if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll");
    int result = 0;
    try {
        check(rich != nullptr, "Cannot load RichEdit for the current input laboratory");
        reader_theme::prepare_fonts(); current_three_input_laboratory();
    } catch (const std::exception& error) { std::cerr << "FAIL current input laboratory: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich);
    OleUninitialize(); return result;
}

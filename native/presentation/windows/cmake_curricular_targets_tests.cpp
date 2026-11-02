#include "core/attempts.hpp"
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
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
using winrt::Windows::Data::Json::JsonObject;
std::string phase = "initialization";
void require(bool condition, std::string_view reason) {
    if (!condition) throw std::runtime_error(phase + ": " + std::string(reason));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void await(const std::function<bool()>& done, std::string_view reason) {
    const auto deadline = GetTickCount64() + 90000;
    do {
        pump(); if (done()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error(phase + ": " + std::string(reason));
}
HWND child(HWND parent, int id) {
    struct Search { int id; HWND found = nullptr; } search{id};
    EnumChildWindows(parent, [](HWND window, LPARAM raw) -> BOOL {
        auto& query = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(window) != query.id) return TRUE;
        query.found = window; return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.found;
}
std::string caption(HWND window) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    text.resize(static_cast<std::size_t>(GetWindowTextW(window, text.data(), static_cast<int>(text.size()))));
    return utf8(text);
}
std::string bytes(const std::filesystem::path& path) {
    const auto value = state::read_bytes(path);
    return {value.begin(), value.end()};
}
std::string windows_newlines(std::string_view text) {
    std::string result;
    for (const auto value : text) { if (value == '\n') result += '\r'; result += value; }
    return result;
}
void require_physical_directory(const std::filesystem::path& directory) {
    state::require_safe_path(directory);
    const auto handle = CreateFileW(state::native_io_path(directory).c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    require(handle != INVALID_HANDLE_VALUE, "Cannot inspect the actual QA build directory");
    try { state::require_unredirected_handle(handle, directory); }
    catch (...) { CloseHandle(handle); throw; }
    CloseHandle(handle);
}

// Literal oracles reviewed by Work, not inferred from the program under test.
// Original target-dependent-p2.json SHA-256:
// 90d05d9123059be6fcabb3458f9495ba99a7fd807a5a86d40435e15c44094e3e.
// That historical Android evidence is provenance only; this executable never
// reads an Android checkout, its .gradle directory or a previous console log.
constexpr std::string_view boxes_output =
    "piezas=0 esperado=0 obtenido=0 OK\n"
    "piezas=4 esperado=1 obtenido=1 OK\n"
    "piezas=8 esperado=2 obtenido=2 OK\n"
    "piezas=20 esperado=5 obtenido=5 OK\n"
    "piezas=1 esperado=1 obtenido=1 OK\n"
    "piezas=5 esperado=2 obtenido=2 OK\n"
    "piezas=17 esperado=5 obtenido=5 OK\nBateria: OK\n";
constexpr std::string_view lots_output =
    "piezas=0 esperado=0 obtenido=0 OK\n"
    "piezas=6 esperado=1 obtenido=1 OK\n"
    "piezas=7 esperado=2 obtenido=2 OK\n"
    "piezas=18 esperado=3 obtenido=3 OK\nBateria: OK\n";
constexpr std::string_view client_output = "1:2";
struct Reference { std::string_view path; std::size_t page, block; };
struct Location {
    std::string_view name, authored_target;
    std::size_t page, block;
    std::array<Reference, 2> references;
};
constexpr std::array<Location, 7> locations{{
    {"datos_y_listas_de_objetivos", "comprobar_cajas", 4, 5, {{{"CMakeLists.txt", 5, 2}, {}}}},
    {"datos_y_listas_de_objetivos", "comprobar_cajas", 5, 8, {{{"CMakeLists.txt", 5, 2}, {}}}},
    {"datos_y_listas_de_objetivos", "comprobar_cajas", 7, 5, {{{"CMakeLists.txt", 8, 2}, {}}}},
    {"datos_y_listas_de_objetivos", "verificar_lotes", 9, 5, {{{"CMakeLists.txt", 10, 2}, {}}}},
    {"reorganizar_la_descripcion_conservando_el_comprobador", "consultar_cajas", 4, 5,
        {{{"CMakeLists.txt", 5, 5}, {"componente/CMakeLists.txt", 5, 7}}}},
    {"reorganizar_la_descripcion_conservando_el_comprobador", "comprobar_cajas", 7, 4,
        {{{"componente/CMakeLists.txt", 8, 4}, {}}}},
    {"reorganizar_la_descripcion_conservando_el_comprobador", "verificar_lotes", 9, 4,
        {{{"CMakeLists.txt", 10, 5}, {"codigo/lotes/CMakeLists.txt", 10, 7}}}}
}};

struct Fixture {
    const Location& location;
    curriculum::Lesson lesson;
    std::filesystem::path asset;
    std::vector<std::uint8_t> asset_before;
    WindowsEnvironment environment;
    curriculum::CppProjectFiles files;
    std::string target, selected, battery, client, battery_source, client_source, draft_key;
    std::string expected_output;
    state::Preferences unrelated, reserve;
    HWND host = nullptr, view = nullptr;
    std::size_t executions = 0;

    explicit Fixture(std::size_t index) : location(locations.at(index)) {
        phase = "location " + std::to_string(index + 1) + " load actual asset";
        asset = std::filesystem::path(build::source_root) /
            "app/src/main/assets/content/es/programming/programming_practice_tools/comprobaciones_repetibles" /
            ("lesson_" + std::string(location.name) + ".json");
        asset_before = state::read_bytes(asset); lesson = windows::load_lesson(asset);
        auto& activity = lesson.pages.at(location.page - 1).blocks.at(location.block - 1);
        require(lesson.id == "programming_practice_tools__comprobaciones_repetibles__" + std::string(location.name)
            && activity.type == curriculum::BlockType::cpp_compiler && activity.cmake
            && activity.cmake->target == location.authored_target && activity.cmake->build_type == "Debug"
            && activity.cmake->options.empty() && !activity.output_check && activity.initial_code.empty()
            && activity.project_files.size() == 6 && !lesson.compiler_puzzles_required
            && curriculum::activity_prerequisites(lesson, activity).empty()
            && activity.standard_input.value_or("").empty(),
            "The published independent CMake project, identity or authored configuration changed");
        const bool lots = location.page == 9;
        battery = lots ? "verificar_lotes" : "comprobar_cajas";
        client = lots ? "consultar_lotes" : "consultar_cajas";
        battery_source = lots ? "pruebas/lotes_casos.cpp" : "pruebas/cajas_casos.cpp";
        client_source = lots ? "programa/consulta.cpp" : "app/consulta.cpp";
        expected_output = lots ? lots_output : boxes_output;
        files = activity.project_files; selected = files.front().first; target = activity.cmake->target;
        require(selected == "CMakeLists.txt" && source(battery_source).find("Bateria: OK") != std::string::npos
            && source(client_source).find("<< \":\" <<") != std::string::npos,
            "The original independent battery/client source was replaced");
        // This contract is QA-only. No published JSON is edited or credited.
        curriculum::OutputCase battery_case, client_case;
        battery_case.executable = battery; battery_case.outputs = {expected_output};
        client_case.executable = client; client_case.outputs = {std::string(client_output)};
        activity.output_check = curriculum::OutputCheck{curriculum::TextComparison::exact, {battery_case, client_case}};
        draft_key = curriculum::draft_key("programming", lesson, activity);

        const auto base = std::filesystem::path(build::source_root) / "out/qa/cmake-curricular-targets";
        require(base.is_absolute() && (base.root_name() == L"E:" || base.root_name() == L"e:"),
            "CMake curricular QA requires the absolute project QA directory on E:");
        state::require_safe_path(base);
        const auto token = L"cmake-target-" + std::to_wstring(index + 1) + L"-"
            + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        environment.options = {ProfileId::Codex, token, false};
        environment.paths = make_profile_paths(base, ProfileId::Codex, token);
        require(!std::filesystem::exists(environment.paths.root), "Refusing to reuse an existing QA profile");
        // CMakeScratch/TryCompile and MSVC descendants need a short physical
        // address. State and copied run evidence stay on E:; only regenerable
        // compiler workspaces use the already approved physical build root.
        const std::filesystem::path project_base = L"C:/LGP-ToolchainBuilds/LGAED-CMakeTargets-QA";
        state::require_safe_path(project_base);
        environment.paths.projects = project_base / token;
        state::require_safe_path(environment.paths.projects);
        require(environment.paths.projects.is_absolute()
            && std::filesystem::weakly_canonical(environment.paths.projects).parent_path()
                == std::filesystem::weakly_canonical(project_base)
            && environment.paths.projects.filename() == token && !std::filesystem::exists(environment.paths.projects),
            "Refusing to reuse a workspace or escape the approved short QA build directory");
        initialize_profile_directories(environment.paths);
        require_physical_directory(environment.paths.projects);
        state::StateData initial; state::prepare_lesson_state(initial, lesson);
        // Position only: this fixture does not assert navigation through, or
        // invent responses to, earlier curricular quizzes.
        state::save_current_page(initial, lesson, location.page - 1);
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences["cmake_targets_unrelated"]["preserve"] = std::string(" \t QA unchanged\r\n");
        auto& finite = initial.preferences["attempt_reserve_seconds_test"];
        finite["reserve_programming"] = 3.; finite["recovery_interval_programming"] = attempts::recovery_millis;
        finite["updated_at_programming"] = static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        unrelated = initial.preferences.at("cmake_targets_unrelated"); reserve = finite;
        state::StateStore store(environment.paths.root); (void)store.load(); store.save(initial);
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"CMake curricular targets QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create the isolated hidden QA host"); open();
    }
    ~Fixture() {
        if (view) { (void)flush_lesson_view(view); DestroyWindow(view); }
        if (host) DestroyWindow(host);
        // Retain this unique fixture's state, binary streams and CMake artifacts
        // for diagnosis. Never delete or reuse another run's generated data.
    }
    const curriculum::Block& block() const { return lesson.pages.at(location.page - 1).blocks.at(location.block - 1); }
    std::string source(std::string_view path) const {
        const auto found = std::ranges::find(files, path, [](const auto& file) -> const std::string& { return file.first; });
        require(found != files.end(), "A required original project file disappeared"); return found->second;
    }
    HWND control(int part) const {
        const auto id = 1000 + static_cast<int>(location.block - 1) * 100 + part;
        const auto window = child(view, id); require(window != nullptr, "Missing actual lesson control " + std::to_string(id));
        return window;
    }
    HWND config(int id) const { const auto window = child(control(6), id); require(window != nullptr, "Missing CMake configuration control"); return window; }
    std::string text(int part) const { return edit_text(control(part)); }
    void open() {
        view = create_lesson_view(host, environment, lesson);
        require(view != nullptr, "Cannot create the actual curricular CMake view");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        require(lesson_page_state(view).index == location.page - 1 && !IsWindowVisible(host),
            "The fixture changed its focal page or exposed a desktop window");
    }
    state::StateData saved() {
        require(flush_lesson_view(view), "Cannot flush isolated CMake state");
        state::StateStore store(environment.paths.root); return store.load();
    }
    void preserved(std::size_t cases, bool completed) {
        const auto data = saved();
        require(data.preferences.at("cmake_targets_unrelated") == unrelated
            && data.preferences.at("attempt_reserve_seconds_test") == reserve,
            "A compiler comparison changed unrelated preferences or finite hearts");
        require(windows::compiler_puzzle_case_count(data, lesson, block()) == cases
            && windows::compiler_puzzle_completed(data, lesson, block()) == completed,
            "The actual objective recorded the wrong number of target-bound cases");
        for (const auto& page : lesson.pages) for (const auto& other : page.blocks) {
            require(!state::get_response(data, lesson, other), "CMake QA invented a quiz response");
            if (other.type == curriculum::BlockType::cpp_compiler && other.stable_id != block().stable_id)
                require(!windows::compiler_puzzle_completed(data, lesson, other)
                    && windows::compiler_puzzle_case_count(data, lesson, other) == 0,
                    "The focal execution credited another curricular project");
        }
        require(state::read_bytes(asset) == asset_before, "The curricular asset changed during technical QA");
        const auto project = JsonObject::Parse(winrt::to_hstring(state::string_value(data, "cpp_lesson_drafts", draft_key + "/project")));
        const auto entries = project.GetNamedArray(L"files");
        require(entries.Size() == files.size() && winrt::to_string(project.GetNamedString(L"selected")) == selected,
            "The original project file order or selected source was not saved");
        for (std::uint32_t index = 0; index < entries.Size(); ++index) {
            const auto item = entries.GetObjectAt(index);
            require(winrt::to_string(item.GetNamedString(L"path")) == files[index].first
                && winrt::to_string(item.GetNamedString(L"code")) == files[index].second,
                "A project draft lost exact bytes or changed an untouched source");
        }
        const auto configuration = JsonObject::Parse(winrt::to_hstring(state::string_value(data, "cpp_lesson_drafts", draft_key + "/cmake")));
        require(winrt::to_string(configuration.GetNamedString(L"target")) == target
            && winrt::to_string(configuration.GetNamedString(L"optionsText")).empty()
            && winrt::to_string(configuration.GetNamedString(L"buildType")) == "Debug"
            && edit_text(config(400)) == target && edit_text(config(402)).empty()
            && caption(config(404)) == "Debug" && state::get_draft(data, draft_key, "").input.empty(),
            "CMake target, options, build type or exact empty input was not preserved");
    }
    void edit(HWND window, std::string_view value) {
        require(IsWindowEnabled(window), "A current editable project control is unexpectedly disabled");
        CHARRANGE all{0, -1}; SendMessageW(window, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto native = wide(value);
        SendMessageW(window, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(native.c_str()));
        require(edit_text(window) == value, "The actual editor changed supplied source characters");
    }
    void select_file(std::string_view path) {
        const auto found = std::ranges::find(files, path, [](const auto& file) -> const std::string& { return file.first; });
        require(found != files.end(), "Cannot select an unknown project file");
        const auto tab = child(control(5), 1000 + static_cast<int>(found - files.begin()));
        require(tab && IsWindowEnabled(tab), "The original project file has no enabled real tab");
        SendMessageW(tab, BM_CLICK, 0, 0); selected = path;
        require(text(10) == found->second, "Selecting an original file showed another source");
    }
    void edit_file(std::string_view path, std::string_view value, bool dispatch = true) {
        select_file(path); edit(control(10), value);
        const auto found = std::ranges::find(files, path, [](const auto& file) -> const std::string& { return file.first; });
        found->second = value; if (dispatch) pump();
    }
    void select_target(std::string_view value) { edit(config(400), value); target = value; pump(); }
    void references() {
        for (const auto& reference : location.references) if (!reference.path.empty()) {
            const auto& published = lesson.pages.at(reference.page - 1).blocks.at(reference.block - 1);
            require(published.type == curriculum::BlockType::code && published.language == "cmake" && !published.code.empty(),
                "The reviewed published CMake reference is no longer a code block");
            edit_file(reference.path, published.code);
        }
    }
    windows::CppRunRequest request() const {
        windows::CppRunRequest result; result.project_files = files; result.cmake = block().cmake;
        result.cmake->target = target; result.projects_directory = environment.paths.projects; result.project_key = draft_key;
        return result;
    }
    std::set<std::filesystem::path> runs() const {
        std::set<std::filesystem::path> result;
        const auto directory = environment.paths.projects / L"cpp-runs";
        if (std::filesystem::exists(directory)) for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.is_directory()) result.insert(entry.path());
        return result;
    }
    std::filesystem::path preserve_capture(const std::filesystem::path& directory) const {
        require(directory.parent_path() == environment.paths.projects / L"cpp-runs"
            && directory.filename().wstring().starts_with(L"run-"), "Refusing to copy a capture outside this fixture's own runs");
        state::require_safe_path(directory);
        const auto parent = environment.paths.logs / L"native-runs";
        const auto destination = parent / directory.filename(); state::require_safe_path(destination);
        std::filesystem::create_directories(state::native_io_path(parent));
        require(std::filesystem::create_directory(state::native_io_path(destination)), "Refusing to overwrite a previous native capture");
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            state::require_safe_path(entry.path());
            require(entry.is_regular_file(), "An unexpected directory appeared in this run's redirected stream capture");
            const auto copy = destination / entry.path().filename();
            require(std::filesystem::copy_file(state::native_io_path(entry.path()), state::native_io_path(copy))
                && state::read_bytes(copy) == state::read_bytes(entry.path()), "The durable E: capture differs from actual native run bytes");
        }
        return destination;
    }
    std::filesystem::path execute(bool success) {
        const auto before = runs(); const auto run = control(11);
        require(IsWindowEnabled(run) && caption(run) == "Compilar, ejecutar y comprobar", "No enabled combined CMake action");
        SendMessageW(run, BM_CLICK, 0, 0);
        require(!IsWindowEnabled(run), "The real CMake action never entered its pending state");
        await([&] { return IsWindowEnabled(run); }, "The native CMake action never completed");
        const auto after = runs(); std::vector<std::filesystem::path> created;
        for (const auto& entry : after) if (!before.contains(entry)) created.push_back(entry);
        require(created.size() == 1, "An actual CMake action did not create exactly one fresh run capture");
        require(bytes(created.front() / L"stdin.bin").empty(), "The fresh executable received invented standard input");
        const auto toolchain = bytes(created.front() / L"toolchain.txt");
        require(toolchain.find("compiler=" + utf8((std::filesystem::path(build::educational_vc_root) / L"bin/Hostx64/x64/cl.exe").wstring()) + '\n') != std::string::npos,
            "The execution did not use the current configured native MSVC toolchain");
        if (success) {
            require(text(34) == "Ejecución terminada (código 0)." || text(34) == "Caso correcto. Casos comprobados: 1 de 2.",
                "The actual program did not terminate successfully: " + text(34) + "\n" + text(14));
            const auto availability = windows::inspect_cpp_project(request());
            require(availability.configured && availability.built && availability.target_type == "EXECUTABLE"
                && availability.artifact.filename() == wide(target + ".exe"),
                "The selected exact File API target is not a current built native executable");
            DWORD binary_type = 0;
            require(GetBinaryTypeW(availability.artifact.c_str(), &binary_type) && binary_type == SCS_64BIT_BINARY,
                "CMake did not create an actual Windows x64 executable");
            for (const auto& [path, code] : files)
                require(bytes(availability.workspace / L"p" / wide(path)) == code,
                    "CMake compiled different project sources than the current captured draft");
        }
        ++executions; return preserve_capture(created.front());
    }
    void output(std::string_view expected, curriculum::OutputVerdict verdict) {
        const auto directory = execute(true);
        const auto stdout_bytes = bytes(directory / L"execution-stdout.bin");
        const auto stderr_bytes = bytes(directory / L"execution-stderr.bin");
        require(stdout_bytes == windows_newlines(expected) && stderr_bytes.empty() && text(14) == expected,
            "The current native executable stdout/stderr differs from the independent literal oracle");
        // Evaluate these actual fresh captured bytes independently as well as
        // checking the UI/receipt. A completed achievement is sticky and must
        // not be confused with a fresh verdict for a subsequently edited goal.
        curriculum::FreshExecution actual;
        actual.source_revision = executions; actual.compilation_succeeded = true; actual.execution_succeeded = true;
        actual.exit_code = 0; actual.executable = target; actual.standard_output = stdout_bytes; actual.standard_error = stderr_bytes;
        require(windows::evaluate_cpp_output_check(*block().output_check, actual, executions).verdict == verdict,
            "Exact current target/output matching disagrees with its fresh captured program");
        require(windows::evaluate_cpp_output_check(*block().output_check, actual, executions + 1).verdict == curriculum::OutputVerdict::unavailable,
            "An obsolete source revision was given an educational verdict");
        std::cout << "native_run=" << executions << " target=" << target << " stdout_bytes=" << stdout_bytes.size()
            << " stderr_bytes=0 exit=0 fresh_verdict=" << static_cast<int>(verdict) << '\n';
    }
    void reopen(std::size_t cases, bool completed) {
        preserved(cases, completed); const auto before = saved(); const auto previous_runs = runs();
        const auto project = state::string_value(before, "cpp_lesson_drafts", draft_key + "/project");
        const auto configuration = state::string_value(before, "cpp_lesson_drafts", draft_key + "/cmake");
        require(flush_lesson_view(view), "Cannot flush before closing the focal CMake page");
        DestroyWindow(view); view = nullptr; open(); preserved(cases, completed);
        const auto after = saved();
        require(state::string_value(after, "cpp_lesson_drafts", draft_key + "/project") == project
            && state::string_value(after, "cpp_lesson_drafts", draft_key + "/cmake") == configuration
            && text(10) == source(selected) && runs() == previous_runs && text(14).empty(),
            "Reopening changed exact drafts/configuration or resurrected an old execution console");
        require(completed ? text(4).find("Correcto") != std::string::npos : text(4).empty(),
            "Reopening resurrected an uncompleted verdict or discarded a completed sticky achievement");
    }
    void reset() {
        const auto button = control(12); require(IsWindowEnabled(button), "Explicit exercise Reset is unavailable");
        SendMessageW(button, BM_CLICK, 0, 0); files = block().project_files;
        selected = files.front().first; target = block().cmake->target; pump();
        preserved(0, false); require(text(4).empty(), "Explicit Reset retained the old achievement feedback");
    }
};

void contrasts(Fixture& fixture) {
    phase = "location 1 completed achievement is distinct from a fresh edited verdict";
    const auto complete = windows::compiler_puzzle_feedback(fixture.saved(), fixture.lesson, fixture.block());
    const auto original_battery = fixture.source(fixture.battery_source);
    fixture.edit_file(fixture.battery_source, fixture.source(fixture.client_source));
    fixture.select_target(fixture.battery);
    fixture.output(client_output, curriculum::OutputVerdict::incorrect); fixture.preserved(2, true);
    require(windows::compiler_puzzle_feedback(fixture.saved(), fixture.lesson, fixture.block()) == complete
        && fixture.text(4).find("Correcto") != std::string::npos,
        "A fresh mismatch silently revoked or rewrote an already completed objective");
    fixture.reopen(2, true);

    phase = "location 1 actual executable without an authored target case";
    fixture.reset(); fixture.references();
    const auto root = fixture.source("CMakeLists.txt");
    fixture.edit_file("CMakeLists.txt", root + "\nadd_executable(qa_sin_caso app/consulta.cpp)\n"
        "target_link_libraries(qa_sin_caso PRIVATE envios)\n"
        "target_compile_features(qa_sin_caso PRIVATE cxx_std_20)\n");
    fixture.select_target("qa_sin_caso");
    fixture.output(client_output, curriculum::OutputVerdict::unavailable); fixture.preserved(0, false);
    require(fixture.text(4).empty(), "An unlisted executable borrowed the matching output of another target");

    phase = "location 1 real build diagnostic is not an incorrect answer";
    fixture.edit_file("CMakeLists.txt", root); fixture.select_target(fixture.battery);
    fixture.edit_file(fixture.battery_source, "#error QA_CMAKE_TARGET_BUILD_FAILURE\n" + original_battery);
    const auto failure = fixture.execute(false);
    const auto diagnostics = bytes(failure / L"build-stdout.bin") + bytes(failure / L"build-stderr.bin");
    require(diagnostics.find("QA_CMAKE_TARGET_BUILD_FAILURE") != std::string::npos
        && fixture.text(34).find("la fase terminó con código ") != std::string::npos
        && fixture.text(4).empty() && !std::filesystem::exists(failure / L"execution-stdout.bin"),
        "A failed native build was executed, suppressed its diagnostic or produced an educational rejection");
    fixture.preserved(0, false); fixture.edit_file(fixture.battery_source, original_battery);

    phase = "location 1 stale in-flight result cannot accredit newly edited source";
    require(!windows::inspect_cpp_project(fixture.request()).built,
        "The failed build unexpectedly left a current artifact for the stale-result contrast");
    const auto before_stale = fixture.runs();
    const auto run = fixture.control(11); require(IsWindowEnabled(run), "Cannot start the stale-result contrast");
    SendMessageW(run, BM_CLICK, 0, 0);
    require(!IsWindowEnabled(run), "Stale-result contrast did not start a real native request");
    // Hold only this test's UI completion queue until a real old executable
    // has finished. inspect_cpp_project cannot acquire the worker's workspace
    // mutex during the run; the completed stream is independently checked too.
    // The subsequent edit therefore rejects a genuine late successful result,
    // rather than merely stopping a request before any child process starts.
    std::filesystem::path stale_capture;
    const auto deadline = GetTickCount64() + 90000;
    do {
        if (windows::inspect_cpp_project(fixture.request()).built)
            for (const auto& directory : fixture.runs()) if (!before_stale.contains(directory)
                && std::filesystem::is_regular_file(directory / L"execution-stdout.bin")
                && bytes(directory / L"execution-stdout.bin") == windows_newlines(fixture.expected_output)
                && bytes(directory / L"execution-stderr.bin").empty()) stale_capture = directory;
        if (!stale_capture.empty()) break;
        Sleep(5);
    } while (GetTickCount64() < deadline);
    require(!stale_capture.empty() && !IsWindowEnabled(run),
        "The old native program did not finish before its queued UI result was deliberately invalidated");
    fixture.edit_file(fixture.battery_source, original_battery + "\n// QA newer draft, not the running capture\n", false);
    await([&] { return IsWindowEnabled(run); }, "The obsolete native request did not finish/cancel");
    (void)fixture.preserve_capture(stale_capture); ++fixture.executions;
    fixture.preserved(0, false);
    require(fixture.text(4).empty(), "A late result accredited a different current source revision");
    fixture.edit_file(fixture.battery_source, original_battery); fixture.preserved(0, false);
    fixture.output(fixture.expected_output, curriculum::OutputVerdict::correct); fixture.preserved(1, false);
    require(fixture.text(4).empty(), "Returning to earlier source resurrected the old complete achievement");
}

void run_location(std::size_t index) {
    Fixture fixture(index);
    phase = "location " + std::to_string(index + 1) + " published CMake references and initial uncredited state";
    fixture.preserved(0, false); fixture.references(); fixture.select_target(fixture.battery);
    const auto original_battery = fixture.source(fixture.battery_source);
    const auto original_client = fixture.source(fixture.client_source);

    phase = "location " + std::to_string(index + 1) + " operation 1 real correct battery";
    fixture.output(fixture.expected_output, curriculum::OutputVerdict::correct); fixture.preserved(1, false);
    require(fixture.text(4).empty(), "One target alone completed the two-target objective");

    phase = "location " + std::to_string(index + 1) + " operation 2 battery prints client output";
    fixture.edit_file(fixture.battery_source, original_client); fixture.preserved(0, false);
    fixture.output(client_output, curriculum::OutputVerdict::incorrect); fixture.preserved(0, false);
    require(fixture.text(4).find("Incorrecto") != std::string::npos,
        "The selected battery borrowed the different client's expected output");
    // Each location reopens after an actual rejection. No receipt, verdict or
    // console from that prior executable may become a fresh result on reopen.
    fixture.reopen(0, false);

    phase = "location " + std::to_string(index + 1) + " operation 3 restore exact battery and execute afresh";
    fixture.edit_file(fixture.battery_source, original_battery); fixture.preserved(0, false);
    fixture.output(fixture.expected_output, curriculum::OutputVerdict::correct); fixture.preserved(1, false);

    phase = "location " + std::to_string(index + 1) + " operation 4 real correct client";
    fixture.select_target(fixture.client); fixture.preserved(1, false);
    require(fixture.text(4).empty(), "Selecting a different executable retained an unavailable old verdict");
    fixture.output(client_output, curriculum::OutputVerdict::correct); fixture.preserved(2, true);
    require(fixture.text(4).find("Correcto") != std::string::npos,
        "Both real target-bound executions did not complete the objective");
    require(fixture.executions == 4, "The seven-location matrix no longer performs exactly four main operations per location");
    if (index == 0) contrasts(fixture);
    require(state::read_bytes(fixture.asset) == fixture.asset_before, "The source curricular JSON was modified");
    std::cout << "PASS location=" << index + 1 << " lesson=" << fixture.lesson.id
        << " page=" << fixture.location.page << " block=" << fixture.location.block
        << " four_main_native_operations=4 original_asset_unchanged=1 qa_root="
        << utf8(fixture.environment.paths.root.native()) << '\n';
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    if (argc != 2 || std::string_view(argv[1]).size() != 1 || argv[1][0] < '1' || argv[1][0] > '7') {
        std::cerr << "Usage: cmake_curricular_targets_tests <location 1-7>\n"; return 2;
    }
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try {
        require(rich != nullptr, "Cannot load the actual RichEdit project controls");
        reader_theme::prepare_fonts(); run_location(static_cast<std::size_t>(argv[1][0] - '1'));
    } catch (const std::exception& error) { std::cerr << "FAIL CMake curricular targets: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

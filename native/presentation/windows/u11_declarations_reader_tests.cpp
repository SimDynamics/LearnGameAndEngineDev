#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/u11_assignment_entry_fixture.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <exception>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr std::string_view expected_output =
    "Caso 1\nValor: 4\nValor: 12\nError: Nombre ya definido\nValor: 4\nAceptadas: 3\nRechazadas: 1\nTabla: base=4\n"
    "Caso 2\nValor: 0\nValor: 0\nAceptadas: 2\nRechazadas: 0\nTabla: a=0\n"
    "Caso 3\nValor: -100\nValor: -100\nAceptadas: 2\nRechazadas: 0\nTabla: a=-100\n"
    "Caso 4\nValor: 100\nValor: 100\nAceptadas: 2\nRechazadas: 0\nTabla: a=100\n"
    "Caso 5\nError: Numero fuera de dominio\nError: Nombre no definido\nAceptadas: 0\nRechazadas: 2\nTabla:\n"
    "Caso 6\nError: Nombre no definido\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 1\nTabla: a=2\n"
    "Caso 7\nError: Se esperaba ;\nAceptadas: 0\nRechazadas: 1\nTabla:\n"
    "Caso 8\nValor: 3\nValor: -3\nValor: 3\nValor: -3\nAceptadas: 4\nRechazadas: 0\nTabla: dato=3 dato2=-3\n";
constexpr std::string_view prediction_answer =
    "Error: Se esperaba un nombre\nError: Se esperaba =\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 2\nTabla: salto=2";
static_assert(expected_output.size() == 650 && prediction_answer.size() == 109);
constexpr std::string_view assignment_output =
    "Caso 1\nValor: 7\nValor: 9\nValor: 19\nError: Se esperaba ;\nValor: 9\nAceptadas: 4\nRechazadas: 1\nTabla: base=9\n"
    "Caso 2\nValor: 4\nError: Divisor cero\nValor: 4\nAceptadas: 2\nRechazadas: 1\nTabla: a=4\n"
    "Caso 3\nValor: 4\nError: Resultado fuera de dominio\nValor: 4\nAceptadas: 2\nRechazadas: 1\nTabla: a=4\n"
    "Caso 4\nError: Nombre no definido\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 1\nTabla: a=2\n"
    "Caso 5\nValor: 5\nValor: 2\nError: Se esperaba ;\nValor: 5\nValor: 2\nAceptadas: 4\nRechazadas: 1\nTabla: a=5 b=2\n"
    "Caso 6\nValor: 5\nError: Se esperaba ;\nAceptadas: 1\nRechazadas: 1\nTabla: a=5\n"
    "Caso 7\nValor: 6\nValor: 6\nAceptadas: 2\nRechazadas: 0\nTabla: a=6\n"
    "Caso 8\nValor: 5\nError: Nombre no definido\nValor: 6\nAceptadas: 2\nRechazadas: 1\nTabla: paso=5\n";
constexpr std::string_view assignment_answer =
    "Valor: 6\nValor: 2\nError: Se esperaba ;\nValor: 6\nValor: 2\nAceptadas: 4\nRechazadas: 1\nTabla: a=6 b=2";
static_assert(assignment_output.size() == 711 && assignment_answer.size() == 98);
constexpr std::string_view constants_output =
    "Caso 1\nValor: 4\nValor: 6\nValor: 9\nValor: 6\nError: Nombre constante\nValor: 100\nAceptadas: 5\nRechazadas: 1\nTabla: unidad=1(C) limite=100(C) base=9(V) copia=6(C)\n"
    "Caso 2\nError: Se esperaba ;\nError: Nombre no definido\nAceptadas: 0\nRechazadas: 2\nTabla: unidad=1(C) limite=100(C)\n"
    "Caso 3\nValor: 1\nError: Nombre constante\nValor: 1\nAceptadas: 2\nRechazadas: 1\nTabla: unidad=1(C) limite=100(C) x=1(C)\n"
    "Caso 4\nError: Nombre ya definido\nValor: 1\nAceptadas: 1\nRechazadas: 1\nTabla: unidad=1(C) limite=100(C)\n"
    "Caso 5\nError: Resultado fuera de dominio\nError: Nombre no definido\nAceptadas: 0\nRechazadas: 2\nTabla: unidad=1(C) limite=100(C)\n"
    "Caso 6\nValor: 1\nValor: 2\nValor: 3\nValor: 4\nValor: 5\nValor: 6\nError: Tabla llena\nValor: 1\nAceptadas: 7\nRechazadas: 1\nTabla: unidad=1(C) limite=100(C) a=1(V) b=2(V) c=3(V) d=4(V) e=5(V) f=6(V)\n";
constexpr std::string_view constants_answer =
    "Valor: 3\nValor: 4\nValor: 6\nValor: 4\nError: Nombre constante\nValor: 6\nAceptadas: 5\nRechazadas: 1\nTabla: unidad=1(C) limite=100(C) fija2=6(V) Fija=4(C)";
static_assert(constants_output.size() == 809 && constants_answer.size() == 149);

struct ReaderSpec {
    std::string_view label, token, folder, preference, filename, asset_lf_sha, asset_crlf_sha;
    std::string_view compiler, prediction, reference, following;
    std::size_t compiler_page, prediction_page, reference_page, initial_bytes, reference_bytes;
    std::string_view output, answer, wrong, hidden_first, hidden_second;
    bool following_note;
};
const ReaderSpec declarations{
    "U11L19", "declarations-reader", "u11-declarations-reader", "u11_declarations_reader_qa",
    "lesson_declaraciones_aceptadas_solo_al_completar.json",
    "bfaa88c3f4294c30bf50b58f96aff91fbf71ffdbdfd20c7471d1c71e358c1cb6",
    "fb5275cf607976cfe5d7e8aeb0ddf940b4253956f1ac2e1453ba499fee5f2c8c",
    "block_0004_0002", "work_u11l19_nombre_y_igual", "block_0007_0001", "block_0005_0001",
    4, 5, 7, 195, 11892, expected_output, prediction_answer,
    "Error: Se esperaba =\nError: Se esperaba un nombre\nValor: 2\nValor: 2\nAceptadas: 2\nRechazadas: 2\nTabla: salto=2",
    "Error: Se esperaba un nombre", "Tabla: salto=2", false
};
const ReaderSpec assignments{
    "U11L20", "assignments-reader", "u11-assignment-compiler-reader", "u11_assignments_reader_qa",
    "lesson_asignaciones_con_anticipacion_limitada.json",
    "69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9",
    "3b550905c93559ecd2506b2f0c1b49039a375c210dd36e12f60964988cf8e8f4",
    "block_0005_0002", "block_0007_0001", "block_0008_0001", "block_0007_0002",
    5, 7, 8, 206, 12442, assignment_output, assignment_answer,
    "Valor: 6\nValor: 2\nError: Se esperaba ;\nValor: 2\nValor: 2\nAceptadas: 4\nRechazadas: 1\nTabla: a=2 b=2",
    {}, {}, true
};
const ReaderSpec constants{
    "U11L21", "constants-reader", "u11-constants-compiler-reader", "u11_constants_reader_qa",
    "lesson_constantes_del_lenguaje_y_nombres_predefinidos.json",
    "d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db",
    "47277feda13e97b2d4862886755e6598483a813c4c8247d978e007d2a370db84",
    "block_0005_0002", "work_u11l21_nombres_completos", "block_0007_0001", "block_0006_0001",
    5, 6, 7, 218, 12921, constants_output, constants_answer,
    "Valor: 3\nValor: 4\nValor: 6\nValor: 4\nValor: 2\nValor: 6\nAceptadas: 6\nRechazadas: 0\nTabla: unidad=1(C) limite=100(C) fija2=6(V) Fija=2(C)",
    {}, {}, false
};

bool audited_source(const std::vector<std::uint8_t>& bytes, const ReaderSpec& spec) {
    // Complete Git publications also authenticated by compiler_import_fixture_source.py.
    // U11L21 includes the bibliography/text-semantics revision in 06d31ba73be4b0c7.
    // Admit only the exact LF and CRLF checkout bytes; never normalize or rewrite
    // the supplied source, accept mixed endings, or fall back to an older lesson.
    const auto sha = state::sha256(bytes);
    return sha == spec.asset_lf_sha || sha == spec.asset_crlf_sha;
}

std::string visible(std::string value) {
    std::erase(value, '`'); std::erase(value, '\r'); return value;
}

void require(bool value, std::string_view message) {
    if (!value) throw std::runtime_error(std::string(message));
}
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
void wait(const std::function<bool()>& complete, std::string_view description, ULONGLONG timeout = 120000) {
    const auto deadline = GetTickCount64() + timeout;
    do {
        pump(); if (complete()) return;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 5, QS_ALLINPUT);
    } while (GetTickCount64() < deadline);
    throw std::runtime_error("Timeout: " + std::string(description));
}
HWND find(HWND parent, int identifier) {
    struct Search { int identifier; HWND result = nullptr; } search{identifier};
    EnumChildWindows(parent, [](HWND item, LPARAM raw) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(raw);
        if (GetDlgCtrlID(item) == value.identifier) { value.result = item; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::string bytes(const std::filesystem::path& path) {
    const auto value = state::read_bytes(path); return {value.begin(), value.end()};
}
curriculum::StatePosition position(const curriculum::Lesson& lesson, std::string_view identity) {
    const auto result = curriculum::stable_block_position(lesson, identity);
    require(result.has_value(), "An audited current compiler reader identity is missing"); return *result;
}

std::string printable(std::string_view value, std::size_t maximum = 180) {
    std::string result = "\""; constexpr char hex[] = "0123456789abcdef";
    const auto count = std::min(value.size(), maximum);
    for (std::size_t index = 0; index < count; ++index) {
        const auto byte = static_cast<unsigned char>(value[index]);
        if (byte == '\n') result += "\\n";
        else if (byte == '\r') result += "\\r";
        else if (byte == '\t') result += "\\t";
        else if (byte == '\\' || byte == '"') { result += '\\'; result += static_cast<char>(byte); }
        else if (byte < 32 || byte >= 127) { result += "\\x"; result += hex[byte >> 4]; result += hex[byte & 15]; }
        else result += static_cast<char>(byte);
    }
    if (count < value.size()) result += "...";
    return result + '"';
}
void print_snapshot(std::string_view label, const LessonReaderSnapshot& value) {
    std::cout << "REOPEN " << label << " key=" << printable(value.key) << " index=" << value.index
        << " offset=" << value.offset << " heard=" << value.heard_through << " finished=" << value.finished
        << " playing=" << value.playing << " allocated=" << value.allocated << " planned=" << value.planned
        << " preparing=" << value.preparing << " waiting_gate=" << value.waiting_gate << " parts=" << value.segments.size() << '\n';
    if (value.index < value.segments.size()) {
        const auto& part = value.segments[value.index];
        std::cout << "REOPEN " << label << " selected region=" << printable(part.region) << " block=" << part.block
            << " range=" << part.start << ':' << part.end << " feedback=" << part.feedback << " correct=" << part.correct
            << " available=" << part.available << " duration_ms=" << part.duration_ms << '\n';
    }
}
void print_history(std::string_view label, const narration::History& value) {
    std::cout << "REOPEN " << label << " history entries=" << value.entries.size()
        << " active_key=" << printable(value.active_key) << " active_owner=" << printable(value.active_owner) << '\n';
    for (std::size_t index = 0; index < std::min<std::size_t>(value.entries.size(), 4); ++index) {
        const auto& entry = value.entries[index]; const auto& at = entry.position;
        std::cout << "REOPEN " << label << " entry[" << index << "] key=" << printable(entry.key)
            << " stable=" << printable(entry.stable) << " page=" << printable(entry.page) << " segment=" << at.segment
            << " offset=" << at.milliseconds << " heard=" << at.heard_through << " finished=" << at.finished;
        if (at.heard_prefix) std::cout << " prefix={fingerprint=" << printable(at.heard_prefix->fingerprint)
            << ",microseconds=" << at.heard_prefix->microseconds << ",duration=" << at.heard_prefix->duration_microseconds << '}';
        else std::cout << " prefix=none";
        std::cout << '\n';
    }
    if (value.entries.size() > 4) std::cout << "REOPEN " << label << " remaining entries omitted\n";
}
void diagnose_reopen(const LessonReaderSnapshot& before, const LessonReaderSnapshot& after,
        const narration::History& before_history, const narration::History& after_history) {
    std::cout << "REOPEN equality key=" << (before.key == after.key) << " index=" << (before.index == after.index)
        << " offset=" << (before.offset == after.offset) << " heard=" << (before.heard_through == after.heard_through)
        << " finished=" << (before.finished == after.finished) << " playing=" << (before.playing == after.playing)
        << " allocated=" << (before.allocated == after.allocated) << " history=" << (before_history == after_history) << '\n';
    print_snapshot("before", before); print_snapshot("after", after);
    std::cout << "REOPEN history equality active_key=" << (before_history.active_key == after_history.active_key)
        << " active_owner=" << (before_history.active_owner == after_history.active_owner)
        << " entry_count=" << (before_history.entries.size() == after_history.entries.size()) << '\n';
    const auto common = std::min({before_history.entries.size(), after_history.entries.size(), std::size_t{4}});
    for (std::size_t index = 0; index < common; ++index) {
        const auto& old_entry = before_history.entries[index]; const auto& new_entry = after_history.entries[index];
        if (old_entry == new_entry) continue;
        std::cout << "REOPEN entry[" << index << "] equality key=" << (old_entry.key == new_entry.key)
            << " stable=" << (old_entry.stable == new_entry.stable) << " page=" << (old_entry.page == new_entry.page)
            << " segment=" << (old_entry.position.segment == new_entry.position.segment)
            << " offset=" << (old_entry.position.milliseconds == new_entry.position.milliseconds)
            << " heard=" << (old_entry.position.heard_through == new_entry.position.heard_through)
            << " finished=" << (old_entry.position.finished == new_entry.position.finished)
            << " prefix=" << (old_entry.position.heard_prefix == new_entry.position.heard_prefix) << '\n';
    }
    print_history("before", before_history); print_history("after", after_history);
}

struct Harness {
    const ReaderSpec& spec;
    std::shared_ptr<windows::NarrationWorker> backend;
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path asset;
    std::vector<std::uint8_t> original_asset;
    HWND host = nullptr, view = nullptr;
    std::unique_ptr<LessonReader> reader;
    state::Preferences unrelated;
    test::CompilerReaderEntrySetup entry_setup;
    struct Recording { std::string sha; std::filesystem::file_time_type modified; };
    std::map<std::filesystem::path, Recording> recordings;

    explicit Harness(std::shared_ptr<windows::NarrationWorker> worker, const ReaderSpec& selected,
            const std::filesystem::path& historical_source = {}, std::string_view historical_sha = {},
            std::function<void(state::StateData&, const curriculum::Lesson&)> historical_setup = {})
        : spec(selected), backend(std::move(worker)) {
        require(historical_source.empty() == historical_sha.empty()
            && historical_source.empty() == !static_cast<bool>(historical_setup),
            "A historical reader source needs both its complete hash and explicit isolated entry setup");
        asset = historical_source.empty() ? std::filesystem::path(build::source_root) /
            "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos" / spec.filename
            : historical_source;
        original_asset = state::read_bytes(asset);
        require(historical_source.empty() ? audited_source(original_asset, spec) : state::sha256(original_asset) == historical_sha,
            "The current " + std::string(spec.label) + " snapshot changed; review its exact reader scope before updating this pin");
        lesson = windows::load_lesson(asset);
        const auto token = wide(spec.token) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        environment.options = {ProfileId::Codex, token, false};
        environment.paths = make_profile_paths(std::filesystem::path(build::source_root) / "out/qa" / spec.folder, ProfileId::Codex, token);
        require(!std::filesystem::exists(environment.paths.root), "The current compiler reader requires a new isolated physical QA profile");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root); auto initial = store.load();
        require(initial.preferences.empty(), "The new declarations reader QA already contains student data");
        state::prepare_lesson_state(initial, lesson);
        if(historical_setup)historical_setup(initial,lesson);
        else entry_setup=test::prepare_compiler_reader_entry(initial,lesson,spec.compiler_page);
        auto& preferences = initial.preferences["lesson_narration_settings"];
        preferences["engine"] = std::string("KOKORO"); preferences["language"] = std::string("es");
        preferences["kokoro_voice:es"] = std::string("ef_dora"); preferences["rate"] = 3.;
        preferences["kokoro_advance_generation"] = true;
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences[std::string(spec.preference)]["opaque"] = std::string(" \t conservar\r\n保存");
        unrelated = initial.preferences.at(std::string(spec.preference)); store.save(initial);
        require(narration::read_history(initial).entries.empty(), "The fresh QA contains a fabricated listening history");
        const auto title = L"Hidden current " + wide(spec.label) + L" reader QA";
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", title.c_str(),
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create hidden actual compiler reader host"); open(spec.compiler_page);
    }
    ~Harness() {
        if (std::uncaught_exceptions()) backend->shutdown();
        close(); if (host) DestroyWindow(host);
    }
    HWND control(std::size_t block, int part) const {
        const auto item = find(view, 1000 + static_cast<int>(block) * 100 + part);
        require(item != nullptr, "An actual current compiler reader control is missing"); return item;
    }
    LessonReaderSnapshot snapshot() const {
        const auto value = reader->snapshot(); require(value.error.empty(), "Real " + std::string(spec.label) + " reader error: " + value.error); return value;
    }
    void ready(bool all = true) const {
        try {
            wait([&] {
                const auto value = snapshot();
                return value.planned && !value.segments.empty() && value.index < value.segments.size()
                    && (all ? std::ranges::all_of(value.segments, [](const auto& part) { return part.available; }) : value.segments[value.index].available);
            }, "actual current compiler speech preparation and word alignment");
        } catch (const std::exception& failure) {
            const auto value = reader->snapshot();
            std::cout << "PREPARATION failure " << spec.label << " all=" << all << " reason=" << printable(failure.what())
                << " planned=" << value.planned << " preparing=" << value.preparing
                << " suspended=" << value.preparation_suspended << " index=" << value.index
                << " available=" << std::ranges::count_if(value.segments, [](const auto& part) { return part.available; })
                << '/' << value.segments.size() << " playing=" << value.playing << " allocated=" << value.allocated
                << " finished=" << value.finished << " waiting_gate=" << value.waiting_gate
                << " error=" << printable(value.error) << " key=" << printable(value.key) << '\n';
            std::size_t pending_shown = 0;
            for (std::size_t index = 0; index < value.segments.size(); ++index) {
                const auto& part = value.segments[index];
                if (index != value.index && (part.available || pending_shown >= 6)) continue;
                if (!part.available) ++pending_shown;
                std::cout << "PREPARATION segment=" << index << " block=" << part.block << " region=" << printable(part.region)
                    << " range=" << part.start << ':' << part.end << " available=" << part.available
                    << " feedback=" << part.feedback << " correct=" << part.correct << " duration_ms=" << part.duration_ms
                    << " timings=" << part.timings.size() << " source=" << printable(part.source, part.source.size())
                    << " spoken=" << printable(part.spoken, part.spoken.size()) << '\n';
            }
            throw;
        }
    }
    void open(std::size_t expected_page) {
        require(!view && !reader, "Close the earlier declarations QA view before reopening");
        LessonViewHooks hooks;
        hooks.before_page_change = [&](HWND) { if (reader) reader->pause(); };
        hooks.before_destroy = [&](HWND) { reader.reset(); };
        hooks.content_changed = [&](HWND) { if (reader) reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (reader) reader->feedback(block, correct); };
        hooks.priority_work = [worker = backend](std::stop_token stop) -> std::shared_ptr<void> {
            return std::make_shared<windows::NarrationPriorityLease>(worker->heavy_work_lease(stop));
        };
        view = create_lesson_view(host, environment, lesson, std::move(hooks));
        require(view != nullptr, "Cannot create current native compiler lesson view");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        require(!IsWindowVisible(host) && lesson_page_state(view).index == expected_page, "The hidden QA opened another page or became visible");
        reader = std::make_unique<LessonReader>(view, environment, backend, [] {}); ready(false);
    }
    void close() {
        reader.reset(); if (view) { (void)flush_lesson_view(view); DestroyWindow(view); view = nullptr; }
    }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot flush current isolated declarations state"); return read_lesson_state(view);
    }
    void navigate(std::size_t page) {
        reader->pause();
        while(lesson_page_state(view).index<page){
            const auto next=lesson_page_state(view).index+1;
            require(navigate_lesson_view(view,next),"A sequential compiler-reader page remains locked");ready();
            if(next<page)std::cout<<"QA TRANSIT p"<<next+1<<": admitted arrival only; no playback or human-study claim\n";
        }
        require(navigate_lesson_view(view, page), "The actual current compiler dependent page remains locked"); ready();
    }
    void edit(std::size_t block, int part, std::string_view value) {
        const auto target = control(block, part); require(IsWindowEnabled(target), "The actual declarations QA editor is disabled");
        CHARRANGE all{0, -1}; SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto text = wide(value); SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str())); pump();
        require(edit_text(target) == value, "RichEdit changed the supplied literal program or prediction");
    }
    void click(std::size_t block, int part) {
        const auto target = control(block, part); require(IsWindowEnabled(target), "The actual declarations QA action is disabled");
        SendMessageW(target, BM_CLICK, 0, 0); pump();
    }
    std::filesystem::path compile(std::size_t block, std::string_view source) {
        const auto folder = environment.paths.projects / "cpp-runs"; std::set<std::filesystem::path> old;
        if (std::filesystem::exists(folder)) for (const auto& entry : std::filesystem::directory_iterator(folder)) if (entry.is_directory()) old.insert(entry.path());
        reader->pause(); click(block, 11);
        wait([&] { return IsWindowEnabled(control(block, 11)); }, "the actual current educational compilation and execution");
        reader->pause(); ready();
        std::vector<std::filesystem::path> created;
        for (const auto& entry : std::filesystem::directory_iterator(folder)) if (entry.is_directory() && !old.contains(entry.path())) created.push_back(entry.path());
        require(created.size() == 1, "The declarations fixture did not run one fresh educational program");
        const auto directory = created.front();
        require(bytes(directory / "main.cpp") == source && bytes(directory / "stdin.bin").empty()
            && bytes(directory / "execution-stderr.bin").empty() && edit_text(control(block, 34)) == "Ejecución terminada (código 0).",
            "The actual declarations verdict did not follow its own successful educational run");
        std::cout << "RUN " << utf8(directory.native()) << '\n'; return directory;
    }
    void verify_audio(const LessonReaderSegmentSnapshot& part, bool require_word_timings = true) {
        require(part.available && !part.recording.empty(), "A declarations reader segment has no actual recording");
        const auto data = state::read_bytes(part.recording);
        const auto u16 = [&](std::size_t at) { return unsigned(data[at]) | (unsigned(data[at + 1]) << 8); };
        const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
        const auto tag = [&](std::size_t at, std::string_view value) { return std::equal(value.begin(), value.end(), data.begin() + at); };
        const bool valid_pcm = data.size() > 44 && tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data")
            && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000 && u16(34) == 16 && u32(40) == data.size() - 44
            && (data.size() - 44) % 2 == 0 && std::ranges::any_of(data.begin() + 44, data.end(), [](auto sample) { return sample != 0; })
            && std::abs(part.duration_ms - (data.size() - 44) / 48.) <= 1 && (!require_word_timings || !part.timings.empty());
        if (!valid_pcm) {
            const bool header = data.size() > 44;
            const auto current = reader->snapshot();
            const auto found = std::ranges::find_if(current.segments, [&](const auto& candidate) {
                return candidate.region == part.region && candidate.start == part.start && candidate.end == part.end
                    && candidate.recording == part.recording;
            });
            const auto recording = utf8(part.recording.native());
            std::cout << "PCM failure " << spec.label << " segment=" << (found - current.segments.begin())
                << '/' << current.segments.size() << " selected=" << current.index
                << " block=" << part.block << " region=" << printable(part.region)
                << " range=" << part.start << ':' << part.end << " feedback=" << part.feedback << " correct=" << part.correct
                << " source=" << printable(part.source, part.source.size()) << " spoken=" << printable(part.spoken, part.spoken.size())
                << " recording=" << printable(recording, recording.size()) << '\n';
            std::cout << "PCM predicates size_gt_44=" << header << " riff=" << (header && tag(0, "RIFF"))
                << " wave=" << (header && tag(8, "WAVE")) << " fmt=" << (header && tag(12, "fmt "))
                << " data=" << (header && tag(36, "data")) << " pcm=" << (header && u16(20) == 1)
                << " mono=" << (header && u16(22) == 1) << " rate_24000=" << (header && u32(24) == 24000)
                << " bits_16=" << (header && u16(34) == 16) << " payload_size=" << (header && u32(40) == data.size() - 44)
                << " even_payload=" << (header && (data.size() - 44) % 2 == 0)
                << " non_silent=" << (header && std::ranges::any_of(data.begin() + 44, data.end(), [](auto sample) { return sample != 0; }))
                << " duration_matches=" << (header && std::abs(part.duration_ms - (data.size() - 44) / 48.) <= 1)
                << " required_timings=" << (!require_word_timings || !part.timings.empty())
                << " bytes=" << data.size() << " duration_ms=" << part.duration_ms << " timings=" << part.timings.size() << '\n';
        }
        require(valid_pcm,
            "The narration lacks actual non-silent mono 24 kHz PCM16 and source-bound word timings");
        double previous = 0;
        for (const auto& timing : part.timings) {
            const bool valid_timing = timing.start < timing.end && timing.end <= wide(part.source).size() && timing.start_ms >= previous
                && std::isfinite(timing.start_ms) && std::isfinite(timing.end_ms) && timing.end_ms >= timing.start_ms
                && timing.end_ms <= part.duration_ms + 1;
            if (!valid_timing)
                std::cout << "TIMING failure " << spec.label << " block=" << part.block << " region=" << printable(part.region)
                    << " source=" << printable(part.source, part.source.size()) << " range=" << timing.start << ':' << timing.end
                    << " source_utf16=" << wide(part.source).size() << " start_ms=" << timing.start_ms << " end_ms=" << timing.end_ms
                    << " previous_start_ms=" << previous << " duration_ms=" << part.duration_ms << '\n';
            require(valid_timing, "The actual timing does not address its displayed UTF-16 source");
            previous = timing.start_ms;
        }
        recordings.try_emplace(part.recording, Recording{state::sha256(data), std::filesystem::last_write_time(part.recording)});
    }
    void privacy() const {
        const auto page = lesson_page_state(view).index; const auto regions = lesson_reading_regions(view);
        const auto planned = snapshot();
        for (std::size_t index = 0; index < lesson.pages[page].blocks.size(); ++index) {
            const auto& block = lesson.pages[page].blocks[index];
            if (block.type != curriculum::BlockType::cpp_compiler && block.type != curriculum::BlockType::code && block.type != curriculum::BlockType::output_prediction) continue;
            const auto source = find(view, 1000 + static_cast<int>(index) * 100 + (block.type == curriculum::BlockType::cpp_compiler ? 10 : 1));
            const auto answer = block.type == curriculum::BlockType::output_prediction ? find(view, 1000 + static_cast<int>(index) * 100 + 2) : nullptr;
            const auto console = block.type == curriculum::BlockType::cpp_compiler ? find(view, 1000 + static_cast<int>(index) * 100 + 14) : nullptr;
            for (const auto& region : regions) require((!source || region.target != source) && (!answer || region.target != answer)
                && (!console || region.target != console), "A program body, console or typed prediction entered current compiler narration");
            for (const auto& part : planned.segments) require((!source || part.target != source) && (!answer || part.target != answer)
                && (!console || part.target != console), "An editor/reference/client/console body received reader audio or word shading");
        }
        const auto undisclosed = [&](const auto& text) { return (spec.answer.empty() || text.find(spec.answer) == std::string::npos)
            && (spec.hidden_first.empty() || text.find(spec.hidden_first) == std::string::npos)
            && (spec.hidden_second.empty() || text.find(spec.hidden_second) == std::string::npos); };
        for (const auto& region : regions) require(undisclosed(region.source_text), "The literal expected prediction leaked into narration regions");
        for (const auto& part : planned.segments) require(undisclosed(part.source), "The literal expected prediction leaked into reader audio");
    }
    void preserved() const {
        const auto data = saved();
        require(data.preferences.at(std::string(spec.preference)) == unrelated && state::read_bytes(asset) == original_asset && !IsWindowVisible(host),
            "The focal reader changed unrelated preferences, curricular bytes or desktop visibility");
        require(narration::read_history(data).entries.size() <= 3, "The focal reader exceeded the ordinary three page slots");
        for (const auto& [path, recording] : recordings)
            require(state::sha256(state::read_bytes(path)) == recording.sha && std::filesystem::last_write_time(path) == recording.modified,
                "An already prepared compatible PCM recording was rewritten");
    }
    void reopen_paused(const LessonReaderSnapshot& before) {
        require(!before.playing && !before.allocated && !before.finished,
            "The focal reopen requires an explicitly stopped current reader");
        const auto page = lesson_page_state(view).index;
        close();
        // Closing checkpoints the final prepared recording certificate, even
        // when its freshly answered feedback has never played. Compare reopening
        // against that durable checkpoint; the snapshot must still restore the
        // exact pre-close position and zero listening credit independently.
        const auto history = narration::read_history(state::StateStore(environment.paths.root).load());
        open(page); ready(); const auto after = snapshot();
        const auto restored_history = narration::read_history(saved());
        const bool unchanged = after.key == before.key && after.index == before.index && after.offset == before.offset
            && after.heard_through == before.heard_through && !after.playing && !after.allocated && !after.finished
            && restored_history == history;
        if (!unchanged) diagnose_reopen(before, after, history, restored_history);
        require(unchanged,
            "Focal reopening changed the actual pause, page slots or playback ownership");
        preserved();
    }
};

std::vector<std::size_t> feedback_parts(Harness& fixture, std::size_t block, const std::string& expected) {
    const auto snapshot = fixture.snapshot(); const auto source = wide(expected); std::vector<bool> covered(source.size()); std::vector<std::size_t> result;
    require(edit_text(fixture.control(block, 4)) == visible(expected), "The current compiler feedback lost its complete authored wording");
    for (std::size_t index = 0; index < snapshot.segments.size(); ++index) {
        const auto& part = snapshot.segments[index]; if (!part.feedback || part.block != block) continue;
        require(part.start < part.end && part.end <= source.size() && part.source == utf8(source.substr(part.start, part.end - part.start)),
            "A declarations feedback segment differs from its displayed UTF-16 source interval");
        for (auto at = part.start; at < part.end; ++at) covered[at] = true;
        fixture.verify_audio(part); result.push_back(index);
    }
    for (std::size_t at = 0; at < source.size(); ++at) require(iswspace(source[at]) || covered[at], "The feedback plan omitted an authored non-whitespace character");
    require(!result.empty(), "The current declarations feedback has no actual reader segments"); return result;
}

void prepared_prose(Harness& fixture, std::size_t block, const std::string& expected) {
    const auto snapshot = fixture.snapshot(); const auto source = wide(expected); std::vector<bool> covered(source.size());
    const auto target = fixture.control(block, 0);
    std::size_t count = 0;
    for (const auto& part : snapshot.segments) {
        if (part.feedback || part.block != block || part.target != target) continue;
        require(part.start < part.end && part.end <= source.size()
            && part.source == utf8(source.substr(part.start, part.end - part.start)),
            "Prepared criterion audio differs from its exact original UTF-16 source interval");
        for (auto at = part.start; at < part.end; ++at) covered[at] = true;
        fixture.verify_audio(part); ++count;
    }
    for (std::size_t at = 0; at < source.size(); ++at)
        require(iswspace(source[at]) || covered[at], "The prepared reader plan truncated a criterion before the questionless compiler");
    require(count > 0, "A preceding criterion has no actual prepared narration");
    std::cout << "PREPARED criterion block=" << block << " utf16=" << source.size() << " segments=" << count
        << "; source and PCM/timings checked, not full paragraph playback\n";
}

void exercise(const std::shared_ptr<windows::NarrationWorker>& backend, const ReaderSpec& spec) {
    Harness f(backend, spec);
    const auto compiler_at = position(f.lesson, spec.compiler), prediction_at = position(f.lesson, spec.prediction);
    const auto reference_at = position(f.lesson, spec.reference), following_at = position(f.lesson, spec.following);
    const auto& compiler = f.lesson.pages[compiler_at.page].blocks[compiler_at.block];
    const auto& prediction = f.lesson.pages[prediction_at.page].blocks[prediction_at.block];
    const auto& reference = f.lesson.pages[reference_at.page].blocks[reference_at.block];
    const auto& following = f.lesson.pages[following_at.page].blocks[following_at.block];
    require(compiler_at.page == spec.compiler_page && compiler_at.block == 4
        && prediction_at.page == spec.prediction_page && prediction_at.block == 1
        && reference_at.page == spec.reference_page && reference_at.block == 1
        && following_at.page == prediction_at.page && following_at.block == prediction_at.block + 1
        && compiler.question.empty() && compiler.output_check && f.lesson.compiler_puzzles_required
        && compiler.output_check->cases.size() == 1 && compiler.output_check->cases.front().outputs == std::vector<std::string>{std::string(spec.output)}
        && prediction.answers == std::vector<std::string>{std::string(spec.answer)} && !prediction.review_feedback.empty()
        && !prediction.explanation.empty() && reference.type == curriculum::BlockType::code && reference.code.size() == spec.reference_bytes
        && compiler.initial_code.size() == spec.initial_bytes
        && (spec.following_note ? following.type == curriculum::BlockType::note && !following.text.empty() : following.type == curriculum::BlockType::code),
        "The current compiler owners, no-question objective or independent output oracles changed");
    for (const auto& page : f.lesson.pages) for (const auto& block : page.blocks)
        require(test::compiler_reader_response_unchanged(f.saved(),f.lesson,block,f.entry_setup),
            "The fresh compiler QA contains an undisclosed response or changed synthetic prerequisite");
    require(!windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler), "The untouched real compiler already has a success receipt");
    std::cout << "SCOPE current " << spec.label << "; fresh p" << spec.compiler_page + 1
        << "; ordinary synthetic prerequisites="<<f.entry_setup.ordinary_responses.size()
        << "; compiler receipts, imports and listening history were not seeded; earlier activities are not UI acceptance\n";
    f.ready(); f.privacy();
    const auto pending_regions = lesson_reading_regions(f.view);
    require(std::ranges::any_of(pending_regions, [&](const auto& region) { return region.block_index == compiler_at.block
        && region.required_compiler && region.is_exercise && !region.solved; }), "The actual questionless compiler has no pending reader boundary");
    for (std::size_t index = 1; index < compiler_at.block; ++index) {
        require(std::ranges::any_of(pending_regions, [&](const auto& region) { return region.block_index == index
            && region.source_text == f.lesson.pages[compiler_at.page].blocks[index].text; }), "A criterion paragraph preceding the questionless compiler is absent from narration");
        prepared_prose(f, index, f.lesson.pages[compiler_at.page].blocks[index].text);
    }
    require(!lesson_page_state(f.view).can_next && !navigate_lesson_view(f.view, prediction_at.page), "The untouched compiler allowed its dependent page");
    std::cout << "PHASE actual questionless compiler boundary, forward seek and reader stop\n";
    f.reader->restart(); f.reader->pause(); f.reader->seek_seconds(600);
    auto pending = f.snapshot();
    require(pending.segments[pending.index].block == compiler_at.block && !pending.finished && !pending.allocated && pending.heard_through == 0,
        "Forward preview skipped the questionless compiler or invented listening");
    f.reader->seek_seconds(-1); f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); return value.waiting_gate && !value.playing && !value.allocated
        && value.segments[value.index].block == compiler_at.block; }, "the actual declarations compiler waits for its own check");
    f.verify_audio(f.snapshot().segments[f.snapshot().index], false);
    f.reader->seek_seconds(600);
    require(f.snapshot().segments[f.snapshot().index].block == compiler_at.block && !f.snapshot().finished
        && !navigate_lesson_view(f.view, prediction_at.page), "Seeking at the compiler boundary opened subsequent content");
    std::cout << "PHASE actual empty-output program, Incorrecto and retry stop\n";
    const auto initial = f.compile(compiler_at.block, compiler.initial_code);
    require(bytes(initial / "execution-stdout.bin").empty() && !windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler),
        "The real initial program acquired false completion credit");
    const auto incorrect_parts = feedback_parts(f, compiler_at.block, "✗ Incorrecto");
    std::set<std::size_t> initial_observed;
    f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); const auto& part = value.segments[value.index];
        if (value.playing && part.feedback && part.block == compiler_at.block) initial_observed.insert(value.index);
        return !value.playing && !value.allocated && !value.preparing && part.feedback && part.block == compiler_at.block
            && value.index == incorrect_parts.back() && value.offset + 1 >= part.duration_ms
            && std::ranges::all_of(incorrect_parts, [&](auto index) { return initial_observed.contains(index); });
    }, "the complete real Incorrecto verdict and retry stop");
    require(!f.snapshot().finished && !f.snapshot().segments[f.snapshot().index].correct && !lesson_page_state(f.view).can_next
        && !navigate_lesson_view(f.view, prediction_at.page), "The rejected compiler bypassed its retry boundary");
    f.reader->seek_seconds(600);
    require(f.snapshot().segments[f.snapshot().index].feedback && f.snapshot().index <= incorrect_parts.back(), "A forward preview escaped the rejected compiler verdict");
    std::cout << "PHASE actual complete-objective program, paused Correcto and explicit continuation\n";
    f.edit(compiler_at.block, 10, reference.code); const auto correct_run = f.compile(compiler_at.block, reference.code);
    const auto output = bytes(correct_run / "execution-stdout.bin"); std::string windows_output;
    for (const auto value : spec.output) { if (value == '\n') windows_output += '\r'; windows_output += value; }
    require((output == spec.output || output == windows_output) && windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler),
        "The actual reference did not earn its complete authored objective");
    const auto compiler_correct_parts = feedback_parts(f, compiler_at.block, "✓ Correcto");
    const auto paused_correct = f.snapshot();
    require(paused_correct.index == compiler_correct_parts.front() && paused_correct.offset == 0 && !paused_correct.playing
        && !paused_correct.allocated && !paused_correct.finished, "Correcto ignored the manual pause or skipped its unheard verdict");
    f.reader->toggle();
    wait([&] { return f.snapshot().finished; }, "explicit playback completes the real compiler Correcto");
    require(f.snapshot().heard_through > static_cast<std::int64_t>(compiler_correct_parts.back()), "The compiler verdict completed without actual playback credit");
    f.preserved();
    std::cout << "PHASE actual recovery question, rejected guide and retry stop\n";
    f.navigate(prediction_at.page); f.privacy(); f.reader->restart(); f.reader->pause(); f.reader->seek_seconds(600);
    pending = f.snapshot();
    require(pending.segments[pending.index].block == prediction_at.block && !pending.finished && pending.heard_through == 0
        && !lesson_page_state(f.view).can_next, "Forward preview skipped the new recovery or invented listening");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); return value.waiting_gate && !value.playing && !value.allocated
        && value.segments[value.index].block == prediction_at.block; }, "the new recovery waits before its following reference");
    const auto wrong = std::string(spec.wrong);
    f.reader->pause(); f.edit(prediction_at.block, 2, wrong); f.click(prediction_at.block, 3); f.reader->pause(); f.ready();
    const auto rejected = state::get_response(f.saved(), f.lesson, prediction);
    require(rejected && !rejected->correct && rejected->answer == wrong, "The actual wrong prediction did not produce an ordinary rejected answer");
    const auto wrong_parts = feedback_parts(f, prediction_at.block, "✗ Incorrecto\n\n" + prediction.review_feedback);
    std::set<std::size_t> observed;
    const auto observe = [&](const LessonReaderSnapshot& value) {
        if (value.playing && value.segments[value.index].feedback && value.segments[value.index].block == prediction_at.block) observed.insert(value.index);
    };
    f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); observe(value);
        require(!value.playing || value.segments[value.index].block <= prediction_at.block, "The rejected guide played a following reference");
        return !value.playing && !value.allocated && !value.preparing && value.segments[value.index].feedback
            && value.index == wrong_parts.back() && value.offset + 1 >= value.segments[value.index].duration_ms
            && std::ranges::all_of(wrong_parts, [&](auto index) { return observed.contains(index); });
    }, "the complete authored recovery guide and retry stop");
    require(std::ranges::all_of(wrong_parts, [&](auto index) { return observed.contains(index); }) && !f.snapshot().finished
        && !lesson_page_state(f.view).can_next, "The rejected recovery omitted guide playback or allowed advancement");
    std::cout << "PHASE focal rejected recovery reopens silently with its complete guide\n";
    f.reader->pause(); const auto rejected_pause = f.snapshot(); f.reopen_paused(rejected_pause); f.privacy();
    const auto reopened_rejected = state::get_response(f.saved(), f.lesson, prediction);
    require(reopened_rejected && !reopened_rejected->correct && reopened_rejected->answer == wrong
        && reopened_rejected->raw_json == rejected->raw_json && edit_text(f.control(prediction_at.block, 2)) == wrong
        && edit_text(f.control(prediction_at.block, 4)) == visible("✗ Incorrecto\n\n" + prediction.review_feedback)
        && !lesson_page_state(f.view).can_next, "Silent reopening changed the actual rejected answer, complete guide or retry gate");
    f.reader->seek_seconds(600);
    require(f.snapshot().segments[f.snapshot().index].feedback && f.snapshot().index <= wrong_parts.back(), "Seeking escaped the rejected recovery guide");
    std::cout << "PHASE real recovery success, complete explanation and following " << (spec.following_note ? "note body" : "reference caption") << '\n';
    f.reader->pause(); f.edit(prediction_at.block, 2, spec.answer); f.click(prediction_at.block, 3); f.reader->pause(); f.ready();
    const auto correct_parts = feedback_parts(f, prediction_at.block, "✓ Correcto\n\n" + prediction.explanation); observed.clear(); f.privacy();
    const auto accepted = state::get_response(f.saved(), f.lesson, prediction);
    require(accepted && accepted->correct && accepted->answer == spec.answer && lesson_page_state(f.view).can_next,
        "The actual corrected prediction did not complete its own page objective");
    require(!f.snapshot().playing && !f.snapshot().allocated && f.snapshot().index == correct_parts.front() && f.snapshot().offset == 0,
        "The new correct explanation ignored the manual pause");
    std::cout << "PHASE focal accepted recovery reopens at its manually paused explanation\n";
    const auto explanation_pause = f.snapshot(); f.reopen_paused(explanation_pause); f.privacy();
    const auto reopened_accepted = state::get_response(f.saved(), f.lesson, prediction);
    require(reopened_accepted && reopened_accepted->correct && reopened_accepted->answer == spec.answer
        && reopened_accepted->raw_json == accepted->raw_json && edit_text(f.control(prediction_at.block, 2)) == spec.answer
        && edit_text(f.control(prediction_at.block, 4)) == visible("✓ Correcto\n\n" + prediction.explanation),
        "Silent reopening changed the accepted recovery or its complete unheard explanation");
    f.reader->toggle();
    wait([&] { const auto value = f.snapshot(); observe(value); return value.playing
        && !value.segments[value.index].feedback && value.segments[value.index].block > prediction_at.block
        && (!spec.following_note || value.segments[value.index].target == f.control(following_at.block, 0));
    }, "the entire real explanation precedes its actual following curricular content");
    f.reader->pause(); const auto after_explanation = f.snapshot();
    require(std::ranges::all_of(correct_parts, [&](auto index) { return observed.contains(index); })
        && after_explanation.heard_through > static_cast<std::int64_t>(correct_parts.back()), "The reader continued before every explanation part actually played");
    const auto& next = after_explanation.segments[after_explanation.index]; const auto following_regions = lesson_reading_regions(f.view);
    require(next.block == following_at.block && std::ranges::any_of(following_regions, [&](const auto& region) {
        return region.target == next.target && (spec.following_note
            ? region.target == f.control(following_at.block, 0) && region.source_text == following.text && !region.passive_label
            : region.passive_label); }), "The complete explanation did not reach its actual following note body/reference caption");
    if (spec.following_note) prepared_prose(f, following_at.block, following.text);
    f.verify_audio(next, spec.following_note); f.privacy(); f.preserved();
    require(edit_text(f.control(prediction_at.block, 2)) == spec.answer
        && windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler) && bytes(initial / "main.cpp") == compiler.initial_code
        && bytes(correct_run / "main.cpp") == reference.code, "Reader continuation changed the actual answer, literal program or compiler achievement");
    const auto final_state = f.saved();
    for (const auto& page : f.lesson.pages) for (const auto& block : page.blocks)
        if (block.stable_id != compiler.stable_id && block.stable_id != prediction.stable_id)
            require(test::compiler_reader_response_unchanged(final_state,f.lesson,block,f.entry_setup),
                "The focal reader changed an exact synthetic prerequisite or fabricated a response for another untouched activity");
    std::cout << "PASS current " << spec.label << " reader: fresh p" << spec.compiler_page + 1
        << " QA; three preceding criteria fully planned with prepared source-bound PCM/timings (not full paragraph playback); actual no-question required gate and seek; two real educational runs; Incorrecto retry stop; manual pause and explicit Correcto playback; actual recovery wait/rejected guide/full explanation playback before "
        << (spec.following_note ? "the following note body (also fully planned, not fully played)" : "reference")
        << "; silent rejected/accepted reopens; non-silent 24 kHz PCM, original asset and recordings retained. Earlier pages, imports, human listening and full curricular traversal are outside this focal regression.\n";
}
int entry_setup_case(const ReaderSpec& spec){
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/spec.filename;
    const auto original=state::read_bytes(asset);require(audited_source(original,spec),"The current compiler-reader snapshot differs from its audited revision");
    const auto lesson=windows::load_lesson(asset);const auto compiler=position(lesson,spec.compiler),prediction=position(lesson,spec.prediction);
    require(compiler.page==spec.compiler_page&&prediction.page==spec.prediction_page,"The audited compiler/recovery destinations changed");
    if(spec.token==assignments.token){
    struct Field {std::string_view id;bool question;std::string_view sha;};
    constexpr std::array fields{
        Field{"block_0002_0003",true,"1d09b90cd8682b1e179b1692dadf126e35e14600ee1ebddbb531646f1dc4056f"},
        Field{"block_0002_0003",false,"fd83fedfda451e39455543bad51ff91e4bb53736d7a3f06851f37c8fa7a2649d"},
        Field{"block_0004_0001",false,"86db2f2a49f75d55d128f7e5a6da3c2dd03ff854fac1ca19c57218e636c4537b"},
        Field{"block_0004_0002",true,"7569359594104ec14a3c77a57d7a5b2e2012cdefcc02b1243b16047dc2d7e499"},
        Field{"block_0004_0002",false,"298fa6db7b6c42436768017d04c5b7c1ba5eae0448fc54d6e4640a46b7f05a59"},
        Field{"block_0007_0001",false,"f04c1e07e1c7deab38360970dd91b9f1aa0949debe7944bf85e7242bb10884e0"}};
    for(const auto& field:fields){const auto at=position(lesson,field.id);const auto& block=lesson.pages[at.page].blocks[at.block];
        require(state::sha256(field.question?block.question:block.explanation)==field.sha,"One of the six current editorial fields changed");}
    }
    state::StateData initial;state::prepare_lesson_state(initial,lesson);
    state::Draft draft;draft.source=lesson.pages[compiler.page].blocks[compiler.block].initial_code+"\n// isolated QA target draft";
    draft.input="exact\tinput\r\n";draft.selection_start=3;draft.selection_end=9;
    const auto draft_key=curriculum::draft_key("programming",lesson,lesson.pages[compiler.page].blocks[compiler.block]);
    state::put_draft(initial,draft_key,draft);initial.preferences["assignment-entry-headless-qa"]["opaque"]=std::string(" preserve\t\r\n");
    const auto history=narration::read_history(initial);
    auto old_setup=initial;state::save_current_page(old_setup,lesson,compiler.page);
    const auto old_entry=test::compiler_reader_entry_decision(old_setup,lesson,compiler.page);
    require(old_entry.page==0&&!old_entry.pending_math,"The old address-only setup did not reproduce the real earlier-puzzle gate");
    auto prepared=initial;const auto setup=test::prepare_compiler_reader_entry(prepared,lesson,compiler.page);
    require(!setup.ordinary_responses.empty()&&test::compiler_reader_entry_decision(prepared,lesson,compiler.page).page==compiler.page&&
        state::current_page(prepared,lesson)==compiler.page,"The shared evaluated prerequisite setup did not admit the focal compiler page");
    state::validate_lesson_state(prepared,lesson);
    require(narration::read_history(prepared)==history&&history.entries.empty(),"The setup fabricated listening or a bookmark");
    for(const auto& [domain,values]:initial.preferences)if(domain!="learning_progress")for(const auto& [key,value]:values)
        require(state::find(prepared,domain,key)&&*state::find(prepared,domain,key)==value,"The setup changed a target draft or unrelated data");
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){
        require(!windows::compiler_puzzle_completed(prepared,lesson,block),"An ordinary prerequisite created a compiler achievement");
        require(test::compiler_reader_response_unchanged(prepared,lesson,block,setup),"The shared setup created a response outside its disclosed prefix");}
    require(state::can_reach_page(prepared,lesson,compiler.page+1),"The first next page lost its arrival permission");
    if(prediction.page>compiler.page+1)require(!state::can_reach_page(prepared,lesson,prediction.page)&&
        state::clamp_reachable_page(prepared,lesson,prediction.page)==compiler.page+1,
        "The arrival frontier skipped an unvisited intermediate page");
    auto later_data=prepared;state::Response later;later.answer=std::string(spec.answer);
    require(curriculum::evaluate_text_answer(lesson.pages[prediction.page].blocks[prediction.block],later.answer).correct,
        "The explicit later-answer model is incompatible with the current recovery contract");
    later.correct=true;state::put_response(later_data,lesson,lesson.pages[prediction.page].blocks[prediction.block],later);
    later_data.preferences["learning_progress"]["lesson_"+lesson.id+"_completed"]=true;
    require(test::compiler_reader_entry_decision(later_data,lesson,prediction.page).page==compiler.page,
        "A later answer or old completed flag bypassed the real compiler receipt");
    if(prediction.page>compiler.page+1)require(!state::can_reach_page(later_data,lesson,prediction.page),
        "Later data fabricated a visit to the intermediate page");
    const auto before_p8=initial;bool rejected=false;
    try{(void)test::prepare_compiler_reader_entry(initial,lesson,prediction.page);}catch(const std::exception& error){
        rejected=std::string_view(error.what()).find("compiler receipt")!=std::string_view::npos;}
    require(rejected&&initial==before_p8,"Preparing the recovery page fabricated a compiler receipt or partially wrote failed setup");
    for(const auto& [id,raw]:setup.ordinary_responses){(void)raw;const auto at=position(lesson,id);const auto& block=lesson.pages[at.page].blocks[at.block];
        auto missing=prepared;missing.preferences["learning_progress"].erase(state::response_storage_key(lesson,block));
        state::validate_lesson_state(missing,lesson);const auto missing_before=missing;
        require(test::compiler_reader_entry_decision(missing,lesson,compiler.page).page==at.page&&missing==missing_before,
            "A missing ordinary prerequisite was admitted or the entry query mutated it");
        auto wrong=prepared;auto response=*state::get_response(wrong,lesson,block);response.correct=false;
        if(block.type==curriculum::BlockType::multiple_choice){response.selected=static_cast<std::int64_t>((*block.correct_index+1)%block.options.size());
            response.answer=block.options[static_cast<std::size_t>(response.selected)];require(!curriculum::evaluate_choice(block,static_cast<std::size_t>(response.selected)).correct,
                "The rejected-choice fixture accidentally selected a correct answer");}
        else if(block.type==curriculum::BlockType::order_code){std::swap(response.order.front(),response.order.back());
            const std::vector<std::size_t> order(response.order.begin(),response.order.end());
            require(!curriculum::evaluate_order(block,order).correct,"The rejected-order fixture accidentally preserved a valid permutation");}
        else{response.answer="QA incorrect ordinary answer";
            require(!curriculum::evaluate_text_answer(block,response.answer).correct,"The rejected-text fixture accidentally matched its contract");}
        state::clear_response_review(response);state::put_response(wrong,lesson,block,response);state::validate_lesson_state(wrong,lesson);const auto wrong_before=wrong;
        require(test::compiler_reader_entry_decision(wrong,lesson,compiler.page).page==at.page&&wrong==wrong_before,
            "A rejected ordinary prerequisite was admitted or changed by the entry query");
    }
    require(state::read_bytes(asset)==original,"The headless setup test changed the curricular asset");
    std::cout<<"PASS headless "<<spec.label<<" entry setup: "<<setup.ordinary_responses.size()
        <<" evaluated synthetic ordinary prerequisites, sequential arrival to p"<<compiler.page+1
        <<", absent/rejected predecessors blocked; unproved compiler blocks p"<<prediction.page+1
        <<"; target draft and empty history intact"<<(spec.token==assignments.token?"; six current editorial fields and intermediate p7 checked":"")
        <<". No UI, Worker, playback, native compilation or real learner achievement.\n";
    return 0;
}
int entry_setup_only(){for(const auto* spec:{&declarations,&assignments,&constants})(void)entry_setup_case(*spec);return 0;}
#include "presentation/windows/u11_compiler_historical_reader_test.hpp"
#include "presentation/windows/u11_u12_compiler_historical_reader_test.hpp"
} // namespace

int main(int argc, char** argv) {
    if(argc==2&&std::string_view(argv[1])=="--entry-setup"){
        try{return entry_setup_only();}
        catch(const std::exception& error){std::cerr<<"FAIL headless assignment entry setup: "<<error.what()<<'\n';return 1;}
    }
    const compiler_historical::HistoricalReaderSpec* historical=nullptr;
    const remaining_compiler_historical::Source* remaining=nullptr;
    if(argc==5){const std::string_view mode=argv[1];
        if(mode=="--declarations-historical")historical=&compiler_historical::declarations_source;
        else if(mode=="--assignments-historical")historical=&compiler_historical::assignments_source;
        else if(mode=="--constants-historical")historical=&compiler_historical::constants_source;
        else if(mode=="--session-historical")remaining=&remaining_compiler_historical::session;
        else if(mode=="--characters-historical")remaining=&remaining_compiler_historical::characters;
        const std::string_view phase=argv[4];
        if((!historical&&!remaining)||phase.size()!=1||phase.front()<'0'||phase.front()>(remaining?'3':'1'))return 2;
    }
    else if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--assignments" && std::string_view(argv[1]) != "--constants")) return 2;
    const auto& spec = historical ? historical->current : argc == 1 ? declarations : std::string_view(argv[1]) == "--assignments" ? assignments : constants;
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try {
        require(rich != nullptr, "Cannot load native RichEdit"); reader_theme::prepare_fonts();
        auto backend = std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        struct Shutdown { std::shared_ptr<windows::NarrationWorker> worker; ~Shutdown() { worker->shutdown(); } } cleanup{backend};
        if(remaining)remaining_compiler_historical::exercise(backend,*remaining,std::filesystem::path(argv[2]),std::filesystem::path(argv[3]),argv[4][0]-'0');
        else if(historical)compiler_historical::exercise(backend,*historical,std::filesystem::path(argv[2]),std::filesystem::path(argv[3]),argv[4][0]-'0');
        else exercise(backend, spec);
    } catch (const std::exception& error) { std::cerr << "FAIL " << (historical||remaining?"historical ":"current ") << (remaining?remaining->label:spec.label) << " compiler reader: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

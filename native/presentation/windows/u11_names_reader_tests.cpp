#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr std::string_view expected_output =
    "Caso 1\nReservado: Pasillo_2\nSigno: *\nNumero: 4\nSigno: ;\nFin\n"
    "Caso 2\nNombre: n\nSigno: ;\nFin\nCaso 3\nNombre: Crea\nSigno: ;\nFin\n"
    "Caso 4\nPalabra: crea\nSigno: ;\nFin\nCaso 5\nNombre: A1_B\nSigno: ;\nFin\n"
    "Caso 6\nNombre: abcdefghijkl\nSigno: ;\nFin\nCaso 7\nError: Nombre demasiado largo\n"
    "Caso 8\nError: Caracter no admitido\nCaso 9\nNombre: solo\nFin\n";
constexpr std::string_view prediction_answer =
    "Lleno: Buffer ocupado\nGuardado: v:0:f\nSiguiente: ;\nVacio: Token invalido\nEntrada: n:8";
static_assert(expected_output.size() == 327 && prediction_answer.size() == 85);

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
    require(result.has_value(), "An audited U11L17 reader identity is missing"); return *result;
}

struct Harness {
    std::shared_ptr<windows::NarrationWorker> backend;
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    std::filesystem::path asset;
    std::vector<std::uint8_t> original_asset;
    HWND host = nullptr, view = nullptr;
    std::unique_ptr<LessonReader> reader;
    state::Preferences unrelated;
    std::optional<narration::Bookmark> other_page;
    std::optional<narration::Bookmark> compiler_page;
    std::optional<narration::Bookmark> recovery_page;
    struct Recording { std::string sha; std::filesystem::file_time_type modified; };
    std::map<std::filesystem::path, Recording> recordings;

    explicit Harness(std::shared_ptr<windows::NarrationWorker> worker) : backend(std::move(worker)) {
        asset = std::filesystem::path(build::source_root) /
            "app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_nombres_del_lenguaje_y_tokens_con_texto.json";
        original_asset = state::read_bytes(asset);
        require(state::sha256(original_asset) == "335036782a5c6eb7d1edd1a60c1b062572a791e8c6e4ce54e14fd25b246df4b0",
            "The U11L17 reader snapshot changed; review its exact scope before updating this pin");
        lesson = windows::load_lesson(asset);
        const auto token = L"names-reader-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        environment.options = {ProfileId::Codex, token, false};
        environment.paths = make_profile_paths(std::filesystem::path(build::source_root) / "out/qa/u11-names-reader", ProfileId::Codex, token);
        require(!std::filesystem::exists(environment.paths.root), "U11L17 reader requires a new isolated physical QA profile");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root); auto initial = store.load();
        require(initial.preferences.empty(), "The new reader QA already contains student data");
        state::prepare_lesson_state(initial, lesson);
        auto& preferences = initial.preferences["lesson_narration_settings"];
        preferences["engine"] = std::string("KOKORO"); preferences["language"] = std::string("es");
        preferences["kokoro_voice:es"] = std::string("ef_dora"); preferences["rate"] = 3.;
        preferences["kokoro_advance_generation"] = false;
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences["u11_names_reader_qa"]["opaque"] = std::string(" \t conservar\r\n保存");
        unrelated = initial.preferences.at("u11_names_reader_qa"); store.save(initial);
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Hidden current U11L17 reader QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(host != nullptr, "Cannot create hidden actual reader host"); open(0);
    }
    ~Harness() {
        if (std::uncaught_exceptions()) backend->shutdown();
        close(); if (host) DestroyWindow(host);
    }
    HWND control(std::size_t block, int part) const {
        const auto item = find(view, 1000 + static_cast<int>(block) * 100 + part);
        require(item != nullptr, "An actual U11L17 reader control is missing"); return item;
    }
    LessonReaderSnapshot snapshot() const {
        const auto value = reader->snapshot(); require(value.error.empty(), "Real U11L17 reader error: " + value.error); return value;
    }
    void ready(bool all = true) const {
        wait([&] {
            const auto value = snapshot();
            return value.planned && !value.segments.empty() && value.index < value.segments.size()
                && (all ? std::ranges::all_of(value.segments, [](const auto& part) { return part.available; }) : value.segments[value.index].available);
        }, "actual U11L17 speech preparation and word alignment");
    }
    void open(std::size_t expected_page) {
        require(!view && !reader, "Close the earlier QA view before reopening");
        LessonViewHooks hooks;
        hooks.before_page_change = [&](HWND) { if (reader) reader->pause(); };
        hooks.before_destroy = [&](HWND) { reader.reset(); };
        hooks.content_changed = [&](HWND) { if (reader) reader->content_changed(); };
        hooks.feedback_changed = [&](HWND, std::size_t block, bool correct) { if (reader) reader->feedback(block, correct); };
        hooks.priority_work = [worker = backend](std::stop_token stop) -> std::shared_ptr<void> {
            return std::make_shared<windows::NarrationPriorityLease>(worker->heavy_work_lease(stop));
        };
        view = create_lesson_view(host, environment, lesson, std::move(hooks));
        require(view != nullptr, "Cannot create current native U11L17 lesson view");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        require(!IsWindowVisible(host) && lesson_page_state(view).index == expected_page, "The QA host became visible or opened another page");
        reader = std::make_unique<LessonReader>(view, environment, backend, [] {}); ready(false);
    }
    void close() {
        reader.reset(); if (view) { (void)flush_lesson_view(view); DestroyWindow(view); view = nullptr; }
    }
    state::StateData saved() const {
        require(flush_lesson_view(view), "Cannot flush the current isolated reader state"); return read_lesson_state(view);
    }
    void navigate(std::size_t page) {
        reader->pause(); require(navigate_lesson_view(view, page), "The actual U11L17 page remains locked"); ready();
    }
    void edit(std::size_t block, int part, std::string_view value) {
        const auto target = control(block, part); require(IsWindowEnabled(target), "The actual QA editor is disabled");
        CHARRANGE all{0, -1}; SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
        const auto text = wide(value); SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str())); pump();
        require(edit_text(target) == value, "The actual RichEdit changed supplied source or answer characters");
    }
    void click(std::size_t block, int part) {
        const auto target = control(block, part); require(IsWindowEnabled(target), "The native QA action is disabled");
        SendMessageW(target, BM_CLICK, 0, 0); pump();
    }
    std::filesystem::path compile(std::size_t block, std::string_view source) {
        const auto folder = environment.paths.projects / "cpp-runs"; std::set<std::filesystem::path> old;
        if (std::filesystem::exists(folder)) for (const auto& entry : std::filesystem::directory_iterator(folder)) if (entry.is_directory()) old.insert(entry.path());
        reader->pause(); click(block, 11);
        wait([&] { return IsWindowEnabled(control(block, 11)); }, "the actual U11L17 educational compilation and execution");
        reader->pause(); ready();
        std::vector<std::filesystem::path> created;
        for (const auto& entry : std::filesystem::directory_iterator(folder)) if (entry.is_directory() && !old.contains(entry.path())) created.push_back(entry.path());
        require(created.size() == 1, "The reader fixture did not run one fresh educational program");
        const auto directory = created.front();
        require(bytes(directory / "main.cpp") == source && bytes(directory / "stdin.bin").empty()
            && bytes(directory / "execution-stderr.bin").empty() && edit_text(control(block, 34)) == "Ejecución terminada (código 0).",
            "The reader callback did not follow its own successful educational run");
        std::cout << "RUN " << utf8(directory.native()) << '\n'; return directory;
    }
    void verify_audio(const LessonReaderSegmentSnapshot& part, bool require_word_timings = true) {
        require(part.available && !part.recording.empty(), "A current reader segment has no real recording");
        const auto data = state::read_bytes(part.recording);
        const auto u16 = [&](std::size_t at) { return unsigned(data[at]) | (unsigned(data[at + 1]) << 8); };
        const auto u32 = [&](std::size_t at) { return u16(at) | (u16(at + 2) << 16); };
        const auto tag = [&](std::size_t at, std::string_view value) { return std::equal(value.begin(), value.end(), data.begin() + at); };
        require(data.size() > 44 && tag(0, "RIFF") && tag(8, "WAVE") && tag(12, "fmt ") && tag(36, "data")
            && u16(20) == 1 && u16(22) == 1 && u32(24) == 24000 && u16(34) == 16 && u32(40) == data.size() - 44
            && (data.size() - 44) % 2 == 0 && std::ranges::any_of(data.begin() + 44, data.end(), [](auto sample) { return sample != 0; })
            && std::abs(part.duration_ms - (data.size() - 44) / 48.) <= 1 && (!require_word_timings || !part.timings.empty()),
            "The current narration lacks real non-silent mono 24 kHz PCM16 and word alignment");
        double previous = 0;
        for (const auto& timing : part.timings) {
            require(timing.start < timing.end && timing.end <= wide(part.source).size() && timing.start_ms >= previous
                && std::isfinite(timing.start_ms) && std::isfinite(timing.end_ms) && timing.end_ms >= timing.start_ms
                && timing.end_ms <= part.duration_ms + 1, "The real word timing does not address its UTF-16 source");
            previous = timing.start_ms;
        }
        recordings.try_emplace(part.recording, Recording{state::sha256(data), std::filesystem::last_write_time(part.recording)});
    }
    void privacy() const {
        const auto page = lesson_page_state(view).index; const auto regions = lesson_reading_regions(view);
        for (std::size_t index = 0; index < lesson.pages[page].blocks.size(); ++index) {
            const auto& block = lesson.pages[page].blocks[index];
            const auto source = find(view, 1000 + static_cast<int>(index) * 100 + (block.type == curriculum::BlockType::cpp_compiler ? 10 : 1));
            if (block.type != curriculum::BlockType::cpp_compiler && block.type != curriculum::BlockType::code && block.type != curriculum::BlockType::output_prediction) continue;
            const auto answer = block.type == curriculum::BlockType::output_prediction ? find(view, 1000 + static_cast<int>(index) * 100 + 2) : nullptr;
            for (const auto& region : regions) require((!source || region.target != source) && (!answer || region.target != answer), "An editor/reference/client body or typed answer entered lesson narration");
            for (const auto& part : snapshot().segments) require((!source || part.target != source) && (!answer || part.target != answer), "A C++ source body or typed answer received reader audio or word shading");
        }
        for (const auto& region : regions)
            require(region.source_text.find(prediction_answer) == std::string::npos && region.source_text.find("Lleno: Buffer ocupado") == std::string::npos
                && region.source_text.find("Guardado: v:0:f") == std::string::npos, "The expected recovery output leaked into a narration region");
        for (const auto& part : snapshot().segments)
            require(part.source.find(prediction_answer) == std::string::npos && part.source.find("Lleno: Buffer ocupado") == std::string::npos
                && part.source.find("Guardado: v:0:f") == std::string::npos, "The expected recovery output leaked into narration");
    }
    void preserved() const {
        const auto data = saved(); const auto history = narration::read_history(data);
        require(data.preferences.at("u11_names_reader_qa") == unrelated && state::read_bytes(asset) == original_asset && !IsWindowVisible(host),
            "Reader QA changed unrelated preferences, curriculum bytes or desktop visibility");
        require(history.entries.size() <= 3, "Reader activity created a fourth parallel page bookmark");
        if (other_page) {
            const auto found = std::ranges::find(history.entries, other_page->key, &narration::Bookmark::key);
            require(found != history.entries.end() && *found == *other_page, "The actual other-page PCM bookmark changed or was evicted");
        }
        if (compiler_page) {
            const auto found = std::ranges::find(history.entries, compiler_page->key, &narration::Bookmark::key);
            require(found != history.entries.end() && *found == *compiler_page, "Reading the recovery changed the real completed compiler-page bookmark");
        }
        if (recovery_page) {
            const auto found = std::ranges::find(history.entries, recovery_page->key, &narration::Bookmark::key);
            require(found != history.entries.end() && *found == *recovery_page, "Resetting the compiler changed the actual dependent-page bookmark");
        }
        for (const auto& [path, recording] : recordings)
            require(state::sha256(state::read_bytes(path)) == recording.sha && std::filesystem::last_write_time(path) == recording.modified,
                "A compatible real recording was rewritten during reader restoration");
    }
    void reopen_exact(const LessonReaderSnapshot& before) {
        require(!before.playing && !before.allocated && !before.finished && before.offset > 120
            && before.offset + 200 < before.segments[before.index].duration_ms, "The requested pause is not a released partial recording");
        const auto page = lesson_page_state(view).index; const auto history = narration::read_history(saved());
        close(); open(page); ready(); const auto after = snapshot();
        require(after.key == before.key && after.index == before.index && after.offset == before.offset && after.heard_through == before.heard_through
            && !after.playing && !after.allocated && narration::read_history(saved()) == history,
            "Reopening changed the exact real pause, saved slots or playback ownership"); preserved();
    }
};

std::vector<std::size_t> feedback_parts(Harness& fixture, std::size_t block, const std::string& expected) {
    const auto snapshot = fixture.snapshot(); const auto source = wide(expected); std::vector<bool> covered(source.size()); std::vector<std::size_t> result;
    require(edit_text(fixture.control(block, 4)) == expected, "The current feedback lost its full authored wording");
    for (std::size_t index = 0; index < snapshot.segments.size(); ++index) {
        const auto& part = snapshot.segments[index]; if (!part.feedback || part.block != block) continue;
        require(part.start < part.end && part.end <= source.size() && part.source == utf8(source.substr(part.start, part.end - part.start)),
            "The feedback audio source differs from its exact displayed UTF-16 interval");
        for (auto at = part.start; at < part.end; ++at) covered[at] = true;
        fixture.verify_audio(part); result.push_back(index);
    }
    for (std::size_t at = 0; at < source.size(); ++at) require(iswspace(source[at]) || covered[at], "The full feedback plan omitted a non-whitespace character");
    require(!result.empty(), "The actual feedback has no reader segments"); return result;
}

void exercise(const std::shared_ptr<windows::NarrationWorker>& backend) {
    std::cout << "PHASE prepare current U11L17 and an actual other-page PCM bookmark\n";
    Harness f(backend);
    const auto compiler_at = position(f.lesson, "block_0005_0003"), prediction_at = position(f.lesson, "work_u11l17_reserva_textual");
    const auto& compiler = f.lesson.pages[compiler_at.page].blocks[compiler_at.block];
    const auto& prediction = f.lesson.pages[prediction_at.page].blocks[prediction_at.block];
    const auto& choice = f.lesson.pages[5].blocks[2]; const auto& reference = f.lesson.pages[7].blocks[1];
    require(compiler_at.page == 5 && compiler_at.block == 4 && prediction_at.page == 6 && prediction_at.block == 1
        && compiler.output_check && compiler.output_check->cases.size() == 1 && compiler.output_check->cases.front().outputs == std::vector<std::string>{std::string(expected_output)}
        && prediction.answers == std::vector<std::string>{std::string(prediction_answer)} && choice.correct_index,
        "The audited U11L17 reader owners or independent oracles changed");
    for (const auto& page : f.lesson.pages) for (const auto& block : page.blocks)
        require(!state::get_response(f.saved(), f.lesson, block), "A fresh reader QA response was seeded");

    // This separate page slot is earned by actual PCM playback, not fabricated
    // history or a finished/heard flag. Only the current sentence is prepared.
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); return value.playing && value.offset > 120
        && value.offset + 200 < value.segments[value.index].duration_ms; }, "actual other-page narration");
    f.reader->pause(); const auto other = f.snapshot(); f.verify_audio(other.segments[other.index]);
    const auto other_history = narration::read_history(f.saved());
    const auto other_slot = std::ranges::find(other_history.entries, other.key, &narration::Bookmark::key);
    require(other_slot != other_history.entries.end() && other_slot->position.milliseconds == other.offset
        && other_slot->position.heard_prefix && other_slot->position.heard_prefix->microseconds > 0, "The other-page slot lacks real playback evidence");
    f.other_page = *other_slot;
    // Isolate the focal page by its saved UI address, without answering earlier
    // puzzles or inventing narration history. The p1 heard slot remains exact.
    f.close(); state::StateStore focal_store(f.environment.paths.root); auto focal_state = focal_store.load();
    state::save_current_page(focal_state, f.lesson, compiler_at.page);
    focal_state.preferences["lesson_narration_settings"]["kokoro_advance_generation"] = true;
    focal_store.save(focal_state); f.open(compiler_at.page); f.ready(); f.privacy();
    std::cout << "PHASE required compiler pending reader gate and actual choice\n";
    f.click(2, 20 + static_cast<int>(*choice.correct_index) * 2); f.click(2, 3); f.reader->pause(); f.ready();
    require(state::get_response(f.saved(), f.lesson, choice)->correct, "The actual prerequisite choice did not complete");
    f.reader->seek_seconds(600); const auto pending = f.snapshot();
    require(pending.segments[pending.index].block == compiler_at.block && !pending.finished && !pending.allocated && pending.heard_through == 0,
        "A forward preview bypassed the current compiler or invented feedback listening");
    f.reader->toggle(); wait([&] { return f.snapshot().waiting_gate; }, "pending compiler reader stop");
    require(!navigate_lesson_view(f.view, prediction_at.page), "The pending compiler opened the recovery page");
    std::cout << "PHASE initial real program and Incorrecto playback\n";
    const auto initial = f.compile(compiler_at.block, compiler.initial_code);
    require(bytes(initial / "execution-stdout.bin").empty() && !windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler), "The initial program acquired false execution credit");
    feedback_parts(f, compiler_at.block, "✗ Incorrecto");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); return !value.playing && !value.allocated && !value.preparing && value.segments[value.index].feedback; }, "complete initial Incorrecto playback");
    require(!f.snapshot().finished, "Incorrecto falsely completed the compiler reading gate");
    f.reader->seek_seconds(600); require(f.snapshot().segments[f.snapshot().index].feedback, "Forward preview escaped the wrong compiler verdict");
    std::cout << "PHASE correct real nine-report program and Correcto playback\n";
    f.edit(compiler_at.block, 10, reference.code); const auto correct_run = f.compile(compiler_at.block, reference.code);
    const auto actual_output = bytes(correct_run / "execution-stdout.bin"); std::string windows_output;
    for (const auto value : expected_output) { if (value == '\n') windows_output += '\r'; windows_output += value; }
    require((actual_output == expected_output || actual_output == windows_output) && windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler), "The actual reference did not earn its aggregate output goal");
    feedback_parts(f, compiler_at.block, "✓ Correcto");
    f.reader->toggle(); wait([&] { return f.snapshot().finished; }, "complete real compiler Correcto playback"); f.preserved();
    const auto compiler_snapshot = f.snapshot(); const auto compiler_history = narration::read_history(f.saved());
    const auto compiler_slot = std::ranges::find(compiler_history.entries, compiler_snapshot.key, &narration::Bookmark::key);
    require(compiler_slot != compiler_history.entries.end() && compiler_slot->position.finished,
        "The actually played Correcto has no completed compiler-page bookmark"); f.compiler_page = *compiler_slot;

    std::cout << "PHASE new recovery pending gate and rejected guide\n";
    f.navigate(prediction_at.page); f.privacy(); f.reader->restart(); f.reader->pause(); f.reader->seek_seconds(600);
    const auto question_gate = f.snapshot();
    require(question_gate.segments[question_gate.index].block == prediction_at.block && !question_gate.finished && question_gate.heard_through == 0
        && !lesson_page_state(f.view).can_next, "A recovery preview skipped its unanswered boundary or invented listening");
    f.reader->toggle(); wait([&] { return f.snapshot().waiting_gate; }, "new recovery question stops before the reference");
    f.reader->pause();
    const std::string wrong = "Lleno: Token invalido\nGuardado: v:0:f\nSiguiente: ;\nVacio: Token invalido\nEntrada: n:8";
    f.edit(prediction_at.block, 2, wrong); f.click(prediction_at.block, 3); f.reader->pause(); f.ready();
    const auto wrong_text = "✗ Incorrecto\n\n" + prediction.review_feedback;
    const auto wrong_parts = feedback_parts(f, prediction_at.block, wrong_text);
    std::set<std::size_t> observed;
    const auto observe = [&](const LessonReaderSnapshot& value) {
        if (value.playing && value.segments[value.index].feedback && value.segments[value.index].block == prediction_at.block) observed.insert(value.index);
    };
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); observe(value); return value.playing && value.segments[value.index].source.find("Comprueba primero") != std::string::npos
        && value.offset > 120 && value.offset + 200 < value.segments[value.index].duration_ms; }, "word-aligned rejected recovery guide");
    f.reader->pause(); const auto wrong_pause = f.snapshot();
    std::cout << "PHASE exact rejected-guide pause and full reopen\n"; f.reopen_exact(wrong_pause);
    require(edit_text(f.control(prediction_at.block, 2)) == wrong && !lesson_page_state(f.view).can_next, "Guide reopen changed the rejected response or allowed advancement");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); observe(value); return !value.playing && !value.allocated && !value.preparing && value.segments[value.index].feedback; }, "entire rejected recovery guide and stop");
    require(std::ranges::all_of(wrong_parts, [&](auto index) { return observed.contains(index); }) && !f.snapshot().finished, "An unheard guide part was treated as complete");
    f.reader->seek_seconds(600); require(f.snapshot().segments[f.snapshot().index].feedback, "Seeking escaped the rejected guide into the reference");
    std::cout << "PHASE successful recovery explanation and real PCM pause\n";
    f.reader->pause(); f.edit(prediction_at.block, 2, prediction_answer); f.click(prediction_at.block, 3); f.reader->pause(); f.ready();
    const auto correct_text = "✓ Correcto\n\n" + prediction.explanation;
    const auto correct_parts = feedback_parts(f, prediction_at.block, correct_text); observed.clear(); f.privacy();
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); observe(value); return value.playing && value.segments[value.index].source.find("El carácter f") != std::string::npos
        && value.offset > 120 && value.offset + 200 < value.segments[value.index].duration_ms; }, "word-aligned successful recovery explanation");
    f.reader->pause(); const auto correct_pause = f.snapshot();
    std::cout << "PHASE exact successful-explanation pause and full reopen\n"; f.reopen_exact(correct_pause);
    const auto accepted = state::get_response(f.saved(), f.lesson, prediction);
    require(accepted && accepted->correct && accepted->answer == prediction_answer, "Successful explanation reopen lost the actual response");
    f.reader->toggle(); wait([&] { const auto value = f.snapshot(); observe(value); return value.playing && !value.segments[value.index].feedback && value.segments[value.index].block > prediction_at.block; }, "fully heard explanation reaches the actual following reference caption");
    f.reader->pause();
    require(std::ranges::all_of(correct_parts, [&](auto index) { return observed.contains(index); }), "The reader continued before an explanation part actually played");
    const auto after_explanation = f.snapshot();
    require(after_explanation.heard_through > static_cast<std::int64_t>(correct_parts.back()), "The complete played explanation did not earn its feedback boundary");
    const auto& caption = after_explanation.segments[after_explanation.index]; const auto following_regions = lesson_reading_regions(f.view);
    require(std::ranges::any_of(following_regions, [&](const auto& region) { return region.target == caption.target && region.passive_label; }),
        "The complete explanation did not continue to the actual passive reference caption");
    // This spoken label differs from the compact visible C++ caption. Verify
    // its actual PCM, without requiring invented word mappings onto that label.
    f.verify_audio(caption, false); f.preserved();
    const auto recovery_history = narration::read_history(f.saved());
    const auto recovery_slot = std::ranges::find(recovery_history.entries, after_explanation.key, &narration::Bookmark::key);
    require(recovery_slot != recovery_history.entries.end(), "The heard explanation has no saved recovery-page bookmark"); f.recovery_page = *recovery_slot;
    require(narration::read_history(f.saved()).entries.size() == 3, "The actual other page, compiler page and recovery page did not share exactly three slots");
    std::cout << "PHASE independent compiler Reset relocks the recovery and reopens\n";
    f.compiler_page.reset(); // Returning to p6 may now update its own slot.
    f.navigate(compiler_at.page); f.reader->pause(); f.click(compiler_at.block, 12); f.reader->pause(); f.ready(); f.privacy();
    require(!windows::compiler_puzzle_completed(f.saved(), f.lesson, compiler) && !navigate_lesson_view(f.view, prediction_at.page), "Explicit Reset did not relock the recovery owner");
    const auto retained = state::get_response(f.saved(), f.lesson, prediction);
    require(retained && retained->correct && retained->answer == accepted->answer && retained->raw_json == accepted->raw_json, "Reset rewrote the separate recovery response");
    f.close(); f.open(compiler_at.page); f.ready(); f.privacy(); f.preserved();
    require(!f.snapshot().playing && !f.snapshot().allocated && !f.snapshot().finished && !navigate_lesson_view(f.view, prediction_at.page), "Reopening Reset bypassed its gate or started old audio");
    std::cout << "PASS current U11L17 reader: real Kokoro PCM/timings, actual other-page bookmark, two real owner runs, pending/incorrect/correct boundaries, full guide/explanation playback, exact released pauses/reopens, three bounded page slots, source privacy and scoped Reset. No synthetic hearing, full curricular traversal or human listening acceptance claimed.\n";
}
} // namespace

int main() {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try {
        require(rich != nullptr, "Cannot load native RichEdit"); reader_theme::prepare_fonts();
        auto backend = std::make_shared<windows::NarrationWorker>(std::filesystem::path(build::source_root) / "out/narration/runtime.json");
        struct Shutdown { std::shared_ptr<windows::NarrationWorker> worker; ~Shutdown() { worker->shutdown(); } } cleanup{backend};
        exercise(backend);
    } catch (const std::exception& error) { std::cerr << "FAIL U11 names reader: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

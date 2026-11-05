#include "core/attempts.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr std::string_view lesson_id = "cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada";
constexpr std::string_view before_sha = "dfbbac04b7a04df14f09c88b4b7b84ed59ab2e6b4b13fc1261d042d93b743fe0";
constexpr std::string_view after_sha = "768458f3b6b8d646dcd44fe154b5f264f31f6eb2c3851bb36c5442ebad700157";
struct Plan { const char* identity; std::size_t page, block; curriculum::BlockType type; const char* suffix; };
constexpr std::array plans{
    Plan{"block_0002_0003", 2, 3, curriculum::BlockType::order_code, "__e03"},
    Plan{"block_0004_0001", 4, 1, curriculum::BlockType::output_prediction, "__e04"},
    Plan{"block_0004_0002", 4, 2, curriculum::BlockType::multiple_choice, "__e05"},
    Plan{"block_0007_0001", 7, 1, curriculum::BlockType::output_prediction, "__e06"},
};
void check(bool value, std::string_view message) { if (!value) throw std::runtime_error(std::string(message)); }
void pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        if (message.message != WM_QUIT) { TranslateMessage(&message); DispatchMessageW(&message); }
}
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
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
std::string visible(std::string value) { std::erase(value, '`'); return value; }
bool same_response(const std::optional<state::Response>& old, const std::optional<state::Response>& current) {
    if (old.has_value() != current.has_value()) return false;
    return !old || (old->answer == current->answer && old->correct == current->correct && old->raw_json == current->raw_json
        && old->selected == current->selected && old->order == current->order
        && old->reviewed_answer_identity == current->reviewed_answer_identity && old->historical_choice == current->historical_choice);
}
struct ReadingProjection {
    std::string key, anchor, source, context, math_mode, retired_successor;
    int target_id = 0;
    std::size_t block = 0;
    bool feedback = false, correct = false, is_exercise = false, solved = false, passive_label = false, required_compiler = false;
    bool operator==(const ReadingProjection&) const = default;
};
std::vector<ReadingProjection> project(const std::vector<LessonReadingRegion>& regions) {
    std::vector<ReadingProjection> result;
    for (const auto& region : regions)
        result.push_back({region.key, region.anchor, region.source_text, region.reading_context, region.math_mode, region.retired_successor,
            region.target ? GetDlgCtrlID(region.target) : 0, region.block_index, region.feedback, region.correct, region.is_exercise,
            region.solved, region.passive_label, region.required_compiler});
    return result;
}

struct View {
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host = nullptr, view = nullptr;
    std::string draft_owner;
    View(curriculum::Lesson model, std::size_t page) : lesson(std::move(model)) {
        const auto token = L"assignment-editorial-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        environment.options = {ProfileId::Codex, token, false};
        environment.paths = make_profile_paths(std::filesystem::path(build::source_root) / "out/qa/u11-assignment-editorial", ProfileId::Codex, token);
        check(!std::filesystem::exists(environment.paths.root), "The assignment editorial QA requires a fresh isolated physical profile");
        initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);
        check(store.load().preferences.empty(), "The assignment editorial QA already contains student data");
        state::StateData initial; state::prepare_lesson_state(initial, lesson); state::save_current_page(initial, lesson, page);
        initial.preferences["native_windows"]["ui_language"] = std::string("es");
        initial.preferences["assignment_editorial_opaque_qa"]["unchanged"] = std::string(" \t texto ajeno\r\n保存");
        for (const auto& source_page : lesson.pages) for (const auto& block : source_page.blocks)
            if (block.type == curriculum::BlockType::cpp_compiler && block.output_check) {
                draft_owner = curriculum::draft_key("programming", lesson, block);
                state::Draft draft; draft.source = block.initial_code + "\n// borrador personal aislado\n";
                draft.input = " \t entrada propia\r\n"; draft.indentation_json = state::make_indentation_metadata(draft.source, {});
                draft.selection_start = 4; draft.selection_end = 11; state::put_draft(initial, draft_owner, draft);
                // Synthetic receipts only make the dependent p8 view available.
                // No program runs here; this is not educational execution evidence.
                windows::CppRunRequest request; request.revision = 1; request.source_utf8 = draft.source;
                request.project_files = block.project_files; request.source_files = block.source_files; request.cmake = block.cmake;
                for (const auto& goal : block.output_check->cases) {
                    request.stdin_utf8 = goal.standard_input;
                    curriculum::FreshExecution execution{1, true, true, 0, goal.standard_input, goal.outputs.front()};
                    execution.executable = goal.executable;
                    if (goal.standard_error == curriculum::StandardErrorExpectation::non_blank) execution.standard_error = "Synthetic view prerequisite only";
                    (void)windows::record_compiler_puzzle_case(initial, lesson, block, request, execution, "Synthetic view prerequisite only");
                }
                check(windows::compiler_puzzle_completed(initial, lesson, block), "The isolated dependent-view prerequisite could not be prepared");
            }
        // These are storage-only bookmarks with no recording or claimed hearing.
        auto history = narration::begin({}, "assignment-storage-a", "assignment-owner-a", {0, 137, 0, false}, "opaque A", "assignment-page-a");
        history = narration::begin(std::move(history), "assignment-storage-b", "assignment-owner-b", {1, 239, 0, false}, "opaque B", "assignment-page-b");
        history = narration::begin(std::move(history), "assignment-storage-c", "assignment-owner-c", {2, 317, 0, false}, "opaque C", "assignment-page-c");
        narration::write_history(initial, history); store.save(initial);
        host = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_NOACTIVATE, L"STATIC", L"Hidden U11L20 editorial QA",
            WS_OVERLAPPEDWINDOW, 0, 0, 1100, 850, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        check(host != nullptr, "Cannot create the hidden assignment editorial host"); open(page);
    }
    ~View() { if (view) { (void)flush_lesson_view(view); DestroyWindow(view); } if (host) DestroyWindow(host); }
    void open(std::size_t expected_page) {
        view = create_lesson_view(host, environment, lesson); check(view != nullptr, "Cannot mount the actual assignment lesson");
        MoveWindow(view, 0, 0, 1100, 850, TRUE); pump();
        check(!IsWindowVisible(host) && lesson_page_state(view).index == expected_page, "The isolated editorial view opened another page or activated the desktop");
    }
    void reopen() {
        const auto page = lesson_page_state(view).index;
        check(flush_lesson_view(view), "Cannot save the actual assignment editorial view"); DestroyWindow(view); view = nullptr; open(page);
    }
    HWND item(std::size_t block, int part) const {
        const auto result = descendant(view, 1000 + static_cast<int>(block) * 100 + part);
        check(result != nullptr, "A real assignment editorial control is missing"); return result;
    }
    state::StateData saved() const {
        check(flush_lesson_view(view), "Cannot persist the isolated assignment editorial state"); return read_lesson_state(view);
    }
    void click(std::size_t block, int part) {
        const auto target = item(block, part); check(IsWindowEnabled(target), "The actual assignment editorial action is disabled");
        SendMessageW(target, BM_CLICK, 0, 0); pump();
    }
    std::vector<std::size_t> order(std::size_t block) const {
        const auto& model = lesson.pages[lesson_page_state(view).index].blocks[block];
        std::vector<std::pair<LONG, std::size_t>> positions; const auto list = item(block, 5);
        for (std::size_t index = 0; index < model.fragments.size(); ++index) {
            RECT bounds{}; const auto row = GetDlgItem(list, 100 + static_cast<int>(index));
            check(row && GetWindowRect(row, &bounds), "An independently movable assignment row is missing"); positions.emplace_back(bounds.top, index);
        }
        std::ranges::sort(positions); std::vector<std::size_t> result;
        for (const auto& [top, index] : positions) result.push_back(index); return result;
    }
    void arrange(std::size_t block, const std::vector<std::size_t>& wanted) {
        const auto list = item(block, 5);
        for (std::size_t target = 0; target < wanted.size(); ++target) {
            const auto current = order(block); auto from = std::ranges::find(current, wanted[target]) - current.begin();
            check(from < static_cast<std::ptrdiff_t>(current.size()), "A literal assignment ordering piece disappeared");
            while (from > static_cast<std::ptrdiff_t>(target)) {
                SendMessageW(GetDlgItem(list, 100 + static_cast<int>(wanted[target])), WM_SYSKEYDOWN, VK_UP, LPARAM{1} << 29); pump(); --from;
            }
        }
        check(order(block) == wanted, "The native assignment ordering did not retain its requested permutation");
    }
    void answer(std::size_t index, bool correct) {
        const auto& block = lesson.pages[lesson_page_state(view).index].blocks[index];
        if (block.type == curriculum::BlockType::order_code) {
            auto wanted = block.correct_order; if (!correct) std::swap(wanted[0], wanted[1]);
            check(curriculum::evaluate_order(block, wanted).correct == correct, "The assignment order oracle is not discriminating"); arrange(index, wanted);
        } else if (block.type == curriculum::BlockType::multiple_choice) {
            const auto option = correct ? *block.correct_index : (*block.correct_index + block.options.size() - 1) % block.options.size();
            check(curriculum::evaluate_choice(block, option).correct == correct, "The assignment choice oracle is not discriminating");
            click(index, 20 + static_cast<int>(option) * 2);
        } else {
            const auto text = correct ? block.answers.front() : "Respuesta incorrecta de QA";
            check(curriculum::evaluate_text_answer(block, text).correct == correct, "The assignment prediction oracle is not discriminating");
            const auto target = item(index, 2); check(IsWindowEnabled(target), "The actual assignment prediction is locked");
            CHARRANGE all{0, -1}; SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&all));
            const auto content = wide(text); SendMessageW(target, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(content.c_str())); pump();
            check(edit_text(target) == text, "RichEdit changed the literal assignment prediction");
        }
    }
};

struct SourceCoverage { std::size_t controls = 0, normal_controls = 0, references = 0, digits = 0, colored_references = 0; } source_coverage;
CHARFORMAT2W character(HWND target, std::size_t at) {
    CHARRANGE selected{}, one{static_cast<LONG>(at), static_cast<LONG>(at + 1)};
    SendMessageW(target, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selected));
    SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&one));
    CHARFORMAT2W result{}; result.cbSize = sizeof(result);
    SendMessageW(target, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&result));
    SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selected)); return result;
}
bool same_style(const CHARFORMAT2W& before, const CHARFORMAT2W& after, bool pending) {
    const auto effects = pending ? ~DWORD{CFE_AUTOCOLOR} : ~DWORD{0};
    return (before.dwEffects & effects) == (after.dwEffects & effects)
        && before.yHeight == after.yHeight && before.yOffset == after.yOffset
        && before.bCharSet == after.bCharSet && before.bPitchAndFamily == after.bPitchAndFamily
        && std::wstring_view(before.szFaceName) == after.szFaceName && before.wWeight == after.wWeight
        && before.sSpacing == after.sSpacing && before.lcid == after.lcid && before.sStyle == after.sStyle
        && before.wKerning == after.wKerning && before.bUnderlineType == after.bUnderlineType
        && before.bAnimation == after.bAnimation && before.bRevAuthor == after.bRevAuthor;
}
void source(View& fixture, HWND target, std::string_view authored) {
    check(edit_text(target) == visible(std::string(authored)), "A full assignment question/explanation lost text or displayed backticks");
    const auto regions = lesson_reading_regions(fixture.view);
    check(std::ranges::any_of(regions, [&](const auto& region) { return region.target == target && region.source_text == authored; }),
        "The displayed assignment source differs from its complete reader source");
    const auto before = fixture.saved(); const auto shown = wide(edit_text(target)), raw = wide(authored);
    const bool high_contrast = reader_theme::palette(target).high_contrast;
    std::vector<CHARFORMAT2W> original;
    for (std::size_t at = 0; at < shown.size(); ++at) original.push_back(character(target, at));
    const auto references = rich_text_reference_ranges(target);
    std::size_t previous_end = 0, reference_count = 0, digit_count = 0, colored_count = 0;
    for (const auto& [first, last] : references) {
        check(first < last && first >= previous_end && last <= shown.size(), "An assignment reference falls outside its actual displayed UTF-16 text");
        previous_end = last;
    }
    const auto reference_at = [&](std::size_t at) { return std::ranges::any_of(references,
        [&](const auto& range) { return range.first <= at && at < range.second; }); };
    const auto surface = reader_theme::background(target);
    const auto background = [&](const CHARFORMAT2W& style) { return (style.dwEffects & CFE_AUTOBACKCOLOR) ? surface : style.crBackColor; };
    const auto semantic_surface = references.empty() ? surface : background(original[references.front().first]);
    const auto grays = reader_theme::unread_colors(surface, semantic_surface);
    for (std::size_t at = 0; at < shown.size(); ++at) {
        const auto& style = original[at]; const bool reference = reference_at(at), digit = shown[at] >= L'0' && shown[at] <= L'9';
        check(!(style.dwEffects & CFE_UNDERLINE), "An assignment question or feedback acquired a typographic underline");
        // These four activities' questions/guides/explanations have no citation
        // years or chrome counters: every displayed digit is curricular text.
        if (digit) { check(reference, "An assignment curricular number lacks reference ownership"); ++digit_count; }
        if (reference) {
            ++reference_count;
            if (style.crTextColor != reader_theme::foreground(target)) ++colored_count;
            if (!high_contrast)
                check(!(style.dwEffects & CFE_AUTOBACKCOLOR) && style.crBackColor == reader_theme::reference_background(target),
                    "An assignment technical/numeric reference lacks its exact migrated Android neutral background");
        }
    }
    if (authored.find('`') != authored.npos)
        check(reference_count > 0, "An explicitly marked assignment source has no actual technical reference ranges");
    CHARRANGE selection{0, static_cast<LONG>(std::min<std::size_t>(3, shown.size()))};
    SendMessageW(target, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&selection));
    const auto verify_ink = [&](std::size_t read_boundary) {
        check(edit_text(target) == visible(std::string(authored)) && rich_text_reference_ranges(target) == references,
            "Assignment shading changed its text or technical reference ownership");
        for (std::size_t at = 0; at < original.size(); ++at) {
            const auto current = character(target, at); const auto& initial = original[at];
            const bool pending = at >= read_boundary;
            check(same_style(initial, current, pending), "Assignment shading changed a glyph's original style, typeface or dimensions");
            check(background(current) == background(initial)
                && (current.dwEffects & CFE_AUTOBACKCOLOR) == (initial.dwEffects & CFE_AUTOBACKCOLOR)
                && ((initial.dwEffects & CFE_AUTOBACKCOLOR) || current.crBackColor == initial.crBackColor),
                "Assignment reading state changed an original prose or neutral reference background");
            if (pending) {
                const auto original_surface = background(initial);
                const auto gray = original_surface == semantic_surface ? grays.second
                    : original_surface == surface ? grays.first : reader_theme::unread_color(original_surface);
                check(current.crTextColor == gray && !(current.dwEffects & CFE_AUTOCOLOR),
                    "An unread assignment glyph or colored technical phrase escaped the neutral gray policy");
            } else check(current.crTextColor == initial.crTextColor,
                "A read/restored assignment glyph did not recover its exact original category ink");
        }
        CHARRANGE current{}; SendMessageW(target, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&current));
        check(current.cpMin == selection.cpMin && current.cpMax == selection.cpMax,
            "Inspecting assignment ink or applying reading shade changed the native selection");
    };
    {
        ReadingShade shade(target, std::string(authored));
        check(shade.matches(target, authored), "The actual assignment text cannot map its original narration source");
        shade.read_through(0); verify_ink(0);
        // This source projection is independent of ReadingShade's map: these
        // exact questions/feedback hide only backticks and normalize CRLF.
        const auto midpoint = raw.size() / 2; std::size_t displayed_midpoint = 0;
        for (std::size_t at = 0; at < midpoint; ++at)
            if (raw[at] != L'`' && !(raw[at] == L'\r' && at + 1 < raw.size() && raw[at + 1] == L'\n')) ++displayed_midpoint;
        check(displayed_midpoint > 0 && displayed_midpoint < shown.size(), "An assignment source lacks a genuine partial-reading boundary");
        shade.read_through(midpoint); verify_ink(displayed_midpoint);
        shade.read_through(0); verify_ink(0);
        shade.read_through(raw.size()); verify_ink(shown.size());
        // Restore while genuinely dimmed too; a full-read restore alone would
        // not exercise restoration of the pending suffix.
        shade.read_through(0); verify_ink(0); shade.restore(); verify_ink(shown.size());
    }
    CHARRANGE after{}; SendMessageW(target, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
    check(after.cpMin == selection.cpMin && after.cpMax == selection.cpMax && edit_text(target) == visible(std::string(authored))
        && fixture.saved() == before, "Source shading changed assignment text, selection or student state");
    ++source_coverage.controls; source_coverage.normal_controls += !high_contrast;
    source_coverage.references += reference_count; source_coverage.digits += digit_count;
    source_coverage.colored_references += colored_count;
}
void feedback(View& fixture, std::size_t index, const curriculum::Block& block, int phase) {
    const auto target = fixture.item(index, 4); const auto regions = lesson_reading_regions(fixture.view);
    if (phase < 2) {
        check(edit_text(target).empty() && std::ranges::none_of(regions, [&](const auto& region) { return region.target == target; }),
            "An empty/draft assignment exposed a verdict or hidden success explanation"); return;
    }
    const bool correct = phase == 3;
    const auto response = state::get_response(fixture.saved(), fixture.lesson, block);
    check(response.has_value(), "The actual assignment verdict has no stored response");
    const auto guide = curriculum::incorrect_feedback(block, block.type == curriculum::BlockType::multiple_choice
        ? std::optional<std::size_t>(static_cast<std::size_t>(response->selected)) : std::nullopt);
    const auto raw = std::string(correct ? "✓ Correcto\n\n" : "✗ Incorrecto\n\n") + (correct ? block.explanation : guide);
    check(!guide.empty() && edit_text(target) == visible(raw), "The full assignment guide/explanation is missing or attributed to another result");
    check(std::ranges::any_of(regions, [&](const auto& region) { return region.target == target && region.feedback
        && region.correct == correct && region.solved == correct && region.source_text == raw; }),
        "The assignment verdict has the wrong reader source or exercise boundary");
    if (!correct) check(raw.find(block.explanation) == std::string::npos, "An incorrect assignment disclosed its success explanation");
    source(fixture, target, raw);
}
std::string short_text(std::string_view value) {
    auto text = wide(value); if (text.size() > 180) text.resize(180);
    auto result = utf8(text); std::string escaped;
    for (const auto character : result) {
        if (character == '\n') escaped += "\\n";
        else if (character == '\r') escaped += "\\r";
        else if (character == '\t') escaped += "\\t";
        else escaped += character;
    }
    return escaped;
}
void print_projection(std::string_view label, std::size_t index, const ReadingProjection& value) {
    std::cout << "ARCHIVE " << label << '[' << index << "] target=" << value.target_id << " block=" << value.block
        << " key=" << value.key << " anchor=" << value.anchor << " context=" << value.context << " math=" << value.math_mode
        << " flags=" << value.feedback << ',' << value.correct << ',' << value.is_exercise << ',' << value.solved
        << ',' << value.passive_label << ',' << value.required_compiler << " successor=" << value.retired_successor
        << " source=" << short_text(value.source) << '\n';
}
void previous_script(View& fixture, std::string_view page, const std::vector<ReadingProjection>& expected, const Plan& plan, int phase) {
    const auto before = fixture.saved(); const auto current = project(lesson_reading_regions(fixture.view));
    const auto archives = editorial_lesson_reading_archives(fixture.view);
    const auto found = std::ranges::find_if(archives, [&](const auto& archive) { return archive.page_identity == page && project(archive.regions) == expected; });
    std::set<std::string> changed;
    for (const auto& old : expected) {
        const auto actual = std::ranges::find(current, old.key, &ReadingProjection::key);
        check(actual != current.end(), "An editorial assignment revision lost an actual reader anchor");
        if (old.source != actual->source) changed.insert(old.key);
    }
    if (changed.empty()) {
        // An unanswered/rejected p8 has never exposed its success explanation.
        // Revising only that hidden field leaves the complete visible script
        // unchanged, so it needs no predecessor archive or new audio boundary.
        check(current == expected, "A hidden assignment explanation changed the actual unchanged reader script");
        check(std::ranges::all_of(archives, [&](const auto& archive) {
            return archive.page_identity == page && project(archive.regions) == expected && archive.revised_regions.empty();
        }), "An unchanged assignment script fabricated archived feedback or revised audio regions");
        check(fixture.saved() == before, "Checking an unchanged assignment reader script altered student state");
        return;
    }
    if (found == archives.end()) {
        std::cout << "ARCHIVE missing identity=" << plan.identity << " phase=" << phase << " page=" << plan.page
            << " block=" << plan.block << " expected_regions=" << expected.size() << " current_regions=" << current.size()
            << " archives=" << archives.size() << " expected_page=" << page << '\n';
        for (const auto& key : changed) std::cout << "ARCHIVE expected revised=" << key << '\n';
        for (std::size_t candidate = 0; candidate < std::min<std::size_t>(archives.size(), 8); ++candidate) {
            const auto& archive = archives[candidate]; const auto actual = project(archive.regions);
            std::cout << "ARCHIVE candidate=" << candidate << " page_equal=" << (archive.page_identity == page)
                << " regions=" << actual.size() << " revised=" << archive.revised_regions.size() << '\n';
            for (const auto& key : archive.revised_regions) std::cout << "ARCHIVE candidate revised=" << key << '\n';
            unsigned shown = 0;
            for (std::size_t index = 0; index < std::max(expected.size(), actual.size()) && shown < 6; ++index) {
                if (index < expected.size() && index < actual.size() && expected[index] == actual[index]) continue;
                ++shown;
                if (index < expected.size() && index < actual.size()) {
                    const auto& old = expected[index]; const auto& now = actual[index];
                    std::cout << "ARCHIVE diff index=" << index << " equality key=" << (old.key == now.key)
                        << " anchor=" << (old.anchor == now.anchor) << " source=" << (old.source == now.source)
                        << " context=" << (old.context == now.context) << " math=" << (old.math_mode == now.math_mode)
                        << " target=" << (old.target_id == now.target_id) << " block=" << (old.block == now.block)
                        << " feedback=" << (old.feedback == now.feedback) << " correct=" << (old.correct == now.correct)
                        << " exercise=" << (old.is_exercise == now.is_exercise) << " solved=" << (old.solved == now.solved)
                        << " passive=" << (old.passive_label == now.passive_label) << " required=" << (old.required_compiler == now.required_compiler)
                        << " successor=" << (old.retired_successor == now.retired_successor) << '\n';
                }
                if (index < expected.size()) print_projection("expected", index, expected[index]);
                if (index < actual.size()) print_projection("actual", index, actual[index]);
            }
        }
    }
    check(found != archives.end(), "The authenticated assignment archive does not reconstruct its exact actual prior controls/script");
    check(std::set<std::string>(found->revised_regions.begin(), found->revised_regions.end()) == changed,
        "Assignment archives omitted a revised source or marked unchanged guides/passages as rewritten");
    check(fixture.saved() == before, "Reconstructing assignment reading history changed student state");
}
void closed_loader_annotation(const curriculum::Lesson& before, const curriculum::Lesson& after) {
    using namespace winrt::Windows::Data::Json;
    const auto copy = [&] { return JsonObject::Parse(winrt::to_hstring(after.raw_json)); };
    const auto block = [](const JsonObject& document, std::size_t page, std::size_t index) {
        return document.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(page))
            .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(index));
    };
    const auto refresh_terminal = [](const JsonObject& document, std::string_view identity, const JsonObject& current) {
        const auto proofs = document.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
        for (unsigned index = proofs.Size(); index > 0; --index) {
            const auto proof = proofs.GetObjectAt(index - 1);
            if (winrt::to_string(proof.GetNamedString(L"id")) != identity) continue;
            proof.Insert(L"afterSha256", JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(current)))); return;
        }
        throw std::runtime_error("A negative assignment fixture lost its terminal proof");
    };
    std::size_t checked = 0, generic = 0;
    const auto no_annotation = [&](const JsonObject& document, std::string_view locale = "es", bool must_load = false) {
        std::optional<curriculum::Lesson> parsed;
        try { parsed = windows::parse_lesson_json(winrt::to_string(document.Stringify()), locale); }
        catch (const std::exception&) { if (must_load) throw; ++checked; return; }
        catch (const winrt::hresult_error&) { if (must_load) throw; ++checked; return; }
        check(parsed->state_layout.reading_revisions.empty(), "A different owner/locale/field/payload/proof acquired the closed assignment reading archive");
        ++checked; if (must_load) ++generic;
    };
    check(before.state_layout.reading_revisions.empty() && after.state_layout.reading_revisions.size() == 4,
        "The exact assignment suffix did not derive its four authenticated preceding stages");
    {
        // Work may publish unrelated prose before integrating the six closed
        // fields. Preserve that newer prefix without falling back to live
        // assets or pretending the four reviewed payloads changed with it.
        auto extended = copy(); auto paragraph = block(extended, 3, 1);
        check(paragraph.GetNamedString(L"type") == L"paragraph", "The extra-prefix fixture lost its prose owner");
        const auto original = test::compile_check_test_fingerprint(paragraph);
        const auto prose = winrt::to_string(paragraph.GetNamedString(L"text"));
        paragraph.Insert(L"text", JsonValue::CreateStringValue(winrt::to_hstring(prose
            + "\n\nAclaración editorial adicional para comprobar un prefijo vigente.")));
        JsonObject proof;
        proof.Insert(L"id", JsonValue::CreateStringValue(L"block_0003_0001"));
        proof.Insert(L"beforeSha256", JsonValue::CreateStringValue(winrt::to_hstring(original)));
        proof.Insert(L"afterSha256", JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(paragraph))));
        const auto layout = extended.GetNamedObject(L"stateLayout");
        const auto published = layout.GetNamedArray(L"editorial");
        check(published.Size() == 24, "The pinned assignment fixture lost its twenty-plus-four editorial prefix");
        JsonArray enlarged;
        for (unsigned index = 0; index < 20; ++index) enlarged.Append(published.GetAt(index));
        enlarged.Append(proof);
        for (unsigned index = 20; index < published.Size(); ++index) enlarged.Append(published.GetAt(index));
        layout.Insert(L"editorial", enlarged);
        const auto with_prefix = windows::parse_lesson_json(winrt::to_string(extended.Stringify()), "es");
        check(with_prefix.state_layout.reading_revisions.size() == after.state_layout.reading_revisions.size(),
            "An unrelated preceding prose edit hid the four closed assignment reading stages");
        for (std::size_t index = 0; index < after.state_layout.reading_revisions.size(); ++index) {
            const auto& original_stage = after.state_layout.reading_revisions[index];
            const auto& moved_stage = with_prefix.state_layout.reading_revisions[index];
            check(moved_stage.editorial_prefix == original_stage.editorial_prefix + 1
                && moved_stage.previous_blocks == original_stage.previous_blocks,
                "A newer editorial prefix changed the authenticated assignment block snapshots or their exact prefix positions");
        }
        std::cout << "PASS assignment reading provenance after a valid twenty-first preceding prose edge\n";
    }
    {
        auto altered = copy(); altered.Insert(L"id", JsonValue::CreateStringValue(L"another_assignment_owner")); no_annotation(altered);
    }
    {
        auto altered = copy(); altered.GetNamedObject(L"stateLayout").Insert(L"locale", JsonValue::CreateStringValue(L"en")); no_annotation(altered, "en");
    }
    {
        auto altered = copy(); auto current = block(altered, 2, 3);
        current.Insert(L"id", JsonValue::CreateStringValue(L"another_activity_owner")); refresh_terminal(altered, "block_0002_0003", current); no_annotation(altered);
    }
    {
        auto altered = copy(); auto current = block(altered, 2, 3);
        current.Insert(L"type", JsonValue::CreateStringValue(L"fill_code")); refresh_terminal(altered, "block_0002_0003", current); no_annotation(altered);
    }
    {
        auto altered = copy(); const auto current = altered.GetNamedObject(L"stateLayout").GetNamedArray(L"current").GetObjectAt(2).GetNamedArray(L"blocks");
        current.SetAt(3, JsonValue::CreateStringValue(L"foreign_block_identity")); no_annotation(altered);
    }
    {
        // A different question is legal generic editorial data when its proof
        // is coherent, but it has no permission to borrow these six old texts.
        auto altered = copy(); auto current = block(altered, 2, 3);
        current.Insert(L"question", JsonValue::CreateStringValue(L"Pregunta editorial distinta, sin autorización para recuperar otro audio."));
        refresh_terminal(altered, "block_0002_0003", current); no_annotation(altered, "es", true);
    }
    {
        // Guidance remains generically editable. Its complete block payload
        // must still prevent it from masquerading as the reviewed publication.
        auto altered = copy(); auto current = block(altered, 2, 3);
        current.Insert(L"reviewFeedback", JsonValue::CreateStringValue(L"Otra guía editorial válida para una prueba aislada."));
        refresh_terminal(altered, "block_0002_0003", current); no_annotation(altered, "es", true);
    }
    {
        auto altered = copy(); auto current = block(altered, 4, 1);
        current.GetNamedArray(L"answers").SetAt(0, JsonValue::CreateStringValue(L"Otra respuesta no autorizada"));
        refresh_terminal(altered, "block_0004_0001", current); no_annotation(altered);
    }
    {
        auto altered = copy(); const auto proofs = altered.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
        proofs.RemoveAtEnd(); no_annotation(altered);
    }
    {
        auto altered = copy(); const auto proofs = altered.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
        proofs.GetObjectAt(proofs.Size() - 1).Insert(L"afterSha256", JsonValue::CreateStringValue(winrt::to_hstring(std::string(64, '0')))); no_annotation(altered);
    }
    {
        auto altered = copy(); altered.GetNamedObject(L"stateLayout").Insert(L"reading_revisions", JsonArray{}); no_annotation(altered);
    }
    check(checked == 11 && generic == 2, "The closed assignment loader omitted a boundary or rejected generic editorial prose");
    std::cout << "PASS eleven assignment loader boundaries; two coherent generic editorial changes load without acquiring closed reading provenance\n";
}
void run(const std::filesystem::path& before_path, const std::filesystem::path& after_path) {
    const auto before_bytes = state::read_bytes(before_path), after_bytes = state::read_bytes(after_path);
    check(state::sha256(before_bytes) == before_sha && state::sha256(after_bytes) == after_sha,
        "The native assignment fixture differs from its complete independently pinned Git/candidate bytes");
    const auto before = windows::load_lesson(before_path), after = windows::load_lesson(after_path);
    check(before.id == lesson_id && after.id == lesson_id && before.locale == "es" && after.locale == "es"
        && before.state_layout.version == 2 && after.state_layout.version == 2
        && before.state_layout.lineage == after.state_layout.lineage && before.state_layout.signatures == after.state_layout.signatures,
        "The six assignment clarifications changed their Spanish owner, family or response contracts");
    closed_loader_annotation(before, after);
    std::size_t groups = 0;
    for (const auto& plan : plans) {
        const auto at = curriculum::stable_block_position(before, plan.identity), next = curriculum::stable_block_position(after, plan.identity);
        check(at && next && *at == *next && at->page == plan.page && at->block == plan.block,
            "An exact assignment clarification moved from its original identity/address");
        const auto& old = before.pages[at->page].blocks[at->block]; const auto& revised = after.pages[next->page].blocks[next->block];
        check(old.type == plan.type && revised.type == plan.type && old.id == std::string(lesson_id) + plan.suffix && old.id == revised.id
            && old.answers == revised.answers && old.options == revised.options && old.correct_index == revised.correct_index
            && old.fragments == revised.fragments && old.correct_order == revised.correct_order && old.code == revised.code
            && old.review_feedback == revised.review_feedback && old.review_feedback_options == revised.review_feedback_options
            && curriculum::exercise_key(before, old) == curriculum::exercise_key(after, revised)
            && state::response_storage_key(before, old) == state::response_storage_key(after, revised),
            "An assignment text clarification changed an answer, guide, program, activity ID or storage key");
        for (int phase = 0; phase < 4; ++phase) {
            std::cout << "GROUP assignment identity=" << plan.identity << " phase=" << phase << '\n';
            View fixture(before, at->page); source(fixture, fixture.item(at->block, 0), old.question);
            if (phase > 0) fixture.answer(at->block, phase == 3);
            if (phase > 1) fixture.click(at->block, 3);
            feedback(fixture, at->block, old, phase);
            const auto saved = fixture.saved(); const auto answer = state::get_response(saved, before, old);
            check(phase == 0 ? !answer : (answer && answer->correct == (phase == 3) && state::response_reviewed(*answer, old) == (phase == 2)),
                "The previous actual assignment input did not create its intended empty/draft/rejected/correct state");
            const auto history = narration::read_history(saved); const auto available = attempts::balance(saved, "programming", now_ms()).available;
            const auto previous_page = lesson_page_state(fixture.view).narration_page_identity;
            const auto previous_regions = project(lesson_reading_regions(fixture.view));
            const auto old_order = old.type == curriculum::BlockType::order_code ? fixture.order(at->block) : std::vector<std::size_t>{};
            fixture.lesson = after; fixture.reopen();
            const auto verify = [&] {
                const auto current = fixture.saved(); const auto response = state::get_response(current, after, revised);
                check(same_response(answer, response), "The assignment editorial adoption rewrote its exact response/review/opaque JSON");
                check(current.preferences.at("learning_progress") == saved.preferences.at("learning_progress"),
                    "Assignment editorial adoption changed responses, progress, original addresses or family metadata");
                check(narration::read_history(current) == history && current.preferences.at("assignment_editorial_opaque_qa") == saved.preferences.at("assignment_editorial_opaque_qa")
                    && attempts::balance(current, "programming", now_ms()).available == available,
                    "Assignment adoption changed stored pauses, unrelated preferences or attempt balance");
                const auto original_draft = state::get_draft(saved, fixture.draft_owner, ""), current_draft = state::get_draft(current, fixture.draft_owner, "");
                check(original_draft.source == current_draft.source && original_draft.input == current_draft.input
                    && original_draft.indentation_json == current_draft.indentation_json && original_draft.selection_start == current_draft.selection_start
                    && original_draft.selection_end == current_draft.selection_end && original_draft.has_saved == current_draft.has_saved,
                    "Assignment adoption changed an independent compiler source/input/indentation/caret");
                for (const auto& source_page : before.pages) for (const auto& compiler : source_page.blocks)
                    if (compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check)
                        check(windows::compiler_puzzle_case_count(saved, before, compiler) == windows::compiler_puzzle_case_count(current, after, compiler),
                            "Assignment text adoption changed its explicitly synthetic prerequisite receipts");
                source(fixture, fixture.item(at->block, 0), revised.question); feedback(fixture, at->block, revised, phase);
                if (old.type == curriculum::BlockType::order_code) check(fixture.order(at->block) == old_order, "Assignment adoption moved the literal ordered answer");
                if (old.type == curriculum::BlockType::output_prediction && answer)
                    check(edit_text(fixture.item(at->block, 2)) == answer->answer, "Assignment adoption changed the literal prediction input");
                if (old.type == curriculum::BlockType::multiple_choice && answer)
                    check(SendMessageW(fixture.item(at->block, 20 + static_cast<int>(answer->selected) * 2), BM_GETCHECK, 0, 0) == BST_CHECKED,
                        "Assignment adoption selected another answer");
                if (phase == 3) check(!IsWindowEnabled(fixture.item(at->block, 3)), "A previously correct assignment became checkable after text adoption");
                previous_script(fixture, previous_page, previous_regions, plan, phase);
            };
            verify(); fixture.reopen(); verify();
            if (phase < 3) {
                if (phase != 2) { fixture.answer(at->block, false); fixture.click(at->block, 3); feedback(fixture, at->block, revised, 2); }
                const auto rejected = state::get_response(fixture.saved(), after, revised);
                fixture.reopen(); feedback(fixture, at->block, revised, 2);
                check(same_response(rejected, state::get_response(fixture.saved(), after, revised)), "Reopening changed a real current assignment rejection");
                fixture.answer(at->block, true); fixture.click(at->block, 3); feedback(fixture, at->block, revised, 3);
                const auto accepted = state::get_response(fixture.saved(), after, revised);
                fixture.reopen(); feedback(fixture, at->block, revised, 3);
                check(accepted && accepted->correct && same_response(accepted, state::get_response(fixture.saved(), after, revised)),
                    "The actual current assignment success/explanation did not survive reopening");
            }
            ++groups;
        }
    }
    check(groups == 16 && state::read_bytes(before_path) == before_bytes && state::read_bytes(after_path) == after_bytes,
        "The assignment editorial regression changed its immutable fixtures or omitted a state");
    check(source_coverage.controls > 0 && source_coverage.references > 0 && source_coverage.digits > 0
        && (!source_coverage.normal_controls || source_coverage.colored_references > 0),
        "Assignment shading coverage omitted actual numeric, technical or originally colored references");
    std::cout << "PASS assignment source ink: " << source_coverage.controls << " actual question/feedback checks, "
        << source_coverage.references << " reference glyphs including " << source_coverage.digits << " curricular digits and "
        << source_coverage.colored_references << " originally colored glyphs; neutral Android backgrounds outside high contrast without underline, pending gray, partial/rewound/full-read and pending restoration, exact styles/text/selection/state\n";
    std::cout << "PASS U11L20 six exact editorial fields: sixteen real native empty/draft/rejected/correct groups; original responses, guides, answers, compiler draft/prerequisites, stored three-page history and attempts retained; full current questions/feedback, exact prior reading scripts/revised anchors, retry and reopen. Synthetic prerequisites/storage bookmarks only; no program execution, PCM, human listening, historical import or full curricular traversal claimed.\n";
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    if (argc != 3) { std::cerr << "Expected immutable before/after assignment fixture paths\n"; return 2; }
    const auto apartment = OleInitialize(nullptr); if (FAILED(apartment)) return 2;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    const auto rich = LoadLibraryW(L"Msftedit.dll"); int result = 0;
    try { check(rich != nullptr, "Cannot load native RichEdit"); reader_theme::prepare_fonts(); run(argv[1], argv[2]); }
    catch (const std::exception& error) { std::cerr << "FAIL U11 assignment editorial view: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich); OleUninitialize(); return result;
}

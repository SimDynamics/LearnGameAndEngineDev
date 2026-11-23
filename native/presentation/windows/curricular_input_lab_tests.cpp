#include "core/lesson.hpp"
#include "core/attempts.hpp"
#include "core/profile.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/compiler_diagnostic_display.hpp"
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
#include <iostream>
#include <set>
#include <span>
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
std::string escaped_diagnostic_text(std::string_view value) {
    return winrt::to_string(winrt::Windows::Data::Json::JsonValue::CreateStringValue(wide(value)).Stringify());
}
std::string display_line_breaks(std::string_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\r') {
            result += '\n';
            if (index + 1 < value.size() && value[index + 1] == '\n') ++index;
        } else result += value[index];
    }
    return result;
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

const curriculum::Block& active_block(const curriculum::Lesson& lesson, std::string_view identity) {
    const auto at = position(lesson, identity);
    return lesson.pages.at(at.page).blocks.at(at.block);
}
const curriculum::Block& archived_compiler(const curriculum::Lesson& lesson, std::string_view identity) {
    check(!curriculum::stable_block_position(lesson, identity) && curriculum::retired_block(lesson, identity),
        "A historical compiler was restored to the active lesson or lost its retirement");
    const auto found = std::ranges::find(lesson.retired_blocks, identity, &curriculum::Block::stable_id);
    check(found != lesson.retired_blocks.end() && found->type == curriculum::BlockType::cpp_compiler
        && found->output_check && !found->output_check->cases.empty(),
        "An authenticated retired compiler lost its executable contract");
    bool witnessed = false;
    for (const auto& history : lesson.state_layout.retirement_history)
        if (std::ranges::find(history.retired, identity) != history.retired.end())
            for (const auto& page : history.pages) for (const auto& previous : page.blocks)
                if (previous.stable_id == identity) {
                    check(previous.initial_code == found->initial_code && previous.standard_input == found->standard_input
                        && previous.output_check && previous.output_check->cases.size() == found->output_check->cases.size()
                        && curriculum::same_output_check_runtime_requirements(*previous.output_check, *found->output_check)
                        && curriculum::draft_key("programming", lesson, previous) == curriculum::draft_key("programming", lesson, *found),
                        "The retired compiler does not match its authenticated publication and storage owner");
                    for (std::size_t index = 0; index < previous.output_check->cases.size(); ++index)
                        check(curriculum::same_output_case_requirements(previous.output_check->cases[index], found->output_check->cases[index])
                            && previous.output_check->cases[index].explanation == found->output_check->cases[index].explanation,
                            "An archived input, output or explanation changed after retirement");
                    witnessed = true;
                }
    check(witnessed, "The retired compiler has no authenticated publication snapshot");
    return *found;
}

std::string crt_stdout(std::string_view lf) {
    std::string result;
    for (const char item : lf) { if (item == '\n') result += '\r'; result += item; }
    return result;
}

struct U3Case { std::string_view input, output; };
constexpr std::array<U3Case, 8> combinations_cases{{
    {"1 0 0", "Entrada denegada"}, {"0 0 0", "Entrada denegada"},
    {"0 0 1", "Entrada denegada"}, {"0 1 0", "Entrada denegada"},
    {"0 1 1", "Entrada permitida"}, {"1 0 1", "Entrada permitida"},
    {"1 1 0", "Entrada denegada"}, {"1 1 1", "Entrada permitida"}
}};
constexpr std::array<U3Case, 7> repartition_cases{{
    {"28 0", "0\nCapacidad invalida"}, {"28 -1", "0\nCapacidad invalida"},
    {"28 4", "1\nGrupos: 7"}, {"28 6", "0\nGrupos: 4"},
    {"0 4", "1\nGrupos: 0"}, {"1 4", "0\nGrupos: 0"},
    {"120 12", "1\nGrupos: 10"}
}};

void u3_contract(const curriculum::Lesson& lesson, bool combinations) {
    const auto& compiler = active_block(lesson, combinations ? "p1u3l5_construir_acceso" : "p1u3l6_construir_reparto");
    const auto prerequisite = position(lesson, combinations ? "p1u3l5_reparar_parentesis" : "p1u3l6_reparar_resto");
    const auto& reference = active_block(lesson, combinations ? "p1u3l5_ejemplo_acceso" : "p1u3l6_ejemplo_reparto");
    const std::span<const U3Case> cases = combinations ? std::span<const U3Case>{combinations_cases}
        : std::span<const U3Case>{repartition_cases};
    check(compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == cases.size() && compiler.standard_input == cases.front().input
        && !compiler.initial_code.empty() && compiler.initial_code != reference.code && lesson.compiler_puzzles_required
        && reference.type == curriculum::BlockType::code && !reference.code.empty()
        && curriculum::activity_prerequisites(lesson, compiler) == std::vector<curriculum::StatePosition>{prerequisite}
        && curriculum::reference_activity(lesson, reference) == curriculum::stable_block_position(lesson, compiler.stable_id),
        "The active U3 construction lost its unfinished draft, first case, prerequisite or own reference");
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& item = compiler.output_check->cases[index];
        check(item.standard_input == cases[index].input
            && item.outputs == std::vector<std::string>{std::string(cases[index].output), std::string(cases[index].output) + '\n'},
            "An active U3 input/output contract or optional final LF changed");
    }
    const auto& original = archived_compiler(lesson, "block_0003_0002");
    const auto& input = archived_compiler(lesson, "block_0003_0007");
    const auto& extra = archived_compiler(lesson, combinations ? "combinaciones_faltantes_editor" : "cero_y_totales_validos_editor");
    check(original.output_check->cases.size() == 1 && input.output_check->cases.size() == (combinations ? 5 : 4)
        && extra.output_check->cases.size() == (combinations ? 3 : 4)
        && original.source_format_original_initial_code && input.source_format_original_initial_code
        && curriculum::draft_key("programming", lesson, compiler) != curriculum::draft_key("programming", lesson, extra),
        "The historical U3 editors lost format provenance, independent cases or storage identity");
}

std::filesystem::path combinations_asset() {
    return std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_combinar_condiciones.json";
}
void combinations_contract(const curriculum::Lesson& lesson) {
    u3_contract(lesson, true);
}



// Quizzes are prepared from their authored answers to isolate compiler UI.
// U2L9 prepares earlier pages; U3 also prepares the focal page's quizzes.
// This is explicit QA setup, not evidence of quiz interaction. Compiler
// prerequisites are observed separately through actual native executions.
void prepare_u2l9_prior_quizzes(state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target, bool include_target_page = false) {
    using curriculum::BlockType;
    using namespace winrt::Windows::Data::Json;
    for(std::size_t p=0;p<target.page+(include_target_page?1:0);++p) {
        for(const auto& block:lesson.pages[p].blocks) {
            state::Response response;
            if(block.type==BlockType::multiple_choice) {
                check(block.correct_index.has_value(),"An earlier QA choice has no authored answer");
                response.selected=static_cast<std::int64_t>(*block.correct_index);
                response.correct=curriculum::evaluate_choice(block,*block.correct_index).correct;
            } else if(block.type==BlockType::fill_code||block.type==BlockType::output_prediction) {
                check(!block.answers.empty(),"An earlier QA answer is missing");
                response.answer=block.answers.front();
                response.correct=curriculum::evaluate_text_answer(block,response.answer).correct;
            } else if(block.type==BlockType::inline_fill_code) {
                JsonArray answers;
                for(const auto& answer:block.answers)answers.Append(JsonValue::CreateStringValue(wide(answer)));
                JsonObject raw;raw.Insert(L"answers",answers);
                response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_hole_answers(block,block.answers).correct;
            } else if(block.type==BlockType::drag_fill_code) {
                JsonArray slots;
                for(const auto slot:block.correct_order)slots.Append(JsonValue::CreateNumberValue(static_cast<double>(slot)));
                JsonObject raw;raw.Insert(L"slots",slots);
                response.raw_json=winrt::to_string(raw.Stringify());
                response.correct=curriculum::evaluate_drag_answers(block,block.correct_order).correct;
            } else {
                check(block.type!=BlockType::order_code&&block.type!=BlockType::math_puzzle,
                    "A new earlier U2L9 exercise needs an explicitly audited QA preparation");
                continue;
            }
            check(response.correct,"An authored earlier-page QA response is invalid");
            state::put_response(data,lesson,block,response);
        }
        (void)state::mark_page_reached(data,lesson,p);
    }
}

// Real prerequisite observations for the current and authenticated archived
// U2L9 publications. Only the first case earns its receipt; an independent
// boxes input is observed without turning it into another required case.
void prepare_u2l9_prerequisites(state::StateData& data,const curriculum::Lesson& lesson,
        curriculum::StatePosition target,const std::filesystem::path& projects) {
    check(lesson.id=="cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida"&&
          lesson.compiler_puzzles_required,"U2L9 prerequisite QA requires its actual published policy");
    const auto source_at=[&](std::string_view identity)->std::string {
        const auto at=curriculum::stable_block_position(lesson,identity);
        check(at.has_value(),"An authored U2L9 prerequisite reference is missing");
        const auto& block=lesson.pages.at(at->page).blocks.at(at->block);
        check(!block.code.empty(),"An authored U2L9 prerequisite source is empty");
        return block.code;
    };
    static std::uint64_t request_id=90000;
    for(std::size_t p=0;p<=target.page;++p)
        for(std::size_t b=0;b<lesson.pages[p].blocks.size();++b) {
            if(p==target.page&&b>=target.block)break;
            const auto& owner=lesson.pages[p].blocks[b];
            if(owner.type!=curriculum::BlockType::cpp_compiler)continue;
            check(owner.output_check&&!owner.output_check->cases.empty(),
                "A preceding U2L9 compiler has no independent output contract");
            const bool boxes=owner.stable_id=="p1u2l9_reparar_lectura_de_cajas"||
                owner.stable_id=="block_0006_0002";
            std::string source;
            if(boxes)source=source_at("block_0007_0001");
            else if(owner.stable_id=="block_0008_0008")source=owner.initial_code;
            else if(owner.stable_id=="block_0009_0003")source=source_at("block_0010_0001");
            else if(owner.stable_id=="block_0011_0006")source=source_at("block_0012_0002");
            else check(false,"U2L9 QA must explicitly audit a newly added prerequisite");
            check(windows::compiler_puzzle_case_count(data,lesson,owner)==0&&
                !windows::compiler_puzzle_completed(data,lesson,owner),
                "A supposedly fresh U2L9 prerequisite already has credit");
            const std::size_t observations=boxes?2:1;
            check(owner.output_check->cases.size()>=observations,
                "The independent boxes input disappeared from its original contract");
            for(std::size_t index=0;index<observations;++index) {
                const auto& item=owner.output_check->cases[index];
                windows::CppRunRequest request;
                request.request_id=++request_id;request.revision=request_id;
                request.project_key=curriculum::draft_key("programming",lesson,owner);
                request.projects_directory=projects;request.source_utf8=source;
                request.stdin_utf8=item.standard_input;
                request.compile_timeout=std::chrono::seconds(60);
                request.execution_timeout=std::chrono::seconds(8);
                check(windows::compiler_runtime_request_allowed(owner,request),
                    "The prerequisite source or input does not match its own U2L9 owner");
                const auto result=windows::run_cpp(request);
                const auto captured=windows::capture_cpp_execution(request,result);
                check(result.error_utf8.empty()&&result.compilation.succeeded()&&
                    result.execution.succeeded()&&result.execution.stderr_bytes.empty()&&
                    windows::evaluate_cpp_output_check(*owner.output_check,captured,request.revision).verdict==
                        curriculum::OutputVerdict::correct,
                    "A real U2L9 prerequisite failed its independently authored input/output");
                if(index==0)check(windows::record_compiler_puzzle_case(
                    data,lesson,owner,request,captured,"Real U2L9 prerequisite execution"),
                    "A real first prerequisite case did not produce its own receipt");
            }
            check(windows::compiler_puzzle_completed(data,lesson,owner)&&
                  windows::compiler_puzzle_case_count(data,lesson,owner)==1,
                "A U2L9 prerequisite required or credited more than its first manual case");
        }
}

void observe_u3_compiler(state::StateData& data, const curriculum::Lesson& lesson,
        const curriculum::Block& owner, std::string_view source, std::size_t index,
        const std::filesystem::path& projects) {
    check(owner.output_check && index < owner.output_check->cases.size(), "Missing actual U3 observation contract");
    static std::uint64_t request_id = 91000;
    windows::CppRunRequest request;
    request.request_id = ++request_id; request.revision = request_id;
    request.project_key = curriculum::draft_key("programming", lesson, owner);
    request.projects_directory = projects; request.source_utf8 = source;
    request.stdin_utf8 = owner.output_check->cases[index].standard_input;
    request.input_check = owner.input_check; request.local_int_check = owner.local_int_check;
    request.local_string_check = owner.local_string_check; request.local_role_check = owner.local_role_check;
    request.compile_timeout = std::chrono::seconds(60); request.execution_timeout = std::chrono::seconds(8);
    check(windows::compiler_runtime_request_allowed(owner, request),
        "The U3 archived/prerequisite observation does not match its own runtime contract");
    const auto result = windows::run_cpp(request);
    const auto captured = windows::capture_cpp_execution(request, result);
    const auto evaluation = windows::evaluate_cpp_output_check(*owner.output_check, captured, request.revision);
    check(result.error_utf8.empty() && result.compilation.succeeded() && result.execution.succeeded()
        && result.execution.stderr_bytes.empty() && evaluation.verdict == curriculum::OutputVerdict::correct,
        "An actual archived/prerequisite U3 program failed its published input/output");
    check(windows::record_compiler_puzzle_case(data, lesson, owner, request, captured,
            "✓ Correcto" + (evaluation.feedback.empty() ? std::string{} : "\n\n" + evaluation.feedback)),
        "The authentic U3 execution could not record its own independent receipt");
}

void prepare_u3_archived_state(state::StateData& data, const curriculum::Lesson& lesson,
        bool combinations, const std::filesystem::path& projects) {
    std::size_t observations = 0;
    for (const auto id : {"block_0003_0002", "block_0003_0007",
            combinations ? "combinaciones_faltantes_editor" : "cero_y_totales_validos_editor"}) {
        const auto& owner = archived_compiler(lesson, id);
        for (std::size_t index = 0; index < owner.output_check->cases.size(); ++index) {
            observe_u3_compiler(data, lesson, owner, owner.initial_code, index, projects);
            ++observations;
        }
        check(windows::compiler_puzzle_completed(data, lesson, owner)
            && windows::compiler_puzzle_case_count(data, lesson, owner) == owner.output_check->cases.size(),
            "A retired U3 contract lost an independently observed case");
        state::Draft draft;
        draft.source = owner.initial_code + "// Independent archived QA draft.\n";
        draft.input = " \t" + owner.standard_input.value_or("") + "\n ";
        draft.selection_start = 12; draft.selection_end = 19; draft.has_saved = true;
        state::put_draft(data, curriculum::draft_key("programming", lesson, owner), draft);
    }
    check(observations == 9, "The U3 historical laboratories lost one of their nine published observations");
    std::cout << "PASS " << (combinations ? "U3L5" : "U3L6")
        << " archives: nine real MSVC observations of three retired contracts; independent receipts and QA drafts retained\n";
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
    if (u3 || combinations) u3_contract(lesson, combinations);
    const auto target = position(lesson, combinations ? "p1u3l5_construir_acceso"
        : u3 ? "p1u3l6_construir_reparto" : "block_0009_0003");
    const auto reference = (u3 || combinations) ? target : position(lesson, "block_0010_0001");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& authored_reference = lesson.pages[reference.page].blocks[reference.block];
    check(compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == (combinations ? 8 : u3 ? 7 : 4)
        && lesson.compiler_puzzles_required
        && ((u3 || combinations) || curriculum::activity_prerequisites(lesson, compiler).empty()),
        "The visual QA target lost its actual first-case compiler contract");
    check(combinations ? compiler.standard_input == "1 0 0"
        : u3 ? compiler.standard_input == "28 0" && !compiler.initial_code.empty()
        : compiler.stable_id == "block_0009_0003" && authored_reference.stable_id == "block_0010_0001"
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
    if (!u3 && !combinations) {
        prepare_u2l9_prior_quizzes(data, lesson, target);
        prepare_u2l9_prerequisites(data, lesson, target, environment.paths.projects);
    } else {
        prepare_u2l9_prior_quizzes(data, lesson, target, true);
        const auto& prerequisite = active_block(lesson, combinations ? "p1u3l5_reparar_parentesis" : "p1u3l6_reparar_resto");
        const auto& source = active_block(lesson, combinations ? "p1u3l5_ejemplo_reparacion" : "p1u3l6_ejemplo_resto");
        observe_u3_compiler(data, lesson, prerequisite, source.code, 0, environment.paths.projects);
    }
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
    // U3 keeps its original initialCode. U2L9's preceding boxes editor has
    // real independent evidence. Earlier quizzes are explicit QA setup;
    // the focal draft remains uncredited.
    state::Draft draft;
    draft.source = (u3 || combinations) ? compiler.initial_code : authored_reference.code;
    draft.input = *compiler.standard_input;
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    state::put_draft(data, draft_key, draft);
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        const auto at = curriculum::stable_block_position(lesson, block.stable_id);
        if (!at || at->page > target.page || (!(u3 || combinations) && at->page == target.page))
            check(!state::get_response(data, lesson, block), "Visual QA answered a focal or later quiz");
        const bool preceding = at &&
            (at->page < target.page || (at->page == target.page && at->block < target.block));
        if (block.type == curriculum::BlockType::cpp_compiler && !preceding)
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
        << "\nsynthetic_compiler_prerequisites=none\nquiz_responses="
        << ((u3 || combinations) ? "through-focal-page-authored-QA-setup" : "earlier-pages-authored-QA-setup")
        << "\ncompiler_receipts="
        << ((u3 || combinations) ? "real-prerequisite-first-case" : "real-preceding-boxes-first-case")
        << "\ntarget_completed=0\ncompiler_puzzles_required=" << lesson.compiler_puzzles_required
        << "\nwindows_created=0\n";
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
    for (const auto identity : {"p1u3l5_construir_acceso", "p1u3l5_reparar_parentesis",
            "combinaciones_faltantes_editor", "block_0003_0007"}) {
        const auto at = curriculum::stable_block_position(lesson, identity);
        const auto& block = at ? lesson.pages[at->page].blocks[at->block] : archived_compiler(lesson, identity);
        const auto draft = state::get_draft(data, curriculum::draft_key("programming", lesson, block),
            block.initial_code, block.standard_input.value_or(""));
        JsonObject item;
        item.Insert(L"identity", JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        item.Insert(L"case_count", JsonValue::CreateNumberValue(static_cast<double>(windows::compiler_puzzle_case_count(data, lesson, block))));
        item.Insert(L"required_cases", JsonValue::CreateNumberValue(1));
        item.Insert(L"declared_cases", JsonValue::CreateNumberValue(static_cast<double>(block.output_check->cases.size())));
        item.Insert(L"archived", JsonValue::CreateBooleanValue(!at.has_value()));
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
        if (lesson.id == "cpp_fundamentals__objetos_y_estado__leer_una_entrada_conocida") {
            prepare_u2l9_prior_quizzes(initial, lesson, target);
            prepare_u2l9_prerequisites(initial, lesson, target, environment.paths.projects);
        } else if (lesson.id == "cpp_fundamentals__expresiones_y_decisiones__combinar_condiciones"
                || lesson.id == "cpp_fundamentals__expresiones_y_decisiones__validez_y_cortocircuito") {
            prepare_u2l9_prior_quizzes(initial, lesson, target, true);
            prepare_u3_archived_state(initial, lesson,
                lesson.id == "cpp_fundamentals__expresiones_y_decisiones__combinar_condiciones", environment.paths.projects);
        }
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
        check(status == "Ejecución terminada (código 0)." || status.starts_with("Caso correcto. Casos comprobados: ")
            || status.starts_with("Comprobación obligatoria: ") || status.starts_with("Caso ejecutado. "),
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
        const auto stdout_bytes = bytes(directory / L"execution-stdout.bin");
        const auto stderr_bytes = bytes(directory / L"execution-stderr.bin");
        const auto raw_expected = expected_raw.empty() ? expected : expected_raw;
        check(stdout_bytes == raw_expected && stderr_bytes.empty(),
            "Captured program output differs: stdout=" + escaped_diagnostic_text(stdout_bytes) + ", expected=" + escaped_diagnostic_text(raw_expected)
                + ", stderr=" + escaped_diagnostic_text(stderr_bytes));
        std::string console_expected(expected);
        std::string caption_expected = "Salida del programa (stdout)";
        if (expected.empty()) {
            // The UI selects the nonempty compiler channel when a correct
            // native process writes no stdout/stderr. Keep its diagnostic
            // separate from the exact empty program capture checked above.
            const auto compiler_bytes = bytes(directory / L"compilation-stdout.bin")
                + bytes(directory / L"compilation-stderr.bin");
            if (!compiler_bytes.empty()) {
                const std::array roots{directory};
                console_expected = display_line_breaks(utf8(compact_compiler_diagnostic_paths(wide(compiler_bytes), roots)));
                caption_expected = "Diagnósticos de compilación y enlace";
            }
        }
        check(text(14) == console_expected && text(30) == caption_expected,
            "Visible console channel differs: caption=" + escaped_diagnostic_text(text(30)) + ", expected_caption=" + escaped_diagnostic_text(caption_expected)
                + ", text=" + escaped_diagnostic_text(text(14)) + ", expected_text=" + escaped_diagnostic_text(console_expected));
        check(!IsWindowVisible(host), "A laboratory execution exposed the QA host");
        ++executions;
        return directory;
    }
};

void current_three_input_laboratory(const std::filesystem::path& historical = {}) {
    phase = historical.empty() ? "load current U2L9 asset and contracts" : "load authentic archived U2L9";
    const auto path = historical.empty() ? std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/objetos_y_estado/lesson_leer_una_entrada_conocida.json"
        : historical;
    const auto asset_hash = state::sha256(state::read_bytes(path));
    if (!historical.empty())
        check(asset_hash == "61ff67f148ad78c31036763889ebdb85e31ce315cb1103de516ee975819fd0e1",
            "The historical U2L9 fixture is not the exact published d90d3156 source");
    const auto lesson = windows::load_lesson(path);
    const auto target = position(lesson, "block_0009_0003");
    const auto reference = position(lesson, "block_0010_0001");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& authored_reference = lesson.pages[reference.page].blocks[reference.block];
    check(compiler.stable_id == "block_0009_0003" && authored_reference.stable_id == "block_0010_0001"
        && compiler.type == curriculum::BlockType::cpp_compiler && compiler.output_check
        && compiler.output_check->input_comparison == curriculum::TextComparison::tokens
        && compiler.output_check->cases.size() == 4 && compiler.standard_input == "Iris 4 2"
        && authored_reference.type == curriculum::BlockType::code && authored_reference.language == "cpp"
        && !authored_reference.code.empty() && lesson.compiler_puzzles_required,
        "The actual required U2L9 laboratory/reference lost its first case or three optional trials");
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
    check(!navigate_lesson_view(fixture.view, reference.page),
        "An uncredited U2L9 first case opened its later reference");
    fixture.replace(10, authored_reference.code);
    const auto retained = [&](std::size_t count) {
        const auto saved = fixture.saved();
        check(saved.preferences.at("input_lab_qa_unrelated") == unrelated,
            "Input laboratory operations changed an unrelated QA preference");
        check(windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == (count >= 1),
            "Real input executions did not preserve the exact number of independently checked cases");
        for (const auto& page : lesson.pages) for (const auto& other : page.blocks)
            if (other.type == curriculum::BlockType::cpp_compiler && other.stable_id != compiler.stable_id)
                check(windows::compiler_puzzle_case_count(saved, lesson, other) ==
                        windows::compiler_puzzle_case_count(initial, lesson, other)
                    && windows::compiler_puzzle_completed(saved, lesson, other) ==
                        windows::compiler_puzzle_completed(initial, lesson, other)
                    && compiler_receipts(saved, lesson, other) == compiler_receipts(initial, lesson, other),
                    "The focal laboratory changed another compiler's independent receipt");
        check(lesson.compiler_puzzles_required, "QA changed the published required compiler policy");
    };
    for (std::size_t index = 0; index < cases.size(); ++index) {
        phase = "case " + std::to_string(index + 1) + ": " + std::string(cases[index].canonical);
        fixture.replace(15, cases[index].input);
        fixture.run(authored_reference.code, cases[index].input, cases[index].output);
        retained(index + 1);
        check(fixture.text(4) == "✓ Correcto",
            "A successful first case was withheld pending optional input trials");
        if (index == 0) {
            check(fixture.text(34).find("Comprobación obligatoria: 1 de 1.") != std::string::npos
                && fixture.text(34).find("Pruebas opcionales comprobadas: 0 de 3.") != std::string::npos,
                "The first Iris result did not distinguish its three optional trials");
            phase = "open the actual reference after only Iris";
            check(navigate_lesson_view(fixture.view, reference.page),
                "The first Iris observation did not unlock its own reference");
            pump();
            check(edit_text(fixture.control(reference.block, 1)) == authored_reference.code,
                "The first-case reference differs from its authored source");
            check(navigate_lesson_view(fixture.view, target.page),
                "Cannot return from the first-case reference to its editor");
            pump();
            const auto first_receipts = compiler_receipts(fixture.saved(), lesson, compiler);
            fixture.reopen();
            check(fixture.text(10) == authored_reference.code && fixture.text(15) == cases[0].input
                && fixture.text(4) == "✓ Correcto"
                && compiler_receipts(fixture.saved(), lesson, compiler) == first_receipts,
                "Reopening after Iris lost the one-case achievement, source or input");
            retained(1);
        }
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
    const auto before_reopen_receipts = compiler_receipts(fixture.saved(), lesson, compiler);
    check(before_reopen.has_saved && before_reopen.source == authored_reference.code && before_reopen.input == mixed_input,
        "The real editor/input controls did not persist their exact current draft");
    phase = "destroy and reopen the actual p10 laboratory";
    fixture.reopen();
    const auto restored = state::get_draft(fixture.saved(), draft_key, "");
    check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == authored_reference.code
        && fixture.text(15) == mixed_input && restored.source == before_reopen.source && restored.input == before_reopen.input
        && fixture.text(4) == "✓ Correcto"
        && compiler_receipts(fixture.saved(), lesson, compiler) == before_reopen_receipts,
        "Destroying/reopening lost the actual source, prepared input, page or achievement");
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
    std::cout << "PASS " << (historical.empty() ? "current" : "authenticated archived")
        << " U2L9 block_0009_0003: real independent preceding compiler evidence; "
        << "Iris alone opens its reference and survives reopen; three optional cases, "
        << "seven focal MSVC runs, exact source/input/receipts, repeated token-whitespace input, "
        << "and unchanged historical/publication bytes\n";
}

void preserve_u3_independent_state(const state::StateData& baseline, const state::StateData& saved,
        const curriculum::Lesson& lesson, const curriculum::Block& focal) {
    check(saved.preferences.at("input_lab_qa_unrelated") == baseline.preferences.at("input_lab_qa_unrelated"),
        "U3 laboratory operations changed an unrelated preference");
    if (baseline.preferences.contains("attempt_reserve_seconds_test"))
        check(saved.preferences.at("attempt_reserve_seconds_test") == baseline.preferences.at("attempt_reserve_seconds_test"),
            "A U3 compiler experiment consumed the puzzle attempt reserve");
    for (const auto& page : lesson.pages) for (const auto& other : page.blocks) {
        if (other.type == curriculum::BlockType::cpp_compiler) {
            if (other.stable_id == focal.stable_id) continue;
            check(compiler_receipts(saved, lesson, other) == compiler_receipts(baseline, lesson, other)
                && windows::compiler_puzzle_case_count(saved, lesson, other) == windows::compiler_puzzle_case_count(baseline, lesson, other)
                && windows::compiler_puzzle_completed(saved, lesson, other) == windows::compiler_puzzle_completed(baseline, lesson, other),
                "The U3 construction changed its prerequisite or another compiler achievement");
        } else {
            const auto before = state::get_response(baseline, lesson, other);
            const auto after = state::get_response(saved, lesson, other);
            check(before.has_value() == after.has_value(), "The U3 compiler invented or removed an unrelated quiz response");
            if (before) check(before->answer == after->answer && before->correct == after->correct
                && before->raw_json == after->raw_json && before->selected == after->selected && before->order == after->order
                && before->reviewed_answer_identity == after->reviewed_answer_identity,
                "The U3 compiler changed an independently prepared quiz response");
        }
    }
    for (const auto& other : lesson.retired_blocks) if (other.type == curriculum::BlockType::cpp_compiler) {
        const auto key = curriculum::draft_key("programming", lesson, other);
        const auto before = state::get_draft(baseline, key, ""), after = state::get_draft(saved, key, "");
        check(before.has_saved && after.has_saved && before.source == after.source && before.input == after.input
            && before.selection_start == after.selection_start && before.selection_end == after.selection_end
            && before.indentation_json == after.indentation_json
            && compiler_receipts(baseline, lesson, other) == compiler_receipts(saved, lesson, other)
            && windows::compiler_puzzle_case_count(saved, lesson, other) == other.output_check->cases.size()
            && windows::compiler_puzzle_completed(saved, lesson, other),
            "An active U3 experiment changed an authenticated archived draft, cursor, input or receipt");
        check(!curriculum::stable_block_position(lesson, other.stable_id),
            "An archived U3 compiler was restored to the active sequence");
    }
    state::validate_lesson_state(saved, lesson);
}

void complete_u3_prerequisite(View& fixture, curriculum::StatePosition target, bool combinations) {
    const auto& lesson = fixture.lesson;
    const auto prerequisite = position(lesson, combinations ? "p1u3l5_reparar_parentesis" : "p1u3l6_reparar_resto");
    const auto& compiler = lesson.pages[prerequisite.page].blocks[prerequisite.block];
    const auto& reference = active_block(lesson, combinations ? "p1u3l5_ejemplo_reparacion" : "p1u3l6_ejemplo_resto");
    // The persisted focal bookmark must clamp to the first unfinished
    // compiler, even though the earlier quiz answers were prepared for QA.
    check(lesson_page_state(fixture.view).index == prerequisite.page
        && !windows::compiler_puzzle_completed(fixture.saved(), lesson, compiler)
        && !navigate_lesson_view(fixture.view, target.page),
        "The U3 construction opened before its genuine repair prerequisite");
    fixture.block = prerequisite.block;
    check(fixture.text(10) == compiler.initial_code && compiler.local_int_check,
        "The real U3 prerequisite lost its unfinished source or independent authored probes");
    fixture.replace(10, reference.code);
    const auto& first = compiler.output_check->cases.front();
    fixture.run(reference.code, first.standard_input, first.outputs.front(), crt_stdout(first.outputs.front()));
    const auto observed = fixture.saved();
    check(windows::compiler_puzzle_completed(observed, lesson, compiler)
        && windows::compiler_puzzle_case_count(observed, lesson, compiler) == 1 && fixture.text(4) == "✓ Correcto",
        "One genuine prerequisite execution and its probes did not open the dependent construction");
    check(navigate_lesson_view(fixture.view, target.page), "The earned U3 prerequisite did not release its actual dependent page");
    pump(); fixture.block = target.block;
}

void verify_first_case_reference(View& fixture, curriculum::StatePosition target,
        const curriculum::Block& reference, std::size_t optional_count) {
    check(fixture.text(4) == "✓ Correcto"
        && fixture.text(34).find("Comprobación obligatoria: 1 de 1.") != std::string::npos
        && fixture.text(34).find("Pruebas opcionales comprobadas: 0 de " + std::to_string(optional_count) + ".") != std::string::npos,
        "The first genuine U3 case was withheld pending optional trials");
    const auto at = position(fixture.lesson, reference.stable_id);
    check(navigate_lesson_view(fixture.view, at.page), "The first U3 case did not immediately unlock its actual reference page");
    pump();
    check(edit_text(fixture.control(at.block, 1)) == reference.code, "The unlocked U3 reference lost its authored source");
    check(navigate_lesson_view(fixture.view, target.page), "Cannot return from the U3 reference to its actual editor");
    pump();
}

void current_u3_stdout_transport() {
    phase = "load current U3L6 construction and authenticated retired stdout contracts";
    const auto path = std::filesystem::path(build::source_root) /
        "app/src/main/assets/content/es/programming/cpp_fundamentals/expresiones_y_decisiones/lesson_validez_y_cortocircuito.json";
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    u3_contract(lesson, false);
    const auto target = position(lesson, "p1u3l6_construir_reparto");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& reference = active_block(lesson, "p1u3l6_ejemplo_reparto");
    const auto reference_at = position(lesson, reference.stable_id);
    const auto source = reference.code;
    View fixture(lesson, target);
    phase = "U3L6 real repair prerequisite and local-int probes";
    complete_u3_prerequisite(fixture, target, false);
    const auto initial = fixture.saved();
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    check(fixture.text(10) == compiler.initial_code && fixture.text(15) == *compiler.standard_input,
        "U3L6 construction did not begin with its actual unfinished draft and first prepared input");
    const auto retained = [&](std::size_t count, bool completed, std::string_view code, std::string_view input) {
        const auto saved = fixture.saved();
        const auto draft = state::get_draft(saved, draft_key, compiler.initial_code, *compiler.standard_input);
        check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == code && fixture.text(15) == input
            && draft.source == code && draft.input == input
            && windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == completed,
            "U3L6 lost its exact source/input, own case evidence, first-case gate or resumed page");
        preserve_u3_independent_state(initial, saved, lesson, compiler);
    };
    phase = "U3L6 unfinished source is a real incorrect first-case attempt";
    fixture.run(compiler.initial_code, *compiler.standard_input, "");
    retained(0, false, compiler.initial_code, *compiler.standard_input);
    check(fixture.text(4).starts_with("✗ Incorrecto") && !navigate_lesson_view(fixture.view, reference_at.page),
        "The unfinished U3L6 draft opened its reference or earned a case");

    const auto observe = [&](std::size_t index) {
        const auto& item = repartition_cases[index];
        fixture.replace(15, item.input);
        const auto directory = fixture.run(source, item.input, item.output, crt_stdout(item.output));
        const auto raw = bytes(directory / L"execution-stdout.bin");
        curriculum::FreshExecution execution;
        execution.source_revision = 1;
        execution.compilation_succeeded = execution.execution_succeeded = true;
        execution.exit_code = 0;
        execution.standard_input = bytes(directory / L"stdin.bin");
        execution.standard_output = raw;
        const auto exact = curriculum::evaluate_output_check(*compiler.output_check, execution, 1);
        const auto native = windows::evaluate_cpp_output_check(*compiler.output_check, execution, 1);
        check(exact.verdict == curriculum::OutputVerdict::incorrect
            && native.verdict == curriculum::OutputVerdict::correct && native.stdout_correct
            && native.feedback == compiler.output_check->cases[index].explanation
            && execution.standard_output == raw && raw == crt_stdout(item.output),
            "U3L6 CRT CRLF was rewritten or accepted by weakening its shared LF-only output contract");
        if (index == 0) {
            const auto separator = execution.standard_output.find("\r\n");
            check(separator != std::string::npos, "The actual U3L6 first case emitted no native line separator");
            execution.standard_output.erase(separator, 2);
            check(windows::evaluate_cpp_output_check(*compiler.output_check, execution, 1).verdict
                == curriculum::OutputVerdict::incorrect, "U3L6 accepted two required output lines concatenated");
        }
    };
    phase = "U3L6 optional case before mandatory case cannot unlock";
    fixture.replace(10, source); observe(1);
    retained(1, false, source, repartition_cases[1].input);
    check(!navigate_lesson_view(fixture.view, reference_at.page), "U3L6 optional input unlocked the mandatory first case");
    fixture.reopen(); retained(1, false, source, repartition_cases[1].input);
    // Changing an unfinished program invalidates its partial proof. Restoring
    // the source does not resurrect that proof or invent a first-case receipt.
    fixture.replace(10, compiler.initial_code);
    retained(0, false, compiler.initial_code, repartition_cases[1].input);
    fixture.replace(10, source); retained(0, false, source, repartition_cases[1].input);

    phase = "U3L6 first case alone opens continuation and reference";
    observe(0); retained(1, true, source, repartition_cases[0].input);
    verify_first_case_reference(fixture, target, reference, repartition_cases.size() - 1);
    const auto first_receipts = compiler_receipts(fixture.saved(), lesson, compiler);
    fixture.reopen(); retained(1, true, source, repartition_cases[0].input);
    check(fixture.text(4) == "✓ Correcto" && compiler_receipts(fixture.saved(), lesson, compiler) == first_receipts,
        "Reopening U3L6 lost its first-case achievement or replaced its evidence");

    for (std::size_t index = 1; index < repartition_cases.size(); ++index) {
        phase = "U3L6 optional native output observation " + std::to_string(index);
        observe(index); retained(index + 1, true, source, repartition_cases[index].input);
        check(fixture.text(4) == "✓ Correcto", "A U3L6 optional trial replaced its earned success");
    }
    const auto completed_receipts = compiler_receipts(fixture.saved(), lesson, compiler);
    fixture.reopen(); retained(repartition_cases.size(), true, source, repartition_cases.back().input);
    check(fixture.text(4) == "✓ Correcto"
        && compiler_receipts(fixture.saved(), lesson, compiler) == completed_receipts,
        "U3L6 reopening lost its independent optional results");
    bool reading = false;
    for (const auto& region : lesson_reading_regions(fixture.view))
        if (region.target == fixture.control(target.block, 4)) {
            reading = true;
            check(region.feedback && region.correct && region.source_text == fixture.text(4),
                "Restored U3L6 feedback differs between the real reader and visible result");
        }
    check(reading && fixture.executions == 10 && !IsWindowVisible(fixture.host)
        && state::sha256(state::read_bytes(path)) == asset_hash,
        "U3L6 QA lost a fresh run, changed the authored asset or exposed the desktop");
    std::cout << "PASS current U3L6 p1u3l6_construir_reparto: genuine repair prerequisite and probes; "
        "seven inputs with only 28 0 mandatory; ten fresh UI MSVC runs, raw CRLF capture/shared LF comparison, "
        "concatenation rejection, optional-before-first negative, draft/input/achievement reopen, "
        "nine independently executed archived cases preserved; no desktop/listening acceptance\n";
}

void current_u3_combinations() {
    phase = "load current U3L5 construction and authenticated retired combination contracts";
    const auto path = combinations_asset();
    const auto asset_hash = state::sha256(state::read_bytes(path));
    const auto lesson = windows::load_lesson(path);
    combinations_contract(lesson);
    const auto target = position(lesson, "p1u3l5_construir_acceso");
    const auto& compiler = lesson.pages[target.page].blocks[target.block];
    const auto& reference = active_block(lesson, "p1u3l5_ejemplo_acceso");
    const auto reference_at = position(lesson, reference.stable_id);
    const auto source = reference.code;
    auto wrong = source;
    const std::string expression = "(tiene_pase || tiene_invitacion) && sala_abierta";
    const auto expression_at = wrong.find(expression);
    check(expression_at != std::string::npos && wrong.find(expression, expression_at + 1) == std::string::npos,
        "The published U3L5 reference changed; the intentionally wrong grouping must be reviewed");
    wrong.replace(expression_at, expression.size(), "tiene_pase || (tiene_invitacion && sala_abierta)");
    View fixture(lesson, target, true);
    phase = "U3L5 real repair prerequisite and local-int probes";
    complete_u3_prerequisite(fixture, target, true);
    const auto initial = fixture.saved();
    const auto draft_key = curriculum::draft_key("programming", lesson, compiler);
    check(fixture.text(10) == compiler.initial_code && fixture.text(15) == *compiler.standard_input,
        "U3L5 construction lost its unfinished draft or first input: input=" + escaped_diagnostic_text(fixture.text(15))
            + ", expected=" + escaped_diagnostic_text(*compiler.standard_input));
    check(attempts::number(initial, "reserve_programming", -1.) == 3.
        && state::integer_value(initial, "attempt_reserve_seconds_test", "recovery_interval_programming") == attempts::recovery_millis
        && state::find(initial, "premium_state", "state") == nullptr,
        "U3L5 QA lost its saved reserve or fabricated a Premium entitlement");
    std::cout << "U3L5 reserve policy: personal_premium_enabled=" << attempts::personal_premium_enabled
        << " effective_premium=" << attempts::premium(initial)
        << "; saved reserve preservation is checked under the unchanged product policy\n";
    const auto retained = [&](std::size_t count, bool completed, std::string_view code, std::string_view input) {
        const auto saved = fixture.saved();
        const auto draft = state::get_draft(saved, draft_key, compiler.initial_code, *compiler.standard_input);
        check(lesson_page_state(fixture.view).index == target.page && fixture.text(10) == code && fixture.text(15) == input
            && draft.source == code && draft.input == input
            && windows::compiler_puzzle_case_count(saved, lesson, compiler) == count
            && windows::compiler_puzzle_completed(saved, lesson, compiler) == completed,
            "U3L5 lost its exact draft, first-case gate, independent optional evidence or resumed page");
        preserve_u3_independent_state(initial, saved, lesson, compiler);
    };
    phase = "U3L5 unfinished draft cannot earn the mandatory case";
    fixture.run(compiler.initial_code, *compiler.standard_input, "");
    retained(0, false, compiler.initial_code, *compiler.standard_input);
    check(fixture.text(4).starts_with("✗ Incorrecto") && !navigate_lesson_view(fixture.view, reference_at.page),
        "The unfinished U3L5 draft earned a receipt or opened its reference");

    phase = "U3L5 optional 000 before mandatory 100 stays blocked";
    fixture.replace(10, source); fixture.replace(15, combinations_cases[1].input);
    fixture.run(source, combinations_cases[1].input, combinations_cases[1].output);
    retained(1, false, source, combinations_cases[1].input);
    check(!navigate_lesson_view(fixture.view, reference_at.page), "An optional U3L5 input opened the mandatory first-case gate");
    fixture.reopen(); retained(1, false, source, combinations_cases[1].input);

    phase = "U3L5 wrong grouping invalidates an unfinished program and fails the first input";
    fixture.replace(10, wrong); retained(0, false, wrong, combinations_cases[1].input);
    fixture.replace(15, combinations_cases[0].input);
    fixture.run(wrong, combinations_cases[0].input, "Entrada permitida");
    retained(0, false, wrong, combinations_cases[0].input);
    check(fixture.text(4).starts_with("✗ Incorrecto"), "The actual wrong U3L5 grouping was accepted for 100");
    fixture.reopen(); retained(0, false, wrong, combinations_cases[0].input);
    fixture.replace(10, source); retained(0, false, source, combinations_cases[0].input);

    phase = "U3L5 mandatory 100 alone opens continuation and reference";
    fixture.run(source, combinations_cases[0].input, combinations_cases[0].output);
    retained(1, true, source, combinations_cases[0].input);
    verify_first_case_reference(fixture, target, reference, combinations_cases.size() - 1);
    const auto first_receipts = compiler_receipts(fixture.saved(), lesson, compiler);
    fixture.reopen(); retained(1, true, source, combinations_cases[0].input);
    check(fixture.text(4) == "✓ Correcto" && compiler_receipts(fixture.saved(), lesson, compiler) == first_receipts,
        "Reopening U3L5 changed its first genuine achievement");

    for (std::size_t index = 1; index < combinations_cases.size(); ++index) {
        phase = "U3L5 optional Boolean combination " + std::to_string(index);
        fixture.replace(15, combinations_cases[index].input);
        fixture.run(source, combinations_cases[index].input, combinations_cases[index].output);
        retained(index + 1, true, source, combinations_cases[index].input);
        check(fixture.text(4) == "✓ Correcto", "A U3L5 optional trial withheld its previously earned achievement");
    }
    const auto achievements = [&](const state::StateData& data) {
        auto result = compiler_receipts(data, lesson, compiler);
        std::erase_if(result, [](const auto& item) { return item.first.find("/review/") != std::string::npos; });
        return result;
    };
    const auto earned = achievements(fixture.saved());
    phase = "U3L5 later wrong experiment preserves the original achievement";
    fixture.replace(10, wrong); fixture.replace(15, combinations_cases[0].input);
    fixture.run(wrong, combinations_cases[0].input, "Entrada permitida");
    retained(combinations_cases.size(), true, wrong, combinations_cases[0].input);
    check(fixture.text(4).starts_with("✗ Incorrecto") && achievements(fixture.saved()) == earned,
        "A later rejected U3L5 experiment overwrote its genuine success evidence");
    fixture.reopen(); retained(combinations_cases.size(), true, wrong, combinations_cases[0].input);
    check(fixture.text(4).starts_with("✗ Incorrecto") && achievements(fixture.saved()) == earned,
        "Reopening U3L5 replaced the earlier achievement or lost the actual latest rejection");

    phase = "U3L5 explicit actual reset clears only its own objective";
    const auto reset = fixture.control(target.block, 12);
    wchar_t reset_class[32]{};
    check(GetClassNameW(reset, reset_class, 32) && lstrcmpiW(reset_class, L"BUTTON") == 0 && IsWindowEnabled(reset),
        "The actual U3L5 reset action is not an enabled native button");
    SendMessageW(reset, BM_CLICK, 0, 0); pump();
    retained(0, false, compiler.initial_code, combinations_cases[0].input);
    check(fixture.text(4).empty() && fixture.text(14).empty()
        && compiler_receipts(fixture.saved(), lesson, compiler).empty(), "Explicit U3L5 reset retained old output or receipts");
    fixture.reopen(); retained(0, false, compiler.initial_code, combinations_cases[0].input);
    check(fixture.text(4).empty() && fixture.executions == 13 && !IsWindowVisible(fixture.host)
        && state::sha256(state::read_bytes(path)) == asset_hash,
        "U3L5 reset/reopening resurrected achievement, lost an actual run or changed authored bytes");
    std::cout << "PASS current U3L5 p1u3l5_construir_acceso: genuine repair prerequisite and probes; "
        "eight inputs with only 1 0 0 mandatory; thirteen fresh UI MSVC runs, wrong grouping, "
        "optional-before-first negative, partial-program invalidation, first-case continuation/reopen, "
        "retained achievement after rejection and explicit reset; nine archived observations, "
        "historical drafts and saved reserve preserved under the current Premium policy; no desktop/listening acceptance\n";
}
}

int main(int argc, char** argv) {
    std::cout << std::unitbuf; std::cerr << std::unitbuf;
    const bool u3_stdout = argc == 2 && std::string_view(argv[1]) == "--u3-stdout";
    const bool u3_combinations = argc == 2 && std::string_view(argv[1]) == "--u3-combinations";
    const bool u2l9_history = argc == 3 && std::string_view(argv[1]) == "--u2l9-history";
    if (argc != 1 && !u3_stdout && !u3_combinations && !u2l9_history) {
        const bool shape = argc == 3 && (std::string_view(argv[1]) == "--prepare-visual"
            || std::string_view(argv[1]) == "--prepare-u3-visual"
            || std::string_view(argv[1]) == "--prepare-u3-combinations-visual"
            || std::string_view(argv[1]) == "--inspect-u3-combinations-visual");
        const auto raw = shape ? std::string_view(argv[2]) : std::string_view{};
        const std::wstring token(raw.begin(), raw.end());
        if (!shape || !is_valid_qa_instance(token)) {
            std::cerr << "Usage: curricular_input_lab_tests [--u3-stdout | --u3-combinations | --u2l9-history <authentic-json> | --prepare-visual <safe-new-QA-token> | --prepare-u3-visual <safe-new-QA-token> | --prepare-u3-combinations-visual <safe-new-QA-token> | --inspect-u3-combinations-visual <safe-QA-token>]\n";
            return 2;
        }
        // U2L9 preparation executes its preceding boxes program and prepares
        // earlier quizzes explicitly. The focal editor stays uncredited and
        // no visible controls are created.
        try {
            winrt::init_apartment();
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
        else if (u3_stdout) current_u3_stdout_transport();
        else current_three_input_laboratory(u2l9_history ? std::filesystem::path(argv[2]) : std::filesystem::path{});
    } catch (const std::exception& error) { std::cerr << "FAIL current input laboratory: " << error.what() << '\n'; result = 1; }
    if (rich) FreeLibrary(rich);
    OleUninitialize(); return result;
}

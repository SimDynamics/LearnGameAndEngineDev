// Isolated Windows QA. Model captures below test conservation and migration;
// only actual_reference_runs invokes MSVC. No curricular asset is rewritten.
#include "core/catalog.hpp"
#include "core/compiler_flow.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/lesson_entry_policy.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace {
using namespace lgaed;
using namespace curriculum;
using namespace state;
using namespace windows;
using namespace winrt::Windows::Data::Json;

constexpr std::string_view real_owner =
    "u11_movimientos_compensados_en_los_extremos_comprobacion";
constexpr std::string_view real_reference =
    "u11_movimientos_compensados_en_los_extremos_referencia_codigo";
constexpr std::string_view real_full_reference = "block_0005_0002";
constexpr std::string_view real_before_sha =
    "5f0b750fed94e1200c9a7c9be7f1247b2396ac86556685dab9abed4c3a7bed88";

void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class Fn> void rejects(Fn&& fn, std::string_view message) {
    bool refused = false;
    try { fn(); } catch (const std::exception&) { refused = true; }
    catch (const winrt::hresult_error&) { refused = true; }
    require(refused, message);
}
std::string bytes(const std::filesystem::path& path) {
    const auto data = read_bytes(path);
    return {data.begin(), data.end()};
}
void text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(native_io_path(path.parent_path()));
    std::ofstream out(native_io_path(path), std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(out.good(), "Cannot write an isolated case-order QA file");
}
const Block& editor(const Lesson& lesson, std::string_view identity) {
    const auto position = stable_block_position(lesson, identity);
    require(position.has_value(), "Case-order fixture lost a stable block owner");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
std::string receipt_root(const Lesson& lesson, const Block& block) {
    return "compiler-puzzle/v1/" + lesson.locale + '/' + lesson.id + '/'
        + exercise_key(lesson, block) + '/';
}
std::string receipt_base(const Lesson& lesson, const Block& block) {
    return receipt_root(lesson, block) + sha256(reference_objective_frame(lesson, block)) + '/';
}
bool same_draft(const Draft& a, const Draft& b) {
    return a.source == b.source && a.input == b.input
        && a.indentation_json == b.indentation_json
        && a.selection_start == b.selection_start && a.selection_end == b.selection_end
        && a.has_saved == b.has_saved;
}
CppRunRequest model_request(const Block& block, std::string_view program) {
    require(block.output_check && !block.compile_check
        && !block.output_check->rejection_operation
        && !block.output_check->observation_report && !block.output_check->assertion_check
        && !block.open_battery_probe && !block.named_rule_source_check && !block.cmake_graph_check,
        "A model receipt must not impersonate a diagnostic or independent runtime probe");
    CppRunRequest request;
    request.request_id = 37;
    request.revision = 101;
    request.source_utf8 = std::string(program);
    request.project_files = block.project_files;
    request.source_files = block.source_files;
    request.cmake = block.cmake;
    // Project payloads are the exact authored files. The models verify target
    // isolation and migration; they do not claim a CMake configure/build/run.
    return request;
}
FreshExecution model_execution(const Block& block, const CppRunRequest& request,
        std::size_t index) {
    const auto& item = block.output_check->cases.at(index);
    require(!item.outputs.empty() && !item.balance_three_pairs,
        "A model case needs an ordinary fully declared stdout goal");
    FreshExecution result;
    result.source_revision = request.revision;
    result.compilation_succeeded = true;
    result.execution_succeeded = item.exit_code == 0;
    result.execution_completed = true;
    result.exit_code = item.exit_code;
    result.standard_input = item.standard_input;
    result.standard_output = item.outputs.front();
    result.executable = item.executable ? item.executable
        : request.cmake ? std::optional<std::string>{request.cmake->target} : std::nullopt;
    result.standard_error = item.standard_error_exact.value_or(
        item.standard_error == StandardErrorExpectation::non_blank ? "QA diagnostic\n" : "");
    return result;
}
void seed_case(StateData& data, const Lesson& lesson, const Block& block,
        std::string_view program, std::size_t index) {
    auto request = model_request(block, program);
    request.stdin_utf8 = block.output_check->cases.at(index).standard_input;
    if (request.cmake && block.output_check->cases.at(index).executable)
        request.cmake->target = *block.output_check->cases.at(index).executable;
    const auto execution = model_execution(block, request, index);
    const auto evaluation = evaluate_cpp_output_check(*block.output_check, execution, request.revision);
    require(evaluation.verdict == OutputVerdict::correct,
        "The model capture does not satisfy its declared source case");
    // A valid optional case is recorded even though this return value remains
    // false until the required first case has succeeded.
    (void)record_compiler_puzzle_case(data, lesson, block, request, execution,
        "Correcto\n" + block.output_check->cases.at(index).explanation);
}
std::string personal_program(const Block& block, std::string_view marker) {
    return "    // QA state model " + std::string(marker) + "\r\n" + block.initial_code;
}
StateData seeded_state(const Lesson& lesson, std::string_view owner, bool save_draft = true) {
    StateData data;
    prepare_lesson_state(data, lesson);
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        if (block.type == BlockType::cpp_compiler) {
            if (save_draft || block.stable_id != owner) {
                Draft draft;
                draft.source = personal_program(block, block.stable_id);
                draft.input = " \t entrada propia\r\n";
                draft.indentation_json = make_indentation_metadata(draft.source, {0});
                draft.selection_start = 4; draft.selection_end = 11;
                put_draft(data, draft_key("programming", lesson, block), draft);
            }
            // Independent prerequisite completion is explicit QA model data;
            // it neither credits the focal editor nor borrows its program.
            if (block.stable_id != owner && block.output_check && !block.output_check->cases.empty())
                seed_case(data, lesson, block, personal_program(block, "independent"), 0);
            continue;
        }
        Response saved;
        saved.raw_json = R"({"incorrectAttempts":4,"opaque":{"origin":"case-order QA","preserved":true}})";
        if (block.type == BlockType::multiple_choice && block.correct_index) {
            saved.selected = static_cast<std::int64_t>(*block.correct_index);
            saved.correct = evaluate_choice(block, *block.correct_index).correct;
        } else if (block.type == BlockType::output_prediction && !block.answers.empty()) {
            saved.answer = block.answers.front();
            saved.correct = evaluate_text_answer(block, saved.answer).correct;
        } else if (block.type == BlockType::order_code) {
            saved.order.assign(block.correct_order.begin(), block.correct_order.end());
            saved.correct = evaluate_order(block, block.correct_order).correct;
        } else continue;
        require(saved.correct, "An independent QA response is not correct under its real evaluator");
        put_response(data, lesson, block, saved);
    }
    for (std::size_t page = 0; page < lesson.pages.size(); ++page)
        save_current_page(data, lesson, page);
    // A retained legacy whole-lesson flag cannot replace a native case proof.
    data.preferences["learning_progress"]["lesson_" + lesson.id + "_completed"] = true;
    data.preferences["case_order_qa"]["opaque"] = std::string(" \t untouched\r\n");
    data.preferences["case_order_qa"]["enabled"] = true;
    data.preferences["case_order_qa"]["counter"] = std::int64_t{29};
    data.preferences["case_order_qa"]["labels"] = std::vector<std::string>{"one", " \t two\r\n"};
    // These are three independent QA page owners, including for one-page
    // lesson fixtures. Reusing a page deliberately replaces its prior slot.
    auto history = narration::begin({}, "case-order-a", "qa-owner-a",
        {0, 173, 0, false}, "stored source A", "qa_case_order_lesson_a/page_0000");
    history = narration::begin(std::move(history), "case-order-b", "qa-owner-b",
        {1, 239, 0, false}, "stored source B", "qa_case_order_lesson_b/page_0000");
    history = narration::begin(std::move(history), "case-order-c", "qa-owner-c",
        {2, 317, 0, false}, "stored source C", "qa_case_order_lesson_c/page_0000");
    require(history.entries.size() == 3,
        "Case-order QA must seed three distinct page bookmarks before adoption");
    narration::write_history(data, history);
    require(narration::read_history(data) == history,
        "Case-order QA bookmarks must round-trip exactly before adoption");
    validate_lesson_state(data, lesson);
    return data;
}
std::string xml_text(std::string_view value) {
    std::string result;
    for (const char c : value) {
        if (c == '&') result += "&amp;";
        else if (c == '<') result += "&lt;";
        else if (c == '>') result += "&gt;";
        else if (c == '"') result += "&quot;";
        else if (c == '\r') result += "&#13;";
        else if (c == '\n') result += "&#10;";
        else if (c == '\t') result += "&#9;";
        else result += c;
    }
    return result;
}
void export_preferences(const std::filesystem::path& directory, const StateData& data) {
    for (const auto& [domain, values] : data.preferences) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for (const auto& [key, value] : values) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto name = " name=\"" + xml_text(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>)
                xml += "<string" + name + ">" + xml_text(item) + "</string>";
            else if constexpr (std::is_same_v<T, bool>)
                xml += "<boolean" + name + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>)
                xml += "<long" + name + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                xml += "<set" + name + ">";
                for (const auto& entry : item) xml += "<string>" + xml_text(entry) + "</string>";
                xml += "</set>";
            } else throw std::runtime_error("Unsupported isolated case-order XML preference type");
        }, value);
        xml += "</map>";
        text_file(directory / "shared_prefs" / (domain + ".xml"), xml);
    }
}
// Only these reviewed append-only metadata families can grow in this matrix.
// Build their complete expected values from the independently parsed layout;
// no learning_progress prefix or arbitrary new value is exempted below.
std::map<std::string, std::string> reviewed_metadata(const Lesson& lesson) {
    const auto& layout = lesson.state_layout;
    require(layout.version == 2, "Case-order metadata QA requires the stable v2 layout");
    const auto prefix = "lesson_" + lesson.id + "_state_";
    const auto suffix = '_' + lesson.locale;
    std::map<std::string, std::string> result;
    const auto strings = [](const auto& values) {
        JsonArray row;
        for (const auto& value : values)
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(value)));
        return row;
    };
    const auto keep = [&](std::string_view name, const JsonArray& rows, bool allow_empty = false) {
        if (rows.Size() || allow_empty)
            result.emplace(prefix + std::string(name) + suffix, winrt::to_string(rows.Stringify()));
    };
    JsonArray additions;
    for (const auto& entry : layout.additions) {
        auto row = strings(std::array{entry.id, entry.sha256});
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(entry.storage.page)));
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(entry.storage.block)));
        row.Append(JsonValue::CreateStringValue(winrt::to_hstring(entry.progress_page)));
        additions.Append(row);
    }
    keep("additions_v2", additions, true);
    JsonArray compilers;
    for (const auto& revision : layout.compiler_revisions) {
        auto row = strings(std::array{revision.id, revision.sha256, revision.current_payload});
        for (const auto& reference : revision.references)
            for (const auto& value : {reference.id, reference.sha256, reference.current_payload})
                row.Append(JsonValue::CreateStringValue(winrt::to_hstring(value)));
        compilers.Append(row);
    }
    keep("compiler_revisions_v1", compilers);
    JsonArray choices;
    for (const auto& revision : layout.choice_revisions)
        choices.Append(strings(std::array{revision.id, revision.sha256, revision.representation_signature}));
    keep("choice_revisions_v1", choices);
    JsonArray retired;
    for (const auto& entry : layout.retired)
        retired.Append(strings(std::array{entry.id, entry.page, entry.next, entry.sha256}));
    keep("retired_v2", retired);
    JsonArray retirement_history;
    for (const auto& snapshot : layout.retirement_history)
        retirement_history.Append(JsonValue::CreateStringValue(winrt::to_hstring(snapshot.sha256)));
    keep("retirement_history_v2", retirement_history);
    if (layout.has_editorial_returns) {
        JsonArray editorial;
        for (const auto& proof : JsonObject::Parse(winrt::to_hstring(layout.raw_json)).GetNamedArray(L"editorial"))
            editorial.Append(JsonValue::CreateStringValue(winrt::to_hstring(
                test::compile_check_test_fingerprint(proof))));
        keep("editorial_returns_v1", editorial);
    }
    return result;
}

void preserved_values(const StateData& before, const StateData& after,
        const Lesson& lesson, std::string_view owner, std::string_view order_pin) {
    for (const auto& [domain, values] : before.preferences) for (const auto& [key, value] : values) {
        const auto stored = find(after, domain, key);
        require(stored != nullptr, "Case-order adoption deleted an existing preference: " + domain + "/" + key);
        if (domain == "learning_progress" && key == order_pin) {
            require(std::holds_alternative<std::vector<std::string>>(value)
                && std::holds_alternative<std::vector<std::string>>(*stored),
                "Case-order provenance changed its stored type");
            const auto& old_rows = std::get<std::vector<std::string>>(value);
            const auto& new_rows = std::get<std::vector<std::string>>(*stored);
            require(new_rows.size() >= old_rows.size()
                && std::equal(old_rows.begin(), old_rows.end(), new_rows.begin()),
                "Case-order adoption truncated or rewrote an old provenance row");
        } else require(*stored == value,
            "Case-order adoption changed an old receipt, answer, draft, pause or unrelated typed value: "
                + domain + "/" + key);
    }
    const auto allowed_root = receipt_root(lesson, editor(lesson, owner));
    for (const auto& [domain, values] : after.preferences) for (const auto& [key, value] : values)
        if (!find(before, domain, key))
            require((domain == "learning_progress" && key == order_pin)
                || (domain == "native_windows" && key.starts_with(allowed_root)),
                "Case-order adoption created state outside the reviewed owner: " + domain + "/" + key);
    require(narration::read_history(after) == narration::read_history(before)
        && narration::read_history(after).entries.size() == 3,
        "Case-order adoption changed a stored narration pause");
}
void preserved_drafts(const StateData& original, const StateData& data,
        const Lesson& before, const Lesson& after) {
    for (const auto& page : before.pages) for (const auto& block : page.blocks) {
        if (block.type != BlockType::cpp_compiler) continue;
        const auto& current = editor(after, block.stable_id);
        const auto old_key = draft_key("programming", before, block);
        require(old_key == draft_key("programming", after, current)
            && exercise_key(before, block) == exercise_key(after, current),
            "A case permutation changed an editor's historical storage address");
        const auto old_draft = get_draft(original, old_key, block.initial_code, block.standard_input.value_or(""));
        const auto new_draft = get_draft(data, old_key, current.initial_code, current.standard_input.value_or(""));
        require(old_draft.has_saved ? same_draft(old_draft, new_draft) : !new_draft.has_saved,
            "Case-order adoption changed a personalized source, stdin, indentation or selection");
    }
    require(current_page(original, before) == current_page(data, after),
        "Case-order adoption rewrote the stored page bookmark");
}
void check_gate(const StateData& data, const Lesson& lesson,
        std::string_view owner, bool expected) {
    const auto completed = [&](const Block& block) {
        return compiler_puzzle_completed(data, lesson, block);
    };
    const auto snapshot = compiler_completion_snapshot(lesson, completed);
    require(snapshot.contains(std::string(owner)) && snapshot.at(std::string(owner)) == expected,
        "Case-order migration accredited the wrong required first case or lost its prerequisite");
    for (const auto& relation : lesson.reference_after) if (relation.activity == owner)
        for (const auto& identity : relation.blocks) {
            const auto position = stable_block_position(lesson, identity);
            require(position.has_value(), "The revised compiler lost a dependent reference");
            const auto& reference = lesson.pages[position->page].blocks[position->block];
            require(compiler_reference_available(lesson, position->page, reference, snapshot, completed) == expected,
                "A reference gate disagrees with the revised required first case");
        }
    // Both catalog modes use the production grouping callback; an old lesson
    // completed flag or read coverage must not override an incomplete editor.
    const auto progress = [&](const Lesson& version) {
        return catalog_lesson_page_progress(data, version, [&](const Block& block) {
            if (block.type == BlockType::cpp_compiler)
                return compiler_puzzle_completed(data, version, block);
            const auto response = get_response(data, version, block);
            return response && response->correct;
        });
    };
    const auto full = progress(lesson);
    const auto metadata = parse_lesson_progress_metadata(lesson.raw_json, lesson.locale);
    const auto projected = progress(metadata);
    require(full.completed == projected.completed && full.total == projected.total,
        "Full and metadata loaders disagree about case-order catalog progress");
    const auto assuming = [&](bool focal) {
        return catalog_lesson_page_progress(data, lesson, [&](const Block& block) {
            if (block.stable_id == owner) return focal;
            if (block.type == BlockType::cpp_compiler) return completed(block);
            const auto response = get_response(data, lesson, block);
            return response && response->correct;
        });
    };
    const auto pending = assuming(false), earned = assuming(true);
    require(earned.completed == pending.completed + 1
        && full.completed == (expected ? earned.completed : pending.completed),
        "Historical whole-lesson completion or read coverage overrode current first-case catalog progress");
    std::vector<EntryRequirement> requirements;
    for (std::size_t p = 0; p < lesson.pages.size(); ++p)
        for (const auto& block : lesson.pages[p].blocks)
            if (block.type == BlockType::cpp_compiler)
                requirements.push_back({p, completed(block) ? EntryCompletion::complete : EntryCompletion::incomplete});
    const auto requested = lesson.pages.size() - 1;
    const auto decision = lesson_entry_decision(requested, lesson.pages.size(), requirements);
    const auto position = stable_block_position(lesson, owner);
    require(position.has_value() && (expected || decision.page <= position->page),
        "Restoring the historical bookmark bypassed the current compiler prerequisite");
    if (std::ranges::all_of(requirements, [](const auto& item) {
        return item.completion == EntryCompletion::complete;
    })) require(decision.page == requested,
        "Optional cases blocked restoration after all required first cases were proved");
}


struct Fixture {
    std::string name, id, before_file, after_file;
    bool real = false;
    Lesson before, after;
    std::vector<Lesson> history;
    std::vector<std::size_t> order;
};
std::string order_pin(const Lesson& lesson) {
    return "lesson_" + lesson.id + "_state_compiler_case_order_revisions_v1_" + lesson.locale;
}
std::vector<std::string> proof_values(const StateData& data, const Lesson& lesson, const Block& block) {
    const auto value = find(data, "native_windows", receipt_base(lesson, block) + "cases");
    if (!value) return {};
    require(std::holds_alternative<std::vector<std::string>>(*value),
        "QA expected a typed vector of native case proofs");
    return std::get<std::vector<std::string>>(*value);
}
std::vector<std::optional<std::size_t>> source_case_mapping(
        const Block& before, const Block& after) {
    const auto old_cases = JsonObject::Parse(winrt::to_hstring(before.raw_json))
        .GetNamedObject(L"outputCheck").GetNamedArray(L"cases");
    const auto new_cases = JsonObject::Parse(winrt::to_hstring(after.raw_json))
        .GetNamedObject(L"outputCheck").GetNamedArray(L"cases");
    std::vector<std::optional<std::size_t>> mapping;
    std::set<unsigned> consumed;
    for (const auto& candidate : new_cases) {
        std::optional<std::size_t> found;
        for (unsigned i = 0; i < old_cases.Size(); ++i) {
            if (!consumed.contains(i)
                && test::compile_check_test_fingerprint(old_cases.GetAt(i))
                    == test::compile_check_test_fingerprint(candidate)) {
                found = i; consumed.insert(i); break;
            }
        }
        mapping.push_back(found);
    }
    return mapping;
}
std::vector<std::optional<std::size_t>> execution_case_mapping(
        const Block& before, const Block& after) {
    std::vector<std::optional<std::size_t>> result;
    std::set<std::size_t> consumed;
    for (const auto& current : after.output_check->cases) {
        std::optional<std::size_t> original;
        if (before.output_check->input_comparison == after.output_check->input_comparison)
            for (std::size_t index = 0; index < before.output_check->cases.size(); ++index)
                if (!consumed.contains(index)
                    && same_output_case_requirements(before.output_check->cases[index], current)) {
                    original = index; consumed.insert(index); break;
                }
        result.push_back(original);
    }
    return result;
}
std::map<std::string, TextSemantics> case_semantics(const Block& block) {
    std::map<std::string, TextSemantics> result;
    for (const auto& [path, meanings] : block.text_semantics)
        if (path.starts_with("/outputCheck/cases/")) result.emplace(path, meanings);
    return result;
}
void verify_publication(const Fixture& fixture) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    require(before.type == BlockType::cpp_compiler && after.type == before.type
        && before.output_check && after.output_check
        && exercise_key(fixture.before, before) == exercise_key(fixture.after, after)
        && draft_key("programming", fixture.before, before)
            == draft_key("programming", fixture.after, after),
        "The fixture is not a stable C++ compiler case-order review");
    require(!after.compiler_case_order_revisions.empty(),
        "The proposed order has no native authenticated revision archive");
    const auto metadata = parse_lesson_progress_metadata(fixture.after.raw_json, fixture.after.locale);
    require(reference_objective_frame(fixture.after, after)
        == reference_objective_frame(metadata, editor(metadata, fixture.id)),
        "Metadata parsing changed the revised executable objective");
    const auto mapping = execution_case_mapping(before, after);
    if (!fixture.order.empty()) {
        const bool new_permutation = after.compiler_case_order_revisions.size()
            > before.compiler_case_order_revisions.size();
        const auto complete_mapping = source_case_mapping(before, after);
        if (new_permutation)
            require(complete_mapping == mapping,
                "A new permutation changed case presentation payloads as well as their order");
        require(fixture.order.size() == mapping.size()
            && mapping.size() == before.output_check->cases.size(),
            "The fixture's declared permutation has the wrong number of cases");
        for (std::size_t i = 0; i < mapping.size(); ++i)
            require(mapping[i] && *mapping[i] == fixture.order[i],
                "Case-order QA detected a changed input, oracle or other evaluated case payload");
        if (new_permutation) {
            std::map<std::string, TextSemantics> expected;
            for (std::size_t index = 0; index < fixture.order.size(); ++index) {
                const auto old_prefix = "/outputCheck/cases/" + std::to_string(fixture.order[index]) + '/';
                const auto new_prefix = "/outputCheck/cases/" + std::to_string(index) + '/';
                for (const auto& [path, descriptor] : case_semantics(before))
                    if (path.starts_with(old_prefix))
                        expected.emplace(new_prefix + path.substr(old_prefix.size()), descriptor);
            }
            require(case_semantics(after) == expected,
                "Equal-looking guides borrowed the role, reading or exact span of another case");
        }
    }
    std::vector<const Lesson*> sources{&fixture.before};
    for (const auto& stage : fixture.history) sources.push_back(&stage);
    std::size_t authenticated_archives = 0;
    for (const auto& revision : after.compiler_case_order_revisions) {
        const auto historical = compiler_case_order_previous_block(after, revision);
        require(historical.stable_id == after.stable_id
            && historical.raw_json == revision.raw_json
            && historical.output_check.has_value(),
            "A historical case-order helper lost the archived owner or complete source");
        const auto found = std::ranges::find_if(sources, [&](const Lesson* source) {
            const auto at = stable_block_position(*source, fixture.id);
            return at && test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(
                source->pages[at->page].blocks[at->block].raw_json))) == revision.sha256;
        });
        if (found == sources.end()) continue;
        const auto& source = **found;
        const auto& original = editor(source, fixture.id);
        require(reference_objective_frame(source, original)
            == reference_objective_frame(fixture.after, historical)
            && historical.initial_code == original.initial_code
            && historical.source_format_original_initial_code.value_or(historical.initial_code)
                == original.source_format_original_initial_code.value_or(original.initial_code)
            && historical.source_format_original_project_files.value_or(historical.project_files)
                == original.source_format_original_project_files.value_or(original.project_files)
            && case_semantics(historical) == case_semantics(original),
            "A historical case-order version borrowed another source's objective, formatting or case meanings");
        if (fixture.name == "real-u11-six-movements")
            require(historical.source_format_original_initial_code == original.source_format_original_initial_code
                && historical.source_format_original_project_files == original.source_format_original_project_files,
                "The authentic real U11 predecessor lost its actual formatting sidecars");
        require(historical.output_check->cases.size() == original.output_check->cases.size(),
            "A historical case-order version changed its number of cases");
        for (std::size_t i = 0; i < original.output_check->cases.size(); ++i) {
            const auto& a = historical.output_check->cases[i];
            const auto& b = original.output_check->cases[i];
            require(same_output_case_requirements(a, b) && a.explanation == b.explanation
                && a.review_feedback.size() == b.review_feedback.size(),
                "A historical case-order version changed an evaluated requirement or guide");
            for (std::size_t j = 0; j < a.review_feedback.size(); ++j)
                require(a.review_feedback[j].outputs == b.review_feedback[j].outputs
                    && a.review_feedback[j].text == b.review_feedback[j].text,
                    "Historical case-order feedback moved to a different verdict");
        }
        ++authenticated_archives;
    }
    require(authenticated_archives != 0 || after.compiler_case_order_revisions.size()
            == before.compiler_case_order_revisions.size(),
        "No newly introduced case-order archive matched an independently supplied complete predecessor");
    if (fixture.name == "real-u11-six-movements") {
        require(fixture.real && sha256(fixture.before.raw_json) == real_before_sha
            && fixture.before.raw_json.size() == 199237 && fixture.id == real_owner
            && before.output_check->cases.size() == 22 && after.output_check->cases.size() == 22,
            "The real 22-case regression lost the exact f2f86fc Git publication");
        require(fixture.order.size() == 22 && fixture.order.front() == 21,
            "The real U11L7 revision did not move the last declared case first");
        for (std::size_t i = 1; i < 22; ++i)
            require(fixture.order[i] == i - 1, "The other 21 U11L7 cases changed relative order");
        require(before.source_format_original_initial_code.has_value()
            && !case_semantics(before).empty()
            && editor(fixture.before, "block_0003_0005").compiler_revision.has_value(),
            "The real test omitted published formatting, semantics or the independent closed expansion");
    }
}
void check_order_pin(const StateData& data, const Lesson& lesson) {
    std::vector<std::string> expected;
    for (const auto& revision : lesson.state_layout.compiler_case_order_revisions)
        expected.push_back(revision.record_sha256);
    const auto stored = find(data, "learning_progress", order_pin(lesson));
    require(stored && std::holds_alternative<std::vector<std::string>>(*stored)
        && std::get<std::vector<std::string>>(*stored) == expected,
        "The native pin does not retain the exact global authenticated case-order journal");
}
void no_old_feedback_lent(const StateData& data, const Lesson& lesson, const Block& block) {
    const auto feedback = compiler_puzzle_success_feedback(data, lesson, block);
    if (!feedback || feedback->text.empty()) return;
    const auto& first = block.output_check->cases.front();
    require(!first.explanation.empty() && feedback->text.ends_with(first.explanation),
        "Migrated first-case credit borrowed feedback from a different old case");
    if (block.text_semantics.contains("/outputCheck/cases/0/explanation"))
        require(feedback->source_path == "/outputCheck/cases/0/explanation",
            "A migrated marked guide lost its exact new first-case semantic path");
}
void check_composite_values(const StateData& before, const StateData& after,
        const Fixture& fixture) {
    const auto previous = reviewed_metadata(fixture.before);
    const auto current = reviewed_metadata(fixture.after);
    auto unchanged = before;
    for (const auto& [key, expected] : current) {
        const auto old_value = find(before, "learning_progress", key);
        const auto new_value = find(after, "learning_progress", key);
        require(new_value && std::holds_alternative<std::string>(*new_value),
            "Reviewed metadata is absent or has a different type: learning_progress/" + key);
        const auto new_rows = JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*new_value)));
        require(winrt::to_string(new_rows.Stringify()) == expected,
            "Reviewed metadata differs from the complete current layout: learning_progress/" + key);
        if (old_value) {
            require(std::holds_alternative<std::string>(*old_value) && previous.contains(key),
                "Reviewed metadata changed its original type or owner: learning_progress/" + key);
            const auto old_rows = JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*old_value)));
            require(winrt::to_string(old_rows.Stringify()) == previous.at(key),
                "Reviewed metadata differs from its complete predecessor: learning_progress/" + key);
            require(new_rows.Size() >= old_rows.Size(),
                "Reviewed metadata truncated a prior row: learning_progress/" + key);
            for (unsigned i = 0; i < old_rows.Size(); ++i)
                require(old_rows.GetAt(i).Stringify() == new_rows.GetAt(i).Stringify(),
                    "Reviewed metadata rewrote a prior row: learning_progress/" + key);
            if (new_rows.Size() == old_rows.Size())
                require(*old_value == *new_value,
                    "Unchanged reviewed metadata lost its exact stored bytes: learning_progress/" + key);
        } else require(!previous.contains(key),
            "The original state omitted its reviewed metadata: learning_progress/" + key);
        // Exempt only the exact, fully checked append above. Every other key
        // must remain byte/type identical under preserved_values.
        unchanged.preferences["learning_progress"][key] = *new_value;
    }
    for (const auto& [key, expected] : previous)
        require(current.contains(key),
            "The current layout removed reviewed metadata: learning_progress/" + key);
    preserved_values(unchanged, after, fixture.after, fixture.id, order_pin(fixture.after));
}
void imported_state(const Fixture& fixture, const StateData& original,
        const std::filesystem::path& directory, std::string_view mode) {
    // Android transports SharedPreferences XML and JSON-v1 strings inside it.
    // It has no permission to import native Windows execution proofs.
    auto android = original;
    android.preferences.erase("native_windows");
    const auto exported = directory / (fixture.name + "-" + std::string(mode) + "-xml");
    export_preferences(exported, android);
    const auto preview = inspect_android_export(exported);
    require(preview.data == android, "XML inspection changed typed preferences or embedded JSON");
    validate_lesson_state(preview.data, fixture.after);
    require(preview.data == android, "Read-only import validation mutated original preferences");
    const auto destination = directory / (fixture.name + "-" + std::string(mode) + "-import");
    commit_android_import(preview, destination);
    StateStore store(destination);
    auto data = store.load();
    require(data == android, "Android import commit changed the inspected source state");
    prepare_lesson_state(data, fixture.after);
    validate_lesson_state(data, fixture.after);
    preserved_drafts(android, data, fixture.before, fixture.after);
    require(narration::read_history(data) == narration::read_history(android),
        "Android import changed a case-order narration bookmark");
    const auto& target = editor(fixture.after, fixture.id);
    require(!compiler_puzzle_completed(data, fixture.after, target)
        && compiler_puzzle_case_count(data, fixture.after, target) == 0,
        "Android JSON answers or drafts manufactured native case-order credit");
    for (const auto& lesson : fixture.history)
        require(!compiler_puzzle_completed(data, lesson, editor(lesson, fixture.id))
            && compiler_puzzle_case_count(data, lesson, editor(lesson, fixture.id)) == 0,
            "Android import fabricated a historical native execution receipt");
    for (const auto& [domain, values] : android.preferences) for (const auto& [key, value] : values)
        if (domain != "learning_progress" || (!key.starts_with("lesson_" + fixture.after.id + "_state_")))
            require(find(data, domain, key) && *find(data, domain, key) == value,
                "Android migration rewrote an original answer, opaque value or JSON-v1 draft");
    store.save(data);
    StateStore reopened(destination);
    require(reopened.load() == data, "Imported case-order state changed on physical reopening");
    for (const auto& file : preview.files)
        require(sha256(read_bytes(exported / file.relative_path)) == file.sha256
            && sha256(read_bytes(destination / "state/active/android" / file.relative_path)) == file.sha256,
            "Case-order import changed an original XML file or its archived bytes");
    const auto reserved = directory / (fixture.name + "-" + std::string(mode) + "-reserved");
    auto forbidden = original;
    forbidden.preferences["native_windows"]["case-order-reserved-domain-negative"] = true;
    export_preferences(reserved, forbidden);
    rejects([&] { (void)inspect_android_export(reserved); },
        "Android import accepted the reserved native execution domain");
}
void current_state(const Fixture& fixture, const StateData& data,
        const StateData& original, const std::vector<std::size_t>& seeds) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto mapping = execution_case_mapping(before, after);
    std::size_t expected_count = 0;
    for (const auto prior : mapping)
        if (prior && std::ranges::find(seeds, *prior) != seeds.end()) ++expected_count;
    const bool expected_complete = !mapping.empty() && mapping.front()
        && std::ranges::find(seeds, *mapping.front()) != seeds.end();
    require(compiler_puzzle_case_count(data, fixture.after, after) == expected_count
        && compiler_puzzle_completed(data, fixture.after, after) == expected_complete,
        "Case-order adoption lost a proved case or credited an unproved new first case");
    require(compiler_puzzle_case_count(data, fixture.before, before)
            == compiler_puzzle_case_count(original, fixture.before, before)
        && compiler_puzzle_completed(data, fixture.before, before)
            == compiler_puzzle_completed(original, fixture.before, before)
        && compiler_puzzle_feedback(data, fixture.before, before)
            == compiler_puzzle_feedback(original, fixture.before, before),
        "Case-order adoption rewrote the complete predecessor's historical achievement");
    check_order_pin(data, fixture.after);
    preserved_drafts(original, data, fixture.before, fixture.after);
    check_gate(data, fixture.after, fixture.id, expected_complete);
    if (expected_complete
        && reference_objective_frame(fixture.before, before)
            != reference_objective_frame(fixture.after, after))
        no_old_feedback_lent(data, fixture.after, after);
    if (expected_complete && (fixture.name == "synthetic-later-guidance"
            || fixture.name == "synthetic-later-marks")) {
        const auto old_feedback = compiler_puzzle_success_feedback(data, fixture.before, before);
        const auto current_feedback = compiler_puzzle_success_feedback(data, fixture.after, after);
        require(old_feedback && current_feedback && old_feedback->text == current_feedback->text
            && current_feedback->source_path.empty(),
            "A later guide or mark retroactively supplied a semantic origin to an old feedback receipt");
    }
}
void reject_corrupt_state(const Fixture& fixture, const StateData& original) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto base = receipt_base(fixture.before, before);
    const auto try_bad = [&](StateData damaged) {
        const auto preserved = damaged;
        rejects([&] { prepare_lesson_state(damaged, fixture.after); },
            "Case-order adoption accepted corrupt execution provenance");
        require(damaged == preserved, "Rejected case-order adoption partially rewrote state");
    };
    auto bad = original;
    bad.preferences["native_windows"][base + "program"] = std::int64_t{42};
    try_bad(bad);
    const auto proofs = proof_values(original, fixture.before, before);
    if (!proofs.empty()) {
        auto forged = proofs;
        const auto proof = std::ranges::find_if(forged, [](const auto& value) { return !value.empty(); });
        require(proof != forged.end(), "The corruption test needs an existing execution proof");
        *proof = "forged-case-proof";
        bad = original; bad.preferences["native_windows"][base + "cases"] = forged; try_bad(bad);
        bad = original; bad.preferences["native_windows"][base + "program"] = std::string("other program"); try_bad(bad);
        bad = original; bad.preferences["native_windows"][base + "completed"] = std::string("forged-seal"); try_bad(bad);
    }
}
std::size_t native_matrix(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto mapping = execution_case_mapping(before, after);
    std::vector<std::pair<std::string, std::vector<std::size_t>>> modes{
        {"no-receipt", {}}, {"old-first", {0}}
    };
    if (mapping.front() && *mapping.front() != 0)
        modes.push_back({"new-first-was-optional", {*mapping.front()}});
    std::vector<std::size_t> all;
    // Finish with case zero, obtaining the genuine legacy all-case seal through
    // the public recorder instead of seeding/recomputing an internal hash.
    for (std::size_t i = 1; i < before.output_check->cases.size(); ++i) all.push_back(i);
    all.push_back(0);
    modes.push_back({"all-old-cases", std::move(all)});
    for (const auto& [mode, seeds] : modes) {
        auto original = seeded_state(fixture.before, fixture.id);
        const auto program = personal_program(before, "program-A");
        for (const auto index : seeds) seed_case(original, fixture.before, before, program, index);
        validate_lesson_state(original, fixture.before);
        const auto profile = directory / (fixture.name + "-" + mode + "-native");
        StateStore store(profile);
        require(store.load().preferences.empty(), "Case-order native QA profile is not new");
        store.save(original);
        auto data = store.load();
        require(data == original && decode(encode(data)) == original,
            "Native binary codec or physical reopen changed the predecessor state");
        validate_lesson_state(data, fixture.after);
        require(data == original, "Read-only case-order validation changed its source state");
        prepare_lesson_state(data, fixture.after);
        validate_lesson_state(data, fixture.after);
        check_composite_values(original, data, fixture);
        current_state(fixture, data, original, seeds);
        const auto adopted = data;
        prepare_lesson_state(data, fixture.after);
        require(data == adopted, "Case-order adoption is not idempotent");
        auto downgrade = data;
        if (fixture.before.state_layout.compiler_case_order_revisions.size()
                < fixture.after.state_layout.compiler_case_order_revisions.size()) {
            rejects([&] { prepare_lesson_state(downgrade, fixture.before); },
                "Removing a published case-order journal accepted a downgrade");
            require(downgrade == data, "Rejecting a removed case-order journal changed saved data");
        } else {
            // A later guide/meaning edit is allowed without inventing a new
            // permutation or treating ordinary editorial change as a downgrade.
            validate_lesson_state(downgrade, fixture.before);
            require(downgrade == data, "Read-only historical guidance validation changed state");
        }
        auto bad_pin = data;
        auto pin = std::get<std::vector<std::string>>(*find(bad_pin, "learning_progress", order_pin(fixture.after)));
        require(!pin.empty(), "The malformed-pin test needs adopted provenance");
        pin.front() = std::string(64, '0');
        bad_pin.preferences["learning_progress"][order_pin(fixture.after)] = pin;
        const auto damaged = bad_pin;
        rejects([&] { prepare_lesson_state(bad_pin, fixture.after); },
            "Case-order adoption accepted a rewritten historical journal pin");
        require(bad_pin == damaged, "Rejecting a malformed case-order pin changed saved data");
        store.save(data);
        StateStore reopened(profile);
        require(reopened.load() == data, "Adopted case-order state changed on physical reopening");
        imported_state(fixture, original, directory, mode);
        reject_corrupt_state(fixture, original);
        auto reset = data;
        reset_compiler_puzzle(reset, fixture.after, after);
        auto expected_reset = data;
        const auto root = receipt_root(fixture.after, after);
        if (const auto domain = expected_reset.preferences.find("native_windows");
            domain != expected_reset.preferences.end())
            std::erase_if(domain->second, [&](const auto& item) { return item.first.starts_with(root); });
        require(reset == expected_reset, "Scoped compiler Reset changed a draft, answer, pause or other owner");
        prepare_lesson_state(reset, fixture.after);
        require(!compiler_puzzle_completed(reset, fixture.after, after)
            && compiler_puzzle_case_count(reset, fixture.after, after) == 0
            && !compiler_puzzle_completed(reset, fixture.before, before)
            && compiler_puzzle_case_count(reset, fixture.before, before) == 0,
            "Preparing after Reset resurrected current or archived case-order credit");
        for (const auto& history : fixture.history)
            require(compiler_puzzle_case_count(reset, history, editor(history, fixture.id)) == 0,
                "Reset retained a proof in an intermediate case-order version");
        preserved_drafts(original, reset, fixture.before, fixture.after);
        require(narration::read_history(reset) == narration::read_history(original),
            "Scoped Reset changed stored narration pauses");
        std::cout << "PASS case-order state " << fixture.name << '/' << mode
            << ": original proofs and typed state, required-first gate, XML/JSON-v1 import, reopen, atomic refusal and Reset\n";
    }
    return modes.size();
}



void different_programs(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    auto original = seeded_state(fixture.before, fixture.id);
    const auto program_a = personal_program(before, "program-A");
    const auto program_b = personal_program(before, "program-B");
    seed_case(original, fixture.before, before, program_a, 0);
    auto data = original;
    prepare_lesson_state(data, fixture.after);
    require(!compiler_puzzle_completed(data, fixture.after, after),
        "The program separation fixture needs an unproved new first case");
    auto changed = model_request(after, program_b);
    invalidate_compiler_puzzle_attempt(data, fixture.after, after, changed);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 0
        && compiler_puzzle_completed(data, fixture.before, before),
        "Editing an incomplete current program erased its historical achievement or retained foreign proofs");
    seed_case(data, fixture.after, after, program_b, 1);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 1
        && !compiler_puzzle_completed(data, fixture.after, after),
        "A different program borrowed program A's required first-case credit");
    prepare_lesson_state(data, fixture.after);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 1,
        "Repeated adoption resurrected partial receipts from program A");
    seed_case(data, fixture.after, after, program_b, 0);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 2
        && compiler_puzzle_completed(data, fixture.after, after)
        && string_value(data, "native_windows", receipt_base(fixture.after, after) + "program")
            == compiler_program_frame(changed),
        "Program B did not earn only its own two explicitly evaluated cases");
    for (const auto& [key, value] : original.preferences.at("native_windows"))
        require(find(data, "native_windows", key) && *find(data, "native_windows", key) == value,
            "Program B overwrote an archived execution or independent editor");
    check_gate(data, fixture.after, fixture.id, true);
    const auto achieved = data;
    changed.source_utf8 += "// another draft, not a new execution\n";
    invalidate_compiler_puzzle_attempt(data, fixture.after, after, changed);
    require(compiler_puzzle_completed(data, fixture.after, after)
        && proof_values(data, fixture.after, after) == proof_values(achieved, fixture.after, after),
        "A mere edit revoked a properly earned first-case achievement");
    StateStore store(directory / "different-programs");
    require(store.load().preferences.empty(), "The program-separation profile is not isolated");
    store.save(data); StateStore reopened(directory / "different-programs");
    require(reopened.load() == data, "Reopening mixed the two captured programs");
    std::cout << "PASS case-order programs A/B: no receipt union, historical A retained, B proves its own new first, later edit preserves its earned seal\n";
}
void successive_adoption(const Fixture& fixture) {
    if (fixture.history.size() < 2) return;
    const auto& before = editor(fixture.before, fixture.id);
    auto original = seeded_state(fixture.before, fixture.id);
    seed_case(original, fixture.before, before, personal_program(before, "chain-A"),
        before.output_check->cases.size() - 1);
    auto direct = original;
    prepare_lesson_state(direct, fixture.after);
    auto staged = original;
    for (const auto& lesson : fixture.history) {
        prepare_lesson_state(staged, lesson);
        validate_lesson_state(staged, lesson);
        const auto adopted = staged;
        prepare_lesson_state(staged, lesson);
        require(staged == adopted, "Intermediate case-order adoption is not idempotent");
    }
    require(staged == direct,
        "Direct and chronological case-order adoption produced different receipts, pins or feedback");
    std::cout << "PASS case-order chain " << fixture.name
        << ": direct and chronological adoption agree across exact complete publications\n";
}
void explicit_return(const Fixture& fixture) {
    require(fixture.history.size() >= 2, "The return fixture has no chronological review history");
    const auto& before = editor(fixture.before, fixture.id);
    const auto& first_stage = fixture.history.front();
    const auto& first = editor(first_stage, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    require(reference_objective_frame(fixture.before, before)
            == reference_objective_frame(fixture.after, after)
        && reference_objective_frame(first_stage, first)
            != reference_objective_frame(fixture.before, before),
        "The explicit return did not restore a previously authenticated order objective");
    auto original = seeded_state(fixture.before, fixture.id);
    const auto program = personal_program(before, "return-A");
    seed_case(original, fixture.before, before, program, before.output_check->cases.size() - 1);
    const auto old_proofs = proof_values(original, fixture.before, before);
    auto data = original;
    prepare_lesson_state(data, first_stage);
    require(compiler_puzzle_completed(data, first_stage, first)
        && !compiler_puzzle_completed(data, fixture.before, before),
        "The first return step misidentified the old optional receipt");
    const auto mapping = source_case_mapping(before, first);
    const auto optional = std::ranges::find(mapping, std::optional<std::size_t>{0});
    require(optional != mapping.end() && optional != mapping.begin(),
        "The return fixture needs the old first case to be optional in its middle version");
    seed_case(data, first_stage, first, program, static_cast<std::size_t>(optional - mapping.begin()));
    require(compiler_puzzle_case_count(data, first_stage, first) == 2,
        "The middle version did not record the explicitly tried optional case");
    const auto middle = data;
    for (std::size_t i = 1; i < fixture.history.size(); ++i)
        prepare_lesson_state(data, fixture.history[i]);
    require(compiler_puzzle_completed(data, fixture.after, after)
        && compiler_puzzle_case_count(data, fixture.after, after) == 2,
        "Returning to an existing objective failed to add its newly proved first case");
    const auto new_proofs = proof_values(data, fixture.after, after);
    require(new_proofs.size() == old_proofs.size(), "A returned objective changed its case count");
    for (std::size_t i = 0; i < old_proofs.size(); ++i)
        if (!old_proofs[i].empty())
            require(new_proofs[i] == old_proofs[i], "An explicit return overwrote a pre-existing proof");
    require(string_value(data, "native_windows", receipt_base(fixture.after, after) + "program")
        == compiler_program_frame(model_request(after, program)),
        "An explicit return changed the proved program");
    const auto intermediate_prefix = receipt_base(first_stage, first);
    for (const auto& [key, value] : middle.preferences.at("native_windows"))
        if (key.starts_with(intermediate_prefix))
            require(find(data, "native_windows", key) && *find(data, "native_windows", key) == value,
                "An explicit return rewrote its independent intermediate achievement");
    check_gate(data, fixture.after, fixture.id, true);
    const auto preserved = data;
    prepare_lesson_state(data, fixture.after);
    require(data == preserved, "An explicit return replays its merge after adoption");
    reset_compiler_puzzle(data, fixture.after, after);
    prepare_lesson_state(data, fixture.after);
    require(!compiler_puzzle_completed(data, fixture.after, after)
        && !compiler_puzzle_completed(data, first_stage, first)
        && compiler_puzzle_case_count(data, fixture.after, after) == 0,
        "Reset resurrected a proof through an authenticated return");
    std::cout << "PASS case-order explicit return: same-program missing proof appended, old proofs and middle achievement exact, no resurrection after Reset\n";
}
void empty_draft(const Fixture& fixture) {
    auto data = seeded_state(fixture.before, fixture.id, false);
    const auto original = data;
    prepare_lesson_state(data, fixture.after);
    preserved_drafts(original, data, fixture.before, fixture.after);
    const auto& after = editor(fixture.after, fixture.id);
    require(!get_draft(data, draft_key("programming", fixture.after, after),
        after.initial_code, after.standard_input.value_or("")).has_saved
        && !compiler_puzzle_completed(data, fixture.after, after),
        "A new case order invented an untouched draft or first-case execution");
}
void actual_reference_runs(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto& reference = editor(fixture.before, real_full_reference);
    const auto& fragment = editor(fixture.before, real_reference);
    require(reference.type == BlockType::code && !reference.code.empty()
        && reference.code == editor(fixture.after, real_full_reference).code,
        "The two real MSVC runs need the exact unchanged complete U11L7 reference source");
    // The later reference is a function-body excerpt of this already complete
    // published reader, not a separate translation unit or source replacement.
    const auto excerpt = reference.code.find(fragment.code);
    require(fragment.type == BlockType::code && !fragment.code.empty()
        && fragment.code == editor(fixture.after, real_reference).code
        && excerpt != std::string::npos
        && reference.code.find(fragment.code, excerpt + fragment.code.size()) == std::string::npos,
        "The published complete reference must contain its unchanged capacity fragment exactly once");
    std::size_t executions = 0;
    for (const std::size_t index : {std::size_t{0}, std::size_t{21}}) {
        CppRunRequest request;
        request.request_id = 300 + index; request.revision = 500 + index;
        request.source_utf8 = reference.code;
        request.stdin_utf8 = before.output_check->cases.at(index).standard_input;
        request.projects_directory = directory / ("msvc-" + std::to_string(index));
        request.compile_timeout = std::chrono::seconds(55);
        request.execution_timeout = std::chrono::seconds(5);
        require(compiler_runtime_request_allowed(before, request),
            "The authentic U11 reference violates its original runtime contract");
        const auto result = run_cpp(request);
        require(result.error_utf8.empty() && result.compilation.succeeded(),
            "Actual U11 case-order MSVC compilation failed: "
                + result.error_utf8 + result.compilation.stdout_bytes + result.compilation.stderr_bytes);
        const auto execution = capture_cpp_execution(request, result);
        require(execution.execution_completed
            && evaluate_cpp_output_check(*before.output_check, execution, request.revision).verdict
                == OutputVerdict::correct,
            "Actual U11 case-order execution failed its exact input/stdout/exit contract");
        auto data = seeded_state(fixture.before, fixture.id);
        (void)record_compiler_puzzle_case(data, fixture.before, before, request, execution,
            "Correcto\n" + before.output_check->cases.at(index).explanation);
        require(compiler_puzzle_case_count(data, fixture.before, before) == 1
            && compiler_puzzle_completed(data, fixture.before, before) == (index == 0),
            "The actual original capture accredited the wrong original first case");
        const auto original = data;
        prepare_lesson_state(data, fixture.after);
        require(compiler_puzzle_case_count(data, fixture.after, after) == 1
            && compiler_puzzle_completed(data, fixture.after, after) == (index == 21),
            "The actual authenticated capture did not follow its own case into the new order");
        check_gate(data, fixture.after, fixture.id, index == 21);
        preserved_drafts(original, data, fixture.before, fixture.after);
        const auto profile = directory / ("msvc-state-" + std::to_string(index));
        StateStore store(profile);
        require(store.load().preferences.empty(), "The real MSVC receipt profile is not isolated");
        store.save(data); StateStore reopened(profile);
        require(reopened.load() == data,
            "A real MSVC case-order receipt changed on physical reopening");
        ++executions;
        std::cout << "PASS actual MSVC U11L7 original case " << index
            << ": one capture, exact oracle, old/current first-case distinction and persisted receipt\n";
    }
    require(executions == 2, "Case-order QA omitted one of its two real MSVC executions");
}
Fixture read_fixture(const JsonObject& entry, const std::filesystem::path& directory,
        std::map<std::filesystem::path, std::string>& originals) {
    Fixture result;
    result.name = winrt::to_string(entry.GetNamedString(L"name"));
    result.id = winrt::to_string(entry.GetNamedString(L"id"));
    result.before_file = winrt::to_string(entry.GetNamedString(L"before"));
    result.after_file = winrt::to_string(entry.GetNamedString(L"after"));
    result.real = entry.GetNamedBoolean(L"real", false);
    const auto load = [&](std::string_view file) {
        const auto path = directory / std::string(file);
        const auto raw = bytes(path);
        originals.emplace(path, raw);
        auto lesson = parse_lesson_json(raw, "es");
        const auto metadata = parse_lesson_progress_metadata(raw, "es");
        require(lesson.id == metadata.id && lesson.pages.size() == metadata.pages.size(),
            "Full and metadata case-order loaders disagree about a complete publication");
        return lesson;
    };
    result.before = load(result.before_file);
    result.after = load(result.after_file);
    if (entry.HasKey(L"history")) for (const auto& value : entry.GetNamedArray(L"history"))
        result.history.push_back(load(winrt::to_string(value.GetObject().GetNamedString(L"source"))));
    require(!result.history.empty() && result.history.back().raw_json == result.after.raw_json,
        "Case-order history must end with its complete after publication");
    if (entry.HasKey(L"order")) for (const auto& value : entry.GetNamedArray(L"order")) {
        const auto number = value.GetNumber();
        require(number >= 0 && number == static_cast<double>(static_cast<std::size_t>(number)),
            "Case-order QA manifest contains a non-integer permutation");
        result.order.push_back(static_cast<std::size_t>(number));
    }
    if (result.name == "real-u11-expanded-order")
        require(result.real && sha256(result.before.raw_json)
            == "9e569e76b0450625bfa15a4a8a8c6ba9ab2fbb006047e1e6ab651636f0382ea8"
            && result.before.raw_json.size() == 78011
            && editor(result.before, result.id).output_check->cases.size() == 19,
            "The expansion composition lost the exact independently pinned original-19 Git source");
    return result;
}
std::size_t invalid_documents(const JsonArray& entries, const std::filesystem::path& directory) {
    std::size_t count = 0;
    for (const auto& value : entries) {
        const auto entry = value.GetObject();
        const auto name = winrt::to_string(entry.GetNamedString(L"name"));
        const auto raw = bytes(directory / winrt::to_string(entry.GetNamedString(L"file")));
        auto expected = winrt::to_string(entry.GetNamedString(L"message", L""));
        if (name == "later-stderr-change" || name == "later-comparison-change"
                || name == "later-target-change")
            expected = "Case ordering changed an existing case, channel or comparison mode";
        for (const bool metadata : {false, true}) {
            std::string message;
            try {
                if (metadata) (void)parse_lesson_progress_metadata(raw, "es");
                else (void)parse_lesson_json(raw, "es");
            } catch (const std::exception& error) { message = error.what(); }
            catch (const winrt::hresult_error& error) { message = winrt::to_string(error.message()); }
            require(!message.empty() && (expected.empty() || message.find(expected) != std::string::npos),
                "The native case-order loader accepted an invalid contract or used the wrong guard: " + name);
        }
        ++count;
    }
    require(count != 0, "Case-order QA needs contract/history negative documents");
    return count;
}


} // namespace

int main(int argc, char** argv) try {
    require(argc == 3, "Usage: compiler_case_order_state_tests <fixtures> <isolated-qa-root>");
    winrt::init_apartment();
    const std::filesystem::path fixtures(argv[1]);
    const auto qa = std::filesystem::path(argv[2]) /
        ("case-order-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    require_safe_path(fixtures); require_safe_path(qa);
    const auto manifest = JsonObject::Parse(winrt::to_hstring(bytes(fixtures / "manifest.json")));
    require(manifest.GetNamedNumber(L"version", 1) == 1, "Unsupported case-order QA manifest version");
    std::map<std::filesystem::path, std::string> originals;
    std::vector<Fixture> cases;
    std::size_t failures = 0, scenarios = 0, invalid = 0;
    const auto run = [&](std::string_view label, const auto& action) {
        try {
            action();
            return true;
        } catch (const winrt::hresult_error& error) {
            std::cerr << "FAIL case-order " << label << ": " << winrt::to_string(error.message()) << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL case-order " << label << ": " << error.what() << '\n';
        }
        ++failures;
        return false;
    };
    for (const auto& value : manifest.GetNamedArray(L"cases")) {
        const auto entry = value.GetObject();
        const auto name = winrt::to_string(entry.GetNamedString(L"name"));
        run("load " + name, [&] { cases.push_back(read_fixture(entry, fixtures, originals)); });
    }
    run("fixture inventory", [&] {
        require(!cases.empty(), "No authenticated compiler case-order fixtures were generated");
    });
    for (const auto& fixture : cases) {
        if (!run(fixture.name + " publication", [&] { verify_publication(fixture); })) continue;
        run(fixture.name + " conservation/import", [&] { scenarios += native_matrix(fixture, qa); });
        run(fixture.name + " chronological adoption", [&] { successive_adoption(fixture); });
    }
    const auto real = std::ranges::find(cases, "real-u11-six-movements", &Fixture::name);
    run("programs A/B", [&] {
        require(real != cases.end(), "Case-order QA omitted the complete real U11L7 22-case publication");
        different_programs(*real, qa);
    });
    run("untouched draft", [&] {
        require(real != cases.end(), "Case-order QA omitted the complete real U11L7 22-case publication");
        empty_draft(*real);
    });
    const auto returned = std::ranges::find(cases, "real-u11-return", &Fixture::name);
    run("explicit return", [&] {
        require(returned != cases.end(), "Case-order QA omitted its authenticated return");
        explicit_return(*returned);
    });
    run("invalid documents", [&] { invalid = invalid_documents(manifest.GetNamedArray(L"invalid"), fixtures); });
    run("actual MSVC references", [&] {
        require(real != cases.end(), "Case-order QA omitted the complete real U11L7 22-case publication");
        actual_reference_runs(*real, qa);
    });
    run("original fixture bytes", [&] {
        for (const auto& [path, original] : originals)
            require(bytes(path) == original, "Case-order QA rewrote an authenticated fixture publication");
    });
    if (failures) {
        std::cerr << "FAIL compiler case-order: " << failures
            << " independent QA groups failed; the remaining groups were still checked.\n";
        return 1;
    }
    std::cout << "PASS compiler case-order: " << cases.size() << " complete-source fixtures, "
        << scenarios << " native conservation/import scenarios, " << invalid
        << " invalid documents through both loaders, and exactly two real MSVC runs.\n"
        << "The conservation captures and three stored pause positions are synthetic QA models; "
        << "XML contains preserved JSON-v1 drafts/responses. No student profile, curriculum edit, "
        << "UI traversal, speech synthesis or listening is claimed.\n";
    return 0;
} catch (const winrt::hresult_error& error) {
    std::cerr << "FAIL compiler case-order: " << winrt::to_string(error.message()) << '\n';
    return 1;
} catch (const std::exception& error) {
    std::cerr << "FAIL compiler case-order: " << error.what() << '\n';
    return 1;
}

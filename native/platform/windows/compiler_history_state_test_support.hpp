#pragma once
// Shared isolated compiler-history QA support. The source models exercise
// state conservation; only callers of run_cpp claim actual compilation.
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

namespace lgaed::test::compiler_history {
using namespace lgaed;
using namespace curriculum;
using namespace state;
using namespace windows;
using namespace winrt::Windows::Data::Json;

inline void require(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}
template<class Fn> void rejects(Fn&& fn, std::string_view message) {
    bool refused = false;
    try { fn(); } catch (const std::exception&) { refused = true; }
    catch (const winrt::hresult_error&) { refused = true; }
    require(refused, message);
}
inline std::string bytes(const std::filesystem::path& path) {
    const auto data = read_bytes(path);
    return {data.begin(), data.end()};
}
inline void text_file(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(native_io_path(path.parent_path()));
    std::ofstream out(native_io_path(path), std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    require(out.good(), "Cannot write an isolated case-order QA file");
}
inline const Block& editor(const Lesson& lesson, std::string_view identity) {
    const auto position = stable_block_position(lesson, identity);
    require(position.has_value(), "Case-order fixture lost a stable block owner");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
inline std::string receipt_root(const Lesson& lesson, const Block& block) {
    return "compiler-puzzle/v1/" + lesson.locale + '/' + lesson.id + '/'
        + exercise_key(lesson, block) + '/';
}
inline std::string receipt_base(const Lesson& lesson, const Block& block) {
    return receipt_root(lesson, block) + sha256(reference_objective_frame(lesson, block)) + '/';
}
inline bool same_draft(const Draft& a, const Draft& b) {
    return a.source == b.source && a.input == b.input
        && a.indentation_json == b.indentation_json
        && a.selection_start == b.selection_start && a.selection_end == b.selection_end
        && a.has_saved == b.has_saved;
}
inline CppRunRequest model_request(const Block& block, std::string_view program) {
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
inline FreshExecution model_execution(const Block& block, const CppRunRequest& request,
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
inline void seed_case(StateData& data, const Lesson& lesson, const Block& block,
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
inline std::string personal_program(const Block& block, std::string_view marker) {
    return "    // QA state model " + std::string(marker) + "\r\n" + block.initial_code;
}
inline StateData seeded_state(const Lesson& lesson, std::string_view owner, bool save_draft = true) {
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
inline std::string xml_text(std::string_view value) {
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
inline void export_preferences(const std::filesystem::path& directory, const StateData& data) {
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
inline std::map<std::string, std::string> reviewed_metadata(const Lesson& lesson) {
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

inline void preserved_values(const StateData& before, const StateData& after,
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
inline void preserved_drafts(const StateData& original, const StateData& data,
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
inline void check_gate(const StateData& data, const Lesson& lesson,
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

inline std::vector<std::string> proof_values(const StateData& data, const Lesson& lesson, const Block& block) {
    const auto value = find(data, "native_windows", receipt_base(lesson, block) + "cases");
    if (!value) return {};
    require(std::holds_alternative<std::vector<std::string>>(*value),
        "QA expected a typed vector of native case proofs");
    return std::get<std::vector<std::string>>(*value);
}
inline std::vector<std::optional<std::size_t>> source_case_mapping(
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
inline std::vector<std::optional<std::size_t>> execution_case_mapping(
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
inline std::map<std::string, TextSemantics> case_semantics(const Block& block) {
    std::map<std::string, TextSemantics> result;
    for (const auto& [path, meanings] : block.text_semantics)
        if (path.starts_with("/outputCheck/cases/")) result.emplace(path, meanings);
    return result;
}
inline void no_old_feedback_lent(const StateData& data, const Lesson& lesson, const Block& block) {
    const auto feedback = compiler_puzzle_success_feedback(data, lesson, block);
    if (!feedback || feedback->text.empty()) return;
    const auto& first = block.output_check->cases.front();
    require(!first.explanation.empty() && feedback->text.ends_with(first.explanation),
        "Migrated first-case credit borrowed feedback from a different old case");
    if (block.text_semantics.contains("/outputCheck/cases/0/explanation"))
        require(feedback->source_path == "/outputCheck/cases/0/explanation",
            "A migrated marked guide lost its exact new first-case semantic path");
}
} // namespace lgaed::test::compiler_history

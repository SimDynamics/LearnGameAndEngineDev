// Isolated Windows QA. Model captures below test conservation and migration;
// only actual_reference_runs invokes MSVC. No curricular asset is rewritten.
#include "platform/windows/compiler_history_state_test_support.hpp"
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
using namespace lgaed::test::compiler_history;
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

void malformed_execution_receipt_never_clears_last_review(const Fixture& fixture) {
    // Authenticated, multi-case curricular owner. Phase captures here are
    // synthetic and never masquerade as a real MSVC compile/run result.
    const auto& block=editor(fixture.before,fixture.id);
    require(block.output_check && block.output_check->cases.size()>1 &&
        !block.compile_check,
        "The atomic receipt QA needs a real multi-case output editor");
    const auto program=personal_program(block,"atomic-qa");
    auto request=model_request(block,program);
    request.stdin_utf8=block.output_check->cases.front().standard_input;
    if(request.cmake && block.output_check->cases.front().executable)
        request.cmake->target=*block.output_check->cases.front().executable;
    const auto accepted=model_execution(block,request,0);
    require(evaluate_cpp_output_check(*block.output_check,accepted,request.revision).verdict==
        OutputVerdict::correct,
        "The recorded source is not an authentic first-case QA model");

    auto data=seeded_state(fixture.before,fixture.id);
    require(record_compiler_puzzle_case(data,fixture.before,block,request,accepted,
        "Correcto\nInitial QA result"),
        "Could not establish the original authenticated first-case receipt");
    auto rejected=accepted;
    rejected.standard_output="QA: intentionally wrong, not a successful result";
    require(evaluate_cpp_output_check(*block.output_check,rejected,request.revision).verdict==
        OutputVerdict::incorrect &&
        record_compiler_puzzle_review(data,fixture.before,block,request,rejected,
            "Incorrecto\nLatest evaluated QA attempt"),
        "Could not establish the latest rejected attempt alongside the achievement");
    const auto prefix=receipt_base(fixture.before,block);
    const auto feedback_key=prefix+"review/feedback";
    require(find(data,"native_windows",feedback_key)!=nullptr &&
        compiler_puzzle_completed(data,fixture.before,block),
        "The authentic receipt or the last reviewed attempt was not preserved");
    const auto valid_state=data;

    // A damaged receipt is not evidence of a failed edit. Neither retrying
    // a correct run nor visiting an edited draft may erase the last review.
    for(const std::string_view field:{"cases","program","completed","feedback"}) {
        auto corrupt=valid_state;
        const auto key=prefix+std::string(field);
        require(find(corrupt,"native_windows",key)!=nullptr,
            "The expected original receipt field is not stored");
        corrupt.preferences["native_windows"][key]=std::int64_t{42};
        const auto before=corrupt;
        auto candidate=request;
        if(field=="feedback") {
            candidate=model_request(block,personal_program(block,"alternate-program"));
            candidate.stdin_utf8=request.stdin_utf8;
            if(candidate.cmake && block.output_check->cases.front().executable)
                candidate.cmake->target=*block.output_check->cases.front().executable;
        }
        const auto result=model_execution(block,candidate,0);
        rejects([&] {
            (void)record_compiler_puzzle_case(corrupt,fixture.before,block,candidate,
                result,"Correcto\nRetry of QA model");
        }, "A malformed stored execution receipt did not reject a later success");
        require(corrupt==before && find(corrupt,"native_windows",feedback_key)!=nullptr,
            "A rejected successful recheck erased the review or changed the old achievement");
        if(field!="feedback") {
            auto edited=request;edited.source_utf8+="\n// another unsent QA draft";
            rejects([&] { invalidate_compiler_puzzle_attempt(corrupt,fixture.before,block,edited); },
                "An edit ignored a malformed saved program, cases or completion proof");
            require(corrupt==before,
                "An edit discarded a rejected attempt before discovering corrupted state");
        }
    }
    auto recovered=valid_state;
    require(record_compiler_puzzle_case(recovered,fixture.before,block,request,accepted,
        "Correcto\nValid QA recheck"),
        "A genuine recheck was blocked by the previous malformed receipt tests");
    require(!find(recovered,"native_windows",feedback_key) &&
        compiler_puzzle_completed(recovered,fixture.before,block),
        "A valid later success failed to clear the last rejection or changed the achievement");
    std::cout<<"PASS compiler receipt atomicity: damaged cases, program, completion and feedback "
        "do not erase latest review; valid recheck clears review and preserves first-case completion\n";
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
    run("atomic malformed execution receipt", [&] {
        require(real != cases.end(), "Atomic QA needs the complete original U11L7 publication");
        malformed_execution_receipt_never_clears_last_review(*real);
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

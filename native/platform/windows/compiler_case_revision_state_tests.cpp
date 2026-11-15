// Isolated Windows QA for authenticated replacement of a compiler's case list.
// Model captures prove state conservation only. actual_reference_runs performs
// the six separately identified MSVC compilations/executions.
#include "platform/windows/compiler_history_state_test_support.hpp"

#include <functional>
#include <numeric>

namespace {
using namespace lgaed;
using namespace curriculum;
using namespace state;
using namespace windows;
using namespace lgaed::test::compiler_history;
using namespace winrt::Windows::Data::Json;

struct Fixture {
    std::string name, id, before_file, after_file, reference_file, reference_source;
    bool real = false, snapshot_epoch_only = false;
    Lesson before, after;
    std::optional<Lesson> snapshot_source, receipt_source;
    std::vector<Lesson> history;
};
struct RealSource {
    std::string_view tag, owner, reference, signature, end_marker;
    std::string_view source_sha, reference_sha, initial_sha;
    std::size_t source_bytes, current_cases;
    std::string_view first_input, first_output, initial_output;
};
constexpr std::array real_sources{
    RealSource{"u4l4", "block_0005_0002", "block_0006_0002", "bool tamano_valido(", "\nint main()",
        "25358f6748add1cba92d891690d9dab1a53c036f88f6b5d13dce55eeb960edb8",
        "5a0e2c7ec860e9c6419f2d42578387c6e21ac842c2d46c1b05186882013c1a13",
        "084b773dfe8fb54e6e6c71716626ed0fb95d2f66edb1adcd41bc434093d38a05", 166347, 16,
        "1\n", "1:1:1\n", "1:0:91\n"},
    RealSource{"u4l6", "block_0006_0002", "block_0007_0002", "Conteo contar_lineas(", "\nstd::string datos(",
        "5379136f740d584cc715a575ef446da366f5ba9788681d0b87f3372029c367e6",
        "2ab36bda4d0a1279f13dc68ab4d37e0f3076ddea588171b62f3847bcaccbdac5",
        "40c0f4c0600fd68c71ecfff52da531f396a478ce1e015086463e72f098b1d466", 536987, 9,
        "LF\n", "LF:2:0:0:0\n", "LF:0:0:0:0\n"},
    RealSource{"u4l7", "block_0006_0003", "block_0007_0002", "Totales contar_tipos(", "\nint main()",
        "c2f47d0b21244feaa54c335849369dd0029ef4f152448798d9ff4698616cf671",
        "9ea517e0ca1eebed13d5d06a25cbc06a328c71ac9fc93e6bf8f957748dd8756f",
        "5328b550737206824b87e72aff3d20121c3b356a2eb2631bee3049ba166f2d99", 568093, 8,
        "1\n4\ntextura 5\nmalla 240\nsonido 8\nsonido 2\n",
        "textura=1 malla=1 sonido=2 bytes=255\n", "textura=0 malla=0 sonido=0 bytes=0\n"}
};
const RealSource* real_source(const Fixture& fixture) {
    for (const auto& item : real_sources)
        if (fixture.before_file == std::string(item.tag) + ".before.json") return &item;
    return nullptr;
}
std::string case_pin(const Lesson& lesson) {
    return "lesson_" + lesson.id + "_state_compiler_case_revisions_v1_" + lesson.locale;
}
std::string order_pin(const Lesson& lesson) {
    return "lesson_" + lesson.id + "_state_compiler_case_order_revisions_v1_" + lesson.locale;
}
std::map<std::string, std::vector<std::string>> journal_pins(const Lesson& lesson) {
    std::map<std::string, std::vector<std::string>> result;
    for (const auto& entry : lesson.state_layout.compiler_case_revisions)
        result[case_pin(lesson)].push_back(entry.record_sha256);
    for (const auto& entry : lesson.state_layout.compiler_case_order_revisions)
        result[order_pin(lesson)].push_back(entry.record_sha256);
    return result;
}
void check_journals(const StateData& data, const Lesson& lesson) {
    const auto expected = journal_pins(lesson);
    for (const auto& key : {case_pin(lesson), order_pin(lesson)}) {
        const auto stored = find(data, "learning_progress", key);
        if (!expected.contains(key)) {
            require(!stored, "A native journal pin has no authenticated source: " + key);
            continue;
        }
        require(stored && std::holds_alternative<std::vector<std::string>>(*stored)
            && std::get<std::vector<std::string>>(*stored) == expected.at(key),
            "A typed native journal differs from its complete authenticated source: " + key);
    }
}
void check_conservation(const StateData& before, const StateData& after,
        const Lesson& previous_lesson, const Lesson& current_lesson, std::string_view owner) {
    const auto previous = reviewed_metadata(previous_lesson);
    const auto current = reviewed_metadata(current_lesson);
    auto unchanged = before;
    for (const auto& [key, expected] : current) {
        const auto old_value = find(before, "learning_progress", key);
        const auto new_value = find(after, "learning_progress", key);
        require(new_value && std::holds_alternative<std::string>(*new_value),
            "A reviewed metadata family changed type or disappeared: " + key);
        const auto new_rows = JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*new_value)));
        require(winrt::to_string(new_rows.Stringify()) == expected,
            "Reviewed metadata differs from the complete current layout: " + key);
        if (old_value) {
            require(previous.contains(key) && std::holds_alternative<std::string>(*old_value),
                "A reviewed metadata family changed its original type or owner: " + key);
            const auto old_rows = JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*old_value)));
            require(winrt::to_string(old_rows.Stringify()) == previous.at(key)
                && new_rows.Size() >= old_rows.Size(), "Reviewed metadata lost its exact predecessor: " + key);
            for (unsigned i = 0; i < old_rows.Size(); ++i)
                require(old_rows.GetAt(i).Stringify() == new_rows.GetAt(i).Stringify(),
                    "Reviewed metadata rewrote a historical row: " + key);
            if (old_rows.Size() == new_rows.Size())
                require(*old_value == *new_value, "Unchanged metadata lost its original bytes: " + key);
        } else require(!previous.contains(key), "Original state omitted its reviewed metadata: " + key);
        unchanged.preferences["learning_progress"][key] = *new_value;
    }
    for (const auto& [key, expected] : previous)
        require(current.contains(key), "The current layout removed a reviewed metadata family: " + key);
    check_journals(before, previous_lesson);
    check_journals(after, current_lesson);
    const auto old_journals = journal_pins(previous_lesson);
    const auto new_journals = journal_pins(current_lesson);
    for (const auto& [key, rows] : new_journals) {
        if (old_journals.contains(key)) {
            const auto& old_rows = old_journals.at(key);
            require(rows.size() >= old_rows.size() && std::equal(old_rows.begin(), old_rows.end(), rows.begin()),
                "A case or order journal changed its original prefix: " + key);
        }
        unchanged.preferences["learning_progress"][key] = rows;
    }
    for (const auto& [key, rows] : old_journals)
        require(new_journals.contains(key), "A case or order journal disappeared: " + key);
    // Only the fully derived values above are normalized. The common checker
    // still rejects any other new/changed preference outside this editor.
    preserved_values(unchanged, after, current_lesson, owner, case_pin(current_lesson));
}
std::vector<std::optional<std::size_t>> retained_cases(const Block& before, const Block& after) {
    std::vector<std::optional<std::size_t>> result;
    for (const auto& candidate : after.output_check->cases) {
        std::optional<std::size_t> match;
        if (before.output_check->input_comparison == after.output_check->input_comparison)
            for (std::size_t i = 0; i < before.output_check->cases.size(); ++i)
                if (same_output_case_requirements(before.output_check->cases[i], candidate)) {
                    match = i; break;
                }
        result.push_back(match);
    }
    return result;
}
bool contains_case(const Block& source, const OutputCase& candidate, TextComparison comparison) {
    return source.output_check->input_comparison == comparison
        && std::ranges::any_of(source.output_check->cases, [&](const auto& value) {
            return same_output_case_requirements(value, candidate);
        });
}
void same_version(const Lesson& source, const Block& expected,
        const Lesson& context, const Block& historical) {
    require(historical.stable_id == expected.stable_id
        && test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(historical.raw_json)))
            == test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(expected.raw_json)))
        && reference_objective_frame(context, historical) == reference_objective_frame(source, expected)
        && historical.initial_code == expected.initial_code
        && historical.project_files == expected.project_files
        && historical.source_format_original_initial_code.value_or(historical.initial_code)
            == expected.source_format_original_initial_code.value_or(expected.initial_code)
        && historical.source_format_original_project_files.value_or(historical.project_files)
            == expected.source_format_original_project_files.value_or(expected.project_files)
        && case_semantics(historical) == case_semantics(expected),
        "A historical case view borrowed source, contracts, formatting or meanings from another publication");
    require(historical.output_check && expected.output_check
        && historical.output_check->input_comparison == expected.output_check->input_comparison
        && historical.output_check->cases.size() == expected.output_check->cases.size(),
        "A historical case view changed its parsed comparison or case count");
    for (std::size_t i = 0; i < expected.output_check->cases.size(); ++i) {
        const auto& a = historical.output_check->cases[i];
        const auto& b = expected.output_check->cases[i];
        require(same_output_case_requirements(a, b) && a.explanation == b.explanation
            && a.review_feedback.size() == b.review_feedback.size(),
            "A historical parsed case lost its exact evaluated requirements or public guidance");
        for (std::size_t j = 0; j < a.review_feedback.size(); ++j)
            require(a.review_feedback[j].outputs == b.review_feedback[j].outputs
                && a.review_feedback[j].text == b.review_feedback[j].text,
                "Historical feedback was attached to a different case or verdict");
    }
}
void verify_publication(const Fixture& fixture) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    require(before.type == BlockType::cpp_compiler && after.type == before.type
        && before.output_check && after.output_check && !after.compiler_case_revisions.empty()
        && exercise_key(fixture.before, before) == exercise_key(fixture.after, after)
        && draft_key("programming", fixture.before, before) == draft_key("programming", fixture.after, after),
        "The fixture is not an authenticated review of the same compiler owner");
    const auto old_layout = JsonObject::Parse(winrt::to_hstring(fixture.before.state_layout.raw_json));
    const auto new_layout = JsonObject::Parse(winrt::to_hstring(fixture.after.state_layout.raw_json));
    for (const auto* key : {L"origin", L"family"})
        require(test::compile_check_test_fingerprint(old_layout.GetNamedValue(key))
            == test::compile_check_test_fingerprint(new_layout.GetNamedValue(key)),
            "A case revision changed original addresses or Android state families");
    std::vector<const Lesson*> sources{&fixture.before};
    for (const auto& stage : fixture.history) sources.push_back(&stage);
    std::size_t archives_from_publications = 0;
    for (const auto& revision : after.compiler_case_revisions) {
        const auto original = compiler_case_revision_previous_block(after, revision);
        const auto revised = compiler_case_revision_after_block(after, revision);
        require(original.raw_json == revision.raw_json && revised.raw_json == revision.after_raw_json
            && original.output_check && revised.output_check
            && original.stable_id == fixture.id && revised.stable_id == fixture.id,
            "The native case journal lost one complete authenticated endpoint");
        auto original_raw = JsonObject::Parse(winrt::to_hstring(original.raw_json));
        auto revised_raw = JsonObject::Parse(winrt::to_hstring(revised.raw_json));
        auto original_check = original_raw.GetNamedObject(L"outputCheck");
        auto revised_check = revised_raw.GetNamedObject(L"outputCheck");
        original_check.Remove(L"cases"); revised_check.Remove(L"cases");
        original_raw.SetNamedValue(L"outputCheck", original_check);
        revised_raw.SetNamedValue(L"outputCheck", revised_check);
        require(test::compile_check_test_fingerprint(original_raw)
            == test::compile_check_test_fingerprint(revised_raw),
            "A case revision changed its source, client, modes or fields outside the case list");
        std::size_t matched_endpoints = 0;
        for (const auto& endpoint : {original, revised}) {
            const auto endpoint_sha = test::compile_check_test_fingerprint(
                JsonObject::Parse(winrt::to_hstring(endpoint.raw_json)));
            const Lesson* matching = nullptr;
            const bool after_edge = endpoint.raw_json == revised.raw_json;
            const auto desired_prefix = revision.editorial_index + static_cast<std::size_t>(after_edge);
            for (const auto* source : sources) {
                const auto& candidate = editor(*source, fixture.id);
                if (test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(candidate.raw_json)))
                        != endpoint_sha) continue;
                const auto count = JsonObject::Parse(winrt::to_hstring(source->state_layout.raw_json))
                    .GetNamedArray(L"editorial").Size();
                if (count != desired_prefix) continue;
                // Select the real publication independently of the semantic
                // values we are about to test. Same-prefix later authoring is
                // allowed: before uses its latest source, after its first.
                matching = source;
                if (after_edge) break;
            }
            if (matching) {
                same_version(*matching, editor(*matching, fixture.id), fixture.after, endpoint);
                ++archives_from_publications;
                ++matched_endpoints;
            }
        }
        const auto existed = std::ranges::any_of(before.compiler_case_revisions, [&](const auto& prior) {
            return prior.record_sha256 == revision.record_sha256;
        });
        require(existed || matched_endpoints == 2,
            "A new case-review edge needs both independently supplied complete publications");
    }
    require(archives_from_publications != 0,
        "No case archive matched an independently supplied full publication");
    for (const auto& revision : after.compiler_case_order_revisions) {
        const auto historical = compiler_case_order_previous_block(after, revision);
        const Lesson* matching = nullptr;
        for (const auto* source : sources) {
            const auto count = JsonObject::Parse(winrt::to_hstring(source->state_layout.raw_json))
                .GetNamedArray(L"editorial").Size();
            if (count == revision.editorial_index && test::compile_check_test_fingerprint(
                    JsonObject::Parse(winrt::to_hstring(editor(*source, fixture.id).raw_json))) == revision.sha256)
                matching = source;
        }
        if (matching) same_version(*matching, editor(*matching, fixture.id), fixture.after, historical);
        const auto existed = std::ranges::any_of(before.compiler_case_order_revisions, [&](const auto& prior) {
            return prior.record_sha256 == revision.record_sha256;
        });
        require(existed || matching,
            "A newly composed permutation lost its independently supplied predecessor case list");
    }
    for (const auto* version : sources) {
        const auto metadata = parse_lesson_progress_metadata(version->raw_json, version->locale);
        const auto& active = editor(*version, fixture.id);
        const auto& compact = editor(metadata, fixture.id);
        require(reference_objective_frame(*version, active) == reference_objective_frame(metadata, compact)
            && active.compiler_case_revisions.size() == compact.compiler_case_revisions.size()
            && active.compiler_case_order_revisions.size() == compact.compiler_case_order_revisions.size(),
            "Full and metadata loaders disagree about a case revision or its active objective");
        for (std::size_t i = 0; i < active.compiler_case_revisions.size(); ++i)
            require(active.compiler_case_revisions[i].record_sha256 == compact.compiler_case_revisions[i].record_sha256,
                "The metadata loader changed a full journal record hash");
    }
    if (const auto* spec = real_source(fixture)) {
        require(fixture.real && fixture.id == spec->owner && fixture.before.raw_json.size() == spec->source_bytes
            && sha256(fixture.before.raw_json) == spec->source_sha
            && before.output_check->cases.size() == 4 && after.output_check->cases.size() == spec->current_cases
            && before.initial_code == after.initial_code && sha256(before.initial_code) == spec->initial_sha,
            "The real AssetCheck fixture lost its exact 2a470e publication, unchanged client or expected case count");
        const auto& first = after.output_check->cases.front();
        require(first.standard_input == spec->first_input && first.outputs == std::vector<std::string>{std::string(spec->first_output)}
            && first.exit_code == 0 && !contains_case(before, first, after.output_check->input_comparison),
            "The real first case is not the independent, discriminating input requested by the author");
        const auto& function = editor(fixture.before, spec->reference);
        require(function.type == BlockType::code && function.code == editor(fixture.after, spec->reference).code,
            "A real case revision changed its protected published solution function");
        const auto start = before.initial_code.find(spec->signature);
        require(start != std::string::npos && (start == 0 || before.initial_code[start - 1] == '\n')
            && before.initial_code.find(spec->signature, start + spec->signature.size()) == std::string::npos,
            "The unchanged initial program has no unique reviewed function definition");
        const auto end = before.initial_code.find(spec->end_marker, start + spec->signature.size());
        require(end != std::string::npos, "The published client suffix is absent");
        auto solution = function.code;
        while (!solution.empty() && solution.back() == '\n') solution.pop_back();
        const auto assembled = before.initial_code.substr(0, start) + solution + '\n' + before.initial_code.substr(end);
        require(fixture.reference_source == assembled && sha256(assembled) == spec->reference_sha,
            "The executable reference is not the published function inside the exact unchanged initial client");
        if (spec->tag == "u4l7") {
            const auto mapping = retained_cases(before, after);
            for (std::size_t old_index = 0; old_index < 4; ++old_index)
                require(std::ranges::count(mapping, std::optional<std::size_t>{old_index}) == 1,
                    "U4L7 lost or duplicated a complete historical reset contrast");
        }
    }
    if (fixture.name == "real-u11-closed-expansion") {
        require(fixture.real && fixture.id == "block_0003_0005" && fixture.history.size() == 2
            && fixture.before.raw_json.size() == 78011
            && sha256(fixture.before.raw_json) == "9e569e76b0450625bfa15a4a8a8c6ba9ab2fbb006047e1e6ab651636f0382ea8"
            && fixture.history.front().raw_json.size() == 199237
            && sha256(fixture.history.front().raw_json) == "5f0b750fed94e1200c9a7c9be7f1247b2396ac86556685dab9abed4c3a7bed88"
            && before.output_check->cases.size() == 19
            && editor(fixture.history.front(), fixture.id).output_check->cases.size() == 21
            && after.output_check->cases.size() == 20 && after.compiler_revision.has_value(),
            "The closed expansion composition lost its exact complete original-19 or published-21 Git source");
        require(test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(before.raw_json)))
            == test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(after.compiler_revision->raw_json))),
            "The closed compiler archive is not the exact independently published original owner");
    }
}

void check_current(const Fixture& fixture, const StateData& original,
        const StateData& data, const std::vector<std::size_t>& seeds) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto mapping = retained_cases(before, after);
    const auto was_seeded = [&](const auto& index) {
        return index && std::ranges::find(seeds, *index) != seeds.end();
    };
    const auto expected_count = static_cast<std::size_t>(std::ranges::count_if(mapping, was_seeded));
    const bool completed = !mapping.empty() && was_seeded(mapping.front());
    require(compiler_puzzle_case_count(data, fixture.after, after) == expected_count
        && compiler_puzzle_completed(data, fixture.after, after) == completed,
        "A case revision lost exact same-program evidence or credited an unexecuted independent case");
    require(proof_values(data, fixture.before, before) == proof_values(original, fixture.before, before)
        && compiler_puzzle_case_count(data, fixture.before, before) == compiler_puzzle_case_count(original, fixture.before, before)
        && compiler_puzzle_completed(data, fixture.before, before) == compiler_puzzle_completed(original, fixture.before, before)
        && compiler_puzzle_feedback(data, fixture.before, before) == compiler_puzzle_feedback(original, fixture.before, before),
        "A case revision rewrote the original version's exact proofs, result or feedback");
    const auto current_proofs = proof_values(data, fixture.after, after);
    if (!current_proofs.empty()) {
        require(current_proofs.size() == after.output_check->cases.size(), "The current proof vector has the wrong shape");
        for (std::size_t i = 0; i < mapping.size(); ++i)
            require((!current_proofs[i].empty()) == was_seeded(mapping[i]),
                "A reused proof moved to a different full input/output/channel requirement");
    }
    if (!completed) require(!compiler_puzzle_success_feedback(data, fixture.after, after),
        "An unproved first case borrowed the old aggregate's success feedback");
    if (completed && reference_objective_frame(fixture.before, before)
            != reference_objective_frame(fixture.after, after))
        no_old_feedback_lent(data, fixture.after, after);
    check_gate(data, fixture.after, fixture.id, completed);
    preserved_drafts(original, data, fixture.before, fixture.after);
    check_journals(data, fixture.after);
}
void reject_corrupt_state(const Fixture& fixture, const StateData& original) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto base = receipt_base(fixture.before, before);
    const auto refusal = [&](StateData damaged, std::string_view reason) {
        const auto exact = damaged;
        rejects([&] { prepare_lesson_state(damaged, fixture.after); }, reason);
        require(damaged == exact, "A rejected case revision partially mutated existing state");
    };
    auto bad = original;
    bad.preferences["native_windows"][base + "program"] = std::int64_t{42};
    refusal(bad, "A case revision accepted a wrongly typed historical program");
    const auto proofs = proof_values(original, fixture.before, before);
    if (!proofs.empty()) {
        auto forged = proofs;
        const auto proof = std::ranges::find_if(forged, [](const auto& value) { return !value.empty(); });
        require(proof != forged.end(), "Corruption QA needs one genuine model receipt");
        *proof = "forged-case-proof";
        bad = original; bad.preferences["native_windows"][base + "cases"] = forged;
        refusal(bad, "A case revision accepted an invented case proof");
        bad = original; bad.preferences["native_windows"][base + "program"] = std::string("another program");
        refusal(bad, "A case revision accepted proofs captured for another program");
        bad = original; bad.preferences["native_windows"][base + "completed"] = std::string("forged-seal");
        refusal(bad, "A case revision accepted an invented completion seal");
    }
}
std::set<std::string> declared_adoption_markers(const Lesson& lesson) {
    std::set<std::string> result;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) {
        if (block.type != BlockType::cpp_compiler) continue;
        const auto root = receipt_root(lesson, block);
        std::optional<std::size_t> first_edge;
        auto expansion = block;
        for (const auto& revision : block.compiler_case_order_revisions) {
            result.insert(root + "case-order-adopted/" + revision.record_sha256);
            if (!first_edge || revision.editorial_index < *first_edge) {
                first_edge = revision.editorial_index;
                expansion = compiler_case_order_previous_block(block, revision);
            }
        }
        for (const auto& revision : block.compiler_case_revisions) {
            result.insert(root + "case-revision-adopted/" + revision.record_sha256);
            if (!first_edge || revision.editorial_index < *first_edge) {
                first_edge = revision.editorial_index;
                expansion = compiler_case_revision_previous_block(block, revision);
            }
        }
        if (block.compiler_revision)
            result.insert(root + "revision-adopted/" + sha256(reference_objective_frame(lesson, expansion)));
    }
    return result;
}
void imported_state(const Fixture& fixture, const StateData& original,
        const std::filesystem::path& directory, std::string_view mode) {
    auto android = original;
    android.preferences.erase("native_windows");
    const auto prefix = fixture.name + '-' + std::string(mode);
    const auto exported = directory / (prefix + "-xml");
    export_preferences(exported, android);
    const auto preview = inspect_android_export(exported);
    require(preview.data == android, "XML inspection changed typed preferences or embedded JSON-v1");
    validate_lesson_state(preview.data, fixture.after);
    require(preview.data == android, "Read-only import validation mutated its input");
    const auto profile = directory / (prefix + "-import");
    commit_android_import(preview, profile);
    StateStore store(profile);
    auto data = store.load();
    require(data == android, "Android import commit changed the inspected source state");
    prepare_lesson_state(data, fixture.after);
    validate_lesson_state(data, fixture.after);
    // Import legitimately recreates adoption markers for other authenticated
    // compiler owners too. Derive those exact bool keys from their journals;
    // no execution proof or arbitrary native-domain value is exempted.
    const auto markers = declared_adoption_markers(fixture.after);
    auto with_markers = android;
    for (const auto& key : markers) {
        const auto value = find(data, "native_windows", key);
        require(value && std::holds_alternative<bool>(*value) && std::get<bool>(*value),
            "Import omitted or changed an authenticated adoption marker: " + key);
        with_markers.preferences["native_windows"][key] = true;
    }
    if (const auto domain = data.preferences.find("native_windows"); domain != data.preferences.end())
        for (const auto& [key, value] : domain->second)
            require(markers.contains(key) && std::holds_alternative<bool>(value) && std::get<bool>(value),
                "Import manufactured a proof, program or undeclared adoption marker: " + key);
    check_conservation(with_markers, data, fixture.before, fixture.after, fixture.id);
    preserved_drafts(android, data, fixture.before, fixture.after);
    for (const auto& page : fixture.after.pages) for (const auto& block : page.blocks)
        if (block.type == BlockType::cpp_compiler && block.output_check)
            require(!compiler_puzzle_completed(data, fixture.after, block)
                && compiler_puzzle_case_count(data, fixture.after, block) == 0,
                "Android answers or JSON-v1 drafts manufactured native compiler execution credit");
    for (const auto& version : fixture.history)
        require(!compiler_puzzle_completed(data, version, editor(version, fixture.id))
            && compiler_puzzle_case_count(data, version, editor(version, fixture.id)) == 0,
            "Android import invented a historical compiler receipt");
    store.save(data); StateStore reopened(profile);
    require(reopened.load() == data, "Imported revised state changed on physical reopening");
    for (const auto& file : preview.files)
        require(sha256(read_bytes(exported / file.relative_path)) == file.sha256
            && sha256(read_bytes(profile / "state/active/android" / file.relative_path)) == file.sha256,
            "Import changed an original XML file or its archived bytes");
    auto forbidden = original;
    forbidden.preferences["native_windows"]["case-revision-reserved-domain-negative"] = true;
    const auto invalid = directory / (prefix + "-reserved");
    export_preferences(invalid, forbidden);
    rejects([&] { (void)inspect_android_export(invalid); }, "Android import accepted the reserved execution domain");
}
void check_reset(const Fixture& fixture, const StateData& original, const StateData& adopted) {
    const auto& after = editor(fixture.after, fixture.id);
    auto data = adopted, expected = adopted;
    const auto root = receipt_root(fixture.after, after);
    reset_compiler_puzzle(data, fixture.after, after);
    if (const auto domain = expected.preferences.find("native_windows"); domain != expected.preferences.end())
        std::erase_if(domain->second, [&](const auto& value) { return value.first.starts_with(root); });
    require(data == expected, "Reset changed a draft, answer, pause or another editor's state");
    prepare_lesson_state(data, fixture.after);
    std::vector<const Lesson*> versions{&fixture.before, &fixture.after};
    for (const auto& stage : fixture.history) versions.push_back(&stage);
    for (const auto* version : versions)
        require(!compiler_puzzle_completed(data, *version, editor(*version, fixture.id))
            && compiler_puzzle_case_count(data, *version, editor(*version, fixture.id)) == 0,
            "Preparing after Reset resurrected a proof through case/order history or an explicit return");
    require(narration::read_history(data) == narration::read_history(original), "Reset changed one of three valid pauses");
    preserved_drafts(original, data, fixture.before, fixture.after);
    check_journals(data, fixture.after);
}
std::size_t native_matrix(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto mapping = retained_cases(before, after);
    std::vector<std::pair<std::string, std::vector<std::size_t>>> modes{{"no-receipt", {}}, {"old-first", {0}}};
    if (!mapping.empty() && mapping.front() && *mapping.front() != 0)
        modes.push_back({"current-first-was-optional", {*mapping.front()}});
    const auto retained = std::ranges::find_if(mapping, [](const auto& value) { return value && *value != 0; });
    if (retained != mapping.end() && (mapping.empty() || !mapping.front() || *retained != mapping.front()))
        modes.push_back({"retained-optional", {**retained}});
    std::vector<std::size_t> all;
    for (std::size_t i = 1; i < before.output_check->cases.size(); ++i) all.push_back(i);
    all.push_back(0);
    modes.push_back({"all-old-cases", all});
    std::size_t completed_modes = 0, failed_modes = 0;
    for (const auto& [mode, seeds] : modes) {
      try {
        auto original = seeded_state(fixture.before, fixture.id);
        const auto program = personal_program(before, "case-revision-A");
        for (const auto index : seeds) seed_case(original, fixture.before, before, program, index);
        validate_lesson_state(original, fixture.before);
        const auto profile = directory / (fixture.name + '-' + mode + "-native");
        StateStore store(profile);
        require(store.load().preferences.empty(), "Case-revision state profile is not isolated");
        store.save(original);
        auto data = store.load();
        require(data == original && decode(encode(data)) == original,
            "Native transport or physical reopen changed the complete predecessor state");
        validate_lesson_state(data, fixture.after);
        require(data == original, "Read-only revised-state validation changed its source");
        prepare_lesson_state(data, fixture.after);
        validate_lesson_state(data, fixture.after);
        check_conservation(original, data, fixture.before, fixture.after, fixture.id);
        check_current(fixture, original, data, seeds);
        const auto adopted = data;
        prepare_lesson_state(data, fixture.after);
        require(data == adopted, "Case revision adoption is not idempotent");
        auto downgrade = data;
        if (journal_pins(fixture.before) != journal_pins(fixture.after)) {
            rejects([&] { prepare_lesson_state(downgrade, fixture.before); },
                "A historical publication removed a pinned case/order review");
            require(downgrade == data, "Refusing a journal downgrade changed saved data");
        } else {
            validate_lesson_state(downgrade, fixture.before);
            require(downgrade == data, "Reading a historical guide changed saved data");
        }
        for (const auto& [key, rows] : journal_pins(fixture.after)) {
            auto damaged = data;
            auto bad_rows = rows; bad_rows.front() = std::string(64, '0');
            damaged.preferences["learning_progress"][key] = bad_rows;
            const auto exact = damaged;
            rejects([&] { prepare_lesson_state(damaged, fixture.after); }, "A rewritten case/order pin was accepted");
            require(damaged == exact, "Refusing a rewritten journal pin partially changed state");
            damaged = data; damaged.preferences["learning_progress"][key] = std::string("wrong typed journal");
            const auto typed = damaged;
            rejects([&] { prepare_lesson_state(damaged, fixture.after); }, "A case/order journal changed its native type");
            require(damaged == typed, "Refusing a wrongly typed pin changed state");
        }
        const auto marker = receipt_root(fixture.after, after) + "case-revision-adopted/"
            + after.compiler_case_revisions.front().record_sha256;
        auto damaged = data; damaged.preferences["native_windows"][marker] = false;
        const auto exact = damaged;
        rejects([&] { prepare_lesson_state(damaged, fixture.after); }, "An invalid case adoption marker was accepted");
        require(damaged == exact, "Refusing an invalid adoption marker changed state");
        store.save(data); StateStore reopened(profile);
        require(reopened.load() == data, "A case revision changed on physical reopening");
        imported_state(fixture, original, directory, mode);
        reject_corrupt_state(fixture, original);
        check_reset(fixture, original, data);
        std::cout << "PASS case-revision state " << fixture.name << '/' << mode
            << ": exact case/program, original receipts/drafts/three pauses, typed pins, import/reopen, atomic refusal and Reset\n";
        ++completed_modes;
      } catch (const winrt::hresult_error& error) {
        std::cerr << "FAIL case-revision state " << fixture.name << '/' << mode
            << ": " << winrt::to_string(error.message()) << '\n';
        ++failed_modes;
      } catch (const std::exception& error) {
        std::cerr << "FAIL case-revision state " << fixture.name << '/' << mode
            << ": " << error.what() << '\n';
        ++failed_modes;
      }
    }
    require(failed_modes == 0, "Independent state modes failed; the remaining modes were still checked");
    return completed_modes;
}

void check_historical_selection(const Fixture& fixture, const StateData& data) {
    const auto& after = editor(fixture.after, fixture.id);
    std::vector<const Lesson*> versions{&fixture.before};
    for (const auto& stage : fixture.history) versions.push_back(&stage);
    std::set<std::string> seen{reference_objective_frame(fixture.after, after)};
    const Lesson* selected = nullptr;
    const Lesson* partial = nullptr;
    for (auto it = versions.rbegin(); it != versions.rend(); ++it) {
        const auto& block = editor(**it, fixture.id);
        if (!seen.insert(reference_objective_frame(**it, block)).second) continue;
        if (compiler_puzzle_completed(data, **it, block)) { selected = *it; break; }
        if (!partial && compiler_puzzle_case_count(data, **it, block)) partial = *it;
    }
    if (!selected) selected = partial;
    const auto count = selected ? compiler_puzzle_case_count(data, *selected, editor(*selected, fixture.id)) : 0;
    const auto complete = selected && compiler_puzzle_completed(data, *selected, editor(*selected, fixture.id));
    const auto feedback = selected ? compiler_puzzle_feedback(data, *selected, editor(*selected, fixture.id)) : std::string{};
    require(compiler_puzzle_historical_case_count(data, fixture.after, after) == count
        && compiler_puzzle_historical_completed(data, fixture.after, after) == complete
        && compiler_puzzle_historical_feedback(data, fixture.after, after) == feedback,
        "Historical progress combined separate case versions or borrowed another version's feedback");
}
void chronological_adoption(const Fixture& fixture) {
    const auto& before = editor(fixture.before, fixture.id);
    auto original = seeded_state(fixture.before, fixture.id);
    const auto program = personal_program(before, "case-history-chain");
    seed_case(original, fixture.before, before, program, before.output_check->cases.size() - 1);
    auto direct = original, staged = original;
    prepare_lesson_state(direct, fixture.after);
    for (const auto& version : fixture.history) {
        prepare_lesson_state(staged, version);
        validate_lesson_state(staged, version);
        const auto exact = staged;
        prepare_lesson_state(staged, version);
        require(staged == exact, "An intermediate case/order stage is not idempotent");
    }
    require(staged == direct,
        "Direct and chronological case/order adoption produced different receipts, feedback, pauses or pins");
    check_historical_selection(fixture, direct);
    std::cout << "PASS case-revision chronological " << fixture.name
        << ": complete intermediate versions and historical selection agree\n";
}
void different_programs(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    require(!retained_cases(before, after).front(),
        "Program isolation QA needs a newly independent first case");
    auto original = seeded_state(fixture.before, fixture.id);
    const auto a = personal_program(before, "program-A");
    const auto b = personal_program(before, "program-B");
    for (std::size_t i = 0; i < before.output_check->cases.size(); ++i)
        seed_case(original, fixture.before, before, a, i);
    auto data = original;
    prepare_lesson_state(data, fixture.after);
    require(!compiler_puzzle_completed(data, fixture.after, after),
        "An old aggregate program was credited with the new first independent input");
    auto changed = model_request(after, b);
    invalidate_compiler_puzzle_attempt(data, fixture.after, after, changed);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 0
        && compiler_puzzle_completed(data, fixture.before, before),
        "Editing the incomplete revised program retained foreign proofs or revoked historical achievement");
    seed_case(data, fixture.after, after, b, 1);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 1
        && !compiler_puzzle_completed(data, fixture.after, after),
        "An optional case from program B borrowed program A's completion");
    const auto partial = data;
    prepare_lesson_state(data, fixture.after);
    require(data == partial, "Adoption replay reintroduced another program's old receipts");
    seed_case(data, fixture.after, after, b, 0);
    require(compiler_puzzle_case_count(data, fixture.after, after) == 2
        && compiler_puzzle_completed(data, fixture.after, after)
        && string_value(data, "native_windows", receipt_base(fixture.after, after) + "program")
            == compiler_program_frame(changed),
        "Program B did not earn exactly its own first and one optional case");
    for (const auto& [key, value] : original.preferences.at("native_windows"))
        require(find(data, "native_windows", key) && *find(data, "native_windows", key) == value,
            "Current program B overwrote a historical program A receipt or another editor");
    check_gate(data, fixture.after, fixture.id, true);
    check_historical_selection(fixture, data);
    const auto achieved = data;
    changed.source_utf8 += "// later edit is not an execution\n";
    invalidate_compiler_puzzle_attempt(data, fixture.after, after, changed);
    require(compiler_puzzle_completed(data, fixture.after, after)
        && proof_values(data, fixture.after, after) == proof_values(achieved, fixture.after, after),
        "A later draft edit revoked an earned same-program first-case achievement");
    StateStore store(directory / "case-revision-programs");
    require(store.load().preferences.empty(), "The program isolation profile is not new");
    store.save(data); StateStore reopened(directory / "case-revision-programs");
    require(reopened.load() == data, "Reopening mixed independent program histories");
    check_reset(fixture, original, data);
    std::cout << "PASS case-revision programs A/B: no union, historical achievement exact, first case required, optional cases do not block\n";
}
bool cross_version_programs(const Fixture& fixture) {
    if (fixture.history.size() < 2) return false;
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto old_to_final = retained_cases(before, after);
    for (std::size_t stage = 0; stage + 1 < fixture.history.size(); ++stage) {
        const auto& middle_lesson = fixture.history[stage];
        const auto& middle = editor(middle_lesson, fixture.id);
        if (reference_objective_frame(middle_lesson, middle) == reference_objective_frame(fixture.before, before)
            || reference_objective_frame(middle_lesson, middle) == reference_objective_frame(fixture.after, after)) continue;
        const auto middle_to_final = retained_cases(middle, after);
        std::optional<std::size_t> old_index, middle_index;
        for (const auto& index : old_to_final)
            if (index && *index != 0 && !same_output_case_requirements(
                    before.output_check->cases[*index], middle.output_check->cases.front())) { old_index = index; break; }
        for (const auto& index : middle_to_final)
            if (index && (!old_index || !same_output_case_requirements(
                    middle.output_check->cases[*index], before.output_check->cases[*old_index]))) {
                middle_index = index; break;
            }
        if (!old_index || !middle_index) continue;
        auto data = seeded_state(fixture.before, fixture.id);
        const auto a = personal_program(before, "version-A");
        const auto b = personal_program(before, "version-B");
        seed_case(data, fixture.before, before, a, *old_index);
        const auto original = data;
        for (std::size_t i = 0; i <= stage; ++i) prepare_lesson_state(data, fixture.history[i]);
        if (compiler_puzzle_completed(data, middle_lesson, middle)) continue;
        invalidate_compiler_puzzle_attempt(data, middle_lesson, middle, model_request(middle, b));
        seed_case(data, middle_lesson, middle, b, *middle_index);
        const auto middle_data = data;
        for (std::size_t i = stage + 1; i < fixture.history.size(); ++i)
            prepare_lesson_state(data, fixture.history[i]);
        const bool returns_to_a = reference_objective_frame(fixture.before, before)
            == reference_objective_frame(fixture.after, after);
        const auto& expected_source = returns_to_a ? before : middle;
        const auto seeded = returns_to_a ? *old_index : *middle_index;
        const auto expected = retained_cases(expected_source, after);
        std::size_t count = 0;
        for (const auto& index : expected) if (index && *index == seeded) ++count;
        const bool complete = expected.front() && *expected.front() == seeded;
        require(compiler_puzzle_case_count(data, fixture.after, after) == count
            && compiler_puzzle_completed(data, fixture.after, after) == complete
            && string_value(data, "native_windows", receipt_base(fixture.after, after) + "program")
                == compiler_program_frame(model_request(after, returns_to_a ? a : b)),
            "A later case review combined receipts from separate programs or replaced a pre-existing program");
        const auto old_proofs = proof_values(original, fixture.before, before);
        const auto current_old = proof_values(data, fixture.before, before);
        require(old_proofs == current_old, "Cross-version adoption changed historical program A proofs");
        require(proof_values(data, middle_lesson, middle) == proof_values(middle_data, middle_lesson, middle),
            "Cross-version adoption rewrote the independent middle program B proofs");
        check_historical_selection(fixture, data);
        check_reset(fixture, original, data);
        std::cout << "PASS case-revision version programs " << fixture.name
            << ": one coherent program selected across case/order history\n";
        return true;
    }
    return false;
}
bool explicit_return(const Fixture& fixture) {
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    if (fixture.history.size() < 2 || reference_objective_frame(fixture.before, before)
            != reference_objective_frame(fixture.after, after)) return false;
    const Lesson* intermediate = nullptr;
    for (const auto& version : fixture.history)
        if (reference_objective_frame(version, editor(version, fixture.id))
                != reference_objective_frame(fixture.before, before)) { intermediate = &version; break; }
    require(intermediate, "An explicit return fixture needs a different intervening case list");
    require(before.output_check->cases.size() > 1,
        "Return QA needs a distinct original first and optional case");
    auto data = seeded_state(fixture.before, fixture.id);
    const auto program = personal_program(before, "return-A");
    const auto last = before.output_check->cases.size() - 1;
    seed_case(data, fixture.before, before, program, last);
    const auto original = data;
    const auto old_proofs = proof_values(original, fixture.before, before);
    const auto& middle = editor(*intermediate, fixture.id);
    prepare_lesson_state(data, *intermediate);
    const auto mapping = retained_cases(before, middle);
    const auto old_first = std::ranges::find(mapping, std::optional<std::size_t>{0});
    const bool can_prove_first = old_first != mapping.end();
    if (can_prove_first)
        seed_case(data, *intermediate, middle, program, static_cast<std::size_t>(old_first - mapping.begin()));
    const auto middle_data = data;
    bool past_middle = false;
    for (const auto& version : fixture.history) {
        if (&version == intermediate) { past_middle = true; continue; }
        if (past_middle) prepare_lesson_state(data, version);
    }
    const auto current_proofs = proof_values(data, fixture.after, after);
    require(current_proofs.size() == old_proofs.size(), "Returning to an old objective changed its proof-vector shape");
    for (std::size_t i = 0; i < old_proofs.size(); ++i)
        if (!old_proofs[i].empty()) require(old_proofs[i] == current_proofs[i],
            "An authenticated return rewrote a nonempty historical proof");
    require(compiler_puzzle_completed(data, fixture.after, after) == can_prove_first
        && compiler_puzzle_case_count(data, fixture.after, after) == (can_prove_first ? 2U : 1U),
        "A return lost same-program evidence or fabricated an execution of its returning first case");
    const auto middle_prefix = receipt_base(*intermediate, middle);
    for (const auto& [key, value] : middle_data.preferences.at("native_windows"))
        if (key.starts_with(middle_prefix)) require(find(data, "native_windows", key)
            && *find(data, "native_windows", key) == value,
            "An explicit return overwrote its separate intermediate result or feedback");
    const auto exact = data;
    prepare_lesson_state(data, fixture.after);
    require(data == exact, "An explicit return replayed its proof recovery");
    check_historical_selection(fixture, data);
    check_reset(fixture, original, data);
    std::cout << "PASS case-revision explicit return " << fixture.name
        << ": existing proofs exact, only genuine same-program missing evidence recovered\n";
    return true;
}
bool historical_gap_recovery(const Fixture& fixture, const std::filesystem::path& directory) {
    if (fixture.history.size() != 2) return false;
    const auto& before = editor(fixture.before, fixture.id);
    const auto& middle_lesson = fixture.history.front();
    const auto& middle = editor(middle_lesson, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    const auto mapping = retained_cases(before, after);
    if (mapping.empty() || !mapping.front()
        || contains_case(middle, after.output_check->cases.front(), after.output_check->input_comparison)
        || reference_objective_frame(fixture.before, before) == reference_objective_frame(fixture.after, after)) return false;
    const auto middle_mapping = retained_cases(middle, after);
    const auto optional = std::ranges::find(middle_mapping, std::optional<std::size_t>{0});
    require(optional != middle_mapping.end() && optional != middle_mapping.begin(),
        "A removed-case recovery fixture needs its middle first case retained as a final optional case");
    auto original = seeded_state(fixture.before, fixture.id);
    const auto a = personal_program(before, "gap-program-A");
    const auto b = personal_program(before, "gap-program-B");
    seed_case(original, fixture.before, before, a, *mapping.front());
    for (const bool same_program : {true, false}) {
        auto data = original;
        prepare_lesson_state(data, middle_lesson);
        require(compiler_puzzle_case_count(data, middle_lesson, middle) == 0
            && !compiler_puzzle_completed(data, middle_lesson, middle),
            "A case absent from the intermediate list received an invented intermediate receipt");
        seed_case(data, middle_lesson, middle, same_program ? a : b, 0);
        const auto middle_state = data;
        prepare_lesson_state(data, fixture.after);
        require(compiler_puzzle_case_count(data, fixture.after, after) == (same_program ? 2U : 1U)
            && compiler_puzzle_completed(data, fixture.after, after) == same_program,
            "Recovery skipped the older exact case or combined programs across a case's absence");
        const auto proofs = proof_values(data, fixture.after, after);
        require(proofs.size() == after.output_check->cases.size()
            && (!proofs.front().empty()) == same_program
            && !proofs[static_cast<std::size_t>(optional - middle_mapping.begin())].empty(),
            "Recovered proof positions do not belong to their own exact cases");
        const auto middle_prefix = receipt_base(middle_lesson, middle);
        for (const auto& [key, value] : middle_state.preferences.at("native_windows"))
            if (key.starts_with(middle_prefix)) require(find(data, "native_windows", key)
                && *find(data, "native_windows", key) == value,
                "Recovery rewrote the independent intermediate version's achievement");
        check_conservation(original, data, fixture.before, fixture.after, fixture.id);
        check_gate(data, fixture.after, fixture.id, same_program);
        check_historical_selection(fixture, data);
        const auto profile = directory / (fixture.name + (same_program ? "-gap-same-program" : "-gap-other-program"));
        StateStore store(profile);
        require(store.load().preferences.empty(), "The historical-gap profile is not isolated");
        store.save(data); StateStore reopened(profile);
        require(reopened.load() == data, "Recovered old-case evidence changed on physical reopening");
        check_reset(fixture, original, data);
    }
    std::cout << "PASS case-revision historical gap " << fixture.name
        << ": removed case recovered from its actual older version for the same program only\n";
    return true;
}
void untouched_draft(const Fixture& fixture) {
    auto data = seeded_state(fixture.before, fixture.id, false);
    const auto original = data;
    prepare_lesson_state(data, fixture.after);
    preserved_drafts(original, data, fixture.before, fixture.after);
    const auto& current = editor(fixture.after, fixture.id);
    require(!get_draft(data, draft_key("programming", fixture.after, current),
        current.initial_code, current.standard_input.value_or("")).has_saved
        && !compiler_puzzle_completed(data, fixture.after, current),
        "A revised case list invented an untouched draft or a first-case execution");
}
std::string phase_detail(std::string_view name, const CppPhaseResult& result) {
    return std::string(name) + " status=" + std::to_string(static_cast<int>(result.status))
        + " exit=" + (result.exit_code ? std::to_string(*result.exit_code) : std::string("none"))
        + " stdout=[" + result.stdout_bytes + "] stderr=[" + result.stderr_bytes + "]";
}
std::size_t actual_reference_runs(const Fixture& fixture, const std::filesystem::path& directory) {
    const auto* spec = real_source(fixture);
    require(spec && fixture.real, "Actual MSVC execution requires one of the exact pinned real AssetCheck publications");
    const auto& before = editor(fixture.before, fixture.id);
    const auto& after = editor(fixture.after, fixture.id);
    std::size_t executions = 0;
    for (const bool reference : {false, true}) {
        CppRunRequest request;
        request.request_id = reference ? 311 : 310; request.revision = reference ? 511 : 510;
        request.source_utf8 = reference ? fixture.reference_source : before.initial_code;
        request.stdin_utf8 = after.output_check->cases.front().standard_input;
        request.projects_directory = directory / (std::string(spec->tag) + (reference ? "-msvc-reference" : "-msvc-initial"));
        request.compile_timeout = std::chrono::seconds(55);
        request.execution_timeout = std::chrono::seconds(5);
        request.backend = CppRunRequest::Backend::native_msvc;
        require(compiler_runtime_request_allowed(after, request), "The unchanged AssetCheck client is not an allowed runtime request");
        const auto result = run_cpp(request);
        const auto diagnostics = result.error_utf8 + "\n" + phase_detail("compile", result.compilation)
            + "\n" + phase_detail("execute", result.execution);
        require(result.error_utf8.empty() && result.compilation.succeeded(),
            "Actual AssetCheck MSVC compilation failed: " + diagnostics);
        const auto execution = capture_cpp_execution(request, result);
        require(execution.execution_completed && execution.exit_code == 0 && execution.standard_error.empty(),
            "Actual AssetCheck execution did not complete normally: " + diagnostics);
        const auto evaluation = evaluate_cpp_output_check(*after.output_check, execution, request.revision);
        require(evaluation.verdict == (reference ? OutputVerdict::correct : OutputVerdict::incorrect),
            "Actual AssetCheck first-case verdict disagrees with the published reference or discriminating starter: " + diagnostics);
        OutputCheck independent;
        OutputCase expected;
        expected.standard_input = std::string(spec->first_input);
        expected.outputs = {std::string(reference ? spec->first_output : spec->initial_output)};
        independent.cases.push_back(expected);
        require(evaluate_cpp_output_check(independent, execution, request.revision).verdict == OutputVerdict::correct,
            "Actual AssetCheck stdout differs from the independently pinned initial/reference result: " + diagnostics);
        auto data = seeded_state(fixture.before, fixture.id);
        prepare_lesson_state(data, fixture.after);
        const auto original = data;
        const auto earned = record_compiler_puzzle_case(data, fixture.after, after, request, execution,
            evaluation.feedback);
        require(earned == reference && compiler_puzzle_completed(data, fixture.after, after) == reference
            && compiler_puzzle_case_count(data, fixture.after, after) == (reference ? 1U : 0U),
            "An actual MSVC result accredited the wrong case or required optional cases");
        if (!reference) require(data == original, "A wrong initial result manufactured a compiler receipt");
        require(compiler_puzzle_case_count(data, fixture.before, before) == 0,
            "Executing one new individual input manufactured a historical aggregate receipt");
        check_gate(data, fixture.after, fixture.id, reference);
        preserved_drafts(original, data, fixture.before, fixture.after);
        const auto profile = directory / (std::string(spec->tag) + (reference ? "-actual-reference-state" : "-actual-initial-state"));
        StateStore store(profile);
        require(store.load().preferences.empty(), "The actual MSVC capture profile is not isolated");
        store.save(data); StateStore reopened(profile);
        require(reopened.load() == data, "The real MSVC case receipt changed on physical reopening");
        ++executions;
        std::cout << "PASS actual MSVC " << spec->tag << (reference ? " reference" : " unchanged starter")
            << ": full program, exact first input/stdout/exit, " << (reference ? "one required receipt" : "correct rejection") << '\n';
    }
    return executions;
}

void retirement_epoch(const Fixture& fixture, const std::filesystem::path& directory) {
    require(fixture.snapshot_source && fixture.receipt_source
        && fixture.after.state_layout.retirement_history.size() == 1,
        "Retirement epoch QA needs one exact snapshot source and one intermediate receipt source");
    const auto& expected_source = *fixture.snapshot_source;
    const auto& expected = editor(expected_source, fixture.id);
    const auto& receipt_source = *fixture.receipt_source;
    const auto& receipt_editor = editor(receipt_source, fixture.id);
    const auto& active = editor(fixture.after, fixture.id);
    const auto& snapshot = fixture.after.state_layout.retirement_history.front();
    const Block* archived = nullptr;
    for (const auto& page : snapshot.pages) for (const auto& block : page.blocks)
        if (block.stable_id == fixture.id) archived = &block;
    require(archived, "The complete retirement snapshot omitted its unchanged compiler owner");
    same_version(expected_source, expected, fixture.after, *archived);
    const auto records = [](const auto& revisions) {
        std::vector<std::string> result;
        for (const auto& revision : revisions) result.push_back(revision.record_sha256);
        return result;
    };
    require(records(archived->compiler_case_revisions) == records(expected.compiler_case_revisions)
        && records(archived->compiler_case_order_revisions) == records(expected.compiler_case_order_revisions),
        "Equal block bytes selected the wrong retirement epoch and lost or borrowed an intermediate case history");
    const bool history_existed = !expected.compiler_case_revisions.empty();
    require(expected.compiler_case_revisions.size() == (history_existed ? 2U : 0U)
        && active.compiler_case_revisions.size() == 2,
        "Retirement epoch QA must distinguish original A from returned A after two reviews");
    auto data = seeded_state(receipt_source, fixture.id);
    seed_case(data, receipt_source, receipt_editor, personal_program(receipt_editor, "retirement-B"), 0);
    require(compiler_puzzle_completed(data, receipt_source, receipt_editor),
        "The intermediate B model receipt must authenticate its own first case");
    prepare_lesson_state(data, fixture.after);
    require(!compiler_puzzle_completed(data, fixture.after, active)
        && compiler_puzzle_historical_completed(data, fixture.after, active),
        "The B receipt must remain historical without proving A's different first case");
    const auto exact = data;
    const auto verify = [&](const StateData& stored) {
        require(compiler_puzzle_historical_case_count(stored, fixture.after, *archived) == (history_existed ? 1U : 0U)
            && compiler_puzzle_historical_completed(stored, fixture.after, *archived) == history_existed
            && compiler_puzzle_historical_feedback(stored, fixture.after, *archived)
                == (history_existed ? compiler_puzzle_feedback(stored, receipt_source, receipt_editor) : std::string{}),
            "Archived reader history lost B after a return or retroactively exposed B before it existed");
    };
    verify(data);
    require(data == exact, "Reading archived compiler progress mutated its execution evidence");
    const auto profile = directory / (fixture.name + "-snapshot-epoch");
    StateStore store(profile);
    require(store.load().preferences.empty(), "The retirement epoch profile is not isolated");
    store.save(data); StateStore reopened(profile);
    const auto restored = reopened.load();
    require(restored == data, "Retirement epoch state changed on physical reopening");
    verify(restored);
    std::cout << "PASS case-revision retirement epoch " << fixture.name
        << ": exact journals/meanings and reader history of B " << (history_existed ? "retained" : "absent") << '\n';
}

Fixture read_fixture(const JsonObject& entry, const std::filesystem::path& directory,
        std::map<std::filesystem::path, std::string>& originals) {
    Fixture result;
    result.name = winrt::to_string(entry.GetNamedString(L"name"));
    result.id = winrt::to_string(entry.GetNamedString(L"id"));
    result.before_file = winrt::to_string(entry.GetNamedString(L"before"));
    result.after_file = winrt::to_string(entry.GetNamedString(L"after"));
    result.real = entry.GetNamedBoolean(L"real", false);
    result.snapshot_epoch_only = entry.GetNamedBoolean(L"snapshotEpochOnly", false);
    const auto load = [&](std::string_view file) {
        const auto path = directory / std::string(file);
        const auto raw = bytes(path); originals.emplace(path, raw);
        auto lesson = parse_lesson_json(raw, "es");
        const auto metadata = parse_lesson_progress_metadata(raw, "es");
        require(lesson.id == metadata.id && lesson.pages.size() == metadata.pages.size(),
            "Full and metadata loaders disagree about a complete case-review publication");
        return lesson;
    };
    result.before = load(result.before_file);
    result.after = load(result.after_file);
    if (result.snapshot_epoch_only) {
        result.snapshot_source = load(winrt::to_string(entry.GetNamedString(L"snapshotSource")));
        result.receipt_source = load(winrt::to_string(entry.GetNamedString(L"receiptSource")));
    }
    for (const auto& item : entry.GetNamedArray(L"history"))
        result.history.push_back(load(winrt::to_string(item.GetObject().GetNamedString(L"source"))));
    require(!result.history.empty() && result.history.back().raw_json == result.after.raw_json,
        "Case-revision fixture history must end with its exact complete after publication");
    if (entry.HasKey(L"reference")) {
        result.reference_file = winrt::to_string(entry.GetNamedString(L"reference"));
        const auto path = directory / result.reference_file;
        result.reference_source = bytes(path); originals.emplace(path, result.reference_source);
    }
    return result;
}
std::size_t invalid_documents(const JsonArray& entries, const std::filesystem::path& directory) {
    std::size_t count = 0, failures = 0;
    for (const auto& item : entries) {
        const auto entry = item.GetObject();
        const auto name = winrt::to_string(entry.GetNamedString(L"name"));
        const auto raw = bytes(directory / winrt::to_string(entry.GetNamedString(L"file")));
        const auto expected = winrt::to_string(entry.GetNamedString(L"message", L""));
        bool passed = true;
        for (const bool metadata : {false, true}) {
            std::string message;
            try {
                if (metadata) (void)parse_lesson_progress_metadata(raw, "es");
                else (void)parse_lesson_json(raw, "es");
            } catch (const std::exception& error) { message = error.what(); }
            catch (const winrt::hresult_error& error) { message = winrt::to_string(error.message()); }
            if (message.empty() || (!expected.empty() && message.find(expected) == std::string::npos)) {
                std::cerr << "FAIL case-revision invalid " << name << (metadata ? " metadata" : " full")
                    << ": expected=[" << expected << "] actual=[" << message << "]\n";
                passed = false;
            }
        }
        if (passed) ++count; else ++failures;
    }
    require(entries.Size() != 0, "Case-revision QA needs contract and history negatives");
    require(!failures, "Some invalid case-review documents were accepted or rejected by the wrong guard");
    return count;
}
} // namespace

int main(int argc, char** argv) try {
    require(argc == 3, "Usage: compiler_case_revision_state_tests <fixtures> <isolated-qa-root>");
    winrt::init_apartment();
    const std::filesystem::path fixtures(argv[1]);
    const auto qa = std::filesystem::path(argv[2]) /
        ("case-revision-" + std::to_string(GetCurrentProcessId()) + '-' + std::to_string(GetTickCount64()));
    require_safe_path(fixtures); require_safe_path(qa);
    const auto manifest = JsonObject::Parse(winrt::to_hstring(bytes(fixtures / "manifest.json")));
    require(manifest.GetNamedNumber(L"version", 1) == 1, "Unsupported case-revision QA manifest version");
    std::map<std::filesystem::path, std::string> originals;
    std::vector<Fixture> cases;
    std::size_t failures = 0, scenarios = 0, invalid = 0, actual_runs = 0, returns = 0, version_programs = 0, gaps = 0, retirement_epochs = 0;
    const auto run = [&](std::string_view label, const auto& action) {
        try { action(); return true; }
        catch (const winrt::hresult_error& error) {
            std::cerr << "FAIL case-revision " << label << ": " << winrt::to_string(error.message()) << '\n';
        } catch (const std::exception& error) {
            std::cerr << "FAIL case-revision " << label << ": " << error.what() << '\n';
        }
        ++failures; return false;
    };
    for (const auto& item : manifest.GetNamedArray(L"cases")) {
        const auto entry = item.GetObject();
        const auto name = winrt::to_string(entry.GetNamedString(L"name"));
        run("load " + name, [&] { cases.push_back(read_fixture(entry, fixtures, originals)); });
    }
    run("fixture inventory", [&] { require(!cases.empty(), "No authenticated case-review fixtures were generated"); });
    for (const auto& fixture : cases) {
        if (!run(fixture.name + " publication", [&] { verify_publication(fixture); })) continue;
        if (fixture.snapshot_epoch_only) {
            run(fixture.name + " retirement epoch", [&] { retirement_epoch(fixture, qa); ++retirement_epochs; });
            continue;
        }
        run(fixture.name + " conservation/import", [&] { scenarios += native_matrix(fixture, qa); });
        run(fixture.name + " chronological adoption", [&] { chronological_adoption(fixture); });
        run(fixture.name + " return", [&] { if (explicit_return(fixture)) ++returns; });
        run(fixture.name + " version programs", [&] { if (cross_version_programs(fixture)) ++version_programs; });
        run(fixture.name + " historical gap", [&] { if (historical_gap_recovery(fixture, qa)) ++gaps; });
    }
    for (const auto& spec : real_sources) {
        const auto found = std::ranges::find_if(cases, [&](const auto& fixture) { return real_source(fixture) == &spec; });
        run(std::string(spec.tag) + " actual MSVC", [&] {
            require(found != cases.end(), "A real pinned AssetCheck fixture is missing");
            verify_publication(*found);
            actual_runs += actual_reference_runs(*found, qa);
        });
        run(std::string(spec.tag) + " untouched draft", [&] {
            require(found != cases.end(), "A real pinned AssetCheck fixture is missing"); untouched_draft(*found);
        });
        if (spec.tag == "u4l7") run("programs A/B", [&] {
            require(found != cases.end(), "The U4L7 exact retained aggregate fixture is missing"); different_programs(*found, qa);
        });
    }
    run("invalid documents", [&] { invalid = invalid_documents(manifest.GetNamedArray(L"invalid"), fixtures); });
    run("coverage", [&] {
        require(actual_runs == 6 && returns > 0 && version_programs > 0 && gaps > 0 && retirement_epochs == 2,
            "QA omitted an actual MSVC run, authenticated return, cross-version program isolation or removed-case recovery");
        require(std::ranges::any_of(cases, [](const auto& fixture) {
            return fixture.name == "real-u11-closed-expansion";
        }), "QA omitted the authenticated closed compiler expansion followed by a case review");
    });
    run("fixture bytes", [&] {
        for (const auto& [path, exact] : originals) require(bytes(path) == exact,
            "Case-revision QA changed an authenticated publication or published reference function");
    });
    if (failures) {
        std::cerr << "FAIL compiler case revision: " << failures << " independent groups failed; other groups were still checked.\n";
        return 1;
    }
    std::cout << "PASS compiler case revision: " << scenarios << " state models, " << invalid
        << " invalid documents in both loaders, " << actual_runs
        << " actual MSVC first-case executions, coherent returns/import/reopen/Reset; original fixtures unchanged.\n";
    return 0;
} catch (const winrt::hresult_error& error) {
    std::cerr << winrt::to_string(error.message()) << '\n'; return 1;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}

#pragma once

// QA only. Complete positional lessons and v2 destinations are authenticated
// Git publications. The exported preferences are isolated synthetic Android
// data, not student progress or claims of historical compilation/listening.
#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::test {
namespace compiler_import_detail {
inline void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Publication {
    std::string_view name, lesson_id, before_sha, after_sha;
};
inline constexpr std::array<Publication, 5> publications{{
    {"u11l19", "cpp_fundamentals__texto_interpretacion_y_flujos__declaraciones_aceptadas_solo_al_completar",
        "887dbfb3f98c0f04c92567d2d1a46d82ddb1bd3ebe761ecf60c17a905e1815a0",
        "bfaa88c3f4294c30bf50b58f96aff91fbf71ffdbdfd20c7471d1c71e358c1cb6"},
    {"u11l20", "cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada",
        "cde8f8a248fcc8a7eab16033f2e469932eb24929e5fff3cae3d77c688f1338f8",
        "69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9"},
    {"u11l21", "cpp_fundamentals__texto_interpretacion_y_flujos__constantes_del_lenguaje_y_nombres_predefinidos",
        "65b1eb97508bfb7f6dbbc695f25c8cfa27994333892878c44c297b7ccea463cc",
        "d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db"},
    {"u11l22", "cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado",
        "02556412590ab4bd53c86eff1074ba2b52145fcf9a4d7d2bee771cd4af806bf7",
        "1932b3c86cabf23bf02154eb2d0371c3b8e7fe48fc01e42e1d559b3220cb1696"},
    {"u12l20", "cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia",
        "f2e04cdddc482d708f29ef5954d388cc68a9d2d69c69345430b0585afed04789",
        "744739618d59f751567445eedbfa97fca20ce300ce61f3061d33eea610870532"},
}};
struct Owner {
    std::string_view lesson, compiler_id;
    curriculum::StatePosition compiler;
    std::string_view response_id;
    curriculum::StatePosition response;
};
inline constexpr std::array<Owner, 7> owners{{
    {"u11l19", "block_0004_0002", {4, 2}, "block_0003_0003", {3, 3}},
    {"u11l20", "block_0005_0002", {5, 2}, "block_0004_0001", {4, 1}},
    {"u11l21", "block_0005_0002", {5, 2}, "block_0004_0001", {4, 1}},
    {"u11l22", "block_0003_0001", {3, 1}, "block_0006_0001", {6, 1}},
    {"u11l22", "block_0006_0003", {6, 3}, "block_0006_0001", {6, 1}},
    {"u12l20", "block_0005_0004", {5, 4}, "block_0002_0005", {2, 5}},
    {"u12l20", "block_0009_0002", {9, 2}, "block_0007_0002", {7, 2}},
}};
inline const curriculum::Block& at(const curriculum::Lesson& lesson, curriculum::StatePosition position) {
    return lesson.pages.at(position.page).blocks.at(position.block);
}
inline const curriculum::Block& mapped(const curriculum::Lesson& lesson, std::string_view identity,
                                      curriculum::StatePosition original) {
    const auto position = curriculum::stable_block_position(lesson, identity);
    require(position.has_value(), "Compiler import lost an authenticated current owner");
    const auto& block = at(lesson, *position);
    require(block.address.page == original.page && block.address.block == original.block
        && block.address.addition_namespace.empty(), "Compiler import changed an original storage address");
    return block;
}
inline bool same_draft(const state::Draft& a, const state::Draft& b) {
    return a.source == b.source && a.input == b.input && a.indentation_json == b.indentation_json
        && a.selection_start == b.selection_start && a.selection_end == b.selection_end && a.has_saved == b.has_saved;
}
inline bool same_response(const state::Response& a, const state::Response& b) {
    return a.answer == b.answer && a.correct == b.correct && a.raw_json == b.raw_json
        && a.selected == b.selected && a.order == b.order && a.reviewed_answer_identity == b.reviewed_answer_identity
        && a.historical_choice == b.historical_choice;
}
} // namespace compiler_import_detail

// Reuse the real suite's SharedPreferences XML exporter through this callback:
// export_preferences(path, data). No import, state store or evaluator is mocked.
template<class ExportPreferences>
std::size_t compiler_contract_import_state_tests(const std::filesystem::path& base,
        const std::filesystem::path& fixtures, ExportPreferences export_preferences) {
    using namespace curriculum;
    using namespace state;
    using compiler_import_detail::require;
    std::size_t scenarios = 0;
    for (const auto& publication : compiler_import_detail::publications) {
        const auto before_path = fixtures / (std::string(publication.name) + "-before.json");
        const auto after_path = fixtures / (std::string(publication.name) + "-after.json");
        const auto before_bytes = read_bytes(before_path), after_bytes = read_bytes(after_path);
        require(sha256(before_bytes) == publication.before_sha && sha256(after_bytes) == publication.after_sha,
            "Compiler import needs the exact published positional and mapped sources, not projected history");
        const auto before = windows::load_lesson(before_path, "es");
        const auto current = windows::load_lesson(after_path, "es");
        require(before.id == publication.lesson_id && current.id == before.id && before.state_layout.version == 0
            && current.state_layout.version == 2 && !before.compiler_puzzles_required && current.compiler_puzzles_required,
            "Compiler import lost its positional-to-v2 required-objective transition");
        for (const auto& owner : compiler_import_detail::owners) {
            if (owner.lesson != publication.name) continue;
            const auto& old_editor = compiler_import_detail::at(before, owner.compiler);
            const auto& editor = compiler_import_detail::mapped(current, owner.compiler_id, owner.compiler);
            const auto& old_response = compiler_import_detail::at(before, owner.response);
            const auto& response = compiler_import_detail::mapped(current, owner.response_id, owner.response);
            require(old_editor.type == BlockType::cpp_compiler && editor.type == old_editor.type
                && old_editor.initial_code == editor.initial_code && old_editor.output_check && editor.output_check
                && old_editor.output_check->cases.size() == 1 && editor.output_check->cases.size() == 1
                && draft_key("programming", before, old_editor) == draft_key("programming", current, editor),
                "Compiler import changed a focal starter, objective or independent draft key");
            require(old_response.type == BlockType::output_prediction && response.type == old_response.type
                && !old_response.answers.empty() && old_response.answers == response.answers
                && old_response.output_comparison == response.output_comparison && old_response.id == response.id
                && response_storage_key(before, old_response) == response_storage_key(current, response),
                "Compiler import changed an independent historical prediction contract");
            const auto key = draft_key("programming", before, old_editor);
            const auto response_key = response_storage_key(before, old_response);
            for (const std::string_view mode : {"empty", "draft", "incorrect", "correct"}) {
                StateData original;
                prepare_lesson_state(original, before);
                // The genuine legacy Android bookmark is positional. Avoid
                // seeding later mapped reach metadata into the historical input.
                set_saved_page(original, before.id, owner.compiler.page);
                original.preferences["compiler_import_qa"]["unchanged"] = std::string(" \t original\r\n");
                original.preferences["compiler_import_qa"]["enabled"] = true;
                original.preferences["compiler_import_qa"]["attempts"] = std::int64_t{4};
                original.preferences["compiler_import_qa"]["labels"] = std::vector<std::string>{"first", " \t second\r\n"};
                // put_draft also includes optional Windows caret sidecars in
                // these synthetic preferences. Keeping them tests lossless
                // transport of extra typed data, not historical Android caret
                // persistence: that Android source did not write those fields.
                // Other editors in the same lesson retain their own source,
                // stdin, indentation and caret, independently of the focal one.
                for (const auto& other : compiler_import_detail::owners) {
                    if (other.lesson != owner.lesson || other.compiler_id == owner.compiler_id) continue;
                    const auto& block = compiler_import_detail::at(before, other.compiler);
                    Draft independent;
                    independent.source = "    // independent compiler QA\r\n" + block.initial_code;
                    independent.input = " \t separate input\r\n";
                    independent.indentation_json = make_indentation_metadata(independent.source, {0});
                    independent.selection_start = 4; independent.selection_end = 8;
                    put_draft(original, draft_key("programming", before, block), independent);
                }
                if (mode != "empty") {
                    Draft draft;
                    draft.source = "    // original compiler draft QA\r\n" + old_editor.initial_code;
                    draft.input = " \t input kept exactly\r\n";
                    draft.indentation_json = make_indentation_metadata(draft.source, {0});
                    draft.selection_start = 2; draft.selection_end = 9;
                    put_draft(original, key, draft);
                    Response saved;
                    saved.answer = mode == "correct" ? old_response.answers.front() : "__isolated_unaccepted_prediction__";
                    saved.correct = mode == "correct";
                    require(evaluate_text_answer(old_response, saved.answer).correct == saved.correct,
                        "Compiler import QA prediction does not have its intended original verdict");
                    saved.raw_json = R"({"incorrectAttempts":4,"opaque":{"source":"isolated compiler import QA","retained":true}})";
                    if (mode == "incorrect") mark_response_reviewed(saved, old_response);
                    put_response(original, before, old_response, saved);
                }
                // Stored QA bookmarks exercise the bound and exact preservation.
                // They do not represent a real pause, PCM or completed listening.
                auto history = narration::begin({}, "compiler-import-a", "owner-a", {1, 137, 0, false},
                                                "unchanged source A", "page-a");
                history = narration::begin(std::move(history), "compiler-import-b", "owner-b", {2, 239, 0, false},
                                           "unchanged source B", "page-b");
                history = narration::begin(std::move(history), "compiler-import-c", "owner-c", {0, 317, 0, false},
                                           old_response.explanation, "page-c");
                narration::write_history(original, history);
                require(!original.preferences.contains("native_windows"), "Android QA must not originate Windows execution receipts");
                validate_lesson_state(original, before);
                const auto prior_draft = get_draft(original, key, old_editor.initial_code, old_editor.standard_input.value_or(""));
                const auto prior_response = get_response(original, before, old_response);
                const auto prior_json = string_value(original, "learning_progress", response_key);
                const auto verify = [&](const StateData& data) {
                    const auto restored = get_response(data, current, response);
                    require(restored.has_value() == prior_response.has_value()
                        && string_value(data, "learning_progress", response_key) == prior_json,
                        "Compiler import invented, dropped or reserialized a historical prediction");
                    if (prior_response) require(compiler_import_detail::same_response(*restored, *prior_response)
                        && response_reviewed(*restored, response) == (mode == "incorrect"),
                        "Compiler import changed an answer, achievement, last rejection or opaque attempt metadata");
                    require(compiler_import_detail::same_draft(get_draft(data, key, editor.initial_code,
                                editor.standard_input.value_or("")), prior_draft),
                        "Compiler import changed saved source, stdin, indentation, caret or empty-draft status");
                    require(narration::read_history(data) == history && history.entries.size() == 3
                        && current_page(data, current) == display_page(current, owner.compiler.page),
                        "Compiler import changed the original page or one of three stored narration bookmarks");
                    for (const auto& [domain, values] : original.preferences)
                        for (const auto& [name, value] : values)
                            require(find(data, domain, name) && *find(data, domain, name) == value,
                                "Compiler import overwrote an original typed value or independent editor state");
                    for (const auto& page : current.pages) for (const auto& compiler : page.blocks)
                        if (compiler.type == BlockType::cpp_compiler) {
                            require(!windows::compiler_puzzle_completed(data, current, compiler)
                                && windows::compiler_puzzle_case_count(data, current, compiler) == 0
                                && windows::compiler_puzzle_historical_case_count(data, current, compiler) == 0,
                                "An Android answer or saved program manufactured native execution credit");
                            windows::CppRunRequest capture;
                            capture.source_utf8 = get_draft(data, draft_key("programming", current, compiler), compiler.initial_code).source;
                            capture.stdin_utf8 = get_draft(data, draft_key("programming", current, compiler), compiler.initial_code).input;
                            require(!windows::compiler_puzzle_current_feedback(data, current, compiler, capture),
                                "Compiler import presented ordinary feedback as a newly executed compiler verdict");
                        }
                    require(!data.preferences.contains("native_windows"), "Compiler import created a reserved execution domain");
                };
                const auto name = std::string(owner.lesson) + '-' + std::string(owner.compiler_id) + '-' + std::string(mode);
                auto adopted = original;
                validate_lesson_state(adopted, current);
                require(adopted == original, "Read-only compiler import validation modified its source state");
                prepare_lesson_state(adopted, current); verify(adopted);
                const auto prepared = adopted; prepare_lesson_state(adopted, current);
                require(adopted == prepared, "Compiler import adoption is not idempotent");
                StateStore native(base / (name + "-native"));
                require(native.load().preferences.empty(), "Compiler adoption QA requires an unused profile");
                native.save(original);
                auto persisted = native.load(); prepare_lesson_state(persisted, current); verify(persisted);
                require(persisted == adopted, "Persisting the positional source changed its subsequent adoption");
                native.save(persisted);
                StateStore reopened_native(base / (name + "-native"));
                require(reopened_native.load() == persisted, "Native reopening changed the adopted compiler state");
                const auto exported = base / (name + "-export");
                export_preferences(exported, original);
                const auto preview = inspect_android_export(exported);
                require(preview.data == original, "Compiler XML import changed original typed preference values");
                validate_lesson_state(preview.data, current); verify(preview.data);
                require(preview.data == original, "Compiler import preview changed data during read-only validation");
                const auto destination = base / (name + "-import");
                commit_android_import(preview, destination);
                StateStore imported(destination); auto data = imported.load();
                require(data == original, "Compiler import transaction did not retain the exact positional state");
                prepare_lesson_state(data, current); validate_lesson_state(data, current); verify(data);
                require(data == adopted, "Compiler import and direct adoption disagree about the authenticated state");
                imported.save(data); StateStore reopened(destination); const auto restored = reopened.load();
                require(restored == data, "Committed compiler import changed on native reopening"); verify(restored);
                for (const auto& file : preview.files)
                    require(sha256(read_bytes(exported / file.relative_path)) == file.sha256
                        && sha256(read_bytes(destination / "state/active/android" / file.relative_path)) == file.sha256,
                        "Compiler import changed a source XML or its archived original bytes");
                ++scenarios;
            }
        }
        require(read_bytes(before_path) == before_bytes && read_bytes(after_path) == after_bytes,
            "Compiler import QA modified an authenticated curricular publication");
    }
    require(scenarios == 28, "Compiler import omitted a focal owner or state scenario");
    std::cout << "PASS 28 focal compiler imports: seven U11L19-L22/U12L20 owners, authentic positional-to-v2 sources, "
        "empty/draft/rejected/correct ordinary responses, exact compiler drafts/selection and three stored bookmarks, "
        "XML inspection/commit/archive and native reopen; no compiler execution, new receipt or listening claimed.\n";
    return scenarios;
}
} // namespace lgaed::test

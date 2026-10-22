#include "core/lesson_state.hpp"
#include "core/lesson.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace lgaed::state;
namespace {
int groups = 0;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Fn> void rejects(Fn fn) {
    bool rejected = false;
    try { fn(); } catch (...) { rejected = true; }
    require(rejected, "Expected refusal without changing stored state");
}
void text_file(const std::filesystem::path& file, std::string_view text) {
    std::filesystem::create_directories(native_io_path(file.parent_path()));
    std::ofstream output(native_io_path(file), std::ios::binary | std::ios::trunc); output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("Could not write QA fixture: " + file.string());
}
void fixture(const std::filesystem::path& root) {
    text_file(root / "shared_prefs" / "learning_progress.xml",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>"
        "<int name=\"lesson_test__unit__lesson_page\" value=\"4\"/>"
        "<boolean name=\"lesson_test__unit__lesson_started\" value=\"true\"/>"
        "<string name=\"exercise_test__unit__lesson_page_4_block_2\">{&quot;answer&quot;:&quot;Zona inicial&quot;,&quot;correct&quot;:true}</string>"
        "<string name=\"exercise_test__unit__lesson_page_4_block_8\">{&quot;answer&quot;:&quot;parcial &quot;,&quot;correct&quot;:false}</string>"
        "<set name=\"lesson_test__unit__lesson_read_blocks_v2_es\"><string>b2</string><string>b8</string></set>"
        "<long name=\"attempt_timestamp\" value=\"1792536389797\"/>"
        "<float name=\"speed\" value=\"1.25\"/>"
        "</map>");
    text_file(root / "shared_prefs" / "cpp_lesson_drafts.xml",
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><map>"
        "<string name=\"programming/test__unit__lesson/4/7/source\">{&quot;version&quot;:1,&quot;code&quot;:&quot;    return 0;\\n&quot;}</string>"
        "<string name=\"programming/test__unit__lesson/4/7/sourceEncoding\">json-v1</string>"
        "<string name=\"programming/test__unit__lesson/4/7/input\">{&quot;version&quot;:1,&quot;input&quot;:&quot; \\t \\n&quot;}</string>"
        "<string name=\"programming/test__unit__lesson/4/7/inputEncoding\">json-v1</string>"
        "<string name=\"programming/test__unit__lesson/4/4/source\">original independiente</string>"
        "</map>");
    text_file(root / "files" / "cpp_projects" / "saved-source.cpp", "\tint value = 3;\r\n");
}
std::string xml_text(std::string_view value, bool numeric_newlines = false) {
    std::string escaped;
    for (const auto character : value) {
        if (numeric_newlines && character == '\r') escaped += "&#13;";
        else if (numeric_newlines && character == '\n') escaped += "&#10;";
        else if (numeric_newlines && character == '\t') escaped += "&#9;";
        else if (character == '&') escaped += "&amp;";
        else if (character == '<') escaped += "&lt;";
        else if (character == '>') escaped += "&gt;";
        else if (character == '"') escaped += "&quot;";
        else escaped += character;
    }
    return escaped;
}
void export_preferences(const std::filesystem::path& root, const StateData& data, bool numeric_newlines = false) {
    for (const auto& [domain, values] : data.preferences) {
        std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for (const auto& [key, value] : values) std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            const auto name = " name=\"" + xml_text(key) + "\"";
            if constexpr (std::is_same_v<T, std::string>) xml += "<string" + name + ">" + xml_text(item, numeric_newlines) + "</string>";
            else if constexpr (std::is_same_v<T, bool>) xml += "<boolean" + name + " value=\"" + (item ? "true" : "false") + "\"/>";
            else if constexpr (std::is_same_v<T, std::int64_t>) xml += "<int" + name + " value=\"" + std::to_string(item) + "\"/>";
            else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                xml += "<set" + name + ">";
                for (const auto& entry : item) xml += "<string>" + xml_text(entry, numeric_newlines) + "</string>";
                xml += "</set>";
            } else throw std::runtime_error("Unsupported synthetic preference type");
        }, value);
        xml += "</map>"; text_file(root / "shared_prefs" / (domain + ".xml"), xml);
    }
}
void append_be(std::string& output, std::uint64_t value, unsigned count) {
    for (unsigned i = count; i > 0; --i) output.push_back(static_cast<char>(value >> ((i - 1) * 8)));
}
void append_text(std::string& output, std::string_view text) {
    append_be(output, text.size(), 4); output += text;
}
void add_revision(const std::filesystem::path& root) {
    const auto key = sha256("programming/test__unit__lesson/4/7"); std::string bytes;
    append_text(bytes, "lgp-cpp-local-history-v1"); append_text(bytes, key);
    append_text(bytes, "9a8a7874-b19a-4700-86ac-88bd102a63d5"); append_be(bytes, 1792536389797, 8);
    append_text(bytes, "Revision de prueba"); append_be(bytes, 1, 4);
    append_text(bytes, "main.cpp"); append_text(bytes, "    return 0;\n");
    append_be(bytes, 1, 4); append_text(bytes, "main.cpp"); append_be(bytes, 1, 4);
    append_text(bytes, "main.cpp"); append_text(bytes, make_indentation_metadata("    return 0;\n", {0}));
    text_file(root / "files" / "cpp_project_history" / key / (sha256(bytes) + ".revision"), bytes);
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply an isolated output/QA directory");
        const auto base = std::filesystem::absolute(argv[1]) /
            ("state-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        require(!std::filesystem::exists(base), "QA directory must not preexist");
        std::filesystem::create_directories(base);
        StateData initial;
        initial.preferences["types"]["text"] = std::string(" \t\n\r\n    ");
        initial.preferences["types"]["français_élève"] = std::string("cœur, niño, std::cout, 𝑥");
        initial.preferences["types"]["number"] = std::int64_t{-987654321};
        initial.preferences["types"]["float"] = 1.25;
        initial.preferences["types"]["boolean"] = false;
        initial.preferences["types"]["list"] = std::vector<std::string>{"es", "fr", "en"};
        const auto bytes = encode(initial); require(decode(bytes) == initial, "Typed state round-trip failed");
        auto broken = bytes; broken.pop_back(); rejects([&] { (void)decode(broken); });
        broken = bytes; broken.push_back(0); rejects([&] { (void)decode(broken); }); ++groups;

        require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 regression");
        set_saved_page(initial, "test__unit__lesson", 4); require(saved_page(initial, "test__unit__lesson") == 4, "Page identity lost");
        put_response(initial, "exercise_test__unit__lesson_page_4_block_2", {"Zona inicial", true});
        put_response(initial, "exercise_test__unit__lesson_page_4_block_8", {"parcial ", false});
        require(get_response(initial, "exercise_test__unit__lesson_page_4_block_2")->correct, "Correct state lost");
        require(get_response(initial, "exercise_test__unit__lesson_page_4_block_8")->answer == "parcial ", "Partial answer changed"); ++groups;

        lgaed::curriculum::Block review_block; review_block.type = lgaed::curriculum::BlockType::output_prediction;
        Response unreviewed{"std::cout / línea\n", false};
        require(!response_reviewed(unreviewed, review_block), "A partial draft was treated as an evaluated wrong answer");
        mark_response_reviewed(unreviewed, review_block);
        require(unreviewed.reviewed_answer_identity == "78fd5a779d08ebb0ef80584f5c97cbb3f80cdc2977cbcd72afcf598878c6fa07",
            "Android quoted-string review identity changed");
        require(response_reviewed(unreviewed, review_block), "Evaluated review identity was not retained");
        put_response(initial, "reviewed-example", unreviewed);
        auto reviewed = *get_response(initial, "reviewed-example");
        require(response_reviewed(reviewed, review_block), "Reviewed wrong answer was not restored");
        reviewed.answer += ' '; require(!response_reviewed(reviewed, review_block), "Changed answer reused old review");
        clear_response_review(reviewed); reviewed.answer.pop_back();
        require(!response_reviewed(reviewed, review_block), "Editing back revived an unsubmitted review");
        put_response(initial, "reviewed-example", reviewed);
        require(get_response(initial, "reviewed-example")->reviewed_answer_identity.empty(), "Cleared review marker remained in raw JSON");
        reviewed.correct = true; mark_response_reviewed(reviewed, review_block);
        require(!response_reviewed(reviewed, review_block), "Correct answer became incorrect review");
        review_block.type = lgaed::curriculum::BlockType::multiple_choice;
        Response choice; choice.selected = 1; mark_response_reviewed(choice, review_block);
        require(choice.reviewed_answer_identity == "6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b", "Choice review identity changed");
        review_block.type = lgaed::curriculum::BlockType::order_code;
        Response order; order.order = {2, 0, 1}; mark_response_reviewed(order, review_block);
        require(order.reviewed_answer_identity == "a50c12ec8e20411bc79e811c15b13a8ea16e58f08d1c8015c257f83f6bc1c3fd", "Order review identity changed"); ++groups;

        Draft draft{"    return 0;\n", " \t \n", make_indentation_metadata("    return 0;\n", {0}), 4, 4};
        const auto key = "programming/test__unit__lesson/4/7";
        put_draft(initial, key, draft);
        const auto restored = get_draft(initial, key, "ignored", "ignored");
        require(restored.source == draft.source && restored.input == draft.input &&
            restored.indentation_json == draft.indentation_json && restored.selection_start == 4, "Draft whitespace/identity changed");
        require(indentation_units(restored.source, restored.indentation_json) == std::vector<std::int64_t>{0}, "Tab identity lost");
        require(indentation_units("    manually typed", make_indentation_metadata("    manually typed", {})).empty(), "Manual spaces became TAB");
        require(get_draft(initial, "programming/test__unit__lesson/4/4", "independent").source == "independent", "Editor drafts collided"); ++groups;

        const std::string crlf_source = "\r\n    return 0;\r\n";
        Draft crlf{crlf_source, "Rita\r4\r\n \t ", make_indentation_metadata(crlf_source, {2}), 6, 6};
        put_draft(initial, "programming/test__unit__lesson/5/1", crlf);
        const auto original_crlf = get_draft(initial, "programming/test__unit__lesson/5/1", "");
        require(original_crlf.source == crlf_source && original_crlf.input == crlf.input &&
                indentation_units(original_crlf.source, original_crlf.indentation_json) == std::vector<std::int64_t>{2},
                "CRLF/CR or original UTF-16 indentation offsets changed"); ++groups;

        lgaed::curriculum::Lesson mapped;
        mapped.id = "mapped__unit__lesson"; mapped.locale = "es"; mapped.pages.resize(2);
        mapped.state_layout.version = 1;
        mapped.state_layout.origin = {{"p1", {"b1", "b2"}}, {"p2", {"b3"}}};
        mapped.state_layout.current = {{"p2", {"b3", "b2"}}, {"p1", {"b1"}}};
        mapped.state_layout.lineage = sha256("p1:b1,b2|p2:b3");
        mapped.state_layout.legacy_lineage = mapped.state_layout.lineage;
        StateData mapped_state; set_saved_page(mapped_state, mapped.id, 0);
        prepare_lesson_state(mapped_state, mapped);
        require(current_page(mapped_state, mapped) == 1, "Historical bookmark treated as visible page");
        save_current_page(mapped_state, mapped, 0);
        require(saved_page(mapped_state, mapped.id) == 1, "Visible page did not save historical slot");
        require(std::get<std::vector<std::string>>(*find(mapped_state, "learning_progress", "lesson_mapped__unit__lesson_read_blocks_v1")).size() == 3,
            "Reading coverage lost after reorder");
        const auto before_wrong_family = mapped_state;
        mapped.state_layout.lineage = std::string(64, '0');
        rejects([&] { prepare_lesson_state(mapped_state, mapped); });
        require(mapped_state == before_wrong_family, "Wrong family altered progress"); ++groups;

        StateStore store(base / "windows-profile"); require(store.empty(), "New profile not empty");
        require(store.load().preferences.empty(), "New state was invented"); store.save(initial);
        StateStore reopened(base / "windows-profile"); require(reopened.load() == initial, "Durable reopen failed");
        StateStore stale(base / "windows-profile"); (void)stale.load();
        auto updated = initial; set_saved_page(updated, "test__unit__lesson", 5); reopened.save(updated);
        rejects([&] { stale.save(initial); }); require(store.load() == updated, "Stale writer replaced newer state"); ++groups;

        const auto source = base / "export"; fixture(source); add_revision(source);
        const auto xml_before = sha256(read_bytes(source / "shared_prefs" / "learning_progress.xml"));
        auto preview = inspect_android_export(source);
        require(preview.preference_count == 12 && preview.project_revisions == 1, "Incomplete Android preview");
        require(saved_page(preview.data, "test__unit__lesson") == 4, "Android page not imported");
        require(get_response(preview.data, "exercise_test__unit__lesson_page_4_block_2")->correct, "Android completed answer lost");
        require(get_response(preview.data, "exercise_test__unit__lesson_page_4_block_8")->answer == "parcial ", "Android partial answer lost");
        const auto imported = get_draft(preview.data, key, "bad");
        require(imported.source == "    return 0;\n" && imported.input == " \t \n", "Android draft data altered"); ++groups;

        commit_android_import(preview, base / "imported-profile");
        StateStore imported_store(base / "imported-profile"); require(imported_store.load() == preview.data, "Transaction did not import exact preferences");
        require(sha256(read_bytes(source / "shared_prefs" / "learning_progress.xml")) == xml_before, "Import changed source");
        for (const auto& file : preview.files)
            require(sha256(read_bytes(base / "imported-profile" / "state" / "active" / "android" / file.relative_path)) == file.sha256, "Archive changed original bytes");
        rejects([&] { commit_android_import(preview, base / "imported-profile"); });
        require(imported_store.load() == preview.data, "Duplicate import changed real state"); ++groups;

        text_file(source / "files" / "cpp_projects" / "saved-source.cpp", "changed");
        rejects([&] { commit_android_import(preview, base / "changed-target"); });
        require(!std::filesystem::exists(base / "changed-target" / "state" / "active"), "Partial import became active"); ++groups;

        const auto malicious = base / "malicious"; fixture(malicious);
        text_file(malicious / "shared_prefs" / "learning_progress.xml",
            "<!DOCTYPE map [<!ENTITY external SYSTEM 'file:///C:/Windows/win.ini'>]><map><string name='x'>&external;</string></map>");
        rejects([&] { (void)inspect_android_export(malicious); }); ++groups;

        const auto version = base / "unknown-version"; fixture(version);
        text_file(version / "shared_prefs" / "cpp_lesson_drafts.xml",
            "<map><string name='programming/a/0/0/sourceEncoding'>json-v2</string></map>");
        rejects([&] { (void)inspect_android_export(version); }); ++groups;

        const auto wider_export = base / "wider-export"; fixture(wider_export);
        text_file(wider_export / "databases" / "student.db", "opaque database");
        rejects([&] { (void)inspect_android_export(wider_export); }); ++groups;

        const auto corrupt = base / "corrupt-history"; fixture(corrupt); add_revision(corrupt);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(native_io_path(corrupt / "files" / "cpp_project_history")))
            if (entry.is_regular_file()) { text_file(entry.path(), "corrupt"); break; }
        rejects([&] { (void)inspect_android_export(corrupt); }); ++groups;

        auto proved = initial;
        proved.preferences["cpp_lesson_drafts"][std::string(key) + "/source"] = std::string("    return 0;\n    ");
        proved.preferences["cpp_lesson_drafts"].erase(std::string(key) + "/sourceEncoding");
        require(get_draft(proved, key, "").source == draft.source, "SHA-proven legacy repair failed");
        proved.preferences["cpp_lesson_drafts"][std::string(key) + "/input"] = std::string("Leo\n2\n    ");
        proved.preferences["cpp_lesson_drafts"].erase(std::string(key) + "/inputEncoding");
        proved.preferences["cpp_lesson_drafts"].erase(std::string(key) + "/inputSha256");
        require(get_draft(proved, key, "").input == "Leo\n2\n    ", "Unproven manual whitespace removed"); ++groups;

        auto escaped_source = initial;
        Draft deliberate{"    return 0;\n    ", "", "", 0, 0};
        put_draft(escaped_source, key, deliberate);
        // An old sidecar is not permission to repair already escaped source text.
        escaped_source.preferences["cpp_lesson_drafts"][std::string(key) + "/sourceIndentation_v2"] =
            make_indentation_metadata("    return 0;\n", {0});
        const auto escaped_before = escaped_source;
        const auto preserved = get_draft(escaped_source, key, "");
        require(preserved.source == deliberate.source && preserved.indentation_json.empty(),
                "json-v1 source was trimmed using stale indentation metadata");
        require(escaped_source == escaped_before, "Reading a stale optional sidecar changed stored data");
        escaped_source.preferences["cpp_lesson_drafts"][std::string(key) + "/sourceEncoding"] = std::string{};
        rejects([&] { (void)get_draft(escaped_source, key, ""); }); ++groups;

        const auto real_lesson = lgaed::windows::load_lesson(std::filesystem::path(lgaed::build::source_root) /
            L"app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json");
        require(real_lesson.state_layout.version == 2 && real_lesson.pages.size() == 6, "Expected existing U1L2 v2 contract");
        const lgaed::curriculum::Block* prediction = nullptr;
        const lgaed::curriculum::Block* conceptual = nullptr;
        std::vector<const lgaed::curriculum::Block*> editors;
        for (const auto& block : real_lesson.pages.at(4).blocks) {
            if (block.type == lgaed::curriculum::BlockType::output_prediction) prediction = &block;
            if (block.type == lgaed::curriculum::BlockType::multiple_choice) conceptual = &block;
            if (block.type == lgaed::curriculum::BlockType::cpp_compiler) editors.push_back(&block);
        }
        require(prediction && conceptual && editors.size() == 2, "Expected independent prediction, editors and conceptual answer");
        StateData real_state; save_current_page(real_state, real_lesson, 4);
        const auto prediction_key = lgaed::curriculum::exercise_key(real_lesson, *prediction);
        const auto conceptual_key = lgaed::curriculum::exercise_key(real_lesson, *conceptual);
        real_state.preferences["learning_progress"][prediction_key] = std::string("{\"answer\":\"Zona inicial\",\"correct\":true}");
        real_state.preferences["learning_progress"][conceptual_key] = std::string("{\"selectedIndex\":-1,\"correct\":false}");
        const auto original_key = lgaed::curriculum::draft_key("programming", real_lesson, *editors[0]);
        const auto modified_key = lgaed::curriculum::draft_key("programming", real_lesson, *editors[1]);
        require(original_key != modified_key && original_key.find("state-layout-v2/") != original_key.npos,
                "New original editor reused the historical modification draft");
        auto changed_code = editors[1]->initial_code;
        const auto literal = changed_code.find("Zona inicial"); require(literal != changed_code.npos, "Expected existing original literal");
        changed_code.replace(literal, std::string("Zona inicial").size(), "Caja recibida");
        Draft original{editors[0]->initial_code, " \t \r\n", "", 0, 0};
        Draft modified{changed_code + "    ", "Leo\n2\n", "", 0, 0};
        put_draft(real_state, original_key, original); put_draft(real_state, modified_key, modified);
        real_state.preferences["cpp_lesson_drafts"][modified_key + "/sourceIndentation_v2"] =
            make_indentation_metadata(changed_code, {static_cast<std::int64_t>(changed_code.find("    std::cout"))});
        // Native selection keys are intentionally absent: Android's single-source draft did not store them.
        for (const auto& draft_key : {original_key, modified_key}) {
            real_state.preferences["cpp_lesson_drafts"].erase(draft_key + "/windowsSelectionStart");
            real_state.preferences["cpp_lesson_drafts"].erase(draft_key + "/windowsSelectionEnd");
        }
        const auto before_validation = real_state;
        validate_lesson_state(real_state, real_lesson);
        require(real_state == before_validation, "Read-only lesson validation changed Android state");
        auto bad_correct = real_state;
        bad_correct.preferences["learning_progress"][prediction_key] = std::string("{\"answer\":\"Caja diferente\",\"correct\":true}");
        const auto bad_correct_before = bad_correct;
        rejects([&] { validate_lesson_state(bad_correct, real_lesson); });
        require(bad_correct == bad_correct_before, "Incompatible correct state was silently reset");
        auto partial = real_state;
        partial.preferences["learning_progress"][prediction_key] = std::string("{\"answer\":\"Zona inicial\",\"correct\":false}");
        validate_lesson_state(partial, real_lesson);
        require(!get_response(partial, prediction_key)->correct, "Unsubmitted correct text became a submitted correct answer");
        auto bad_choice = real_state; Response out_of_range;
        out_of_range.selected = static_cast<std::int64_t>(conceptual->options.size());
        mark_response_reviewed(out_of_range, *conceptual); put_response(bad_choice, conceptual_key, out_of_range);
        rejects([&] { validate_lesson_state(bad_choice, real_lesson); });
        auto empty_pin = real_state;
        empty_pin.preferences["learning_progress"]["lesson_" + real_lesson.id + "_state_lineage_v2_es"] = std::string{};
        const auto empty_pin_before = empty_pin;
        rejects([&] { validate_lesson_state(empty_pin, real_lesson); });
        require(empty_pin == empty_pin_before, "An empty existing v2 pin was overwritten as a fresh profile"); ++groups;

        const auto real_export = base / "real-contract-export"; export_preferences(real_export, real_state);
        auto real_preview = inspect_android_export(real_export);
        validate_lesson_state(real_preview.data, real_lesson);
        const auto real_before = sha256(read_bytes(real_export / "shared_prefs" / "cpp_lesson_drafts.xml"));
        commit_android_import(real_preview, base / "real-contract-profile");
        StateStore real_store(base / "real-contract-profile"); const auto real_restored = real_store.load();
        validate_lesson_state(real_restored, real_lesson);
        require(current_page(real_restored, real_lesson) == 4, "U1L2 historical page did not recover visible page 5");
        require(get_response(real_restored, prediction_key)->correct, "U1L2 completed prediction lost its identity");
        require(get_draft(real_restored, original_key, "").source == original.source &&
                get_draft(real_restored, modified_key, "").source == modified.source &&
                get_draft(real_restored, original_key, "").input == original.input,
                "U1L2 independent sources or exact hidden stdin changed during import");
        require(sha256(read_bytes(real_export / "shared_prefs" / "cpp_lesson_drafts.xml")) == real_before,
                "U1L2 export source changed during compatibility checks"); ++groups;

        const auto reserved_export_rejected = [&](std::string_view name, Value receipt) {
            auto reserved_state = real_state;
            reserved_state.preferences["native_windows"]["reference-proof/v1/es/" + modified_key] = std::move(receipt);
            reserved_state.preferences["independent_android"]["retained"] = std::string(" \\t original Android preference ");
            const auto reserved_source = base / ("reserved-windows-" + std::string(name));
            export_preferences(reserved_source, reserved_state);
            text_file(reserved_source / "files" / "cpp_projects" / "retained.cpp", "    return 0;\r\n");
            const auto source_hashes = [&] {
                std::map<std::filesystem::path, std::string> result;
                for (const auto& file : std::filesystem::recursive_directory_iterator(native_io_path(reserved_source)))
                    if (file.is_regular_file()) result.emplace(file.path(), sha256(read_bytes(file.path())));
                return result;
            };
            const auto hashes_before = source_hashes();
            StateStore destination(base / ("reserved-windows-profile-" + std::string(name)));
            const auto destination_before = destination.load();
            require(destination.empty() && destination_before.preferences.empty(), "Reserved-domain QA destination was not empty");
            bool refused = false;
            try { (void)inspect_android_export(reserved_source); }
            catch (const std::runtime_error& error) {
                const std::string message = error.what();
                require(message.find("native_windows") != message.npos && message.find("reservado de Windows") != message.npos &&
                        message.find("no se ha importado nada") != message.npos,
                        "Reserved Windows domain did not produce its explicit pre-publication diagnostic");
                refused = true;
            }
            require(refused, "Android import accepted the Windows-only completion domain");
            require(source_hashes() == hashes_before, "Rejecting the Windows-only domain changed the export source");
            require(destination.empty() && destination.load() == destination_before,
                    "Rejecting the Windows-only domain published state or changed the empty profile");
            ++groups;
        };
        auto reference_frame = lgaed::curriculum::reference_objective_frame(real_lesson, *editors[1]);
        reference_frame += "/source/" + std::to_string(modified.source.size()) + ':' + modified.source;
        reference_frame += "/input/0:";
        reserved_export_rejected("reference-proof", sha256(reference_frame));
        reserved_export_rejected("integer", std::int64_t{17});

        auto legacy_references = real_state;
        std::string legacy_source;
        for (const auto character : original.source) legacy_source += character == '\n' ? "\r\n" : std::string(1, character);
        const std::string legacy_input = "A\r\nB\rC\t \n&#13;";
        auto& legacy_prefs = legacy_references.preferences["cpp_lesson_drafts"];
        legacy_prefs[original_key + "/source"] = legacy_source;
        legacy_prefs[original_key + "/input"] = legacy_input;
        legacy_prefs.erase(original_key + "/sourceEncoding");
        legacy_prefs.erase(original_key + "/inputEncoding");
        legacy_prefs.erase(original_key + "/inputSha256");
        const auto references_export = base / "legacy-character-references";
        export_preferences(references_export, legacy_references, true);
        text_file(references_export / "shared_prefs" / "xml_character_probe.xml",
            "<map><string name='hex'>A&#xD;&#xA;B&#xD;C&#x9; </string>"
            "<string name='physical'>A\r\nB\rC</string>"
            "<string name='literal'><![CDATA[&#13;]]>&amp;#13;</string></map>");
        auto references_preview = inspect_android_export(references_export);
        validate_lesson_state(references_preview.data, real_lesson);
        const auto legacy_read = get_draft(references_preview.data, original_key, "");
        require(legacy_read.source == legacy_source && legacy_read.input == legacy_input,
                "XML numeric CR/LF references changed legacy source or stdin");
        require(string_value(references_preview.data, "xml_character_probe", "hex") == "A\r\nB\rC\t " &&
                string_value(references_preview.data, "xml_character_probe", "physical") == "A\nB\nC" &&
                string_value(references_preview.data, "xml_character_probe", "literal") == "&#13;&#13;",
                "XML 1.0 reference/physical-line-ending/CDATA distinction changed");
        const auto references_before = sha256(read_bytes(references_export / "shared_prefs" / "cpp_lesson_drafts.xml"));
        commit_android_import(references_preview, base / "legacy-reference-profile");
        StateStore references_store(base / "legacy-reference-profile"); const auto references_state = references_store.load();
        require(get_draft(references_state, original_key, "").source == legacy_source &&
                get_draft(references_state, original_key, "").input == legacy_input,
                "Legacy referenced CR/LF changed after committed import and reopen");
        require(sha256(read_bytes(references_export / "shared_prefs" / "cpp_lesson_drafts.xml")) == references_before,
                "Legacy XML source changed during reference preservation"); ++groups;

        const auto state_file = base / "windows-profile" / "state" / "active" / "state.bin";
        text_file(state_file, "corrupt"); rejects([&] { (void)store.load(); }); ++groups;
        rejects([&] { store.save(initial); });
        std::cout << groups << " state/import groups passed; QA: " << base.string() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << "State/import failure: " << error.what() << '\n'; return 1; }
}

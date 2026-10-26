#include "core/lesson_state.hpp"
#include "core/lesson.hpp"
#include "core/catalog.hpp"
#include "core/retirement_test_fixture.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

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
void retirement_state_tests(const lgaed::curriculum::Lesson& before, const std::filesystem::path& base) {
    using namespace lgaed::curriculum;
    const std::vector<std::string> retiring{"ejecucion_original_consigna","ejecucion_original_editor"};
    const auto retired = lgaed::test::retire_lesson(before,retiring);
    const auto old_position = stable_block_position(before,retiring[1]);
    require(old_position.has_value(),"Historical compiler fixture is absent");
    const auto& old_editor = before.pages[old_position->page].blocks[old_position->block];
    const auto& archived_editor = retired.retired_blocks.at(1);
    const auto old_key = draft_key("programming",before,old_editor);
    require(old_key == draft_key("programming",retired,archived_editor),"Archived compiler draft key changed");
    StateData original;
    prepare_lesson_state(original,before);
    save_current_page(original,before,old_position->page);
    Draft draft{old_editor.initial_code + "// borrador historico independiente\n", " \t\n", "", 5, 5};
    put_draft(original,old_key,draft);
    put_response(original,exercise_key(before,old_editor),{"historial conservado",true});
    original.preferences["native_windows"]["reference-proof/v1/es/" + old_key] = sha256("historical opaque proof");
    StateStore store(base/"retired-compiler-profile");
    require(store.load().preferences.empty(),"Retirement QA profile must begin empty");
    store.save(original);
    auto migrated = store.load(); validate_lesson_state(migrated,retired);
    prepare_lesson_state(migrated,retired);
    for (const auto& [domain, values] : original.preferences) for (const auto& [key, value] : values)
        require(migrated.preferences.at(domain).at(key) == value,"Retirement overwrote existing student data");
    require(get_draft(migrated,old_key,"").source == draft.source && get_draft(migrated,old_key,"").input == draft.input,
        "Retired compiler lost its draft or significant stdin");
    require(current_page(migrated,retired) == old_position->page,
        "Retired compiler changed the historical page bookmark");
    store.save(migrated); const auto reopened = store.load(); validate_lesson_state(reopened,retired);
    require(reopened == migrated,"Retirement state did not survive normal persistence/reopening");
    const auto integrated=lgaed::windows::load_lesson(before.source_path);
    require(integrated.compiler_puzzles_required&&integrated.retired_blocks.size()==2,
        "The active Spanish asset has not integrated the expected retirement");
    auto current=reopened;validate_lesson_state(current,integrated);prepare_lesson_state(current,integrated);
    require(current==reopened,"Opening the integrated asset rewrote the preserved migration data");
    auto downgraded = reopened; const auto downgrade_before = downgraded;
    rejects([&]{prepare_lesson_state(downgraded,before);});
    require(downgraded == downgrade_before,"Rejecting a removed retirement archive modified stored data");
    auto rewritten_lesson = retired;
    rewritten_lesson.state_layout.retired.at(0).sha256 = std::string(64,'0');
    auto rewritten = reopened; const auto rewrite_before = rewritten;
    rejects([&]{prepare_lesson_state(rewritten,rewritten_lesson);});
    require(rewritten == rewrite_before,"Rejecting an archived snapshot change modified stored data");
    for(const bool remove:{false,true}) {
        auto changed_history=retired;
        if(remove)changed_history.state_layout.retirement_history.clear();
        else changed_history.state_layout.retirement_history.at(0).sha256=std::string(64,'0');
        auto saved=reopened;const auto saved_before=saved;
        rejects([&]{prepare_lesson_state(saved,changed_history);});
        require(saved==saved_before,"Rejecting changed or removed historical reading provenance modified student data");
    }
    for (const auto language : {"en","fr"}) {
        const auto translated = lgaed::windows::load_lesson(std::filesystem::path(lgaed::build::source_root)/
            "app/src/main/assets/content"/language/"programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json",language);
        auto changed_language = reopened; prepare_lesson_state(changed_language,translated);
        require(changed_language == reopened,"Changing to an untouched translation rewrote Spanish retirement state");
    }
    ++groups;
}
void refined_order_state_tests(const std::filesystem::path& base) {
    using namespace lgaed::curriculum;
    const auto original = lgaed::windows::parse_lesson_json(R"json({"id":"qa_order_lines","title":"Ordenar sin perder estado","pages":[{"blocks":[{"type":"order_code","id":"order","question":"Ordena las líneas.","fragments":["    return 0;\n}","#include <iostream>\nint main() {\n    std::cout << \"Paso\\n\";"],"correctOrder":[1,0],"explanation":"Conserva el programa."}]}]})json");
    const auto& old_block = original.pages[0].blocks[0];
    auto refined = original;
    auto& block = refined.pages[0].blocks[0];
    OrderRefinement contract;
    contract.id = block.stable_id;contract.raw_json = block.raw_json;
    contract.original_fragments = block.fragments;contract.original_correct_order = block.correct_order;
    contract.groups = {{0,1},{2,3,4}};
    block.fragments = {"    return 0;", "}", "#include <iostream>", "int main() {", "    std::cout << \"Paso\\n\";"};
    block.correct_order = {2,3,4,0,1};
    contract.representation_signature = sha256(order_representation_frame(block));
    block.order_refinement = contract;
    const auto key = exercise_key(original,old_block);
    const auto sidecar = response_storage_key(refined,block);
    require(key == exercise_key(refined,block) && key != sidecar,"Refinement changed the activity identity or reused its legacy storage");
    StateData empty;const auto empty_before=empty;
    require(!get_response(empty,refined,block) && empty==empty_before,"An empty order invented a response or wrote migration state");
    for (const auto mode : {"empty", "draft", "wrong", "correct"}) {
        StateData saved;
        Response old;old.raw_json=R"({"incorrectAttempts":4,"opaque":{"cursor":7},"answer":"unchanged"})";
        old.answer="unchanged";old.correct=std::string_view(mode)=="correct";
        if(std::string_view(mode)!="empty")old.order=old.correct?std::vector<std::int64_t>{1,0}:std::vector<std::int64_t>{0,1};
        if(std::string_view(mode)=="wrong")mark_response_reviewed(old,old_block);
        put_response(saved,key,old);
        const auto legacy_json=string_value(saved,"learning_progress",key);const auto before=saved;
        const auto projected=get_response(saved,refined,block);
        require(projected && projected->correct==old.correct && projected->answer==old.answer && saved==before,
            "Reading the refined order changed the stored answer or its prior verdict");
        const auto expected=old.order.empty()?std::vector<std::int64_t>{}:old.correct?std::vector<std::int64_t>{2,3,4,0,1}:std::vector<std::int64_t>{0,1,2,3,4};
        require(projected->order==expected,"Historical groups were not expanded in their exact attempted sequence");
        require(response_reviewed(*projected,block)==(std::string_view(mode)=="wrong"),
            "A draft was promoted to a reviewed answer or a previous review was lost");
        validate_lesson_state(saved,refined);
        const auto export_root=base/(std::string("order-export-")+mode);
        export_preferences(export_root,saved);
        const auto source_hash=sha256(read_bytes(export_root/"shared_prefs/learning_progress.xml"));
        const auto preview=inspect_android_export(export_root);
        validate_lesson_state(preview.data,refined);
        require(get_response(preview.data,refined,block)->order==expected &&
            string_value(preview.data,"learning_progress",key)==legacy_json,
            "Importing a historical order changed its JSON or projected sequence");
        commit_android_import(preview,base/(std::string("order-profile-")+mode));
        StateStore store(base/(std::string("order-profile-")+mode));auto reopened=store.load();
        require(sha256(read_bytes(export_root/"shared_prefs/learning_progress.xml"))==source_hash,
            "Order refinement modified an Android source export");
        auto changed=*get_response(reopened,refined,block);
        changed.order={0,2,1,3,4};changed.correct=false;mark_response_reviewed(changed,block);
        put_response(reopened,refined,block,changed);
        require(string_value(reopened,"learning_progress",key)==legacy_json &&
            get_response(reopened,refined,block)->order==changed.order &&
            get_response(reopened,refined,block)->raw_json.find("\"incorrectAttempts\":4")!=std::string::npos &&
            get_response(reopened,refined,block)->raw_json.find("\"cursor\":7")!=std::string::npos,
            "Editing separate lines overwrote the grouped original or discarded opaque attempt metadata");
        store.save(reopened);reopened=store.load();validate_lesson_state(reopened,refined);
        require(get_response(reopened,refined,block)->order==changed.order &&
            response_reviewed(*get_response(reopened,refined,block),block),"Refined attempt/review did not survive reopening");
        const auto sidecar_export=base/(std::string("order-sidecar-export-")+mode);
        export_preferences(sidecar_export,reopened);
        const auto sidecar_import=inspect_android_export(sidecar_export);
        validate_lesson_state(sidecar_import.data,refined);
        require(get_response(sidecar_import.data,refined,block)->order==changed.order&&
            string_value(sidecar_import.data,"learning_progress",key)==legacy_json,
            "Importing a saved line sidecar lost its independent composition or overwrote the original");
        auto translated=original;translated.locale="fr";
        require(get_response(reopened,translated,translated.pages[0].blocks[0])->order==old.order &&
            get_response(reopened,translated,translated.pages[0].blocks[0])->correct==old.correct,
            "Editing Spanish lines changed the unchanged translation's grouped answer");
        put_response(reopened,refined,block,{});
        require(get_response(reopened,refined,block)->order.empty() && !get_response(reopened,refined,block)->correct &&
            string_value(reopened,"learning_progress",key)==legacy_json,
            "Reset did not clear only the refined composition");
        auto malformed=reopened;auto duplicate=changed;duplicate.order={0,0,2,3,4};
        // Deliberately bypass the writing API to simulate corrupt imported data.
        put_response(malformed,sidecar,duplicate);const auto corrupt_before=malformed;
        rejects([&]{validate_lesson_state(malformed,refined);});
        require(malformed==corrupt_before,"Rejecting a corrupt refinement changed student data");
        auto foreign=refined;foreign.pages[0].blocks[0].fragments[0]+=" changed";
        rejects([&]{(void)get_response(reopened,foreign,foreign.pages[0].blocks[0]);});
        ++groups;
    }
    auto different_locale=refined;different_locale.locale="en";
    require(response_storage_key(different_locale,different_locale.pages[0].blocks[0])!=sidecar,
        "Refined state sidecars do not distinguish locale");
    std::cout<<"PASS grouped-to-line order: empty/draft/reviewed/solved projections, immutable original JSON, Android import, opaque attempts, independent locale, sidecar/reopen/reset and corrupt-state rejection\n";
}
void all_authored_refined_orders_preserve_state(const std::filesystem::path& base) {
    using namespace lgaed::curriculum;
    using namespace winrt::Windows::Data::Json;
    const auto fixtures=std::filesystem::path(lgaed::build::cpp_tool_host).parent_path().parent_path()/"test-fixtures/order-lines";
    const auto bytes=read_bytes(fixtures/"manifest.json");
    const auto manifest=JsonObject::Parse(winrt::to_hstring(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
    const auto entries=manifest.GetNamedArray(L"entries");
    require(entries.Size()==14,"The audited refinement corpus must retain all fourteen activities");
    std::size_t old_pieces=0,new_pieces=0;
    for(const auto& value:entries) {
        const auto entry=value.GetObject();
        const auto old_path=fixtures/winrt::to_string(entry.GetNamedString(L"old"));
        const auto current_path=fixtures/winrt::to_string(entry.GetNamedString(L"refined"));
        const auto original=lgaed::windows::load_lesson(old_path);
        const auto current=lgaed::windows::load_lesson(current_path);
        const auto position=stable_block_position(current,winrt::to_string(entry.GetNamedString(L"stableId")));
        require(position.has_value(),"The authored refinement lost its stable activity");
        const auto& block=current.pages[position->page].blocks[position->block];
        require(block.order_refinement.has_value(),"The authored order has no compatible refinement projection");
        const auto old_position=stable_block_position(original,block.stable_id);
        const auto& old_block=old_position?original.pages[old_position->page].blocks[old_position->block]
            :original.pages[block.address.page].blocks[block.address.block];
        old_pieces+=old_block.fragments.size();new_pieces+=block.fragments.size();
        const auto key=exercise_key(original,old_block);
        StateData data;Response solved;solved.correct=true;
        for(const auto piece:old_block.correct_order)solved.order.push_back(static_cast<std::int64_t>(piece));
        solved.raw_json=R"({"incorrectAttempts":3,"opaque":{"selection":23}})";
        put_response(data,key,solved);const auto raw=string_value(data,"learning_progress",key);
        const auto before=data;const auto projected=get_response(data,current,block);
        require(projected&&projected->correct&&data==before,"Refining an authored solved puzzle rewrote its prior state");
        std::vector<std::int64_t> expected;for(const auto piece:block.correct_order)expected.push_back(static_cast<std::int64_t>(piece));
        require(projected->order==expected,"An authored solved order was assigned to a different line sequence");
        validate_lesson_state(data,current);prepare_lesson_state(data,current);
        const auto pin="lesson_"+current.id+"_state_order_refinements_v1_es";
        require(find(data,"learning_progress",pin),"Published order refinements were not pinned to their source/representation");
        if(old_pieces==4) {
            const auto catalog_root=base/"order-catalog";
            const auto unit=current.id.substr(0,current.id.rfind("__"));
            const auto relative=std::filesystem::path(winrt::to_string(entry.GetNamedString(L"path")));
            text_file(catalog_root/"es/programming/courses.json",R"([{"id":"cpp_fundamentals","title":"QA","number":1}])");
            text_file(catalog_root/"es/programming/cpp_fundamentals/course_cpp_fundamentals.json",
                "{\"id\":\"cpp_fundamentals\",\"title\":\"QA\",\"units\":[{\"id\":\""+unit+"\",\"title\":\"QA\",\"number\":1}]}");
            text_file(catalog_root/"es/programming/cpp_fundamentals"/relative.parent_path()/("unit_"+relative.parent_path().string()+".json"),
                "{\"courseId\":\"cpp_fundamentals\",\"unitId\":\""+unit+"\",\"title\":\"QA\",\"lessons\":[{\"id\":\""+current.id+"\",\"title\":\"QA\",\"number\":1}]}");
            text_file(catalog_root/"es/programming/cpp_fundamentals"/relative,current.raw_json);
            CatalogLocation location{"es","programming","cpp_fundamentals",unit,{}};
            const auto card=lgaed::windows::load_catalog(catalog_root,location).entries.at(0);
            auto shown=data;save_current_page(shown,current,position->page);
            for(const auto& sibling:current.pages[position->page].blocks)if(sibling.type==BlockType::multiple_choice) {
                Response answer;answer.correct=true;answer.selected=static_cast<std::int64_t>(*sibling.correct_index);
                put_response(shown,current,sibling,answer);
            }
            const auto completed=lgaed::windows::catalog_entry_progress(catalog_root,location,card,shown);
            auto changed=*projected;changed.correct=false;std::swap(changed.order.front(),changed.order.back());
            put_response(shown,current,block,changed);
            const auto pending=lgaed::windows::catalog_entry_progress(catalog_root,location,card,shown);
            require(completed.completed==pending.completed+1,"Catalog credited the old grouped success over a newer pending line response");
            reset_catalog_progress(shown,{{},{},{current.id}});
            require(lgaed::windows::catalog_entry_progress(catalog_root,location,card,shown).completed==0,
                "Catalog retained a refined achievement after explicit scope reset");
        }
        for(const auto change:{"remove","snapshot","representation"}) {
            auto bad=current;
            if(std::string_view(change)=="remove")bad.state_layout.order_refinements.clear();
            else if(std::string_view(change)=="snapshot")bad.state_layout.order_refinements[0].sha256=std::string(64,'0');
            else bad.state_layout.order_refinements[0].representation_signature=std::string(64,'0');
            auto retained=data;const auto prior=retained;
            rejects([&]{prepare_lesson_state(retained,bad);});
            require(retained==prior,"Rejecting a removed/changed order projection modified progress");
        }
        const auto export_root=base/("authored-order-export-"+std::to_string(old_pieces));
        // Import the original grouped answer, rather than Windows' new sidecar.
        export_preferences(export_root,before);
        const auto preview=inspect_android_export(export_root);
        validate_lesson_state(preview.data,current);
        require(get_response(preview.data,current,block)->order==expected&&
            string_value(preview.data,"learning_progress",key)==raw,"An authored grouped import lost its exact response or correct line expansion");
        auto edited=*projected;edited.correct=false;
        std::swap(edited.order.front(),edited.order.back());put_response(data,current,block,edited);
        for(const auto locale:{"en","fr"}) {
            const auto translated=lgaed::windows::load_lesson(fixtures/winrt::to_string(entry.GetNamedString(winrt::to_hstring(locale))),locale);
            auto switched=data;prepare_lesson_state(switched,translated);
            require(string_value(switched,"learning_progress",key)==raw,"Switching translation replaced the original grouped response");
        }
        const auto sidecar=response_storage_key(current,block);
        data.preferences["learning_progress"]["order-lines/v1/es/"+std::string(64,'0')+"/exercise_other_page_0_block_0"]=std::string("outside scope");
        const auto invalid_sidecar="order-lines/v1/de/"+std::string(64,'0')+'/'+key;
        data.preferences["learning_progress"][invalid_sidecar]=std::string("unknown metadata");
        reset_catalog_progress(data,{{},{},{current.id}});
        require(!find(data,"learning_progress",sidecar)&&!find(data,"learning_progress",key)&&find(data,"learning_progress",pin)&&
            find(data,"learning_progress",invalid_sidecar)&&
            find(data,"learning_progress","order-lines/v1/es/"+std::string(64,'0')+"/exercise_other_page_0_block_0"),
            "Reset scope retained a refined achievement or erased unrelated/unknown state/provenance");
        ++groups;
    }
    require(old_pieces==65&&new_pieces==124,"The authored order corpus no longer covers the complete 65-to-124 refinement");
    std::cout<<"PASS fourteen authored order projections (65 to124), grouped import, unchanged translations, append-only runtime provenance and scoped course/unit reset\n";
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply an isolated output/QA directory");
        const auto base = std::filesystem::absolute(argv[1]) /
            ("state-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        require(!std::filesystem::exists(base), "QA directory must not preexist");
        std::filesystem::create_directories(base);
        refined_order_state_tests(base);
        all_authored_refined_orders_preserve_state(base);
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

        const auto fills = lgaed::windows::parse_lesson_json(R"({"id":"fill-state","title":"Fill","pages":[{"blocks":[{"type":"inline_fill_code","template":"std::cout << \"[]\" << [];","answers":["a  b","!="] ,"answerAlternatives":[["a  b"],["!=","not_eq"]]},{"type":"drag_fill_code","template":"int a = 0[] int b = 1[]","choices":[";",";","}"],"correctOrder":[0,1]}]}]})");
        const auto& inline_block = fills.pages[0].blocks[0]; const auto& drag_block = fills.pages[0].blocks[1];
        Response inline_review; inline_review.raw_json = R"({"answers":[" a  b","<<"],"correct":false,"opaque":{"original":"metadata"}})";
        mark_response_reviewed(inline_review,inline_block);
        require(inline_review.reviewed_answer_identity == sha256(R"([" a  b","<<"])"),"Inline review identity does not match Android answers-array framing");
        StateData fill_state; const auto inline_key = lgaed::curriculum::exercise_key(fills,inline_block), drag_key = lgaed::curriculum::exercise_key(fills,drag_block);
        put_response(fill_state,inline_key,inline_review);
        auto inline_restored = *get_response(fill_state,inline_key);
        require(response_reviewed(inline_restored,inline_block) && inline_restored.raw_json.find("metadata") != std::string::npos,"Inline review/opaque metadata was not preserved");
        inline_restored.raw_json = R"({"answers":["a  b","<<"],"correct":false})";
        require(!response_reviewed(inline_restored,inline_block),"Editing one inline hole reused previous incorrect feedback");
        Response drag_review; drag_review.raw_json = R"({"slots":[-1,0],"correct":false})";
        mark_response_reviewed(drag_review,drag_block);
        require(drag_review.reviewed_answer_identity == sha256("[-1,0]"),"Drag review identity does not match Android slots-array framing");
        put_response(fill_state,drag_key,drag_review); const auto fill_before = fill_state;
        validate_lesson_state(fill_state,fills); require(fill_state == fill_before,"Partial fill validation changed original data");
        fill_state.preferences["learning_progress"][inline_key] = std::string(R"({"answers":["a  b","not_eq"],"correct":true,"opaque":{"original":"metadata"}})");
        fill_state.preferences["learning_progress"][drag_key] = std::string(R"({"slots":[1,0],"correct":true})");
        validate_lesson_state(fill_state,fills);
        auto invalid_fill = fill_state; invalid_fill.preferences["learning_progress"][inline_key] = std::string(R"({"answers":["a b","not_eq"],"correct":true})");
        rejects([&] { validate_lesson_state(invalid_fill,fills); });
        for (const auto malformed : {R"({"slots":[0,0],"correct":false})", R"({"slots":[-2,1],"correct":false})", R"({"slots":[3,1],"correct":false})", R"({"slots":[-1,0],"correct":true})", R"({"slots":[0.5,1],"correct":false})"}) {
            invalid_fill = fill_state; invalid_fill.preferences["learning_progress"][drag_key] = std::string(malformed);
            const auto preserved = invalid_fill; rejects([&] { validate_lesson_state(invalid_fill,fills); });
            require(invalid_fill == preserved,"Invalid imported physical card data was silently rewritten");
        }
        ++groups;

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

        StateData navigation_only;
        navigation_only.preferences["native_windows"]["catalog/route"] = std::string("programming");
        navigation_only.preferences["native_windows"]["catalog/course"] = std::string("cpp_fundamentals");
        navigation_only.preferences["native_windows"]["catalog/v1/es/programming/cpp_fundamentals///selection"] = std::int64_t{4};
        navigation_only.preferences["native_windows"]["ui_language"] = std::string("fr");
        navigation_only.preferences["native_windows"]["content_language"] = std::string("es");
        require(can_import_android_profile({}) && can_import_android_profile(navigation_only), "Untouched/catalog-only profile cannot import");
        for (const auto domain : {"learning_progress","cpp_lesson_drafts","narration_settings","unknown"}) {
            auto real_data = navigation_only; real_data.preferences[domain]["preserve"] = std::string("user data");
            require(!can_import_android_profile(real_data), "Import eligibility silently permits a non-UI domain");
            const auto target = base / (std::string("blocked-ui-") + domain);
            StateStore seed(target); (void)seed.load(); seed.save(real_data);
            const auto before = sha256(read_bytes(target / "state/active/state.bin"));
            rejects([&] { commit_android_import(preview,target); });
            require(sha256(read_bytes(target / "state/active/state.bin")) == before && seed.load() == real_data,
                    "Refusing a non-UI domain changed previous student data");
        }
        auto unknown_ui = navigation_only; unknown_ui.preferences["native_windows"]["reference-proof/v1/es/programming/test/0/0"] = std::string("not UI");
        require(!can_import_android_profile(unknown_ui), "An unknown native key qualified as navigation-only"); ++groups;

        const auto navigated_target = base / "navigated-import-profile";
        StateStore navigated_seed(navigated_target); (void)navigated_seed.load(); navigated_seed.save(navigation_only);
        commit_android_import(preview,navigated_target);
        auto merged_expected = preview.data; merged_expected.preferences["native_windows"] = navigation_only.preferences.at("native_windows");
        StateStore navigated_store(navigated_target);
        require(navigated_store.load() == merged_expected, "Import after browsing lost UI preferences or changed imported progress/drafts");
        for (const auto& file : preview.files)
            require(sha256(read_bytes(navigated_target / "state/active/android" / file.relative_path)) == file.sha256,
                    "Import after browsing changed archived Android bytes");
        require(sha256(read_bytes(source / "shared_prefs/learning_progress.xml")) == xml_before, "Navigated import changed Android source");
        // Recreate the precise on-disk state of a crash between publishing the
        // verified archive and replacing state.bin. No student data is used.
        const auto archived_file = navigated_target / "state/active/android/files/cpp_projects/saved-source.cpp";
        const auto archived_time = std::filesystem::last_write_time(archived_file);
        navigated_store.save(navigation_only);
        commit_android_import(preview,navigated_target);
        require(navigated_store.load() == merged_expected && std::filesystem::last_write_time(archived_file) == archived_time,
                "Interrupted import did not reuse its exact verified archive or restore UI/progress atomically");
        rejects([&] { commit_android_import(preview,navigated_target); });
        require(navigated_store.load() == merged_expected, "A repeated navigated import replaced real imported state"); ++groups;

        const auto foreign_target = base / "navigated-foreign-file";
        StateStore foreign_store(foreign_target); (void)foreign_store.load(); foreign_store.save(navigation_only);
        text_file(foreign_target / "state/active/personal.txt","unrelated user file");
        rejects([&] { commit_android_import(preview,foreign_target); });
        require(foreign_store.load() == navigation_only && sha256(read_bytes(foreign_target / "state/active/personal.txt")) == sha256("unrelated user file"),
                "Import replaced an unknown file in a navigation-only profile"); ++groups;

        const auto modified_export = base / "navigated-changed-source"; fixture(modified_export);
        const auto modified_preview = inspect_android_export(modified_export);
        text_file(modified_export / "files/cpp_projects/saved-source.cpp","changed after preview");
        const auto changed_ui_target = base / "changed-ui-target";
        StateStore changed_ui_store(changed_ui_target); (void)changed_ui_store.load(); changed_ui_store.save(navigation_only);
        rejects([&] { commit_android_import(modified_preview,changed_ui_target); });
        require(changed_ui_store.load() == navigation_only && !std::filesystem::exists(changed_ui_target / "state/active/android"),
                "Changed source partially published over previous UI preferences"); ++groups;

        // An archive that changed after an interrupted transaction is not ours
        // to clean or overwrite, even though its old marker remains present.
        navigated_store.save(navigation_only);
        text_file(archived_file,"later external edit");
        rejects([&] { commit_android_import(preview,navigated_target); });
        require(navigated_store.load() == navigation_only && sha256(read_bytes(archived_file)) == sha256("later external edit"),
                "Retry discarded a changed archive or replaced prior navigation state"); ++groups;

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

        const auto real_lesson = lgaed::test::load_pre_retirement_lesson("lesson_primer_programa_y_salida.json");
        retirement_state_tests(real_lesson,base);
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

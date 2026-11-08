#include "core/lesson_state.hpp"
#include "core/lesson.hpp"
#include "core/catalog.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/compiler_revision_test_fixture.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/math_label_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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
    using namespace winrt::Windows::Data::Json;
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
    original.preferences["unrelated_preferences"]["enabled"] = true;
    original.preferences["unrelated_preferences"]["timestamp"] = std::int64_t{1792536389797};
    original.preferences["unrelated_preferences"]["rate"] = 2.5;
    original.preferences["unrelated_preferences"]["labels"] = std::vector<std::string>{"primera", " \\t segunda \\n"};
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
    const std::set<std::string> expected_retired{"ejecucion_original_consigna","ejecucion_original_editor","block_0005_0002","block_0005_0008"};
    std::set<std::string> actual_retired;for(const auto& block:integrated.retired_blocks)actual_retired.insert(block.stable_id);
    require(integrated.compiler_puzzles_required&&actual_retired==expected_retired,
        "The active Spanish asset lost its two retired executions or its two conserved reference programs");
    auto current=reopened;validate_lesson_state(current,integrated);prepare_lesson_state(current,integrated);
    const auto prefix="lesson_"+integrated.id;
    const auto retired_pin=prefix+"_state_retired_v2_es",history_pin=prefix+"_state_retirement_history_v2_es",order_pin=prefix+"_state_order_refinements_v1_es";
    const std::set<std::string> provenance_keys{retired_pin,history_pin,order_pin};
    // The published asset now also archives the two comparison programs and
    // pins the line refinement. Only this audited append-only provenance can
    // differ: every answer, draft, preference, bookmark and opaque value stays.
    for(const auto& [domain,values]:reopened.preferences)for(const auto& [key,value]:values){
        const auto retained=find(current,domain,key);require(retained,"Opening the integrated asset removed an existing preference");
        if(domain=="learning_progress"&&(key==retired_pin||key==history_pin)){
            const auto previous=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(value)));
            const auto appended=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*retained)));
            require(appended.Size()>=previous.Size(),"Integrated retirement removed historical provenance");
            for(std::uint32_t i=0;i<previous.Size();++i)require(previous.GetAt(i).Stringify()==appended.GetAt(i).Stringify(),
                "Integrated retirement rewrote an earlier provenance record");
        }else require(*retained==value,"Opening the integrated asset changed an existing answer, draft or preference");
    }
    for(const auto& [domain,values]:current.preferences)for(const auto& [key,value]:values)if(!find(reopened,domain,key))
        require(domain=="learning_progress"&&provenance_keys.contains(key),"Integrated retirement added unrelated state");
    const auto recorded_retired=JsonArray::Parse(winrt::to_hstring(string_value(current,"learning_progress",retired_pin)));
    require(recorded_retired.Size()==expected_retired.size(),"Integrated retirement did not preserve exactly the four reviewed archives");
    for(std::uint32_t i=0;i<recorded_retired.Size();++i){const auto row=recorded_retired.GetAt(i).GetArray();const auto& record=integrated.state_layout.retired.at(i);
        require(row.Size()==4&&winrt::to_string(row.GetStringAt(0))==record.id&&winrt::to_string(row.GetStringAt(1))==record.page&&
            winrt::to_string(row.GetStringAt(2))==record.next&&winrt::to_string(row.GetStringAt(3))==record.sha256,
            "An archived block lost its exact owner, successor or source fingerprint");}
    const auto recorded_history=JsonArray::Parse(winrt::to_hstring(string_value(current,"learning_progress",history_pin)));
    require(recorded_history.Size()==integrated.state_layout.retirement_history.size(),"Integrated retirement omitted its published reading provenance");
    for(std::uint32_t i=0;i<recorded_history.Size();++i)require(winrt::to_string(recorded_history.GetStringAt(i))==integrated.state_layout.retirement_history[i].sha256,
        "Integrated retirement changed the source of an archived reading presentation");
    const auto recorded_order=JsonArray::Parse(winrt::to_hstring(string_value(current,"learning_progress",order_pin)));
    require(recorded_order.Size()==1&&integrated.state_layout.order_refinements.size()==1,"The current order needs exactly its one published provenance record");
    const auto order_record=recorded_order.GetAt(0).GetArray();const auto& refinement=integrated.state_layout.order_refinements.front();
    require(order_record.Size()==3&&winrt::to_string(order_record.GetStringAt(0))=="block_0003_0002"&&
        winrt::to_string(order_record.GetStringAt(1))==refinement.sha256&&winrt::to_string(order_record.GetStringAt(2))==refinement.representation_signature,
        "The integrated order pin does not match its exact published source and line representation");
    store.save(current);auto integrated_reopened=store.load();validate_lesson_state(integrated_reopened,integrated);prepare_lesson_state(integrated_reopened,integrated);
    require(integrated_reopened==current,"Integrated retirement/refinement was not idempotent after persistence/reopening");
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
        auto changed_language = current; prepare_lesson_state(changed_language,translated);
        require(changed_language == current,"Changing to an untouched translation rewrote Spanish retirement/refinement state");
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
void reviewed_order_equivalences_preserve_history(const std::filesystem::path& base){
    using namespace lgaed::curriculum;
    struct Case{const char* file;const char* identity;std::vector<std::int64_t> alternative;};
    const std::vector<Case> cases{
        {"objetos_y_estado/lesson_representar_procesar_y_recuperar.json","block_0002_0001",{3,2,4,5,8,0,1,6,7}},
        {"colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json","block_0005_0004",{3,2,4,7,6,5,0,8,1,9,10}},
        {"funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json","orden_validar_antes_de_calcular",{3,2,4,5,6,7,0,1}},
        {"excepciones_y_recuperacion/lesson_recuperar_por_operacion.json","block_0002_0003",{4,3,5,7,6,8,9,10,11,12,13,18,17,0,1,2,14,15,16}},
        {"archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json","block_0004_0003",{4,3,2,5,6,7,8,9,0,1}}
    };
    const auto content=std::filesystem::path(lgaed::build::source_root)/"app/src/main/assets/content";
    for(std::size_t index=0;index<cases.size();++index){const auto& test=cases[index];
        const auto lesson=lgaed::windows::load_lesson(content/"es/programming/cpp_fundamentals"/test.file);
        const auto position=stable_block_position(lesson,test.identity);require(position.has_value(),"A reviewed order lost its published identity");
        const auto& block=lesson.pages[position->page].blocks[position->block];
        require(!block.order_equivalence_groups.empty(),"The current reviewed order has no approved equivalence groups");
        auto previous=lesson;auto& previous_block=previous.pages[position->page].blocks[position->block];previous_block.order_equivalence_groups.clear();
        std::vector<std::size_t> alternative(test.alternative.begin(),test.alternative.end());
        require(evaluate_order(block,alternative).correct&&!evaluate_order(previous_block,alternative).correct,
            "The independent equivalent order did not reproduce the previous rejection and current acceptance");
        require(exercise_key(previous,previous_block)==exercise_key(lesson,block)&&
            response_storage_key(previous,previous_block)==response_storage_key(lesson,block)&&
            order_representation_frame(previous_block)==order_representation_frame(block),
            "An acceptance-only order revision changed its historical identity, storage or line representation");
        StateData empty;require(!get_response(empty,lesson,block)&&empty.preferences.empty(),"Enabling reviewed equivalents invented an answer in an empty profile");
        for(const auto mode:{"draft","wrong","canonical"}){
            StateData data;prepare_lesson_state(data,previous);
            data.preferences["unrelated_order_preferences"]["retained"]=std::string(" \t untouched \r\n");
            Response old;old.raw_json=R"({"incorrectAttempts":4,"opaque":{"selection":17}})";
            old.order=test.alternative;old.correct=std::string_view(mode)=="canonical";
            if(old.correct)old.order.assign(block.correct_order.begin(),block.correct_order.end());
            if(std::string_view(mode)=="wrong")mark_response_reviewed(old,previous_block);
            put_response(data,previous,previous_block,old);
            const auto storage=response_storage_key(previous,previous_block);
            const auto raw=string_value(data,"learning_progress",storage);const auto before=data;
            const auto restored=get_response(data,lesson,block);
            require(restored&&restored->order==old.order&&restored->correct==old.correct&&
                response_reviewed(*restored,block)==(std::string_view(mode)=="wrong")&&data==before,
                "Enabling an equivalent order silently regraded an old draft or incorrect reviewed answer");
            validate_lesson_state(data,lesson);prepare_lesson_state(data,lesson);
            require(data==before,"Reviewed order equivalents rewrote an existing preference or provenance pin");
            const auto name=std::to_string(index)+'-'+mode;
            StateStore store(base/("reviewed-order-"+name));require(store.load().preferences.empty(),"The reviewed-order QA profile must begin empty");
            store.save(data);auto reopened=store.load();
            validate_lesson_state(reopened,lesson);require(reopened==before,"An old reviewed order changed after normal persistence/reopening");
            const auto exported=base/("reviewed-order-export-"+name);export_preferences(exported,data);
            const auto source_hash=sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"));
            const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,lesson);
            const auto imported_response=get_response(preview.data,lesson,block);
            require(imported_response&&imported_response->order==old.order&&imported_response->correct==old.correct&&
                response_reviewed(*imported_response,block)==(std::string_view(mode)=="wrong")&&
                string_value(preview.data,"learning_progress",storage)==raw,
                "Importing a prior line response promoted its old verdict or changed its serialized history");
            commit_android_import(preview,base/("reviewed-order-import-"+name));
            StateStore imported_store(base/("reviewed-order-import-"+name));const auto imported=imported_store.load();
            require(string_value(imported,"learning_progress",storage)==raw&&
                sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"))==source_hash,
                "Committed order import changed the original export or historical response");
            auto checked=*restored;checked.order=test.alternative;checked.correct=true;clear_response_review(checked);
            put_response(reopened,lesson,block,checked);validate_lesson_state(reopened,lesson);
            require(get_response(reopened,lesson,block)->correct&&get_response(reopened,lesson,block)->order==test.alternative&&
                get_response(reopened,lesson,block)->raw_json.find("\"incorrectAttempts\":4")!=std::string::npos&&
                get_response(reopened,lesson,block)->raw_json.find("\"selection\":17")!=std::string::npos,
                "Explicitly checking an equivalent order lost its achievement or opaque attempt history");
            auto old_policy=reopened;rejects([&]{validate_lesson_state(old_policy,previous);});
            require(old_policy==reopened,"Refusing a new equivalent achievement under the old evaluator modified its data");
            store.save(reopened);const auto correct=store.load();validate_lesson_state(correct,lesson);
            require(correct==reopened,"A newly accepted equivalent order failed normal persistence/reopening");
            for(const auto locale:{"en","fr"}){
                const auto translated=lgaed::windows::load_lesson(content/locale/"programming/cpp_fundamentals"/test.file,locale);
                require(translated.id==lesson.id&&translated.locale==locale,"The untouched translation belongs to another lesson or locale");
                // Spanish additions need not exist in an untranslated asset.
                // Every translated activity must retain its own exact policy.
                for(const auto& page:translated.pages)for(const auto& translated_block:page.blocks)
                    require(translated_block.order_equivalence_groups.empty(),"A Spanish reviewed allowance was transferred to an untouched translation");
                auto switched=correct;prepare_lesson_state(switched,translated);
                require(switched==correct&&string_value(switched,"learning_progress",storage)==string_value(correct,"learning_progress",storage),
                    "Opening an untouched translation rewrote a Spanish line response or its provenance");
            }
            ++groups;
        }
    }
    std::cout<<"PASS five reviewed order equivalents: unchanged keys/provenance, empty/draft/wrong/canonical history, explicit new success, native reopen, Android import and untouched translations\n";
}
void reviewed_choice_revisions_preserve_responses(const std::filesystem::path& base,bool consult_only=false) {
    using namespace lgaed::curriculum;
    const auto content=std::filesystem::path(lgaed::build::source_root)/"app/src/main/assets/content";
    require(lgaed::test::choice_revision_cases().size()==8,"The reviewed choice inventory must include the eight closed allowances");
    std::size_t reviewed=0;
    for(std::size_t index=0;index<lgaed::test::choice_revision_cases().size();++index) {
        const auto& plan=lgaed::test::choice_revision_cases()[index];
        const bool consult=std::string_view(plan.filename)=="lesson_una_tabla_persistente_de_nombres.json"&&std::string_view(plan.identity)=="block_0000_0003";
        if(consult_only&&!consult)continue;++reviewed;
        const auto integrated=lgaed::windows::load_lesson(content/"es/programming/cpp_fundamentals/texto_interpretacion_y_flujos"/plan.filename);
        const auto before=lgaed::test::choice_revision_before(integrated,plan.identity);
        const auto current=lgaed::test::revise_choice_lesson(before,plan.identity);const auto position=stable_block_position(current,plan.identity);
        require(position.has_value(),"A reviewed choice lost its published identity");
        const auto& block=current.pages[position->page].blocks[position->block];const auto& old_block=before.pages[position->page].blocks[position->block];
        const auto original_key=exercise_key(before,old_block),sidecar=response_storage_key(current,block);
        require(original_key==exercise_key(current,block)&&original_key!=sidecar&&sidecar.starts_with("choice-options/v1/es/"),
            "Choice revision reused its historical response slot or changed its original key");
        StateData empty;require(!get_response(empty,current,block)&&empty.preferences.empty(),"An empty profile acquired an invented historical choice");
        for(const auto mode:{"draft","wrong","correct"}) {
            StateData data;prepare_lesson_state(data,before);
            data.preferences["choice_opaque"]["keep"]=std::string(" \t preserved \r\n");
            std::string personal_draft_key;
            if(consult){
                require(position->page==0&&position->block==3&&plan.option==0&&old_block.correct_index==1,
                    "The U11L18 consultation correction moved outside its exact published owner");
                const auto editor_at=stable_block_position(before,"block_0005_0002");require(editor_at.has_value(),"The U11L18 personal draft owner is missing");
                const auto& editor=before.pages[editor_at->page].blocks[editor_at->block];personal_draft_key=draft_key("programming",before,editor);
                Draft draft;draft.source=editor.initial_code+"\n    // borrador personal conservado\r\n";draft.input=" \t entrada propia\r\n";
                draft.indentation_json=make_indentation_metadata(draft.source,{});draft.selection_start=4;draft.selection_end=11;put_draft(data,personal_draft_key,draft);
                save_current_page(data,before,7);data.preferences["learning_progress"]["lesson_"+before.id+"_started"]=true;
                // These are stored legacy positions only. They are not a claim
                // of played PCM or certificates for the revised option.
                auto history=lgaed::narration::begin({},"consult-storage-page-a","consult-storage-owner-a",{0,117,0,false},"historical unrelated text A","consult-storage-page-a");
                history=lgaed::narration::begin(std::move(history),"consult-storage-page-b","consult-storage-owner-b",{1,239,0,false},"historical unrelated text B","consult-storage-page-b");
                history=lgaed::narration::begin(std::move(history),"consult-storage-page-c","consult-storage-owner-c",{2,317,0,false},old_block.options[0],"consult-storage-page-c");
                lgaed::narration::write_history(data,history);
            }
            Response old;old.raw_json=R"({"incorrectAttempts":4,"opaque":{"source":"original choice"}})";
            old.selected=std::string_view(mode)=="draft"?(consult?static_cast<std::int64_t>(plan.option):-1):static_cast<std::int64_t>(std::string_view(mode)=="correct"?*old_block.correct_index:
                plan.option==*old_block.correct_index?(plan.option+1)%old_block.options.size():plan.option);
            old.correct=std::string_view(mode)=="correct";if(std::string_view(mode)=="wrong")mark_response_reviewed(old,old_block);
            put_response(data,original_key,old);const auto original_raw=string_value(data,"learning_progress",original_key);const auto untouched=data;
            const auto restored=get_response(data,current,block);
            require(restored&&restored->historical_choice&&restored->selected==old.selected&&restored->correct==old.correct&&restored->raw_json==original_raw
                &&response_reviewed(*restored,block)==(std::string_view(mode)=="wrong")&&data==untouched,
                "Restoring a prior choice changed its verdict, opaque metadata or provenance");
            require(selected_choice_text(*restored,block)==(old.selected<0?std::string{}:old_block.options[static_cast<std::size_t>(old.selected)])
                &&choice_incorrect_feedback(*restored,block)==incorrect_feedback(old_block,old.selected<0?std::nullopt:std::optional<std::size_t>(static_cast<std::size_t>(old.selected))),
                "An old response was attributed the corrected option or new guidance");
            if(restored->correct)require(choice_correct_feedback(*restored,block)==old_block.explanation,
                "A historical success was attributed the new explanation instead of its original text");
            rejects([&]{put_response(data,current,block,*restored);});require(data==untouched,"Rejecting an implicit historical-to-current write changed data");
            validate_lesson_state(data,current);prepare_lesson_state(data,current);const auto adopted=data;
            const auto pin="lesson_"+current.id+"_state_choice_revisions_v1_es";
            require(find(data,"learning_progress",pin)&&string_value(data,"learning_progress",original_key)==original_raw,"Choice provenance pin replaced original response metadata");
            auto prior_pin=data;
            if(const auto value=find(untouched,"learning_progress",pin))prior_pin.preferences["learning_progress"][pin]=*value;
            else prior_pin.preferences["learning_progress"].erase(pin);
            require(prior_pin==untouched,"Adopting a choice revision rewrote unrelated preferences or historical state");
            prepare_lesson_state(data,current);require(data==adopted,"Choice revision adoption is not idempotent");
            auto rollback=data;rejects([&]{prepare_lesson_state(rollback,before);});require(rollback==data,"Rejecting a removed revision changed saved data");
            const auto name=std::to_string(index)+'-'+mode;StateStore store(base/("choice-"+name));(void)store.load();store.save(data);
            auto reopened=store.load();validate_lesson_state(reopened,current);require(reopened==data,"Historical choice changed during normal persistence/reopening");
            // Other integrated compiler revisions retain Windows-only adoption
            // receipts. They must not be disguised as an Android export.
            if(data.preferences.contains("native_windows")) {
                const auto reserved=base/("choice-native-export-"+name);export_preferences(reserved,data);
                rejects([&]{(void)inspect_android_export(reserved);});
            }
            auto android=data;android.preferences.erase("native_windows");
            // The consultation fixture checks exact CR/LF/tab preservation.
            // Raw CRLF would be normalized by XML before import can retain it.
            const auto exported=base/("choice-export-"+name);export_preferences(exported,android,consult);const auto source_hash=sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"));
            const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,current);
            require(get_response(preview.data,current,block)->historical_choice&&string_value(preview.data,"learning_progress",original_key)==original_raw,
                "Imported choice lost its original blob or was silently promoted to current text");
            commit_android_import(preview,base/("choice-import-"+name));StateStore imported_store(base/("choice-import-"+name));const auto imported=imported_store.load();
            require(string_value(imported,"learning_progress",original_key)==original_raw&&sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"))==source_hash,
                "Choice import changed original response bytes or its source export");
            if(consult){
                require(imported==android,"The U11L18 import changed typed preferences");
                require(lgaed::narration::read_history(imported)==lgaed::narration::read_history(untouched),
                    "The U11L18 import changed stored narration positions");
                require(current_page(imported,current)==7,"The U11L18 import changed the lesson page bookmark");
                const auto draft=get_draft(imported,personal_draft_key,"");const auto previous_draft=get_draft(untouched,personal_draft_key,"");
                require(draft.has_saved&&draft.source==previous_draft.source&&draft.input==previous_draft.input&&draft.indentation_json==previous_draft.indentation_json
                    &&draft.selection_start==previous_draft.selection_start&&draft.selection_end==previous_draft.selection_end,
                    "The U11L18 choice import changed an independent compiler draft, stdin, indentation or selection");
                for(const auto& file:preview.files)require(sha256(read_bytes(exported/file.relative_path))==file.sha256
                    &&sha256(read_bytes(base/("choice-import-"+name)/"state/active/android"/file.relative_path))==file.sha256,
                    "The U11L18 choice import changed its original XML export or archived bytes");
            }
            Response selected;selected.selected=static_cast<std::int64_t>(plan.option);selected.correct=evaluate_choice(block,plan.option).correct;
            if(!selected.correct)mark_response_reviewed(selected,block);put_response(reopened,current,block,selected);validate_lesson_state(reopened,current);
            const auto actual=get_response(reopened,current,block);
            require(actual&&!actual->historical_choice&&actual->selected==selected.selected&&actual->correct==selected.correct&&selected_choice_text(*actual,block)==plan.replacement
                &&string_value(reopened,"learning_progress",original_key)==original_raw&&actual->raw_json.find("incorrectAttempts")==std::string::npos,
                "A new choice overwrote or inherited an old attempt's text, verdict or opaque metadata");
            if(plan.feedback)require(choice_incorrect_feedback(*actual,block)==plan.feedback,"The revised incorrect choice lost its own exact guidance");
            if(actual->correct)require(choice_correct_feedback(*actual,block)==block.explanation,
                "A newly checked choice retained its historical success explanation");
            store.save(reopened);const auto saved=store.load();require(saved==reopened&&!get_response(saved,current,block)->historical_choice,"New choice provenance failed normal reopening");
            Response reset;put_response(reopened,current,block,reset);require(get_response(reopened,current,block)->selected==-1&&!get_response(reopened,current,block)->correct
                &&!get_response(reopened,current,block)->historical_choice&&string_value(reopened,"learning_progress",original_key)==original_raw,
                "Explicit reset revived an old achievement or erased the archived choice");
            for(const auto corrupt:{"index","correct","original","pin"}) {
                auto invalid=saved;Response wrong;wrong.selected=static_cast<std::int64_t>(block.options.size());
                if(std::string_view(corrupt)=="correct"){wrong.selected=static_cast<std::int64_t>((*block.correct_index+1)%block.options.size());wrong.correct=true;}
                if(std::string_view(corrupt)=="pin")invalid.preferences["learning_progress"][pin]=std::string("[\"invalid\"]");
                else put_response(invalid,std::string_view(corrupt)=="original"?original_key:sidecar,wrong);
                const auto preserved=invalid;
                rejects([&]{validate_lesson_state(invalid,current);});require(invalid==preserved,"Rejecting corrupt choice provenance altered saved state");
            }
            ++groups;
        }
    }
    require(reviewed==(consult_only?1u:lgaed::test::choice_revision_cases().size()),"A reviewed choice case was skipped");
    std::cout<<"PASS "<<reviewed<<" reviewed choice revisions: old draft/rejection/success text and explanations, opaque history, separate current selection/reset, append-only pins, native reopen and Android import; U11L18 also preserves its independent personal draft, typed progress and three storage-only bookmarks, without claiming synthesis or listening\n";
}
void reviewed_compiler_expansions_preserve_execution_history(const std::filesystem::path& base) {
    using namespace lgaed;using namespace curriculum;using namespace windows;
    using namespace winrt::Windows::Data::Json;
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    for(std::size_t index=0;index<test::compiler_revision_cases().size();++index) {
        const auto& plan=test::compiler_revision_cases()[index];
        const auto integrated=load_lesson(folder/plan.filename);
        const auto before=test::compiler_revision_before(integrated,plan.identity);
        const auto current=test::revise_compiler_lesson(before,plan.identity);const auto position=stable_block_position(current,plan.identity);
        require(position.has_value(),"The expanded compiler lost its historical identity");
        const auto& old=before.pages[position->page].blocks[position->block];const auto& block=current.pages[position->page].blocks[position->block];
        for(unsigned mode=0;mode<3;++mode) {
            StateData data;prepare_lesson_state(data,before);data.preferences["opaque_compiler"]["keep"]=std::string(" \t\r\n retained");
            CppRunRequest captured;captured.revision=7;captured.source_utf8=old.initial_code+"\n// QA captured receipt program\n";
            Draft draft;draft.source=captured.source_utf8;draft.input=old.standard_input.value_or("");put_draft(data,draft_key("programming",before,old),draft);
            const auto seeded=mode==2?old.output_check->cases.size():mode==1?old.output_check->cases.size()/2:0;
            for(std::size_t i=0;i<seeded;++i) {
                const auto& item=old.output_check->cases[i];captured.stdin_utf8=item.standard_input;
                FreshExecution execution{7,true,true,0,item.standard_input,item.outputs.front()};
                (void)record_compiler_puzzle_case(data,before,old,captured,execution,"Correcto histórico.");
            }
            const auto original=data;prepare_lesson_state(data,current);validate_lesson_state(data,current);
            const auto old_base="compiler-puzzle/v1/"+before.locale+'/'+before.id+'/'+exercise_key(before,old)+'/'+sha256(reference_objective_frame(before,old))+'/';
            for(const auto damage:{"program","proof","seal"}) {
                if(!seeded&&(std::string_view(damage)=="proof"||std::string_view(damage)=="seal"))continue;
                if(std::string_view(damage)=="seal"&&mode!=2)continue;
                auto malformed=original;
                if(std::string_view(damage)=="program")malformed.preferences["native_windows"][old_base+"program"]=std::int64_t{42};
                if(std::string_view(damage)=="proof") {
                    auto proofs=std::get<std::vector<std::string>>(*find(malformed,"native_windows",old_base+"cases"));proofs[0]="forged";
                    malformed.preferences["native_windows"][old_base+"cases"]=std::move(proofs);
                }
                if(std::string_view(damage)=="seal")malformed.preferences["native_windows"][old_base+"completed"]=std::string("forged");
                const auto preserved=malformed;rejects([&]{prepare_lesson_state(malformed,current);});require(malformed==preserved,"Rejecting a corrupt inherited compiler receipt partially adopted its state");
            }
            // Once Work integrates both L15 editors, restoring only this
            // editor's previous contract retains the other published row.
            // Adoption must append exactly the independently pinned new row;
            // every old row remains byte-identical and in its original order.
            const auto pin="lesson_"+current.id+"_state_compiler_revisions_v1_es";
            auto expected_pin=find(original,"learning_progress",pin)
                ?JsonArray::Parse(winrt::to_hstring(string_value(original,"learning_progress",pin))):JsonArray{};
            JsonArray expected_row;
            for(const auto field:{plan.identity,plan.before_full,plan.after_full})expected_row.Append(JsonValue::CreateStringValue(winrt::to_hstring(field)));
            if(plan.reference_identity)for(const auto field:{plan.reference_identity,plan.reference_before,plan.reference_after})
                expected_row.Append(JsonValue::CreateStringValue(winrt::to_hstring(field)));
            expected_pin.Append(expected_row);const auto expected_pin_text=winrt::to_string(expected_pin.Stringify());
            require(string_value(data,"learning_progress",pin)==expected_pin_text,
                "Compiler adoption changed a previous provenance row or did not append exactly its independently pinned compiler/reference revision");
            for(const auto& [domain,preferences]:original.preferences)for(const auto& [name,value]:preferences){
                const auto actual=find(data,domain,name);
                const bool exact_append=domain=="learning_progress"&&name==pin;
                const bool preserved=exact_append?actual&&std::holds_alternative<std::string>(*actual)
                    &&std::get<std::string>(*actual)==expected_pin_text:actual&&*actual==value;
                if(!preserved){
                    std::cerr<<"Compiler adoption mismatch: index="<<index<<" identity="<<plan.identity<<" mode="<<mode
                        <<" domain="<<domain<<" key="<<name<<" expected-type="<<value.index()<<" actual-type=";
                    if(actual)std::cerr<<actual->index();else std::cerr<<"absent";
                    if(const auto text=std::get_if<std::string>(&value))std::cerr<<" expected="<<*text;
                    if(actual)if(const auto text=std::get_if<std::string>(actual))std::cerr<<" actual="<<*text;
                    std::cerr<<'\n';
                }
                require(preserved,"Adopting a compiler expansion rewrote historical or unrelated data");
            }
            require(!compiler_puzzle_completed(data,current,block)&&compiler_puzzle_historical_case_count(data,current,block)==seeded&&
                compiler_puzzle_historical_completed(data,current,block)==(seeded==old.output_check->cases.size()),
                "Historical cases became current completion or lost their provenance");
            require(compiler_puzzle_case_count(data,current,block)==(plan.suffix?0:seeded),"The expansion reused changed expected output or discarded unchanged valid cases");
            if(!plan.suffix&&mode==2) {
                auto inherited=data,changed_program=data;auto different=captured;different.source_utf8+="// different captured program\n";
                for(std::size_t extra=old.output_check->cases.size();extra<block.output_check->cases.size();++extra) {
                    const auto& item=block.output_check->cases[extra];captured.stdin_utf8=item.standard_input;different.stdin_utf8=item.standard_input;
                    FreshExecution execution{7,true,true,0,item.standard_input,item.outputs.front()};
                    (void)record_compiler_puzzle_case(inherited,current,block,captured,execution,"Correcto ampliado.");
                    (void)record_compiler_puzzle_case(changed_program,current,block,different,execution,"Only new contrasts.");
                }
                require(compiler_puzzle_completed(inherited,current,block)&&compiler_puzzle_case_count(inherited,current,block)==21,
                    "The same captured program had to repeat its nineteen preserved execution cases");
                require(!compiler_puzzle_completed(changed_program,current,block)&&compiler_puzzle_case_count(changed_program,current,block)==2&&
                    compiler_puzzle_historical_completed(changed_program,current,block),"A different program borrowed nineteen historical execution receipts");
            }
            const auto adopted=data;prepare_lesson_state(data,current);require(data==adopted,"Compiler expansion adoption is not idempotent");
            auto rollback=data;rejects([&]{prepare_lesson_state(rollback,before);});require(rollback==data,"Rejecting a compiler downgrade modified progress");
            auto edited=data;auto other=captured;other.source_utf8+="// other program\n";
            invalidate_compiler_puzzle_attempt(edited,current,block,other);invalidate_compiler_puzzle_attempt(edited,current,block,captured);prepare_lesson_state(edited,current);
            require(!compiler_puzzle_case_count(edited,current,block)&&compiler_puzzle_historical_case_count(edited,current,block)==seeded,
                "Edit/Undo resurrected an incomplete inherited receipt or erased its historical program");
            const auto name=std::to_string(index)+'-'+std::to_string(mode);StateStore store(base/("compiler-"+name));(void)store.load();store.save(data);
            auto reopened=store.load();validate_lesson_state(reopened,current);require(reopened==data,"Reopening changed an expanded compiler's historical/current proof");
            const auto reserved_export=base/("compiler-native-export-"+name);export_preferences(reserved_export,data);
            rejects([&]{(void)inspect_android_export(reserved_export);});
            auto android=data;android.preferences.erase("native_windows");
            const auto exported=base/("compiler-export-"+name);export_preferences(exported,android,true);const auto preview=inspect_android_export(exported);
            validate_lesson_state(preview.data,current);commit_android_import(preview,base/("compiler-import-"+name));
            StateStore imported_store(base/("compiler-import-"+name));auto imported=imported_store.load();validate_lesson_state(imported,current);
            for(const auto& [domain,preferences]:android.preferences)for(const auto& [key,value]:preferences)
                require(find(imported,domain,key)&&*find(imported,domain,key)==value,"Android import changed historical learning data, drafts or provenance pins");
            require(!compiler_puzzle_historical_case_count(imported,current,block)&&!compiler_puzzle_case_count(imported,current,block)&&
                get_draft(imported,draft_key("programming",current,block),"").source==captured.source_utf8,
                "Android import manufactured native execution credit or erased the original code draft");
            auto failed=FreshExecution{7,false,false,-1,"","wrong"};
            require(!record_compiler_puzzle_case(reopened,current,block,captured,failed,"Incorrecto")&&compiler_puzzle_historical_case_count(reopened,current,block)==seeded,
                "A technical failure accredited a compiler expansion or erased its historical cases");
            for(const auto& item:block.output_check->cases) {
                captured.stdin_utf8=item.standard_input;
                FreshExecution execution{7,true,true,0,item.standard_input,item.outputs.front()};
                (void)record_compiler_puzzle_case(reopened,current,block,captured,execution,"Correcto ampliado.");
            }
            require(compiler_puzzle_completed(reopened,current,block)&&compiler_puzzle_feedback(reopened,current,block)=="Correcto ampliado."&&
                compiler_puzzle_historical_case_count(reopened,current,block)==seeded,"The fresh complete expanded program lost old credit or failed its new objective");
            invalidate_compiler_puzzle_attempt(reopened,current,block,other);require(compiler_puzzle_completed(reopened,current,block),"Editing revoked a completed expanded objective");
            auto malformed=reopened;malformed.preferences["learning_progress"][pin]=std::string("[\"invalid\"]");
            const auto preserved=malformed;rejects([&]{prepare_lesson_state(malformed,current);});require(malformed==preserved,"Rejecting corrupted compiler provenance changed data");
            reset_compiler_puzzle(reopened,current,block);prepare_lesson_state(reopened,current);
            require(!compiler_puzzle_completed(reopened,current,block)&&!compiler_puzzle_historical_completed(reopened,current,block)&&
                !compiler_puzzle_case_count(reopened,current,block)&&!compiler_puzzle_historical_case_count(reopened,current,block),
                "Explicit Reset revived old compiler credit or retained a completed expansion");
        }
    }
    std::cout<<"PASS ten reviewed compiler expansions: native empty/partial/completed proofs and reopen, unchanged-case reuse, new outputs, Edit/Undo and Reset; Android drafts/pins retained without invented native credit or reserved-domain import\n";
}
void late_compiler_batteries_keep_one_historical_execution_receipt(const std::filesystem::path& base){
    using namespace lgaed;using namespace curriculum;using namespace windows;
    const auto directory=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    std::size_t owners=0;
    for(const auto& plan:test::compiler_revision_cases())if(plan.reports_before&&!plan.initial_insert){
        ++owners;const auto path=directory/plan.filename;const auto bytes=read_bytes(path);
        const auto before=test::compiler_revision_before(load_lesson(path),plan.identity);
        const auto after=test::revise_compiler_lesson(before,plan.identity);
        const auto at=stable_block_position(before,plan.identity),reference=stable_block_position(before,plan.reference_identity);
        require(at&&reference,"A late compiler battery lost its stable editor/reference");
        const auto& original=before.pages[at->page].blocks[at->block];const auto& expanded=after.pages[at->page].blocks[at->block];
        require(original.output_check->cases.size()==1&&expanded.output_check->cases.size()==1,
            "Printed late-battery reports were incorrectly represented as separate receipts");
        const auto& old_goal=original.output_check->cases.front();const auto& new_goal=expanded.output_check->cases.front();
        for(const auto mode:{"empty","draft","complete"}){
            const auto name=std::string(plan.identity)+'-'+std::to_string(owners)+'-'+mode;
            StateData data;prepare_lesson_state(data,before);
            data.preferences["late_compiler_battery_qa"]["opaque"]=std::string(" \t retained metadata\r\n");
            const auto key=draft_key("programming",before,original);
            const bool has_draft=std::string_view(mode)!="empty",completed=std::string_view(mode)=="complete";
            CppRunRequest captured;captured.revision=17;
            captured.source_utf8=completed?before.pages[reference->page].blocks[reference->block].code:
                original.initial_code+"\n// QA partial construction, not executed\n";
            if(has_draft){Draft draft;draft.source=captured.source_utf8;draft.input=" \t personal input\n";put_draft(data,key,draft);}
            Response legacy;legacy.correct=completed;legacy.raw_json=R"({"incorrectAttempts":4,"opaque":{"origin":"stored compiler QA"}})";
            if(has_draft)put_response(data,before,original,legacy);
            // An aggregate native receipt represents the entire original
            // stdout. This QA API fixture does not claim a compiler execution.
            if(completed){FreshExecution execution{captured.revision,true,true,0,old_goal.standard_input,old_goal.outputs.front()};
                require(record_compiler_puzzle_case(data,before,original,captured,execution,"Synthetic aggregate old receipt; no execution"),
                    "Cannot prepare the single historical late-battery receipt");}
            const auto historical=data;const auto old_response=get_response(historical,before,original);
            const auto old_draft=get_draft(historical,key,original.initial_code,original.standard_input.value_or(""));
            prepare_lesson_state(data,after);validate_lesson_state(data,after);
            const auto verify_draft_and_legacy=[&](const StateData& current){
                const auto draft=get_draft(current,key,expanded.initial_code,expanded.standard_input.value_or(""));
                require(draft.source==old_draft.source&&draft.input==old_draft.input&&draft.has_saved==old_draft.has_saved,
                    "A late compiler expansion replaced an empty starter or personalized source/stdin draft");
                const auto response=get_response(current,after,expanded);
                require(response.has_value()==old_response.has_value(),"A late compiler expansion invented or removed legacy progress");
                if(old_response)require(response->raw_json==old_response->raw_json&&response->correct==old_response->correct,
                    "A late compiler expansion rewrote the original achievement/attempt metadata");
                require(current.preferences.at("late_compiler_battery_qa")==historical.preferences.at("late_compiler_battery_qa"),
                    "A late compiler expansion changed unrelated data");
            };
            verify_draft_and_legacy(data);
            require(compiler_puzzle_historical_case_count(data,after,expanded)==(completed?1:0)
                &&compiler_puzzle_historical_completed(data,after,expanded)==completed
                &&compiler_puzzle_case_count(data,after,expanded)==0&&!compiler_puzzle_completed(data,after,expanded),
                "An old aggregate late-battery achievement was counted as printed reports or as the expanded objective");
            StateStore store(base/("late-compiler-native-"+name));(void)store.load();store.save(data);auto reopened=store.load();
            require(reopened==data,"Reopening changed a late compiler expansion's exact adopted state");verify_draft_and_legacy(reopened);
            // Android keeps its original response/draft but has no Windows
            // execution seals. Removing the reserved native domain is explicit.
            auto android=historical;android.preferences.erase("native_windows");
            const auto exported=base/("late-compiler-export-"+name);export_preferences(exported,android,true);
            const auto export_hash=sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"));
            const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,after);verify_draft_and_legacy(preview.data);
            const auto destination=base/("late-compiler-import-"+name);commit_android_import(preview,destination);
            StateStore imported_store(destination);auto imported=imported_store.load();prepare_lesson_state(imported,after);validate_lesson_state(imported,after);
            verify_draft_and_legacy(imported);
            require(!compiler_puzzle_case_count(imported,after,expanded)&&!compiler_puzzle_historical_case_count(imported,after,expanded)
                &&!compiler_puzzle_completed(imported,after,expanded),"Android import manufactured a late-battery native execution receipt");
            require(sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"))==export_hash,"Late-battery import changed its source export");
            FreshExecution incomplete{captured.revision,true,true,0,new_goal.standard_input,old_goal.outputs.front()};
            require(!record_compiler_puzzle_case(reopened,after,expanded,captured,incomplete,"Only the original printed reports")
                &&!compiler_puzzle_case_count(reopened,after,expanded)&&!compiler_puzzle_completed(reopened,after,expanded),
                "The old aggregate stdout incorrectly passed the expanded late-battery objective");
            FreshExecution complete{captured.revision,true,true,0,new_goal.standard_input,new_goal.outputs.front()};
            require(record_compiler_puzzle_case(reopened,after,expanded,captured,complete,"Synthetic aggregate expanded receipt; no execution")
                &&compiler_puzzle_case_count(reopened,after,expanded)==1&&compiler_puzzle_completed(reopened,after,expanded)
                &&compiler_puzzle_historical_case_count(reopened,after,expanded)==(completed?1:0),
                "A full late-battery stdout did not create exactly one new receipt or erased its old provenance");
            reset_compiler_puzzle(reopened,after,expanded);prepare_lesson_state(reopened,after);
            require(!compiler_puzzle_case_count(reopened,after,expanded)&&!compiler_puzzle_historical_case_count(reopened,after,expanded)
                &&!compiler_puzzle_completed(reopened,after,expanded),"Reset resurrected an old late-battery aggregate receipt");
            verify_draft_and_legacy(reopened);++groups;
        }
        require(read_bytes(path)==bytes,"Late compiler-battery QA changed a curricular asset");
    }
    require(owners==2,"Dedicated late-battery conservation did not cover U11L15 and U11L16");
    std::cout<<"PASS U11L15/L16 late compiler batteries: empty starter, personalized draft and old aggregate success; one historical receipt rather than14/9, separate expanded goal, native reopen, explicit Android import without native credit and scoped Reset. All receipts are synthetic QA; no compiler execution or audio claimed\n";
}
void second_u11_compiler_starter_preserves_independent_drafts_and_receipts(const std::filesystem::path& base){
    using namespace lgaed;using namespace curriculum;using namespace windows;
    using namespace winrt::Windows::Data::Json;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto bytes=read_bytes(path);const auto before=test::compiler_revision_before(load_lesson(path),"block_0006_0003");
    const auto after=test::revise_compiler_lesson(before,"block_0006_0003");
    const auto at=stable_block_position(before,"block_0006_0003"),reference=stable_block_position(before,"block_0010_0001"),
        first_at=stable_block_position(before,"block_0002_0002"),first_ref=stable_block_position(before,"block_0009_0001");
    require(at&&reference&&first_at&&first_ref,"The paired U11L15 editor QA lost a stable owner/reference");
    const auto& old=before.pages[at->page].blocks[at->block];const auto& current=after.pages[at->page].blocks[at->block];
    const auto& first=before.pages[first_at->page].blocks[first_at->block];const auto& first_current=after.pages[first_at->page].blocks[first_at->block];
    const auto first_payload=test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(first.raw_json)));
    const auto& first_plan=test::compiler_revision_case(before,"block_0002_0002");
    require(first_payload==first_plan.before_full||first_payload==first_plan.after_full,"The independent first editor lost its complete authenticated payload");
    const auto key=draft_key("programming",before,old),first_key=draft_key("programming",before,first);
    require(old.output_check->cases.size()==1&&current.output_check->cases.size()==1
        &&current.compiler_revision->previous_initial_code==old.initial_code&&current.initial_code!=old.initial_code,
        "The starter-aware revision lost its single historical objective or previous initial source");
    const auto& old_goal=old.output_check->cases.front();const auto& new_goal=current.output_check->cases.front();
    for(const auto mode:{"empty","draft","complete"}){
        const auto name=std::string("u11-second-starter-")+mode;const bool saved=std::string_view(mode)!="empty",success=std::string_view(mode)=="complete";
        StateData data;prepare_lesson_state(data,before);data.preferences["second_starter_qa"]["opaque"]=std::string(" \t unrelated\r\n");
        CppRunRequest first_program;first_program.revision=31;first_program.source_utf8=before.pages[first_ref->page].blocks[first_ref->block].code;
        Draft first_draft;first_draft.source=first_program.source_utf8+"\n// personal first editor draft\n";first_draft.input=" first stdin \t\n";put_draft(data,first_key,first_draft);
        FreshExecution first_execution{31,true,true,0,first.output_check->cases.front().standard_input,first.output_check->cases.front().outputs.front()};
        require(record_compiler_puzzle_case(data,before,first,first_program,first_execution,"Synthetic independent first-editor receipt"),
            "Cannot prepare the independent first editor's aggregate receipt");
        CppRunRequest captured;captured.revision=47;captured.source_utf8=success?before.pages[reference->page].blocks[reference->block].code:
            old.initial_code+"\n// personal second editor draft with four calls\n";
        if(saved){Draft draft;draft.source=captured.source_utf8;draft.input=" second stdin \t\n";put_draft(data,key,draft);
            Response response;response.correct=success;response.raw_json=R"({"incorrectAttempts":3,"opaque":{"owner":"second editor historical QA"}})";put_response(data,before,old,response);}
        if(success){FreshExecution execution{47,true,true,0,old_goal.standard_input,old_goal.outputs.front()};
            require(record_compiler_puzzle_case(data,before,old,captured,execution,"Synthetic original four-report receipt"),
                "Cannot prepare the second editor's one historical aggregate receipt");}
        const auto original=data;const auto old_draft=get_draft(original,key,old.initial_code,old.standard_input.value_or(""));
        const auto old_response=get_response(original,before,old),first_response=get_response(original,before,first);
        const auto verify=[&](const StateData& value,bool native,std::string_view phase){
            const auto check=[&](bool condition,std::string_view message){if(!condition)throw std::runtime_error(name+" / "+std::string(phase)+": "+std::string(message));};
            const auto draft=get_draft(value,key,current.initial_code,current.standard_input.value_or(""));
            check(draft.has_saved==saved&&draft.source==(saved?old_draft.source:current.initial_code)
                &&draft.input==(saved?old_draft.input:current.standard_input.value_or("")),
                "Starter adoption overwrote a personal four-call draft or denied the new five-call initial to an empty editor");
            const auto prior=get_response(value,after,current);check(prior.has_value()==old_response.has_value(),"Starter adoption invented or discarded legacy achievement metadata");
            if(prior)check(prior->raw_json==old_response->raw_json&&prior->correct==old_response->correct,"Starter adoption changed the historical response/attempt payload");
            const auto first_saved=get_draft(value,first_key,first_current.initial_code,first_current.standard_input.value_or(""));
            check(first_saved.has_saved,"The second compiler revision removed the first editor's saved-draft flag");
            check(first_saved.source==first_draft.source,"The second compiler revision changed the first editor's exact personal source");
            check(first_saved.input==first_draft.input,"The second compiler revision changed the first editor's exact stdin");
            // The QA transition Stringify serializes the surrounding lesson;
            // JSON whitespace is independent of its complete pinned payload.
            check(test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(first_current.raw_json)))==first_payload,
                "The second compiler revision changed the first editor's complete authenticated payload");
            const auto first_prior=get_response(value,after,first_current);
            check(first_prior.has_value()==first_response.has_value(),"The second compiler revision invented or removed the first editor's response");
            if(first_prior)check(first_prior->raw_json==first_response->raw_json&&first_prior->correct==first_response->correct,
                "The second compiler revision changed the first editor's exact response/attempt metadata");
            check(compiler_puzzle_completed(value,after,first_current)==native,"The second compiler revision/reset changed first-editor completion or Android import manufactured it");
            check(compiler_puzzle_case_count(value,after,first_current)==(native?1:0),"The second compiler revision/reset changed the first editor's receipt count");
            check(!native||compiler_puzzle_feedback(value,after,first_current)=="Synthetic independent first-editor receipt",
                "The second compiler revision/reset changed the first editor's exact saved feedback");
            check(value.preferences.at("second_starter_qa")==original.preferences.at("second_starter_qa"),"Starter adoption changed unrelated preferences");
        };
        prepare_lesson_state(data,after);validate_lesson_state(data,after);verify(data,true,"adopt");
        // The independent first editor can already have its expanded contract.
        // Its entire pinned row remains unchanged; only this closed second
        // compiler/reference pair is appended to the same provenance array.
        const auto pin="lesson_"+after.id+"_state_compiler_revisions_v1_es";
        auto expected_pin=find(original,"learning_progress",pin)
            ?JsonArray::Parse(winrt::to_hstring(string_value(original,"learning_progress",pin))):JsonArray{};
        const auto& starter_plan=test::compiler_revision_case(after,"block_0006_0003");JsonArray expected_row;
        for(const auto field:{starter_plan.identity,starter_plan.before_full,starter_plan.after_full,
            starter_plan.reference_identity,starter_plan.reference_before,starter_plan.reference_after})
            expected_row.Append(JsonValue::CreateStringValue(winrt::to_hstring(field)));
        expected_pin.Append(expected_row);const auto expected_pin_text=winrt::to_string(expected_pin.Stringify());
        require(string_value(data,"learning_progress",pin)==expected_pin_text,
            "Starter adoption changed the first editor's provenance or appended a compiler/reference pair outside its independent pins");
        for(const auto& [domain,preferences]:original.preferences)for(const auto& [entry_name,value]:preferences){
            const auto actual=find(data,domain,entry_name);
            const bool exact_append=domain=="learning_progress"&&entry_name==pin;
            const bool preserved=exact_append?actual&&std::holds_alternative<std::string>(*actual)
                &&std::get<std::string>(*actual)==expected_pin_text:actual&&*actual==value;
            if(!preserved){
                std::cerr<<"Starter adoption mismatch: mode="<<mode<<" domain="<<domain<<" key="<<entry_name
                    <<" expected-type="<<value.index()<<" actual-type=";
                if(actual)std::cerr<<actual->index();else std::cerr<<"absent";
                if(const auto text=std::get_if<std::string>(&value))std::cerr<<" expected="<<*text;
                if(actual)if(const auto text=std::get_if<std::string>(actual))std::cerr<<" actual="<<*text;
                std::cerr<<'\n';
            }
            require(preserved,"Starter adoption rewrote an authenticated old receipt or unrelated historical byte");
        }
        require(compiler_puzzle_historical_case_count(data,after,current)==(success?1:0)
            &&compiler_puzzle_historical_completed(data,after,current)==success
            &&(!success||compiler_puzzle_historical_feedback(data,after,current)=="Synthetic original four-report receipt")
            &&!compiler_puzzle_case_count(data,after,current)&&!compiler_puzzle_completed(data,after,current),
            "Four old reports lost their one aggregate receipt or silently became a fifth-report achievement");
        const auto adopted=data;prepare_lesson_state(data,after);require(data==adopted,"Starter-aware compiler adoption is not idempotent");
        StateStore store(base/name);(void)store.load();store.save(data);auto reopened=store.load();validate_lesson_state(reopened,after);
        require(reopened==data,"Reopening changed starter-aware adopted state");verify(reopened,true,"reopen");
        auto android=original;android.preferences.erase("native_windows");const auto exported=base/(name+"-export");export_preferences(exported,android,true);
        const auto source_hash=sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"));
        const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,after);verify(preview.data,false,"import preview");
        const auto destination=base/(name+"-import");commit_android_import(preview,destination);StateStore import_store(destination);auto imported=import_store.load();
        prepare_lesson_state(imported,after);validate_lesson_state(imported,after);verify(imported,false,"import reopen");
        require(!compiler_puzzle_case_count(imported,after,current)&&!compiler_puzzle_historical_case_count(imported,after,current)
            &&!compiler_puzzle_completed(imported,after,current)&&sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"))==source_hash,
            "Starter-aware Android import manufactured receipts or changed its source export");
        FreshExecution incomplete{47,true,true,0,new_goal.standard_input,old_goal.outputs.front()};
        require(!record_compiler_puzzle_case(reopened,after,current,captured,incomplete,"Only four reports")&&!compiler_puzzle_completed(reopened,after,current),
            "The second editor accepted its original four-report stdout as the expanded five-report objective");
        FreshExecution complete{47,true,true,0,new_goal.standard_input,new_goal.outputs.front()};
        require(record_compiler_puzzle_case(reopened,after,current,captured,complete,"Synthetic five-report receipt")
            &&compiler_puzzle_case_count(reopened,after,current)==1&&compiler_puzzle_historical_case_count(reopened,after,current)==(success?1:0),
            "The second editor did not preserve distinct one-receipt old/new aggregate objectives");
        auto reset_first=reopened;reset_compiler_puzzle(reset_first,after,first_current);prepare_lesson_state(reset_first,after);
        require(!compiler_puzzle_completed(reset_first,after,first_current)&&compiler_puzzle_completed(reset_first,after,current)
            &&compiler_puzzle_case_count(reset_first,after,current)==1&&compiler_puzzle_historical_case_count(reset_first,after,current)==(success?1:0)
            &&get_draft(reset_first,key,current.initial_code,current.standard_input.value_or("")).source==(saved?old_draft.source:current.initial_code),
            "Reset of the independent first editor erased the second editor's draft or old/new aggregate proof");
        verify(reopened,true,"new success");reset_compiler_puzzle(reopened,after,current);prepare_lesson_state(reopened,after);verify(reopened,true,"reset second");
        require(!compiler_puzzle_case_count(reopened,after,current)&&!compiler_puzzle_historical_case_count(reopened,after,current)
            &&!compiler_puzzle_completed(reopened,after,current),"Reset restored an obsolete second-editor receipt");++groups;
    }
    require(read_bytes(path)==bytes,"Starter-aware compiler QA modified curricular bytes");
    std::cout<<"PASS second U11L15 starter: empty gets five calls; four-call draft/legacy response/one aggregate achievement stay exact, new goal remains pending; reopen/import/scoped Reset preserve independent first editor. Receipts are synthetic QA, not execution evidence\n";
}
void reviewed_u11_math_label_preserves_native_and_imported_state(const std::filesystem::path& base){
    using namespace lgaed;using namespace winrt::Windows::Data::Json;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto asset=read_bytes(path);const auto fixture=test::u11_domain_label_fixture(windows::load_lesson(path));
    const auto& original=fixture.before.pages[8].blocks[2];const auto& current=fixture.after.pages[8].blocks[2];
    const auto authored=JsonObject::Parse(winrt::to_hstring(original.raw_json));
    for(const auto mode:{"empty","partial","incorrect","correct"}){
        StateData data;prepare_lesson_state(data,fixture.before);
        data.preferences["learning_progress"]["u11_domain_opaque_qa"]=std::string(" \t unchanged\r\n");
        const auto compiler=curriculum::stable_block_position(fixture.before,"block_0002_0002");
        require(compiler.has_value(),"U11 matching import QA lost its independent compiler draft");
        const auto draft_key_value=draft_key("programming",fixture.before,fixture.before.pages[compiler->page].blocks[compiler->block]);
        Draft draft;draft.source="// isolated original student draft\n\tint value = 7;\n";draft.input=" \t entrada\n";put_draft(data,draft_key_value,draft);
        if(std::string_view(mode)!="empty"){
            const auto pairs=JsonObject::Parse(authored.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"validator").GetNamedObject(L"expected").Stringify());
            if(std::string_view(mode)=="partial")pairs.Remove(L"dominio");
            else if(std::string_view(mode)=="incorrect"){pairs.Insert(L"dominio",JsonValue::CreateStringValue(L"completa"));pairs.Insert(L"valida",JsonValue::CreateStringValue(L"limite"));}
            JsonObject matching;matching.Insert(L"type",JsonValue::CreateStringValue(L"matching"));matching.Insert(L"pairs",pairs);
            JsonObject answers;answers.Insert(L"casos",matching);JsonObject payload;payload.Insert(L"responses",answers);
            payload.Insert(L"contentFingerprint",JsonValue::CreateStringValue(winrt::to_hstring(test::u11_domain_android_before)));
            payload.Insert(L"incorrectAttempts",JsonValue::CreateNumberValue(3));payload.Insert(L"opaque",JsonObject::Parse(LR"({"selected":17,"origin":"historical matching import QA"})"));
            Response response;response.raw_json=winrt::to_string(payload.Stringify());response.correct=std::string_view(mode)=="correct";
            if(std::string_view(mode)=="incorrect")mark_response_reviewed(response,original);put_response(data,fixture.before,original,response);
        }
        // These are stored historical fixtures, never evidence of playback or
        // native compiler execution. Android must not acquire Windows receipts.
        data.preferences.erase("native_windows");const auto before=data;
        const auto verify=[&](const StateData& value){const auto old=get_response(before,fixture.before,original),saved=get_response(value,fixture.after,current);
            require(old.has_value()==saved.has_value(),"U11 math-label adoption invented or discarded a response");
            if(old)require(saved->raw_json==old->raw_json&&saved->correct==old->correct
                &&response_reviewed(*saved,current)==(std::string_view(mode)=="incorrect"),"U11 math-label adoption changed historical matching metadata/credit");
            const auto restored=get_draft(value,draft_key_value,"");require(restored.source==draft.source&&restored.input==draft.input,"U11 math-label adoption overwrote another editor draft");
            require(string_value(value,"learning_progress","u11_domain_opaque_qa")==" \t unchanged\r\n","U11 math-label adoption changed unrelated progress");
            for(const auto& page:fixture.after.pages)for(const auto& block:page.blocks)if(block.type==curriculum::BlockType::cpp_compiler&&block.output_check)
                require(!windows::compiler_puzzle_completed(value,fixture.after,block),"U11 matching import manufactured a compiler execution receipt");
        };
        prepare_lesson_state(data,fixture.after);validate_lesson_state(data,fixture.after);verify(data);
        StateStore store(base/(std::string("u11-matching-native-")+mode));(void)store.load();store.save(data);verify(store.load());
        const auto exported=base/(std::string("u11-matching-export-")+mode);export_preferences(exported,before,true);
        const auto export_hash=sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"));
        const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,fixture.after);verify(preview.data);
        const auto destination=base/(std::string("u11-matching-import-")+mode);commit_android_import(preview,destination);
        StateStore imported(destination);auto reopened=imported.load();prepare_lesson_state(reopened,fixture.after);validate_lesson_state(reopened,fixture.after);verify(reopened);
        require(sha256(read_bytes(exported/"shared_prefs/learning_progress.xml"))==export_hash,"U11 matching import changed the source export");++groups;
    }
    require(read_bytes(path)==asset,"U11 matching import QA changed the curricular asset");
    std::cout<<"PASS U11 domain label: empty/partial/rejected/successful historical matching payload, original Android fingerprint, independent source/stdin draft, native adoption/reopen and explicit Android import; no invented compiler receipts or playback\n";
}
void remainder_editorial_preserves_native_and_imported_state(const std::filesystem::path& base,const std::filesystem::path& before_path){
    using namespace lgaed;using namespace curriculum;using namespace winrt::Windows::Data::Json;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_resto_y_asociacion_en_el_interprete.json";
    const auto before_bytes=read_bytes(before_path),asset=read_bytes(path);
    require(sha256(before_bytes)=="e46f006765a5fa313430b13dd5b1b0a305a6add572beea529fe84f14ab0bf739",
        "The remainder import fixture differs from its pinned Git publication");
    const auto before=windows::load_lesson(before_path),current=windows::load_lesson(path);
    const auto previous_at=stable_block_position(before,"block_0002_0002"),current_at=stable_block_position(current,"block_0002_0002");
    require(previous_at&&current_at&&*previous_at==*current_at&&previous_at->page==2&&previous_at->block==2,
        "The remainder explanation import changed its stable or visible address");
    const auto& old=before.pages[previous_at->page].blocks[previous_at->block];
    const auto& block=current.pages[current_at->page].blocks[current_at->block];
    require(before.id==current.id&&before.locale=="es"&&current.locale=="es"&&old.type==BlockType::order_code&&block.type==old.type
        &&old.id=="cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete__e04"&&block.id==old.id
        &&old.fragments==block.fragments&&old.correct_order==std::vector<std::size_t>{1,2,3,0}&&block.correct_order==old.correct_order
        &&old.question==block.question&&old.review_feedback==block.review_feedback
        &&exercise_key(before,old)==exercise_key(current,block)&&response_storage_key(before,old)==response_storage_key(current,block)
        &&order_representation_frame(old)==order_representation_frame(block)&&before.state_layout.lineage==current.state_layout.lineage,
        "The remainder editorial changed a protected puzzle, owner, answer or storage contract");
    const auto old_text_hash=sha256(old.explanation),new_text_hash=sha256(block.explanation);
    require(old_text_hash=="c3df0179bd09fbe11d54af6e11c1fb18a140dbc9ee57894070ef1bd11eb25d9b"
        &&new_text_hash=="0d826e2c9c883119e498ec90650b531ec9fe8a1ed3ae5417dfa653e2f3c7238d"
        &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(old.raw_json)))=="524e9d5823d04f180a87ea73f34ac891b0d102450036493991026f93ce65ae2e"
        &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(block.raw_json)))=="f1d3e2efbfd02a258da6416c5de3ad4590e2130eb4db28283f97c4a05b1270c0",
        "The remainder import did not use the independently authenticated complete editorial pair");
    StateData current_metadata;prepare_lesson_state(current_metadata,current);
    const auto editor_at=stable_block_position(before,"block_0003_0004");require(editor_at.has_value(),"Remainder import lost the independent compiler editor");
    const auto& editor=before.pages[editor_at->page].blocks[editor_at->block];const auto editor_key=draft_key("programming",before,editor);
    const auto storage=response_storage_key(before,old);
    for(const auto mode:{"empty","draft","incorrect","correct"}){
        const auto stage=std::string_view(mode);StateData original;prepare_lesson_state(original,before);save_current_page(original,before,previous_at->page);
        original.preferences["attempt_reserve_seconds_test"]["remaining"]=std::int64_t{763};
        original.preferences["attempt_reserve_seconds_test"]["opaque"]=std::string(" \t original reserve\r\n");
        original.preferences["remainder_editorial_qa"]["labels"]=std::vector<std::string>{"preservar"," \t original\r\n"};
        original.preferences["narration_settings"]["language"]=std::string("es");
        Draft draft;draft.source="    // borrador propio anterior\r\nint main() { return 0; }\n";draft.input=" \t entrada\r\n";
        draft.indentation_json=make_indentation_metadata(draft.source,{0});put_draft(original,editor_key,draft);
        if(stage!="empty"){
            Response response;response.order={0,1,2,3};
            if(stage=="incorrect")response.order={1,3,2,0};
            else if(stage=="correct"){response.order.assign(old.correct_order.begin(),old.correct_order.end());response.correct=true;}
            JsonObject opaque;opaque.Insert(L"incorrectAttempts",JsonValue::CreateNumberValue(4));
            opaque.Insert(L"explanationAtAttempt",JsonValue::CreateStringValue(winrt::to_hstring(old.explanation)));
            opaque.Insert(L"opaque",JsonObject::Parse(LR"({"selection":17,"note":"original Android QA"})"));response.raw_json=winrt::to_string(opaque.Stringify());
            if(stage=="incorrect")mark_response_reviewed(response,old);put_response(original,before,old,response);
        }
        // This is stored prior narration metadata, not evidence of synthesis or
        // listening. The new explanation must not inherit its heard prefix.
        auto history=narration::begin({},"remainder-old-explanation-qa","remainder-import-qa",{0,137,0,false},old.explanation,"remainder-page-qa");
        history.entries.front().position.heard_prefix=narration::HeardPrefix{old_text_hash,137000,1000000};narration::write_history(original,history);
        original.preferences.erase("native_windows");validate_lesson_state(original,before);const auto raw=string_value(original,"learning_progress",storage);
        const auto verify=[&](const StateData& data){
            const auto response=get_response(data,current,block),previous=get_response(original,before,old);
            require(response.has_value()==previous.has_value()&&string_value(data,"learning_progress",storage)==raw,
                "Remainder editorial import invented, removed or reserialized a historical answer");
            if(previous)require(response->order==previous->order&&response->correct==previous->correct&&response->raw_json==previous->raw_json
                &&response->reviewed_answer_identity==previous->reviewed_answer_identity&&response_reviewed(*response,block)==(stage=="incorrect"),
                "Remainder editorial import regraded an order or changed its reviewed identity/attempt metadata");
            const auto restored=get_draft(data,editor_key,"");require(restored.source==draft.source&&restored.input==draft.input
                &&restored.indentation_json==draft.indentation_json&&restored.has_saved,
                "Remainder editorial import changed the independent source/stdin/indentation draft");
            require(current_page(data,current)==previous_at->page&&narration::read_history(data)==history
                &&narration::heard_prefix_matches(narration::read_history(data).entries.front().position,old_text_hash,1000000)
                &&!narration::heard_prefix_matches(narration::read_history(data).entries.front().position,new_text_hash,1000000),
                "Remainder editorial import changed the bookmark or credited the new explanation as previously heard");
            for(const auto& [domain,values]:original.preferences)for(const auto& [key,value]:values)
                require(find(data,domain,key)&&*find(data,domain,key)==value,"Remainder editorial import overwrote original typed preferences, attempts or reading provenance");
            for(const auto& [domain,values]:data.preferences)for(const auto& [key,value]:values)if(!find(original,domain,key))
                require(find(current_metadata,domain,key)&&*find(current_metadata,domain,key)==value,
                    "Remainder editorial import added state beyond authenticated current metadata");
            for(const auto& page:current.pages)for(const auto& candidate:page.blocks)if(candidate.type==BlockType::cpp_compiler&&candidate.output_check)
                require(!windows::compiler_puzzle_completed(data,current,candidate)&&windows::compiler_puzzle_case_count(data,current,candidate)==0
                    &&windows::compiler_puzzle_historical_case_count(data,current,candidate)==0,"Remainder import manufactured a Windows compiler execution receipt");
        };
        auto adopted=original;validate_lesson_state(adopted,current);require(adopted==original,"Read-only remainder editorial validation modified state");
        prepare_lesson_state(adopted,current);verify(adopted);const auto prepared=adopted;prepare_lesson_state(adopted,current);
        require(adopted==prepared,"Remainder editorial adoption is not idempotent");
        const auto name=std::string("remainder-editorial-")+mode;StateStore native(base/(name+"-native"));(void)native.load();native.save(adopted);
        auto native_reopened=native.load();validate_lesson_state(native_reopened,current);verify(native_reopened);
        require(native_reopened==adopted,"Remainder editorial native reopening changed the adopted snapshot");
        const auto exported=base/(name+"-export");export_preferences(exported,original,true);
        const auto preview=inspect_android_export(exported);require(preview.data==original,"Remainder Android export/import changed typed source values");
        const auto preserved_preview=preview.data;validate_lesson_state(preview.data,current);require(preview.data==preserved_preview,"Remainder import preview validation mutated source state");verify(preview.data);
        const auto destination=base/(name+"-import");commit_android_import(preview,destination);StateStore imported(destination);auto imported_data=imported.load();
        prepare_lesson_state(imported_data,current);validate_lesson_state(imported_data,current);verify(imported_data);imported.save(imported_data);
        const auto imported_reopened=imported.load();require(imported_reopened==imported_data,"Remainder imported state changed on a second Windows reopen");verify(imported_reopened);
        for(const auto& file:preview.files)require(sha256(read_bytes(exported/file.relative_path))==file.sha256
            &&sha256(read_bytes(destination/"state/active/android"/file.relative_path))==file.sha256,
            "Remainder import modified its Android source or archived original bytes");
        if(stage=="correct"){
            auto corrupt=imported_data;auto changed=*get_response(corrupt,current,block);changed.order={1,3,2,0};put_response(corrupt,current,block,changed);
            const auto retained=corrupt;rejects([&]{validate_lesson_state(corrupt,current);});require(corrupt==retained,"Rejecting a forged correct order changed imported data");
        }
        ++groups;
    }
    for(const auto identity_drift:{false,true}){
        auto invalid=JsonObject::Parse(winrt::to_hstring(current.raw_json));auto changed=invalid.GetNamedArray(L"pages").GetObjectAt(2).GetNamedArray(L"blocks").GetObjectAt(2);
        if(identity_drift)changed.Insert(L"id",JsonValue::CreateStringValue(L"unrelated_editorial_owner"));
        else changed.Insert(L"explanation",JsonValue::CreateStringValue(L"Lanzar otra excepción sin la autorización exacta."));
        rejects([&]{(void)windows::parse_lesson_json(winrt::to_string(invalid.Stringify()),"es");});
    }
    require(read_bytes(before_path)==before_bytes&&read_bytes(path)==asset,"Remainder editorial import QA changed fixture or curricular bytes");
    std::cout<<"PASS integrated U11L16 exception explanation: authenticated prior/current complete payloads, empty/draft/rejected/correct historical orders, opaque attempts and original compiler draft, read-only preview, real Android XML import/archive and Windows reopen; old narration metadata stays old, with no new heard prefix or execution credit. No synthesis/listening claimed\n";
}
void published_u11_assignment_editorial_preserves_imported_state(const std::filesystem::path& base,
        const std::filesystem::path& before_path,const std::filesystem::path& after_path){
    using namespace lgaed;using namespace curriculum;using namespace winrt::Windows::Data::Json;
    const auto before_bytes=read_bytes(before_path),after_bytes=read_bytes(after_path);
    require(sha256(before_bytes)=="9d884b47683e4e2f3891f572dab8456dcf7e8d6af65bbd33b7c8707f46ea6db8"
        &&sha256(after_bytes)=="69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9",
        "Assignment import requires exact f90b42a and c0fc45e publication bytes, not a reconstructed candidate");
    const auto before=windows::load_lesson(before_path),current=windows::load_lesson(after_path);
    require(before.id=="cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada"
        &&current.id==before.id&&before.locale=="es"&&current.locale=="es"
        &&before.state_layout.version==2&&current.state_layout.version==2
        &&before.state_layout.lineage==current.state_layout.lineage&&before.state_layout.signatures==current.state_layout.signatures,
        "Published assignment import changed the original Spanish family or response contracts");
    const auto old_edges=JsonObject::Parse(winrt::to_hstring(before.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    const auto new_edges=JsonObject::Parse(winrt::to_hstring(current.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    require(old_edges.Size()==21&&new_edges.Size()==25,"Assignment import must cover the actual 21-to-25 transition");
    for(unsigned i=0;i<old_edges.Size();++i)require(old_edges.GetAt(i).Stringify()==new_edges.GetAt(i).Stringify(),
        "Assignment import rewrote the existing editorial prefix");
    const auto archived=std::ranges::find(current.state_layout.reading_revisions,std::size_t{21},&StateReadingRevision::editorial_prefix);
    require(archived!=current.state_layout.reading_revisions.end()&&archived->previous_blocks.size()==4,
        "The reader loader cannot authenticate all four old owners at the actual 21-edge publication");
    struct Plan {std::string_view identity;StatePosition position;};
    constexpr std::array plans{
        Plan{"block_0002_0003",{2,3}},Plan{"block_0004_0001",{4,1}},
        Plan{"block_0004_0002",{4,2}},Plan{"block_0007_0001",{7,1}}};
    const auto editor_at=stable_block_position(before,"block_0005_0002");
    require(editor_at&&editor_at==stable_block_position(current,"block_0005_0002"),"Assignment import lost its independent compiler owner");
    const auto& editor=before.pages[editor_at->page].blocks[editor_at->block];
    const auto editor_key=draft_key("programming",before,editor);
    StateData current_metadata;prepare_lesson_state(current_metadata,current);
    const auto make_answer=[](const Block& block,bool correct){
        Response response;AnswerEvaluation evaluated;
        if(block.type==BlockType::multiple_choice){
            require(block.correct_index&&block.options.size()>1,"Assignment import needs a real authored choice");
            const auto index=correct?*block.correct_index:(*block.correct_index+1)%block.options.size();
            response.selected=static_cast<std::int64_t>(index);response.answer=block.options[index];evaluated=evaluate_choice(block,index);
        }else if(block.type==BlockType::order_code){
            auto order=block.correct_order;require(order.size()>1,"Assignment import needs a real authored order");
            if(!correct)std::rotate(order.begin(),order.begin()+1,order.end());
            response.order.assign(order.begin(),order.end());evaluated=evaluate_order(block,order);
        }else{
            require(block.type==BlockType::output_prediction&&!block.answers.empty(),"Unexpected assignment import activity type");
            response.answer=correct?block.answers.front():"__isolated_qa_wrong_answer__";
            evaluated=evaluate_text_answer(block,response.answer);
        }
        require(evaluated.correct==correct,"Assignment import setup did not satisfy its intended authored verdict");
        response.correct=correct;return response;
    };
    std::size_t scenarios=0;
    for(const auto& plan:plans){
        const auto old_at=stable_block_position(before,plan.identity),current_at=stable_block_position(current,plan.identity);
        require(old_at&&current_at&&*old_at==plan.position&&*current_at==plan.position,"Assignment import changed a public owner or address");
        const auto& old=before.pages[old_at->page].blocks[old_at->block];
        const auto& block=current.pages[current_at->page].blocks[current_at->block];
        const auto previous=archived->previous_blocks.find(std::string(plan.identity));
        require(previous!=archived->previous_blocks.end()
            &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(previous->second)))
                ==test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(old.raw_json))),
            "The reading archive does not contain the complete authenticated old question and explanation");
        require(old.id==block.id&&old.type==block.type&&old.options==block.options&&old.correct_index==block.correct_index
            &&old.answers==block.answers&&old.fragments==block.fragments&&old.correct_order==block.correct_order
            &&old.code==block.code&&old.review_feedback==block.review_feedback&&old.review_feedback_options==block.review_feedback_options
            &&exercise_key(before,old)==exercise_key(current,block)&&response_storage_key(before,old)==response_storage_key(current,block),
            "Assignment import changed a question's activity contract, answer, guide, program or storage key");
        const auto storage=response_storage_key(before,old),old_text_hash=sha256(old.explanation),new_text_hash=sha256(block.explanation);
        require(old_text_hash!=new_text_hash,"Assignment import lost an explanation revision");
        for(const auto mode:{"empty","draft","incorrect","correct"}){
            const auto stage=std::string_view(mode);StateData original;prepare_lesson_state(original,before);save_current_page(original,before,old_at->page);
            original.preferences["assignment_import_qa"]["labels"]=std::vector<std::string>{"preservar"," \t original\r\n"};
            original.preferences["attempt_reserve_seconds_test"]["remaining"]=std::int64_t{763};
            original.preferences["narration_settings"]["language"]=std::string("es");
            Draft draft;draft.source="    // borrador propio anterior\r\nint main() { return 0; }\n";
            draft.input=" \t entrada\r\n";draft.indentation_json=make_indentation_metadata(draft.source,{0});
            draft.selection_start=2;draft.selection_end=9;put_draft(original,editor_key,draft);
            if(stage!="empty"){
                auto response=make_answer(old,stage=="correct");
                JsonObject opaque;opaque.Insert(L"incorrectAttempts",JsonValue::CreateNumberValue(4));
                opaque.Insert(L"explanationAtAttempt",JsonValue::CreateStringValue(winrt::to_hstring(old.explanation)));
                opaque.Insert(L"opaque",JsonObject::Parse(LR"({"selection":17,"note":"isolated historical assignment QA"})"));
                response.raw_json=winrt::to_string(opaque.Stringify());
                if(stage=="incorrect")mark_response_reviewed(response,old);put_response(original,before,old,response);
            }
            // Deliberate QA metadata tests the three-slot bound and exact
            // provenance. It is not a claim of historical listening or PCM.
            auto history=narration::begin({},"assignment-import-untouched-a","assignment-owner-a",{1,239,0,false},"opaque A","assignment-page-a");
            history=narration::begin(std::move(history),"assignment-import-untouched-b","assignment-owner-b",{2,317,0,false},"opaque B","assignment-page-b");
            history=narration::begin(std::move(history),"assignment-import-old-explanation","assignment-owner-c",{0,137,0,false},old.explanation,"assignment-page-c");
            history.entries.front().position.heard_prefix=narration::HeardPrefix{old_text_hash,137000,1000000};
            narration::write_history(original,history);original.preferences.erase("native_windows");validate_lesson_state(original,before);
            const auto raw=string_value(original,"learning_progress",storage);
            const auto saved_response=get_response(original,before,old);
            const auto verify=[&](const StateData& data){
                const auto restored=get_response(data,current,block);
                require(restored.has_value()==saved_response.has_value()&&string_value(data,"learning_progress",storage)==raw,
                    "Assignment import invented, removed or reserialized an original response");
                if(saved_response)require(restored->answer==saved_response->answer&&restored->correct==saved_response->correct
                    &&restored->selected==saved_response->selected&&restored->order==saved_response->order&&restored->raw_json==saved_response->raw_json
                    &&restored->reviewed_answer_identity==saved_response->reviewed_answer_identity
                    &&restored->historical_choice==saved_response->historical_choice&&response_reviewed(*restored,block)==(stage=="incorrect"),
                    "Assignment import regraded an answer or changed the last rejection and opaque attempt metadata");
                const auto saved_draft=get_draft(data,editor_key,"");
                require(saved_draft.source==draft.source&&saved_draft.input==draft.input&&saved_draft.indentation_json==draft.indentation_json
                    &&saved_draft.selection_start==draft.selection_start&&saved_draft.selection_end==draft.selection_end&&saved_draft.has_saved,
                    "Assignment import changed independent source, input, indentation or caret");
                const auto restored_history=narration::read_history(data);
                require(current_page(data,current)==old_at->page&&restored_history==history&&restored_history.entries.size()==3
                    &&narration::heard_prefix_matches(restored_history.entries.front().position,old_text_hash,1000000)
                    &&!narration::heard_prefix_matches(restored_history.entries.front().position,new_text_hash,1000000),
                    "Assignment import changed bookmarks or credited a revised explanation as already heard");
                for(const auto& [domain,values]:original.preferences)for(const auto& [key,value]:values)
                    require(find(data,domain,key)&&*find(data,domain,key)==value,"Assignment import changed an original typed value, attempt, answer or proof");
                for(const auto& [domain,values]:data.preferences)for(const auto& [key,value]:values)if(!find(original,domain,key))
                    require(find(current_metadata,domain,key)&&*find(current_metadata,domain,key)==value,
                        "Assignment import added state beyond authenticated current metadata");
                for(const auto& page:current.pages)for(const auto& compiler:page.blocks)
                    if(compiler.type==BlockType::cpp_compiler&&compiler.output_check)
                        require(!windows::compiler_puzzle_completed(data,current,compiler)&&windows::compiler_puzzle_case_count(data,current,compiler)==0
                            &&windows::compiler_puzzle_historical_case_count(data,current,compiler)==0,
                            "Assignment import manufactured compiler execution credit from ordinary responses");
            };
            auto adopted=original;validate_lesson_state(adopted,current);require(adopted==original,"Read-only assignment validation changed historical state");
            prepare_lesson_state(adopted,current);verify(adopted);const auto prepared=adopted;prepare_lesson_state(adopted,current);
            require(adopted==prepared,"Published assignment adoption is not idempotent");
            const auto name=std::string(plan.identity)+'-'+mode;
            StateStore native(base/(name+"-native"));(void)native.load();native.save(adopted);
            const auto native_reopened=native.load();require(native_reopened==adopted,"Assignment native reopen changed adopted state");verify(native_reopened);
            const auto exported=base/(name+"-export");export_preferences(exported,original,true);
            const auto preview=inspect_android_export(exported);require(preview.data==original,"Assignment XML import changed typed source values");
            const auto inspected=preview.data;validate_lesson_state(preview.data,current);require(preview.data==inspected,"Assignment preview validation changed source state");verify(preview.data);
            const auto destination=base/(name+"-import");commit_android_import(preview,destination);StateStore imported(destination);auto data=imported.load();
            prepare_lesson_state(data,current);validate_lesson_state(data,current);verify(data);imported.save(data);
            const auto reopened=imported.load();require(reopened==data,"Assignment import changed on a second native reopen");verify(reopened);
            for(const auto& file:preview.files)require(sha256(read_bytes(exported/file.relative_path))==file.sha256
                &&sha256(read_bytes(destination/"state/active/android"/file.relative_path))==file.sha256,
                "Assignment import modified an XML source or its archived original bytes");
            if(stage=="correct"){
                auto invalid=data;auto wrong=make_answer(block,false);wrong.correct=true;put_response(invalid,current,block,wrong);
                const auto retained=invalid;rejects([&]{validate_lesson_state(invalid,current);});
                require(invalid==retained,"Rejecting a forged assignment success changed imported state");
            }
            ++scenarios;++groups;
        }
    }
    require(scenarios==16&&read_bytes(before_path)==before_bytes&&read_bytes(after_path)==after_bytes,
        "Assignment import omitted a published owner/state or modified its immutable sources");
    std::cout<<"PASS published U11L20 21-to-25 editorial transition: sixteen isolated historical response states, authenticated four-owner reading archive, XML import/archive, native adoption/reopen, original attempts/drafts and three stored bookmarks retained; no current compiler or listening credit invented. No player, PCM or human listening exercised.\n";
}
void u11_names_preserves_historical_and_added_prediction_state(const std::filesystem::path& base,const std::filesystem::path& before_path){
    using namespace lgaed;using namespace curriculum;using namespace winrt::Windows::Data::Json;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_nombres_del_lenguaje_y_tokens_con_texto.json";
    const auto historical_bytes=read_bytes(before_path),asset=read_bytes(path);
    require(historical_bytes.size()==36663&&sha256(historical_bytes)=="8a62de3022c0507a12d3cc372e12a92dc69801c7313a657516a7287b343d3003",
        "The U11 names historical fixture differs from its authenticated 90e510f publication");
    require(asset.size()==81062&&sha256(asset)=="335036782a5c6eb7d1edd1a60c1b062572a791e8c6e4ce54e14fd25b246df4b0",
        "The U11 names current fixture differs from the audited Work revision");
    const auto before=windows::load_lesson(before_path),current=windows::load_lesson(path);
    require(before.id=="cpp_fundamentals__texto_interpretacion_y_flujos__nombres_del_lenguaje_y_tokens_con_texto"&&before.id==current.id
        &&before.locale=="es"&&current.locale=="es"&&before.pages.size()==8&&current.pages.size()==8
        &&before.state_layout.version==0&&current.state_layout.version==2&&current.compiler_puzzles_required,
        "The U11 names import lost its unmapped historical owner or current required-compiler family");
    const auto owner_at=stable_block_position(current,"block_0005_0003"),added_at=stable_block_position(current,"work_u11l17_reserva_textual");
    require(owner_at&&added_at&&*owner_at==StatePosition{5,4}&&*added_at==StatePosition{6,1},"The U11 names compiler or added prediction moved outside its audited owners");
    const auto& owner=current.pages[owner_at->page].blocks[owner_at->block];const auto& old_owner=before.pages[5].blocks[3];
    const auto& added=current.pages[added_at->page].blocks[added_at->block];
    const auto metadata=windows::parse_lesson_progress_metadata(current.raw_json);const auto& catalog_added=metadata.pages[6].blocks[1];
    const auto added_namespace=sha256(current.state_layout.lineage+"/work_u11l17_reserva_textual");
    require(current.state_layout.additions.size()==1&&added.type==BlockType::output_prediction
        &&added.address.page==8&&added.address.block==0&&added.address.progress_page=="page_0006"&&added.address.addition_namespace==added_namespace
        &&added.output_comparison==TextComparison::trimmed&&added.answers.size()==1&&added.answers.front().find('\n')!=std::string::npos
        &&exercise_key(current,added)==exercise_key(metadata,catalog_added)&&catalog_added.address.progress_page=="page_0006"
        &&activity_prerequisites(current,added)==std::vector<StatePosition>{*owner_at},
        "The U11 names addition lost its dedicated 8/0 namespace, page-seven progress group or compiler prerequisite");
    require(owner.type==BlockType::cpp_compiler&&old_owner.type==owner.type&&owner.initial_code==old_owner.initial_code
        &&owner.output_check&&owner.output_check->cases.size()==1&&owner.output_check->cases.front().standard_input.empty()
        &&owner.output_check->cases.front().outputs.front().size()==327
        &&JsonObject::Parse(winrt::to_hstring(old_owner.raw_json)).GetNamedObject(L"outputCheck").Stringify()
            ==JsonObject::Parse(winrt::to_hstring(owner.raw_json)).GetNamedObject(L"outputCheck").Stringify()
        &&exercise_key(before,old_owner)==exercise_key(current,owner)&&draft_key("programming",before,old_owner)==draft_key("programming",current,owner),
        "The U11 names migration changed the original nine-report aggregate objective or compiler draft address");
    const auto added_key=exercise_key(current,added),editor_key=draft_key("programming",current,owner);
    StateData historical;save_current_page(historical,before,7);
    historical.preferences["learning_progress"]["lesson_"+before.id+"_started"]=true;
    historical.preferences["learning_progress"]["lesson_"+before.id+"_completed"]=true;
    historical.preferences["u11_names_opaque"]["original"]=std::string(" \t original data\r\n");
    historical.preferences["u11_names_opaque"]["labels"]=std::vector<std::string>{"conservar"," \t segundo\n"};
    historical.preferences["attempt_reserve_seconds_test"]["remaining"]=std::int64_t{731};
    historical.preferences["lesson_narration_settings"]["engine"]=std::string("KOKORO");
    historical.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("ef_dora");
    std::size_t protected_blocks=0,historical_exercises=0;
    for(const auto& page:current.pages)for(const auto& block:page.blocks){
        if(block.stable_id==added.stable_id)continue;
        require(block.address.addition_namespace.empty()&&block.address.page<before.pages.size()
            &&block.address.block<before.pages[block.address.page].blocks.size(),"An original U11 names block acquired a new or invalid state address");
        const auto& old=before.pages[block.address.page].blocks[block.address.block];
        require(old.type==block.type&&exercise_key(before,old)==exercise_key(current,block)&&exercise_key(current,block)!=added_key
            &&old.code==block.code&&old.initial_code==block.initial_code,"The U11 names addition moved an original response slot or changed a protected program");
        ++protected_blocks;Response response;response.correct=true;response.raw_json=R"({"incorrectAttempts":3,"opaque":{"original":"U11 names historical QA"}})";
        if(old.type==BlockType::multiple_choice){require(old.correct_index.has_value(),"A historical U11 names selection lost its answer");response.selected=static_cast<std::int64_t>(*old.correct_index);}
        else if(old.type==BlockType::inline_fill_code){JsonObject payload=JsonObject::Parse(winrt::to_hstring(response.raw_json));JsonArray answers;
            for(const auto& answer:old.answers)answers.Append(JsonValue::CreateStringValue(winrt::to_hstring(answer)));payload.Insert(L"answers",answers);response.raw_json=winrt::to_string(payload.Stringify());}
        else if(old.type==BlockType::fill_code||old.type==BlockType::output_prediction){require(!old.answers.empty(),"A historical U11 names text puzzle lost its answer");response.answer=old.answers.front();}
        else continue;
        put_response(historical,before,old,response);++historical_exercises;
    }
    require(protected_blocks==31&&historical_exercises==6,"The U11 names historical fixture omitted an original block or quiz response");
    Draft draft;draft.source=old_owner.initial_code+"\n    // borrador propio conservado\r\n";draft.input=" \t entrada\r\n";
    draft.selection_start=4;draft.selection_end=9;draft.indentation_json=make_indentation_metadata(draft.source,{});put_draft(historical,editor_key,draft);
    Response opaque;opaque.correct=true;opaque.answer="Correcto anterior de Android, sin recibo nativo";
    opaque.raw_json=R"({"incorrectAttempts":2,"opaque":{"receipt":"Android historical result, not Windows execution"}})";put_response(historical,before,old_owner,opaque);
    const auto historical_compiler_json=string_value(historical,"learning_progress",exercise_key(before,old_owner));
    // Storage-only bookmarks exercise conservation without asserting playback.
    auto history=narration::begin({},"u11-names-other-first","names-owner-a",{0,117,0,false},"original other source","names-other-page-a");
    history=narration::begin(std::move(history),"u11-names-other-second","names-owner-b",{0,239,0,false},"original second source","names-other-page-b");
    history=narration::begin(std::move(history),"u11-names-historical-page","names-owner-c",{0,317,0,false},"original historical source","names-original-page");
    narration::write_history(historical,history);validate_lesson_state(historical,before);
    require(!historical.preferences.contains("native_windows")&&!get_response(historical,current,added),"Preparing the historical U11 names export invented native data or the new answer");
    const auto progress=[&](const StateData& value,const Lesson& lesson){return catalog_lesson_page_progress(value,lesson,[&](const Block& block){
        if(block.type==BlockType::cpp_compiler)return windows::compiler_puzzle_completed(value,lesson,block);
        const auto response=get_response(value,lesson,block);return response&&response->correct;});};
    // The historical bookmark reaches all eight pages, but its opaque Android
    // compiler answer is not a Windows execution receipt. Every compiler is
    // required now, including editors from a publication without the old flag.
    const auto historical_progress=progress(historical,before);
    require(all_pages_reached(historical,before)&&historical_progress.total==8&&historical_progress.completed==7
        &&!windows::compiler_puzzle_completed(historical,before,old_owner)
        &&windows::compiler_puzzle_case_count(historical,before,old_owner)==0
        &&string_value(historical,"learning_progress",exercise_key(before,old_owner))==historical_compiler_json,
        "Historical U11 names arrival or opaque Android achievement was confused with native compiler execution credit");
    const auto verify=[&](const StateData& value,bool solved){
        for(const auto& [domain,values]:historical.preferences)for(const auto& [key,item]:values)
            require(find(value,domain,key)&&*find(value,domain,key)==item,"U11 names adoption/import changed an original answer, opaque receipt, draft, attempts or typed setting");
        const auto restored=get_draft(value,editor_key,"");require(restored.source==draft.source&&restored.input==draft.input&&restored.indentation_json==draft.indentation_json
            &&restored.selection_start==draft.selection_start&&restored.selection_end==draft.selection_end&&restored.has_saved,
            "U11 names import changed exact source/stdin/indentation or selection");
        require(current_page(value,current)==7&&narration::read_history(value)==history&&catalog_lesson_completed(value,current.id)
            &&string_value(value,"learning_progress",exercise_key(current,owner))==historical_compiler_json,
            "U11 names migration changed the core historical page, stored narration or opaque prior achievement");
        require(!windows::compiler_puzzle_completed(value,current,owner)&&windows::compiler_puzzle_case_count(value,current,owner)==0
            &&windows::compiler_puzzle_historical_case_count(value,current,owner)==0,"U11 names Android data manufactured native compiler execution credit");
        const auto pages=progress(value,current);require(pages.total==8&&pages.completed==(solved?7u:6u),
            "Historical read/completed flags incorrectly completed the required compiler or new page-seven prediction");
    };
    auto adopted=historical;validate_lesson_state(adopted,current);require(adopted==historical,"Read-only U11 names adoption validation changed original state");
    prepare_lesson_state(adopted,current);verify(adopted,false);require(!get_response(adopted,current,added),"Adopting U11 names selected the new prediction");
    const auto prepared=adopted;prepare_lesson_state(adopted,current);require(adopted==prepared,"U11 names v2 adoption is not idempotent");
    StateStore native(base/"u11-names-native");(void)native.load();native.save(adopted);require(native.load()==adopted,"U11 names native reopening changed the adopted state");
    const auto roundtrip=[&](const StateData& source,std::string_view suffix,bool solved){
        const auto exported=base/("u11-names-export-"+std::string(suffix));export_preferences(exported,source,true);const auto preview=inspect_android_export(exported);
        require(preview.data==source,"U11 names Android XML inspection changed the exact exported values");
        validate_lesson_state(preview.data,current);require(preview.data==source,"U11 names read-only import validation changed exported data");
        const auto destination=base/("u11-names-import-"+std::string(suffix));commit_android_import(preview,destination);StateStore store(destination);auto imported=store.load();
        prepare_lesson_state(imported,current);validate_lesson_state(imported,current);verify(imported,solved);store.save(imported);const auto reopened=store.load();
        require(reopened==imported,"U11 names imported state changed on Windows reopening");verify(reopened,solved);
        for(const auto& file:preview.files)require(sha256(read_bytes(exported/file.relative_path))==file.sha256
            &&sha256(read_bytes(destination/"state/active/android"/file.relative_path))==file.sha256,"U11 names import changed its source XML or archived original bytes");
        ++groups;return reopened;
    };
    const auto historical_import=roundtrip(historical,"historical",false);require(!get_response(historical_import,current,added),"Importing the previous 31 blocks fabricated an added answer");
    for(const auto mode:{"empty","draft","incorrect","correct"}){
        const auto stage=std::string_view(mode);auto sample=adopted;
        if(stage!="empty"){
            Response response;response.answer=stage=="draft"?"Lleno: Buffer ocupado\n":added.answers.front();response.correct=stage=="correct";
            if(stage=="incorrect"){const auto position=response.answer.find("Guardado: v:0:f");require(position!=std::string::npos,"The U11 names prediction lost its discriminating preserved-token line");
                response.answer.replace(position,std::string("Guardado: v:0:f").size(),"Guardado: n:7:x");}
            response.raw_json=R"({"incorrectAttempts":2,"opaque":{"source":"new U11 names prediction QA"}})";
            if(stage=="incorrect")mark_response_reviewed(response,added);put_response(sample,current,added,response);
        }
        validate_lesson_state(sample,current);verify(sample,stage=="correct");const auto original=get_response(sample,current,added);
        const auto reopened=roundtrip(sample,mode,stage=="correct");const auto restored=get_response(reopened,current,added);
        require(original.has_value()==restored.has_value(),"U11 names prediction import invented or discarded its empty state");
        if(original)require(restored->raw_json==original->raw_json&&restored->answer==original->answer&&restored->correct==original->correct
            &&restored->reviewed_answer_identity==original->reviewed_answer_identity&&response_reviewed(*restored,added)==(stage=="incorrect"),
            "U11 names prediction import regraded its draft/rejection/success or changed opaque attempts");
        if(stage=="correct"){
            auto malformed=reopened;auto forged=*restored;forged.answer="Respuesta distinta";put_response(malformed,current,added,forged);
            const auto retained=malformed;rejects([&]{validate_lesson_state(malformed,current);});require(malformed==retained,"Rejecting a forged U11 names success changed stored data");
        }
    }
    require(read_bytes(before_path)==historical_bytes&&read_bytes(path)==asset,"U11 names conservation QA rewrote its historical fixture or active curriculum");
    std::cout<<"PASS integrated U11L17 names: authenticated unmapped 31-block history, six exact old quiz responses, opaque unproved compiler achievement and personal draft, v2 adoption/reopen and Android XML archives; added prediction empty/draft/rejected/correct retains namespace8/0 and page-seven progress without invented native execution, answers or listening. Core bookmark conservation does not claim view gate redirection or audio\n";
}
void diagnostic_contracts_preserve_native_and_imported_state(const std::filesystem::path& base) {
    using namespace lgaed;using namespace curriculum;using namespace windows;
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    const auto adopt=[&](const Lesson& before,const test::CompileCheckCase& plan){
        const auto integrated=load_lesson(folder/plan.filename);
        const auto live=stable_block_position(integrated,plan.identity),at=stable_block_position(before,plan.identity);
        require(live&&live==at,"Diagnostic adoption lost its current/historical stable owner");
        const auto& authored=integrated.pages[live->page].blocks[live->block];
        require(authored.compile_check&&authored.compile_check->diagnostic==plan.diagnostic&&!authored.compile_check->explanation.empty(),
            "The current diagnostic contract or full Work explanation is absent");
        const auto result=test::with_compile_check(before,plan.identity,plan.diagnostic,authored.compile_check->explanation);
        const auto& block=result.pages[at->page].blocks[at->block];
        require(test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(block.raw_json)))==
            test::compile_check_test_fingerprint(winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(authored.raw_json))),
            "Diagnostic state QA did not reconstruct the complete current authored block");
        return result;
    };
    std::size_t index=0;
    for(const auto& plan:test::compile_check_cases){
        const auto before=test::load_pre_compile_check_lesson(plan.filename),current=adopt(before,plan);
        const auto position=stable_block_position(current,plan.identity);require(position.has_value(),"Missing native diagnostic owner");
        const auto& block=current.pages[position->page].blocks[position->block];
        StateData data;prepare_lesson_state(data,before);
        data.preferences["diagnostic_opaque"]["keep"]=std::string(" \t preserved\r\n");
        Response old;old.correct=true;old.answer="old opaque response";put_response(data,before,before.pages[position->page].blocks[position->block],old);
        Draft draft;draft.source=block.initial_code+"\n// personal draft\n";draft.input=" \t untouched stdin\r\n";
        draft.selection_start=2;draft.selection_end=9;const auto key=draft_key("programming",current,block);put_draft(data,key,draft);
        const auto original=data;prepare_lesson_state(data,current);validate_lesson_state(data,current);
        for(const auto& [domain,values]:original.preferences)for(const auto& [name,value]:values)
            require(find(data,domain,name)&&*find(data,domain,name)==value,"Adopting a diagnostic rewrote prior student data");
        require(!compiler_puzzle_completed(data,current,block)&&get_draft(data,key,"").source==draft.source,
            "An opaque success became diagnostic proof or the current source was replaced");
        const auto pin="lesson_"+current.id+"_state_compile_checks_v1_es";
        require(find(data,"learning_progress",pin),"The diagnostic objective lacks rollback protection");
        const auto adopted=data;prepare_lesson_state(data,current);require(data==adopted,"Diagnostic adoption is not idempotent");
        auto rollback=data;rejects([&]{prepare_lesson_state(rollback,before);});require(rollback==data,"Refusing diagnostic rollback changed saved data");
        auto corrupt=data;corrupt.preferences["learning_progress"][pin]=std::string("[[\"wrong\",\"goal\",\"hash\"]]");
        const auto invalid=corrupt;rejects([&]{prepare_lesson_state(corrupt,current);});require(corrupt==invalid,"Rejecting a corrupt diagnostic pin changed data");
        auto bad_receipt=data;const auto receipt="compiler-puzzle/v1/es/"+current.id+'/'+exercise_key(current,block)+'/'+sha256(reference_objective_frame(current,block))+'/';
        bad_receipt.preferences["native_windows"][receipt+"program"]=std::int64_t{4};
        const auto malformed=bad_receipt;rejects([&]{prepare_lesson_state(bad_receipt,current);});require(bad_receipt==malformed,"Corrupt diagnostic receipt partially adopted metadata");
        const auto name=std::to_string(index++);StateStore store(base/("diagnostic-"+name));(void)store.load();store.save(data);
        auto reopened=store.load();validate_lesson_state(reopened,current);require(reopened==data,"Diagnostic native reopen changed provenance or drafts");
        const auto exported=base/("diagnostic-export-"+name);export_preferences(exported,data,true);
        const auto preview=inspect_android_export(exported);validate_lesson_state(preview.data,current);
        commit_android_import(preview,base/("diagnostic-import-"+name));StateStore imported_store(base/("diagnostic-import-"+name));
        auto imported=imported_store.load();validate_lesson_state(imported,current);
        for(const auto& [domain,values]:data.preferences)for(const auto& [entry,value]:values)
            require(find(imported,domain,entry)&&*find(imported,domain,entry)==value,"Android diagnostic import changed original typed data");
        require(!compiler_puzzle_completed(imported,current,block)&&get_draft(imported,key,"").source==draft.source,
            "Android import manufactured diagnostic success or changed code");
        reset_compiler_puzzle(reopened,current,block);prepare_lesson_state(reopened,current);
        require(find(reopened,"learning_progress",pin)&&!compiler_puzzle_completed(reopened,current,block),"Reset removed diagnostic provenance or invented credit");
        ++groups;
    }
    {
        // Work adds these three objectives to one existing lesson. Begin with
        // the lexically last owner so later adoption cannot rely on appending
        // rows to the old serialized pin or on the order of editorial changes.
        const auto before=test::load_pre_compile_check_lesson("lesson_valores_por_defecto_con_significado.json");
        StateData data;prepare_lesson_state(data,before);
        data.preferences["diagnostic_chain"]["text"]=std::string(" \t retained\r\n");
        data.preferences["diagnostic_chain"]["count"]=std::int64_t{7};
        data.preferences["diagnostic_chain"]["flag"]=true;
        for(const auto plan_index:{1u,2u,3u}) {
            const auto& plan=test::compile_check_cases[plan_index];
            const auto position=stable_block_position(before,plan.identity);require(position.has_value(),"Missing cumulative diagnostic owner");
            const auto& block=before.pages[position->page].blocks[position->block];
            Draft draft;draft.source=block.initial_code+"\n// independently retained "+plan.identity+"\t\r\n";
            draft.input=" \t dormant input\r\n";draft.selection_start=2;draft.selection_end=9;
            put_draft(data,draft_key("programming",before,block),draft);
            Response old;old.correct=true;old.answer="opaque prior answer for "+std::string(plan.identity);put_response(data,before,block,old);
        }
        const auto seeded=data;
        const auto& first_plan=test::compile_check_cases[3];
        const auto first=adopt(before,first_plan);
        prepare_lesson_state(data,first);validate_lesson_state(data,first);
        const auto pin="lesson_"+before.id+"_state_compile_checks_v1_es";
        const auto first_rows=winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(string_value(data,"learning_progress",pin)));
        require(first_rows.Size()==1,"The first cumulative diagnostic did not adopt one exact pin row");
        const auto retained_row=first_rows.GetAt(0).Stringify();
        const auto preserves_previous=[&](const StateData& previous){
            for(const auto& [domain,values]:previous.preferences)for(const auto& [name,value]:values) {
                if(domain=="learning_progress"&&name==pin)continue;
                require(find(data,domain,name)&&*find(data,domain,name)==value,"Extending a diagnostic pin changed previous student data");
            }
        };
        preserves_previous(seeded);
        const auto first_state=data;
        const auto second=adopt(first,test::compile_check_cases[1]);
        prepare_lesson_state(data,second);validate_lesson_state(data,second);preserves_previous(first_state);
        const auto second_rows=winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(string_value(data,"learning_progress",pin)));
        require(second_rows.Size()==2,"Adding an earlier diagnostic owner did not extend the existing pin");
        const auto second_state=data;
        const auto all=adopt(second,test::compile_check_cases[2]);
        prepare_lesson_state(data,all);validate_lesson_state(data,all);preserves_previous(second_state);
        const auto all_rows=winrt::Windows::Data::Json::JsonArray::Parse(winrt::to_hstring(string_value(data,"learning_progress",pin)));
        bool retained_first=false;for(const auto& row:all_rows)retained_first|=row.Stringify()==retained_row;
        require(all_rows.Size()==3&&retained_first,
            "Adopting all three diagnostics changed or replaced the first goal's original pin row");
        for(const auto plan_index:{1u,2u,3u}) {
            const auto& plan=test::compile_check_cases[plan_index];const auto position=stable_block_position(all,plan.identity);
            const auto& block=all.pages[position->page].blocks[position->block];
            require(!compiler_puzzle_completed(data,all,block)&&
                get_draft(data,draft_key("programming",all,block),"").source==get_draft(seeded,draft_key("programming",before,block),"").source,
                "Cumulative diagnostic adoption invented a success or overwrote an independent source draft");
        }
        auto opposite=adopt(before,test::compile_check_cases[2]);
        opposite=adopt(opposite,test::compile_check_cases[1]);
        opposite=adopt(opposite,first_plan);
        auto alternative=seeded;prepare_lesson_state(alternative,opposite);
        require(string_value(alternative,"learning_progress",pin)==string_value(data,"learning_progress",pin),
            "The final three-goal diagnostic pin depended on authoring/adoption order");
        const auto integrated=load_lesson(folder/"lesson_valores_por_defecto_con_significado.json");
        validate_lesson_state(data,integrated);
        const auto all_adopted=data;prepare_lesson_state(data,integrated);
        require(data==all_adopted,"Today's integrated three-goal lesson changed the independently adopted pins or student state");
        StateStore store(base/"diagnostic-cumulative");(void)store.load();store.save(data);auto reopened=store.load();
        validate_lesson_state(reopened,all);require(reopened==data,"Reopening a three-goal diagnostic lesson changed its cumulative pin or drafts");
        const auto preserved=reopened;rejects([&]{prepare_lesson_state(reopened,first);});
        require(reopened==preserved,"Refusing to downgrade three diagnostic goals to the first goal partially changed saved data");
        ++groups;
    }
    std::cout<<"PASS five diagnostic contracts: prior opaque success stays unproved, exact draft/typed-data preservation, native reopen, Android import, corrupt receipts, atomic rollback pins and Reset; three-goal adoption is monotonic and order-independent\n";
}
void rejected_compiler_reviews_preserve_captured_attempts(const std::filesystem::path& base) {
    using namespace lgaed;using namespace curriculum;using namespace windows;
    // These FreshExecution values are deliberate unit-level evaluator inputs,
    // not evidence of compiling, running or hearing a curricular program.
    const auto lesson=parse_lesson_json(R"({"id":"qa_compiler_review","title":"QA","compilerPuzzlesRequired":true,"pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"original","initialStandardInput":"1","outputCheck":{"cases":[{"standardInput":"1","outputs":["2"]},{"standardInput":"2","outputs":["4"]}]}}]}]})");
    const auto& block=lesson.pages[0].blocks[0];const std::string feedback="Incorrecto\nRevisa la muestra completa.";
    CppRunRequest captured;captured.revision=7;captured.source_utf8="// synthetic unit source\n";captured.stdin_utf8="1";
    FreshExecution wrong{7,true,true,0,"1","otro resultado"};
    StateData data;data.preferences["review_qa"]["opaque"]=std::string(" \t conservar\r\n保存");
    Draft draft;draft.source=captured.source_utf8;draft.input=captured.stdin_utf8;draft.selection_start=2;draft.selection_end=7;
    const auto draft_id=draft_key("programming",lesson,block);put_draft(data,draft_id,draft);
    put_response(data,"independent_review_qa",Response{"opaque response",false});const auto original=data;
    require(record_compiler_puzzle_review(data,lesson,block,captured,wrong,feedback)
        &&compiler_puzzle_review(data,lesson,block,captured)==feedback
        &&!compiler_puzzle_completed(data,lesson,block)&&compiler_puzzle_case_count(data,lesson,block)==0,
        "A reviewed evaluator rejection was lost or incorrectly granted a successful compiler case");
    const auto reviewed=data;require(compiler_puzzle_review(data,lesson,block,captured)==feedback&&data==reviewed,
        "Reading a matching rejected attempt rewrote its saved state");
    const auto key="compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+exercise_key(lesson,block)+'/'
        +sha256(reference_objective_frame(lesson,block))+'/';
    for(const auto& [domain,values]:original.preferences)for(const auto& [name,value]:values)
        require(find(data,domain,name)&&*find(data,domain,name)==value,"Recording compiler review changed draft, unrelated data or another answer");
    for(const auto suffix:{"program","cases","completed","feedback"})require(!find(data,"native_windows",key+suffix),
        "A rejected review wrote into the independent successful-case receipt fields");
    auto same=captured;same.request_id=99;same.revision=15;same.projects_directory=base/"unused";
    invalidate_compiler_puzzle_attempt(data,lesson,block,same);
    require(data==reviewed&&compiler_puzzle_review(data,lesson,block,same)==feedback,
        "Transient process/revision metadata invalidated an unchanged restored review");
    StateStore store(base/"compiler-review");(void)store.load();store.save(data);const auto reopened=store.load();
    require(reopened==data&&compiler_puzzle_review(reopened,lesson,block,captured)==feedback,
        "A real state-store reopen lost the exact rejection or changed protected data");
    ++groups;
    for(const auto mode:{0,1,2,3,4}){
        auto unavailable=wrong;
        if(mode==0)unavailable.source_revision=6;
        if(mode==1)unavailable.compilation_succeeded=false;
        if(mode==2)unavailable.execution_succeeded=false;
        if(mode==3)unavailable.exit_code=1;
        if(mode==4)unavailable.standard_input="unregistered input";
        auto attempt=reviewed;const auto before=attempt;
        require(!record_compiler_puzzle_review(attempt,lesson,block,captured,unavailable,feedback)&&attempt==before,
            "A stale, failed or unavailable execution replaced a reviewed rejection");
    }
    auto correct=wrong;correct.standard_output="2";
    auto attempt=reviewed;const auto before_correct=attempt;
    require(!record_compiler_puzzle_review(attempt,lesson,block,captured,correct,feedback)&&attempt==before_correct,
        "A correct result was stored as a rejected review");
    auto mismatched_capture=captured;mismatched_capture.stdin_utf8="2";
    require(!record_compiler_puzzle_review(attempt,lesson,block,mismatched_capture,wrong,feedback)&&attempt==before_correct,
        "An execution was attributed to a different captured stdin");
    require(!record_compiler_puzzle_review(attempt,lesson,block,captured,wrong,"")&&attempt==before_correct,
        "An empty feedback payload replaced a resumable reviewed attempt");
    for(const auto suffix:{"program","input","target","rules","feedback","seal"})for(const auto damage:{0,1,2}){
        auto corrupt=reviewed;auto& values=corrupt.preferences["native_windows"];
        const auto entry=key+"review/"+suffix;
        if(damage==0)values.erase(entry);
        if(damage==1)values[entry]=std::string("tampered");
        if(damage==2)values[entry]=std::int64_t{17};
        const auto before=corrupt;
        require(compiler_puzzle_review(corrupt,lesson,block,captured).empty()&&corrupt==before,
            "A missing, mistyped or tampered review field restored feedback or mutated its stored evidence");
    }
    auto other_lesson=lesson;other_lesson.pages[0].blocks[0].output_check->cases[0].outputs={"new objective"};
    const auto before_objective=reviewed;
    require(compiler_puzzle_review(reviewed,other_lesson,other_lesson.pages[0].blocks[0],captured).empty()
        &&reviewed==before_objective,"A changed compiler objective reused or rewrote an older rejection");
    auto new_guide=lesson;
    new_guide.pages[0].blocks[0].output_check->cases[0].review_feedback.push_back({{"otro resultado"},"Guía curricular actualizada."});
    require(reference_objective_frame(new_guide,new_guide.pages[0].blocks[0])==reference_objective_frame(lesson,block)
        &&compiler_puzzle_review(reviewed,new_guide,new_guide.pages[0].blocks[0],captured).empty()&&reviewed==before_objective,
        "Updating only a pedagogical guide reused obsolete rejection or changed successful-receipt identity/data");
    for(const auto source_changed:{false,true}){
        auto changed=captured;
        if(source_changed)changed.source_utf8+="// edited\n";else changed.stdin_utf8="2";
        auto value=reviewed;require(compiler_puzzle_review(value,lesson,block,changed).empty(),
            "A changed source or exact stdin restored another execution's review");
        invalidate_compiler_puzzle_attempt(value,lesson,block,changed);
        require(compiler_puzzle_review(value,lesson,block,captured).empty()
            &&compiler_puzzle_case_count(value,lesson,block)==0,"Undo resurrected an invalidated reviewed rejection");
        for(const auto suffix:{"program","input","target","rules","feedback","seal"})
            require(!find(value,"native_windows",key+"review/"+suffix),"Invalidation retained a stale review field");
    }
    ++groups;
    auto project=captured;
    project.project_files={{"CMakeLists.txt","cmake_minimum_required(VERSION 3.30)\nproject(QA LANGUAGES CXX)\nadd_executable(alpha main.cpp)\n"},
        {"main.cpp","int main(){return 0;}\n"}};
    project.source_files={"main.cpp"};project.cmake=CmakeConfig{"alpha",{},"Debug"};
    auto project_wrong=wrong;project_wrong.executable="alpha";StateData project_data=original;
    require(record_compiler_puzzle_review(project_data,lesson,block,project,project_wrong,feedback),
        "A matching synthetic project target could not retain its evaluated rejection");
    const auto saved_project=project_data;
    for(const auto mode:{0,1,2,3}){
        auto changed=project;
        if(mode==0)changed.cmake->target="beta";
        if(mode==1)changed.cmake->options["QA_OPTION"]="ON";
        if(mode==2)changed.cmake->build_type="Release";
        if(mode==3)changed.project_files[1].second+="// changed header/source\n";
        auto value=saved_project;
        require(compiler_puzzle_review(value,lesson,block,changed).empty(),
            "A changed target, project configuration or source reused another reviewed attempt");
        invalidate_compiler_puzzle_attempt(value,lesson,block,changed);
        require(compiler_puzzle_review(value,lesson,block,project).empty(),"Project Undo restored a retired rejection");
    }
    auto wrong_target=project_wrong;wrong_target.executable="beta";
    const auto before_target=project_data;
    require(!record_compiler_puzzle_review(project_data,lesson,block,project,wrong_target,feedback)&&project_data==before_target,
        "An execution of another CMake target was recorded for the captured target");
    auto no_target=wrong;no_target.executable.reset();auto single_data=original;
    require(record_compiler_puzzle_review(single_data,lesson,block,captured,no_target,feedback),
        "A valid single-file rejection incorrectly required a nonexistent executable target");
    ++groups;
    auto partial=original;
    require(!record_compiler_puzzle_case(partial,lesson,block,captured,correct,"Correcto")
        &&compiler_puzzle_case_count(partial,lesson,block)==1,"Synthetic partial-case setup did not retain exactly one success");
    auto second=captured;second.stdin_utf8="2";auto wrong_second=wrong;wrong_second.standard_input="2";
    const auto partial_before=partial;
    require(record_compiler_puzzle_review(partial,lesson,block,second,wrong_second,feedback)
        &&compiler_puzzle_case_count(partial,lesson,block)==1&&!compiler_puzzle_completed(partial,lesson,block),
        "Reviewing a later rejected input destroyed or completed the independent partial success");
    for(const auto& [name,value]:partial_before.preferences.at("native_windows"))
        require(find(partial,"native_windows",name)&&*find(partial,"native_windows",name)==value,
            "A rejected input rewrote an existing partial execution proof");
    auto input_changed=partial;invalidate_compiler_puzzle_attempt(input_changed,lesson,block,captured);
    require(compiler_puzzle_review(input_changed,lesson,block,second).empty()
        &&compiler_puzzle_case_count(input_changed,lesson,block)==1,
        "Changing stdin retained its rejected review or discarded the unchanged program's valid other case");
    auto explicit_clear=partial;clear_compiler_puzzle_review(explicit_clear,lesson,block);
    require(explicit_clear==partial_before,"Clearing only reviewed feedback rewrote a draft or partial receipt");
    auto same_case=partial;
    require(!record_compiler_puzzle_case(same_case,lesson,block,captured,correct,"Correcto")
        &&compiler_puzzle_review(same_case,lesson,block,second).empty()&&compiler_puzzle_case_count(same_case,lesson,block)==1,
        "A genuine correct partial case retained obsolete rejection or falsely completed the objective");
    auto complete=wrong_second;complete.standard_output="4";
    require(record_compiler_puzzle_case(partial,lesson,block,second,complete,"Correcto\nAmbos casos")
        &&compiler_puzzle_review(partial,lesson,block,second).empty()&&compiler_puzzle_case_count(partial,lesson,block)==2
        &&compiler_puzzle_completed(partial,lesson,block),"Completing both unit-level cases retained a rejected review or lost sealed success");
    const auto sealed=partial;
    require(record_compiler_puzzle_review(partial,lesson,block,second,wrong_second,feedback)
        &&compiler_puzzle_review(partial,lesson,block,second)==feedback
        &&compiler_puzzle_completed(partial,lesson,block)&&compiler_puzzle_case_count(partial,lesson,block)==2
        &&compiler_puzzle_feedback(partial,lesson,block)=="Correcto\nAmbos casos",
        "A new evaluated rejection after achievement was hidden, or replaced the independent captured success");
    for(const auto& [domain,values]:sealed.preferences)for(const auto& [name,value]:values)
        require(find(partial,domain,name)&&*find(partial,domain,name)==value,
            "A new rejected review rewrote a historical success receipt, draft or unrelated student data");
    store.save(partial);const auto reviewed_after_completion=store.load();
    require(reviewed_after_completion==partial&&compiler_puzzle_review(reviewed_after_completion,lesson,block,second)==feedback
        &&compiler_puzzle_completed(reviewed_after_completion,lesson,block),
        "Reopening a completed objective hid its latest rejected review or lost the independent achievement");
    auto edited=second;edited.source_utf8+="// later draft\n";invalidate_compiler_puzzle_attempt(partial,lesson,block,edited);
    require(partial==sealed&&compiler_puzzle_feedback(partial,lesson,block)=="Correcto\nAmbos casos",
        "Editing after success retained a stale rejected review or changed the independent captured achievement");
    auto reset=reviewed;reset_compiler_puzzle(reset,lesson,block);
    require(compiler_puzzle_review(reset,lesson,block,captured).empty()&&!compiler_puzzle_completed(reset,lesson,block)
        &&compiler_puzzle_case_count(reset,lesson,block)==0,"Explicit Reset retained a rejected review or execution receipt");
    auto reset_expected=original;reset_expected.preferences["native_windows"]={};
    require(reset==reset_expected,"Reset of a reviewed attempt rewrote its draft, selection, input or unrelated response");
    ++groups;
    std::cout<<"PASS compiler review state: synthetic evaluator rejection before/after achievement, exact source/stdin/project/target/objective binding, typed tamper refusal, state-store reopen, scoped invalidation/Reset and unchanged independent partial/sealed success; no compiler execution or audio claimed\n";
}
}
int main(int argc, char** argv) {
    try {
        require(argc==2||(argc==3&&(std::string_view(argv[2])=="--u11-starter"||std::string_view(argv[2])=="--compiler-expansions"||std::string_view(argv[2])=="--u11-consult"||std::string_view(argv[2])=="--compiler-reviews"))
            ||(argc==4&&(std::string_view(argv[2])=="--remainder-editorial"||std::string_view(argv[2])=="--u11-names"))
            ||(argc==5&&std::string_view(argv[2])=="--u11-assignment-editorial"),
            "Supply an isolated output/QA directory, optional --u11-starter, --compiler-expansions, --u11-consult, --compiler-reviews, --remainder-editorial <before fixture>, --u11-names <before fixture> or --u11-assignment-editorial <published before> <published after>");
        const auto base = std::filesystem::absolute(argv[1]) /
            ("state-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        require(!std::filesystem::exists(base), "QA directory must not preexist");
        std::filesystem::create_directories(base);
        if(argc==3){if(std::string_view(argv[2])=="--u11-consult")reviewed_choice_revisions_preserve_responses(base,true);
            else if(std::string_view(argv[2])=="--compiler-reviews")rejected_compiler_reviews_preserve_captured_attempts(base);
            else if(std::string_view(argv[2])=="--compiler-expansions")reviewed_compiler_expansions_preserve_execution_history(base);
            else second_u11_compiler_starter_preserves_independent_drafts_and_receipts(base);return 0;}
        if(argc==4){if(std::string_view(argv[2])=="--u11-names")u11_names_preserves_historical_and_added_prediction_state(base,argv[3]);
            else remainder_editorial_preserves_native_and_imported_state(base,argv[3]);return 0;}
        if(argc==5){published_u11_assignment_editorial_preserves_imported_state(base,argv[3],argv[4]);return 0;}
        refined_order_state_tests(base);
        all_authored_refined_orders_preserve_state(base);
        reviewed_order_equivalences_preserve_history(base);
        reviewed_choice_revisions_preserve_responses(base);
        reviewed_compiler_expansions_preserve_execution_history(base);
        late_compiler_batteries_keep_one_historical_execution_receipt(base);
        second_u11_compiler_starter_preserves_independent_drafts_and_receipts(base);
        reviewed_u11_math_label_preserves_native_and_imported_state(base);
        remainder_editorial_preserves_native_and_imported_state(base,std::filesystem::path(LGAED_WORK_EXACT_EDITORIAL_DIR)/"remainder-before.json");
        u11_names_preserves_historical_and_added_prediction_state(base,LGAED_U11_NAMES_BEFORE_FILE);
        diagnostic_contracts_preserve_native_and_imported_state(base);
        rejected_compiler_reviews_preserve_captured_attempts(base);
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
        navigation_only.preferences["native_windows"]["navigation/lesson"] = std::string("cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida");
        require(can_import_android_profile({}) && can_import_android_profile(navigation_only), "Untouched/catalog/active-lesson navigation cannot import");
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
        const auto refused_native_state = [&](const StateData& blocked, std::string_view name) {
            require(!can_import_android_profile(blocked), "A non-navigation native key or malformed active lesson qualified for import");
            const auto target = base / ("blocked-native-" + std::string(name));
            StateStore seed(target); (void)seed.load(); seed.save(blocked);
            const auto before = sha256(read_bytes(target / "state/active/state.bin"));
            rejects([&] { commit_android_import(preview,target); });
            require(sha256(read_bytes(target / "state/active/state.bin")) == before && seed.load() == blocked,
                "Refusing a native key or malformed navigation value changed previous state");
        };
        std::size_t refused_key = 0;
        for (const auto blocked_key : {"navigation/otro","navigation/lesson-extra","scroll/qa/page_0000",
                "compiler-puzzle/v1/qa/program","reference-proof/v1/es/programming/test/0/0"}) {
            auto unknown_ui = navigation_only;
            unknown_ui.preferences["native_windows"][blocked_key] = std::string("not navigation-only data");
            refused_native_state(unknown_ui,"key-" + std::to_string(refused_key++));
        }
        const std::vector<Value> malformed_lessons{std::int64_t{7},0.5,true,std::vector<std::string>{"lesson"}};
        for (std::size_t index = 0; index < malformed_lessons.size(); ++index) {
            auto malformed = navigation_only;
            malformed.preferences["native_windows"]["navigation/lesson"] = malformed_lessons[index];
            refused_native_state(malformed,"lesson-value-" + std::to_string(index));
        }
        ++groups;

        const auto navigated_target = base / "navigated-import-profile";
        StateStore navigated_seed(navigated_target); (void)navigated_seed.load(); navigated_seed.save(navigation_only);
        commit_android_import(preview,navigated_target);
        auto merged_expected = preview.data; merged_expected.preferences["native_windows"] = navigation_only.preferences.at("native_windows");
        StateStore navigated_store(navigated_target);
        require(navigated_store.load() == merged_expected, "Import after browsing lost UI preferences or changed imported progress/drafts");
        require(string_value(navigated_store.load(),"native_windows","navigation/lesson")
            == string_value(navigation_only,"native_windows","navigation/lesson"),
            "Import changed the exact active-lesson navigation destination");
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

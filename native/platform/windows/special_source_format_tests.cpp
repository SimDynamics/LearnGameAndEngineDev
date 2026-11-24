// Authenticated CRT/intentional-exit formatting: both Windows loaders, real
// MSVC executions and isolated durable state. No desktop/audio claim is made.
#include "core/code_format_compat.hpp"
#include "core/lesson.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
std::string phase="setup";
std::uint64_t sequence=900000;
unsigned executions=0;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(phase+": "+std::string(message));
}
std::string text(const JsonObject& value,const wchar_t* key) {
    return winrt::to_string(value.GetNamedString(key));
}
std::string read(const std::filesystem::path& path) {
    const auto raw=state::read_bytes(path);return {raw.begin(),raw.end()};
}
std::string canonical(std::string_view raw) {
    return winrt::to_string(JsonObject::Parse(winrt::to_hstring(raw)).Stringify());
}
std::filesystem::path inside(const std::filesystem::path& directory,std::string_view raw) {
    const std::filesystem::path relative(raw);
    require(!relative.empty()&&!relative.is_absolute(),"A fixture path is not relative");
    for(const auto& component:relative)require(component!=L"..","A fixture path leaves its directory");
    return directory/relative;
}
const Block& owner(const Lesson& lesson,std::string_view id) {
    if(const auto at=stable_block_position(lesson,id))return lesson.pages.at(at->page).blocks.at(at->block);
    const auto retired=std::ranges::find(lesson.retired_blocks,id,&Block::stable_id);
    require(retired!=lesson.retired_blocks.end(),"An authenticated owner is absent from active and retired blocks");
    return *retired;
}
struct Pin {
    std::string_view id,file,owner,publication,source,formatted,execution_owner,execution_source;
    std::uint32_t exit_code;
};
constexpr std::array pins{
    Pin{"crt","lesson_clasificar_caracteres_y_normalizar_una_copia.json","block_0005_0004",
        "c81c2bdc6faf28736d5208183ca8ca224c54813fc74b3e79127251aa93d1e44f",
        "5b5d9a36e9c9b94ae1719daa09ded0942108b6942c82aa0ccc4580f1da2d8584",
        "ca4a330de7d236469a38fb44b209a91c6f97b1ab776aff195a9305d8f59c14e4",
        "block_0008_0001","fe09c9d9b33431c7f66f2f8be5ea6839c4c810cd6a1ea65c9efba67e6401d1d6",0},
    Pin{"exit_stream","lesson_compartir_flujos_sin_acoplar_el_calculo.json","block_0005_0002",
        "793b441f408f7645fc2b22024cfb8d10bfe28c5d1afc07312ea52969532a206d",
        "b8c398d5b6e0bd49a62eb13e3fa1a28733c4ca3541ebdd6ae5187c9b0b765d58",
        "96cd7102c71d677f70372528dd11c1edd1a613c42a415f8f98a1a61f006f59a1",
        "block_0005_0002","b8c398d5b6e0bd49a62eb13e3fa1a28733c4ca3541ebdd6ae5187c9b0b765d58",1},
    Pin{"exit_guard","lesson_construir_un_programa_de_carga_y_guardado.json","block_0004_0017",
        "73eb2577ac3ed1622aeee92ed54204994927309a7c3c8d4cb0b3daaa4e83f18f",
        "55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504",
        "fdb6f6cbf4867436537644eedd340fa691d9ec777d7b617ad93c718bca05e347",
        "block_0004_0017","55e1e30b1c83b9c85cc9de03d6dedec3c310d6b1f2ea098547807aedb348b504",1}
};
const Pin& pin_for(std::string_view id) {
    const auto found=std::ranges::find(pins,id,&Pin::id);
    require(found!=pins.end(),"Unknown special-source publication");return *found;
}
struct Fixture {
    const Pin* pin;
    JsonObject declaration;
    std::filesystem::path directory;
    std::string before_raw,after_raw,execution_source;
    Lesson before,after,metadata;
};
void verify_contract(const Fixture& fixture,const Lesson& lesson) {
    const auto& pin=*fixture.pin;
    const auto& old=owner(fixture.before,pin.owner);
    const auto& current=owner(lesson,pin.owner);
    require(old.output_check&&current.output_check&&current.output_check->cases.size()==1&&
            current.output_check->input_comparison==TextComparison::exact&&
            current.output_check->cases.front().standard_input.empty()&&
            current.output_check->cases.front().exit_code==pin.exit_code,
        "Formatting changed the exact one-case output/exit contract");
    auto previous=JsonObject::Parse(winrt::to_hstring(old.raw_json));
    auto revised=JsonObject::Parse(winrt::to_hstring(current.raw_json));
    revised.Insert(L"initialCode",previous.GetNamedValue(L"initialCode"));
    require(previous.Stringify()==revised.Stringify()&&
            current.initial_code!=old.initial_code&&
            code_format::equivalent(old.initial_code,current.initial_code,code_format::Language::cpp)&&
            current.source_format_original_initial_code==old.initial_code&&
            old.address.page==current.address.page&&old.address.block==current.address.block&&
            draft_key("programming",fixture.before,old)==draft_key("programming",lesson,current)&&
            reference_objective_frame(fixture.before,old)==reference_objective_frame(lesson,current),
        "Formatting lost canonical source, exact owner, public payload or completion identity");
    const auto& outputs=current.output_check->cases.front().outputs;
    if(pin.id=="crt") {
        require(old.crt_output_original&&current.crt_output_original==old.crt_output_original&&
                outputs.size()==2&&outputs.front()==*current.crt_output_original&&
                outputs.front().size()==802&&outputs.back().size()==844&&
                std::ranges::count(outputs.front(),'\n')==42&&std::ranges::count(outputs.front(),'\r')==3,
            "Formatting lost the authenticated CRT alias or changed original control bytes");
        std::string expanded;
        for(const char c:outputs.front()){if(c=='\n')expanded+='\r';expanded+=c;}
        require(outputs.back()==expanded,"Formatting expanded something other than LF in the CRT output");
    } else require(outputs==std::vector<std::string>{pin.id=="exit_stream"?
            "Entrada invalida|9:4":"Error al abrir salida"}&&!current.crt_output_original,
        "Formatting changed the exact intentional exit stdout or acquired an unrelated CRT alias");
}
void verify_history(const Fixture& fixture) {
    const auto& before=fixture.before.state_layout;
    const auto& after=fixture.after.state_layout;
    require(before.signatures==after.signatures&&before.lineage==after.lineage&&
            after.source_format_revisions.size()==before.source_format_revisions.size()+1,
        "Formatting replaced immutable state lineage or existing source archives");
    for(std::size_t i=0;i<before.source_format_revisions.size();++i) {
        const auto& a=before.source_format_revisions[i];const auto& b=after.source_format_revisions[i];
        require(a.id==b.id&&a.sha256==b.sha256&&canonical(a.raw_json)==canonical(b.raw_json),
            "An existing source-format archive changed");
    }
    const auto original=JsonObject::Parse(winrt::to_hstring(before.raw_json));
    auto updated=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    auto editorial=updated.GetNamedArray(L"editorial");
    require(editorial.Size()==original.GetNamedArray(L"editorial").Size()+1,
        "Formatting added unrelated editorial history");
    editorial.RemoveAt(editorial.Size()-1);
    auto formats=updated.GetNamedArray(L"sourceFormatRevisions");
    require(text(formats.GetObjectAt(formats.Size()-1),L"id")==fixture.pin->owner,
        "The appended format archive belongs to another owner");
    formats.RemoveAt(formats.Size()-1);
    if(!original.HasKey(L"sourceFormatRevisions"))updated.Remove(L"sourceFormatRevisions");
    require(updated.Stringify()==original.Stringify(),"Formatting changed an old metadata field or editorial edge");
}
std::vector<Fixture> load_fixtures(const std::filesystem::path& manifest) {
    const auto document=JsonObject::Parse(winrt::to_hstring(read(manifest)));
    require(document.GetNamedNumber(L"version")==1&&text(document,L"kind")=="special-source-format"&&
            text(document,L"publishedCommit")=="08a11877a2d2c0bf96a846660e3baea69e53f758",
        "The special-source fixture is not its audited publication");
    std::set<std::string> names;std::vector<Fixture> result;
    for(const auto& raw:document.GetNamedArray(L"lessons")) {
        const auto item=raw.GetObject();const auto id=text(item,L"id");phase=id+"/loading";
        require(names.insert(id).second,"Repeated special-source lesson");
        const auto& pin=pin_for(id);
        require(text(item,L"owner")==pin.owner&&text(item,L"sourcePath")=="/initialCode"&&
                text(item,L"publishedPath")==std::string("app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/")+std::string(pin.file),
            "A fixture changed its authenticated path or owner");
        Fixture fixture{&pin,item,manifest.parent_path(),read(inside(manifest.parent_path(),text(item,L"before"))),
            read(inside(manifest.parent_path(),text(item,L"after"))),{},{},{},{}};
        require(state::sha256(fixture.before_raw)==pin.publication&&text(item,L"beforeSha256")==pin.publication&&
                state::sha256(fixture.after_raw)==text(item,L"afterSha256"),"A complete fixture differs from its independent pin");
        fixture.before=parse_lesson_json(fixture.before_raw,"es");
        fixture.after=parse_lesson_json(fixture.after_raw,"es");
        fixture.metadata=parse_lesson_progress_metadata(fixture.after_raw,"es");
        const auto& old=owner(fixture.before,pin.owner);const auto& current=owner(fixture.after,pin.owner);
        const auto old_metadata=parse_lesson_progress_metadata(fixture.before_raw,"es");
        require(old.crt_output_original==owner(old_metadata,pin.owner).crt_output_original&&
                state::sha256(old.initial_code)==pin.source&&state::sha256(current.initial_code)==pin.formatted&&
                text(item,L"source")==old.initial_code&&text(item,L"formattedSource")==current.initial_code,
            "Both loaders or manifest disagree about independently pinned original/formatted sources");
        const auto execution=item.GetNamedObject(L"execution");
        fixture.execution_source=text(execution,L"source");
        const auto& authentic=owner(fixture.before,pin.execution_owner);
        require(text(execution,L"sourceOwner")==pin.execution_owner&&
                text(execution,L"sourcePath")==(pin.id=="crt"?"/code":"/initialCode")&&
                state::sha256(fixture.execution_source)==pin.execution_source&&
                text(execution,L"sourceSha256")==pin.execution_source&&
                fixture.execution_source==(pin.id=="crt"?authentic.code:authentic.initial_code),
            "The execution source differs from its complete authenticated reference");
        verify_history(fixture);verify_contract(fixture,fixture.after);verify_contract(fixture,fixture.metadata);
        result.push_back(std::move(fixture));
    }
    require(names==std::set<std::string>{"crt","exit_stream","exit_guard"},"The fixture must cover all three special owners");
    return result;
}
struct Capture { CppRunRequest request;CppRunResult result;FreshExecution execution; };
Capture run_source(const WindowsEnvironment& environment,const Lesson& lesson,const Block& block,std::string source) {
    Capture capture;auto& request=capture.request;
    request.request_id=++sequence;request.revision=sequence;
    request.project_key=draft_key("programming",lesson,block);
    request.projects_directory=environment.paths.projects/L"special-source-format";
    request.source_utf8=std::move(source);request.stdin_utf8=block.output_check->cases.front().standard_input;
    request.compile_timeout=45s;request.execution_timeout=5s;
    require(compiler_runtime_request_allowed(block,request),"The authentic special goal rejected its runtime request");
    ++executions;capture.result=run_cpp(request);capture.execution=capture_cpp_execution(request,capture.result);
    require(capture.result.error_utf8.empty()&&capture.result.compilation.succeeded()&&
            capture.result.execution.status==CppPhaseStatus::completed&&capture.execution.execution_completed&&
            capture.execution.standard_error.empty(),
        "The authentic special program failed compilation or execution: "+capture.result.error_utf8+
            capture.result.compilation.stdout_bytes+capture.result.compilation.stderr_bytes);
    require(read(capture.result.run_directory/L"main.cpp")==request.source_utf8&&
            read(capture.result.run_directory/L"stdin.bin")==request.stdin_utf8&&
            read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output&&
            read(capture.result.run_directory/L"execution-stderr.bin")==capture.execution.standard_error,
        "The native runner changed the program, stdin or raw output files");
    return capture;
}
bool accepted(const Block& block,const Capture& capture) {
    return evaluate_cpp_output_check(*block.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::correct;
}
void seed(state::StateData& data,const Fixture& fixture) {
    state::prepare_lesson_state(data,fixture.before);
    for(const auto& page:fixture.before.pages)for(const auto& block:page.blocks)if(block.type==BlockType::cpp_compiler) {
        state::Draft draft;draft.source=block.initial_code+"\n// borrador QA independiente\r\n";
        draft.input="\t7\r\n";draft.selection_start=3;draft.selection_end=9;draft.has_saved=true;
        draft.indentation_json=state::make_indentation_metadata(draft.source,{});
        state::put_draft(data,draft_key("programming",fixture.before,block),draft);
    }
    data.preferences["special_source_format_qa"]["text"]=std::string(" conservar\t\r\n ");
    data.preferences["special_source_format_qa"]["enabled"]=true;
    data.preferences["special_source_format_qa"]["count"]=std::int64_t{37};
    state::save_current_page(data,fixture.before,stable_block_position(fixture.before,fixture.pin->owner)->page);
}
void require_preserved(const state::StateData& original,const state::StateData& current,const Lesson& lesson) {
    const auto prefix="lesson_"+lesson.id;
    const auto allowed=[&](std::string_view domain,std::string_view key) {
        return domain=="learning_progress"&&(key==prefix+"_state_retired_v2_"+lesson.locale||
            key==prefix+"_state_retirement_history_v2_"+lesson.locale);
    };
    for(const auto& [domain,values]:original.preferences)for(const auto& [key,value]:values) {
        const auto* now=state::find(current,domain,key);require(now!=nullptr,"Adoption erased a saved value");
        if(!allowed(domain,key))require(*now==value,"Adoption rewrote a receipt, source, cursor, answer or preference");
        else {
            const auto previous=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(value)));
            const auto revised=JsonArray::Parse(winrt::to_hstring(std::get<std::string>(*now)));
            require(revised.Size()>=previous.Size(),"Adoption truncated published retirement history");
            for(unsigned i=0;i<previous.Size();++i)require(previous.GetAt(i).Stringify()==revised.GetAt(i).Stringify(),
                "Adoption rewrote published retirement provenance");
        }
    }
    for(const auto& [domain,values]:current.preferences)for(const auto& [key,value]:values) {
        (void)value;
        if(!state::find(original,domain,key))require(allowed(domain,key),"Adoption created an unrelated value or new receipt");
    }
}
std::string xml_escape(std::string_view source) {
    std::string result;
    for(const char c:source) {
        if(c=='\r')result+="&#13;";else if(c=='\n')result+="&#10;";else if(c=='\t')result+="&#9;";
        else if(c=='&')result+="&amp;";else if(c=='<')result+="&lt;";else if(c=='>')result+="&gt;";
        else if(c=='"')result+="&quot;";else result+=c;
    }return result;
}
void export_preferences(const std::filesystem::path& root,const state::StateData& data) {
    std::filesystem::create_directories(state::native_io_path(root/"shared_prefs"));
    for(const auto& [domain,values]:data.preferences) {
        std::string xml="<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for(const auto& [key,value]:values)std::visit([&](const auto& item) {
            using T=std::decay_t<decltype(item)>;const auto name=" name=\""+xml_escape(key)+"\"";
            if constexpr(std::is_same_v<T,std::string>)xml+="<string"+name+">"+xml_escape(item)+"</string>";
            else if constexpr(std::is_same_v<T,bool>)xml+="<boolean"+name+" value=\""+(item?"true":"false")+"\"/>";
            else if constexpr(std::is_same_v<T,std::int64_t>)xml+="<long"+name+" value=\""+std::to_string(item)+"\"/>";
            else if constexpr(std::is_same_v<T,std::vector<std::string>>) {
                xml+="<set"+name+">";for(const auto& entry:item)xml+="<string>"+xml_escape(entry)+"</string>";xml+="</set>";
            }else throw std::runtime_error("Unsupported isolated import preference type");
        },value);
        xml+="</map>";std::ofstream stream(state::native_io_path(root/"shared_prefs"/(domain+".xml")),std::ios::binary);
        stream.write(xml.data(),static_cast<std::streamsize>(xml.size()));require(stream.good(),"Could not export isolated preferences");
    }
}
void verify_import(const Fixture& fixture,const state::StateData& historical,const std::filesystem::path& qa) {
    // Android transport carries legacy typed preferences/drafts only. Native
    // execution receipts remain in the separate real StateStore test below.
    auto source=historical;source.preferences.erase("native_windows");
    const auto exported=qa/L"legacy-export";export_preferences(exported,source);
    const auto preview=state::inspect_android_export(exported);
    require(preview.data==source,"Windows import changed the original typed legacy preferences");
    const auto destination=qa/L"imported";state::commit_android_import(preview,destination);
    state::StateData saved;
    {
        state::StateStore store(destination);saved=store.load();require(saved==source,"Import changed its inspected data");
        state::prepare_lesson_state(saved,fixture.before);const auto previous=saved;
        state::prepare_lesson_state(saved,fixture.after);require_preserved(previous,saved,fixture.after);
        state::validate_lesson_state(saved,fixture.after);
        require(!compiler_puzzle_completed(saved,fixture.after,owner(fixture.after,fixture.pin->owner)),
            "Imported legacy draft metadata manufactured a native compiler achievement");
        store.save(saved);
    }
    state::StateStore reopened(destination);require(reopened.load()==saved,"Imported state changed on physical reopen");
    for(const auto& file:preview.files)require(state::sha256(state::read_bytes(exported/file.relative_path))==file.sha256,
        "Windows import modified an original exported file");
}
void verify_invalid_and_retired(const Fixture& fixture,const state::StateData& earned) {
    const auto& pin=*fixture.pin;
    const auto negative=fixture.declaration.GetNamedArray(L"negative");
    const auto historical=fixture.declaration.GetNamedArray(L"historical");
    require(negative.Size()==(pin.id=="crt"?10u:8u)&&historical.Size()==3,
        "Special-format QA lacks its exact tampering and historical scenario census");
    std::set<std::string> negative_names,historical_names;
    for(const auto& raw:negative) {
        const auto entry=raw.GetObject();phase=std::string(pin.id)+"/reject/"+text(entry,L"name");
        require(negative_names.insert(text(entry,L"name")).second,"A negative scenario was repeated");
        const auto payload=read(inside(fixture.directory,text(entry,L"path")));
        require(state::sha256(payload)==text(entry,L"sha256"),"A negative fixture changed after export");
        bool full=false,metadata=false;
        try{(void)parse_lesson_json(payload,"es");}catch(const std::exception&){full=true;}catch(const winrt::hresult_error&){full=true;}
        try{(void)parse_lesson_progress_metadata(payload,"es");}catch(const std::exception&){metadata=true;}catch(const winrt::hresult_error&){metadata=true;}
        require(full&&metadata,"An invalid token, owner, output, exit or provenance was accepted by a Windows loader");
    }
    for(const auto& raw:historical) {
        const auto entry=raw.GetObject();phase=std::string(pin.id)+"/history/"+text(entry,L"name");
        require(historical_names.insert(text(entry,L"name")).second,"A historical scenario was repeated");
        const auto payload=read(inside(fixture.directory,text(entry,L"path")));
        require(state::sha256(payload)==text(entry,L"sha256"),"A historical fixture changed after export");
        const auto full=parse_lesson_json(payload,"es"),metadata=parse_lesson_progress_metadata(payload,"es");
        for(const auto* lesson:{&full,&metadata}) {
            verify_contract(fixture,*lesson);
            auto adopted=earned;state::prepare_lesson_state(adopted,*lesson);
            require_preserved(earned,adopted,*lesson);state::validate_lesson_state(adopted,*lesson);
            const auto& block=owner(*lesson,pin.owner);
            require(compiler_puzzle_completed(adopted,*lesson,block)&&compiler_puzzle_case_count(adopted,*lesson,block)==1,
                "The historical format/retirement path lost a real native achievement");
        }
    }
    require(historical_names==std::set<std::string>{"format_after_retirement","retirement_after_format","reformat_after_retirement"},
        "Historical QA omitted a required format/retirement ordering");
    std::cout<<"PASS special-source provenance "<<pin.id<<": "<<negative.Size()<<" negatives and "
        <<historical.Size()<<" authenticated histories in both Windows loaders.\n";
}
void exercise(const Fixture& fixture,const WindowsEnvironment& environment) {
    const auto& pin=*fixture.pin;const auto& old=owner(fixture.before,pin.owner);
    const auto& current=owner(fixture.after,pin.owner);const auto& projected=owner(fixture.metadata,pin.owner);
    phase=std::string(pin.id)+"/native original and formatted source";
    const auto original=run_source(environment,fixture.before,old,old.initial_code);
    const auto formatted=run_source(environment,fixture.after,current,current.initial_code);
    require(original.execution.standard_output==formatted.execution.standard_output&&
            original.execution.exit_code==formatted.execution.exit_code&&
            accepted(old,original)==accepted(current,formatted),"Physical formatting changed the actual program behavior");
    Capture original_success=original,current_success=formatted;
    if(pin.id=="crt") {
        require(!accepted(old,original)&&!accepted(current,formatted),"An empty CRT starter earned the complete report goal");
        original_success=run_source(environment,fixture.before,old,fixture.execution_source);
        current_success=run_source(environment,fixture.after,current,fixture.execution_source);
        require(current_success.execution.standard_output==current.output_check->cases.front().outputs.back(),
            "The authentic CRT reference did not retain its exact 844 native bytes");
    }
    require(accepted(old,original_success)&&accepted(current,current_success)&&
            original_success.execution.exit_code==pin.exit_code&&current_success.execution.exit_code==pin.exit_code,
        "The original and formatted objective disagree about their authentic successful program");
    const auto qa=environment.paths.root/("special-format-"+std::string(pin.id));
    state::StateData legacy;seed(legacy,fixture);
    const auto no_credit=legacy;auto merely_formatted=legacy;state::prepare_lesson_state(merely_formatted,fixture.after);
    require_preserved(legacy,merely_formatted,fixture.after);
    require(!compiler_puzzle_completed(merely_formatted,fixture.after,current),"Formatting manufactured unearned compiler credit");
    require(record_compiler_puzzle_case(legacy,fixture.before,old,original_success.request,original_success.execution,
            "Correcto\n"+old.output_check->cases.front().explanation)&&compiler_puzzle_completed(legacy,fixture.before,old),
        "The actual original execution failed to earn its historical achievement");
    const auto original_receipts=legacy;
    {
        state::StateStore store(qa/L"native");require(store.load().preferences.empty(),"The isolated native profile is not new");store.save(legacy);
    }
    {
        state::StateStore store(qa/L"native");auto restored=store.load();require(restored==legacy,"Physical reopening changed original state");
        state::prepare_lesson_state(restored,fixture.after);require_preserved(original_receipts,restored,fixture.after);
        state::validate_lesson_state(restored,fixture.after);
        require(compiler_puzzle_completed(restored,fixture.after,current)&&compiler_puzzle_completed(restored,fixture.metadata,projected)&&
                compiler_puzzle_case_count(restored,fixture.after,current)==1,
            "The exact first-case achievement was lost during formatting or metadata loading");
        const auto feedback=compiler_puzzle_success_feedback(restored,fixture.after,current);
        require(feedback&&feedback->correct&&feedback->text=="Correcto\n"+current.output_check->cases.front().explanation,
            "Formatting lost or rewrote the earned authored feedback");
        store.save(restored);legacy=restored;
    }
    {state::StateStore store(qa/L"native");require(store.load()==legacy,"Adopted state changed on a second physical reopen");}
    auto fresh=no_credit;state::prepare_lesson_state(fresh,fixture.after);
    require(record_compiler_puzzle_case(fresh,fixture.after,current,current_success.request,current_success.execution,
            "Correcto\n"+current.output_check->cases.front().explanation)&&
            compiler_puzzle_completed(fresh,fixture.before,old)&&compiler_puzzle_completed(fresh,fixture.after,current)&&
            compiler_puzzle_case_count(fresh,fixture.after,current)==1,
        "A fresh formatted execution failed to earn the same original objective with one public case");
    if(pin.exit_code==1) {
        auto zero_source=current.initial_code;const std::string token="int main()";
        const auto at=zero_source.find(token);require(at!=std::string::npos&&zero_source.find(token,at+token.size())==std::string::npos,
            "The authenticated exit source no longer has one main function");
        zero_source.replace(at,token.size(),"int special_format_original_main()");
        zero_source+="\nint main() { special_format_original_main(); return 0; }\n";
        const auto zero=run_source(environment,fixture.after,current,std::move(zero_source));
        require(zero.execution.exit_code==0&&zero.execution.standard_output==current_success.execution.standard_output&&
                !accepted(current,zero),"Correct stdout bypassed the preserved intentional exit code");
        const auto protected_state=fresh;
        require(!record_compiler_puzzle_case(fresh,fixture.after,current,zero.request,zero.execution,"Incorrecto")&&fresh==protected_state,
            "An incorrect normal exit rewrote or earned an achievement");
    }
    verify_import(fixture,no_credit,qa);
    verify_invalid_and_retired(fixture,legacy);
    phase=std::string(pin.id)+"/scoped reset";
    auto reset=legacy;reset_compiler_puzzle(reset,fixture.after,current);
    require(!compiler_puzzle_completed(reset,fixture.before,old)&&!compiler_puzzle_completed(reset,fixture.after,current),
        "Reset left a second achievement for the same formatted objective");
    const auto prefix="compiler-puzzle/v1/"+fixture.before.locale+'/'+fixture.before.id+'/'+exercise_key(fixture.before,old)+'/';
    auto expected=legacy;
    if(const auto domain=expected.preferences.find("native_windows");domain!=expected.preferences.end())
        std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
    require(reset==expected,"Reset removed a draft, other achievement or unrelated preference");
    std::cout<<"PASS special-source native "<<pin.id<<": original/formatted behavior, real first-case receipts in both directions, "
        <<"authored feedback, exact drafts/input/cursors, physical reopen, legacy import without invented credit and scoped Reset.\n";
}
} // namespace

int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    require(argc==2,"Supply the authenticated special-source manifest");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const auto manifest=std::filesystem::path(argv[1]);const auto manifest_bytes=read(manifest);
    const auto fixtures=load_fixtures(manifest);
    const LaunchOptions options{ProfileId::Codex,L"special-source-format-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    for(const auto& fixture:fixtures)exercise(fixture,environment);
    require(executions==10,"Special-source QA omitted or repeated a required native program");
    for(const auto& fixture:fixtures) {
        require(read(inside(fixture.directory,text(fixture.declaration,L"before")))==fixture.before_raw&&
                read(inside(fixture.directory,text(fixture.declaration,L"after")))==fixture.after_raw,
            "Native QA modified a complete authenticated publication");
    }
    require(read(manifest)==manifest_bytes,"Native QA modified its manifest");
    std::cout<<"PASS special-source format: 3 authentic owners; "<<executions
        <<" actual MSVC programs; full/progress loading, durable achievements and verified import. No UI/audio acceptance claimed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL special-source format: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error){std::cerr<<"FAIL special-source Windows "<<phase<<": "<<winrt::to_string(error.message())<<'\n';return 1;}

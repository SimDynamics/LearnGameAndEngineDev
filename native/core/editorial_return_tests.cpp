#include "core/editorial_return_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace winrt::Windows::Data::Json;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action>void rejects(Action action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,message);}
std::string pin_name(const curriculum::Lesson& lesson){return "lesson_"+lesson.id+"_state_editorial_returns_v1_"+lesson.locale;}
std::string proof_pin(const curriculum::Lesson& lesson){
    JsonArray values;const auto root=JsonObject::Parse(winrt::to_hstring(lesson.raw_json));
    for(const auto& proof:root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial"))
        values.Append(JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(proof))));
    return winrt::to_string(values.Stringify());
}
void source_intact(const test::EditorialReturnFixture& fixture){check(state::read_bytes(fixture.source)==fixture.source_bytes,"Editorial QA changed its actual curricular asset");
    check(state::sha256(state::read_bytes(fixture.original_archive))==test::editorial_return_original_sha
        &&state::sha256(state::read_bytes(fixture.intermediate_archive))==test::editorial_return_intermediate_sha,
        "Editorial QA changed an authenticated A/B historical document");}
void write_new(const std::filesystem::path& path,std::string_view text){
    state::require_safe_path(path);check(!std::filesystem::exists(path),"Refusing to overwrite an existing QA file");
    std::filesystem::create_directories(state::native_io_path(path.parent_path()));std::ofstream output(state::native_io_path(path),std::ios::binary);
    output.write(text.data(),static_cast<std::streamsize>(text.size()));check(static_cast<bool>(output),"Cannot save an isolated QA export");
}
std::string xml(std::string_view text){
    std::string value;for(const char c:text)switch(c){case '&':value+="&amp;";break;case '<':value+="&lt;";break;case '>':value+="&gt;";break;
        case '"':value+="&quot;";break;case '\r':value+="&#13;";break;case '\n':value+="&#10;";break;case '\t':value+="&#9;";break;default:value+=c;}return value;
}
void export_state(const std::filesystem::path& root,const state::StateData& data){
    for(const auto& [domain,entries]:data.preferences){std::string document="<?xml version=\"1.0\" encoding=\"utf-8\"?><map>";
        for(const auto& [key,value]:entries)std::visit([&](const auto& item){using T=std::decay_t<decltype(item)>;const auto name=" name=\""+xml(key)+"\"";
            if constexpr(std::is_same_v<T,std::string>)document+="<string"+name+">"+xml(item)+"</string>";
            else if constexpr(std::is_same_v<T,bool>)document+="<boolean"+name+" value=\""+(item?"true":"false")+"\"/>";
            else if constexpr(std::is_same_v<T,std::int64_t>)document+="<long"+name+" value=\""+std::to_string(item)+"\"/>";
            else if constexpr(std::is_same_v<T,double>){std::ostringstream number;number<<std::setprecision(17)<<item;document+="<float"+name+" value=\""+number.str()+"\"/>";}
            else {document+="<set"+name+">";for(const auto& word:item)document+="<string>"+xml(word)+"</string>";document+="</set>";}
        },value);
        write_new(root/"shared_prefs"/(domain+".xml"),document+"</map>");
    }
    write_new(root/"files/cpp_projects/retained.cpp","\t// original project bytes\r\nint main(){}\n");
}
std::map<std::filesystem::path,std::string> file_hashes(const std::filesystem::path& root){
    std::map<std::filesystem::path,std::string> result;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))if(entry.is_regular_file())
        result.emplace(entry.path().lexically_relative(root),state::sha256(state::read_bytes(entry.path())));
    return result;
}
const curriculum::Block& identified(const curriculum::Lesson& lesson,std::string_view identity){
    const auto at=curriculum::stable_block_position(lesson,identity);check(at.has_value(),"A QA state identity disappeared");return lesson.pages[at->page].blocks[at->block];
}
state::StateData snapshot(const curriculum::Lesson& lesson,unsigned mode){
    state::StateData data;state::prepare_lesson_state(data,lesson);state::save_current_page(data,lesson,6);
    auto& opaque=data.preferences["editorial_return_typed_qa"];opaque["text"]=std::string(" español\t\r\n保存 ");opaque["integer"]=std::int64_t{4294967311};
    opaque["fraction"]=1.25;opaque["flag"]=false;opaque["set"]=std::vector<std::string>{"alpha","beta\nline"};
    const auto base="lesson_"+lesson.id;
    data.preferences["learning_progress"][base+"_read_pages_v2_es"]=std::vector<std::string>{"page_0000","page_0001","page_0006"};
    data.preferences["learning_progress"][base+"_read_blocks_v2_es"]=std::vector<std::string>{"block_0000_0000","block_0001_0002","block_0006_0001"};
    data.preferences["native_windows"]["scroll/"+lesson.id+"/page_0006"]=std::int64_t{719};
    if(mode){
        const auto& prediction=identified(lesson,"block_0001_0002"),&choice=identified(lesson,"block_0001_0004");
        state::Response answer;answer.answer=mode==2?prediction.answers.at(0):"\t borrador no enviado ";answer.correct=mode==2;
        answer.raw_json=R"({"opaque":{"source":"preserve original answer"},"incorrectAttempts":4})";
        state::put_response(data,lesson,prediction,answer);
        state::Response selected;selected.selected=static_cast<std::int64_t>(mode==2?*choice.correct_index:(*choice.correct_index+1)%choice.options.size());
        selected.correct=mode==2;selected.raw_json=R"({"opaque":"review metadata retained"})";
        if(mode==1)state::mark_response_reviewed(selected,choice);state::put_response(data,lesson,choice,selected);
        const auto& compiler=identified(lesson,"block_0006_0002");state::Draft draft;
        draft.source="    // personal source\r\nint main(){return 0;}\r\n";draft.input=" \t prepared input\r\n";
        draft.indentation_json=state::make_indentation_metadata(draft.source,{0});draft.selection_start=4;draft.selection_end=19;
        state::put_draft(data,curriculum::draft_key("programming",lesson,compiler),draft);
    }
    // Storage-only markers, with no heard prefix, completed audio or claimed
    // playback. The independent view test obtains real player bookmarks.
    narration::History history;
    for(unsigned at=0;at<3;++at){const auto key="editorial-storage-only-"+std::to_string(at);
        history=narration::begin(std::move(history),key,key,{static_cast<std::int64_t>(at),231+at,0,false},"opaque stable source "+std::to_string(at),"page-"+std::to_string(at));
        history=narration::checkpoint(std::move(history),key,key,{static_cast<std::int64_t>(at),231+at,0,false});}
    narration::write_history(data,history);state::validate_lesson_state(data,lesson);return data;
}
void adopt_exact(state::StateData& data,const curriculum::Lesson& returned){
    const auto before=data;state::validate_lesson_state(data,returned);check(data==before,"Read-only editorial validation changed state");
    state::prepare_lesson_state(data,returned);auto expected=before;
    expected.preferences["learning_progress"][pin_name(returned)]=proof_pin(returned);
    check(data==expected,"Editorial return changed anything beyond its additive complete-history pin");
    const auto adopted=data;state::prepare_lesson_state(data,returned);check(data==adopted,"Editorial return adoption is not idempotent");
    check(state::current_page(data,returned)==6&&narration::read_history(data)==narration::read_history(before),"Editorial return lost page or exact bounded narration markers");
}
void guards(const test::EditorialReturnFixture& fixture){
    rejects([&]{(void)test::editorial_return_historical_lesson(fixture.original_archive,true);},"A was accepted as the independently pinned intermediate B");
    rejects([&]{(void)test::editorial_return_historical_lesson(fixture.intermediate_archive,false);},"B was accepted as the independently pinned original A");
    rejects([&]{(void)test::editorial_return_historical_lesson(fixture.source,true);},"The integrated live C was accepted as historical B");
    using Mutate=std::function<void(JsonObject&)>;
    const auto fail=[&](const Mutate& mutate){auto root=JsonObject::Parse(winrt::to_hstring(fixture.returned.raw_json));mutate(root);const auto raw=winrt::to_string(root.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw,"es");},"Full loader accepted a forged editorial return");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw,"es");},"Catalog metadata accepted a forged editorial return");};
    const auto last=[](JsonObject& root){const auto values=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");return values.GetObjectAt(values.Size()-1);};
    fail([&](JsonObject& root){last(root).Remove(L"returnTo");});
    for(const double revision:{-1.,.5,999.})fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Insert(L"revision",JsonValue::CreateNumberValue(revision));});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Insert(L"historySha256",JsonValue::CreateStringValue(std::wstring(64,L'0')));});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Remove(L"revision");});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Remove(L"previousBlock");});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Insert(L"previousBlock",JsonArray{});});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").GetNamedObject(L"previousBlock").Insert(L"text",JsonValue::CreateStringValue(L"Una presentación anterior falsificada."));});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").GetNamedObject(L"previousBlock").Insert(L"type",JsonValue::CreateStringValue(L"code"));});
    fail([&](JsonObject& root){last(root).GetNamedObject(L"returnTo").Insert(L"extra",JsonValue::CreateBooleanValue(true));});
    fail([&](JsonObject& root){last(root).Insert(L"id",JsonValue::CreateStringValue(L"unknown_identity"));});
    fail([&](JsonObject& root){const auto values=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");values.RemoveAt(0);});
    fail([&](JsonObject& root){const auto values=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");const auto size=values.Size();const auto a=values.GetAt(size-1);values.SetAt(size-1,values.GetAt(size-2));values.SetAt(size-2,a);});
    fail([&](JsonObject& root){const auto values=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");values.Append(JsonObject::Parse(last(root).Stringify()));});
    fail([&](JsonObject& root){last(root).Insert(L"afterSha256",last(root).GetNamedValue(L"beforeSha256"));});
    fail([&](JsonObject& root){root.Insert(L"id",JsonValue::CreateStringValue(L"different_lesson"));});
    // Even a correctly hashed ordinary proof appended after a valid return
    // cannot turn protected source/answers into an editorial-only revision.
    for(const bool source:{false,true})fail([&](JsonObject& root){
        const auto id=source?"block_0006_0002":"block_0001_0002";const auto at=*curriculum::stable_block_position(fixture.returned,id);
        const auto block=root.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at.page)).GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at.block));
        const auto before=test::compile_check_test_fingerprint(block);
        if(source)block.Insert(L"initialCode",JsonValue::CreateStringValue(L"int main(){return 9;}"));
        else block.GetNamedArray(L"answers").SetAt(0,JsonValue::CreateStringValue(L"unreviewed answer"));
        JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(id)));proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(before)));
        proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(block))));
        root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
    });
    std::cout<<"PASS editorial return full-loader/catalog negatives, global history binding and protected source/answer refusal\n";
}
void state_case(const test::EditorialReturnFixture& fixture,const curriculum::Lesson& source,unsigned mode,const std::filesystem::path& base){
    auto data=snapshot(source,mode);const auto previous=data;check(!state::find(data,"learning_progress",pin_name(source)),"Pre-return snapshot already had invented return provenance");
    state::StateStore store(base/"native");(void)store.load();store.save(data);auto reopened=store.load();check(reopened==previous,"Saving the pre-return snapshot changed data");
    adopt_exact(reopened,fixture.returned);store.save(reopened);const auto bytes=state::read_bytes(store.root()/"active/state.bin");
    const auto saved=store.load();check(saved==reopened,"Native reopen lost exact editorial state");
    state::validate_lesson_state(reopened,fixture.returned);state::prepare_lesson_state(reopened,fixture.returned);
    check(reopened==saved&&state::read_bytes(store.root()/"active/state.bin")==bytes,"Read-only repeat changed native state bytes");
    for(const auto* older:{&fixture.original,&fixture.intermediate}){auto rollback=saved;rejects([&]{state::prepare_lesson_state(rollback,*older);},"A pinned return accepted a truncated/older publication");check(rollback==saved,"Rejected editorial rollback partially rewrote state");}
    for(const auto* damage:{"empty","pre-return","invalid","reorder","future","type"}){auto corrupt=saved;const auto key=pin_name(fixture.returned);auto values=JsonArray::Parse(winrt::to_hstring(proof_pin(fixture.returned)));
        if(std::string_view(damage)=="empty")values.Clear();
        else if(std::string_view(damage)=="pre-return"){values.RemoveAtEnd();values.RemoveAtEnd();}
        else if(std::string_view(damage)=="invalid")values.SetAt(0,JsonValue::CreateStringValue(L"forged"));
        else if(std::string_view(damage)=="reorder"){const auto first=values.GetAt(0);values.SetAt(0,values.GetAt(1));values.SetAt(1,first);}
        else if(std::string_view(damage)=="future")values.Append(JsonValue::CreateStringValue(std::wstring(64,L'a')));
        corrupt.preferences["learning_progress"][key]=std::string_view(damage)=="type"?state::Value{std::int64_t{2}}:state::Value{winrt::to_string(values.Stringify())};
        const auto untouched=corrupt;rejects([&]{state::prepare_lesson_state(corrupt,fixture.returned);},"Malformed/truncated/reordered return pin was accepted");check(corrupt==untouched,"Rejecting corrupt return provenance changed student data");}
    // The native-only scroll domain is rejected in an Android-shaped export.
    export_state(base/"reserved-export",previous);rejects([&]{(void)state::inspect_android_export(base/"reserved-export");},"Reserved native scroll state was accepted as Android data");
    auto compatible=previous;compatible.preferences.erase("native_windows");export_state(base/"compatible-export",compatible);
    const auto hashes=file_hashes(base/"compatible-export");const auto preview=state::inspect_android_export(base/"compatible-export");
    check(preview.data==compatible,"Compatible import changed exact typed values, drafts, responses or reading markers");
    state::validate_lesson_state(preview.data,fixture.returned);state::commit_android_import(preview,base/"imported");
    state::StateStore imported(base/"imported");auto imported_data=imported.load();check(imported_data==compatible,"Import transaction changed source preferences");
    adopt_exact(imported_data,fixture.returned);imported.save(imported_data);check(imported.load()==imported_data,"Reopening imported editorial state changed its preserved data");
    check(file_hashes(base/"compatible-export")==hashes,"Editorial import modified its original exported files");
    for(const auto& file:preview.files)check(state::sha256(state::read_bytes(base/"imported/state/active/android"/file.relative_path))==file.sha256,"Import failed to retain an original source file exactly");
    const auto import_bytes=state::read_bytes(base/"imported/state/active/state.bin");rejects([&]{state::commit_android_import(preview,base/"imported");},"Import overwrote an existing profile");
    check(state::read_bytes(base/"imported/state/active/state.bin")==import_bytes,"Rejected repeated import changed committed state");
    // Once adopted, the global pin must also extend for later ORDINARY prose
    // changes. Its protection cannot stop at the last explicit return edge.
    const auto future=JsonObject::Parse(winrt::to_hstring(fixture.returned.raw_json));const auto at=*curriculum::stable_block_position(fixture.returned,"block_0006_0001");
    const auto paragraph=future.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at.page)).GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at.block));
    const auto prior=test::compile_check_test_fingerprint(paragraph);
    paragraph.Insert(L"text",JsonValue::CreateStringValue(paragraph.GetNamedString(L"text")+L"\n\nFrase posterior exclusiva de la prueba QA."));
    JsonObject ordinary;ordinary.Insert(L"id",JsonValue::CreateStringValue(L"block_0006_0001"));ordinary.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(prior)));
    ordinary.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(paragraph))));future.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(ordinary);
    const auto extended=windows::parse_lesson_json(winrt::to_string(future.Stringify()),"es");auto advanced=saved;
    adopt_exact(advanced,extended);const auto retained=advanced;
    rejects([&]{state::prepare_lesson_state(advanced,fixture.returned);},"An ordinary post-return proof disappeared from the adopted global history");
    check(advanced==retained,"Rejecting an ordinary post-return rollback changed data");source_intact(fixture);
    std::cout<<"PASS snapshot="<<(source.raw_json==fixture.original.raw_json?"genuine-A":"current-B")<<" state_mode="<<mode<<" exact native/imported preservation, additive pin and atomic rollback refusal\n";
}
}
int main(){try{
    windows::ensure_winrt_apartment();const auto fixture=test::editorial_return_fixture();
    const auto full=windows::parse_lesson_progress_metadata(fixture.returned.raw_json,"es");
    check(full.id==fixture.returned.id&&fixture.original.state_layout.signatures==fixture.returned.state_layout.signatures
        &&fixture.intermediate.state_layout.signatures==fixture.returned.state_layout.signatures,"The return changed immutable state ownership");
    const auto historical=JsonObject::Parse(winrt::to_hstring(fixture.intermediate.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    const auto integrated=JsonObject::Parse(winrt::to_hstring(fixture.returned.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    check(integrated.Size()==historical.Size()+2,"The live final publication lost the two real return transitions");
    for(unsigned at=0;at<historical.Size();++at)check(test::compile_check_test_fingerprint(historical.GetAt(at))==test::compile_check_test_fingerprint(integrated.GetAt(at)),
        "The integrated return changed its exact independently published B prefix");
    check(fixture.returned.state_layout.editorial_returns.size()==test::editorial_return_cases.size(),"The validated return archive lost an edge");
    for(std::size_t at=0;at<test::editorial_return_cases.size();++at){const auto& archived=fixture.returned.state_layout.editorial_returns[at];
        check(archived.id==test::editorial_return_cases[at].identity&&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(archived.previous_raw_json)))==test::editorial_return_cases[at].before,
            "The validated return archive is not the exact intermediate full payload in global order");}
    guards(fixture);
    const auto base=std::filesystem::path(build::source_root)/"out/qa"/("editorial-return-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    state::require_safe_path(base);check(!std::filesystem::exists(base),"Refusing to reuse an editorial QA directory");std::filesystem::create_directories(state::native_io_path(base));
    for(const auto* source:{&fixture.original,&fixture.intermediate})for(unsigned mode=0;mode<3;++mode)
        state_case(fixture,*source,mode,base/((source==&fixture.original?"A-":"B-")+std::to_string(mode)));
    source_intact(fixture);std::cout<<"PASS six explicit editorial return state/import cases; no compilation, audio playback or curricular acceptance claimed\n";return 0;
}catch(const winrt::hresult_error& error){std::cerr<<"FAIL editorial return: "<<winrt::to_string(error.message())<<'\n';}
 catch(const std::exception& error){std::cerr<<"FAIL editorial return: "<<error.what()<<'\n';}return 1;}

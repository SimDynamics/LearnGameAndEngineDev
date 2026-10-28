#include "core/attempts.hpp"
#include "core/lesson.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <richedit.h>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;

void check(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(std::string(message));
}
void pump() {
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){
        TranslateMessage(&message);DispatchMessageW(&message);
    }
}
HWND descendant(HWND parent,int id) {
    struct Search{int id;HWND result=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{
        auto& value=*reinterpret_cast<Search*>(raw);
        if(GetDlgCtrlID(item)==value.id){value.result=item;return FALSE;}return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.result;
}
std::string xml_text(std::string_view text) {
    std::string result;
    for(const auto character:text)switch(character){
    case '&':result+="&amp;";break;
    case '<':result+="&lt;";break;
    case '>':result+="&gt;";break;
    case '"':result+="&quot;";break;
    case '\'':result+="&apos;";break;
    case '\r':result+="&#13;";break;
    case '\n':result+="&#10;";break;
    case '\t':result+="&#9;";break;
    default:result+=character;break;
    }
    return result;
}
void export_preferences(const std::filesystem::path& folder,const state::StateData& data) {
    check(!std::filesystem::exists(folder),"Legacy-response QA must create its own new synthetic export");
    std::filesystem::create_directories(folder/"shared_prefs");
    for(const auto& [domain,values]:data.preferences){
        std::string xml="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>";
        for(const auto& [key,value]:values)std::visit([&](const auto& item){
            using T=std::decay_t<decltype(item)>;
            const auto name=" name=\""+xml_text(key)+"\"";
            if constexpr(std::is_same_v<T,std::string>)xml+="<string"+name+">"+xml_text(item)+"</string>";
            else if constexpr(std::is_same_v<T,bool>)xml+="<boolean"+name+" value=\""+(item?"true":"false")+"\"/>";
            else if constexpr(std::is_same_v<T,std::int64_t>)xml+="<long"+name+" value=\""+std::to_string(item)+"\"/>";
            else if constexpr(std::is_same_v<T,double>){
                std::ostringstream number;number.imbue(std::locale::classic());number<<std::setprecision(17)<<item;
                xml+="<float"+name+" value=\""+number.str()+"\"/>";
            }else if constexpr(std::is_same_v<T,std::vector<std::string>>){
                xml+="<set"+name+">";for(const auto& text:item)xml+="<string>"+xml_text(text)+"</string>";xml+="</set>";
            }
        },value);
        xml+="</map>";
        std::ofstream stream(state::native_io_path(folder/"shared_prefs"/(domain+".xml")),std::ios::binary);
        stream.write(xml.data(),static_cast<std::streamsize>(xml.size()));stream.close();
        check(static_cast<bool>(stream),"Cannot write the isolated Android preference fixture");
    }
}
using FileHashes=std::map<std::filesystem::path,std::string>;
FileHashes hashes(const std::filesystem::path& folder) {
    FileHashes result;
    for(const auto& item:std::filesystem::recursive_directory_iterator(state::native_io_path(folder)))
        if(item.is_regular_file())result.emplace(item.path().lexically_relative(state::native_io_path(folder)),
                                                state::sha256(state::read_bytes(item.path())));
    return result;
}
struct MountedView {
    const WindowsEnvironment& environment;
    const curriculum::Lesson& lesson;
    HWND host=nullptr,view=nullptr;
    MountedView(const WindowsEnvironment& selected,const curriculum::Lesson& model):environment(selected),lesson(model){
        host=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden legacy-response import QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the hidden legacy-response QA host");
    }
    ~MountedView(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){
        check(!view,"The legacy-response view is already open");
        view=create_lesson_view(host,environment,lesson);
        check(view!=nullptr,"An imported valid historical response prevented the lesson from opening");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        check(!IsWindowVisible(host),"Legacy-response QA must not take the user's desktop");
    }
    void reopen(){check(flush_lesson_view(view),"Cannot save the isolated historical response");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){
        check(flush_lesson_view(view),"Cannot flush the historical-response QA view");
        state::StateStore reader(environment.paths.root);return reader.load();
    }
};
struct Plan {
    const char* name;
    const char* file;
    const char* identity;
    std::size_t page,block;
    const char* answer;
    bool correct,reviewed;
};
void imported_legacy_response(const Plan& plan) {
    const auto asset=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion"/plan.file;
    const auto asset_hash=state::sha256(state::read_bytes(asset));
    const auto lesson=windows::load_lesson(asset);
    const auto position=curriculum::stable_block_position(lesson,plan.identity);
    check(position&&position->page==plan.page&&position->block==plan.block,
        "The current lesson no longer has the audited historical text-response identity");
    const auto& block=lesson.pages[plan.page].blocks[plan.block];
    check(curriculum::uses_single_line_answer(block),"This regression requires the existing authored one-line keyboard policy");
    const std::string answer=plan.answer;
    check(answer.find('\n')!=answer.npos,"The historical fixture must preserve a genuine line break");
    check(curriculum::evaluate_text_answer(block,answer).correct==plan.correct,
        "The historical response does not agree with the unchanged real answer contract");
    auto environment=resolve_windows_environment({ProfileId::Codex,
        L"legacy-response-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+wide(plan.name),false});
    check(!std::filesystem::exists(environment.paths.root),"Legacy-response import cannot reuse an existing profile");
    initialize_profile_directories(environment.paths);
    check(environment.paths.root.parent_path().filename()==L"qa","Legacy responses must stay in an isolated QA profile");
    state::StateData original;
    state::prepare_lesson_state(original,lesson);state::save_current_page(original,lesson,plan.page);
    state::Response response;response.answer=answer;response.correct=plan.correct;
    response.raw_json=R"({"legacyMetadata":"kept verbatim","originalAttempt":7})";
    if(plan.reviewed)state::mark_response_reviewed(response,block);
    state::put_response(original,lesson,block,response);
    // Pin an already migrated finite reserve. Opening/reading/selection must
    // not charge it or alter the imported recovery timestamp.
    auto& reserve=original.preferences["attempt_reserve_seconds_test"];
    reserve["reserve_programming"]=4.25;
    reserve["updated_at_programming"]=std::int64_t{4'000'000'000'000};
    reserve["recovery_interval_programming"]=attempts::recovery_millis;
    const auto historical_attempts=reserve;
    const auto response_key=state::response_storage_key(lesson,block);
    const auto original_json=state::string_value(original,"learning_progress",response_key);
    const auto before_validation=original;
    state::validate_lesson_state(original,lesson);
    check(original==before_validation,"Read-only validation changed a legal historical answer");
    const auto exported=environment.paths.projects/(L"android-"+environment.paths.root.filename().native());
    export_preferences(exported,original);const auto original_hashes=hashes(exported);
    const auto preview=state::inspect_android_export(exported);
    state::validate_lesson_state(preview.data,lesson);
    check(preview.data==original,"Inspecting Android preferences changed the response or its metadata");
    state::commit_android_import(preview,environment.paths.root);
    state::StateStore imported_store(environment.paths.root);const auto imported=imported_store.load();
    state::validate_lesson_state(imported,lesson);
    check(state::string_value(imported,"learning_progress",response_key)==original_json,
        "Publishing the import rewrote the original historical response JSON");
    std::cout<<"PASS Android import accepts legal historical "<<plan.name<<" with LF; mounting unchanged lesson next\n";
    MountedView mounted(environment,lesson);mounted.open();
    const auto verify=[&]{
        check(lesson_page_state(mounted.view).index==plan.page,"The historical response changed the restored lesson page");
        const auto field=descendant(mounted.view,1000+static_cast<int>(plan.block)*100+2);
        const auto feedback=descendant(mounted.view,1000+static_cast<int>(plan.block)*100+4);
        check(field&&feedback,"The real restored text-answer controls are missing");
        check(edit_text(field)==answer,"The view trimmed, flattened or cleared historical answer characters");
        check((IsWindowEnabled(field)!=FALSE)==!plan.correct,"Restoring the answer changed its original solved/editable state");
        const auto feedback_text=edit_text(feedback);
        if(plan.correct)check(feedback_text.starts_with("✓ Correcto"),"A historically correct trimmed answer lost its solved feedback");
        else if(plan.reviewed)check(feedback_text.starts_with("✗ Incorrecto"),"The existing rejected answer lost its reviewed feedback");
        else check(feedback_text.empty(),"An unsubmitted historical draft was automatically checked");
        CHARRANGE selected{1,std::min<LONG>(5,static_cast<LONG>(wide(answer).size()))};
        SendMessageW(field,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
        const auto regions=lesson_reading_regions(mounted.view);
        check(std::ranges::any_of(regions,[&](const auto& region){
            return region.block_index==plan.block&&region.is_exercise&&region.solved==plan.correct;
        }),"The reader lost the original solved/pending boundary of the historical text answer");
        CHARRANGE after{};SendMessageW(field,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&after));
        check(after.cpMin==selected.cpMin&&after.cpMax==selected.cpMax,
            "Inspecting the restored reader regions disturbed native answer selection");
        check(edit_text(field)==answer,"Native selection or reader inspection modified the historical text");
        const auto saved=mounted.saved();
        const auto restored=state::get_response(saved,lesson,block);
        check(restored&&restored->answer==answer&&restored->correct==plan.correct
            &&state::response_reviewed(*restored,block)==plan.reviewed,
            "Opening, reading or saving reinterpreted the historical answer/review");
        check(state::string_value(saved,"learning_progress",response_key)==original_json,
            "Opening or saving discarded opaque historical response metadata");
        check(saved.preferences.at("attempt_reserve_seconds_test")==historical_attempts,
            "Opening, reading or saving consumed attempts for an old answer");
        check(hashes(exported)==original_hashes,"Legacy-response QA changed its synthetic Android source");
        check(state::sha256(state::read_bytes(asset))==asset_hash,"Legacy-response QA modified an authored lesson asset");
    };
    verify();mounted.reopen();verify();
    std::cout<<"PASS historical "<<plan.name<<": exact LF answer/metadata, original verdict, reader boundary, selection, finite attempts and reopening\n";
}
}
int main(){
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    const auto ole=OleInitialize(nullptr);if(FAILED(ole))return 2;
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto rich=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{
        check(rich!=nullptr,"Cannot load RichEdit for the historical-response regression");reader_theme::prepare_fonts();
        // Before Android commit 3cc4594 (2026-10-17), these programming answer
        // editors allowed multiple lines. Storage/evaluation preserve those
        // answers: one-line Enter policy is not a historical data constraint.
        const Plan plans[]={
            {"unsubmitted-output","lesson_texto_secuencia_y_formato.json","block_0000_0003",0,3,"Roca\nAz",false,false},
            {"reviewed-incorrect-output","lesson_texto_secuencia_y_formato.json","block_0000_0003",0,3,"Roca\nVerde",false,true},
            {"correct-trimmed-output","lesson_texto_secuencia_y_formato.json","block_0000_0003",0,3," \tRocaAzul\n ",true,false},
            {"correct-normalized-fill","lesson_fuente_compilacion_y_diagnostico.json","block_0001_0004",1,4,"\n<<\n",true,false},
        };
        for(const auto& plan:plans)imported_legacy_response(plan);
    }catch(const std::exception& error){std::cerr<<"FAIL legacy response import/view: "<<error.what()<<'\n';result=1;}
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}

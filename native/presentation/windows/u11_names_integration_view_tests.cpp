#include "core/lesson.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"

#include <commctrl.h>
#include <ole2.h>
#include <oleacc.h>
#include <richedit.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::presentation;
constexpr std::string_view asset_sha="335036782a5c6eb7d1edd1a60c1b062572a791e8c6e4ce54e14fd25b246df4b0";
// Independent literal oracles from the accepted nine-report contract and the
// contextualized reservation client. Never infer them from a program's output.
constexpr std::string_view expected_stdout=
    "Caso 1\nReservado: Pasillo_2\nSigno: *\nNumero: 4\nSigno: ;\nFin\n"
    "Caso 2\nNombre: n\nSigno: ;\nFin\n"
    "Caso 3\nNombre: Crea\nSigno: ;\nFin\n"
    "Caso 4\nPalabra: crea\nSigno: ;\nFin\n"
    "Caso 5\nNombre: A1_B\nSigno: ;\nFin\n"
    "Caso 6\nNombre: abcdefghijkl\nSigno: ;\nFin\n"
    "Caso 7\nError: Nombre demasiado largo\n"
    "Caso 8\nError: Caracter no admitido\n"
    "Caso 9\nNombre: solo\nFin\n";
constexpr std::string_view expected_prediction=
    "Lleno: Buffer ocupado\nGuardado: v:0:f\nSiguiente: ;\nVacio: Token invalido\nEntrada: n:8";
static_assert(expected_stdout.size()==327&&expected_prediction.size()==85);
void check(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
void wait(const std::function<bool()>& done,std::string_view description,ULONGLONG timeout=90000){const auto deadline=GetTickCount64()+timeout;
    do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<deadline);
    throw std::runtime_error(std::string("Timeout: ")+std::string(description));}
int control_id(std::size_t block,int part){return 1000+static_cast<int>(block)*100+part;}
HWND descendant(HWND parent,int id){struct Search{int id;HWND result=nullptr;} search{id};
    EnumChildWindows(parent,[](HWND item,LPARAM raw)->BOOL{auto& value=*reinterpret_cast<Search*>(raw);if(GetDlgCtrlID(item)==value.id){value.result=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));return search.result;}
std::string window_text(HWND item){const auto length=GetWindowTextLengthW(item);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');
    value.resize(static_cast<std::size_t>(GetWindowTextW(item,value.data(),static_cast<int>(value.size()))));return utf8(value);}
std::string bytes(const std::filesystem::path& path){const auto value=state::read_bytes(path);return {value.begin(),value.end()};}
void physical_directory(const std::filesystem::path& path){const auto handle=CreateFileW(state::native_io_path(path).c_str(),FILE_READ_ATTRIBUTES,
    FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(handle!=INVALID_HANDLE_VALUE,"Cannot open the actual compiler run directory for physical-path verification");
    try{state::require_unredirected_handle(handle,path);}catch(...){CloseHandle(handle);throw;}CloseHandle(handle);}
std::string visible(std::string value){std::erase(value,'`');return value;}
std::string msaa(HWND item,bool required=false,std::string* native_value=nullptr){std::string value=required?std::string{}:window_text(item);Microsoft::WRL::ComPtr<IAccessible> provider;
    const auto status=AccessibleObjectFromWindow(item,static_cast<DWORD>(OBJID_CLIENT),IID_PPV_ARGS(provider.GetAddressOf()));
    check(!required||SUCCEEDED(status),"The actual unlocked source has no native MSAA provider");if(FAILED(status))return value;
    VARIANT self{};self.vt=VT_I4;self.lVal=CHILDID_SELF;bool supplied=false;
    for(const bool content:{false,true}){BSTR text=nullptr;const auto result=content?provider->get_accValue(self,&text):provider->get_accName(self,&text);
        if(result==S_OK){supplied=true;if(text){const auto source=utf8(std::wstring_view(text,SysStringLen(text)));value+='\n';value+=source;if(content&&native_value)*native_value=source;}}SysFreeString(text);}
    check(!required||supplied,"The actual unlocked MSAA provider supplied no source text");
    for(std::size_t at=0;(at=value.find("\r\n",at))!=std::string::npos;)value.replace(at,2,"\n");
    std::ranges::replace(value,'\r','\n');
    if(native_value){for(std::size_t at=0;(at=native_value->find("\r\n",at))!=std::string::npos;)native_value->replace(at,2,"\n");std::ranges::replace(*native_value,'\r','\n');}return value;
}
curriculum::StatePosition position(const curriculum::Lesson& lesson,std::string_view identity){const auto result=curriculum::stable_block_position(lesson,identity);
    check(result.has_value(),"A registered U11L17 stable identity is missing");return *result;}
bool same_response(const state::Response& left,const state::Response& right){return left.answer==right.answer&&left.correct==right.correct&&left.raw_json==right.raw_json
    &&left.selected==right.selected&&left.order==right.order&&left.reviewed_answer_identity==right.reviewed_answer_identity;}
struct View{
    WindowsEnvironment environment;
    curriculum::Lesson lesson;
    HWND host=nullptr,view=nullptr;
    explicit View(curriculum::Lesson source):environment(resolve_windows_environment({ProfileId::Codex,
        L"u11-names-integration-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false})),lesson(std::move(source)){
        // Keep generated runs outside MSIX AppData virtualization. This is a
        // fresh QA profile, never the student's installed profile or cache.
        environment.paths=make_profile_paths(std::filesystem::path(build::source_root)/"out/qa/u11-names-view",ProfileId::Codex,*environment.options.qa_instance);
        check(!std::filesystem::exists(environment.paths.root),"U11 names integration requires a fresh isolated profile");initialize_profile_directories(environment.paths);
        state::StateStore store(environment.paths.root);auto initial=store.load();check(initial.preferences.empty(),"Fresh U11 QA unexpectedly has previous data");
        state::prepare_lesson_state(initial,lesson);state::save_current_page(initial,lesson,5);
        initial.preferences["native_windows"]["ui_language"]=std::string("es");initial.preferences["u11_names_integration_qa"]["opaque"]=std::string("\t conservar\r\n保存");store.save(initial);
        host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Hidden actual U11 names integration QA",WS_OVERLAPPEDWINDOW,
            0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host,"Cannot create hidden native U11 integration host");open();
    }
    ~View(){if(view){(void)flush_lesson_view(view);DestroyWindow(view);}if(host)DestroyWindow(host);}
    void open(){view=create_lesson_view(host,environment,lesson);check(view,"Cannot create the actual U11 names view");MoveWindow(view,0,0,1100,850,TRUE);pump();check(!IsWindowVisible(host),"U11 integration QA exposed the desktop");}
    void reopen(){check(flush_lesson_view(view),"Cannot save U11 integration state before reopening");DestroyWindow(view);view=nullptr;open();}
    state::StateData saved(){check(flush_lesson_view(view),"Cannot persist current U11 integration state");return read_lesson_state(view);}
    HWND item(std::size_t block,int part){const auto result=descendant(view,control_id(block,part));check(result,"Missing actual U11 native control");return result;}
    std::string text(std::size_t block,int part){return edit_text(item(block,part));}
    void click(std::size_t block,int part){const auto target=item(block,part);check(IsWindowEnabled(target),"The actual U11 action is disabled");SendMessageW(target,BM_CLICK,0,0);pump();}
    void replace(std::size_t block,int part,std::string_view source){const auto target=item(block,part);check(IsWindowEnabled(target),"Cannot edit a locked U11 source/answer");
        const auto value=wide(source);CHARRANGE all{0,-1};SendMessageW(target,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));SendMessageW(target,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));pump();
        check(edit_text(target)==source,"The native edit changed authored source or response characters");}
    void page(std::size_t index){check(navigate_lesson_view(view,index),"The requested U11 page remains locked");pump();check(lesson_page_state(view).index==index,"U11 page navigation selected another page");}
    std::filesystem::path run(std::size_t block,std::string_view source,bool executes){
        const auto folder=environment.paths.projects/L"cpp-runs";std::set<std::filesystem::path> old;
        if(std::filesystem::exists(folder))for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_directory())old.insert(entry.path());
        const auto action=item(block,11);check(window_text(action)=="Compilar, ejecutar y comprobar"&&IsWindowEnabled(action),"The actual combined compiler action changed");
        SendMessageW(action,BM_CLICK,0,0);check(!IsWindowEnabled(action),"The actual compiler did not start a fresh asynchronous run");
        wait([&]{return IsWindowEnabled(action)&&text(block,14)!="Compilando…";},"actual U11 compile/run completion");
        std::vector<std::filesystem::path> added;for(const auto& entry:std::filesystem::directory_iterator(folder))if(entry.is_directory()&&!old.contains(entry.path()))added.push_back(entry.path());
        check(added.size()==1,"A compiler button press did not create exactly one new isolated run");const auto directory=added.front();
        physical_directory(directory);
        check(bytes(directory/L"main.cpp")==source&&bytes(directory/L"stdin.bin").empty(),"The real compiler received another source or unexpected stdin");
        if(executes)check(text(block,34)=="Ejecución terminada (código 0)."&&std::filesystem::exists(directory/L"program.exe")
            &&std::filesystem::exists(directory/L"execution-stdout.bin")&&bytes(directory/L"execution-stderr.bin").empty(),"The actual educational program did not run successfully with empty stderr");
        else check(text(block,34).starts_with("La compilación falló (código ")&&!std::filesystem::exists(directory/L"execution-stdout.bin"),"The deliberately invalid source ran or lost its current build diagnosis");
        std::cout<<"RUN source="<<state::sha256(state::read_bytes(directory/L"main.cpp"))<<" directory="<<utf8(directory.native())<<" executed="<<executes<<'\n';return directory;
    }
};
void exercise(){
    const auto asset=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_nombres_del_lenguaje_y_tokens_con_texto.json";
    const auto original=state::read_bytes(asset);check(state::sha256(original)==asset_sha,"The U11L17 integration snapshot changed; review this exact acceptance before updating its pin");
    const auto lesson=windows::load_lesson(asset);const auto compiler_at=position(lesson,"block_0005_0003"),choice_at=position(lesson,"block_0005_0002"),
        prediction_at=position(lesson,"work_u11l17_reserva_textual"),reference_at=position(lesson,"block_0007_0001");
    const auto& compiler=lesson.pages[compiler_at.page].blocks[compiler_at.block];const auto& choice=lesson.pages[choice_at.page].blocks[choice_at.block];
    const auto& prediction=lesson.pages[prediction_at.page].blocks[prediction_at.block];const auto& reference=lesson.pages[reference_at.page].blocks[reference_at.block];
    check(compiler_at.page==5&&compiler_at.block==4&&choice_at.page==5&&choice_at.block==2&&prediction_at.page==6&&prediction_at.block==1
        &&reference_at.page==7&&reference_at.block==1&&lesson.pages.size()==8&&lesson.compiler_puzzles_required&&choice.correct_index==1,
        "The current U11 construction/recovery placement or preceding choice changed");
    check(compiler.output_check&&compiler.output_check->cases.size()==1&&compiler.output_check->cases.front().outputs==std::vector<std::string>{std::string(expected_stdout)}
        &&compiler.output_check->cases.front().standard_input.empty()&&prediction.type==curriculum::BlockType::output_prediction
        &&prediction.answers==std::vector<std::string>{std::string(expected_prediction)}&&prediction.output_comparison==curriculum::TextComparison::trimmed,
        "The current U11 contracts differ from the independent stdout/prediction oracles");
    const std::array<std::string_view,6> references{"block_0006_0000","block_0006_0001","block_0006_0002","block_0006_0003","block_0007_0000","block_0007_0001"};
    const auto dependencies=curriculum::activity_prerequisites(lesson,prediction);check(dependencies.size()==1&&dependencies.front().page==compiler_at.page&&dependencies.front().block==compiler_at.block,
        "The new U11 recovery lost its exact compiler owner");
    for(const auto identity:references){const auto at=position(lesson,identity);const auto owner=curriculum::reference_activity(lesson,lesson.pages[at.page].blocks[at.block]);
        check(owner&&owner->page==compiler_at.page&&owner->block==compiler_at.block,"A U11 reference has another owner or is unguarded");}
    View f(lesson);const auto untouched=f.saved().preferences.at("u11_names_integration_qa");
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks){check(!state::get_response(f.saved(),lesson,block),"The fresh U11 integration profile contains a seeded quiz response");
        if(block.type==curriculum::BlockType::cpp_compiler)check(!windows::compiler_puzzle_completed(f.saved(),lesson,block)&&windows::compiler_puzzle_case_count(f.saved(),lesson,block)==0,"The fresh U11 profile contains a seeded execution proof");}
    const auto preserved=[&]{const auto data=f.saved();check(data.preferences.at("u11_names_integration_qa")==untouched&&narration::read_history(data).entries.empty()
        &&state::read_bytes(asset)==original&&!IsWindowVisible(f.host),"U11 integration changed unrelated data, invented listening, modified content or exposed the desktop");};
    const auto locked=[&]{check(lesson_page_state(f.view).index==compiler_at.page&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,6)&&!navigate_lesson_view(f.view,7),
            "A pending U11 compiler allowed its later recovery/reference pages");
        const auto regions=lesson_reading_regions(f.view);check(std::ranges::any_of(regions,[&](const auto& region){return region.block_index==compiler_at.block&&region.required_compiler&&!region.solved;}),
            "The actual pending U11 compiler has no reader boundary");
        for(const auto identity:references)check(std::ranges::none_of(regions,[&](const auto& region){return region.key.find('/'+std::string(identity)+'/')!=std::string::npos;}),"A locked U11 reference leaked into the reader");
        check(std::ranges::none_of(regions,[](const auto& region){return region.key.find("/work_u11l17_reserva_textual/")!=std::string::npos;}),"The locked new prediction leaked into narration");
        const std::array<std::string,7> secrets{visible(prediction.question),prediction.code,std::string(expected_prediction),reference.code,
            "Lleno: Buffer ocupado","Guardado: v:0:f","Lector_tokens otro{\"8;\"};"};
        // The short probes are private to the later recovery, not public clues
        // from the current construction page. They catch partial MSAA leaks too.
        for(const auto& secret:secrets)for(const auto& block:lesson.pages[compiler_at.page].blocks)
            check(block.raw_json.find(secret)==std::string::npos,"A locked-content privacy probe also appears in the public U11 page");
        struct Probe{const std::array<std::string,7>* secrets;bool leaked=false;} probe{&secrets};
        EnumChildWindows(f.view,[](HWND item,LPARAM raw)->BOOL{auto& probe=*reinterpret_cast<Probe*>(raw);const auto text=msaa(item);
            for(const auto& secret:*probe.secrets)if(text.find(secret)!=std::string::npos){probe.leaked=true;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&probe));
        check(!probe.leaked,"A locked U11 recovery/reference exposed its exclusive question, code or answer through native MSAA");preserved();};
    const auto no_code_narration=[&]{const auto page=lesson_page_state(f.view).index;const auto regions=lesson_reading_regions(f.view);
        for(std::size_t index=0;index<lesson.pages[page].blocks.size();++index){const auto& block=lesson.pages[page].blocks[index];
            if(block.type!=curriculum::BlockType::code&&block.type!=curriculum::BlockType::output_prediction&&block.type!=curriculum::BlockType::cpp_compiler)continue;
            if(const auto source=descendant(f.view,control_id(index,block.type==curriculum::BlockType::cpp_compiler?10:1));source)check(std::ranges::none_of(regions,[&](const auto& region){return region.target==source;}),"A displayed source body was added to lesson narration");
        }};
    locked();no_code_narration();const auto initial_run=f.run(compiler_at.block,compiler.initial_code,true);
    check(bytes(initial_run/L"execution-stdout.bin").empty(),"The original U11 initial produced unexpected physical stdout");
    auto initial_diagnostics=bytes(initial_run/L"compilation-stdout.bin")+bytes(initial_run/L"compilation-stderr.bin");std::erase(initial_diagnostics,'\r');
    check(!initial_diagnostics.empty()&&f.text(compiler_at.block,30)=="Diagnósticos de compilación y enlace"&&f.text(compiler_at.block,14)==initial_diagnostics,
        "The empty U11 execution did not show its fresh compilation diagnostics in the labelled diagnostics channel");
    check(f.text(compiler_at.block,4).starts_with("✗ Incorrecto"),"The initial U11 verdict was not Incorrecto: "+f.text(compiler_at.block,4));
    check(!windows::compiler_puzzle_completed(f.saved(),lesson,compiler),"The compilable empty initial falsely completed the U11 output goal");locked();
    f.click(choice_at.block,20+static_cast<int>(*choice.correct_index)*2);f.click(choice_at.block,3);
    const auto selected=state::get_response(f.saved(),lesson,choice);check(selected&&selected->correct&&f.text(choice_at.block,4)=="✓ Correcto\n\n"+visible(choice.explanation),"The preceding U11 choice did not preserve its complete successful explanation");
    locked();f.replace(compiler_at.block,10,reference.code);const auto accepted_run=f.run(compiler_at.block,reference.code,true);
    const auto raw=bytes(accepted_run/L"execution-stdout.bin");std::string crlf;for(const auto value:expected_stdout){if(value=='\n')crlf+='\r';crlf+=value;}
    check((raw==expected_stdout||raw==crlf)&&f.text(compiler_at.block,14)==expected_stdout&&f.text(compiler_at.block,4).starts_with("✓ Correcto")
        &&windows::compiler_puzzle_case_count(f.saved(),lesson,compiler)==1&&windows::compiler_puzzle_completed(f.saved(),lesson,compiler)&&lesson_page_state(f.view).can_next,
        "The real nine-report U11 reference did not complete exactly one aggregate goal");
    f.reopen();check(f.text(compiler_at.block,10)==reference.code&&f.text(compiler_at.block,14).empty()&&windows::compiler_puzzle_completed(f.saved(),lesson,compiler),"Reopening lost the real compiler achievement/source or restored a transient stdout as a new run");
    f.page(prediction_at.page);const auto prompt=f.text(prediction_at.block,0);
    check(prompt==visible(prediction.question)&&f.text(prediction_at.block,1)==prediction.code&&msaa(f.item(prediction_at.block,1),true).find(prediction.code)!=std::string::npos,
        "Unlocking changed the new recovery question/code or omitted its native accessible source");
    check(f.text(2,1)==lesson.pages[6].blocks[2].code&&f.text(prediction_at.block,4).empty()&&!lesson_page_state(f.view).can_next&&!navigate_lesson_view(f.view,7),
        "The new recovery was already answered or failed to block forward continuation");no_code_narration();
    const auto initial_regions=lesson_reading_regions(f.view);check(std::ranges::any_of(initial_regions,[&](const auto& region){return region.block_index==prediction_at.block&&region.source_text==prediction.question&&!region.solved;}),"The current unanswered recovery lost its exact original narration source");
    check(std::ranges::none_of(initial_regions,[](const auto& region){return region.source_text.find(expected_prediction)!=std::string::npos;}),"An unanswered recovery narrated its hidden expected output");
    const std::string partial="Lleno: Buffer ocupado\nGuardado: v:0:";f.replace(prediction_at.block,2,partial);
    const auto draft=state::get_response(f.saved(),lesson,prediction);check(draft&&!draft->correct&&draft->answer==partial&&!state::response_reviewed(*draft,prediction)&&f.text(prediction_at.block,4).empty(),"Typing an incomplete recovery answer evaluated it automatically");
    f.reopen();const auto reopened_draft=state::get_response(f.saved(),lesson,prediction);check(reopened_draft&&same_response(*draft,*reopened_draft)&&f.text(prediction_at.block,2)==partial&&f.text(prediction_at.block,4).empty(),"Reopening altered the unchecked multiline recovery draft");
    std::string wrong(expected_prediction);const auto colons=wrong.find("v:0:f");check(colons!=wrong.npos,"The independent U11 prediction pin lost its internal separators");wrong.replace(colons,5,"v 0 f");
    f.replace(prediction_at.block,2,wrong);f.click(prediction_at.block,3);const auto rejected=state::get_response(f.saved(),lesson,prediction);
    const auto wrong_feedback="✗ Incorrecto\n\n"+visible(prediction.review_feedback);
    check(rejected&&!rejected->correct&&state::response_reviewed(*rejected,prediction)&&f.text(prediction_at.block,4)==wrong_feedback&&!lesson_page_state(f.view).can_next,
        "Wrong internal output separators were accepted or lost the complete recovery guide");
    check(f.text(prediction_at.block,4).find(visible(prediction.explanation))==std::string::npos,"A rejected recovery revealed its successful explanation");
    f.reopen();const auto reopened_error=state::get_response(f.saved(),lesson,prediction);check(reopened_error&&same_response(*rejected,*reopened_error)&&f.text(prediction_at.block,2)==wrong&&f.text(prediction_at.block,4)==wrong_feedback,"Reopening lost the recovery's exact rejected answer/review/guide");
    const std::string trimmed=" \t"+std::string(expected_prediction)+"\n ";f.replace(prediction_at.block,2,trimmed);f.click(prediction_at.block,3);
    const auto correct=state::get_response(f.saved(),lesson,prediction);const auto successful="✓ Correcto\n\n"+visible(prediction.explanation);
    check(correct&&correct->correct&&correct->answer==trimmed&&f.text(prediction_at.block,4)==successful&&lesson_page_state(f.view).can_next,
        "The current trimmed five-line recovery answer lost its complete explanation or completion");
    check(!IsWindowEnabled(f.item(prediction_at.block,3)),"A successful recovery remained available for another submission");
    const auto feedback_regions=lesson_reading_regions(f.view);check(std::ranges::any_of(feedback_regions,[&](const auto& region){return region.block_index==prediction_at.block&&region.feedback&&region.correct&&region.source_text==successful;}),"The successful explanation is incomplete in the narration source");
    f.reopen();const auto reopened_correct=state::get_response(f.saved(),lesson,prediction);check(reopened_correct&&same_response(*correct,*reopened_correct)&&f.text(prediction_at.block,4)==successful&&f.text(prediction_at.block,2)==trimmed,"The successful new recovery did not reopen with its complete response and explanation");no_code_narration();
    f.page(reference_at.page);const auto shown_reference=f.text(reference_at.block,1);std::string accessible_reference;(void)msaa(f.item(reference_at.block,1),true,&accessible_reference);
    check(shown_reference==reference.code,"The final U11 reference text changed: shown="+std::to_string(shown_reference.size())+" authored="+std::to_string(reference.code.size()));
    // The native MSAA value can expose an excerpt of a long RichEdit document.
    // Verify exactly that prefix; full visible source is pinned above. This does
    // not claim a full-document assistive read through UIA TextPattern.
    check(!accessible_reference.empty()&&reference.code.starts_with(accessible_reference),"The final U11 MSAA excerpt changed authored source characters");
    no_code_narration();preserved();
    f.page(compiler_at.page);const auto before_failure=f.saved();const auto broken=reference.code+"\nthis is deliberately invalid C++;\n";f.replace(compiler_at.block,10,broken);
    const auto failed_run=f.run(compiler_at.block,broken,false);check(!bytes(failed_run/L"compilation-stdout.bin").empty()||!bytes(failed_run/L"compilation-stderr.bin").empty(),"The real invalid source produced no build diagnosis");
    check(f.text(compiler_at.block,4).starts_with("✓ Correcto")&&windows::compiler_puzzle_completed(f.saved(),lesson,compiler)&&windows::compiler_puzzle_case_count(f.saved(),lesson,compiler)==1
        &&f.text(compiler_at.block,14)!=expected_stdout&&lesson_page_state(f.view).can_next,"A later failed build revoked achievement or reused the prior successful console");
    const auto attempts_before=before_failure.preferences.find("attempt_reserve_seconds_test");const auto after_failure=f.saved();const auto attempts_after=after_failure.preferences.find("attempt_reserve_seconds_test");
    check((attempts_before==before_failure.preferences.end())==(attempts_after==after_failure.preferences.end())&&(attempts_before==before_failure.preferences.end()||attempts_before->second==attempts_after->second),"A compiler failure consumed the quiz attempt reserve");
    f.reopen();check(f.text(compiler_at.block,10)==broken&&f.text(compiler_at.block,14).empty()&&windows::compiler_puzzle_completed(f.saved(),lesson,compiler),"Reopening lost the current bad source/sticky achievement or restored old console output");
    f.click(compiler_at.block,12);check(f.text(compiler_at.block,10)==compiler.initial_code&&f.text(compiler_at.block,14).empty()&&f.text(compiler_at.block,4).empty()
        &&!windows::compiler_puzzle_completed(f.saved(),lesson,compiler)&&windows::compiler_puzzle_case_count(f.saved(),lesson,compiler)==0,"Explicit Reset did not restore only the original pending compiler");
    const auto after_reset=f.saved();const auto preserved_prediction=state::get_response(after_reset,lesson,prediction),preserved_choice=state::get_response(after_reset,lesson,choice);
    check(preserved_prediction&&same_response(*correct,*preserved_prediction)&&preserved_choice&&same_response(*selected,*preserved_choice),"Compiler Reset changed its dependent prediction answer or preceding choice");locked();
    f.reopen();locked();const auto last=f.saved();const auto retained_prediction=state::get_response(last,lesson,prediction);check(retained_prediction&&same_response(*correct,*retained_prediction),"The relocked recovery's correct response was lost on reopen");
    std::cout<<"PASS current U11L17: three real compiler actions (empty rejected, nine reports correct, later build failure), six guarded references/new recovery, native multiline draft/error/trimmed success and MSAA/reader privacy, persistent achievement/source/answers, scoped Reset and reopen; no seeded responses/receipts, no audio/import/full traversal/human listening claimed\n";
}
}
int main(){const auto initialized=OleInitialize(nullptr);if(FAILED(initialized))return 2;
    INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES};InitCommonControlsEx(&common);const auto rich=LoadLibraryW(L"Msftedit.dll");int result=0;
    try{check(rich,"Cannot load native RichEdit");reader_theme::prepare_fonts();exercise();}
    catch(const std::exception& error){std::cerr<<"FAIL U11 names integration: "<<error.what()<<'\n';result=1;}
    if(rich)FreeLibrary(rich);OleUninitialize();return result;
}

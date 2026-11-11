#include "presentation/windows/cpp_project_view.hpp"
#include "presentation/windows/internal_surface_test_support.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/compiler_completion.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;using namespace presentation;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void cleanup_own_qa(const std::filesystem::path& base,const std::filesystem::path& directory,std::string_view prefix){
    const auto parent=std::filesystem::weakly_canonical(base),resolved=std::filesystem::weakly_canonical(directory);
    require(parent.is_absolute()&&resolved.is_absolute()&&resolved.parent_path()==parent&&resolved.filename().string().starts_with(prefix),"Refusing to remove a path outside this test's unique QA directory");
    state::require_safe_path(resolved);std::filesystem::remove_all(state::native_io_path(resolved));
}
HWND child(HWND owner,int id){struct Search{int id;HWND item=nullptr;} value{id};EnumChildWindows(owner,[](HWND item,LPARAM lp)->BOOL{auto& found=*reinterpret_cast<Search*>(lp);if(GetDlgCtrlID(item)==found.id){found.item=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&value));return value.item;}
std::string caption(HWND item){const auto length=GetWindowTextLengthW(item);std::wstring value(static_cast<std::size_t>(length)+1,L'\0');value.resize(static_cast<std::size_t>(GetWindowTextW(item,value.data(),static_cast<int>(value.size()))));return utf8(value);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
void await(const std::function<bool()>& done,const char* reason){const auto deadline=GetTickCount64()+90000;do{pump();if(done())return;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<deadline);throw std::runtime_error(reason);}
void click(HWND owner,int id){const auto item=child(owner,id);require(item&&IsWindowEnabled(item),"Expected enabled project control is missing");SendMessageW(item,BM_CLICK,0,0);pump();}
struct ModalScenario {
    enum class Kind{create,record,restore,compare};Kind kind;HWND host;UINT_PTR timer=0;ULONGLONG deadline;int stage=0;bool done=false;std::string error;
    std::string target_path,expected_before,expected_after;
    static inline ModalScenario* active=nullptr;
    ModalScenario(HWND owner,Kind operation,std::string file="main.cpp",
                  std::string prior="return 0",std::string revised="return 9")
        :kind(operation),host(owner),deadline(GetTickCount64()+30000),
         target_path(std::move(file)),expected_before(std::move(prior)),expected_after(std::move(revised)){
         require(!active,"Nested QA scenarios");active=this;timer=SetTimer(host,991,15,tick);
         require(timer!=0,"Cannot schedule app-owned modal QA");}
    ~ModalScenario(){KillTimer(host,timer);active=nullptr;}
    static HWND dialog(int control){if(!active)return nullptr;HWND result=nullptr;std::pair<int,HWND*> search{control,&result};EnumChildWindows(active->host,[](HWND item,LPARAM lp)->BOOL{auto* found=reinterpret_cast<std::pair<int,HWND*>*>(lp);wchar_t name[128]{};GetClassNameW(item,name,128);if(std::wstring_view(name)==L"LearnGameAndEngineDev.CppProjectDialog"&&IsWindowVisible(item)&&GetDlgItem(item,found->first)&&IsWindowEnabled(item)){*found->second=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));if(result)internal_surface_test::require_internal_surface(active->host,result);return result;}
    static void CALLBACK tick(HWND,UINT,UINT_PTR,DWORD){if(!active)return;auto& self=*active;try{self.step();}catch(const std::exception& failure){self.error=failure.what();self.close_all();}}
    void close_all(){EnumChildWindows(host,[](HWND item,LPARAM)->BOOL{wchar_t name[128]{};GetClassNameW(item,name,128);if(std::wstring_view(name)==L"LearnGameAndEngineDev.CppProjectDialog")PostMessageW(item,WM_CLOSE,0,0);return TRUE;},0);if(const auto current=active_internal_window(host))PostMessageW(current,WM_CLOSE,0,0);}
    static void rendered_row(HWND window){const auto list=GetDlgItem(window,505);RECT bounds{};GetClientRect(list,&bounds);const auto height=static_cast<int>(SendMessageW(list,LB_GETITEMHEIGHT,0,0));
        const auto dpi=static_cast<int>(GetDpiForWindow(window));const auto measure_dc=GetDC(list);const auto original_font=SelectObject(measure_dc,reinterpret_cast<HFONT>(SendMessageW(list,WM_GETFONT,0,0)));wchar_t label[512]{};SendMessageW(list,LB_GETTEXT,0,reinterpret_cast<LPARAM>(label));RECT text{0,0,std::max(1L,bounds.right-MulDiv(32,dpi,96)),0};DrawTextW(measure_dc,label,-1,&text,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(measure_dc,original_font);ReleaseDC(list,measure_dc);
        require(height>=text.bottom+MulDiv(24,dpi,96),"A two-line revision row clips its actual text or inner padding");
        const auto screen=GetDC(list),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,std::max(1L,bounds.right),height);ReleaseDC(list,screen);require(dc&&bitmap,"Cannot inspect the app-owned history row raster");const auto previous=SelectObject(dc,bitmap);
        DRAWITEMSTRUCT draw{};draw.CtlType=ODT_LISTBOX;draw.CtlID=505;draw.itemID=0;draw.hwndItem=list;draw.hDC=dc;draw.rcItem={0,0,bounds.right,height};const auto painted=SendMessageW(window,WM_DRAWITEM,505,reinterpret_cast<LPARAM>(&draw));GdiFlush();
        const auto background=reader_theme::background(window);int ink=0;for(int y=MulDiv(12,dpi,96);y<height-MulDiv(12,dpi,96);++y)for(int x=MulDiv(16,dpi,96);x<std::min<LONG>(bounds.right-MulDiv(16,dpi,96),MulDiv(500,dpi,96));++x)if(GetPixel(dc,x,y)!=background)++ink;
        SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);require(painted&&ink>10,"The actual project history row has no rendered revision title/date");
    }
    void step(){if(GetTickCount64()>deadline){error="Project modal scenario timed out";close_all();return;}if(done)return;
        if(kind==Kind::create){const auto window=dialog(501);if(!window)return;set_edit_text(GetDlgItem(window,501),"src/helper.cpp");done=true;SendMessageW(window,WM_COMMAND,IDOK,0);return;}
        if(kind==Kind::record){if(stage==0){const auto window=dialog(505);if(!window||!IsWindowEnabled(GetDlgItem(window,IDOK)))return;stage=1;SendMessageW(window,WM_COMMAND,IDOK,0);return;}
            if(stage==1){const auto window=dialog(501);if(!window)return;set_edit_text(GetDlgItem(window,501),"Primera revisión");stage=2;SendMessageW(window,WM_COMMAND,IDOK,0);return;}
            const auto window=dialog(505);if(!window||!IsWindowEnabled(GetDlgItem(window,505))||SendMessageW(GetDlgItem(window,505),LB_GETCOUNT,0,0)!=1)return;rendered_row(window);done=true;SendMessageW(window,WM_COMMAND,IDCANCEL,0);return;}
        if(stage<2){const auto window=dialog(505);if(!window||!IsWindowEnabled(GetDlgItem(window,505)))return;const auto list=GetDlgItem(window,505);const auto count=SendMessageW(list,LB_GETCOUNT,0,0);if(count<=0)return;int at=0;
            if(stage==1){at=-1;for(int i=0;i<count;++i){const auto length=SendMessageW(list,LB_GETTEXTLEN,static_cast<WPARAM>(i),0);require(length>=0,"History file row could not be read");std::wstring label(static_cast<std::size_t>(length)+1,L'\0');SendMessageW(list,LB_GETTEXT,static_cast<WPARAM>(i),reinterpret_cast<LPARAM>(label.data()));if(label.starts_with(wide(target_path))){at=i;break;}}require(at>=0,"History omitted the requested file");}
            SendMessageW(list,LB_SETCURSEL,static_cast<WPARAM>(at),0);++stage;PostMessageW(window,WM_APP+235,0,0);return;}
        if(stage==2){const auto window=dialog(506);if(!window)return;const auto difference=edit_text(GetDlgItem(window,506));require((expected_before.empty()||difference.find(expected_before)!=std::string::npos)&&
                 (expected_after.empty()||difference.find(expected_after)!=std::string::npos),
                 "History did not display both actual versions of the selected file");
             stage=3;SendMessageW(window,WM_COMMAND,kind==Kind::compare?IDCANCEL:IDOK,0);return;}
        if(kind==Kind::compare) {
            // Read-only comparisons leave both the selected-file list and the
            // revision list open. Dismiss the complete owned modal stack so
            // the next comparison can use the same immutable revision.
            done=true;close_all();return;
        }
        const auto window=dialog(505);if(!window||!IsWindowEnabled(GetDlgItem(window,505)))return;done=true;SendMessageW(window,WM_COMMAND,IDCANCEL,0);
    }
    void verified()const{require(error.empty(),error.c_str());require(done,"Actual modal UI was not exercised");}
};
void integrated_lesson(HWND host){
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"project-lesson-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);auto initial=store.load();initial.preferences["native_windows"]["ui_language"]=std::string("es");store.save(initial);
    auto model=windows::parse_lesson_json(R"json({"id":"qa_project_lesson","title":"Proyectos QA","pages":[{"blocks":[{"id":"direct","type":"cpp_compiler","initialCode":"unused legacy source","projectFiles":[{"path":"main.cpp","initialCode":"#include <iostream>\n#include \"value.hpp\"\nint main(){std::cout << value();std::cerr << \"nota\";}\n"},{"path":"value.hpp","initialCode":"int value();\n"},{"path":"value.cpp","initialCode":"#include \"value.hpp\"\nint value(){return 7;}\n"}],"sourceFiles":["main.cpp","value.cpp"],"initialStandardInput":" \t \n","outputCheck":{"cases":[{"standardInput":" \t \n","outputs":["7"],"explanation":"Salida del proyecto comprobada."}]}}]},{"blocks":[{"id":"cmake","type":"cpp_compiler","initialCode":"unused legacy source","projectFiles":[{"path":"main.cpp","initialCode":"#include <iostream>\nint main(){std::cout << \"CMake listo\";}\n"},{"path":"CMakeLists.txt","initialCode":"cmake_minimum_required(VERSION 3.24)\nproject(lesson_qa LANGUAGES CXX)\nadd_executable(lesson_program main.cpp)\n"}],"cmake":{"target":"lesson_program","options":{},"buildType":"Debug"}}]}]})json");
    curriculum::Page checked;checked.stable_id="checked_page";auto combined=model.pages[1].blocks[0];combined.id="checked";combined.stable_id="checked";combined.address={2,0,{},{}};
    combined.project_files[0].second="#include <iostream>\nint main(){std::cout << \"CMake listo\";std::cerr << \"detalle del objetivo CMake\";}\n";
    curriculum::OutputCase check_case;check_case.outputs={"CMake listo"};check_case.executable="lesson_program";check_case.explanation="Se comprobó el ejecutable previsto.";
    check_case.standard_error=curriculum::StandardErrorExpectation::non_blank;
    combined.output_check=curriculum::OutputCheck{curriculum::TextComparison::exact,{check_case}};
    checked.blocks.push_back(combined);model.pages.push_back(checked);
    HWND view=create_lesson_view(host,environment,model);require(view!=nullptr,"Cannot create integrated project lesson");
    const auto cleanup=[&]{if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}};
    try{
        MoveWindow(view,0,0,1000,750,TRUE);pump();auto source=child(view,1010);auto project=child(view,1005);auto console=child(view,1014);auto run=child(view,1011);require(source&&project&&console&&run,"A multi-file lesson omitted its actual editor/project/console/run control");
        require(edit_text(source).starts_with("#include <iostream>")&&edit_text(child(view,1015))==" \t \n","The project main file or whitespace-sensitive prepared input changed");
        click(view,1011);await([&]{return IsWindowEnabled(run);},"The direct multi-file lesson did not finish");
        require(edit_text(console)=="7"&&edit_text(child(view,1004)).starts_with("✓ Correcto"),"The fresh output from the real multi-file executable was not checked");
        require(IsWindowVisible(child(view,1031))&&!child(view,1023)&&!child(view,1024)&&!child(view,1025),"Real multi-channel output did not expose only Android's channel selector");
        require(reader_theme::palette().high_contrast||reader_theme::background(console)==RGB(5,6,7),"The exercise console lost Android's black output surface");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(1079,BN_CLICKED),0);pump();require(edit_text(console)=="nota","Selecting stderr did not show its original channel");
        CHARFORMAT2W color{};color.cbSize=sizeof(color);SendMessageW(console,EM_SETSEL,0,1);SendMessageW(console,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&color));require(reader_theme::palette().high_contrast||color.crTextColor==RGB(121,226,157),"The stderr console did not retain Android's common green ink");
        const auto valid_feedback=edit_text(child(view,1004));click(project,1001);require(edit_text(source)=="int value();\n"&&edit_text(child(view,1004))==valid_feedback,"Selecting a header invalidated an unchanged successful execution");
        const auto new_header=wide("int value();\n// header edited\n");SendMessageW(source,EM_SETSEL,0,-1);SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(new_header.c_str()));pump();require(edit_text(child(view,1004))==valid_feedback,"Editing a project header revoked the completed objective or its explanation");
        click(view,1011);await([&]{return IsWindowEnabled(run);},"The edited completed project could not run again");
        require(edit_text(child(view,1004))==valid_feedback,"A fresh execution of the edited project revoked the completed objective or its explanation");
        require(edit_text(console)=="7"&&edit_text(child(view,1034))=="Ejecución terminada (código 0).",
            "The completed project stopped exposing fresh stdout and its actual execution status");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(1079,BN_CLICKED),0);pump();require(edit_text(console)=="nota","The rerun stopped exposing its real stderr channel");
        require(flush_lesson_view(view),"The project lesson could not save its draft");const auto persisted=read_lesson_state(view);const auto key=curriculum::draft_key("programming",model,model.pages[0].blocks[0]);
        require(state::string_value(persisted,"cpp_lesson_drafts",key+"/project").find("header edited")!=std::string::npos&&state::get_draft(persisted,key,"").input==" \t \n","The project draft or exact stdin was not preserved");
        require(navigate_lesson_view(view,1),"A completed project page could not advance");pump();const auto config=child(view,1006);run=child(view,1011);source=child(view,1010);require(config&&caption(run)=="Ejecutar objetivo construido"&&!IsWindowEnabled(run),"An unbuilt CMake lesson did not expose the correct execution phase");
        auto configure=child(config,405),build=child(config,406);require(configure&&build&&IsWindowEnabled(configure)&&!IsWindowEnabled(build),"CMake phase enablement ignored the actual workspace");
        const auto options=child(config,402);SetFocus(options);MSG enter{options,WM_KEYDOWN,VK_RETURN,1};require(!dispatch_lesson_enter(enter),"Lesson Enter handling intercepted the multiline CMake options field");
        click(config,405);await([&]{return IsWindowEnabled(configure);},"CMake Configure did not finish");require(IsWindowEnabled(build)&&!IsWindowEnabled(run)&&edit_text(child(view,1034)).find("todavía no se ha construido")!=std::string::npos,"Configuration incorrectly built/executed the program or failed to enable Build");
        click(config,406);await([&]{return IsWindowEnabled(configure);},"CMake Build did not finish");require(IsWindowEnabled(run)&&edit_text(child(view,1034))=="Objetivo construido. Ejecútalo para observar su comportamiento.","A current real CMake executable did not enable Run or left a stale building status");
        click(view,1011);await([&]{return IsWindowEnabled(run);},"CMake Execute did not finish");require(edit_text(child(view,1014))=="CMake listo"&&edit_text(child(view,1004)).empty()&&edit_text(child(view,1034))=="Ejecución terminada (código 0).","CMake Execute failed, invented an output activity or retained the in-progress status");
        const auto program=edit_text(source);const auto changed=wide(program+"// revised\n");SendMessageW(source,EM_SETSEL,0,-1);SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(changed.c_str()));pump();require(IsWindowEnabled(build)&&!IsWindowEnabled(run)&&edit_text(child(view,1034)).starts_with("El proyecto cambió."),"Editing a CMake source did not invalidate only the built artifact or update its original status");
        click(child(view,1005),1001);const auto original_configuration=edit_text(source);const auto bad_configuration=wide("cmake_minimum_required(VERSION 3.24)\nmessage(FATAL_ERROR \"QA configure failure\")\n");
        SendMessageW(source,EM_SETSEL,0,-1);SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(bad_configuration.c_str()));pump();
        click(config,405);await([&]{return IsWindowEnabled(configure);},"The real failing CMake configuration never completed");
        require(edit_text(child(view,1034)).find("la fase terminó con código ")!=std::string::npos&&!IsWindowEnabled(run)&&!IsWindowEnabled(build)&&edit_text(child(view,1004)).empty(),
            "A failed CMake configuration retained its busy status, enabled stale execution or became an incorrect answer");
        const auto restored_configuration=wide(original_configuration);SendMessageW(source,EM_SETSEL,0,-1);SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(restored_configuration.c_str()));pump();
        click(config,405);await([&]{return IsWindowEnabled(configure);},"Correcting the project after configuration failure could not retry");
        require(IsWindowEnabled(build)&&edit_text(child(view,1034)).find("todavía no se ha construido")!=std::string::npos,"CMake configuration did not recover using the corrected current files");
        require(navigate_lesson_view(view,2),"Cannot open the CMake output-check activity");pump();run=child(view,1011);require(IsWindowEnabled(run)&&caption(run)=="Compilar, ejecutar y comprobar","A CMake output activity did not offer its actual combined action before building");
        click(view,1011);await([&]{return IsWindowEnabled(run);},"The combined CMake output activity did not finish");require(edit_text(child(view,1004))=="✓ Correcto\n\nSe comprobó el ejecutable previsto.","The fresh CMake execution was not matched to its authored executable target");
        require(edit_text(child(view,1014))=="CMake listo","The checked CMake goal lost its stable stdout");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(1079,BN_CLICKED),0);pump();
        require(edit_text(child(view,1014))=="detalle del objetivo CMake","The checked CMake target did not expose its own successful runtime stderr independently");
        require(navigate_lesson_view(view,0),"Backward navigation lost the previous project");pump();require(edit_text(child(view,1010)).find("header edited")!=std::string::npos&&edit_text(child(view,1015))==" \t \n"&&edit_text(child(view,1004))==valid_feedback,"Page reopening lost the selected header, exact input or persistent correct explanation");
        click(view,1012);pump();require(edit_text(child(view,1004)).empty(),"Explicit project Reset did not revoke the completed objective");
        cleanup();require(environment.paths.root.parent_path().filename()==L"qa"&&environment.paths.root.parent_path().parent_path().filename()==L"Codex","Project QA profile was not isolated from real student data");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"project-lesson-");
    }catch(...){cleanup();throw;}
}
}



void verify_unrelated_cpp_multicase_feedback(HWND host){
    // A normal C++ objective with two input cases must NEVER receive the
    // U3 History instructions after its first correct-but-partial execution.
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"cpp-other-cases-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto data=store.load();data.preferences["native_windows"]["ui_language"]=std::string("es");
    store.save(data);
    const auto lesson=windows::parse_lesson_json(R"json({
      "id":"qa_cpp_two_independent_inputs","title":"QA C++ sin historial","compilerPuzzlesRequired":true,
      "pages":[{"blocks":[{"id":"program","type":"cpp_compiler",
        "initialCode":"#include <iostream>\nint main(){ int numero{}; if(!(std::cin>>numero))return 2; std::cout << numero*2; return 0; }\n",
        "initialStandardInput":"2",
        "outputCheck":{"cases":[
          {"standardInput":"2","outputs":["4"],"explanation":"Primer dato comprobado."},
          {"standardInput":"3","outputs":["6"],"explanation":"Segundo dato comprobado."}
        ]}
      }]}]})json");
    require(lesson.pages.size()==1&&lesson.pages[0].blocks.size()==1,
        "The independent two-case C++ QA fixture changed shape");
    const auto& block=lesson.pages[0].blocks[0];
    HWND view=create_lesson_view(host,environment,lesson);
    require(view!=nullptr,"Could not open the independent multicase C++ lesson");
    const auto cleanup=[&]{if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}};
    try{
        MoveWindow(view,0,0,920,720,TRUE);pump();
        auto source=child(view,1010),run=child(view,1011),input=child(view,1015);
        require(source&&run&&input&&IsWindowEnabled(run)&&edit_text(input)=="2",
            "The real C++ lesson did not expose its prepared standard input");
        const auto feedback=GetDlgItem(GetParent(source),1004);
        require(feedback&&GetParent(feedback)==GetParent(source),
            "The independent C++ feedback is not in the lesson pane");
        click(view,1011);
        await([&]{return IsWindowEnabled(run);},"First multicase C++ execution did not complete");
        auto saved=read_lesson_state(view);
        require(windows::compiler_puzzle_case_count(saved,lesson,block)==1&&
            !windows::compiler_puzzle_completed(saved,lesson,block)&&
            edit_text(child(view,1014))=="4",
            "The first real C++ input was not recorded as an incomplete 1/2 objective");
        const auto first_feedback=edit_text(feedback);
        require(first_feedback.find("Historial")==std::string::npos &&
            first_feedback.find("historial")==std::string::npos &&
            first_feedback.find("secuencia")==std::string::npos,
            "An unrelated partial C++ objective displayed the U3 History sequence instructions");
        set_edit_text(input,"3");pump();
        click(view,1011);
        await([&]{return IsWindowEnabled(run);},"Second multicase C++ execution did not complete");
        saved=read_lesson_state(view);
        require(windows::compiler_puzzle_case_count(saved,lesson,block)==2&&
            windows::compiler_puzzle_completed(saved,lesson,block)&&
            edit_text(child(view,1014))=="6",
            "A second valid C++ input failed to complete its original two-case objective");
        require(flush_lesson_view(view),"The independent two-case objective was not saved");
        cleanup();
        view=create_lesson_view(host,environment,lesson);
        require(view!=nullptr,"The independent two-case C++ lesson did not reopen");
        MoveWindow(view,0,0,920,720,TRUE);pump();
        require(windows::compiler_puzzle_completed(read_lesson_state(view),lesson,block),
            "The normal two-case success was lost on reopening");
        cleanup();
        require(environment.paths.root.parent_path().filename()==L"qa"&&
            environment.paths.root.parent_path().parent_path().filename()==L"Codex",
            "Multicase C++ QA profile escaped Codex/qa");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"cpp-other-cases-");
    }catch(...){cleanup();throw;}
}


std::string legacy_u3_without_workflow(std::string source) {
    // Git may checkout this JSON with LF or CRLF on different platforms.
    // Remove only the one published root declaration, preserving every other
    // byte including the historical editor and stateLayout payload.
    const std::string field=R"(  "compilerWorkflowChecks": [{"activity": "block_0010_0003", "kind": "cmakeSelectiveRecoveryV1"}],)";
    const auto start=source.find(field);
    require(start!=std::string::npos &&
            source.find(field,start+field.size())==std::string::npos,
        "The published U3 workflow must have exactly one root declaration");
    auto end=start+field.size();
    if(source.compare(end,2,"\r\n")==0)end+=2;
    else if(source.compare(end,1,"\n")==0)++end;
    else throw std::runtime_error("The published U3 workflow has no isolated line boundary");
    source.erase(start,end-start);
    return source;
}

void verify_declared_u3_compiler_workflow(const std::filesystem::path& authored_lesson) {
    // Load the genuine 13-page curriculum. Until Work publishes the root
    // declaration, simulate it only in memory, never in student profiles.
    const auto published=windows::load_lesson(authored_lesson);
    require(published.pages.size()==13&&published.pages[10].blocks.size()>3,
        "The authentic U3 recovery owner is missing");
    const auto& previous=published.pages[10].blocks[3];
    require(previous.compiler_workflow_check==curriculum::CompilerWorkflowCheck::cmake_selective_recovery_v1&&
            windows::compiler_selective_history_required(published,previous),
        "The published U3 editor has no valid explicit History contract");
    std::string declared_json=published.raw_json;
    // Removing the root declaration in memory reproduces an older published
    // asset. That older original keeps its legacy History gate and state keys.
    const auto legacy_json=legacy_u3_without_workflow(declared_json);
    const auto legacy=windows::parse_lesson_json(legacy_json,"es");
    require(!legacy.pages[10].blocks[3].compiler_workflow_check&&
        windows::compiler_selective_history_required(legacy,legacy.pages[10].blocks[3]),
        "Previous published U3 lessons no longer protect selective recovery");
    const auto declared=windows::parse_lesson_json(declared_json,"es");
    const auto progress=windows::parse_lesson_progress_metadata(declared_json,"es");
    const auto& owner=declared.pages[10].blocks[3];
    const auto& summary=progress.pages[10].blocks[3];
    require(owner.compiler_workflow_check==curriculum::CompilerWorkflowCheck::cmake_selective_recovery_v1&&
            summary.compiler_workflow_check==owner.compiler_workflow_check&&
            windows::compiler_selective_history_required(declared,owner)&&
            windows::compiler_selective_history_required(progress,summary),
        "The full and progress readers did not honor the same authored U3 workflow");
    require(owner.raw_json==legacy.pages[10].blocks[3].raw_json&&
            owner.raw_json==previous.raw_json&&owner.project_files==previous.project_files&&
            owner.address.page==previous.address.page&&owner.address.block==previous.address.block&&
            owner.address.addition_namespace==previous.address.addition_namespace&&
            curriculum::exercise_key(declared,owner)==curriculum::exercise_key(published,previous)&&
            curriculum::reference_objective_frame(declared,owner)==
                curriculum::reference_objective_frame(published,previous),
        "The U3 root declaration changed the original response or stored completion identity");
    auto rejects=[&](std::string_view from,std::string_view replacement) {
        auto invalid=declared_json;
        const auto at=invalid.find(from);
        require(at!=std::string::npos,"Missing source token for negative curriculum test");
        invalid.replace(at,from.size(),replacement);
        bool rejected=false;
        try {(void)windows::parse_lesson_json(invalid,"es");}
        catch(const std::exception&){rejected=true;}
        require(rejected,"The runtime parser accepted a forged curricular workflow");
    };
    rejects("cmakeSelectiveRecoveryV1","unrecognizedWorkflow");
    rejects("block_0010_0003\",\"kind","block_0003_0004\",\"kind");
    rejects("\"kind\":\"cmakeSelectiveRecoveryV1\"",
            "\"kind\":\"cmakeSelectiveRecoveryV1\",\"extra\":true");
    {
        auto duplicated=declared_json;
        const std::string item="{\"activity\":\"block_0010_0003\",\"kind\":\"cmakeSelectiveRecoveryV1\"}";
        const auto at=duplicated.find(item);
        require(at!=std::string::npos,"Cannot locate the curricular declaration to duplicate");
        duplicated.insert(at+item.size(),","+item);
        bool rejected=false;
        try {(void)windows::parse_lesson_json(duplicated,"es");}
        catch(const std::exception&){rejected=true;}
        require(rejected,"The runtime parser accepted duplicate History owners");
    }
    {
        bool rejected=false;
        try {(void)windows::parse_lesson_json(declared_json,"fr");}
        catch(const std::exception&){rejected=true;}
        require(rejected,"The Spanish U3 History contract was accepted under another locale");
    }
}

void verify_unmodified_thirteen_page_entry_gates(HWND host,const std::filesystem::path& authored_lesson){
    const auto published=windows::load_lesson(authored_lesson);
    require(published.pages.size()==13&&published.pages[10].blocks.size()==5,
        "The authentic U3 lesson lost its thirteen-page layout");
    const auto& owner=published.pages[10].blocks[3];
    require(owner.stable_id=="block_0010_0003"&&owner.type==curriculum::BlockType::cpp_compiler
        &&owner.output_check&&owner.cmake,"The exact U3 recovery owner is absent");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"u3-original-gates-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto initial=store.load();
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    store.save(initial);
    HWND view=create_lesson_view(host,environment,published);
    require(view!=nullptr,"The full thirteen-page lesson cannot be opened in isolated QA");
    const auto cleanup=[&]{
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}
    };
    try{
        MoveWindow(view,0,0,900,700,TRUE);pump();
        const auto before=lesson_page_state(view);
        const auto options=lesson_options_snapshot(view);
        require(before.count==13&&before.index==0&&
            options.page_count==13&&options.current_page==0&&!options.finish_enabled,
            "A fresh profile entered U3 after its first page or enabled Finish");
        require(std::ranges::find(options.available_pages,std::size_t{10})==options.available_pages.end()
            &&std::ranges::find(options.available_pages,std::size_t{11})==options.available_pages.end()
            &&std::ranges::find(options.available_pages,std::size_t{12})==options.available_pages.end(),
            "The real Pages menu exposed gated U3 content in an unstarted profile");
        require(!navigate_lesson_view(view,10)&&!navigate_lesson_view(view,12),
            "The real page-navigation API bypassed earlier unanswered puzzles");
        require(lesson_page_state(view).index==0&&
            !windows::compiler_puzzle_completed(read_lesson_state(view),published,owner),
            "A rejected navigation fabricated completion or changed the current page");
        cleanup();
        view=create_lesson_view(host,environment,published);
        require(view!=nullptr,"The unchanged original lesson could not reopen");
        MoveWindow(view,0,0,900,700,TRUE);pump();
        const auto reopened=lesson_page_state(view);
        require(reopened.count==13&&reopened.index==0&&
            !navigate_lesson_view(view,10)&&
            !windows::compiler_puzzle_completed(read_lesson_state(view),published,owner),
            "Reopening the complete lesson bypassed an unanswered prerequisite");
        cleanup();
        require(environment.paths.root.parent_path().filename()==L"qa" &&
            environment.paths.root.parent_path().parent_path().filename()==L"Codex",
            "Full-lesson QA profile unexpectedly escaped Codex/qa");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"u3-original-gates-");
    }catch(...){cleanup();throw;}
}


void verify_genuine_u3_first_prerequisites(HWND host,const std::filesystem::path& authored_lesson){
    // Work through the ORIGINAL first three pages using the actual radio
    // controls and Check actions. No direct response writes, page unlocks,
    // pre-marked completions, or altered source/lesson JSON are permitted.
    const auto published=windows::load_lesson(authored_lesson);
    require(published.pages.size()==13,"The U3 QA fixture lost its complete published pages");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"u3-prerequisites-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto initial=store.load();
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    store.save(initial);
    HWND view=create_lesson_view(host,environment,published);
    require(view!=nullptr,"The original thirteen-page lesson did not open in isolated QA");
    const auto cleanup=[&]{if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}};
    try{
        MoveWindow(view,0,0,1000,760,TRUE);pump();
        require(lesson_page_state(view).index==0&&!navigate_lesson_view(view,3),
            "A fresh U3 reader bypassed prerequisites before any answer");
        std::size_t credited=0;
        for(std::size_t page=0;page<3;++page){
            require(lesson_page_state(view).index==page,"U3 prerequisite QA arrived at a different page");
            const auto& blocks=published.pages[page].blocks;
            std::size_t page_exercises=0;
            for(std::size_t index=0;index<blocks.size();++index){
                const auto& exercise=blocks[index];
                if(exercise.type!=curriculum::BlockType::multiple_choice)continue;
                ++page_exercises;
                require(exercise.correct_index&&*exercise.correct_index<exercise.options.size(),
                    "The published U3 prerequisite has no valid authored correct option");
                require(!navigate_lesson_view(view,page+1),
                    "The authentic lesson allowed progression before checking a required answer");
                const int radio_id=1000+static_cast<int>(index)*100+20+
                    static_cast<int>(*exercise.correct_index)*2;
                const int check_id=1000+static_cast<int>(index)*100+3;
                click(view,radio_id);
                require(child(view,check_id)&&IsWindowEnabled(child(view,check_id)),
                    "A selected real U3 answer did not enable its Check control");
                click(view,check_id);
                require(caption(child(view,check_id)).find("Correcto")!=std::string::npos,
                    "The published answer was not accepted by the real Win32 evaluator");
                ++credited;
            }
            require(page_exercises>0,"U3 prerequisite QA accidentally skipped a page without exercises");
            require(navigate_lesson_view(view,page+1),
                "Correctly checked U3 predecessor exercises did not release the next page");
        }
        require(credited==5&&lesson_page_state(view).index==3,
            "The U3 learner did not actually complete all five first-page prerequisite checks");
        const auto& first_compiler=published.pages[3].blocks[4];
        require(first_compiler.type==curriculum::BlockType::cpp_compiler&&first_compiler.output_check,
            "The first mandatory CMake editor moved off the original page four");
        require(!windows::compiler_puzzle_completed(read_lesson_state(view),published,first_compiler)
            &&!navigate_lesson_view(view,4)&&!navigate_lesson_view(view,10),
            "Answering the introductory U3 questions incorrectly credited or bypassed its CMake laboratory");
        require(flush_lesson_view(view),"The QA profile could not persist the real checked answers");
        cleanup();
        view=create_lesson_view(host,environment,published);
        require(view!=nullptr,"The original U3 lesson did not reopen after its introductory answers");
        MoveWindow(view,0,0,1000,760,TRUE);pump();
        require(lesson_page_state(view).index==3
            &&!navigate_lesson_view(view,4)&&!navigate_lesson_view(view,10)
            &&!windows::compiler_puzzle_completed(read_lesson_state(view),published,first_compiler),
            "The reopened reader lost the correct entry point or bypassed the first CMake requirement");
        cleanup();
        require(environment.paths.root.parent_path().filename()==L"qa"&&
                environment.paths.root.parent_path().parent_path().filename()==L"Codex",
            "Original U3 prerequisite QA escaped its isolated profile");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"u3-prerequisites-");
    }catch(...){cleanup();throw;}
}


void verify_genuine_u3_first_cmake_project(HWND host,const std::filesystem::path& authored_lesson) {
    // A real student path from page one: five checked questions followed by
    // edits in BOTH original CMakeLists and two fresh compiled target runs.
    // The page-four project must not be substituted by a detached QA fixture.
    const auto published=windows::load_lesson(authored_lesson);
    require(published.pages.size()==13&&published.pages[3].blocks.size()==6,
        "The published U3 reader lost its original CMake page");
    const auto& project_block=published.pages[3].blocks[4];
    const auto& next_project=published.pages[4].blocks[12];
    require(project_block.stable_id=="block_0003_0004"&&
        project_block.type==curriculum::BlockType::cpp_compiler&&
        project_block.project_files.size()==6&&project_block.output_check&&
        project_block.output_check->cases.size()==2&&next_project.project_files.size()==6,
        "The original page-four CMake project or its two executable cases changed");
    // The next page already contains the correctly distributed example.
    // The source code of both executables, the library and the seven expected
    // observations must be identical before and after this organization task.
    for(std::size_t i=2;i<6;++i)
        require(project_block.project_files[i]==next_project.project_files[i],
            "The reviewed distribution altered non-CMake sources");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"u3-first-cmake-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto initial=store.load();
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    store.save(initial);
    HWND view=create_lesson_view(host,environment,published);
    require(view!=nullptr,"U3 full lesson could not open for original CMake QA");
    const auto cleanup=[&]{if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}};
    try{
        MoveWindow(view,0,0,1100,780,TRUE);pump();
        for(std::size_t page=0;page<3;++page){
            require(lesson_page_state(view).index==page,"U3 QA left the expected introductory page");
            for(std::size_t i=0;i<published.pages[page].blocks.size();++i){
                const auto& item=published.pages[page].blocks[i];
                if(item.type!=curriculum::BlockType::multiple_choice)continue;
                require(item.correct_index&&*item.correct_index<item.options.size(),
                    "A published prerequisite has no valid correct option");
                click(view,1000+static_cast<int>(i)*100+20+static_cast<int>(*item.correct_index)*2);
                click(view,1000+static_cast<int>(i)*100+3);
            }
            require(navigate_lesson_view(view,page+1),
                "The correctly answered U3 prerequisites did not permit natural progress");
        }
        require(lesson_page_state(view).index==3&&!navigate_lesson_view(view,4),
            "The page-four CMake activity was not a required barrier");
        constexpr int project_id=1405,source_id=1410,run_id=1411,config_id=1406;
        auto* project=child(view,project_id);
        auto* source=child(view,source_id);
        auto* run=child(view,run_id);
        auto* config=child(view,config_id);
        require(project&&source&&run&&config,"The original U3 CMake editor is not rendered");
        const auto edit=[&](const std::string& content) {
            const auto value=wide(content);
            SendMessageW(source,EM_SETSEL,0,-1);
            SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(value.c_str()));
            pump();
            require(edit_text(source)==content,"The real CMake source control did not adopt the revision");
        };
        const auto& root=next_project.project_files[0].second;
        const auto& child_source=next_project.project_files[1].second;
        require(project_block.project_files[0].second!=root &&
                project_block.project_files[1].second.empty()&&!child_source.empty(),
            "The CMake lesson lost its initial flat project or reviewed child");
        click(project,1000);edit(root);
        click(project,1001);edit(child_source);
        for(int i=2;i<6;++i){
            click(project,1000+i);
            require(edit_text(source)==project_block.project_files[static_cast<std::size_t>(i)].second,
                "Reorganizing CMake unexpectedly changed an original C++ file");
        }
        require(!windows::compiler_puzzle_completed(read_lesson_state(view),published,project_block),
            "Changing the build description alone granted an execution receipt");
        auto* target=child(config,400);
        auto* configure=child(config,405);
        auto* build=child(config,406);
        require(target&&configure&&build,"The original CMake target and phase controls disappeared");
        set_edit_text(target,"comprobar_cajas");pump();
        click(config,405);
        await([&]{return IsWindowEnabled(configure);},
            "The reorganized CMake project did not complete Configure/Generate");
        require(IsWindowEnabled(build),"The verified CMake description did not enable target build");
        click(config,406);
        await([&]{return IsWindowEnabled(configure);},
            "The real seven-case CMake target did not finish building");
        require(IsWindowEnabled(run),"The built U3 target was not executable");
        click(view,run_id);
        await([&]{return IsWindowEnabled(run);},
            "The original seven-case checker did not complete its real run");
        auto state=read_lesson_state(view);
        require(windows::compiler_puzzle_case_count(state,published,project_block)==1&&
                !windows::compiler_puzzle_completed(state,published,project_block)&&
                edit_text(child(view,1414))==project_block.output_check->cases[0].outputs[0],
            "The first real CMake target did not earn exactly one of two cases");
        require(!navigate_lesson_view(view,4),
            "Only the checker ran; the reader skipped the remaining client target");
        set_edit_text(target,"consultar_cajas");pump();
        click(view,run_id);
        await([&]{return IsWindowEnabled(run);},
            "The real U3 client did not finish its checked CMake run");
        state=read_lesson_state(view);
        require(windows::compiler_puzzle_case_count(state,published,project_block)==2&&
                windows::compiler_puzzle_completed(state,published,project_block)&&
                edit_text(child(view,1414))==project_block.output_check->cases[1].outputs[0],
            "Two distinct CMake targets failed to preserve the full two-case achievement");
        // Reopen this exact original page to make any same-page dependent
        // multiple-choice control visible before answering the next task.
        require(navigate_lesson_view(view,2)&&navigate_lesson_view(view,3),
            "Page-four reentry failed after both target executions");
        const auto& question=published.pages[3].blocks[5];
        require(question.type==curriculum::BlockType::multiple_choice&&question.correct_index,
            "The published CMake page lost its after-laboratory question");
        click(view,1000+5*100+20+static_cast<int>(*question.correct_index)*2);
        click(view,1000+5*100+3);
        require(navigate_lesson_view(view,4)&&lesson_page_state(view).index==4,
            "Two CMake executions and the next real question did not unlock page five");
        require(!navigate_lesson_view(view,10)&&
            !windows::compiler_puzzle_completed(read_lesson_state(view),published,published.pages[10].blocks[3]),
            "The first CMake task falsely credited or unlocked U3 History");
        require(flush_lesson_view(view),"The CMake QA profile could not save its real progress");
        cleanup();
        view=create_lesson_view(host,environment,published);
        require(view!=nullptr,"Original U3 could not reopen after the two CMake targets");
        MoveWindow(view,0,0,1100,780,TRUE);pump();
        require(lesson_page_state(view).index==4&&
            windows::compiler_puzzle_completed(read_lesson_state(view),published,project_block)&&
            !navigate_lesson_view(view,10),
            "Reopening lost the legitimate CMake achievement or bypassed later pages");
        cleanup();
        require(environment.paths.root.parent_path().filename()==L"qa"&&
                environment.paths.root.parent_path().parent_path().filename()==L"Codex",
            "U3 original CMake acceptance escaped its isolated QA profile");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"u3-first-cmake-");
    }catch(...){cleanup();throw;}
}

void integrated_selective_history_lesson(HWND host,const std::filesystem::path& authored_lesson,
        bool declared_workflow=false){
    // Both variants load the genuine 13-page lesson and exercise History
    // through its published editor. The historical variant removes only the
    // new root declaration in memory, never the actual student lesson.
    auto published=windows::load_lesson(authored_lesson);
    if(!declared_workflow){
        // Reuse the exact historical grading route without rewriting the
        // published asset, so the two CI cases cover both compatibility eras.
        published=windows::parse_lesson_json(
            legacy_u3_without_workflow(published.raw_json),"es");
    }else require(published.pages.at(10).blocks.at(3).compiler_workflow_check==
                        curriculum::CompilerWorkflowCheck::cmake_selective_recovery_v1,
            "The integrated U3 lesson did not load the published History contract");
    require(published.id=="programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador"
        &&published.pages.size()==13&&published.pages[10].blocks.size()>=4,
        "The published U3L2 lesson structure differs from the reviewed owner");
    const auto owner=published.pages[10].blocks[3];
    require(owner.stable_id=="block_0010_0003"&&owner.project_files.size()==6
        &&owner.cmake&&owner.cmake->target=="comprobar_cajas"
        &&owner.output_check&&owner.output_check->cases.size()==1,
        "The real recovery editor or its checked CMake target was replaced");
    auto focused=published;
    focused.pages={published.pages[10]};
    focused.pages[0].blocks={owner};
    focused.state_layout={};focused.reference_after.clear();
    focused.activity_after.clear();focused.retired_blocks.clear();
    focused.text_semantics.clear();focused.raw_json.clear();
    focused.compiler_puzzles_required=true;
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"u3-history-lesson-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto initial=store.load();
    initial.preferences["native_windows"]["ui_language"]=std::string("es");
    store.save(initial);
    HWND view=create_lesson_view(host,environment,focused);
    require(view!=nullptr,"Cannot create the real focused U3 CMake lesson view");
    const auto cleanup=[&]{
        if(view){(void)flush_lesson_view(view);DestroyWindow(view);view=nullptr;}
    };
    try {
        MoveWindow(view,0,0,1000,760,TRUE);pump();
        auto* project=child(view,1005);
        auto* source=child(view,1010);
        auto* run=child(view,1011);
        auto* config=child(view,1006);
        require(project&&source&&run&&config&&IsWindowEnabled(run),
            "The published U3 owner lacks its real project/CMake/run controls");
        // The six project tabs reuse IDs 1000..1005. In particular, tab 1004
        // is NOT the lesson feedback control 1004. Resolve feedback from the
        // lesson pane, where the source editor itself is a direct child.
        const auto feedback_item=GetDlgItem(GetParent(source),1004);
        require(feedback_item&&GetParent(feedback_item)==GetParent(source)&&
            feedback_item!=child(project,1004),
            "The focused lesson QA selected a project tab instead of the feedback RichEdit");
        const auto replace_source=[&](const std::string& content){
            const auto wide_source=wide(content);
            SendMessageW(source,EM_SETSEL,0,-1);
            SendMessageW(source,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(wide_source.c_str()));
            pump();
            require(edit_text(source)==content,"The native source editor did not adopt the changed bytes");
        };
        // This initial program already produces seven correct cases. It MUST
        // NOT unlock the U3 history task without recording and comparing.
        click(view,1011);
        await([&]{return IsWindowEnabled(run);},"Initial stdout-only CMake run did not finish");
        require(!windows::compiler_puzzle_completed(read_lesson_state(view),focused,owner),
            "A seven-case stdout match granted selective-history completion");
        const auto initial_feedback=edit_text(feedback_item);
        if(initial_feedback.find("falta acreditar la secuencia")==std::string::npos) {
            const auto initial_status=edit_text(child(view,1034));
            const auto initial_console=edit_text(child(view,1014));
            curriculum::FreshExecution isolated;
            isolated.source_revision=1;
            isolated.compilation_succeeded=true;
            isolated.execution_succeeded=true;
            isolated.execution_completed=true;
            isolated.exit_code=0;
            isolated.executable=owner.cmake->target;
            isolated.standard_input="";
            isolated.standard_output=initial_console;
            const auto isolated_verdict=windows::evaluate_cpp_output_check(
                *owner.output_check,isolated,isolated.source_revision);
            const auto& expected_output=owner.output_check->cases.front().outputs.front();
            std::cerr<<"U3 INITIAL QA feedback="<<initial_feedback.substr(0,900)
                <<" status="<<initial_status.substr(0,900)
                <<" visible_verdict="<<static_cast<int>(isolated_verdict.verdict)
                <<" visible_output_bytes="<<initial_console.size()
                <<" expected_bytes="<<expected_output.size()
                <<" visible_sha="<<state::sha256(initial_console)
                <<" expected_sha="<<state::sha256(expected_output)
                <<" console="<<initial_console.substr(0,1500)<<"\\n";
        }
        require(initial_feedback.find("falta acreditar la secuencia")!=std::string::npos,
            "The real reader did not explain why stdout alone is insufficient");

        {ModalScenario modal(host,ModalScenario::Kind::record);click(project,302);modal.verified();}
        // Both edits must occur in the very same existing project editor.
        click(project,1000);
        const std::string comment="# Comentario independiente de la prueba U3\n";
        replace_source(owner.project_files[0].second+comment);
        click(project,1003);
        auto defective=owner.project_files[3].second;
        const std::string original_formula="return (piezas + 3) / 4;";
        const auto position=defective.find(original_formula);
        require(position!=std::string::npos&&defective.find(original_formula,position+1)==std::string::npos,
            "The published C++ source has no unique original formula");
        defective.replace(position,original_formula.size(),"return piezas / 4;");
        replace_source(defective);

        const auto configure=child(config,405);
        const auto build=child(config,406);
        require(configure&&build,"CMake configuration buttons disappeared");
        if(!IsWindowEnabled(build)){
            click(config,405);
            await([&]{return IsWindowEnabled(configure);},"Defective U3 CMake Configure did not finish");
        }
        require(IsWindowEnabled(build),"CMake could not build the modified project");
        click(config,406);
        await([&]{return IsWindowEnabled(configure);},"Defective U3 CMake Build did not finish");
        click(view,1011);
        await([&]{return IsWindowEnabled(run);},"Defective U3 CMake checked run did not finish");
        require(!windows::compiler_puzzle_completed(read_lesson_state(view),focused,owner),
            "A failing intentional execution granted a completed U3 objective");
        // These are genuine comparison dialogs. The root comment and altered
        // formula must both appear in independent diffs of ONE revision.
        {ModalScenario modal(host,ModalScenario::Kind::compare,"CMakeLists.txt",
            "target_link_libraries(comprobar_cajas PRIVATE envios)",comment);
            click(project,302);modal.verified();}
        {ModalScenario modal(host,ModalScenario::Kind::compare,"componente/src/cajas.cpp",
            original_formula,"return piezas / 4;");
            click(project,302);modal.verified();}
        {ModalScenario modal(host,ModalScenario::Kind::restore,"componente/src/cajas.cpp",
            original_formula,"return piezas / 4;");
            click(project,302);modal.verified();}
        require(edit_text(source)==owner.project_files[3].second,
            "Recovering the selected file did not restore the exact original C++ bytes");
        click(project,1000);
        require(edit_text(source)==owner.project_files[0].second+comment,
            "Recovering one file removed the independent CMake comment");
        if(!IsWindowEnabled(build)){
            click(config,405);
            await([&]{return IsWindowEnabled(configure);},"CMake could not reconfigure the recovered draft");
        }
        require(IsWindowEnabled(build),"The restored source did not enable a new build");
        click(config,406);
        await([&]{return IsWindowEnabled(configure);},"Final U3 CMake Build did not finish");
        click(view,1011);
        await([&]{return IsWindowEnabled(run);},"Final checked U3 CMake run did not finish");
        const auto completed=read_lesson_state(view);
        require(windows::compiler_puzzle_completed(completed,focused,owner),
            "The actual recorded/compared/restored/build/checked U3 reader sequence earned no completion");
        require(windows::compiler_puzzle_case_count(completed,focused,owner)==1,
            "The final authenticated U3 run has no current program-case receipt");
        require(flush_lesson_view(view),"The correct U3 completion was not persisted");
        cleanup();
        view=create_lesson_view(host,environment,focused);
        require(view!=nullptr,"Could not reopen the same isolated U3 lesson");
        MoveWindow(view,0,0,1000,760,TRUE);pump();
        require(windows::compiler_puzzle_completed(read_lesson_state(view),focused,owner),
            "Reopening the real lesson discarded its new history-completion receipt");
        click(view,1012);pump();
        require(!windows::compiler_puzzle_completed(read_lesson_state(view),focused,owner),
            "Explicit Reiniciar código failed to revoke the independent U3 completion");
        cleanup();
        require(environment.paths.root.parent_path().filename()==L"qa"&&
            environment.paths.root.parent_path().parent_path().filename()==L"Codex",
            "The integrated test profile escaped Codex/qa");
        cleanup_own_qa(environment.paths.root.parent_path(),environment.paths.root,"u3-history-lesson-");
    }catch(...){cleanup();throw;}
}

int main(int argc,char** argv){
    using namespace lgaed;using namespace presentation;
    const bool internal_only=argc==3&&std::string_view(argv[2])=="--internal-dialogs";
    const bool u3_selective=argc==4&&std::string_view(argv[2])=="--u3-selective";
    const bool u3_selective_declared=argc==4&&std::string_view(argv[2])=="--u3-selective-declared";
    const bool u3_gates=argc==4&&std::string_view(argv[2])=="--u3-gates";
    const bool u3_declaration=argc==4&&std::string_view(argv[2])=="--u3-declaration";
    const bool u3_prerequisites=argc==4&&std::string_view(argv[2])=="--u3-prerequisites";
    const bool u3_first_cmake=argc==4&&std::string_view(argv[2])=="--u3-first-cmake";
    const bool other_cases=argc==3&&std::string_view(argv[2])=="--other-cases";
    HWND host=nullptr;HMODULE rich=nullptr;
    try{require(argc==2||internal_only||u3_selective||u3_selective_declared||u3_gates||u3_declaration||u3_prerequisites||u3_first_cmake||other_cases,
        "Provide isolated QA output, optionally --internal-dialogs, --other-cases, --u3-selective or --u3-gates <lesson-json>");winrt::init_apartment(winrt::apartment_type::single_threaded);reader_theme::prepare_fonts();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);rich=LoadLibraryW(L"Msftedit.dll");require(rich!=nullptr,"RichEdit is missing");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,L"STATIC",L"Project controls QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);require(host!=nullptr,"Cannot create app-owned QA host");
        const auto editor_window=CreateWindowExW(0,MSFTEDIT_CLASS,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_AUTOHSCROLL|ES_AUTOVSCROLL,0,250,800,240,host,nullptr,GetModuleHandleW(nullptr),nullptr);require(editor_window!=nullptr,"Cannot create the existing source editor");
        windows::CodeEditor editor(editor_window,{},true);
        curriculum::Block block;block.type=curriculum::BlockType::cpp_compiler;block.project_files={{"main.cpp","    return 0;\n"},{"include/value.hpp","// header\n"}};block.source_files={"main.cpp"};
        const auto history_root=std::filesystem::absolute(argv[1])/("cpp-project-view-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        std::string project_json,cmake_json;int source_changes=0,view_changes=0;std::vector<windows::CppRunRequest::Action> actions;
        std::vector<windows::CppHistoryRecoveryEvidence> recovery_events;
        std::vector<windows::CppHistoryComparisonEvidence> comparison_events;
        std::vector<windows::CppHistoryRecordEvidence> recorded_events;
        const CppProjectCallbacks events{[&](std::string value,std::string configuration){project_json=std::move(value);cmake_json=std::move(configuration);},
            [&](bool changed){changed?++source_changes:++view_changes;},
            [&](auto action){actions.push_back(action);},
            [&](const windows::CppHistoryRecoveryEvidence& event){recovery_events.push_back(event);},
            [&](const windows::CppHistoryComparisonEvidence& event){comparison_events.push_back(event);},
            [&](const windows::CppHistoryRecordEvidence& event){recorded_events.push_back(event);}};
        {
            CppProjectView project(host,block,editor_window,editor,"","",history_root,"qa/project","es",events);MoveWindow(project.window(),0,0,800,project.height_for_width(800),TRUE);
            require(editor.document().text==block.project_files[0].second&&editor.document().indentation_starts==std::vector<std::int64_t>{0},"Initial main.cpp lost its original text or predefined tab origin");
            require(!project.configuration_window(),"A direct project acquired CMake controls");windows::CppRunRequest snapshot;project.snapshot(snapshot);require(snapshot.project_files==block.project_files&&snapshot.source_files==block.source_files,"The original multi-file compilation snapshot changed");
            {const auto strip=child(project.window(),304);require(strip&&GetParent(child(project.window(),300))==strip,
                    "Project actions are not in their original independent horizontal scroll row");
                const auto dpi=GetDpiForWindow(project.window());const auto narrow=MulDiv(220,static_cast<int>(dpi),96);
                project.set_text_scale(150);MoveWindow(project.window(),0,0,narrow,project.height_for_width(narrow),TRUE);
                RECT add{},remove{},history{},viewport{};GetWindowRect(child(project.window(),300),&add);GetWindowRect(child(project.window(),301),&remove);
                GetWindowRect(child(project.window(),302),&history);GetClientRect(strip,&viewport);
                require(add.right-add.left!=history.right-history.left&&remove.left>add.left&&history.left>remove.left,
                    "New/Delete/History still use artificial equal columns or changed their Android order");
                SCROLLINFO before{sizeof(before),SIF_ALL};require(GetScrollInfo(strip,SB_HORZ,&before)&&before.nMax+1>static_cast<int>(before.nPage),
                    "Long localized project actions lost their actual horizontal overflow");
                const auto unchanged=project_json;const auto source_before=source_changes;SendMessageW(strip,WM_HSCROLL,SB_RIGHT,0);pump();
                SCROLLINFO after{sizeof(after),SIF_ALL};GetScrollInfo(strip,SB_HORZ,&after);require(after.nPos>before.nPos,
                    "The real action scrollbar does not expose the clipped History action");
                const auto history_button=child(project.window(),302);SendMessageW(history_button,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(20,20));
                SendMessageW(history_button,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(120,20));SendMessageW(strip,WM_LBUTTONUP,0,0);pump();
                GetScrollInfo(strip,SB_HORZ,&after);require(after.nPos<before.nMax-static_cast<int>(before.nPage)+1&&project_json==unchanged&&source_changes==source_before,
                    "Dragging the actions neither scrolled horizontally nor preserved the selected project snapshot");
                project.set_text_scale(100);MoveWindow(project.window(),0,0,800,project.height_for_width(800),TRUE);
                GetScrollInfo(strip,SB_HORZ,&after);if(after.nMax+1<=static_cast<int>(after.nPage)){SCROLLBARINFO bar{sizeof(bar)};
                    require(!GetScrollBarInfo(strip,OBJID_HSCROLL,&bar)||(bar.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)),
                        "A fitting action row retained an unnecessary visible scrollbar");}
            }
            click(project.window(),1001);require(project.selected_file()=="include/value.hpp"&&!IsWindowVisible(child(project.window(),303))&&source_changes==0&&view_changes==1,"Selecting a header changed the program or exposed a source checkbox");
            editor.reset({"// manual header\t \n",{},2,2});project.capture();click(project.window(),1000);require(editor.document().indentation_starts==std::vector<std::int64_t>{0},"Switching files lost predefined tab identity");
            SendMessageW(editor_window,EM_SETSEL,4,4);SendMessageW(editor_window,WM_KEYDOWN,VK_BACK,1);SendMessageW(editor_window,WM_CHAR,L'\b',1);SendMessageW(editor_window,WM_KEYUP,VK_BACK,LPARAM{1}<<31);editor.capture_change();project.capture();require(editor.document().text=="return 0;\n","Backspace did not remove the predefined tab as one unit");
            editor.undo();project.capture();require(editor.document().text==block.project_files[0].second&&editor.document().indentation_starts==std::vector<std::int64_t>{0},"Undo lost per-file indentation identity");
            {ModalScenario scenario(host,ModalScenario::Kind::record);click(project.window(),302);scenario.verified();}
            require(recorded_events.size()==1
                &&recorded_events[0].revision_id.size()==64
                &&recorded_events[0].recorded_file_count==2
                &&recorded_events[0].file_sha256.at("main.cpp")==state::sha256("    return 0;\n")
                &&recorded_events[0].file_sha256.at("include/value.hpp")==state::sha256("// manual header\t \n"),
                "The record UI did not authenticate the persisted two-file snapshot");
            require(recovery_events.empty()&&comparison_events.empty(),
                "Recording a revision claimed an unperformed comparison or recovery");
            editor.reset({"    return 9;\n",{},4,4});project.capture();
            {ModalScenario scenario(host,ModalScenario::Kind::restore);click(project.window(),302);scenario.verified();}
            require(recorded_events.size()==1&&recorded_events[0].revision_id==recovery_events[0].revision_id,
                "History comparison/recovery did not use the revision actually recorded through the same UI");
            require(comparison_events.size()==1&&recovery_events.size()==1&&comparison_events[0].changed
                &&comparison_events[0].revision_id==recovery_events[0].revision_id
                &&comparison_events[0].path=="main.cpp"
                &&comparison_events[0].added_lines==1
                &&comparison_events[0].removed_lines==1
                &&comparison_events[0].revision_sha256==recovery_events[0].after_sha256
                &&comparison_events[0].draft_sha256==recovery_events[0].before_sha256,
                "The actual comparison dialog failed to bind its displayed diff to the selected recovery");
            require(recovery_events.size()==1&&recovery_events[0].changed
                &&recovery_events[0].revision_id.size()==64
                &&recovery_events[0].path=="main.cpp"
                &&recovery_events[0].preserved_other_files==1
                &&recovery_events[0].before_sha256==std::optional<std::string>{state::sha256("    return 9;\n")}
                &&recovery_events[0].after_sha256==std::optional<std::string>{state::sha256("    return 0;\n")},
                "The actual Recover this file dialog omitted or forged the verified operation receipt");
            require(editor.document().text==block.project_files[0].second&&editor.document().indentation_starts==std::vector<std::int64_t>{0},"Restoring the actual history diff lost the original per-file tab metadata");project.snapshot(snapshot);
            require(snapshot.project_files[1].second=="// manual header\t \n","Restoring main.cpp changed the independent header");
            {ModalScenario scenario(host,ModalScenario::Kind::create);click(project.window(),300);scenario.verified();}project.snapshot(snapshot);require(snapshot.project_files.back().first=="src/helper.cpp"&&snapshot.source_files.back()=="src/helper.cpp","Create-file dialog did not update the same project snapshot");
            click(project.window(),301);project.snapshot(snapshot);require(snapshot.project_files.size()==2&&snapshot.source_files==block.source_files,"Deleting one file changed unrelated sources");
            click(project.window(),303);project.snapshot(snapshot);require(snapshot.source_files.empty(),"A direct source could not be excluded as an editable draft");click(project.window(),303);
            editor.reset({"    manual",{},4,4});project.capture();const auto saved_project=project_json;click(project.window(),1001);click(project.window(),1000);require(editor.document().indentation_starts.empty(),"Four manual spaces became a predefined tab after selecting another file");
            SendMessageW(editor_window,WM_KEYDOWN,VK_BACK,1);SendMessageW(editor_window,WM_CHAR,L'\b',1);SendMessageW(editor_window,WM_KEYUP,VK_BACK,LPARAM{1}<<31);editor.capture_change();project.capture();require(editor.document().text=="   manual","Backspace removed four manual spaces as a tab");
            CppProjectView reopened(host,block,editor_window,editor,saved_project,"",history_root,"qa/project","es",events);require(editor.document().text=="    manual"&&editor.document().indentation_starts.empty()&&editor.document().selection_start==4,"Reopening the Android-compatible project draft changed manual spaces or cursor");
            const auto before=source_changes;project.set_language("fr");project.set_text_scale(150);require(source_changes==before,"Language/scale invalidated the source snapshot");
        }
        {auto one=block;one.project_files.resize(1);CppProjectView project(host,one,editor_window,editor,"","",history_root,"qa/single-file","es",events);
            MoveWindow(project.window(),0,0,800,project.height_for_width(800),TRUE);
            // A real pointer click first focuses its enabled toolbar button.
            // The following child notice must retain that focus.
            SetFocus(child(project.window(),301));const auto focused=GetFocus();windows::CppRunRequest before;project.snapshot(before);
            click(project.window(),301);HWND notice=nullptr;
            EnumChildWindows(project.window(),[](HWND item,LPARAM raw)->BOOL{if(IsWindowVisible(item)&&caption(item)=="Conserva al menos un archivo en el proyecto."){*reinterpret_cast<HWND*>(raw)=item;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&notice));
            windows::CppRunRequest after;project.snapshot(after);
            RECT main_bounds{},notice_bounds{};GetClientRect(host,&main_bounds);GetWindowRect(notice,&notice_bounds);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&notice_bounds),2);
            const auto notice_style=GetWindowLongPtrW(notice,GWL_STYLE);
            const bool notice_valid=notice&&GetParent(notice)==project.window()&&GetAncestor(notice,GA_ROOT)==host
                &&(notice_style&WS_CHILD)&&!(notice_style&WS_POPUP)&&(GetWindowLongPtrW(notice,GWL_EXSTYLE)&WS_EX_NOACTIVATE)
                &&notice_bounds.left>=0&&notice_bounds.top>=0&&notice_bounds.right<=main_bounds.right&&notice_bounds.bottom<=main_bounds.bottom
                &&IsWindowEnabled(host)&&GetFocus()==focused&&after.project_files==before.project_files&&after.source_files==before.source_files;
            if(!notice_valid){wchar_t focused_class[80]{};GetClassNameW(GetFocus(),focused_class,80);
                std::cerr<<"DELETE LAST actual: notice="<<static_cast<void*>(notice)<<" style="<<(notice?GetWindowLongPtrW(notice,GWL_EXSTYLE):0)<<" focus="<<static_cast<void*>(focused)<<" -> "<<static_cast<void*>(GetFocus())
                    <<" focus_id="<<GetDlgCtrlID(GetFocus())<<" focus_class="<<utf8(focused_class)<<" delete_button="<<static_cast<void*>(child(project.window(),301))<<" files_equal="<<(after.project_files==before.project_files)<<" file_count="<<after.project_files.size()<<'\n';}
            require(notice_valid,
                "Deleting the last file blocked/took focus or deleted the only remaining source instead of showing Android's brief notice");DestroyWindow(notice);
        }
        block.cmake=curriculum::CmakeConfig{"sample",{{"WITH_TRACE","ON"}},"RelWithDebInfo"};
        {CppProjectView project(host,block,editor_window,editor,"","",history_root,"qa/cmake","en",events);MoveWindow(project.window(),0,0,800,project.height_for_width(800),TRUE);MoveWindow(project.configuration_window(),0,500,800,project.configuration_height_for_width(800),TRUE);
            require(!IsWindowVisible(child(project.window(),303))&&project.configuration_window(),"CMake exposed direct source selection or omitted its settings");windows::CppRunRequest snapshot;project.snapshot(snapshot);require(snapshot.cmake==block.cmake&&snapshot.source_files.empty(),"Authored target/options/build type did not reach the native request");
            require(edit_text(child(project.configuration_window(),400))=="sample"&&edit_text(child(project.configuration_window(),402))=="WITH_TRACE=ON","CMake configuration controls changed authored values");
            const auto formatting_changes=source_changes;project.set_text_scale(150);project.set_text_scale(100);
            require(source_changes==formatting_changes,"Formatting CMake target/options emitted a source/configuration change without editing their text");
            project.snapshot(snapshot);require(snapshot.cmake==block.cmake,"Formatting CMake controls changed the authored target/options");
            project.set_availability(false,false,false);require(IsWindowEnabled(child(project.configuration_window(),405))&&!IsWindowEnabled(child(project.configuration_window(),406)),"Build is enabled before actual configuration");
            click(project.configuration_window(),405);require(actions.back()==windows::CppRunRequest::Action::configure,"Configure button triggered another phase");project.set_availability(false,true,false);click(project.configuration_window(),406);require(actions.back()==windows::CppRunRequest::Action::build,"Build button executed rather than building");
            project.set_availability(true,true,true);require(!IsWindowEnabled(child(project.configuration_window(),405))&&!IsWindowEnabled(child(project.configuration_window(),406)),"Busy configuration/build buttons remain enabled");
            const auto changed_before=source_changes;set_edit_text(child(project.configuration_window(),400),"revised");set_edit_text(child(project.configuration_window(),402),"WITH_TRACE=OFF\nMODE=two words");project.snapshot(snapshot);require(snapshot.cmake->target=="revised"&&snapshot.cmake->options.at("MODE")=="two words"&&source_changes>changed_before&&cmake_json.find("revised")!=std::string::npos,"Configuration edits did not immediately persist and invalidate the exact request");
            project.reset();project.snapshot(snapshot);require(snapshot.cmake==block.cmake&&snapshot.project_files==block.project_files,"Reset did not restore the authored project and CMake settings only");
        }
        if(other_cases)verify_unrelated_cpp_multicase_feedback(host);
        else if(u3_gates)verify_unmodified_thirteen_page_entry_gates(host,std::filesystem::path(argv[3]));
        else if(u3_declaration)verify_declared_u3_compiler_workflow(std::filesystem::path(argv[3]));
        else if(u3_prerequisites)verify_genuine_u3_first_prerequisites(host,std::filesystem::path(argv[3]));
        else if(u3_first_cmake)verify_genuine_u3_first_cmake_project(host,std::filesystem::path(argv[3]));
        else if(u3_selective)integrated_selective_history_lesson(host,std::filesystem::path(argv[3]));
        else if(u3_selective_declared)integrated_selective_history_lesson(host,std::filesystem::path(argv[3]),true);
        else if(!internal_only)integrated_lesson(host);
        cleanup_own_qa(std::filesystem::absolute(argv[1]),history_root,"cpp-project-view-");
        DestroyWindow(host);host=nullptr;FreeLibrary(rich);rich=nullptr;
        if(internal_only)std::cout<<"PASS internal project create/history/record/diff/restore pages, one enabled root/client bounds, preserved source/cursor/tab/Undo and CMake controls; no compilation\n";
        else if(u3_selective_declared)std::cout<<"PASS declared Win32 U3 History: original editor, record, failure, compare, recover, success and reopen\n";
        else if(u3_selective)std::cout<<"PASS actual Win32 U3 owner focused lesson: no stdout shortcut; record, build, failed checked run, two UI diffs, selective file restore, rebuild, completed checked run, persistent reopen and reset\n";
        else if(u3_first_cmake)std::cout<<"PASS original U3 first CMake project: five true questions, distributed CMake, two target runs, page-five access and reopening\n";
        else if(u3_prerequisites)std::cout<<"PASS U3 original pages one through four: five genuinely checked quiz answers, no fake precompletion, blocked CMake laboratory after reopen\n";
        else if(u3_declaration)std::cout<<"PASS declared U3 History: full/summary reader, immutable exercise identity and negative contracts\n";
        else if(u3_gates)std::cout<<"PASS original thirteen-page U3 lesson: new isolated profile cannot jump to U3 history or Finish; page gate survives reopen; no other lessons or student data modified\n";
        else if(other_cases)std::cout<<"PASS unrelated C++ with two inputs: no U3 History advice, 1/2 then 2/2 exact executions and preserved completion after reopen\n";
        else std::cout<<"PASS real project file/header selection, independent source/cursor/tab origin and Undo, create/delete, Android history record/diff/individual restore, CMake controls and native multi-file/configure/build/execute integration\n";return 0;
    }catch(const std::exception& error){if(host)DestroyWindow(host);if(rich)FreeLibrary(rich);std::cerr<<"Project controls failure: "<<error.what()<<'\n';return 1;}
}

#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "core/learning_flow.hpp"
#include "core/view_preferences.hpp"
#include "core/attempts.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/narration_licenses.hpp"
#include "presentation/windows/rich_text.hpp"
#include "native_build_config.hpp"
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <oleacc.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <exception>
#include <bit>
#include <chrono>
#include <tuple>
#include <atomic>
#include <thread>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void run_case(const char* name,void(*run)()){const auto start=std::chrono::steady_clock::now();run();std::cout<<"CASE "<<name<<" elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<'\n';}
std::wstring native_text(HWND window);
void select(HWND view,int id,int value){SendDlgItemMessageW(view,id,CB_SETCURSEL,value,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(view,id)));}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}}
HWND internal_dialog_with_control(int id){
    struct Search{int id;HWND found=nullptr;} search{id};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND root,LPARAM raw)->BOOL{
        auto& query=*reinterpret_cast<Search*>(raw);
        EnumChildWindows(root,[](HWND child,LPARAM data)->BOOL{auto& query=*reinterpret_cast<Search*>(data);wchar_t name[32]{};GetClassNameW(child,name,32);
            if(std::wstring_view(name)==L"#32770"&&GetDlgItem(child,query.id)){query.found=child;return FALSE;}return TRUE;},raw);
        return !query.found;
    },reinterpret_cast<LPARAM>(&search));return search.found;
}
void license_settings_navigation_tests(HWND owner);
void license_window_tests(HWND owner){
    ShowWindow(GetAncestor(owner,GA_ROOT),SW_SHOW);SetForegroundWindow(GetAncestor(owner,GA_ROOT));pump();
    const std::array<const char*,3> locales{"es","en","fr"};
    const std::array<const wchar_t*,3> first{L"Kokoro y voces",L"Kokoro and voices",L"Kokoro et voix"};
    const std::array<const wchar_t*,3> last{L"Misaki · Apache 2.0",L"Misaki · Apache 2.0",L"Misaki · Apache 2.0"};
    const std::array paths{L"app/src/main/res/raw/kokoro_model_notice.txt",L"app/src/main/res/raw/french_fluidaudio_license.txt",L"app/src/main/res/raw/french_siwis_attribution.txt",L"app/src/main/res/raw/french_fluid_resources_notices.txt",L"app/src/main/res/raw/french_rule_g2p_notice.txt",L"app/src/main/res/raw/misaki_bart_attribution.txt",L"app/src/main/res/raw/spacy_pos_attribution.txt",L"app/src/main/res/raw/spacy_pos_spacy_license.txt",L"app/src/main/res/raw/spacy_pos_thinc_license.txt",L"app/src/main/res/raw/spacy_pos_model_license.txt",L"app/src/main/res/raw/onnxruntime_license.txt",L"app/src/main/res/raw/onnxruntime_third_party_notices.txt",L"app/src/main/res/raw/onnxruntime_source_information.txt",L"app/src/main/assets/licenses/FluidAudio-SpanishG2P.txt",L"app/src/main/assets/narration/misaki/NOTICE.txt",L"app/src/main/assets/narration/misaki/LICENSE.txt"};
    for(std::size_t locale=0;locale<locales.size();++locale)for(int index=0;index<16;++index){
        const auto window=lgaed::presentation::open_narration_licenses(owner,locales[locale]);
        const auto list=GetDlgItem(window,2),field=GetDlgItem(window,1);
        check(IsWindow(window)&&SendMessageW(list,LB_GETCOUNT,0,0)==16,"Narration licenses omitted the original sixteen-item Android menu");
        wchar_t label[256]{};SendMessageW(list,LB_GETTEXT,0,reinterpret_cast<LPARAM>(label));check(std::wstring(label)==first[locale],"License labels do not use the selected interface language");
        SendMessageW(list,LB_GETTEXT,15,reinterpret_cast<LPARAM>(label));check(std::wstring(label)==last[locale],"The last original notice is missing from the menu");
        check(IsWindowVisible(list)&&!IsWindowVisible(field),"Opening licenses bypassed the list and immediately opened a notice");
        SendMessageW(list,LB_SETTOPINDEX,index,0);RECT row{};SendMessageW(list,LB_GETITEMRECT,index,reinterpret_cast<LPARAM>(&row));
        RECT client{};GetClientRect(list,&client);const auto click=MAKELPARAM(std::max(1L,client.right/2),std::max(1L,(std::max(0L,row.top)+std::min(client.bottom,row.bottom))/2));
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,click);check(IsWindow(window),"Selecting a license card was confused with IDCANCEL");SendMessageW(list,WM_LBUTTONUP,0,click);pump();
            check(IsWindowVisible(field)&&!IsWindowVisible(list)&&GetWindowTextLengthW(field)>0,"An original narration notice was not loaded into its real text control");
            check(native_text(GetDlgItem(window,3))!=native_text(window),"The selected notice lost its own title");
        const auto source=lgaed::state::read_bytes(std::filesystem::path(lgaed::build::source_root)/paths[static_cast<std::size_t>(index)],1024*1024);std::string expected(source.begin(),source.end());
        std::erase(expected,'\r');check(lgaed::presentation::edit_text(field)==expected,"An original notice was truncated or its text changed");
        if(index==4){SetWindowPos(window,nullptr,0,0,360,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);const auto heading=GetDlgItem(window,3);RECT bounds{};GetClientRect(heading,&bounds);const auto dc=GetDC(heading);const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)));auto measured=bounds;measured.bottom=0;DrawTextW(dc,native_text(heading).c_str(),-1,&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(heading,dc);check(bounds.bottom>=measured.bottom,"A long translated license heading was clipped at narrow widths");}
        const auto close=GetDlgItem(window,4);const auto dc=GetDC(close);const auto buffer=CreateCompatibleDC(dc);const auto bitmap=CreateCompatibleBitmap(dc,140,48);const auto old=SelectObject(buffer,bitmap);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=4;draw.hwndItem=close;draw.hDC=buffer;draw.rcItem={0,0,140,48};draw.itemAction=ODA_DRAWENTIRE;
        const auto handled=SendMessageW(window,WM_DRAWITEM,4,reinterpret_cast<LPARAM>(&draw));SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(close,dc);check(handled!=FALSE,"License Close owner-drawn button has no painting handler");
        SendMessageW(window,WM_DPICHANGED_AFTERPARENT,0,0);LOGFONTW font{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(list,WM_GETFONT,0,0)),sizeof(font),&font);
        check(font.lfHeight==-MulDiv(18,GetDpiForWindow(window),96),"License menu ignored the current DPI");
        SetFocus(field);
        MSG escape{};escape.hwnd=field;escape.message=WM_KEYDOWN;escape.wParam=VK_ESCAPE;escape.lParam=1;
        if(!IsDialogMessageW(window,&escape)){TranslateMessage(&escape);DispatchMessageW(&escape);}pump();
        check(!IsWindow(window),"Escape from a notice failed to return to settings as in Android");
    }
    license_settings_navigation_tests(owner);
}
void reset_text_tests(){
    using namespace lgaed;using namespace presentation;
    const auto android_string=[](std::string_view locale,std::string_view key){
        const auto directory=std::filesystem::path(build::source_root)/L"app/src/main/res"/(locale=="es"?"values":"values-"+std::string(locale));
        for(const auto file:{L"strings.xml",L"options_ui_strings.xml"}){const auto bytes=state::read_bytes(directory/file,4*1024*1024);const std::string xml(bytes.begin(),bytes.end());
            const auto marker="name=\""+std::string(key)+"\">";const auto start=xml.find(marker);if(start==std::string::npos)continue;const auto content=start+marker.size(),end=xml.find("</string>",content);check(end!=std::string::npos,"Malformed original Android reset string");
            auto result=xml.substr(content,end-content);for(std::size_t at=0;(at=result.find("\\n",at))!=std::string::npos;++at)result.replace(at,2,"\n");return result;}
        throw std::runtime_error("Original Android reset string is absent");
    };
    for(const auto locale:{"es","en","fr"}){
        for(const auto& [scope,key]:std::array{std::pair{SettingsScope::course,"course_settings_reset"},std::pair{SettingsScope::unit,"unit_settings_reset"},std::pair{SettingsScope::lesson,"lesson_settings_reset"}}){
            const auto text=settings_reset_text(scope,locale);check(!text.title.empty()&&text.confirmation==android_string(locale,std::string(key)+"_confirmation")&&text.success==android_string(locale,std::string(key)+"_success"),"A scoped reset confirmation/success differs from the original Android wording");}
        for(const auto route:{"programming","math_physics"}){const auto text=settings_reset_text(SettingsScope::global,locale,route);const auto key=std::string("settings_")+route+"_progress";
            check(!text.title.empty()&&text.confirmation==android_string(locale,key+"_confirmation")&&text.success==android_string(locale,key+"_reset_success"),"A route reset omitted Android's scope, preserved-settings explanation or success notice");}
    }
    bool rejected=false;try{(void)settings_reset_text(SettingsScope::global,"es","unknown");}catch(const std::invalid_argument&){rejected=true;}check(rejected,"An unknown route received a misleading reset confirmation");
}
bool boolean(const lgaed::state::StateData& data,const char* domain,const char* key){const auto value=lgaed::state::find(data,domain,key);check(value&&std::holds_alternative<bool>(*value),"Expected a persisted boolean preference");return std::get<bool>(*value);}
struct ScopedFixture{
    lgaed::state::StateData data;HWND host=nullptr,view=nullptr;int updates=0,changes=0,closed=0,resets=0,scope_id=0;std::vector<std::string> reset_routes;
    ScopedFixture(lgaed::presentation::SettingsScope scope,lgaed::state::StateData initial={},std::string course="course_a",bool programming=true,bool native_mouse_host=false):data(std::move(initial)){
        scope_id=static_cast<int>(scope);
        constexpr wchar_t mouse_host_class[]=L"LearnGameAndEngineDev.SettingsMouseTests";
        if(native_mouse_host){WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);
            type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.hbrBackground=lgaed::presentation::reader_theme::brush(lgaed::presentation::reader_theme::Surface::screen);type.lpszClassName=mouse_host_class;
            check(RegisterClassExW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Cannot register a genuine native mouse-test host");}
        // WindowFromPoint deliberately skips static text controls. Real input
        // fixtures need an ordinary root window, as the application uses.
        host=CreateWindowExW(0,native_mouse_host?mouse_host_class:L"STATIC",L"Scoped settings QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create scoped settings QA host");
        lgaed::presentation::SettingsAccess access;access.scope=scope;access.course_id=std::move(course);access.title="Título del contenido";access.eyebrow="Contexto";access.programming=programming;
        access.programming_course_ids={"course_a","course_b","course_c"};access.math_course_ids={"math_a","math_b"};access.reset_route=[this](std::string_view route){reset_routes.emplace_back(route);};
        access.read=[this]{return data;};access.update=[this](const auto& update){++updates;update(data);};access.changed=[this]{++changes;};access.close=[this]{++closed;};access.reset_scope=[this]{++resets;};
        view=lgaed::presentation::create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);
    }
    ~ScopedFixture(){if(view)DestroyWindow(view);if(host)DestroyWindow(host);}
    HWND control(int id)const{return GetDlgItem(view,id);}
    void has(int id,bool expected)const{if((control(id)!=nullptr)!=expected)throw std::runtime_error("Settings control "+std::to_string(id)+" scope "+std::to_string(scope_id)+(expected?" is missing":" should not exist"));}
    bool checked(int id)const{check(control(id)!=nullptr,"Missing scoped checkbox");return SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED;}
    void toggle(int id,bool value){check(control(id)&&IsWindowEnabled(control(id)),"Attempted to change a disabled scoped checkbox");SendMessageW(control(id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
    void click(int id){check(control(id)&&IsWindowEnabled(control(id)),"Missing/disabled scoped action");SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));pump();}
    void scale(int value){check(control(5120)&&IsWindowEnabled(control(5120)),"Text-size slider is missing or disabled");SendMessageW(control(5120),TBM_SETPOS,TRUE,value);SendMessageW(view,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,value),reinterpret_cast<LPARAM>(control(5120)));}
};
struct WrapResetAction{std::wstring title;bool accept=true,found=false;std::exception_ptr error;};
WrapResetAction* wrap_reset_action=nullptr;
void CALLBACK wrap_reset_timer(HWND,UINT,UINT_PTR timer,DWORD){
    const auto dialog=internal_dialog_with_control(IDYES);
    if(!dialog||!wrap_reset_action)return;KillTimer(nullptr,timer);wrap_reset_action->found=true;
    if(native_text(dialog)!=wrap_reset_action->title||!GetDlgItem(dialog,IDNO)){wrap_reset_action->error=std::make_exception_ptr(std::runtime_error("The internal wrap-reset confirmation lost its title or conservative No action"));SendMessageW(dialog,WM_COMMAND,IDNO,0);return;}
    SendMessageW(dialog,WM_COMMAND,wrap_reset_action->accept?IDYES:IDNO,0);
}
void click_wrap_reset(ScopedFixture& fixture,int id,bool accept=true){
    ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
    WrapResetAction action;action.title=native_text(fixture.control(id));action.accept=accept;wrap_reset_action=&action;
    const auto timer=SetTimer(nullptr,0,20,wrap_reset_timer);check(timer!=0,"Cannot schedule the real wrap-reset confirmation");
    fixture.click(id);KillTimer(nullptr,timer);wrap_reset_action=nullptr;
    if(action.error)std::rethrow_exception(action.error);check(action.found,"Resetting all course wrap exceptions omitted the confirmation dialog");
}
struct WrapAvailabilityAction{std::wstring title,body;bool found=false;std::exception_ptr error;};
WrapAvailabilityAction* wrap_availability_action=nullptr;
void CALLBACK wrap_availability_timer(HWND,UINT,UINT_PTR timer,DWORD){
    if(!wrap_availability_action)return;
    const auto dialog=internal_dialog_with_control(IDOK);
    if(!dialog)return;
    KillTimer(nullptr,timer);auto& action=*wrap_availability_action;action.found=true;
    struct Contents{const WrapAvailabilityAction* action;std::vector<HWND> buttons;bool body_found=false;} contents{&action};
    EnumChildWindows(dialog,[](HWND item,LPARAM raw)->BOOL{auto& found=*reinterpret_cast<Contents*>(raw);wchar_t name[80]{};GetClassNameW(item,name,80);
        if(_wcsicmp(name,L"BUTTON")==0)found.buttons.push_back(item);if(native_text(item)==found.action->body)found.body_found=true;return TRUE;},reinterpret_cast<LPARAM>(&contents));
    try{check(native_text(dialog)==action.title&&contents.buttons.size()==1&&!native_text(contents.buttons.front()).empty(),"Mathematics availability became a destructive confirmation or lost its sole dismiss action");
        check(GetDlgCtrlID(contents.buttons.front())!=IDYES&&GetDlgCtrlID(contents.buttons.front())!=IDNO,"Mathematics availability exposed a Yes/No decision instead of information");
        check(contents.body_found,"Mathematics availability notice differs from the complete original Android explanation");
    }catch(...){action.error=std::current_exception();}
    if(contents.buttons.size()==1)SendMessageW(contents.buttons.front(),BM_CLICK,0,0);else SendMessageW(dialog,WM_CLOSE,0,0);
}
void scoped_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["student_answer"]=std::string("immutable answer");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    const auto progress=seed.preferences["learning_progress"],drafts=seed.preferences["cpp_lesson_drafts"];
    {ScopedFixture global(SettingsScope::global,seed);check(global.updates==0&&global.data==seed,"Opening global defaults wrote preferences");
        for(const auto id:{5101,5103,5104,5105,5106,5108,5110,5111,5112,5113,5114})global.has(id,true);
        for(const auto id:{5102,5107,5109,5116,5117,5118})global.has(id,false);
        global.toggle(5110,false);check(!curriculum::learning_flow(global.data).between_lessons&&global.updates==1,"Global continuation did not apply live");}
    curriculum::save_course_flow(seed,"course_b",{false,true,false,true});
    {ScopedFixture course(SettingsScope::course,seed);course.has(5117,false);check(course.checked(5116)&&!IsWindowEnabled(course.control(5110)),"Inherited course controls are editable");
        for(int control=5111;control<=5113;++control){RECT previous{},current{};GetWindowRect(course.control(control-1),&previous);GetWindowRect(course.control(control),&current);
            check(previous.bottom==current.top,"Consecutive course switches gained non-Android vertical gaps or overlap");}
        course.toggle(5116,false);course.toggle(5110,false);course.toggle(5111,true);course.toggle(5112,true);course.toggle(5113,true);
        check(curriculum::learning_flow(course.data,"course_a")==curriculum::LearningFlow{false,true,true,true},"Course flow did not apply live");
        check(curriculum::learning_flow(course.data)==curriculum::LearningFlow{}&&curriculum::learning_flow(course.data,"course_b")==curriculum::LearningFlow{false,true,false,true},"Course flow changed another scope");
        course.toggle(5116,true);check(!curriculum::has_course_flow(course.data,"course_a")&&!IsWindowEnabled(course.control(5110)),"Course inheritance did not apply live");
        check(course.checked(5110)&&!course.checked(5111)&&!course.checked(5112)&&!course.checked(5113),"Inherited course controls still display the obsolete course override instead of the effective global values");
        check(course.data.preferences["learning_progress"]==progress&&course.data.preferences["cpp_lesson_drafts"]==drafts,"Course options changed student data");}
    {ScopedFixture unit(SettingsScope::unit,seed);unit.has(5115,true);unit.has(5101,false);unit.has(5123,false);unit.click(5115);check(unit.resets==1&&unit.data==seed,"Unit reset did not delegate only its scope");}
    {ScopedFixture audio(SettingsScope::audio,seed);for(const auto id:{5103,5104,5105,5106,5108})audio.has(id,true);
        for(const auto id:{5101,5102,5107,5109,5110,5111,5112,5113,5114,5115,5116,5117,5118})audio.has(id,false);
        audio.toggle(5108,false);check(!boolean(audio.data,"lesson_narration_settings","kokoro_advance_generation"),"Audio preparation did not persist live");}
    {ScopedFixture lesson(SettingsScope::lesson,seed);for(const auto id:{5105,5106,5115,5118})lesson.has(id,true);lesson.has(5123,false);
        lesson.click(5115);check(lesson.resets==1&&lesson.data==seed,"Lesson reset modified unrelated data");}
    std::cout<<"PASS native scopes: no invented content/rate/visibility selectors; live global/course options, inheritance and protected data\n";
}
void appearance_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["answer"]=std::string("exact answer");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_global_text_scale(seed,115);appearance::save_course_text_scale(seed,"course_a",false,90);
    appearance::save_global_wrap(seed,false,{});appearance::save_course_wrap(seed,"course_a",false);appearance::save_wrap_overwrite(seed,"course_a",false);
    appearance::save_course_wrap(seed,"outside_programming",false);appearance::save_wrap_overwrite(seed,"outside_programming",false);
    auto& accents=seed.preferences["appearance_settings"];
    accents["accent_red"]=std::int64_t{10};accents["accent_green"]=std::int64_t{20};accents["accent_blue"]=std::int64_t{30};
    for(const auto& route:{std::string("programming"),std::string("math_physics")}){accents["route_accent_red_"+route]=std::int64_t{1};accents["route_accent_green_"+route]=std::int64_t{2};accents["route_accent_blue_"+route]=std::int64_t{3};accents["route_allow_global_accent_overwrite_"+route]=true;}
    accents["default_global_accent_red"]=std::int64_t{41};accents["default_global_accent_green"]=std::int64_t{42};accents["default_global_accent_blue"]=std::int64_t{43};
    accents["default_route_accent_red_math_physics"]=std::int64_t{61};accents["default_route_accent_green_math_physics"]=std::int64_t{62};accents["default_route_accent_blue_math_physics"]=std::int64_t{63};
    const auto progress=seed.preferences["learning_progress"],drafts=seed.preferences["cpp_lesson_drafts"];
    {ScopedFixture global(SettingsScope::global,seed);check(!IsWindowEnabled(global.control(5123))&&!(GetWindowLongPtrW(global.control(5123),GWL_STYLE)&WS_VISIBLE),"Save is visible without a deferred edit");
        check(SendMessageW(global.control(5120),TBM_GETRANGEMIN,0,0)==75&&SendMessageW(global.control(5120),TBM_GETRANGEMAX,0,0)==150,"Text scale changed Android range");
        global.scale(125);global.toggle(5110,false);global.toggle(5114,true);
        check(appearance::global_text_scale(global.data)==125&&!curriculum::learning_flow(global.data).between_lessons&&appearance::global_code_wrap(global.data),"Scale, flow or wrapping did not apply immediately");
        check(appearance::text_scale(global.data,"course_a")==90&&!appearance::code_wrap(global.data,"course_a")&&appearance::code_wrap(global.data,"course_b"),"Live appearance ignored course protections");
        const auto live=global.data;global.toggle(5128,false);global.click(5130);global.click(5132);
        check(global.data==live&&IsWindowEnabled(global.control(5123))&&(GetWindowLongPtrW(global.control(5123),GWL_STYLE)&WS_VISIBLE),"Colour propagation escaped its explicit transaction or omitted Save");
        global.click(5123);check(reader_theme::appearance_accent(global.data)==RGB(41,42,43)&&reader_theme::appearance_accent(global.data,"programming")==RGB(1,2,3)&&reader_theme::appearance_accent(global.data,"math_physics")==RGB(61,62,63),"Colour transaction lost new protection or explicit route priority");
        check(!IsWindowEnabled(global.control(5123))&&!(GetWindowLongPtrW(global.control(5123),GWL_STYLE)&WS_VISIBLE),"Save remained visible after colour commit");
        check(global.data.preferences["learning_progress"]==progress&&global.data.preferences["cpp_lesson_drafts"]==drafts,"Appearance changed student data");seed=global.data;
        click_wrap_reset(global,5133);check(appearance::allows_global_wrap(global.data,"course_a")&&!appearance::allows_global_wrap(global.data,"outside_programming"),"Reset wrapping changed another scope");}
    {ScopedFixture lesson(SettingsScope::lesson,seed);lesson.has(5123,false);lesson.toggle(5122,true);check(!IsWindowEnabled(lesson.control(5120)),"Inherited course size stayed editable");
        check(native_text(lesson.control(5143))==L"Tamaño del curso · 125% · Global","Inherited text size lost Android's complete label or changed its percentage format");
        check(SendMessageW(lesson.control(5120),TBM_GETPOS,0,0)==125&&appearance::course_text_scale(lesson.data,"course_a")==90,"The inherited size slider disagrees with its preview or erased the saved custom size");
        lesson.toggle(5122,false);check(SendMessageW(lesson.control(5120),TBM_GETPOS,0,0)==90,"Disabling inheritance did not restore the custom size slider");lesson.scale(135);lesson.toggle(5124,true);
        check(native_text(lesson.control(5143))==L"Tamaño del curso · 135%","Independent course size retained a misleading Global label");
        check(appearance::text_scale(lesson.data,"course_a")==135&&appearance::global_text_scale(lesson.data)==125&&appearance::allows_global_wrap(lesson.data,"course_a"),"Course appearance did not apply live in its own scope");
        lesson.toggle(5122,true);check(appearance::text_scale(lesson.data,"course_a")==125&&appearance::course_text_scale(lesson.data,"course_a")==135&&SendMessageW(lesson.control(5120),TBM_GETPOS,0,0)==125,"Restoring inheritance erased separate course size or kept its old slider position");}
    appearance::save_course_text_scale(seed,"course_a",true,90);{ScopedFixture inherited(SettingsScope::lesson,seed);check(SendMessageW(inherited.control(5120),TBM_GETPOS,0,0)==125&&inherited.updates==0&&inherited.data==seed,"Opening an inherited course used its inactive custom scale or modified stored preferences");}
    std::cout<<"PASS live typography/flow/wrap and scoped inheritance; only RGB propagation has explicit Save, protected route ordering and exact data preservation\n";
}
void math_wrap_reset_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["answer"]=std::string("untouched");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_global_wrap(seed,false,{});
    for(const auto course:{"course_a","course_b","math_a","math_b","unrelated_course"}){
        appearance::save_course_wrap(seed,course,true);appearance::save_wrap_overwrite(seed,course,false);}
    for(const auto scope:{SettingsScope::course,SettingsScope::unit,SettingsScope::audio,SettingsScope::lesson}){ScopedFixture other(scope,seed);other.has(5137,false);}
    const std::array<const char*,3> locales{"es","en","fr"};
    const std::array<const wchar_t*,3> labels{L"Consultar disponibilidad de excepciones",L"Check exception availability",L"Vérifier la disponibilité des exceptions"};
    const std::array<const wchar_t*,3> titles{L"Matemáticas y Física",L"Mathematics and Physics",L"Mathématiques et physique"};
    const std::array<const wchar_t*,3> bodies{
        L"Todavía no hay cursos disponibles en esta ruta.\n\nCuando se agreguen cursos, este botón permitirá restablecer sus excepciones de ajuste de línea.",
        L"There are no courses available in this learning path yet.\n\nWhen courses are added, this button will allow you to reset their line wrapping exceptions.",
        L"Aucun cours n’est encore disponible dans ce parcours.\n\nLorsque des cours seront ajoutés, ce bouton permettra de réinitialiser leurs exceptions de retour à la ligne."};
    for(std::size_t index=0;index<locales.size();++index){
        auto localized=seed;localized.preferences["native_windows"]["ui_language"]=std::string(locales[index]);ScopedFixture fixture(SettingsScope::global,localized);fixture.has(5133,true);fixture.has(5137,true);
        check(native_text(fixture.control(5137))==labels[index]&&fixture.data==localized&&fixture.updates==0,"Opening Mathematics availability altered data or lost the original action caption");
        WrapAvailabilityAction action{titles[index],bodies[index]};wrap_availability_action=&action;
        ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
        const auto timer=SetTimer(nullptr,0,20,wrap_availability_timer);check(timer!=0,"Cannot inspect the original Mathematics availability notice");
        fixture.click(5137);KillTimer(nullptr,timer);wrap_availability_action=nullptr;if(action.error)std::rethrow_exception(action.error);
        check(action.found&&fixture.updates==0&&fixture.changes==0&&fixture.data==localized,"Consulting Mathematics availability changed exceptions, another course, answers or intentional stdin whitespace");
    }
    std::cout<<"PASS original Mathematics exception availability: complete ES/EN/FR notice, no destructive reset and exact preservation of every route and student datum\n";
}
struct ShortcutAction{bool restore=false,save=false,edit=false,found=false,revealed=false;std::array<std::wstring,20> opened{};std::exception_ptr error;};
ShortcutAction* shortcut_action=nullptr;
std::wstring native_text(HWND window){std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(window,text.data(),static_cast<int>(text.size()))));return text;}
void CALLBACK shortcut_timer(HWND,UINT,UINT_PTR timer,DWORD){
    const auto dialog=internal_dialog_with_control(5350);
    if(!dialog||!shortcut_action)return;KillTimer(nullptr,timer);auto& action=*shortcut_action;action.found=true;
    try{
        check((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_POPUP),"Shortcut settings opened a separate application window");
        const auto viewport=GetDlgItem(dialog,5350);check(viewport!=nullptr,"Shortcut dialog omitted its scrolling viewport");
        for(int index=0;index<20;++index){const auto input=GetDlgItem(viewport,5400+index);check(input!=nullptr,"Shortcut dialog did not create all 20 configured slots");action.opened[static_cast<std::size_t>(index)]=native_text(input);}
        check(GetDlgItem(viewport,5420)==nullptr,"Shortcut configuration exposed TAB as an additional editable slot");
        SetFocus(GetDlgItem(viewport,5419));RECT selected{},area{};GetWindowRect(GetDlgItem(viewport,5419),&selected);MapWindowPoints(nullptr,viewport,reinterpret_cast<POINT*>(&selected),2);GetClientRect(viewport,&area);
        action.revealed=GetFocus()==GetDlgItem(viewport,5419)&&selected.top>=0&&selected.bottom<=area.bottom;
        if(action.restore)SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(5351,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,5351)));
        if(action.edit){SetWindowTextW(GetDlgItem(viewport,5400),L"(");SetWindowTextW(GetDlgItem(viewport,5401),L"  ");SetWindowTextW(GetDlgItem(viewport,5402),L"");SetWindowTextW(GetDlgItem(viewport,5403),L"::");}
        SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(action.save?IDOK:IDCANCEL,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(dialog,action.save?IDOK:IDCANCEL)));
    }catch(...){action.error=std::current_exception();SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void open_shortcuts(ScopedFixture& fixture,ShortcutAction& action){
    ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
    shortcut_action=&action;const auto timer=SetTimer(nullptr,0,20,shortcut_timer);check(timer!=0,"Cannot schedule real shortcut dialog QA");
    fixture.has(5136,false); // Windows no longer presents the Programming floating keyboard.
    SendMessageW(fixture.view,WM_COMMAND,MAKEWPARAM(5136,BN_CLICKED),0);pump();KillTimer(nullptr,timer);shortcut_action=nullptr;if(action.error)std::rethrow_exception(action.error);
    check(action.found,"Settings did not open the actual native shortcut dialog");check(action.revealed,"Keyboard focus did not reveal the last shortcut input in the scrolled viewport");
}
void shortcut_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["answer"]=std::string("keep");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_course_text_scale(seed,"course_a",false,90);
    winrt::Windows::Data::Json::JsonArray initial;for(int index=0;index<44;++index)initial.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::to_hstring("old_"+std::to_string(index))));
    seed.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=winrt::to_string(initial.Stringify());
    const auto progress=seed.preferences["learning_progress"],drafts=seed.preferences["cpp_lesson_drafts"];
    {
        ScopedFixture lesson(SettingsScope::lesson,seed);ShortcutAction cancel;cancel.restore=true;cancel.edit=true;open_shortcuts(lesson,cancel);
        check(lesson.data==seed&&lesson.updates==0,"Cancelling shortcut edits modified or truncated the existing 44-key list");
        check(cancel.opened[0]==L"old_0"&&cancel.opened[19]==L"old_19","Shortcut dialog did not show original entries in order");
        lesson.scale(130);ShortcutAction save;save.edit=true;save.save=true;open_shortcuts(lesson,save);
        const auto values=parse_state_shortcuts(lesson.data);check(values.size()==19&&values[0]=="("&&values[1]=="  "&&values[2]=="::"&&values.back()=="old_19",
            "Shortcut Save altered literal spaces, introduced auto-pairs, retained empty slots or changed order");
        check(lesson.updates==2&&appearance::text_scale(lesson.data,"course_a")==130&&!lesson.control(5123),"Shortcut Save changed live text scale or invented another Save button");
        ShortcutAction defaults;defaults.restore=true;defaults.save=true;open_shortcuts(lesson,defaults);
        const auto restored=parse_state_shortcuts(lesson.data),original=parse_state_shortcuts(state::StateData{});
        check(original.size()==44&&restored.size()==20&&std::equal(restored.begin(),restored.end(),original.begin()),"Explicit shortcut Defaults/Save did not use the original first 20 defaults");
        check(lesson.data.preferences["learning_progress"]==progress&&lesson.data.preferences["cpp_lesson_drafts"]==drafts,"Shortcut Save modified student data or stdin whitespace");
    }
    for(const auto scope:{SettingsScope::global,SettingsScope::course,SettingsScope::unit,SettingsScope::audio}){ScopedFixture other(scope,seed);other.has(5136,false);}
    ScopedFixture mathematics(SettingsScope::lesson,seed,"math_course",false);mathematics.has(5136,false);
    std::cout<<"PASS native shortcut editor: 20 real inputs, keyboard scroll visibility, exact strings, preserved 44-key cancellation, explicit default restore and isolated Save\n";
}
struct ComboMetrics{int selection=0,list=0,height=0,client=0,item=0,dpi=96,font_height=0;};
LOGFONTW control_font(HWND control){LOGFONTW value{};const auto font=reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0));check(font&&GetObjectW(font,sizeof(value),&value),"Settings control has no measurable native font");return value;}
HWND child_with_caption(HWND parent,std::wstring_view caption){for(auto child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))if(native_text(child)==caption)return child;return nullptr;}
RECT relative_rect(HWND child,HWND parent){RECT value{};GetWindowRect(child,&value);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&value),2);return value;}
int reveal_control(HWND view,HWND target){
    // Position setup through the same native wheel path, without animating a
    // journey through every preceding control in an unrelated visual test.
    for(int count=0;count<8;++count){const auto control=relative_rect(target,view);RECT viewport{};GetClientRect(view,&viewport);
        if(control.top>=0&&control.bottom<=viewport.bottom)return count;
        const auto target_top=std::max<LONG>(0,(viewport.bottom-(control.bottom-control.top))/2);
        int wheel_delta=std::clamp(-MulDiv(control.top-target_top,WHEEL_DELTA,MulDiv(48,GetDpiForWindow(view),96)),-32760,32760);
        if(wheel_delta==0)wheel_delta=control.top>target_top?-1:1;
        SendMessageW(view,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(static_cast<SHORT>(wheel_delta))),0);}
    check(false,"The native wheel setup did not reveal the entire real Settings control");return 0;
}
void license_settings_navigation_tests(HWND owner){
    using namespace lgaed;using namespace presentation;
    state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["learning_progress"]["answer"]=std::string("unchanged");data.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");const auto saved=data;
    HWND settings=nullptr,license=nullptr;int writes=0,closed=0;
    SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){++writes;change(data);};access.close=[&]{++closed;};
    access.open_licenses=[&]{license=open_narration_licenses(settings,"es");};
    settings=create_settings_view(owner,std::move(access));RECT root{};GetClientRect(owner,&root);MoveWindow(settings,0,0,root.right,root.bottom,TRUE);
    const auto rgb=GetDlgItem(settings,5720),button=GetDlgItem(settings,5141);check(rgb&&button,"Create real deferred-colour and license controls for navigation QA");
    SetWindowTextW(rgb,L"123");check(IsWindowEnabled(GetDlgItem(settings,5123))&&data==saved&&writes==0,"Create an unsaved colour draft before opening licenses");
    reveal_control(settings,button);SetFocus(button);SCROLLINFO before{sizeof(before),SIF_POS};GetScrollInfo(settings,SB_VERT,&before);const auto button_rect=relative_rect(button,settings);
    check(GetFocus()==button&&before.nPos>0,"License navigation fixture must begin on the focused, scrolled Settings action");
    for(const bool escape:{false,true}){
        SendMessageW(button,BM_CLICK,0,0);pump();
        check(license&&IsWindow(license)&&GetParent(license)==owner&&GetAncestor(license,GA_ROOT)==owner&&(GetWindowLongPtrW(license,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(license,GWL_STYLE)&WS_POPUP),"Production Settings license action opened outside the same main window");
        check(IsWindowEnabled(owner)&&!IsWindowVisible(settings)&&active_internal_window(owner)==license&&GetDlgItem(settings,5720)==rgb,
            "Opening licenses disabled the root, exposed Settings or recreated its draft controls");
        const auto list=GetDlgItem(license,2);SendMessageW(list,LB_SETCURSEL,0,0);SendMessageW(license,WM_APP+1,0,0);pump();
        const auto body=GetDlgItem(license,1);check(IsWindowVisible(body)&&GetWindowTextLengthW(body)>0,"Open an actual original notice inside the internal license screen");
        if(escape){MSG key{};key.hwnd=body;key.message=WM_KEYDOWN;key.wParam=VK_ESCAPE;key.lParam=1;if(!IsDialogMessageW(license,&key)){TranslateMessage(&key);DispatchMessageW(&key);}pump();}
        else{SendMessageW(GetDlgItem(license,4),BM_CLICK,0,0);pump();}
        SCROLLINFO after{sizeof(after),SIF_POS};GetScrollInfo(settings,SB_VERT,&after);const auto restored=relative_rect(button,settings);
        check(!IsWindow(license)&&IsWindow(settings)&&IsWindowVisible(settings)&&IsWindowEnabled(owner)&&GetDlgItem(settings,5720)==rgb&&GetDlgItem(settings,5141)==button&&GetFocus()==button,
            "Closing an internal license failed to restore the same live Settings controls/focus");
        check(after.nPos==before.nPos&&EqualRect(&button_rect,&restored)&&native_text(rgb)==L"123"&&IsWindowEnabled(GetDlgItem(settings,5123))&&data==saved&&writes==0&&closed==0,
            "License navigation altered the Settings scroll, unsaved colour draft, saved preferences or student data");
    }
    DestroyWindow(settings);std::cout<<"PASS production Settings -> license list -> original notice -> Close/Escape: same root enabled, hidden owner, exact live controls/focus/scroll/draft restored\n";
}
LRESULT CALLBACK measurement_probe(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){
    if(message==WM_GETTEXT||message==WM_GETTEXTLENGTH)++*reinterpret_cast<int*>(raw);return DefSubclassProc(window,message,wp,lp);
}
class ChildGeometryProbe{
    struct Item{HWND window;RECT bounds;LONG_PTR style,extended_style;int id,position_changes=0;};
    HWND parent_;std::vector<Item> items_;
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR raw){if(message==WM_WINDOWPOSCHANGED)++reinterpret_cast<Item*>(raw)->position_changes;return DefSubclassProc(window,message,wp,lp);}
public:
    explicit ChildGeometryProbe(HWND parent):parent_(parent){
        for(auto child=GetWindow(parent,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))items_.push_back({child,relative_rect(child,parent),GetWindowLongPtrW(child,GWL_STYLE),GetWindowLongPtrW(child,GWL_EXSTYLE),GetDlgCtrlID(child)});
        for(auto& item:items_)check(SetWindowSubclass(item.window,proc,2,reinterpret_cast<DWORD_PTR>(&item))!=FALSE,"Cannot observe the actual Settings child layout");
    }
    ~ChildGeometryProbe(){for(const auto& item:items_)if(IsWindow(item.window))RemoveWindowSubclass(item.window,proc,2);}
    void verify_shift(LONG delta)const{auto child=GetWindow(parent_,GW_CHILD);int observed=0;
        for(const auto& item:items_){check(child==item.window&&GetParent(child)==parent_&&GetDlgCtrlID(child)==item.id,"Scrolling changed a Settings HWND, identity or sibling order");child=GetWindow(child,GW_HWNDNEXT);
            check(GetWindowLongPtrW(item.window,GWL_STYLE)==item.style&&GetWindowLongPtrW(item.window,GWL_EXSTYLE)==item.extended_style,"Scrolling changed a Settings control's styles");
            if(!(item.style&WS_VISIBLE))continue;const auto bounds=relative_rect(item.window,parent_);
            check(bounds.left==item.bounds.left&&bounds.right==item.bounds.right&&bounds.top==item.bounds.top+delta&&bounds.bottom==item.bounds.bottom+delta,"A grouped native scroll changed a control's measured size or relative placement");
            check(item.position_changes==1,"A grouped scroll positioned the same unchanged Settings child more than once");++observed;}
        check(!child&&observed>20,"The grouped layout fixture did not observe the complete real Settings controls");
    }
};
void settings_presentation_tests(){
    using namespace lgaed;using namespace presentation;state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    ScopedFixture fixture(SettingsScope::global,seed);const int dpi=static_cast<int>(GetDpiForWindow(fixture.view));const auto px=[dpi](int value){return MulDiv(value,dpi,96);};
    const auto title=child_with_caption(fixture.view,L"Título del contenido"),subtitle=child_with_caption(fixture.view,L"Configuración general de LearnGameAndEngineDev");
    const auto section=child_with_caption(fixture.view,L"Idioma de la aplicación");check(title&&subtitle&&section,"Settings omitted its visible hierarchy");
    if(!reader_theme::palette(fixture.view).high_contrast){
        check(reader_theme::foreground(title)==RGB(255,255,255)&&reader_theme::foreground(section)==RGB(255,255,255),"Options headings lost their Android white ink");
        check(reader_theme::foreground(subtitle)==RGB(174,180,190),"Options subtitle lost its Android secondary ink");
        const auto section_description=child_with_caption(fixture.view,L"Selecciona el idioma de la aplicación, el contenido y la narración.");
        const auto card_description=child_with_caption(fixture.view,L"Comprobaciones ilimitadas, sin consumir corazones.");
        const auto narration_button=fixture.control(5104);
        check(section_description&&reader_theme::foreground(section_description)==RGB(174,180,190),"A section description uses the muted card-body ink");
        check(card_description&&reader_theme::foreground(card_description)==RGB(143,149,159),"A card description lost its Android muted ink");
        check(narration_button&&native_text(narration_button)==L"Motor: Kokoro","The narration engine is not the original action with its effective selection");
        RECT button_bounds{};GetClientRect(narration_button,&button_bounds);const auto source=GetDC(narration_button),memory=CreateCompatibleDC(source);const auto image=CreateCompatibleBitmap(source,button_bounds.right,button_bounds.bottom);ReleaseDC(narration_button,source);
        check(memory&&image,"Cannot capture the real narration action");const auto previous=SelectObject(memory,image);DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=5104;draw.itemAction=ODA_DRAWENTIRE;draw.hwndItem=narration_button;draw.hDC=memory;draw.rcItem=button_bounds;
        const auto painted=SendMessageW(fixture.view,WM_DRAWITEM,5104,reinterpret_cast<LPARAM>(&draw));const auto fill=GetPixel(memory,button_bounds.right/2,px(8));SelectObject(memory,previous);DeleteObject(image);DeleteDC(memory);
        check(painted&&fill==reader_theme::palette(fixture.view).accent,"Narration selection lost Android's real accent-filled action style");
    }
    SetWindowTextW(title,L"Título del contenido con una segunda parte");
    const auto title_font=control_font(title),subtitle_font=control_font(subtitle),section_font=control_font(section),button_font=control_font(fixture.control(5123));
    check(-title_font.lfHeight>=MulDiv(32,dpi,96)&&title_font.lfWeight>=FW_BOLD,"Settings title does not use the Android32-sp hierarchy");
    check(-subtitle_font.lfHeight>=MulDiv(18,dpi,96)&&-section_font.lfHeight>=MulDiv(23,dpi,96),"Settings subtitle or section hierarchy remains undersized");
    check(-button_font.lfHeight>=MulDiv(18,dpi,96)&&button_font.lfWeight>=FW_BOLD,"Primary settings action is not18-sp bold");
    LONG narrow_title_height=0,wide_title_height=0;
    for(const int width:{360,760}){
        MoveWindow(fixture.view,0,0,px(width),px(520),TRUE);pump();const auto title_rect=relative_rect(title,fixture.view),subtitle_rect=relative_rect(subtitle,fixture.view),section_rect=relative_rect(section,fixture.view);
        check(subtitle_rect.top>=title_rect.bottom&&section_rect.top>=subtitle_rect.bottom,"Growing settings text overlaps the following row");
        if(width==360)narrow_title_height=title_rect.bottom-title_rect.top;else wide_title_height=title_rect.bottom-title_rect.top;
        const auto combo=fixture.control(5101);const auto bounds=relative_rect(combo,fixture.view);RECT client{};GetClientRect(fixture.view,&client);
        check(GetParent(combo)==fixture.view&&GetDlgCtrlID(combo)==5101,"Decorating the selector changed its native parent or ID");
        check(bounds.left==px(42)&&client.right-bounds.right==px(42),"Selector card omitted its18 DIP inner padding or24 DIP page margin");
        check(SendMessageW(combo,CB_GETITEMHEIGHT,static_cast<WPARAM>(-1),0)>=px(56),"Native selector fell below the Android56 DIP field reference");
        if(!IsWindowEnabled(fixture.control(5123)))fixture.toggle(5128,!fixture.checked(5128));const auto save=relative_rect(fixture.control(5123),fixture.view);check(save.bottom-save.top>=px(62),"Primary settings action is shorter than the reference62 DIP minimum");
    }
    check(narrow_title_height>wide_title_height,"The settings title did not grow naturally on the narrow viewport");
    int measurement_reads=0;check(SetWindowSubclass(title,measurement_probe,1,reinterpret_cast<DWORD_PTR>(&measurement_reads))!=FALSE,"Cannot observe the real text measurement boundary");
    SendMessageW(fixture.view,WM_SIZE,0,0);check(measurement_reads>0,"Settings size changes did not measure the current title");measurement_reads=0;
    const auto old_title=relative_rect(title,fixture.view);{
    ChildGeometryProbe children(fixture.view);SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);const auto scrolled_title=relative_rect(title,fixture.view);children.verify_shift(scrolled_title.top-old_title.top);
    check(measurement_reads==0&&scrolled_title.top<old_title.top&&scrolled_title.bottom-scrolled_title.top==old_title.bottom-old_title.top,"A pure scroll remeasured every Settings row or changed its text geometry");
    LONG previous_top=scrolled_title.top;for(int count=0;count<3;++count){SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);const auto current=relative_rect(title,fixture.view);check(current.top<previous_top&&current.bottom-current.top==old_title.bottom-old_title.top,"Repeated native line scrolling lost direction or stable text geometry");previous_top=current.top;}
    for(int count=0;count<4;++count)SendMessageW(fixture.view,WM_VSCROLL,SB_LINEUP,0);
    check(measurement_reads==0&&relative_rect(title,fixture.view).top==old_title.top,"Reverse native line scrolling lost its position or remeasured unchanged text");}
    RemoveWindowSubclass(title,measurement_probe,1);
    MoveWindow(fixture.view,0,0,px(360),px(520),TRUE);pump();const auto combo=fixture.control(5101);
    for(int count=0;count<200;++count){const auto bounds=relative_rect(combo,fixture.view);RECT client{};GetClientRect(fixture.view,&client);if(bounds.top>=px(18)+1&&bounds.bottom<=client.bottom-px(18)-1)break;SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);}
    const auto bounds=relative_rect(combo,fixture.view);RECT client{};GetClientRect(fixture.view,&client);
    check(bounds.top>=px(18)&&bounds.bottom<=client.bottom-px(18),"Padded selector card cannot be scrolled fully into view");
    const auto screen=GetDC(fixture.view),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,client.right,client.bottom);ReleaseDC(fixture.view,screen);
    check(dc&&bitmap,"Cannot capture selector-card presentation");const auto previous=SelectObject(dc,bitmap);SendMessageW(fixture.view,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    const auto palette=reader_theme::palette(fixture.view);const int y=(bounds.top+bounds.bottom)/2;
    const auto expected_card=palette.high_contrast?palette.card:RGB(32,33,38),expected_canvas=palette.high_contrast?palette.canvas:RGB(16,17,20);
    const bool card=GetPixel(dc,bounds.left-px(9),y)==expected_card,canvas=GetPixel(dc,px(4),y)==expected_canvas;
    check(reader_theme::background(fixture.view)==expected_canvas&&reader_theme::background(combo)==expected_card,"Options surfaces differ from Android XML");
    if(!palette.high_contrast){const int sample_x=bounds.left-px(9);int card_top=y,card_bottom=y+1;
        while(card_top>0&&GetPixel(dc,sample_x,card_top-1)==expected_card)--card_top;
        while(card_bottom<client.bottom&&GetPixel(dc,sample_x,card_bottom)==expected_card)++card_bottom;
        check(std::abs(card_top-(bounds.top-px(18)))<=1&&std::abs(card_bottom-(bounds.bottom+px(18)))<=1,
            "A selector inside a card receives its18 DIP vertical padding twice");
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);check(card&&canvas,"The18 DIP selector padding has no real card painted behind it");
    if(!palette.high_contrast){const auto reset=fixture.control(5134);
    SetWindowTextW(reset,L"Reiniciar únicamente el progreso y las respuestas de este contenido de Programación");
    SendMessageW(fixture.view,WM_SIZE,0,0);pump();RECT reset_bounds{};GetClientRect(reset,&reset_bounds);
    const auto reset_screen=GetDC(reset),reset_dc=CreateCompatibleDC(reset_screen);
    const auto reset_bitmap=CreateCompatibleBitmap(reset_screen,reset_bounds.right,reset_bounds.bottom);ReleaseDC(reset,reset_screen);
    check(reset_dc&&reset_bitmap,"Cannot capture the long reset action");const auto old_bitmap=SelectObject(reset_dc,reset_bitmap);
    const auto old_font=SelectObject(reset_dc,reinterpret_cast<HFONT>(SendMessageW(reset,WM_GETFONT,0,0)));
    TEXTMETRICW metrics{};GetTextMetricsW(reset_dc,&metrics);auto text_bounds=reset_bounds;InflateRect(&text_bounds,-px(14),-px(8));auto measured=text_bounds;
    const auto caption=native_text(reset);DrawTextW(reset_dc,caption.c_str(),static_cast<int>(caption.size()),&measured,DT_CENTER|DT_WORDBREAK|DT_CALCRECT|DT_NOPREFIX);
    const int text_height=measured.bottom-measured.top;
    check(text_height>=3*metrics.tmHeight&&reset_bounds.bottom>=text_height+px(16),"The long reset fixture did not retain all its wrapped lines");
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=5134;draw.itemAction=ODA_DRAWENTIRE;draw.hwndItem=reset;draw.hDC=reset_dc;draw.rcItem=reset_bounds;
    check(SendMessageW(fixture.view,WM_DRAWITEM,5134,reinterpret_cast<LPARAM>(&draw))!=FALSE,"The native reset action was not painted");
    const auto ink=reader_theme::accent_foreground(fixture.view);
    const int last_line_top=text_bounds.top+std::max<LONG>(0,(text_bounds.bottom-text_bounds.top-text_height)/2)+text_height-metrics.tmHeight;
    int last_line_pixels=0;for(int row=last_line_top;row<std::min<LONG>(last_line_top+metrics.tmHeight,reset_bounds.bottom);++row)
        for(int column=text_bounds.left;column<text_bounds.right;++column)if(GetPixel(reset_dc,column,row)==ink)++last_line_pixels;
    SelectObject(reset_dc,old_font);SelectObject(reset_dc,old_bitmap);DeleteObject(reset_bitmap);DeleteDC(reset_dc);
    check(last_line_pixels>10,"A long reset action lost its final wrapped text line");}
    check(fixture.data==seed&&fixture.updates==0,"Settings presentation changed preferences or student data");
    std::cout<<"PASS native settings presentation: reference font hierarchy, padded selector cards, minimum actions and responsive text growth\n";
}
void settings_dpi_notice_tests(){
    using namespace lgaed;using namespace presentation;state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    ScopedFixture fixture(SettingsScope::global,seed);fixture.toggle(5128,false);fixture.scale(125);const auto before=fixture.data;
    const auto title=child_with_caption(fixture.view,L"Título del contenido"),language=fixture.control(5101);const auto old_font=SendMessageW(title,WM_GETFONT,0,0);
    SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);SCROLLINFO prior_scroll{sizeof(prior_scroll),SIF_POS};GetScrollInfo(fixture.view,SB_VERT,&prior_scroll);
    SendMessageW(fixture.view,WM_DPICHANGED_AFTERPARENT,0,0);const auto face=control_font(title);SCROLLINFO after_scroll{sizeof(after_scroll),SIF_POS};GetScrollInfo(fixture.view,SB_VERT,&after_scroll);
    check(SendMessageW(title,WM_GETFONT,0,0)!=old_font&&-face.lfHeight==MulDiv(32,GetDpiForWindow(fixture.view),96),"Settings kept the old font after a parent DPI notification");
    check(fixture.control(5101)==language&&fixture.data==before&&prior_scroll.nPos==after_scroll.nPos&&!fixture.checked(5128)&&IsWindowEnabled(fixture.control(5123)),"DPI refresh replaced controls, lost buffered colours/scroll or modified preferences");
    ShowWindow(fixture.host,SW_SHOWNOACTIVATE);SetActiveWindow(fixture.host);SetFocus(language);check(GetFocus()==language,"The notice fixture could not focus its original native input");const auto active=GetActiveWindow();
    const auto popup_text=L"Aviso breve de prueba con tipografía escalada";show_settings_notice(fixture.host,"Aviso breve de prueba con tipografía escalada");HWND popup=nullptr;
    popup=FindWindowExW(fixture.host,nullptr,L"STATIC",popup_text);
    check(popup&&native_text(popup)==popup_text&&IsWindowVisible(popup)&&(GetWindowLongPtrW(popup,GWL_STYLE)&WS_CHILD)&&GetParent(popup)==fixture.host,"The informational notice did not stay inside its application page");const auto notice=control_font(popup);
    check(GetFocus()==language&&GetActiveWindow()==active,"Creating the nonblocking notice stole focus or activated a different window");pump();
    check(GetFocus()==language&&GetActiveWindow()==active,"Painting the nonblocking notice stole focus from the original input");
    check(_wcsicmp(notice.lfFaceName,L"Roboto")==0&&-notice.lfHeight==MulDiv(16,GetDpiForWindow(popup),96),"The brief notice still uses an unscaled Windows stock font");
    check((GetWindowLongPtrW(popup,GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0,"Giving the notice a font made it take keyboard focus");DestroyWindow(popup);
    const std::array<const char*,3> locales{"es","en","fr"};const std::array<const wchar_t*,3> messages{L"Excepciones de Programación restablecidas",L"Programming Exceptions Reset",L"Exceptions de programmation réinitialisées"};
    for(std::size_t index=0;index<locales.size();++index){auto localized=seed;localized.preferences["native_windows"]["ui_language"]=std::string(locales[index]);ScopedFixture options(SettingsScope::global,localized);
        click_wrap_reset(options,5133,false);check(options.data==localized,"Cancelling localized reset changed settings");click_wrap_reset(options,5133);HWND success=nullptr;
        success=FindWindowExW(options.view,nullptr,L"STATIC",messages[index]);
        check(success!=nullptr,"Confirmed reset omitted its translated nonblocking success notice");DestroyWindow(success);}
    std::cout<<"PASS Settings DPI notification/font recreation with retained handles/state/drafts/scroll and real scaled nonactivating notices; ES/EN/FR reset Cancel/confirm/success\n";
}
void rgb_options_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    seed.preferences["learning_progress"]["answer"]=std::string("conservar exactamente");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    seed.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("ef_santa");
    auto& accents=seed.preferences["appearance_settings"];
    const std::array<std::string,3> routes{"","programming","math_physics"};
    const std::array<std::string,3> channels{"red","green","blue"};
    const auto key=[](std::string_view route,std::string_view channel,bool defaults){
        return defaults?(route.empty()?"default_global_accent_"+std::string(channel):"default_route_accent_"+std::string(channel)+"_"+std::string(route)):
            route.empty()?"accent_"+std::string(channel):"route_accent_"+std::string(channel)+"_"+std::string(route);};
    for(std::size_t index=0;index<routes.size();++index)for(std::size_t channel=0;channel<channels.size();++channel){
        accents[key(routes[index],channels[channel],false)]=static_cast<std::int64_t>(10+index*30+channel*10);
        accents[key(routes[index],channels[channel],true)]=static_cast<std::int64_t>(1+index*3+channel);}
    accents["route_allow_global_accent_overwrite_programming"]=true;accents["route_allow_global_accent_overwrite_math_physics"]=true;
    ScopedFixture fixture(SettingsScope::global,seed);const auto px=[dpi=static_cast<int>(GetDpiForWindow(fixture.view))](int value){return MulDiv(value,dpi,96);};
    const auto edit=[&](int id,const wchar_t* text){check(fixture.control(id)!=nullptr,"A real RGB field is missing");SetWindowTextW(fixture.control(id),text);};
    for(int index=0;index<9;++index){const auto slider=fixture.control(5700+index),field=fixture.control(5720+index),defaults=fixture.control(5760+index);
        check(slider&&field&&defaults&&SendMessageW(slider,TBM_GETRANGEMIN,0,0)==0&&SendMessageW(slider,TBM_GETRANGEMAX,0,0)==255,"Options omitted an original RGB slider/manual/default field or changed its0–255 range");
        check(native_text(field)==std::to_wstring(10+(index/3)*30+(index%3)*10)&&native_text(defaults)==std::to_wstring(1+index),"Opening RGB controls did not load the nine current values and nine independent defaults");
        for(const auto input:{field,defaults}){wchar_t hint[8]{};check(SendMessageW(input,EM_GETCUEBANNER,reinterpret_cast<WPARAM>(hint),std::size(hint))&&std::wstring_view(hint)==std::array<const wchar_t*,3>{L"R",L"G",L"B"}[index%3],"An original manual/default RGB field lost its localized channel hint");}
        const auto bounds=relative_rect(field,fixture.view),default_bounds=relative_rect(defaults,fixture.view);
        check(bounds.bottom-bounds.top==px(52)&&default_bounds.bottom-default_bounds.top==px(52),"RGB manual fields lost their Android52 DIP height");
        if(index%3){const auto previous=relative_rect(fixture.control(5720+index-1),fixture.view),previous_default=relative_rect(fixture.control(5760+index-1),fixture.view);
            check(bounds.top==previous.top&&bounds.left>=previous.right+px(8)&&default_bounds.top==previous_default.top&&default_bounds.left>=previous_default.right+px(8),"RGB fields are not three aligned nonoverlapping columns");}}
    for(int index=0;index<3;++index){const auto preview=fixture.control(5125+index);wchar_t name[32]{};GetClassNameW(preview,name,32);
        check(_wcsicmp(name,L"BUTTON")==0&&(GetWindowLongPtrW(preview,GWL_STYLE)&BS_TYPEMASK)==BS_OWNERDRAW&&(GetWindowLongPtrW(preview,GWL_STYLE)&WS_TABSTOP),"The RGB preview is not an accessible native colour-picker button");
        check(native_text(fixture.control(5130+index))==L"Cargar color predeterminado","The original Load Default Color action is missing");
        if(!reader_theme::palette(fixture.view).high_contrast)check(reader_theme::background(fixture.control(5780+index))==RGB(24,25,29)&&reader_theme::background(fixture.control(5760+index*3))==RGB(24,25,29),"Default RGB values lost their nested Android card surface");}
    check(fixture.data==seed&&fixture.updates==0,"Opening RGB controls applied colour drafts");
    ShowWindow(fixture.host,SW_SHOWNOACTIVATE);SetActiveWindow(fixture.host);
    for(const int id:{5720,5760}){const auto input=fixture.control(id);reveal_control(fixture.view,input);SetFocus(fixture.control(5100));
        RECT input_bounds{};GetClientRect(input,&input_bounds);const auto point=MAKELPARAM(2,input_bounds.bottom/2);
        SendMessageW(input,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(input,WM_LBUTTONUP,0,point);
        DWORD first=0,last=0;SendMessageW(input,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        check(GetFocus()==input&&first==0&&last==native_text(input).size(),"The first native click failed to select the whole RGB value as Android selectAllOnFocus does");
        SendMessageW(input,WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(input,WM_LBUTTONUP,0,point);SendMessageW(input,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        check(first==last,"An already-focused RGB field forcibly reselected all text instead of allowing a normal caret click");
        SetFocus(fixture.control(5100));SetFocus(input);SendMessageW(input,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
        check(GetFocus()==input&&first==0&&last==native_text(input).size(),"Keyboard focus failed to select all of an original manual/default RGB value");}
    ShowWindow(fixture.host,SW_HIDE);check(fixture.data==seed&&fixture.updates==0,"Focusing RGB inputs modified colour drafts or persisted data");
    edit(5720,L"");for(const wchar_t digit:std::wstring_view(L"123"))SendMessageW(fixture.control(5720),WM_CHAR,digit,1);
    DWORD selection_start=0,selection_end=0;SendMessageW(fixture.control(5720),EM_GETSEL,reinterpret_cast<WPARAM>(&selection_start),reinterpret_cast<LPARAM>(&selection_end));
    check(native_text(fixture.control(5720))==L"123"&&selection_start==3&&selection_end==3&&SendMessageW(fixture.control(5700),TBM_GETPOS,0,0)==123&&native_text(fixture.control(5740))==L"Rojo · 123","Manual RGB input did not synchronize its slider/label or preserve the typing cursor");
    edit(5721,L"999");check(native_text(fixture.control(5721))==L"255"&&SendMessageW(fixture.control(5701),TBM_GETPOS,0,0)==255,"Out-of-range RGB input did not clamp to255");
    edit(5722,L"12x");check(native_text(fixture.control(5722))==L"12x"&&!IsWindowEnabled(fixture.control(5123))&&(GetWindowLongPtrW(fixture.control(5123),GWL_STYLE)&WS_VISIBLE),"An invalid RGB draft was silently erased or allowed Save");
    SendMessageW(fixture.view,WM_COMMAND,MAKEWPARAM(5123,BN_CLICKED),reinterpret_cast<LPARAM>(fixture.control(5123)));
    check(fixture.data==seed&&fixture.updates==0,"A direct Save command persisted invalid RGB values");
    SendMessageW(fixture.view,WM_DPICHANGED_AFTERPARENT,0,0);check(native_text(fixture.control(5722))==L"12x"&&fixture.data==seed,"DPI refresh discarded an unfinished RGB field or applied colour drafts");
    edit(5722,L"42");edit(5723,L"227");edit(5724,L"87");edit(5725,L"99");edit(5760,L"200");fixture.toggle(5129,false);
    check(fixture.data==seed&&fixture.updates==0,"RGB or default/protection edits were persisted before Save");
    fixture.click(5123);auto expected=seed;
    const std::array<COLORREF,3> expected_colors{RGB(123,255,42),RGB(227,87,99),RGB(70,80,90)};
    for(std::size_t index=0;index<routes.size();++index){const auto color=expected_colors[index];const std::array<int,3> values{GetRValue(color),GetGValue(color),GetBValue(color)};
        for(std::size_t channel=0;channel<channels.size();++channel)expected.preferences["appearance_settings"][key(routes[index],channels[channel],false)]=static_cast<std::int64_t>(values[channel]);}
    expected.preferences["appearance_settings"]["route_allow_global_accent_overwrite_math_physics"]=false;
    expected.preferences["appearance_settings"]["default_global_accent_red"]=std::int64_t{200};
    check(fixture.updates==1&&fixture.changes==1&&fixture.data==expected,"RGB Save did not make one exact transaction with explicit route priority, protection and preserved student data");
    for(int index=0;index<9;++index){const auto text=std::to_wstring(11+(index/3)*10+index%3);edit(5760+index,text.c_str());}
    check(fixture.data==expected,"Editing default values immediately changed the application colours");fixture.click(5123);
    for(std::size_t index=0;index<routes.size();++index)for(std::size_t channel=0;channel<channels.size();++channel)
        expected.preferences["appearance_settings"][key(routes[index],channels[channel],true)]=static_cast<std::int64_t>(11+index*10+channel);
    check(fixture.updates==2&&fixture.data==expected,"Saving all nine defaults changed the selected colours or unrelated state");
    for(int index=0;index<3;++index)fixture.click(5130+index);
    for(int index=0;index<9;++index)check(native_text(fixture.control(5720+index))==std::to_wstring(11+(index/3)*10+index%3)&&SendMessageW(fixture.control(5700+index),TBM_GETPOS,0,0)==11+(index/3)*10+index%3,"Load Default Color did not copy its three pending values into the manual fields and sliders");
    check(fixture.data==expected&&fixture.updates==2,"Loading RGB defaults applied them before Save");
    edit(5768,L"");fixture.data.preferences["native_windows"]["ui_language"]=std::string("fr");const auto localized=fixture.data;
    refresh_settings_language(fixture.view);pump();
    check(native_text(fixture.control(5768)).empty()&&!IsWindowEnabled(fixture.control(5123))&&native_text(fixture.control(5740))==L"Rouge · 11"&&native_text(fixture.control(5130))==L"Charger la couleur par défaut"&&fixture.data==localized,"Changing interface language lost RGB drafts, applied them or omitted original French labels");
    for(const int id:{5721,5761}){wchar_t hint[8]{};check(SendMessageW(fixture.control(id),EM_GETCUEBANNER,reinterpret_cast<WPARAM>(hint),std::size(hint))&&std::wstring_view(hint)==L"V","French RGB fields retained a G hint instead of the original V for Vert");}
    check(child_with_caption(fixture.view,L"Voix et ressources Kokoro")!=nullptr,"Changing language omitted the original Kokoro resources heading");
    fixture.click(5100);check(fixture.closed==1&&fixture.data==localized&&fixture.updates==2,"Leaving without Save committed an unfinished RGB/default draft");
    {ScopedFixture reopened(SettingsScope::global,localized);check(native_text(reopened.control(5720))==L"123"&&native_text(reopened.control(5768))==L"33"&&!IsWindowEnabled(reopened.control(5123)),"Cancelled RGB defaults leaked into the reopened Settings window");}
    std::cout<<"PASS inline RGB parity: nine sliders/current fields/defaults, aligned nested cards, typing cursor, invalid input, DPI/language-preserved drafts, one scoped Save, Load and Cancel\n";
}
void slider_pointer_tests(){
    using namespace lgaed;using namespace presentation;state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    seed.preferences["learning_progress"]["answer"]=std::string("respuesta intacta");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_global_text_scale(seed,100);ScopedFixture fixture(SettingsScope::global,seed);
    ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();const auto slider=fixture.control(5120);reveal_control(fixture.view,slider);
    const auto geometry=[](HWND target){RECT channel{},thumb{};SendMessageW(target,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));SendMessageW(target,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));return std::pair{channel,(thumb.top+thumb.bottom)/2};};
    const auto click=[&](HWND target,int x,int y){SendMessageW(target,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));check(GetFocus()==target&&GetCapture()==target,"A Settings pointer press did not preserve native slider focus and capture");SendMessageW(target,WM_LBUTTONUP,0,MAKELPARAM(x,y));pump();check(GetCapture()!=target,"A Settings pointer release retained the drag capture");};
    auto [channel,y]=geometry(slider);check(channel.right>channel.left,"The real Settings slider has no measurable channel");
    click(slider,channel.right,y);check(SendMessageW(slider,TBM_GETPOS,0,0)==150&&appearance::global_text_scale(fixture.data)==150&&fixture.changes>0,"Clicking the slider's right endpoint stepped by page size instead of selecting and saving150 percent");
    click(slider,(channel.left+channel.right)/2,y);const auto middle=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0));
    check(middle>=112&&middle<=113&&appearance::global_text_scale(fixture.data)==middle,"Clicking the middle did not select its real range position and persist it live");
    SendMessageW(slider,WM_KEYDOWN,VK_LEFT,0);SendMessageW(slider,WM_KEYUP,VK_LEFT,0);
    check(SendMessageW(slider,TBM_GETPOS,0,0)==middle-1&&appearance::global_text_scale(fixture.data)==middle-1,"Pointer parity changed the slider's native one-step keyboard behavior");
    SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(channel.left,y));check(SendMessageW(slider,TBM_GETPOS,0,0)==75&&GetCapture()==slider,"Pressing the left endpoint did not start a native drag at75 percent");
    SendMessageW(slider,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(channel.right,y));check(SendMessageW(slider,TBM_GETPOS,0,0)==150&&appearance::global_text_scale(fixture.data)==150,"Dragging a jumped thumb did not continue to its real pointer position");
    SendMessageW(slider,WM_LBUTTONUP,0,MAKELPARAM(channel.right,y));check(GetCapture()!=slider,"Finishing the native Settings drag did not release capture");
    const auto rgb=fixture.control(5700);reveal_control(fixture.view,rgb);std::tie(channel,y)=geometry(rgb);const auto before=fixture.data;
    click(rgb,channel.right,y);check(SendMessageW(rgb,TBM_GETPOS,0,0)==255&&native_text(fixture.control(5720))==L"255"&&native_text(fixture.control(5740))==L"Rojo · 255"&&fixture.data==before&&IsWindowEnabled(fixture.control(5123)),"A real RGB pointer selection lost its manual/label mapping or escaped deferred Save");
    fixture.click(5123);check(GetRValue(reader_theme::appearance_accent(fixture.data))==255,"Saving a pointer-selected RGB value did not persist the selected channel");
    check(fixture.data.preferences["learning_progress"]==seed.preferences["learning_progress"]&&fixture.data.preferences["cpp_lesson_drafts"]==seed.preferences["cpp_lesson_drafts"],"Settings slider gestures changed protected student data or whitespace");
    ScopedFixture inherited(SettingsScope::lesson,seed);const auto disabled=inherited.control(5120);check(!IsWindowEnabled(disabled),"The inherited course-size fixture is unexpectedly editable");
    const auto disabled_value=SendMessageW(disabled,TBM_GETPOS,0,0);std::tie(channel,y)=geometry(disabled);SendMessageW(disabled,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(channel.right,y));
    check(SendMessageW(disabled,TBM_GETPOS,0,0)==disabled_value&&inherited.data==seed&&GetCapture()!=disabled,"A raw pointer press bypassed a disabled inherited Settings control");
    std::cout<<"PASS Settings real pointer endpoints/midpoint/native drag and keyboard; live scale callbacks, deferred RGB Save, disabled inheritance and protected data\n";
}
void slider_wheel_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    ScopedFixture fixture(SettingsScope::global,data);ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
    for(const auto id:{5120,5700,5701,5702,5703,5704,5705,5706,5707,5708}){
        const auto slider=fixture.control(id);reveal_control(fixture.view,slider);SetFocus(slider);
        const auto value=SendMessageW(slider,TBM_GETPOS,0,0);const auto before=fixture.data;const int updates=fixture.updates,changes=fixture.changes;
        const auto rgb_text=native_text(fixture.control(5720+(id>=5700?id-5700:0)));
        SCROLLINFO initial{sizeof(initial),SIF_POS};check(GetScrollInfo(fixture.view,SB_VERT,&initial)!=FALSE,"Read Settings scroll before wheel on slider");
        SendMessageW(slider,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
        SCROLLINFO moved{sizeof(moved),SIF_POS};GetScrollInfo(fixture.view,SB_VERT,&moved);
        check(moved.nPos>initial.nPos,"Wheel over a focused Settings slider did not scroll its page");
        SendMessageW(slider,WM_MOUSEHWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
        check(SendMessageW(slider,TBM_GETPOS,0,0)==value&&fixture.data==before&&fixture.updates==updates&&fixture.changes==changes&&native_text(fixture.control(5720+(id>=5700?id-5700:0)))==rgb_text,
            "Wheel changed a Settings slider, colour draft or saved preference");
    }
    std::cout<<"PASS focused text-size and all nine RGB sliders: wheel scrolls page, horizontal wheel leaves values/data/drafts unchanged\n";
}
struct SettingsThumbProbe{
    int notifications=0,scroll_depth=0,nested_nc_calc=0,nested_nc_paint=0;std::vector<int> positions;
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR raw){
        auto& self=*reinterpret_cast<SettingsThumbProbe*>(raw);
        if(self.scroll_depth&&message==WM_NCCALCSIZE)++self.nested_nc_calc;
        if(self.scroll_depth&&message==WM_NCPAINT)++self.nested_nc_paint;
        const bool scroll=message==WM_VSCROLL&&!lp&&(LOWORD(wp)==SB_THUMBTRACK||LOWORD(wp)==SB_THUMBPOSITION);
        if(scroll)++self.scroll_depth;
        const auto result=DefSubclassProc(window,message,wp,lp);
        if(scroll)--self.scroll_depth;
        if(message==WM_VSCROLL&&LOWORD(wp)==SB_THUMBTRACK&&!lp){SCROLLINFO info{sizeof(info),SIF_POS};GetScrollInfo(window,SB_VERT,&info);++self.notifications;self.positions.push_back(info.nPos);}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(window,proc,id);return result;
    }
};
void settings_thumb_drag_test(bool focus_handshake){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["learning_progress"]["answer"]=std::string("unchanged");
    ScopedFixture fixture(SettingsScope::global,seed,"course_a",true,true);ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
    RECT host_client{},initial_view{};GetClientRect(fixture.host,&host_client);GetWindowRect(fixture.view,&initial_view);
    // Production sizes its child page to the root client, not to a fixed
    // fraction of an outer window whose frame grows with monitor DPI.
    MoveWindow(fixture.view,host_client.left,host_client.top,host_client.right-host_client.left,host_client.bottom-host_client.top,TRUE);pump();
    if(focus_handshake&&GetForegroundWindow()!=fixture.host){
        std::cout<<"NEED_FOCUS settings_scrollbar_paint host="<<static_cast<void*>(fixture.host)<<" pid="<<GetCurrentProcessId()<<std::endl;
        const auto deadline=GetTickCount64()+10000;
        while(GetForegroundWindow()!=fixture.host&&GetTickCount64()<deadline){
            MsgWaitForMultipleObjects(0,nullptr,FALSE,20,QS_ALLINPUT);pump();
        }
        check(GetForegroundWindow()==fixture.host,"The isolated Settings QA did not receive foreground within its focus handshake");
    }
    SendMessageW(fixture.view,WM_VSCROLL,SB_TOP,0);pump();SCROLLBARINFO bar{sizeof(bar)};
    check(GetScrollBarInfo(fixture.view,OBJID_VSCROLL,&bar)&&!(bar.rgstate[3]&STATE_SYSTEM_UNAVAILABLE),"Settings native scrollbar thumb is unavailable");
    const POINT first{(bar.rcScrollBar.left+bar.rcScrollBar.right)/2,bar.rcScrollBar.top+(bar.xyThumbTop+bar.xyThumbBottom)/2};
    const POINT last{first.x,bar.rcScrollBar.bottom-bar.dxyLineButton-30};
    const auto hit=WindowFromPoint(first),foreground=GetForegroundWindow();
    const auto hit_test=SendMessageW(fixture.view,WM_NCHITTEST,0,MAKELPARAM(first.x,first.y));
    RECT view_bounds{},host_bounds{};GetWindowRect(fixture.view,&view_bounds);GetWindowRect(fixture.host,&host_bounds);
    wchar_t hit_class[128]{},foreground_class[128]{};GetClassNameW(hit,hit_class,128);GetClassNameW(foreground,foreground_class,128);
    DWORD hit_pid=0,foreground_pid=0;GetWindowThreadProcessId(hit,&hit_pid);GetWindowThreadProcessId(foreground,&foreground_pid);
    std::cout<<"METRIC Settings thumb input: dpi="<<GetDpiForWindow(fixture.host)<<" host="<<static_cast<void*>(fixture.host)
        <<" host_bounds="<<host_bounds.left<<','<<host_bounds.top<<','<<host_bounds.right<<','<<host_bounds.bottom
        <<" host_client="<<host_client.left<<','<<host_client.top<<','<<host_client.right<<','<<host_client.bottom
        <<" initial_view_size="<<initial_view.right-initial_view.left<<'x'<<initial_view.bottom-initial_view.top
        <<" view="<<static_cast<void*>(fixture.view)<<" view_bounds="<<view_bounds.left<<','<<view_bounds.top<<','<<view_bounds.right<<','<<view_bounds.bottom
        <<" scrollbar="<<bar.rcScrollBar.left<<','<<bar.rcScrollBar.top<<','<<bar.rcScrollBar.right<<','<<bar.rcScrollBar.bottom
        <<" thumb="<<bar.xyThumbTop<<','<<bar.xyThumbBottom<<" first="<<first.x<<','<<first.y
        <<" hit="<<static_cast<void*>(hit)<<" hit_class="<<utf8(hit_class)<<" hit_pid="<<hit_pid<<" hit_test="<<hit_test
        <<" foreground="<<static_cast<void*>(foreground)<<" foreground_class="<<utf8(foreground_class)<<" foreground_pid="<<foreground_pid<<'\n';
    check(hit==fixture.view&&hit_test==HTVSCROLL,"The Settings fixture does not own its native scrollbar input");
    SCROLLINFO initial{sizeof(initial),SIF_ALL};GetScrollInfo(fixture.view,SB_VERT,&initial);const auto child=fixture.control(5101);const auto before=relative_rect(child,fixture.view);
    SettingsThumbProbe probe;check(SetWindowSubclass(fixture.view,SettingsThumbProbe::proc,0x51415354,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,"Observe real Settings scrollbar notifications");
    const auto expected_track=reader_theme::background(fixture.view);const bool observe_ink=!reader_theme::palette(fixture.view).high_contrast;
    struct TrackSample{COLORREF upper,lower;bool held,controlled;std::size_t phase;};std::vector<TrackSample> samples;
    const int sample_x=bar.rcScrollBar.left+std::max(1L,(bar.rcScrollBar.right-bar.rcScrollBar.left)/8);
    const int track_top=bar.rcScrollBar.top+bar.dxyLineButton,track_bottom=bar.rcScrollBar.bottom-bar.dxyLineButton;
    POINT original{};GetCursorPos(&original);std::atomic<bool> finished=false,input_ok=true;const auto dpi=GetThreadDpiAwarenessContext();
    std::thread input([&,dpi]{const auto previous=SetThreadDpiAwarenessContext(dpi);if(!previous){input_ok=false;finished=true;return;}
        Sleep(25);
        if(WindowFromPoint(first)!=fixture.view||GetForegroundWindow()!=fixture.host||(GetAsyncKeyState(VK_LBUTTON)&0x8000)){input_ok=false;SetThreadDpiAwarenessContext(previous);finished=true;return;}
        SetCursorPos(first.x,first.y);INPUT down{};down.type=INPUT_MOUSE;down.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
        if(SendInput(1,&down,sizeof(down))!=1){input_ok=false;SetThreadDpiAwarenessContext(previous);finished=true;return;}
        const auto observe=[&](std::size_t phase){const auto deadline=GetTickCount64()+350;const auto before=samples.size();
            do{const auto screen=GetDC(nullptr);if(!screen){input_ok=false;return;}
                const POINT upper{sample_x,track_top+(track_bottom-track_top)/4},lower{sample_x,track_top+(track_bottom-track_top)*3/4};
                const bool controlled=GetForegroundWindow()==fixture.host&&WindowFromPoint(upper)==fixture.view&&WindowFromPoint(lower)==fixture.view;
                samples.push_back({GetPixel(screen,upper.x,upper.y),GetPixel(screen,lower.x,lower.y),(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0,controlled,phase});
                ReleaseDC(nullptr,screen);Sleep(2);
            }while(samples.size()-before<4&&GetTickCount64()<deadline);};
        observe(0);SetCursorPos(first.x,first.y+(last.y-first.y)/2);observe(1);SetCursorPos(last.x,last.y);observe(2);
        INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;if(SendInput(1,&up,sizeof(up))!=1){input_ok=false;PostMessageW(fixture.view,WM_LBUTTONUP,0,0);}
        SetThreadDpiAwarenessContext(previous);finished=true;
    });
    while(!finished){MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);pump();}
    input.join();pump();SetCursorPos(original.x,original.y);RemoveWindowSubclass(fixture.view,SettingsThumbProbe::proc,0x51415354);
    SCROLLINFO after{sizeof(after),SIF_ALL};GetScrollInfo(fixture.view,SB_VERT,&after);const auto moved=relative_rect(child,fixture.view);
    SCROLLBARINFO final_bar{sizeof(final_bar)};check(GetScrollBarInfo(fixture.view,OBJID_VSCROLL,&final_bar)!=FALSE,"Read Settings scrollbar after actual tracking");
    const auto held=std::count_if(samples.begin(),samples.end(),[](const auto& sample){return sample.held&&sample.controlled;});
    std::array<std::size_t,3> phase_held{};for(const auto& sample:samples)if(sample.held&&sample.controlled)++phase_held[sample.phase];
    const auto light=std::count_if(samples.begin(),samples.end(),[&](const auto& sample){return sample.held&&sample.controlled&&(sample.upper!=expected_track||sample.lower!=expected_track);});
    std::cout<<"METRIC Settings tracking paint: nested_nccalc="<<probe.nested_nc_calc<<" nested_ncpaint="<<probe.nested_nc_paint
        <<" held_samples="<<held<<" phase_samples="<<phase_held[0]<<','<<phase_held[1]<<','<<phase_held[2]
        <<" wrong_track_samples="<<light<<" expected_track="<<expected_track<<'\n';
    for(std::size_t index=0;index<samples.size();++index){const auto& sample=samples[index];
        if(!sample.held||!sample.controlled||(sample.upper==expected_track&&sample.lower==expected_track))continue;
        const auto report=[&](const char* point,COLORREF ink,int y){std::cerr<<"Settings track mismatch: sample="<<index<<" phase="<<sample.phase
            <<" point="<<point<<" screen="<<sample_x<<','<<y<<" RGB="<<static_cast<int>(GetRValue(ink))<<','<<static_cast<int>(GetGValue(ink))<<','<<static_cast<int>(GetBValue(ink))
            <<" expected_RGB="<<static_cast<int>(GetRValue(expected_track))<<','<<static_cast<int>(GetGValue(expected_track))<<','<<static_cast<int>(GetBValue(expected_track))<<'\n';};
        if(sample.upper!=expected_track)report("upper",sample.upper,track_top+(track_bottom-track_top)/4);
        if(sample.lower!=expected_track)report("lower",sample.lower,track_top+(track_bottom-track_top)*3/4);
    }
    check(input_ok&&probe.notifications>=2&&std::ranges::any_of(probe.positions,[&](int value){return value>initial.nPos;}),"Dragging the real Settings scrollbar did not deliver continuing native thumb movement");
    check(after.nPos>initial.nPos&&before.top-moved.top==after.nPos-initial.nPos&&GetCapture()!=fixture.view,"The Settings thumb moved without its actual content or retained mouse capture");
    check(initial.nMin==after.nMin&&initial.nMax==after.nMax&&initial.nPage==after.nPage&&EqualRect(&bar.rcScrollBar,&final_bar.rcScrollBar),
        "A pure Settings thumb drag changed its range, page or native scrollbar frame");
    check(probe.nested_nc_calc==0&&(!observe_ink||probe.nested_nc_paint==0),"Settings needlessly recalculated or repainted its native frame inside a pure thumb-scroll callback");
    check(!observe_ink||(std::ranges::all_of(phase_held,[](auto count){return count>=2;})&&light==0),"The actual Settings scrollbar flashed a different track colour while its native thumb was held");
    check(fixture.data==seed&&fixture.updates==0&&fixture.changes==0,"Scrolling Settings changed protected student data or preferences");
    std::cout<<"PASS Settings real native thumb drag: notifications="<<probe.notifications<<" scroll="<<initial.nPos<<"->"<<after.nPos<<", exact content movement, released capture and unchanged data\n";
}
void settings_thumb_drag_tests(){settings_thumb_drag_test(false);}
void settings_scrollbar_paint_tests(){settings_thumb_drag_test(true);}
struct NativeColorAction{HWND owner=nullptr;bool accept=false,found=false;std::exception_ptr error;};
NativeColorAction* native_color_action=nullptr;
void CALLBACK native_color_timer(HWND,UINT,UINT_PTR timer,DWORD){
    if(!native_color_action)return;HWND dialog=nullptr;
    EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM raw)->BOOL{wchar_t name[32]{};GetClassNameW(candidate,name,32);
        if(std::wstring_view(name)==L"#32770"&&native_color_action&&GetWindow(candidate,GW_OWNER)==native_color_action->owner&&!(GetWindowLongPtrW(candidate,GWL_STYLE)&WS_CHILD)){*reinterpret_cast<HWND*>(raw)=candidate;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog)return;KillTimer(nullptr,timer);auto& action=*native_color_action;action.found=true;
    try{check(GetWindow(dialog,GW_OWNER)==action.owner&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_CHILD),"The requested Windows colour picker lost its main-window owner or was reparented");
        // The documented common-dialog protocol avoids depending on private
        // control IDs in a particular Windows colour-dialog resource.
        // https://learn.microsoft.com/windows/win32/dlgbox/setrgbstring
        const auto set_rgb=RegisterWindowMessageW(SETRGBSTRINGW);check(set_rgb!=0,"Register the documented Windows colour-picker message");
        SendMessageW(dialog,set_rgb,0,static_cast<LPARAM>(RGB(37,83,129)));
        SendMessageW(dialog,WM_COMMAND,action.accept?IDOK:IDCANCEL,0);
    }catch(...){action.error=std::current_exception();SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void native_color_picker_tests(){
    using namespace lgaed;using namespace presentation;state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    for(const bool accept:{false,true}){ScopedFixture fixture(SettingsScope::global,seed);ShowWindow(fixture.host,SW_SHOW);SetForegroundWindow(fixture.host);pump();
        const auto before=native_text(fixture.control(5720));NativeColorAction action{fixture.host,accept};native_color_action=&action;const auto timer=SetTimer(nullptr,0,20,native_color_timer);check(timer!=0,"Schedule the native Windows colour picker acceptance");
        SendMessageW(fixture.control(5125),BM_CLICK,0,0);KillTimer(nullptr,timer);native_color_action=nullptr;if(action.error)std::rethrow_exception(action.error);
        check(action.found&&fixture.data==seed&&fixture.updates==0,"Opening/accepting the native colour picker persisted colours before Save");
        if(accept){check(native_text(fixture.control(5720))==L"37"&&native_text(fixture.control(5721))==L"83"&&native_text(fixture.control(5722))==L"129"&&IsWindowEnabled(fixture.control(5123)),"The Windows colour selection did not update the RGB draft");
            fixture.click(5123);check(reader_theme::appearance_accent(fixture.data)==RGB(37,83,129)&&fixture.updates==1&&fixture.data.preferences["cpp_lesson_drafts"]==seed.preferences["cpp_lesson_drafts"],"Save did not commit exactly the selected Windows colour while preserving student drafts");
        }else check(native_text(fixture.control(5720))==before&&!IsWindowEnabled(fixture.control(5123)),"Cancelling the native colour picker modified a draft");
    }
    std::cout<<"PASS native Windows colour preview button: main owner, real RGB selection, Cancel, deferred Save and preserved drafts\n";
}
void live_storage_tests(){
    using namespace lgaed;using namespace presentation;state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["learning_progress"]["answer"]=std::string("unchanged");const auto before=data;
    const auto host=CreateWindowExW(0,L"STATIC",L"Live storage QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Create isolated storage observation host");
    int reads=0,writes=0;std::uint64_t stored=513;std::string generation="Preparando la lectura.";
    SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){++writes;change(data);};
    access.generation_status=[&]{return generation;};
    access.storage_snapshot=[&]{++reads;windows::NarrationStorageSnapshot snapshot;snapshot.stored_bytes=stored;snapshot.maximum_bytes=8ull*1024*1024*1024;snapshot.configured_bytes=snapshot.effective_bytes=4ull*1024*1024*1024;return snapshot;};
    const auto view=create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);ShowWindow(host,SW_SHOW);SetForegroundWindow(host);pump();
    const auto slider=GetDlgItem(view,5144),detail=GetDlgItem(view,5145);const auto position=SendMessageW(slider,TBM_GETPOS,0,0),range=SendMessageW(slider,TBM_GETRANGEMAX,0,0);
    check(native_text(detail).find(L"513 B")!=std::wstring::npos,"A small saved audio was misleadingly displayed as0.00 GB");
    const auto wait_for=[&](std::wstring_view expected){const auto deadline=GetTickCount64()+2500;do{pump();if(native_text(detail).find(expected)!=std::wstring::npos)return true;MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(GetTickCount64()<deadline);return false;};
    stored=7ull*1024*1024;generation="Audio listo.";check(wait_for(L"7,00 MB")&&reads>=2&&native_text(GetDlgItem(view,5156))==L"Audio listo.","New background usage/progress did not refresh while Settings remained open");
    reveal_control(view,slider);SetFocus(slider);SendMessageW(slider,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);SendMessageW(slider,WM_MOUSEHWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
    check(SendMessageW(slider,TBM_GETPOS,0,0)==position&&SendMessageW(slider,TBM_GETRANGEMAX,0,0)==range&&data==before&&writes==0,"Usage refresh/wheel changed the audio budget, slider range or student data");
    RECT thumb{};SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM((thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2));
    const auto captured=GetCapture();check(captured==slider,"Start a genuine native storage slider drag");const auto drag_position=SendMessageW(slider,TBM_GETPOS,0,0);const auto drag_data=data;const auto during=native_text(detail);
    stored=15ull*1024*1024;const auto deadline=GetTickCount64()+1150;while(GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    check(GetCapture()==slider&&SendMessageW(slider,TBM_GETPOS,0,0)==drag_position&&native_text(detail)==during&&data==drag_data,"Periodic storage observation disturbed an active native slider drag");
    SendMessageW(slider,WM_LBUTTONUP,0,MAKELPARAM((thumb.left+thumb.right)/2,(thumb.top+thumb.bottom)/2));check(wait_for(L"15,00 MB"),"Usage observation did not resume after releasing the slider");
    const auto screen=create_internal_window(view,L"STATIC",L"Internal storage cover QA",nullptr);ShowWindow(screen,SW_SHOW);stored=21ull*1024*1024;generation="Esperando espacio disponible.";
    SendMessageW(view,WM_DPICHANGED_AFTERPARENT,0,0);const auto hidden_until=GetTickCount64()+1150;while(GetTickCount64()<hidden_until){pump();MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    check(!IsWindowVisible(view)&&active_internal_window(host)==screen,"A periodic refresh/DPI update resurfaced Settings under its active child screen");
    DestroyWindow(screen);check(IsWindowVisible(view)&&wait_for(L"21,00 MB")&&native_text(GetDlgItem(view,5156))==L"Esperando espacio disponible.","Storage/progress did not resume on the same Settings page after returning from an internal screen");
    DestroyWindow(view);DestroyWindow(host);std::cout<<"PASS live storage/progress: B/MB, real periodic refresh, read-only usage, wheel-safe budget, uninterrupted drag and hidden-owner preservation\n";
}
void lesson_settings_actions_tests(){
    using namespace lgaed;using namespace presentation;state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");const auto before=data;
    const auto host=CreateWindowExW(0,L"STATIC",L"Lesson settings actions QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Create isolated lesson actions host");
    std::vector<std::size_t> pages;int top=0,bottom=0,imports=0,finish=0,writes=0;
    SettingsAccess access;access.scope=SettingsScope::lesson;access.read=[&]{return data;};access.update=[&](const auto& change){++writes;change(data);};
    access.lesson_pages={{0,"Página 1"},{1,"Página 2"},{3,"Página 4"}};access.current_page=1;access.navigate_page=[&](std::size_t page){pages.push_back(page);};access.page_top=[&]{++top;};access.page_bottom=[&]{++bottom;};access.import_state=[&]{++imports;};access.finish=[&]{++finish;};
    auto view=create_settings_view(host,access);MoveWindow(view,0,0,960,700,TRUE);const auto selector=GetDlgItem(view,5161);
    check(selector&&SendMessageW(selector,CB_GETCOUNT,0,0)==3&&SendMessageW(selector,CB_GETCURSEL,0,0)==1,"Lesson Settings did not preserve the host's authorized pages/current selection");
    wchar_t label[80]{};SendMessageW(selector,CB_GETLBTEXT,2,reinterpret_cast<LPARAM>(label));check(std::wstring_view(label)==L"Página 4","Lesson Settings invented an unavailable page or changed its identity");
    select(view,5161,2);for(const auto id:{5160,5162,5163,5164})SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,id)));
    check(pages==std::vector<std::size_t>{3}&&imports==1&&top==1&&bottom==1&&finish==0&&!IsWindowEnabled(GetDlgItem(view,5164))&&data==before&&writes==0,"Lesson action routing bypassed the finish gate or changed answers/preferences");
    const auto actions=child_with_caption(view,L"Acciones de la lección"),narration=child_with_caption(view,L"Narración");check(actions&&narration&&relative_rect(actions,view).top<relative_rect(narration,view).top,"Lesson navigation actions are not visible before narration preferences");
    DestroyWindow(view);access.finish_enabled=true;view=create_settings_view(host,access);SendMessageW(GetDlgItem(view,5164),BM_CLICK,0,0);check(finish==1&&writes==0&&data==before,"Enabled finish did not delegate exactly once to the real lesson owner");DestroyWindow(view);
    access.finish={};access.navigate_page={};access.lesson_pages.clear();view=create_settings_view(host,access);check(!GetDlgItem(view,5161)&&!GetDlgItem(view,5164),"Settings exposed actions the lesson owner did not provide");DestroyWindow(view);DestroyWindow(host);
    std::cout<<"PASS internal lesson actions: authorized page IDs, selected page, import/top/bottom delegation, finish gate/absence and unchanged drafts/preferences\n";
}
struct SaveFailureAction {HWND slider=nullptr,owner=nullptr;bool pointer_active=false,synchronous=false,captured=false,found=false;int dialogs=0;};
SaveFailureAction* save_failure_action=nullptr;
void CALLBACK save_failure_timer(HWND,UINT,UINT_PTR,DWORD){
    if(!save_failure_action)return;
    const auto dialog=internal_dialog_with_control(IDOK);
    if(!dialog)return;
    auto& action=*save_failure_action;action.found=true;++action.dialogs;
    action.synchronous=action.synchronous||action.pointer_active;action.captured=action.captured||GetCapture()==action.slider;
    if(action.dialogs==1)SendMessageW(GetParent(action.slider),WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,130),reinterpret_cast<LPARAM>(action.slider));
    // Exercise the same Escape dismissal that follows a failed live slider save.
    SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
}
void save_failure_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");appearance::save_global_text_scale(seed,100);
    const auto host=CreateWindowExW(0,L"STATIC",L"Settings save-failure QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host!=nullptr,"Cannot create the isolated save-failure host");
    SettingsAccess access;access.read=[&]{return seed;};int writes=0;
    access.update=[&](const auto&){++writes;throw std::runtime_error("El perfil cambio en otra instancia; vuelve a abrirlo antes de guardar.");};
    const auto view=create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);ShowWindow(host,SW_SHOW);SetForegroundWindow(host);pump();
    const auto slider=GetDlgItem(view,5120);reveal_control(view,slider);RECT channel{},thumb{};SendMessageW(slider,TBM_GETCHANNELRECT,0,reinterpret_cast<LPARAM>(&channel));SendMessageW(slider,TBM_GETTHUMBRECT,0,reinterpret_cast<LPARAM>(&thumb));
    SaveFailureAction action{slider,host};save_failure_action=&action;const auto timer=SetTimer(nullptr,0,20,save_failure_timer);check(timer!=0,"Cannot dismiss the genuine failed-save dialog");
    action.pointer_active=true;SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(channel.right,(thumb.top+thumb.bottom)/2));action.pointer_active=false;
    // Notifications queued during one failed gesture must share one notice.
    for(int value:{145,140,135}){SendMessageW(slider,TBM_SETPOS,TRUE,value);SendMessageW(view,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,value),reinterpret_cast<LPARAM>(slider));}
    SendMessageW(slider,WM_LBUTTONUP,0,MAKELPARAM(channel.right,(thumb.top+thumb.bottom)/2));pump();
    const bool alive=IsWindow(view)&&IsWindowEnabled(host);const auto shown_scale=SendMessageW(slider,TBM_GETPOS,0,0);
    KillTimer(nullptr,timer);save_failure_action=nullptr;DestroyWindow(view);DestroyWindow(host);
    std::cout<<"METRIC Settings save failure synchronous="<<action.synchronous<<" capture="<<action.captured<<" dialogs="<<action.dialogs<<" writes="<<writes<<'\n';
    check(action.found&&writes>0,"A failed slider save did not present its actual error");
    check(!action.synchronous,"A failed live save entered MessageBox while the native slider pointer callback was still active");
    check(!action.captured,"The failed-save dialog retained slider capture");
    check(action.dialogs==1,"One failed slider gesture presented duplicate error dialogs");
    check(writes==1,"Notifications during a pending/visible save error retried the same failed gesture");
    check(alive&&shown_scale==100,"Dismissing a failed save lost the Settings window or left the rejected scale displayed");
    check(appearance::global_text_scale(seed)==100,"A rejected save changed persisted preferences");
    std::cout<<"PASS Settings failed live save: deferred single error, released native capture, Escape dismissal and preserved preferences\n";
}
ComboMetrics combo_metrics(HWND combo,const char* phase){
    ComboMetrics value;RECT bounds{},client{};GetWindowRect(combo,&bounds);GetClientRect(combo,&client);
    COMBOBOXINFO info{sizeof(info)};check(GetComboBoxInfo(combo,&info)!=FALSE,"Cannot inspect native combo selection rectangle");
    value.selection=static_cast<int>(SendMessageW(combo,CB_GETITEMHEIGHT,static_cast<WPARAM>(-1),0));
    value.list=static_cast<int>(SendMessageW(combo,CB_GETITEMHEIGHT,0,0));value.height=bounds.bottom-bounds.top;
    value.client=client.bottom-client.top;value.item=info.rcItem.bottom-info.rcItem.top;value.dpi=static_cast<int>(GetDpiForWindow(combo));
    const auto font=reinterpret_cast<HFONT>(SendMessageW(combo,WM_GETFONT,0,0));LOGFONTW logical{};
    if(font&&GetObjectW(font,sizeof(logical),&logical))value.font_height=logical.lfHeight;
    std::cout<<"METRIC settings combo "<<phase<<": dpi="<<value.dpi<<" selection="<<value.selection<<" list="<<value.list
        <<" window="<<value.height<<" client="<<value.client<<" item="<<value.item<<" font="<<value.font_height<<'\n';
    return value;
}
class PixelCapture{
    HBITMAP bitmap_=nullptr;HGDIOBJ previous_=nullptr;std::uint32_t* pixels_=nullptr;int width_=0,height_=0;
public:
    HDC dc=nullptr;
    PixelCapture(HDC source,int width,int height):width_(width),height_(height){
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;dc=CreateCompatibleDC(source);bitmap_=CreateDIBSection(source,&info,DIB_RGB_COLORS,&pixels,nullptr,0);pixels_=static_cast<std::uint32_t*>(pixels);
        if(!dc||!bitmap_||!pixels_){if(bitmap_)DeleteObject(bitmap_);if(dc)DeleteDC(dc);throw std::runtime_error("Cannot create the exact Settings pixel snapshot");}
        previous_=SelectObject(dc,bitmap_);
    }
    ~PixelCapture(){SelectObject(dc,previous_);DeleteObject(bitmap_);DeleteDC(dc);}
    PixelCapture(const PixelCapture&)=delete;PixelCapture& operator=(const PixelCapture&)=delete;
    COLORREF pixel(int x,int y)const{if(x<0||x>=width_||y<0||y>=height_)return CLR_INVALID;const auto pixel=pixels_[static_cast<std::size_t>(y)*width_+x];return RGB((pixel>>16)&255,(pixel>>8)&255,pixel&255);}
};
bool checkbox_metrics(HWND checkbox,const char* phase,bool painted=false){
    RECT bounds{};GetClientRect(checkbox,&bounds);const auto dpi=static_cast<int>(GetDpiForWindow(checkbox));
    const auto screen=GetDC(checkbox);PixelCapture capture(screen,bounds.right,bounds.bottom);ReleaseDC(checkbox,screen);const auto dc=capture.dc;
    const auto background=lgaed::presentation::reader_theme::background(checkbox);const auto fill=CreateSolidBrush(background);FillRect(dc,&bounds,fill);DeleteObject(fill);
    if(painted){const auto rendered=GetDC(checkbox);BitBlt(dc,0,0,bounds.right,bounds.bottom,rendered,0,0,SRCCOPY);ReleaseDC(checkbox,rendered);}
    else SendMessageW(checkbox,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    // Complete all GDI drawing once, then inspect the same complete capture in
    // memory. Per-pixel GetPixel calls synchronize with the display driver and
    // make these unchanged full-resolution glyph assertions unnecessarily slow.
    GdiFlush();check(capture.pixel(0,0)==GetPixel(dc,0,0),"The Settings DIB snapshot does not match the native GDI pixel format");
    int min_x=bounds.right,min_y=bounds.bottom,max_x=-1,max_y=-1,pixels=0;
    const bool right=(GetWindowLongPtrW(checkbox,GWL_STYLE)&BS_RIGHTBUTTON)!=0;
    // The Android switch is36DIP wide and its thumb moves from right to left
    // when unchecked. Inspect the whole switch, not only the checked endpoint.
    const auto limit=std::min(static_cast<int>(bounds.right),MulDiv(right?42:16,dpi,96));
    const int glyph_left=right?std::max(0,static_cast<int>(bounds.right)-limit):0,glyph_right=right?static_cast<int>(bounds.right):limit;
    for(int y=0;y<bounds.bottom;++y)for(int x=glyph_left;x<glyph_right;++x){const auto color=capture.pixel(x,y);
        const int contrast=std::max({std::abs(static_cast<int>(GetRValue(color))-GetRValue(background)),
            std::abs(static_cast<int>(GetGValue(color))-GetGValue(background)),std::abs(static_cast<int>(GetBValue(color))-GetBValue(background))});
        if(color!=CLR_INVALID&&contrast>=60){++pixels;min_x=std::min(min_x,x);max_x=std::max(max_x,x);min_y=std::min(min_y,y);max_y=std::max(max_y,y);}}
    const auto glyph_width=max_x>=min_x?max_x-min_x+1:0,glyph_height=max_y>=min_y?max_y-min_y+1:0;
    std::cout<<"METRIC settings checkbox "<<phase<<": dpi="<<dpi<<" control="<<bounds.right<<'x'<<bounds.bottom
        <<" type="<<(GetWindowLongPtrW(checkbox,GWL_STYLE)&BS_TYPEMASK)<<" check="<<SendMessageW(checkbox,BM_GETCHECK,0,0)
        <<" glyph-region="<<glyph_width<<'x'<<glyph_height<<" legible-pixels="<<pixels<<'\n';
    return glyph_width>=MulDiv(8,dpi,96)&&glyph_height>=MulDiv(8,dpi,96);
}
void visual_metrics_tests(){
    ScopedFixture fixture(lgaed::presentation::SettingsScope::global);const auto combo=fixture.control(5101);
    const auto initial=combo_metrics(combo,"after-open");checkbox_metrics(fixture.control(5103),"hidden-print");
    const auto scroll_count=reveal_control(fixture.view,fixture.control(5103));
    const auto visible_checkbox=relative_rect(fixture.control(5103),fixture.view);RECT viewport{};GetClientRect(fixture.view,&viewport);
    check(visible_checkbox.top>=0&&visible_checkbox.bottom<=viewport.bottom,"The native wheel setup did not reveal the entire real narration checkbox");
    std::cout<<"METRIC visual_metrics setup_scroll_count="<<scroll_count<<'\n';
    SetWindowPos(fixture.host,HWND_TOP,0,0,1000,750,SWP_NOACTIVATE|SWP_SHOWWINDOW);pump();
    RedrawWindow(fixture.host,nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW|RDW_ALLCHILDREN);
    checkbox_metrics(fixture.control(5103),"visible-print");const bool checked_legible=checkbox_metrics(fixture.control(5103),"visible-painted",true);
    SendMessageW(fixture.control(5103),BM_SETCHECK,BST_UNCHECKED,0);InvalidateRect(fixture.control(5103),nullptr,TRUE);UpdateWindow(fixture.control(5103));
    const bool unchecked_legible=checkbox_metrics(fixture.control(5103),"unchecked-painted",true);
    MoveWindow(fixture.view,0,0,820,620,TRUE);pump();const auto resized=combo_metrics(combo,"after-resize");
    SCROLLINFO scroll_before{sizeof(scroll_before),SIF_ALL};check(GetScrollInfo(fixture.view,SB_VERT,&scroll_before)!=FALSE,"Settings has no real native scroll state");
    SCROLLBARINFO scrollbar{sizeof(scrollbar)};check(GetScrollBarInfo(fixture.view,OBJID_VSCROLL,&scrollbar)!=FALSE&&
        !(scrollbar.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)),"Settings regression did not expose the real vertical scrollbar");
    RedrawWindow(fixture.view,nullptr,nullptr,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW);pump();
    if(!lgaed::presentation::reader_theme::palette(fixture.view).high_contrast){
        RECT window_bounds{};GetWindowRect(fixture.view,&window_bounds);const auto actual=GetWindowDC(fixture.view);
        const auto track=GetPixel(actual,scrollbar.rcScrollBar.left-window_bounds.left+1,
            (scrollbar.rcScrollBar.top+scrollbar.rcScrollBar.bottom)/2-window_bounds.top);ReleaseDC(fixture.view,actual);
        check(track==lgaed::presentation::reader_theme::background(fixture.view),"The Settings scrollbar retained a white native strip after repaint");
    }
    SCROLLINFO scroll_after{sizeof(scroll_after),SIF_ALL};GetScrollInfo(fixture.view,SB_VERT,&scroll_after);
    check(scroll_before.nMin==scroll_after.nMin&&scroll_before.nMax==scroll_after.nMax&&scroll_before.nPage==scroll_after.nPage&&
        scroll_before.nPos==scroll_after.nPos,"Styling the Settings scrollbar changed its native position or range");
    const auto font=SendMessageW(combo,WM_GETFONT,0,0);SendMessageW(combo,WM_SETFONT,static_cast<WPARAM>(font),TRUE);pump();
    const auto font_reset=combo_metrics(combo,"after-font");
    const auto height=MulDiv(56,initial.dpi,96);
    const auto selected_result=SendMessageW(combo,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),height);
    combo_metrics(combo,"after-set-selection");
    const auto list_result=SendMessageW(combo,CB_SETITEMHEIGHT,0,MulDiv(36,initial.dpi,96));combo_metrics(combo,"after-set-list");
    MoveWindow(fixture.view,0,0,960,700,TRUE);pump();const auto relayout=combo_metrics(combo,"after-relayout");
    std::cout<<"METRIC native CB_SETITEMHEIGHT returns: selection="<<selected_result<<" list="<<list_result<<'\n';
    check(initial.selection>=height&&initial.height>=height,"Settings combo opened below its required 56 DIP selection height");
    check(resized.selection>=height&&resized.height>=height,"Settings combo lost its required selection height during resize");
    check(font_reset.selection>=height&&font_reset.height>=height,"Settings combo lost its required selection height when its font was reapplied");
    check(relayout.selection>=height&&relayout.height>=height,"Settings combo lost its required selection height on subsequent layout");
    check(checked_legible&&unchecked_legible,"Settings checkbox glyph is smaller than 8 DIP at the actual monitor DPI");
    std::cout<<"PASS native settings combo geometry survives initial layout, resize and font changes\n";
}
void attempts_and_owned_settings_tests(){
    using namespace lgaed;using namespace presentation;
    constexpr std::int64_t now=1800000000000LL;
    state::StateData data;data.preferences["learning_progress"]["answer"]=std::string("preserved");
    data.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    const auto protected_progress=data.preferences["learning_progress"],protected_draft=data.preferences["cpp_lesson_drafts"];
    attempts::initialize(data,"programming",now);auto current=attempts::balance(data,"programming",now);
    check(current.available==8&&current.maximum==8&&current.progress==1.&&!current.unlimited,"The Windows free balance differs from Android");
    current=attempts::consume_incorrect(data,"programming",now);check(current.available==7&&current.reserve==7.,"An incorrect response did not consume one shared reserve unit");
    check(attempts::balance(data,"programming",now+attempts::recovery_millis/2).reserve==7.5,"The existing hourly reserve did not recover fractionally");
    check(attempts::recovery_label(8,7.5,false)=="30m"&&attempts::recovery_label(8,6.25,false)=="1h 45m"&&attempts::recovery_label(8,8,false).empty()&&attempts::recovery_label(8,0,true).empty(),"The header recovery label does not match Android’s full-reserve countdown");
    check(attempts::balance(data,"programming",now+attempts::recovery_millis).available==8,"Hourly reserve recovery did not cap at its route maximum");
    check(attempts::balance(data,"math_physics",now).maximum==12&&attempts::balance(data,"math_physics",now).available==12,"Programming changed the Mathematics reserve");
    check(attempts::balance(data,"programming",now-1000).available==7,"A backwards wall-clock jump consumed reserve");
    auto legacy=data;legacy.preferences["attempt_reserve_seconds_test"]["reserve_programming"]=2.;legacy.preferences["attempt_reserve_seconds_test"].erase("recovery_interval_programming");
    legacy.preferences["attempt_reserve_seconds_test"]["updated_at_programming"]=now-30000;
    attempts::initialize(legacy,"programming",now);check(attempts::balance(legacy,"programming",now).reserve==2.5,"The old one-minute Android balance was not converted exactly once");
    const auto migrated=legacy;attempts::initialize(legacy,"programming",now+50000);check(legacy==migrated,"The legacy reserve was converted again");
    data.preferences["premium_state"]["state"]=std::string("PREMIUM");const auto premium_before=data;
    current=attempts::consume_incorrect(data,"programming",now);check(current.unlimited&&data==premium_before,"Premium consumed a reserve or modified existing data");
    check(data.preferences["learning_progress"]==protected_progress&&data.preferences["cpp_lesson_drafts"]==protected_draft,"Reserve updates altered student answers or whitespace");
    {
        ScopedFixture premium(SettingsScope::global,data);premium.has(5138,true);check(!IsWindowEnabled(premium.control(5138)),"Premium status is a purchase/activation control");
        const auto control=premium.control(5138);MoveWindow(control,0,0,420,80,TRUE);RECT bounds{};GetClientRect(control,&bounds);
        const auto screen=GetDC(control),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);ReleaseDC(control,screen);const auto old=SelectObject(dc,bitmap);
        DRAWITEMSTRUCT item{};item.CtlType=ODT_BUTTON;item.CtlID=5138;item.hwndItem=control;item.hDC=dc;item.rcItem=bounds;item.itemState=ODS_DISABLED;
        SendMessageW(premium.view,WM_DRAWITEM,5138,reinterpret_cast<LPARAM>(&item));
        const bool gradient=GetPixel(dc,60,20)!=GetPixel(dc,350,20);
        SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);check(gradient,"Active Premium did not retain Android's real gold gradient");
    }
    data.preferences["premium_state"]["state"]=std::string("FREE");
    {ScopedFixture free(SettingsScope::global,data);free.has(5138,true);check(IsWindowEnabled(free.control(5138)),"Free version lost the Android Premium entry");}
    const auto host=CreateWindowExW(0,L"STATIC",L"Owned settings QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create owned-settings test host");
    int closed=0;SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& mutation){mutation(data);};access.close=[&]{++closed;};
    const auto before=data;const auto frame=create_settings_window(host,std::move(access));
    check(GetParent(frame)==host&&(GetWindowLongPtrW(frame,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(frame,GWL_STYLE)&WS_POPUP),"Settings created a separate top-level application window");
    RECT outer{};GetClientRect(host,&outer);auto inner=relative_rect(frame,host);
    check(EqualRect(&outer,&inner),"The compatibility factory did not fill the main client area");
    MoveWindow(host,0,0,720,580,TRUE);GetClientRect(host,&outer);MoveWindow(frame,0,0,outer.right,outer.bottom,TRUE);pump();inner=relative_rect(frame,host);
    check(EqualRect(&outer,&inner),"The internal settings page cannot fit the resized main client area");
    SendMessageW(frame,WM_CLOSE,0,0);pump();check(closed==1&&IsWindow(host)&&data==before,"Closing settings destroyed its lesson owner or changed stored data");
    DestroyWindow(frame);DestroyWindow(host);
    std::cout<<"PASS Windows internal settings: main-window parent, exact client bounds/close, shared hourly reserves, legacy migration, route separation and Premium gold without purchases\n";
}
void owned_settings_escape_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    seed.preferences["learning_progress"]["answer"]=std::string("preserved answer");
    seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    const auto host=CreateWindowExW(0,L"STATIC",L"Settings Escape QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host!=nullptr,"Cannot create the Settings Escape owner");
    ShowWindow(host,SW_SHOW);SetForegroundWindow(host);pump();
    const std::array<const char*,3> locales{"es","en","fr"};const std::array<const wchar_t*,3> titles{L"Opciones",L"Settings",L"Paramètres"};
    const std::array<int,3> focus_ids{5100,5101,0};
    for(std::size_t index=0;index<locales.size();++index){const auto focus_id=focus_ids[index];
        auto data=seed;data.preferences["native_windows"]["ui_language"]=std::string(locales[index]);int closed=0,updates=0;
        SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& mutation){++updates;mutation(data);};access.close=[&]{++closed;};
        const auto frame=create_settings_window(host,std::move(access));
        const auto view=frame;
        check(view!=nullptr,"The Settings Escape window omitted its shared content");
        check(native_text(frame)==titles[index],"Opening owned Settings used a different EN/FR title until the language was changed");
        // Persisted choices remain saved; buffered appearance edits retain the same close policy as Back.
        select(view,5101,2);pump();const auto before_close=data;const int saved_updates=updates;
        const auto protection=GetDlgItem(view,5128);SendMessageW(protection,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(5128,BN_CLICKED),reinterpret_cast<LPARAM>(protection));
        check(IsWindowEnabled(GetDlgItem(view,5123))&&data==before_close,"Settings Escape setup did not keep its appearance edit buffered");
        if(focus_id){
            const auto control=GetDlgItem(view,focus_id);check(control!=nullptr,"Settings Escape omitted its real focus target");
            SetActiveWindow(host);SetFocus(control);check(GetFocus()==control,"Settings Escape could not focus the real button or combo");
            MSG key{};key.hwnd=control;key.message=WM_KEYDOWN;key.wParam=VK_ESCAPE;key.lParam=1;
            check(IsDialogMessageW(frame,&key)!=FALSE,"The owned Settings frame did not process Escape through native dialog navigation");
        }else SendMessageW(frame,WM_KEYDOWN,VK_ESCAPE,1);
        pump();
        check(closed==1&&IsWindow(host)&&data==before_close&&updates==saved_updates,
            "Escape did not reuse Settings close exactly once or altered saved choices, buffered appearance, answers or whitespace");
        DestroyWindow(frame);
    }
    DestroyWindow(host);
    std::cout<<"PASS owned Settings Escape: real button/combo MSG + IsDialogMessage and direct key reuse Close without saving buffered edits or changing student data\n";
}
struct LanguageAction {int selected=0,choose=-1;bool found=false;std::wstring title,automatic;std::exception_ptr error;};
LanguageAction* language_action=nullptr;
void CALLBACK language_timer(HWND,UINT,UINT_PTR timer,DWORD){
    const auto dialog=internal_dialog_with_control(8349);
    if(!dialog||!language_action)return;KillTimer(nullptr,timer);auto& action=*language_action;action.found=true;
    try{
        check((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_POPUP),"The interface language chooser opened a separate application window");
        check(native_text(dialog)==action.title,"The header language chooser title does not match the selected UI language");
        const auto viewport=GetDlgItem(dialog,8349);check(viewport!=nullptr,"The reusable language chooser omitted its viewport");
        check(native_text(GetDlgItem(viewport,8350))==action.automatic,"The header omitted Android's automatic device-language choice");
        for(int index=0;index<4;++index){const auto control=GetDlgItem(viewport,8350+index);check(control!=nullptr,"The header language chooser omitted one of its original choices");
            check((SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED)==(index==action.selected),"The header language chooser changed or lost the saved selection");}
        check(GetDlgItem(viewport,8354)==nullptr,"The header language chooser invented another curriculum language");
        if(action.choose>=0)SendMessageW(GetDlgItem(viewport,8350+action.choose),BM_CLICK,0,0);else SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    }catch(...){action.error=std::current_exception();SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void header_language_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData data;data.preferences["native_windows"]["content_language"]=std::string("fr");data.preferences["lesson_narration_settings"]["language"]=std::string("en-GB");
    data.preferences["lesson_narration_settings"]["kokoro_voice:en-GB"]=std::string("bm_george");data.preferences["learning_progress"]["answer"]=std::string("unchanged");
    const auto host=CreateWindowExW(0,L"STATIC",L"Header language QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create header language QA host");
    ShowWindow(host,SW_SHOW);SetForegroundWindow(host);pump();
    const std::array<std::string,4> tags{"auto","es","en","fr"};
    for(int selection=1;selection<4;++selection){
        data.preferences["native_windows"]["ui_language"]=tags[selection];const auto before=data;
        LanguageAction action;action.selected=selection;action.choose=0;
        action.title=selection==1?L"Idioma de la aplicación":selection==2?L"App Language":L"Langue de l’application";
        action.automatic=selection==1?L"Automático (idioma del dispositivo)":selection==2?L"Automatic (device language)":L"Automatique (langue de l’appareil)";
        language_action=&action;const auto timer=SetTimer(nullptr,0,20,language_timer);check(timer!=0,"Cannot schedule header language QA");
        const auto result=choose_interface_language(host,data);KillTimer(nullptr,timer);language_action=nullptr;if(action.error)std::rethrow_exception(action.error);
        check(action.found&&result=="auto"&&data==before,"Selecting app language modified curriculum, voice, answers or another setting");
        action.choose=-1;action.found=false;language_action=&action;const auto cancel_timer=SetTimer(nullptr,0,20,language_timer);check(cancel_timer!=0,"Cannot schedule cancellation QA");
        const auto cancelled=choose_interface_language(host,data);KillTimer(nullptr,cancel_timer);language_action=nullptr;if(action.error)std::rethrow_exception(action.error);
        check(action.found&&!cancelled&&data==before,"Cancelling the header language chooser changed the saved language");
    }
    DestroyWindow(host);std::cout<<"PASS Android-compatible header language chooser: automatic/es/en/fr, ES/EN/FR titles, exact selection, Cancel and independent narration/content\n";
}
struct NarrationChoiceAction{int selected=0,choose=-1,count=4;bool found=false,revealed=false;std::wstring title;std::vector<std::wstring> labels;std::exception_ptr error;};
NarrationChoiceAction* narration_choice_action=nullptr;
void CALLBACK narration_choice_timer(HWND,UINT,UINT_PTR timer,DWORD){
    const auto dialog=internal_dialog_with_control(8349);
    if(!dialog||!narration_choice_action)return;KillTimer(nullptr,timer);auto& action=*narration_choice_action;action.found=true;
    try{check((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_CHILD)&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_POPUP),"The narration chooser opened a separate application window");
        check(native_text(dialog)==action.title,"Narration selector dialog omitted its Android title");const auto viewport=GetDlgItem(dialog,8349);
        for(int index=0;index<action.count;++index){const auto choice=GetDlgItem(viewport,8350+index);check(choice!=nullptr,"Narration chooser omitted a valid original voice or language");
            if(!action.labels.empty())check(static_cast<std::size_t>(index)<action.labels.size()&&native_text(choice)==action.labels[index],"The narration chooser changed an original voice name or stable identifier");
            check((SendMessageW(choice,BM_GETCHECK,0,0)==BST_CHECKED)==(index==action.selected),"Narration chooser lost the effective saved choice");}
        check(!GetDlgItem(viewport,8350+action.count),"Narration chooser added a fabricated voice or language");
        const auto last=GetDlgItem(viewport,8350+action.count-1);SetFocus(last);const auto bounds=relative_rect(last,viewport);RECT area{};GetClientRect(viewport,&area);
        std::cout<<"METRIC narration chooser count="<<action.count<<" selected="<<action.selected<<" choose="<<action.choose
            <<" alive="<<IsWindow(dialog)<<" focus="<<GetDlgCtrlID(GetFocus())<<" last="<<GetDlgCtrlID(last)
            <<" row="<<bounds.top<<','<<bounds.bottom<<" viewport="<<area.bottom<<" dpi="<<GetDpiForWindow(viewport)<<'\n';
        check(IsWindow(dialog)&&(action.selected<0||SendMessageW(GetDlgItem(viewport,8350+action.selected),BM_GETCHECK,0,0)==BST_CHECKED),
            "Giving a narration choice keyboard focus committed a false selection or closed the dialog");
        action.revealed=GetFocus()==last&&bounds.top>=0&&bounds.bottom<=area.bottom;
        if(action.count>8){SCROLLINFO scroll{sizeof(scroll),SIF_ALL};check(GetScrollInfo(viewport,SB_VERT,&scroll)&&scroll.nMax>=static_cast<int>(scroll.nPage),"Long voice list has no real scrolling range");}
        if(action.choose<0)SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);else SendMessageW(GetDlgItem(viewport,8350+action.choose),BM_CLICK,0,0);
    }catch(...){action.error=std::current_exception();SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void open_narration_choice(HWND view,int id,NarrationChoiceAction& action){
    const auto root=GetAncestor(view,GA_ROOT);ShowWindow(root,SW_SHOW);SetForegroundWindow(root);pump();
    narration_choice_action=&action;const auto timer=SetTimer(nullptr,0,20,narration_choice_timer);check(timer!=0,"Cannot schedule actual narration selector QA");
    const auto button=GetDlgItem(view,id);wchar_t kind[32]{};GetClassNameW(button,kind,32);check(button&&IsWindowEnabled(button)&&_wcsicmp(kind,L"BUTTON")==0,"Narration selector is not an enabled real action button");
    SendMessageW(button,BM_CLICK,0,0);pump();KillTimer(nullptr,timer);narration_choice_action=nullptr;if(action.error)std::rethrow_exception(action.error);
    check(action.found&&action.revealed,"Narration selection failed to open or reveal its last keyboard-focused option");
}
void open_narration_choice(ScopedFixture& fixture,int id,NarrationChoiceAction& action){open_narration_choice(fixture.view,id,action);}
void unavailable_system_voice_tests(){
    using namespace lgaed;using namespace presentation;
    const auto installed=windows::system_voices();std::string language;std::vector<windows::SystemVoice> matching;
    for(const auto* tag:{"es","en-US","en-GB","fr"}){for(const auto& voice:installed)if(windows::system_voice_matches(voice.language,tag))matching.push_back(voice);if(!matching.empty()){language=tag;break;}}
    if(matching.empty()){std::cout<<"SKIP unavailable-system-voice repair: no installed SAPI token for a supported speech language; no voice installed by test.\n";return;}
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["lesson_narration_settings"]["engine"]=std::string("WINDOWS");
    seed.preferences["lesson_narration_settings"]["language"]=language;seed.preferences["lesson_narration_settings"]["windows_voice:"+language]=std::string("Missing Windows token from isolated QA");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    ScopedFixture fixture(SettingsScope::global,seed);
    check(native_text(fixture.control(5106)).find(L"Voz seleccionada no disponible")!=std::wstring::npos&&IsWindowEnabled(fixture.control(5106))&&fixture.data==seed&&fixture.updates==0,
        "An unavailable stored Windows voice was silently changed or falsely displayed as an installed selected voice");
    NarrationChoiceAction select;select.title=L"Voces de Windows";select.count=static_cast<int>(matching.size());select.selected=-1;for(const auto& voice:matching)select.labels.push_back(winrt::to_hstring(voice.name).c_str());
    open_narration_choice(fixture,5106,select);check(fixture.data==seed&&fixture.updates==0&&native_text(fixture.control(5106)).find(L"Voz seleccionada no disponible")!=std::wstring::npos,"Cancelling voice repair changed the unavailable saved Windows token");
    select.choose=0;select.found=false;select.revealed=false;
    open_narration_choice(fixture,5106,select);auto expected=seed;expected.preferences["lesson_narration_settings"]["windows_voice:"+language]=matching.front().id;
    check(fixture.data==expected&&fixture.updates==1&&native_text(fixture.control(5106)).find(winrt::to_hstring(matching.front().name).c_str())!=std::wstring::npos,
        "Choosing the first real Windows voice failed to repair the unavailable selection explicitly or altered unrelated preferences/drafts");
    ScopedFixture reopened(SettingsScope::global,fixture.data);check(reopened.data==expected&&reopened.updates==0&&native_text(reopened.control(5106))==native_text(fixture.control(5106)),"Reopening Settings lost the explicitly repaired Windows voice");
    std::cout<<"PASS unavailable stored Windows voice: honest status, no automatic mutation, no false radio selection, explicit first-token repair and preserved reopen/drafts\n";
}
void engine_storage_tests(){
    using namespace lgaed;using namespace presentation;
    constexpr auto gib=1024ull*1024*1024,mib=1024ull*1024;
    state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["lesson_narration_settings"]["engine"]=std::string("KOKORO");
    data.preferences["lesson_narration_settings"]["offline_audio_budget_bytes"]=static_cast<std::int64_t>(6*gib);data.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");const auto saved=data;
    const auto host=CreateWindowExW(0,L"STATIC",L"Engine storage QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Create isolated engine/storage fixture");
    int writes=0,changes=0;std::uint64_t kokoro_max=8*gib,kokoro_stored=128*mib;
    SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){++writes;change(data);};access.changed=[&]{++changes;};
    access.storage_snapshot=[&]{const bool system=state::string_value(data,"lesson_narration_settings","engine")=="WINDOWS";
        windows::NarrationStorageSnapshot snapshot;snapshot.stored_bytes=system?512*mib:kokoro_stored;snapshot.maximum_bytes=system?2*gib:kokoro_max;
        snapshot.configured_bytes=static_cast<std::uint64_t>(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes"));snapshot.effective_bytes=std::min(snapshot.configured_bytes,snapshot.maximum_bytes);return snapshot;};
    const auto view=create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,800,TRUE);ShowWindow(host,SW_SHOW);SetForegroundWindow(host);pump();
    const auto slider=GetDlgItem(view,5144),rgb=GetDlgItem(view,5720);SetWindowTextW(rgb,L"123");
    check(SendMessageW(slider,TBM_GETRANGEMAX,0,0)==8192&&SendMessageW(slider,TBM_GETPOS,0,0)==6144,"Kokoro snapshot did not establish its own initial slider range/effective limit");
    reveal_control(view,GetDlgItem(view,5104));SCROLLINFO scroll{sizeof(scroll),SIF_POS};GetScrollInfo(view,SB_VERT,&scroll);
    NarrationChoiceAction system;system.title=L"Motor de voz";system.count=2;system.choose=1;open_narration_choice(view,5104,system);
    auto expected=saved;expected.preferences["lesson_narration_settings"]["engine"]=std::string("WINDOWS");SCROLLINFO after{sizeof(after),SIF_POS};GetScrollInfo(view,SB_VERT,&after);
    check(SendMessageW(slider,TBM_GETRANGEMAX,0,0)==2048&&SendMessageW(slider,TBM_GETPOS,0,0)==2048&&native_text(GetDlgItem(view,5145)).find(L"Límite por motor: 2,00 GB")!=std::wstring::npos&&native_text(GetDlgItem(view,5145)).find(L"512,00 MB")!=std::wstring::npos,
        "An explicit switch to Windows inherited Kokoro's slider capacity/effective limit or saved-byte count");
    check(data==expected&&writes==1&&changes==1&&GetDlgItem(view,5144)==slider&&GetDlgItem(view,5720)==rgb&&native_text(rgb)==L"123"&&IsWindowEnabled(GetDlgItem(view,5123))&&after.nPos==scroll.nPos,
        "Engine snapshot binding rewrote the shared quota, RGB draft, scroll, controls or student data");
    NarrationChoiceAction neural;neural.title=L"Motor de voz";neural.count=2;neural.selected=1;neural.choose=0;open_narration_choice(view,5104,neural);GetScrollInfo(view,SB_VERT,&after);
    check(SendMessageW(slider,TBM_GETRANGEMAX,0,0)==8192&&SendMessageW(slider,TBM_GETPOS,0,0)==6144&&native_text(GetDlgItem(view,5145)).find(L"Límite por motor: 6,00 GB")!=std::wstring::npos&&data==saved&&writes==2&&changes==2&&after.nPos==scroll.nPos&&native_text(rgb)==L"123",
        "Returning to Kokoro failed to restore its capacity while preserving the configured quota/draft/scroll");
    kokoro_max=10*gib;kokoro_stored=256*mib;const auto deadline=GetTickCount64()+2500;
    do{pump();if(native_text(GetDlgItem(view,5145)).find(L"256,00 MB")!=std::wstring::npos)break;MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(GetTickCount64()<deadline);
    check(native_text(GetDlgItem(view,5145)).find(L"256,00 MB")!=std::wstring::npos&&SendMessageW(slider,TBM_GETRANGEMAX,0,0)==8192&&SendMessageW(slider,TBM_GETPOS,0,0)==6144&&data==saved&&writes==2&&changes==2,
        "Periodic usage refresh rebound the slider or persisted a quota after the explicit engine selection ended");
    DestroyWindow(view);DestroyWindow(host);std::cout<<"PASS explicit engine storage binding: distinct Windows/Kokoro capacities, effective limits, shared saved quota, unchanged RGB draft/scroll/HWNDs and stationary periodic thumb\n";
}
void scoped_options_parity_tests(){
    using namespace lgaed;using namespace presentation;
    const std::array<const char*,3> locales{"es","en","fr"};
    const std::array<const wchar_t*,3> descriptions{
        L"Si está activado, este curso utiliza automáticamente los ajustes generales de la aplicación.",L"When enabled, this course automatically uses the general app settings.",L"Lorsque cette option est activée, ce cours utilise automatiquement les paramètres généraux de l’application."};
    const std::array<const wchar_t*,3> course_reset{L"Elimina el progreso y las respuestas de todas las unidades y lecciones de este curso.",L"Deletes progress and answers from all units and lessons in this course.",L"Supprime la progression et les réponses de toutes les unités et leçons de ce cours."};
    const std::array<const wchar_t*,3> unit_reset{L"Elimina el progreso y las respuestas de todas las lecciones pertenecientes a esta unidad.",L"Deletes progress and answers from all lessons in this unit.",L"Supprime la progression et les réponses de toutes les leçons de cette unité."};
    const std::array<const wchar_t*,3> lesson_reset{L"Elimina las respuestas, ejercicios completados, página guardada y estado de finalización de esta lección.",L"Deletes answers, completed exercises, the saved page, and completion status for this lesson.",L"Supprime les réponses, les exercices terminés, la page enregistrée et l’état d’achèvement de cette leçon."};
    const std::array<const wchar_t*,3> lesson_languages{L"Idioma: Español",L"Language: Spanish",L"Langue : Espagnol"};
    for(std::size_t locale=0;locale<locales.size();++locale){state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string(locales[locale]);
        seed.preferences["lesson_narration_settings"]["language"]=std::string("es");seed.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_santa");
        seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
        {ScopedFixture course(SettingsScope::course,seed);check(child_with_caption(course.view,descriptions[locale])&&child_with_caption(course.view,course_reset[locale]),"Course settings omitted its Android inheritance or reset explanation");
            const auto use=relative_rect(course.control(5116),course.view),description=relative_rect(child_with_caption(course.view,descriptions[locale]),course.view),next=relative_rect(course.control(5110),course.view);
            check(use.bottom<=description.top&&description.bottom<=next.top,"Course inheritance explanation is not between the master and continuation switches");check(course.data==seed&&!course.control(5117),"Opening course settings wrote preferences or reintroduced deferred flow Save");}
        {ScopedFixture unit(SettingsScope::unit,seed);check(child_with_caption(unit.view,unit_reset[locale])!=nullptr,"Unit settings uses a generic reset explanation");}
        {ScopedFixture lesson(SettingsScope::lesson,seed);check(child_with_caption(lesson.view,lesson_reset[locale])!=nullptr,"Lesson settings uses a generic reset explanation");
            wchar_t kind[32]{};GetClassNameW(lesson.control(5105),kind,32);check(_wcsicmp(kind,L"BUTTON")==0&&native_text(lesson.control(5105))==lesson_languages[locale],"Lesson speech language is still a separate label and combo");
            check(native_text(lesson.control(5106)).find(L"em_santa")!=std::wstring::npos&&GetWindowTextLengthW(lesson.control(5147))>0,"Lesson voice lost its stable ID or global configuration hint");
            LONG bottom=-1;for(const int id:{5122,5146,5143,5120,5121}){const auto bounds=relative_rect(lesson.control(id),lesson.view);check(bounds.top>=bottom,"Lesson size card differs from Android switch/separator/value/slider/preview order");bottom=bounds.bottom;}
            const auto separator=relative_rect(lesson.control(5146),lesson.view);check(separator.bottom-separator.top==MulDiv(1,GetDpiForWindow(lesson.view),96),"Lesson size separator is not one DIP");
            check((GetWindowLongPtrW(lesson.control(5152),GWL_STYLE)&SS_TYPEMASK)==SS_CENTER&&native_text(lesson.control(5152))==(locale==1?L"75%                                  150%":L"75 %                                  150 %"),"Lesson text-size range lost Android's centered original spacing and locale");
            const auto palette=reader_theme::palette(lesson.view);const auto card=palette.high_contrast?palette.card:RGB(32,33,38);
            check(reader_theme::background(lesson.control(5122))==card&&reader_theme::background(lesson.control(5143))==card,"Course size controls do not share one Android card");check(lesson.data==seed,"Opening lesson settings changed voices, answers or intentional whitespace");}
    }
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["native_windows"]["content_language"]=std::string("fr");seed.preferences["lesson_narration_settings"]["language"]=std::string("es");
    seed.preferences["lesson_narration_settings"]["kokoro_voice:es"]=std::string("em_santa");seed.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_santa");seed.preferences["lesson_narration_settings"]["rate"]=1.5;
    seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");ScopedFixture lesson(SettingsScope::lesson,seed);
    NarrationChoiceAction cancel;cancel.title=L"Idioma de lectura";open_narration_choice(lesson,5105,cancel);check(lesson.data==seed,"Cancelling a lesson language selection changed preferences");
    NarrationChoiceAction english;english.title=L"Idioma de lectura";english.choose=1;open_narration_choice(lesson,5105,english);
    check(state::string_value(lesson.data,"lesson_narration_settings","language")=="en-US"&&native_text(lesson.control(5106)).find(L"am_santa")!=std::wstring::npos,"Lesson language did not retain the selected voice belonging to the new language");
    NarrationChoiceAction voice;voice.title=L"Voz instalada";voice.count=20;voice.selected=19;voice.choose=18;open_narration_choice(lesson,5106,voice);
    check(state::string_value(lesson.data,"lesson_narration_settings","kokoro_voice:en-US")=="am_puck","The scrollable voice dialog did not save the chosen original voice ID");
    NarrationChoiceAction spanish;spanish.title=L"Idioma de lectura";spanish.selected=1;spanish.choose=0;open_narration_choice(lesson,5105,spanish);
    check(state::string_value(lesson.data,"lesson_narration_settings","kokoro_voice:es")=="em_santa"&&native_text(lesson.control(5106)).find(L"em_santa")!=std::wstring::npos,"Switching speech languages replaced another language’s voice");
    auto expected=seed;expected.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_puck");expected.preferences["lesson_narration_settings"]["kokoro_voice"]=std::string("am_puck");
    check(lesson.data==expected,"Lesson selectors changed UI/content language, rate, drafts or unrelated preferences");
    std::cout<<"PASS scoped Android Options parity: complete ES/EN/FR explanations, single size card, real modal speech selectors, scrolling keyboard focus, cancellation and retained voice/dialect/state\n";
}
void profile_location_tests(){
    using namespace lgaed;using namespace presentation;
    const std::filesystem::path roaming=L"Q:\\QA sin datos\\Usuaria Ángela con nombre largo\\AppData\\Roaming\\LearnGameAndEngineDev";
    const std::filesystem::path local=L"Q:\\QA sin datos\\Usuaria Ángela con nombre largo\\AppData\\Local\\LearnGameAndEngineDev\\Codex\\qa\\respaldo-aislado";
    for(const auto* locale:{"es","en","fr"})for(const bool qa:{false,true}){
        const auto root=qa?local:roaming/(std::string_view(locale)=="en"?L"Work":L"Codex");
        state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string(locale);
        data.preferences["learning_progress"]["respuesta_conservada"]=std::string("{\"correct\":true,\"opaque\":7}");
        data.preferences["cpp_lesson_drafts"]["borrador_conservado"]=std::string("int main(){return 7;}\n");
        const auto original=data;int updates=0,changes=0;std::vector<std::wstring> copied;
        const auto host=CreateWindowExW(0,L"STATIC",L"Isolated profile-location QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the profile-location QA host");
        struct DestroyHost{HWND value;~DestroyHost(){if(IsWindow(value))DestroyWindow(value);}} destroy{host};
        SettingsAccess access;access.profile_root=root;access.qa_profile=qa;
        access.read=[&]{return data;};access.update=[&](const auto& update){++updates;update(data);};access.changed=[&]{++changes;};
        access.copy_text=[&](std::wstring_view text){copied.emplace_back(text);};
        const auto view=create_settings_view(host,access);MoveWindow(view,0,0,960,700,TRUE);pump();
        const auto location=GetDlgItem(view,5165),copy=GetDlgItem(view,5166),instructions=GetDlgItem(view,5168);
        check(location&&copy&&instructions&&native_text(location)==root.native(),"Global options did not display the exact environment profile path");
        check((GetWindowLongPtrW(location,GWL_STYLE)&ES_READONLY)&&(GetWindowLongPtrW(location,GWL_STYLE)&ES_MULTILINE),"The profile location must be selectable, read-only and wrapped");
        const auto guidance=native_text(instructions),kind=native_text(GetDlgItem(view,5169));
        if(qa)check(!GetDlgItem(view,5167)&&kind.find(L"QA")!=std::wstring::npos&&guidance.find(L"Roaming")==std::wstring::npos,
            "An isolated Local QA profile was presented as normal Roaming progress or a product-wide backup");
        else check(native_text(GetDlgItem(view,5167))==root.parent_path().native()&&kind.find(L"QA")==std::wstring::npos
            &&guidance.find(L"Work")!=std::wstring::npos&&guidance.find(L"Codex")!=std::wstring::npos
            &&guidance.find(L"AppData\\Roaming")!=std::wstring::npos,
            "Normal-profile backup guidance lost the complete product folder, both instances or the original Roaming restoration location");
        check(data==original&&updates==0&&changes==0&&copied.empty(),"Opening the progress-location section changed saved state or copied text without a click");
        SendMessageW(copy,BM_CLICK,0,0);pump();
        check(copied==std::vector<std::wstring>{root.native()}&&data==original&&updates==0&&changes==0,
            "Copy path did not copy the exact Unicode environment path once or changed progress/drafts/settings");
        MoveWindow(view,0,0,360,620,TRUE);pump();
        RECT bounds{},client{};GetWindowRect(location,&bounds);MapWindowPoints(nullptr,view,reinterpret_cast<POINT*>(&bounds),2);GetClientRect(view,&client);
        check(bounds.left>=0&&bounds.right<=client.right&&bounds.bottom>bounds.top&&SendMessageW(location,EM_GETLINECOUNT,0,0)>1,
            "A long Unicode profile path escaped or failed to wrap in a narrow settings page");
        RECT formatting{};SendMessageW(location,EM_GETRECT,0,reinterpret_cast<LPARAM>(&formatting));
        const auto last=SendMessageW(location,EM_POSFROMCHAR,GetWindowTextLengthW(location)-1,0);
        const auto path_dc=GetDC(location);const auto previous_font=SelectObject(path_dc,reinterpret_cast<HFONT>(SendMessageW(location,WM_GETFONT,0,0)));TEXTMETRICW path_metrics{};
        check(GetTextMetricsW(path_dc,&path_metrics)!=FALSE,"Cannot inspect the wrapped profile path's actual font");SelectObject(path_dc,previous_font);ReleaseDC(location,path_dc);
        const auto last_top=static_cast<short>(HIWORD(last));
        const bool last_visible=last!=-1&&last_top>=formatting.top&&last_top+path_metrics.tmHeight<=formatting.bottom;
        if(!last_visible)std::cerr<<"PROFILE PATH layout: locale="<<locale<<" qa="<<qa<<" lines="<<SendMessageW(location,EM_GETLINECOUNT,0,0)
            <<" first_visible="<<SendMessageW(location,EM_GETFIRSTVISIBLELINE,0,0)<<" width="<<bounds.right-bounds.left<<" height="<<bounds.bottom-bounds.top
            <<" format="<<formatting.top<<','<<formatting.bottom<<" last_y="<<last_top<<" glyph_height="<<path_metrics.tmHeight<<'\n';
        check(last_visible,
            "The profile path's final wrapped line is clipped in a narrow settings page");
        DestroyWindow(view);
        const auto reopened=create_settings_view(host,access);MoveWindow(reopened,0,0,960,700,TRUE);
        check(native_text(GetDlgItem(reopened,5165))==root.native()&&data==original&&updates==0&&changes==0,
            "Reopening profile-location options changed the environment path or saved data");
    }
    for(const auto scope:{SettingsScope::course,SettingsScope::unit,SettingsScope::lesson,SettingsScope::audio,SettingsScope::global}){
        state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");int copied=0,updates=0;
        const auto host=CreateWindowExW(0,L"STATIC",L"Scoped profile-location QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create the scoped profile-location QA host");
        struct DestroyHost{HWND value;~DestroyHost(){if(IsWindow(value))DestroyWindow(value);}} destroy{host};
        SettingsAccess access;access.scope=scope;access.course_id="qa_course";access.route_id="programming";
        access.profile_root=scope==SettingsScope::global?std::filesystem::path(L"relative-profile"):roaming/L"Work";
        access.read=[&]{return data;};access.update=[&](const auto& update){++updates;update(data);};access.copy_text=[&](auto){++copied;};
        const auto view=create_settings_view(host,access);MoveWindow(view,0,0,960,700,TRUE);
        check(!GetDlgItem(view,5165)&&!GetDlgItem(view,5166)&&!GetDlgItem(view,5167),"Scoped options or an invalid relative path invented a global backup location");
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(5166,BN_CLICKED),0);
        check(copied==0&&updates==0,"A stale copy-path action bypassed its global/absolute-path gate or changed preferences");
    }
    std::cout<<"PASS exact Unicode profile path/copy, manual whole-product Roaming guidance, explicitly isolated Local QA, narrow wrapping, reopen and global-only gates; no real profile, backup files or desktop clipboard touched\n";
}
void global_migration_contract_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");data.preferences["learning_progress"]["answer"]=std::string("unchanged");
    const auto host=CreateWindowExW(0,L"STATIC",L"Global migration contract",WS_OVERLAPPEDWINDOW,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create global migration test owner");
    std::vector<std::string> audio;int licenses=0,discarded=0,closed=0;
    SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){change(data);};access.open_audio=[&](std::string_view route){audio.emplace_back(route);};access.open_licenses=[&]{++licenses;};
    access.close=[&]{++closed;};access.unsaved_discarded=[&](std::string_view text){check(!text.empty(),"Unsaved warning was empty");++discarded;};
    access.storage_snapshot=[&]{windows::NarrationStorageSnapshot snapshot;snapshot.stored_bytes=1024ull*1024*1024;snapshot.maximum_bytes=8ull*1024*1024*1024;snapshot.configured_bytes=snapshot.effective_bytes=static_cast<std::uint64_t>(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes",4ll*1024*1024*1024));return snapshot;};
    const auto view=create_settings_view(host,access);MoveWindow(view,0,0,1000,750,TRUE);
    LONG previous=-1;for(const auto* title:{L"Idioma de la aplicación",L"Premium",L"Apariencia general",L"Lectura en voz alta",L"Flujo de aprendizaje",L"Gestos de navegación",L"Código"}){const auto control=child_with_caption(view,title);check(control!=nullptr,"A requested Android section is absent");const auto bounds=relative_rect(control,view);check(bounds.top>previous,"Global sections do not follow the requested order");previous=bounds.top;}
    check(relative_rect(GetDlgItem(view,5152),view).bottom<relative_rect(child_with_caption(view,L"Lectura en voz alta"),view).top&&
        relative_rect(GetDlgItem(view,5141),view).bottom<relative_rect(GetDlgItem(view,5142),view).top,
        "Read aloud and licenses did not stay together after General Appearance and before the deferred-colour Save notice");
    check(!GetDlgItem(view,5102)&&!GetDlgItem(view,5107)&&!GetDlgItem(view,5109)&&!GetDlgItem(view,5118),"Global options invented an unrelated content, speed, reader-visibility or gesture switch");
    for(const int id:{5150,5151}){const auto line=GetDlgItem(view,id);const auto bounds=relative_rect(line,view);check(line&&bounds.bottom-bounds.top==MulDiv(1,GetDpiForWindow(view),96),"Global continuation switches lost Android's one-DIP separators");}
    check((GetWindowLongPtrW(GetDlgItem(view,5152),GWL_STYLE)&SS_TYPEMASK)==SS_CENTER&&native_text(GetDlgItem(view,5152))==L"75 % · 150 %","Global text-size range does not match the original centered Android range");
    const auto kokoro_line=GetDlgItem(view,5153),kokoro_title=GetDlgItem(view,5154),storage_note=GetDlgItem(view,5155);
    check(kokoro_line&&kokoro_title&&storage_note&&native_text(kokoro_title)==L"Voces y recursos Kokoro","The original Kokoro subsection heading/divider is absent");
    const int dpi=static_cast<int>(GetDpiForWindow(view));const auto title_font=control_font(kokoro_title),note_font=control_font(storage_note);
    const auto voice_bounds=relative_rect(GetDlgItem(view,5106),view),line_bounds=relative_rect(kokoro_line,view),heading_bounds=relative_rect(kokoro_title,view),storage_bounds=relative_rect(GetDlgItem(view,5145),view);
    check(line_bounds.bottom-line_bounds.top==MulDiv(1,dpi,96)&&voice_bounds.bottom<storage_bounds.top&&storage_bounds.bottom<line_bounds.top&&line_bounds.bottom<heading_bounds.top&&relative_rect(GetDlgItem(view,5108),view).bottom<line_bounds.top,
        "Shared storage/advance controls did not precede the dedicated Kokoro resources subsection");
    check(title_font.lfHeight==-MulDiv(18,dpi,96)&&title_font.lfWeight>=FW_BOLD&&note_font.lfHeight==-MulDiv(14,dpi,96),"Kokoro headings or the storage explanation lost their original font roles");
    check(native_text(storage_note)==L"El total incluye todas las voces del motor seleccionado. Se reserva al menos 1 GB libre; los audios terminados nunca se borran automáticamente. El progreso corresponde a la voz, el motor y el idioma seleccionados.","The storage explanation omitted its engine-wide voice total, selected generation profile, free-space reserve or preservation policy");
    const auto license_heading=child_with_caption(view,L"Licencias");check(license_heading&&relative_rect(license_heading,view).left==voice_bounds.left&&relative_rect(GetDlgItem(view,5141),view).left==voice_bounds.left,
        "Licenses were separated from the narration card instead of sharing its content alignment");
    if(!reader_theme::palette(view).high_contrast)check(reader_theme::foreground(storage_note)==RGB(210,214,220),"The storage explanation lost the original narration text ink");
    for(const auto id:{5139,5140,5141})SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,id)));
    check(audio==std::vector<std::string>{"programming","math_physics"}&&licenses==1,"Audio/licence buttons are inert or use the wrong route");
    const auto slider=GetDlgItem(view,5144);check(slider&&SendMessageW(slider,TBM_GETPOS,0,0)==4096&&SendMessageW(slider,TBM_GETRANGEMAX,0,0)==8192,"Storage slider did not retain Android's one-MiB range and existing effective limit");SendMessageW(slider,TBM_SETPOS,TRUE,3073);SendMessageW(view,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,3073),reinterpret_cast<LPARAM>(slider));
    check(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes")==3073ll*1024*1024,"Storage slider rounded the selected MiB budget");
    check(native_text(GetDlgItem(view,5145)).find(L"Ya guardado:")!=std::wstring::npos&&native_text(GetDlgItem(view,5148)).find(L"1 GB de reserva")!=std::wstring::npos,"Moving the budget discarded Android's usage/reserve information");
    SendMessageW(slider,TBM_SETPOS,TRUE,0);SendMessageW(view,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(slider));
    check(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes")==0&&native_text(GetDlgItem(view,5145)).find(L"pregeneración pausada")!=std::wstring::npos&&native_text(GetDlgItem(view,5145)).find(L"No se generarán")!=std::wstring::npos,"Zero budget omitted the retained-audio/background-pause explanation");
    const auto warn=GetDlgItem(view,5142);check(warn&&GetRValue(reader_theme::foreground(warn))>GetGValue(reader_theme::foreground(warn)),"Deferred colours have no visible red Save notice");
    NarrationChoiceAction system;system.title=L"Motor de voz";system.count=2;system.choose=1;system.labels={L"Kokoro neuronal",L"Windows TTS"};open_narration_choice(view,5104,system);
    check((GetWindowLongPtrW(GetDlgItem(view,5144),GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongPtrW(GetDlgItem(view,5139),GWL_STYLE)&WS_VISIBLE),"Changing the playback engine hid the retained Kokoro storage/course controls that Android keeps visible");
    NarrationChoiceAction neural;neural.title=L"Motor de voz";neural.count=2;neural.selected=1;neural.choose=0;open_narration_choice(view,5104,neural);
    check((GetWindowLongPtrW(slider,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongPtrW(GetDlgItem(view,5139),GWL_STYLE)&WS_VISIBLE),"Returning to Kokoro failed to restore the existing controls");
    SendDlgItemMessageW(view,5103,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(5103,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,5103)));
    for(const int id:{5104,5105,5106,5108,5139,5140,5141,5144})check(!IsWindowEnabled(GetDlgItem(view,id)),"The narration master switch left a dependent action enabled");
    SendMessageW(view,WM_COMMAND,MAKEWPARAM(5141,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,5141)));check(licenses==1,"A stale license action bypassed the disabled narration master");
    const auto disabled_data=data;SendMessageW(slider,TBM_SETPOS,TRUE,999);SendMessageW(view,WM_HSCROLL,TB_THUMBTRACK,reinterpret_cast<LPARAM>(slider));check(data==disabled_data,"A queued storage slider action bypassed the disabled narration master");
    SendDlgItemMessageW(view,5103,BM_SETCHECK,BST_CHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(5103,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,5103)));
    check(IsWindowEnabled(GetDlgItem(view,5141))&&IsWindowEnabled(slider),"Re-enabling narration did not restore its existing actions");
    const auto before=data;SendMessageW(GetDlgItem(view,5128),BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(5128,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,5128)));SendMessageW(view,WM_APP+222,0,0);
    check(discarded==1&&closed==1&&data==before,"Leaving deferred colours blocked navigation, saved them or omitted the brief warning");DestroyWindow(view);
    data.preferences["lesson_narration_settings"]["offline_audio_budget_bytes"]=70123ll*1024*1024;
    access.storage_snapshot=[&]{windows::NarrationStorageSnapshot snapshot;snapshot.maximum_bytes=128ull*1024*1024*1024;snapshot.configured_bytes=snapshot.effective_bytes=static_cast<std::uint64_t>(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes"));return snapshot;};
    const auto retained=data;const auto large=create_settings_view(host,access);MoveWindow(large,0,0,1000,750,TRUE);const auto large_slider=GetDlgItem(large,5144);
    check(SendMessageW(large_slider,TBM_GETRANGEMAX,0,0)==131072&&SendMessageW(large_slider,TBM_GETPOS,0,0)==70123&&data==retained,"A large existing budget was truncated to a16-bit trackbar range or rewritten on open");
    SendMessageW(large_slider,TBM_SETPOS,TRUE,70001);SendMessageW(large,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,70001),reinterpret_cast<LPARAM>(large_slider));
    check(state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes")==70001ll*1024*1024,"Storage used the truncated notification word instead of the actual selected position");DestroyWindow(large);
    SettingsAccess route;route.scope=SettingsScope::audio;route.route_id="math_physics";route.audio_courses={{"math_a","Curso A"},{"math_b","Curso B"}};route.read=[&]{return data;};route.update=[&](const auto& change){change(data);};
    const auto courses=create_settings_view(host,std::move(route));MoveWindow(courses,0,0,1000,750,TRUE);const auto first=GetDlgItem(courses,5600);check(first&&GetDlgItem(courses,5601),"Course audio management omitted the real course list");
    SendMessageW(first,BM_SETCHECK,BST_UNCHECKED,0);SendMessageW(courses,WM_COMMAND,MAKEWPARAM(5600,BN_CLICKED),reinterpret_cast<LPARAM>(first));check(boolean(data,"lesson_narration_settings","kokoro_paused_course:math_physics:math_a"),"Course audio pause did not use the existing generation key");
    SendMessageW(first,BM_SETCHECK,BST_CHECKED,0);SendMessageW(courses,WM_COMMAND,MAKEWPARAM(5600,BN_CLICKED),reinterpret_cast<LPARAM>(first));check(!boolean(data,"lesson_narration_settings","kokoro_paused_course:math_physics:math_a")&&state::string_value(data,"learning_progress","answer")=="unchanged","Course resume changed answers or another course");DestroyWindow(courses);DestroyWindow(host);
    std::cout<<"PASS global Android section order, complete cards, live budget, real audio/licence actions, red buffered-colour notice, nonblocking discard and scoped course audio pause/resume\n";
}
void immediate_settings_language_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");seed.preferences["native_windows"]["content_language"]=std::string("es");
    seed.preferences["lesson_narration_settings"]["language"]=std::string("es");seed.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_santa");
    seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    ScopedFixture fixture(SettingsScope::global,seed);fixture.scale(125);fixture.toggle(5110,false);fixture.toggle(5114,true);fixture.toggle(5128,false);
    const auto before=fixture.data;select(fixture.view,5101,2);pump();
    check(child_with_caption(fixture.view,L"App Language")&&child_with_caption(fixture.view,L"Read aloud"),"App language did not refresh open Settings immediately");
    auto expected=before;apply_application_language(expected,"en");check(fixture.data==expected,"Application language did not update UI/content/narration together or changed unrelated data");
    check(state::string_value(fixture.data,"native_windows","content_language")=="en"&&state::string_value(fixture.data,"lesson_narration_settings","language")=="en-US","English app selection lost content or narration mapping");
    select(fixture.view,5101,3);pump();check(state::string_value(fixture.data,"native_windows","content_language")=="fr"&&state::string_value(fixture.data,"lesson_narration_settings","language")=="fr","French app selection did not update all languages");
    check(SendMessageW(fixture.control(5120),TBM_GETPOS,0,0)==125&&!fixture.checked(5110)&&fixture.checked(5114)&&!fixture.checked(5128),"Translation lost live appearance or pending colour protection");
    fixture.click(5123);check(appearance::global_text_scale(fixture.data)==125&&!curriculum::learning_flow(fixture.data).between_lessons&&appearance::global_code_wrap(fixture.data),"Saving colour changed live settings");
    check(fixture.data.preferences["cpp_lesson_drafts"]==seed.preferences["cpp_lesson_drafts"]&&state::string_value(fixture.data,"lesson_narration_settings","kokoro_voice:en-US")=="am_santa","Language selection changed user drafts or an existing voice identity");
    auto legacy=seed;legacy.preferences["native_windows"]["ui_language"]=std::string("es");legacy.preferences["native_windows"]["content_language"]=std::string("fr");legacy.preferences["lesson_narration_settings"]["language"]=std::string("fr");
    const auto legacy_drafts=legacy.preferences["cpp_lesson_drafts"];apply_application_language(legacy,"es");
    check(state::string_value(legacy,"native_windows","ui_language")=="es"&&state::string_value(legacy,"native_windows","content_language")=="es"&&state::string_value(legacy,"lesson_narration_settings","language")=="es"&&legacy.preferences["cpp_lesson_drafts"]==legacy_drafts&&state::string_value(legacy,"lesson_narration_settings","kokoro_voice:en-US")=="am_santa","Explicitly reselecting the same UI language failed to synchronize legacy independent preferences or changed voice/drafts");
    const auto synchronized=legacy;apply_application_language(legacy,"es");check(legacy==synchronized,"Reselecting an already synchronized language was not an exact no-op");
    auto british=seed;british.preferences["lesson_narration_settings"]["language"]=std::string("en-GB");apply_application_language(british,"en");check(state::string_value(british,"lesson_narration_settings","language")=="en-GB","Choosing English lost the existing British dialect");
    std::cout<<"PASS live UI/content/narration selection, same-window translation, retained dialect and voice IDs, no extra content selector\n";
}
}
int main(int argc,char** argv){
    using namespace lgaed;using namespace presentation;
    HWND host=nullptr,view=nullptr;
    try{
        std::cout<<std::unitbuf;
        std::string_view selected="all";
        if(argc!=1){check(argc==3&&std::string_view(argv[1])=="--case","Usage: settings_view_tests [--case licenses|controls|save-failure|interactions|engine-storage|profile-location|scrollbar-paint]");selected=argv[2];check(selected=="licenses"||selected=="controls"||selected=="save-failure"||selected=="interactions"||selected=="engine-storage"||selected=="profile-location"||selected=="scrollbar-paint","Unknown Settings test case; expected licenses, controls, save-failure, interactions, engine-storage, profile-location or scrollbar-paint");}
        const auto dpi_set=SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        check(dpi_set||GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext())==DPI_AWARENESS_PER_MONITOR_AWARE,
            "Settings visual QA must use the application's actual per-monitor DPI context");
        const auto common_dll=GetModuleHandleW(L"comctl32.dll");check(common_dll!=nullptr,"The settings test did not load its declared Common Controls dependency");
        const auto version_proc=std::bit_cast<DLLGETVERSIONPROC>(GetProcAddress(common_dll,"DllGetVersion"));
        DLLVERSIONINFO common_version{sizeof(common_version)};
        check(version_proc&&SUCCEEDED(version_proc(&common_version))&&common_version.dwMajorVersion>=6,
            "Settings test must use the same declared Common Controls v6 dependency as the application");
        std::cout<<"METRIC settings Common Controls: "<<common_version.dwMajorVersion<<'.'<<common_version.dwMinorVersion<<'\n';
        INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};
        check(InitCommonControlsEx(&common)!=FALSE,"The declared settings controls could not be registered");
        if(selected=="save-failure"){run_case("save_failure",save_failure_tests);return 0;}
        if(selected=="engine-storage"){run_case("engine_storage",engine_storage_tests);return 0;}
        if(selected=="profile-location"){run_case("profile_location",profile_location_tests);return 0;}
        if(selected=="scrollbar-paint"){run_case("scrollbar_paint",settings_scrollbar_paint_tests);return 0;}
        if(selected=="interactions"){
            run_case("thumb_drag",settings_thumb_drag_tests);run_case("slider_wheel",slider_wheel_tests);run_case("native_colour",native_color_picker_tests);
            run_case("live_storage",live_storage_tests);run_case("lesson_actions",lesson_settings_actions_tests);run_case("unavailable_system_voice",unavailable_system_voice_tests);run_case("engine_storage",engine_storage_tests);return 0;
        }
        state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["native_windows"]["content_language"]=std::string("fr");
        data.preferences["lesson_narration_settings"]["language"]=std::string("en-US");
        data.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_santa");
        data.preferences["learning_progress"]["exercise_untouched"]=std::string("opaque student data");
        const auto progress=data.preferences["learning_progress"];int closed=0,changes=0;
        host=CreateWindowExW(0,L"STATIC",L"Native settings QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create options QA host");
        if(selected!="controls"){
            const auto license_started=std::chrono::steady_clock::now();license_window_tests(host);
            std::cout<<"CASE licenses elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-license_started).count()<<'\n';
            if(selected=="licenses"){DestroyWindow(host);host=nullptr;std::cout<<"PASS all sixteen original narration notices in ES/EN/FR: text, controls, painting, DPI and Escape\n";return 0;}
        }
        const auto initial_started=std::chrono::steady_clock::now();
        SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){change(data);};
        access.changed=[&]{++changes;};access.close=[&]{++closed;};
        view=create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);
        check(native_text(GetDlgItem(view,5104))==L"Motor: Kokoro","Kokoro is not the absent-preference default engine action");
        check(native_text(GetDlgItem(view,5106))==L"Voz Kokoro: am_santa"&&state::string_value(data,"lesson_narration_settings","kokoro_voice:en-US")=="am_santa",
            "Opening options lost the original voice identifier or falsely claimed installed/hash-verified resources");
        NarrationChoiceAction us;us.title=L"Voces Kokoro";us.count=20;us.selected=19;open_narration_choice(view,5106,us);
        NarrationChoiceAction british;british.title=L"Idioma de lectura";british.selected=1;british.choose=2;open_narration_choice(view,5105,british);
        NarrationChoiceAction gb;gb.title=L"Voces Kokoro";gb.count=8;open_narration_choice(view,5106,gb);
        check(state::string_value(data,"lesson_narration_settings","language")=="en-GB","Changing speech language was not persisted");
        check(state::string_value(data,"native_windows","content_language")=="fr"&&state::string_value(data,"native_windows","ui_language")=="es","Speech language changed the curriculum or menus");
        NarrationChoiceAction system;system.title=L"Motor de voz";system.count=2;system.choose=1;open_narration_choice(view,5104,system);
        NarrationChoiceAction spanish_language;spanish_language.title=L"Idioma de lectura";spanish_language.selected=2;spanish_language.choose=0;open_narration_choice(view,5105,spanish_language);
        const auto actual=windows::system_voices();std::size_t spanish=0;for(const auto& voice:actual)if(windows::system_voice_matches(voice.language,"es"))++spanish;
        if(spanish){NarrationChoiceAction installed;installed.title=L"Voces de Windows";installed.count=static_cast<int>(spanish);installed.choose=spanish>1?1:-1;for(const auto& voice:actual)if(windows::system_voice_matches(voice.language,"es"))installed.labels.push_back(winrt::to_hstring(voice.name).c_str());
            open_narration_choice(view,5106,installed);if(spanish>1)check(!state::string_value(data,"lesson_narration_settings","windows_voice:es").empty(),"A selected system voice did not keep its concrete identifier");}
        else check(!IsWindowEnabled(GetDlgItem(view,5106))&&native_text(GetDlgItem(view,5106)).find(L"No hay voces disponibles")!=std::wstring::npos,"A missing Windows voice was presented as an installed working resource");
        check(data.preferences["learning_progress"]==progress,"Changing options modified student progress");
        SendMessageW(GetDlgItem(view,5100),BM_CLICK,0,0);MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        check(closed==1&&changes>=3,"Back or real option changes did not invoke the application hooks");
        DestroyWindow(view);view=nullptr;DestroyWindow(host);host=nullptr;
        std::cout<<"CASE initial_selectors elapsed_ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-initial_started).count()<<'\n';
        run_case("scopes",scoped_tests);
        run_case("reset_text",reset_text_tests);
        run_case("appearance",appearance_tests);
        run_case("math_wrap_reset",math_wrap_reset_tests);
        run_case("shortcuts",shortcut_tests);
        run_case("presentation",settings_presentation_tests);
        run_case("DPI_notice",settings_dpi_notice_tests);
        run_case("RGB",rgb_options_tests);
        run_case("slider_pointer",slider_pointer_tests);
        run_case("thumb_drag",settings_thumb_drag_tests);
        run_case("slider_wheel",slider_wheel_tests);
        run_case("native_colour",native_color_picker_tests);
        run_case("live_storage",live_storage_tests);
        run_case("lesson_actions",lesson_settings_actions_tests);
        run_case("save_failure",save_failure_tests);
        run_case("visual_metrics",visual_metrics_tests);
        run_case("attempts_owned",attempts_and_owned_settings_tests);
        run_case("owned_Escape",owned_settings_escape_tests);
        run_case("header_language",header_language_tests);
        run_case("unavailable_system_voice",unavailable_system_voice_tests);
        run_case("engine_storage",engine_storage_tests);
        run_case("scoped_parity",scoped_options_parity_tests);
        run_case("immediate_language",immediate_settings_language_tests);
        run_case("global_contract",global_migration_contract_tests);
        run_case("profile_location",profile_location_tests);
        std::cout<<"PASS native options: complete shared voices, real SAPI inventory, independent languages and preserved progress\n";return 0;
    }catch(const std::exception& e){if(view)DestroyWindow(view);if(host)DestroyWindow(host);std::cerr<<"FAIL native settings: "<<e.what()<<'\n';return 1;}
}

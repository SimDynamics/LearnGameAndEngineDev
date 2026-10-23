#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/editing_bar.hpp"
#include "platform/windows/system_speech.hpp"
#include "core/learning_flow.hpp"
#include "core/view_preferences.hpp"
#include "core/attempts.hpp"
#include "presentation/windows/ui_language.hpp"
#include <commctrl.h>
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

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void select(HWND view,int id,int value){SendDlgItemMessageW(view,id,CB_SETCURSEL,value,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,CBN_SELCHANGE),reinterpret_cast<LPARAM>(GetDlgItem(view,id)));}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}}
bool boolean(const lgaed::state::StateData& data,const char* domain,const char* key){const auto value=lgaed::state::find(data,domain,key);check(value&&std::holds_alternative<bool>(*value),"Expected a persisted boolean preference");return std::get<bool>(*value);}
struct ScopedFixture{
    lgaed::state::StateData data;HWND host=nullptr,view=nullptr;int updates=0,changes=0,closed=0,resets=0;std::vector<std::string> reset_routes;
    ScopedFixture(lgaed::presentation::SettingsScope scope,lgaed::state::StateData initial={},std::string course="course_a",bool programming=true):data(std::move(initial)){
        host=CreateWindowExW(0,L"STATIC",L"Scoped settings QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create scoped settings QA host");
        lgaed::presentation::SettingsAccess access;access.scope=scope;access.course_id=std::move(course);access.title="Título del contenido";access.eyebrow="Contexto";access.programming=programming;
        access.programming_course_ids={"course_a","course_b","course_c"};access.math_course_ids={"math_a","math_b"};access.reset_route=[this](std::string_view route){reset_routes.emplace_back(route);};
        access.read=[this]{return data;};access.update=[this](const auto& update){++updates;update(data);};access.changed=[this]{++changes;};access.close=[this]{++closed;};access.reset_scope=[this]{++resets;};
        view=lgaed::presentation::create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);
    }
    ~ScopedFixture(){if(view)DestroyWindow(view);if(host)DestroyWindow(host);}
    HWND control(int id)const{return GetDlgItem(view,id);}
    void has(int id,bool expected)const{check((control(id)!=nullptr)==expected,"Settings scope exposed an unrelated control or omitted its own control");}
    bool checked(int id)const{check(control(id)!=nullptr,"Missing scoped checkbox");return SendMessageW(control(id),BM_GETCHECK,0,0)==BST_CHECKED;}
    void toggle(int id,bool value){check(control(id)&&IsWindowEnabled(control(id)),"Attempted to change a disabled scoped checkbox");SendMessageW(control(id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));}
    void click(int id){check(control(id)&&IsWindowEnabled(control(id)),"Missing/disabled scoped action");SendMessageW(view,WM_COMMAND,MAKEWPARAM(id,BN_CLICKED),reinterpret_cast<LPARAM>(control(id)));pump();}
    void scale(int value){check(control(5120)&&IsWindowEnabled(control(5120)),"Text-size slider is missing or disabled");SendMessageW(control(5120),TBM_SETPOS,TRUE,value);SendMessageW(view,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,value),reinterpret_cast<LPARAM>(control(5120)));}
};
void scoped_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["student_answer"]=std::string("immutable answer");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    const auto protected_progress=seed.preferences["learning_progress"],protected_drafts=seed.preferences["cpp_lesson_drafts"];
    {
        ScopedFixture global(SettingsScope::global,seed);check(global.updates==0&&global.data==seed,"Opening global defaults wrote preferences");
        check(global.checked(5110)&&!global.checked(5111)&&!global.checked(5112)&&!global.checked(5113),"Android navigation defaults changed");
        check(curriculum::learning_flow(global.data)==curriculum::LearningFlow{},"Default global flow was not preserved");
        for(const auto id:{5101,5102,5103,5104,5108,5109,5110,5111,5112,5113,5114,5118})global.has(id,true);
        global.has(5116,false);global.has(5117,false);
    }
    curriculum::save_course_flow(seed,"course_b",{false,true,false,true});
    {
        ScopedFixture course(SettingsScope::course,seed);const auto before=course.data;
        for(const auto id:{5110,5111,5112,5113,5115,5116,5117})course.has(id,true);
        for(const auto id:{5101,5102,5103,5104,5105,5106,5107,5108,5109,5114,5118})course.has(id,false);
        check(course.checked(5116)&&!IsWindowEnabled(course.control(5110))&&!IsWindowEnabled(course.control(5117)),"Inherited course controls are editable or Save starts enabled");
        course.toggle(5116,false);course.toggle(5110,false);course.toggle(5111,true);course.toggle(5112,true);course.toggle(5113,true);
        check(course.data==before&&course.updates==0&&course.changes==0,"Course edits were persisted before Save");
        check(IsWindowEnabled(course.control(5117)),"Buffered course edits did not enable Save");course.click(5117);
        check(course.updates==1&&course.changes==1&&!IsWindowEnabled(course.control(5117)),"Course Save did not commit one draft transaction");
        check(curriculum::learning_flow(course.data,"course_a")==curriculum::LearningFlow{false,true,true,true},"Saved course flow differs from its draft");
        check(curriculum::learning_flow(course.data)==curriculum::LearningFlow{}&&curriculum::learning_flow(course.data,"course_b")==curriculum::LearningFlow{false,true,false,true},"Course Save changed global defaults or another course");
        check(course.data.preferences["learning_progress"]==protected_progress&&course.data.preferences["cpp_lesson_drafts"]==protected_drafts,"Saving course flow altered student data or whitespace");
        seed=course.data;
    }
    {
        ScopedFixture course(SettingsScope::course,seed);check(!course.checked(5116)&&IsWindowEnabled(course.control(5110)),"Existing course override was not restored");
        course.toggle(5111,false);course.click(5100);check(course.closed==1&&course.updates==0&&course.data==seed,"Back saved an uncommitted course draft");
    }
    {
        ScopedFixture course(SettingsScope::course,seed);course.toggle(5116,true);check(curriculum::has_course_flow(course.data,"course_a"),"Selecting inheritance removed the stored override before Save");
        check(!IsWindowEnabled(course.control(5110)),"Inheritance did not disable course-only draft controls");course.click(5117);
        check(!curriculum::has_course_flow(course.data,"course_a")&&curriculum::has_course_flow(course.data,"course_b"),"Saving inheritance removed another course's override");
        check(curriculum::learning_flow(course.data,"course_a")==curriculum::learning_flow(course.data),"Saved inherited course does not follow the global flow");
    }
    {
        ScopedFixture unit(SettingsScope::unit,seed);check(unit.data==seed&&unit.updates==0,"Opening unit options wrote preferences");
        unit.has(5115,true);for(const auto id:{5101,5102,5103,5104,5105,5106,5107,5108,5109,5110,5111,5112,5113,5114,5116,5117,5118})unit.has(id,false);
        unit.click(5115);check(unit.resets==1&&unit.data==seed,"Unit reset did not delegate exactly its own scope");
    }
    {
        ScopedFixture audio(SettingsScope::audio,seed);check(audio.data==seed&&audio.updates==0,"Opening audio options wrote preferences");
        for(const auto id:{5103,5104,5105,5106,5107,5108,5109})audio.has(id,true);
        for(const auto id:{5101,5102,5110,5111,5112,5113,5114,5115,5116,5117,5118})audio.has(id,false);
        audio.toggle(5108,false);audio.toggle(5109,false);
        check(!boolean(audio.data,"lesson_narration_settings","kokoro_advance_generation")&&
              !boolean(audio.data,"lesson_narration_settings","controls_visible"),"Audio controls did not persist their own settings");
        check(audio.data.preferences["learning_progress"]==protected_progress&&audio.data.preferences["cpp_lesson_drafts"]==protected_drafts,"Audio settings altered student data");
    }
    {
        ScopedFixture lesson(SettingsScope::lesson,seed);check(lesson.data==seed&&lesson.updates==0,"Opening lesson options wrote preferences");
        for(const auto id:{5105,5106,5115,5118})lesson.has(id,true);
        for(const auto id:{5101,5102,5103,5104,5107,5108,5109,5110,5111,5112,5113,5114,5116,5117})lesson.has(id,false);
        lesson.click(5115);check(lesson.resets==1&&lesson.data==seed,"Lesson reset did not delegate only its selected scope");
    }
    std::cout<<"PASS native settings scopes: defaults, buffered course Save/Back/inheritance, unit, audio, lesson and protected drafts\n";
}
void appearance_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["answer"]=std::string("exact answer");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_global_text_scale(seed,115);appearance::save_course_text_scale(seed,"course_a",false,90);
    appearance::save_global_wrap(seed,false,{});appearance::save_course_wrap(seed,"course_a",false);appearance::save_wrap_overwrite(seed,"course_a",false);
    appearance::save_course_wrap(seed,"outside_programming",false);appearance::save_wrap_overwrite(seed,"outside_programming",false);
    auto& accents=seed.preferences["appearance_settings"];
    accents["accent_red"]=std::int64_t{10};accents["accent_green"]=std::int64_t{20};accents["accent_blue"]=std::int64_t{30};
    for(const auto& route:{std::string("programming"),std::string("math_physics")}){
        accents["route_accent_red_"+route]=std::int64_t{1};accents["route_accent_green_"+route]=std::int64_t{2};accents["route_accent_blue_"+route]=std::int64_t{3};
        accents["route_allow_global_accent_overwrite_"+route]=true;
    }
    accents["default_global_accent_red"]=std::int64_t{41};accents["default_global_accent_green"]=std::int64_t{42};accents["default_global_accent_blue"]=std::int64_t{43};
    accents["default_route_accent_red_math_physics"]=std::int64_t{61};accents["default_route_accent_green_math_physics"]=std::int64_t{62};accents["default_route_accent_blue_math_physics"]=std::int64_t{63};
    const auto progress=seed.preferences["learning_progress"],drafts=seed.preferences["cpp_lesson_drafts"];
    {
        ScopedFixture global(SettingsScope::global,seed);const auto before=global.data;
        check(global.updates==0&&SendMessageW(global.control(5120),TBM_GETPOS,0,0)==115,"Opening appearance did not retain the original text scale");
        check(SendMessageW(global.control(5120),TBM_GETRANGEMIN,0,0)==75&&SendMessageW(global.control(5120),TBM_GETRANGEMAX,0,0)==150,"Text slider changed Android's 75–150 percent range");
        check(!IsWindowEnabled(global.control(5123)),"Appearance Save starts enabled without edits");
        check(SendMessageW(global.control(5101),CB_GETCOUNT,0,0)==4&&SendMessageW(global.control(5101),CB_GETCURSEL,0,0)==0,"Automatic UI language is absent from Android's first position");
        global.scale(125);global.toggle(5110,false);global.toggle(5114,true);global.toggle(5128,false);
        global.click(5130);global.click(5132);
        check(global.data==before&&global.updates==0&&global.changes==0,"Pending global typography, flow, wrapping or colours were saved before Save");
        global.click(5123);check(global.updates==1&&global.changes==1&&!IsWindowEnabled(global.control(5123)),"Appearance Save was not one explicit draft transaction");
        check(appearance::global_text_scale(global.data)==125&&appearance::text_scale(global.data,"course_a")==90,"Global text scale overwrote a protected course exception");
        check(!curriculum::learning_flow(global.data).between_lessons,"Buffered global navigation was not saved");
        check(appearance::global_code_wrap(global.data)&&!appearance::code_wrap(global.data,"course_a")&&appearance::code_wrap(global.data,"course_b"),"Global wrapping ignored the actual course protection");
        check(!appearance::code_wrap(global.data,"outside_programming")&&!appearance::allows_global_wrap(global.data,"outside_programming"),"Global wrapping changed another route");
        check(reader_theme::appearance_accent(global.data)==RGB(41,42,43),"Reset global accent ignored imported custom default components");
        check(reader_theme::appearance_accent(global.data,"programming")==RGB(1,2,3),"The newly selected protection did not apply before global propagation");
        check(reader_theme::appearance_accent(global.data,"math_physics")==RGB(61,62,63),"Explicit route colour in the same Save lost to global propagation");
        check(state::integer_value(global.data,"appearance_settings","default_global_accent_red")==41,"Saving colours changed the user's imported defaults");
        check(global.data.preferences["learning_progress"]==progress&&global.data.preferences["cpp_lesson_drafts"]==drafts,"Appearance changed exact student data or stdin whitespace");
        seed=global.data;
        global.click(5134);global.click(5135);check(global.reset_routes==std::vector<std::string>{"programming","math_physics"}&&global.data==seed,"Route progress actions did not delegate their exact scopes");
        global.click(5133);check(appearance::allows_global_wrap(global.data,"course_a")&&appearance::code_wrap(global.data,"course_a"),"Reset wrap exceptions did not restore the programming course defaults");
        check(!appearance::allows_global_wrap(global.data,"outside_programming"),"Reset wrap exceptions changed a course outside Programming");
    }
    {
        ScopedFixture global(SettingsScope::global,seed);global.scale(80);global.click(5131);global.click(5100);
        check(global.closed==1&&global.updates==0&&global.data==seed,"Back persisted global typography or colour drafts");
        select(global.view,5101,1);check(state::string_value(global.data,"native_windows","ui_language")=="es","Spanish app language did not follow Android's choice order");
        select(global.view,5101,0);check(state::string_value(global.data,"native_windows","ui_language")=="auto","Automatic UI language was not persisted independently");
        check(interface_language(global.data)=="es"||interface_language(global.data)=="en"||interface_language(global.data)=="fr","Automatic UI language did not resolve to a supported language");
    }
    {
        ScopedFixture lesson(SettingsScope::lesson,seed);const auto before=lesson.data;
        check(lesson.checked(5122)==false&&SendMessageW(lesson.control(5120),TBM_GETPOS,0,0)==90,"Lesson options did not restore its course text override");
        lesson.toggle(5122,true);check(!IsWindowEnabled(lesson.control(5120)),"Inherited course text size remains editable");
        lesson.toggle(5122,false);lesson.scale(135);lesson.toggle(5124,true);
        check(lesson.data==before&&lesson.updates==0,"Course typography or wrap protection escaped its draft before Save");
        lesson.click(5123);check(lesson.updates==1&&appearance::text_scale(lesson.data,"course_a")==135&&appearance::global_text_scale(lesson.data)==125,"Saving course typography changed the global size");
        check(appearance::allows_global_wrap(lesson.data,"course_a"),"Course wrap overwrite protection was not saved");
        lesson.toggle(5122,true);lesson.click(5123);
        check(appearance::text_scale(lesson.data,"course_a")==125&&appearance::course_text_scale(lesson.data,"course_a")==135,"Restoring inheritance lost the course's separately saved size");
        check(lesson.data.preferences["learning_progress"]==progress&&lesson.data.preferences["cpp_lesson_drafts"]==drafts,"Saving course appearance changed student data");
    }
    {
        ScopedFixture mathematics(SettingsScope::lesson,seed,"course_math",false);mathematics.has(5120,true);mathematics.has(5122,true);mathematics.has(5123,true);
        mathematics.has(5114,false);mathematics.has(5124,false);mathematics.has(5125,false);
    }
    std::cout<<"PASS native appearance: buffered scale/flow/wrap, course inheritance, custom default colours, protections, explicit route priority, route resets and automatic UI language\n";
}
void math_wrap_reset_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["learning_progress"]["answer"]=std::string("untouched");seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    appearance::save_global_wrap(seed,false,{});
    for(const auto course:{"course_a","course_b","math_a","math_b","unrelated_course"}){
        appearance::save_course_wrap(seed,course,true);appearance::save_wrap_overwrite(seed,course,false);}
    ScopedFixture global(SettingsScope::global,seed);global.has(5133,true);global.has(5137,true);
    check(global.data==seed&&global.updates==0,"Opening global route wrap controls wrote preferences");
    global.click(5137);
    auto expected=seed;
    for(const auto course:{std::string("math_a"),std::string("math_b")}){
        expected.preferences["code_wrap_settings"]["course_wrap_"+course]=false;
        expected.preferences["code_wrap_settings"]["course_allow_global_overwrite_"+course]=true;
    }
    check(global.updates==1&&global.changes==1&&global.data==expected,"Mathematics wrap reset changed Programming, student data or courses outside its actual IDs");
    for(const auto scope:{SettingsScope::course,SettingsScope::unit,SettingsScope::audio,SettingsScope::lesson}){ScopedFixture other(scope,seed);other.has(5137,false);}
    for(const auto& locale:{std::string("es"),std::string("en"),std::string("fr")}){
        auto localized=seed;localized.preferences["native_windows"]["ui_language"]=locale;ScopedFixture fixture(SettingsScope::global,localized);
        wchar_t title[200]{};GetWindowTextW(fixture.control(5137),title,200);
        const std::wstring_view expected_word=locale=="es"?L"Matemáticas":locale=="en"?L"Mathematics":L"Mathématiques";
        check(std::wstring_view(title).find(expected_word)!=std::wstring_view::npos,"Mathematics wrap reset is not translated to the selected interface language");
    }
    std::cout<<"PASS native Mathematics wrap reset: real route IDs only, independent Programming state, protected data and ES/EN/FR labels\n";
}
struct ShortcutAction{bool restore=false,save=false,edit=false,found=false,revealed=false;std::array<std::wstring,20> opened{};std::exception_ptr error;};
ShortcutAction* shortcut_action=nullptr;
std::wstring native_text(HWND window){std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(window))+1,L'\0');text.resize(static_cast<std::size_t>(GetWindowTextW(window,text.data(),static_cast<int>(text.size()))));return text;}
void CALLBACK shortcut_timer(HWND,UINT,UINT_PTR timer,DWORD){
    HWND dialog=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND target,LPARAM context)->BOOL{
        wchar_t name[40]{};GetClassNameW(target,name,40);if(std::wstring_view(name)==L"#32770"&&GetDlgItem(target,5350)){
            *reinterpret_cast<HWND*>(context)=target;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!shortcut_action)return;KillTimer(nullptr,timer);auto& action=*shortcut_action;action.found=true;
    try{
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
        check(lesson.updates==1&&appearance::text_scale(lesson.data,"course_a")==90&&IsWindowEnabled(lesson.control(5123)),"Shortcut Save also committed the unrelated pending text-size draft");
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
void settings_presentation_tests(){
    using namespace lgaed;using namespace presentation;state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    ScopedFixture fixture(SettingsScope::global,seed);const int dpi=static_cast<int>(GetDpiForWindow(fixture.view));const auto px=[dpi](int value){return MulDiv(value,dpi,96);};
    const auto title=child_with_caption(fixture.view,L"Título del contenido"),subtitle=child_with_caption(fixture.view,L"Configuración general de LearnGameAndEngineDev");
    const auto section=child_with_caption(fixture.view,L"Idioma de la aplicación");check(title&&subtitle&&section,"Settings omitted its visible hierarchy");
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
        const auto save=relative_rect(fixture.control(5123),fixture.view);check(save.bottom-save.top>=px(62),"Primary settings action is shorter than the reference62 DIP minimum");
    }
    check(narrow_title_height>wide_title_height,"The settings title did not grow naturally on the narrow viewport");
    MoveWindow(fixture.view,0,0,px(360),px(520),TRUE);pump();const auto combo=fixture.control(5101);
    for(int count=0;count<200;++count){const auto bounds=relative_rect(combo,fixture.view);RECT client{};GetClientRect(fixture.view,&client);if(bounds.top>=px(18)&&bounds.bottom<=client.bottom-px(18))break;SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);}
    const auto bounds=relative_rect(combo,fixture.view);RECT client{};GetClientRect(fixture.view,&client);
    check(bounds.top>=px(18)&&bounds.bottom<=client.bottom-px(18),"Padded selector card cannot be scrolled fully into view");
    const auto screen=GetDC(fixture.view),dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,client.right,client.bottom);ReleaseDC(fixture.view,screen);
    check(dc&&bitmap,"Cannot capture selector-card presentation");const auto previous=SelectObject(dc,bitmap);SendMessageW(fixture.view,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    const auto palette=reader_theme::palette(fixture.view);const int y=(bounds.top+bounds.bottom)/2;
    const bool card=GetPixel(dc,bounds.left-px(9),y)==palette.card,canvas=GetPixel(dc,px(4),y)==palette.canvas;
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);check(card&&canvas,"The18 DIP selector padding has no real card painted behind it");
    if(!palette.high_contrast){const auto reset=fixture.control(5115);
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
    DRAWITEMSTRUCT draw{};draw.CtlType=ODT_BUTTON;draw.CtlID=5115;draw.itemAction=ODA_DRAWENTIRE;draw.hwndItem=reset;draw.hDC=reset_dc;draw.rcItem=reset_bounds;
    check(SendMessageW(fixture.view,WM_DRAWITEM,5115,reinterpret_cast<LPARAM>(&draw))!=FALSE,"The native reset action was not painted");
    const auto ink=reader_theme::accent_foreground(fixture.view);
    const int last_line_top=text_bounds.top+std::max<LONG>(0,(text_bounds.bottom-text_bounds.top-text_height)/2)+text_height-metrics.tmHeight;
    int last_line_pixels=0;for(int row=last_line_top;row<std::min<LONG>(last_line_top+metrics.tmHeight,reset_bounds.bottom);++row)
        for(int column=text_bounds.left;column<text_bounds.right;++column)if(GetPixel(reset_dc,column,row)==ink)++last_line_pixels;
    SelectObject(reset_dc,old_font);SelectObject(reset_dc,old_bitmap);DeleteObject(reset_bitmap);DeleteDC(reset_dc);
    check(last_line_pixels>10,"A long reset action lost its final wrapped text line");}
    check(fixture.data==seed&&fixture.updates==0,"Settings presentation changed preferences or student data");
    std::cout<<"PASS native settings presentation: reference font hierarchy, padded selector cards, minimum actions and responsive text growth\n";
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
bool checkbox_metrics(HWND checkbox,const char* phase,bool painted=false){
    RECT bounds{};GetClientRect(checkbox,&bounds);const auto dpi=static_cast<int>(GetDpiForWindow(checkbox));
    const auto screen=GetDC(checkbox);const auto dc=CreateCompatibleDC(screen);const auto bitmap=CreateCompatibleBitmap(screen,bounds.right,bounds.bottom);
    ReleaseDC(checkbox,screen);check(dc&&bitmap,"Cannot capture native settings checkbox");const auto old=SelectObject(dc,bitmap);
    const auto background=lgaed::presentation::reader_theme::background(checkbox);const auto fill=CreateSolidBrush(background);FillRect(dc,&bounds,fill);DeleteObject(fill);
    if(painted){const auto rendered=GetDC(checkbox);BitBlt(dc,0,0,bounds.right,bounds.bottom,rendered,0,0,SRCCOPY);ReleaseDC(checkbox,rendered);}
    else SendMessageW(checkbox,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    int min_x=bounds.right,min_y=bounds.bottom,max_x=-1,max_y=-1,pixels=0;
    const auto limit=std::min(static_cast<int>(bounds.right),MulDiv(16,dpi,96));
    for(int y=0;y<bounds.bottom;++y)for(int x=0;x<limit;++x){const auto color=GetPixel(dc,x,y);
        const int contrast=std::max({std::abs(static_cast<int>(GetRValue(color))-GetRValue(background)),
            std::abs(static_cast<int>(GetGValue(color))-GetGValue(background)),std::abs(static_cast<int>(GetBValue(color))-GetBValue(background))});
        if(color!=CLR_INVALID&&contrast>=60){++pixels;min_x=std::min(min_x,x);max_x=std::max(max_x,x);min_y=std::min(min_y,y);max_y=std::max(max_y,y);}}
    const auto glyph_width=max_x>=min_x?max_x-min_x+1:0,glyph_height=max_y>=min_y?max_y-min_y+1:0;
    std::cout<<"METRIC settings checkbox "<<phase<<": dpi="<<dpi<<" control="<<bounds.right<<'x'<<bounds.bottom
        <<" type="<<(GetWindowLongPtrW(checkbox,GWL_STYLE)&BS_TYPEMASK)<<" check="<<SendMessageW(checkbox,BM_GETCHECK,0,0)
        <<" glyph-first16dip="<<glyph_width<<'x'<<glyph_height<<" legible-pixels="<<pixels<<'\n';
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return glyph_width>=MulDiv(8,dpi,96)&&glyph_height>=MulDiv(8,dpi,96);
}
void visual_metrics_tests(){
    ScopedFixture fixture(lgaed::presentation::SettingsScope::global);const auto combo=fixture.control(5101);
    const auto initial=combo_metrics(combo,"after-open");checkbox_metrics(fixture.control(5103),"hidden-print");
    for(int count=0;count<200;++count){RECT control{},viewport{};GetWindowRect(fixture.control(5103),&control);MapWindowPoints(nullptr,fixture.view,reinterpret_cast<POINT*>(&control),2);GetClientRect(fixture.view,&viewport);
        if(control.top>=0&&control.bottom<=viewport.bottom)break;SendMessageW(fixture.view,WM_VSCROLL,SB_LINEDOWN,0);}
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
    {ScopedFixture free(SettingsScope::global,data);free.has(5138,false);}
    const auto host=CreateWindowExW(0,L"STATIC",L"Owned settings QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create owned-settings test host");
    int closed=0;SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& mutation){mutation(data);};access.close=[&]{++closed;};
    const auto before=data;const auto frame=create_settings_window(host,std::move(access));
    check(GetWindow(frame,GW_OWNER)==host&&!(GetWindowLongPtrW(frame,GWL_STYLE)&WS_CHILD),"Settings did not open in its own owned top-level window");
    const auto view=FindWindowExW(frame,nullptr,L"LearnGameAndEngineDev.SettingsView",nullptr);check(view&&GetParent(view)==frame,"The owned settings frame does not host the shared settings view");
    MoveWindow(frame,0,0,720,580,TRUE);pump();RECT outer{};GetClientRect(frame,&outer);const auto inner=relative_rect(view,frame);
    check(outer.left==inner.left&&outer.top==inner.top&&outer.right==inner.right&&outer.bottom==inner.bottom,"The independent settings window did not resize its complete content bounds");
    SendMessageW(frame,WM_CLOSE,0,0);check(closed==1&&IsWindow(host)&&data==before,"Closing settings destroyed its lesson owner or changed stored data");
    DestroyWindow(frame);DestroyWindow(host);
    std::cout<<"PASS Windows owned settings: independent resize/close, shared hourly reserves, legacy migration, route separation and Premium gold without purchases\n";
}
void owned_settings_escape_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    seed.preferences["learning_progress"]["answer"]=std::string("preserved answer");
    seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    const auto host=CreateWindowExW(0,L"STATIC",L"Settings Escape QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    check(host!=nullptr,"Cannot create the Settings Escape owner");
    for(const auto focus_id:{5100,5101,0}){
        auto data=seed;int closed=0,updates=0;
        SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& mutation){++updates;mutation(data);};access.close=[&]{++closed;};
        const auto frame=create_settings_window(host,std::move(access));
        const auto view=FindWindowExW(frame,nullptr,L"LearnGameAndEngineDev.SettingsView",nullptr);
        check(view!=nullptr,"The Settings Escape window omitted its shared content");
        // Persisted choices remain saved; buffered appearance edits retain the same close policy as Back.
        select(view,5102,1);const auto before_close=data;const int saved_updates=updates;
        const auto slider=GetDlgItem(view,5120);SendMessageW(slider,TBM_SETPOS,TRUE,125);
        SendMessageW(view,WM_HSCROLL,MAKEWPARAM(TB_THUMBTRACK,125),reinterpret_cast<LPARAM>(slider));
        check(IsWindowEnabled(GetDlgItem(view,5123))&&data==before_close,"Settings Escape setup did not keep its appearance edit buffered");
        if(focus_id){
            const auto control=GetDlgItem(view,focus_id);check(control!=nullptr,"Settings Escape omitted its real focus target");
            SetActiveWindow(frame);SetFocus(control);check(GetFocus()==control,"Settings Escape could not focus the real button or combo");
            MSG key{};key.hwnd=control;key.message=WM_KEYDOWN;key.wParam=VK_ESCAPE;key.lParam=1;
            check(IsDialogMessageW(frame,&key)!=FALSE,"The owned Settings frame did not process Escape through native dialog navigation");
        }else SendMessageW(frame,WM_KEYDOWN,VK_ESCAPE,1);
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
    HWND dialog=nullptr;EnumThreadWindows(GetCurrentThreadId(),[](HWND target,LPARAM context)->BOOL{
        wchar_t name[40]{};GetClassNameW(target,name,40);if(std::wstring_view(name)==L"#32770"&&GetDlgItem(target,8350)){*reinterpret_cast<HWND*>(context)=target;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&dialog));
    if(!dialog||!language_action)return;KillTimer(nullptr,timer);auto& action=*language_action;action.found=true;
    try{
        check(native_text(dialog)==action.title,"The header language chooser title does not match the selected UI language");
        check(native_text(GetDlgItem(dialog,8350))==action.automatic,"The header omitted Android's automatic device-language choice");
        for(int index=0;index<4;++index){const auto control=GetDlgItem(dialog,8350+index);check(control!=nullptr,"The header language chooser omitted one of its original choices");
            check((SendMessageW(control,BM_GETCHECK,0,0)==BST_CHECKED)==(index==action.selected),"The header language chooser changed or lost the saved selection");}
        check(GetDlgItem(dialog,8354)==nullptr,"The header language chooser invented another curriculum language");
        if(action.choose>=0)SendMessageW(GetDlgItem(dialog,8350+action.choose),BM_CLICK,0,0);else SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);
    }catch(...){action.error=std::current_exception();SendMessageW(dialog,WM_COMMAND,IDCANCEL,0);}
}
void header_language_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData data;data.preferences["native_windows"]["content_language"]=std::string("fr");data.preferences["lesson_narration_settings"]["language"]=std::string("en-GB");
    data.preferences["lesson_narration_settings"]["kokoro_voice:en-GB"]=std::string("bm_george");data.preferences["learning_progress"]["answer"]=std::string("unchanged");
    const auto host=CreateWindowExW(0,L"STATIC",L"Header language QA",WS_OVERLAPPEDWINDOW,0,0,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);check(host!=nullptr,"Cannot create header language QA host");
    const std::array<std::string,4> tags{"auto","es","en","fr"};
    for(int selection=1;selection<4;++selection){
        data.preferences["native_windows"]["ui_language"]=tags[selection];const auto before=data;
        LanguageAction action;action.selected=selection;action.choose=0;
        action.title=selection==1?L"Idioma de la aplicación":selection==2?L"App language":L"Langue de l’application";
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
void immediate_settings_language_tests(){
    using namespace lgaed;using namespace presentation;
    state::StateData seed;seed.preferences["native_windows"]["ui_language"]=std::string("es");
    seed.preferences["native_windows"]["content_language"]=std::string("fr");seed.preferences["lesson_narration_settings"]["language"]=std::string("en-US");
    seed.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_santa");
    seed.preferences["keyboard_shortcut_settings"]["cpp_shortcuts"]=std::string("[\"(\",\"  \",\"::\"]");
    seed.preferences["cpp_lesson_drafts"]["stdin"]=std::string(" \t \n");
    ScopedFixture fixture(SettingsScope::global,seed);fixture.scale(125);fixture.toggle(5110,false);fixture.toggle(5114,true);fixture.toggle(5128,false);
    const auto before=fixture.data;select(fixture.view,5101,2);pump();
    check(child_with_caption(fixture.view,L"Application language")&&child_with_caption(fixture.view,L"Read aloud"),"Changing UI language did not update the open settings labels immediately");
    check(SendMessageW(fixture.control(5120),TBM_GETPOS,0,0)==125&&!fixture.checked(5110)&&fixture.checked(5114)&&!fixture.checked(5128)&&IsWindowEnabled(fixture.control(5123)),"Retranslating settings lost pending scale/flow/wrap/protection edits");
    auto expected=before;expected.preferences["native_windows"]["ui_language"]=std::string("en");check(fixture.data==expected,"Changing UI language committed pending appearance, changed shortcuts or altered curriculum/voice/data");
    select(fixture.view,5101,3);pump();check(child_with_caption(fixture.view,L"Langue de l’application")&&child_with_caption(fixture.view,L"Lecture à voix haute"),"French labels did not refresh in the same settings window");
    check(SendMessageW(fixture.control(5120),TBM_GETPOS,0,0)==125&&!fixture.checked(5110)&&fixture.checked(5114)&&!fixture.checked(5128),"A second language change lost pending settings");
    fixture.click(5123);check(appearance::global_text_scale(fixture.data)==125&&!curriculum::learning_flow(fixture.data).between_lessons&&appearance::global_code_wrap(fixture.data),"Pending edits did not remain saveable after live language changes");
    check(fixture.data.preferences["keyboard_shortcut_settings"]==seed.preferences["keyboard_shortcut_settings"]&&fixture.data.preferences["cpp_lesson_drafts"]==seed.preferences["cpp_lesson_drafts"]&&fixture.data.preferences["lesson_narration_settings"]==seed.preferences["lesson_narration_settings"],"Live UI translation modified shortcuts, drafts or narration choices");
    std::cout<<"PASS open settings language refresh: Spanish/English/French labels with preserved unsaved scale/wrap/flow and exact shortcuts, content and voice\n";
}
}
int main(){
    using namespace lgaed;using namespace presentation;
    HWND host=nullptr,view=nullptr;
    try{
        std::cout<<std::unitbuf;
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
        state::StateData data;data.preferences["native_windows"]["ui_language"]=std::string("es");
        data.preferences["native_windows"]["content_language"]=std::string("fr");
        data.preferences["lesson_narration_settings"]["language"]=std::string("en-US");
        data.preferences["lesson_narration_settings"]["kokoro_voice:en-US"]=std::string("am_santa");
        data.preferences["learning_progress"]["exercise_untouched"]=std::string("opaque student data");
        const auto progress=data.preferences["learning_progress"];int closed=0,changes=0;
        host=CreateWindowExW(0,L"STATIC",L"Native settings QA",WS_OVERLAPPEDWINDOW,0,0,1000,750,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host!=nullptr,"Cannot create options QA host");
        SettingsAccess access;access.read=[&]{return data;};access.update=[&](const auto& change){change(data);};
        access.changed=[&]{++changes;};access.close=[&]{++closed;};
        view=create_settings_view(host,std::move(access));MoveWindow(view,0,0,960,700,TRUE);
        check(SendDlgItemMessageW(view,5104,CB_GETCURSEL,0,0)==0,"Kokoro is not the absent-preference default engine");
        check(SendDlgItemMessageW(view,5106,CB_GETCOUNT,0,0)==20,"The US selector omits original catalogue voices");
        const auto selected=static_cast<int>(SendDlgItemMessageW(view,5106,CB_GETCURSEL,0,0));wchar_t value[100]{};
        SendDlgItemMessageW(view,5106,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(value));
        check(std::wstring_view(value)==L"Santa"&&state::string_value(data,"lesson_narration_settings","kokoro_voice:en-US")=="am_santa",
            "Opening options lost the original voice label or changed its concrete identifier");
        select(view,5105,2);
        check(SendDlgItemMessageW(view,5106,CB_GETCOUNT,0,0)==8,"The GB selector omits original catalogue voices");
        check(state::string_value(data,"lesson_narration_settings","language")=="en-GB","Changing speech language was not persisted");
        check(state::string_value(data,"native_windows","content_language")=="fr"&&state::string_value(data,"native_windows","ui_language")=="es","Speech language changed the curriculum or menus");
        select(view,5104,1);select(view,5105,0);
        const auto actual=windows::system_voices();std::size_t spanish=0;for(const auto& voice:actual)if(windows::system_voice_matches(voice.language,"es"))++spanish;
        check(SendDlgItemMessageW(view,5106,CB_GETCOUNT,0,0)==static_cast<LRESULT>(std::max<std::size_t>(1,spanish)),"Windows options do not reflect the real installed voice catalogue");
        if(spanish){select(view,5106,0);check(!state::string_value(data,"lesson_narration_settings","windows_voice:es").empty(),"A selected system voice did not keep its concrete identifier");}
        check(data.preferences["learning_progress"]==progress,"Changing options modified student progress");
        SendMessageW(GetDlgItem(view,5100),BM_CLICK,0,0);MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        check(closed==1&&changes>=3,"Back or real option changes did not invoke the application hooks");
        DestroyWindow(view);view=nullptr;DestroyWindow(host);host=nullptr;
        scoped_tests();
        appearance_tests();
        math_wrap_reset_tests();
        shortcut_tests();
        settings_presentation_tests();
        visual_metrics_tests();
        attempts_and_owned_settings_tests();
        owned_settings_escape_tests();
        header_language_tests();
        immediate_settings_language_tests();
        std::cout<<"PASS native options: complete shared voices, real SAPI inventory, independent languages and preserved progress\n";return 0;
    }catch(const std::exception& e){if(view)DestroyWindow(view);if(host)DestroyWindow(host);std::cerr<<"FAIL native settings: "<<e.what()<<'\n';return 1;}
}

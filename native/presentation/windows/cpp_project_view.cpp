#include "presentation/windows/cpp_project_view.hpp"
#include "presentation/windows/card_list.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/window_fullscreen.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/settings_view.hpp"
#include "platform/windows/state_store.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <thread>

namespace lgaed::presentation {
namespace {
using namespace winrt::Windows::Data::Json;
constexpr wchar_t project_class[]=L"LearnGameAndEngineDev.CppProjectControls";
constexpr wchar_t dialog_class[]=L"LearnGameAndEngineDev.CppProjectDialog";
constexpr UINT card_activated=WM_APP+235;
constexpr UINT dialog_completed=WM_APP+236;
std::string pick(std::string_view language,const char* es,const char* en,const char* fr){return language=="fr"?fr:language=="en"?en:es;}
std::string string(const JsonObject& object,std::wstring_view key,std::string_view fallback={}){return object.HasKey(key)?winrt::to_string(object.GetNamedString(key)):std::string(fallback);}
std::wstring native_text(HWND item){const auto size=GetWindowTextLengthW(item);std::wstring value(static_cast<std::size_t>(size)+1,L'\0');value.resize(static_cast<std::size_t>(GetWindowTextW(item,value.data(),static_cast<int>(value.size()))));return value;}
std::string local_date(std::int64_t milliseconds,std::string_view language){ULARGE_INTEGER ticks{};ticks.QuadPart=static_cast<std::uint64_t>(milliseconds)*10000+116444736000000000ULL;FILETIME utc{ticks.LowPart,ticks.HighPart},local{};SYSTEMTIME time{};
    if(!FileTimeToLocalFileTime(&utc,&local)||!FileTimeToSystemTime(&local,&time))throw std::runtime_error("Invalid revision timestamp");const auto locale=language=="fr"?L"fr-FR":language=="en"?L"en-US":L"es-MX";std::array<wchar_t,128> date{},clock{};
    if(!GetDateFormatEx(locale,DATE_SHORTDATE,&time,nullptr,date.data(),static_cast<int>(date.size()),nullptr)||!GetTimeFormatEx(locale,TIME_NOSECONDS,&time,nullptr,clock.data(),static_cast<int>(clock.size())))throw std::runtime_error("Cannot format revision date");return utf8(std::wstring(date.data())+L" "+clock.data());}
struct Dialog {
    HWND window=nullptr,owner=nullptr,heading=nullptr,body=nullptr,input=nullptr,error=nullptr,primary=nullptr,close=nullptr;
    HFONT font=nullptr;std::function<bool()> accept;std::function<void(int)> activate;std::function<void()> initialize;
    std::jthread worker;std::mutex completion_mutex;std::function<void()> completion;bool busy=false,primary_enabled=false,body_enabled=false;
    std::vector<HWND>* tracked=nullptr;std::string title,close_text,primary_text;bool code=false;
    int heading_height=0;
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(window)),96);}
    void refresh_font(){const auto next=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!next)throw std::runtime_error("Cannot create project dialog font");const auto previous=font;font=next;
        for(auto item=GetWindow(window,GW_CHILD);item;item=GetWindow(item,GW_HWNDNEXT))SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
        for(const auto item:{heading,body})if(item&&GetDlgCtrlID(item)!=505){CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE;format.yHeight=15*14;SendMessageW(item,EM_SETCHARFORMAT,SCF_ALL,reinterpret_cast<LPARAM>(&format));}
        if(previous)DeleteObject(previous);
    }
    HWND control(const wchar_t* type,std::string_view value,DWORD style,int id){const auto text=wide(value);auto result=CreateWindowExW(0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!result)throw std::runtime_error("Cannot create project dialog control");reader_theme::apply(result,reader_theme::Surface::popup);if(font)SendMessageW(result,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return result;}
    void async(std::function<std::function<void()>(std::stop_token)> work){
        if(busy)throw std::runtime_error("Project history is already working");if(worker.joinable())worker.join();busy=true;
        primary_enabled=primary&&IsWindowEnabled(primary);body_enabled=body&&IsWindowEnabled(body);if(primary)EnableWindow(primary,FALSE);if(body)EnableWindow(body,FALSE);
        const auto target=window;worker=std::jthread([this,target,work=std::move(work)](std::stop_token stop){std::function<void()> done;
            try{done=work(stop);}catch(...){const auto error=std::current_exception();done=[error]{std::rethrow_exception(error);};}
            if(stop.stop_requested())return;{std::lock_guard guard(completion_mutex);completion=std::move(done);}PostMessageW(target,dialog_completed,0,0);});
    }
    void layout(){if(!window)return;RECT area{};GetClientRect(window,&area);const auto margin=px(20),gap=px(12),footer=px(52);int top=margin;
        if(heading){SendMessageW(heading,EM_SETEVENTMASK,0,ENM_REQUESTRESIZE);MoveWindow(heading,margin,top,std::max(1L,area.right-margin*2),px(88),TRUE);
            SendMessageW(heading,EM_REQUESTRESIZE,0,0);const auto measured=std::max(px(24),heading_height);MoveWindow(heading,margin,top,std::max(1L,area.right-margin*2),measured,TRUE);top+=measured+gap;}
        if(input){MoveWindow(input,margin,top,std::max(1L,area.right-margin*2),px(40),TRUE);top+=px(48);}
        if(error){MoveWindow(error,margin,top,std::max(1L,area.right-margin*2),px(48),TRUE);top+=px(56);}
        if(body){MoveWindow(body,margin,top,std::max(1L,area.right-margin*2),std::max(px(40),static_cast<int>(area.bottom)-top-footer-gap-margin),TRUE);
            if(GetDlgCtrlID(body)==505){const auto dc=GetDC(body);const auto previous=SelectObject(dc,font);const auto count=SendMessageW(body,LB_GETCOUNT,0,0);
                for(LRESULT index=0;index<count;++index){const auto length=SendMessageW(body,LB_GETTEXTLEN,static_cast<WPARAM>(index),0);if(length<0)continue;std::wstring value(static_cast<std::size_t>(length)+1,L'\0');SendMessageW(body,LB_GETTEXT,static_cast<WPARAM>(index),reinterpret_cast<LPARAM>(value.data()));
                    RECT measured{0,0,std::max(1L,area.right-margin*2-px(32)),0};DrawTextW(dc,value.c_str(),static_cast<int>(length),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SendMessageW(body,LB_SETITEMHEIGHT,static_cast<WPARAM>(index),std::max(px(48),static_cast<int>(measured.bottom)+px(24)));}
                SelectObject(dc,previous);ReleaseDC(body,dc);}}
        const auto buttons_top=std::max(top,static_cast<int>(area.bottom)-footer-margin);const auto width=primary?std::max(1L,(area.right-margin*2-gap)/2):std::max(1L,area.right-margin*2);
        MoveWindow(close,margin,buttons_top,width,footer,TRUE);if(primary)MoveWindow(primary,margin+width+gap,buttons_top,width,footer,TRUE);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,message,wp,lp);try{LRESULT color{};if(reader_theme::control_color(message,wp,lp,color))return color;
            if(message==WM_DRAWITEM){const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);
                if(item.CtlType==ODT_LISTBOX&&item.CtlID==505&&item.itemID!=static_cast<UINT>(-1)){
                    const auto length=SendMessageW(self->body,LB_GETTEXTLEN,item.itemID,0);if(length<0)return FALSE;std::wstring value(static_cast<std::size_t>(length)+1,L'\0');SendMessageW(self->body,LB_GETTEXT,item.itemID,reinterpret_cast<LPARAM>(value.data()));
                    FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::popup));const auto previous=SelectObject(item.hDC,self->font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,reader_theme::foreground(window));auto bounds=item.rcItem;InflateRect(&bounds,-self->px(16),-self->px(12));
                    DrawTextW(item.hDC,value.c_str(),static_cast<int>(length),&bounds,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(item.hDC,previous);reader_theme::paint_keyboard_focus(self->body,item.hDC,item.rcItem,reader_theme::palette(window).accent);return TRUE;
                }
                if(reader_theme::draw_button(item))return TRUE;}
            if(message==WM_ERASEBKGND){RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,reader_theme::brush(reader_theme::Surface::popup));return TRUE;}
            if(message==WM_SIZE){self->layout();return 0;}
            if(message==WM_DPICHANGED||message==WM_DPICHANGED_AFTERPARENT){self->refresh_font();self->layout();return 0;}
            if(message==WM_NOTIFY&&reinterpret_cast<NMHDR*>(lp)->hwndFrom==self->heading&&reinterpret_cast<NMHDR*>(lp)->code==EN_REQUESTRESIZE){const auto size=reinterpret_cast<REQRESIZE*>(lp);self->heading_height=size->rc.bottom-size->rc.top+self->px(4);return 0;}
            if(message==WM_CLOSE||(message==WM_COMMAND&&LOWORD(wp)==IDCANCEL)){DestroyWindow(window);return 0;}
            if(message==WM_COMMAND&&LOWORD(wp)==IDOK){if(!self->busy&&self->accept&&self->accept()&&IsWindow(window))DestroyWindow(window);return 0;}
            if(message==card_activated){if(!self->busy&&self->activate)self->activate(static_cast<int>(SendMessageW(self->body,LB_GETCURSEL,0,0)));return 0;}
            if(message==dialog_completed){if(self->worker.joinable())self->worker.join();std::function<void()> done;{std::lock_guard guard(self->completion_mutex);done=std::move(self->completion);}self->busy=false;
                if(self->primary)EnableWindow(self->primary,self->primary_enabled);if(self->body)EnableWindow(self->body,self->body_enabled);if(done)done();return 0;}
            if(message==WM_NCDESTROY){if(self->tracked)std::erase(*self->tracked,window);self->window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);return DefWindowProcW(window,message,wp,lp);}
        }catch(const std::exception& failure){internal_message_box(window,wide(failure.what()).c_str(),wide(self->title).c_str(),MB_OK|MB_ICONERROR);}
        catch(const winrt::hresult_error& failure){internal_message_box(window,failure.message().c_str(),wide(self->title).c_str(),MB_OK|MB_ICONERROR);}return DefWindowProcW(window,message,wp,lp);
    }
    void create(HWND parent,std::vector<HWND>& windows){owner=parent;tracked=&windows;WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=dialog_class;
        if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register project dialog");const auto caption=wide(title);
        window=create_internal_window(owner,dialog_class,caption.c_str(),this);
        windows.push_back(window);reader_theme::apply(window,reader_theme::Surface::popup);
        refresh_font();
        close=control(L"BUTTON",close_text,WS_TABSTOP|BS_PUSHBUTTON,IDCANCEL);if(!primary_text.empty())primary=control(L"BUTTON",primary_text,WS_TABSTOP|BS_PUSHBUTTON,IDOK);
    }
    void run(){layout_internal_windows(GetAncestor(owner,GA_ROOT));layout();
        ShowWindow(window,SW_SHOW);SetFocus(input?input:body?body:close);if(initialize)initialize();MSG message{};
        while(window&&IsWindow(window)){const auto result=GetMessageW(&message,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(message.wParam));if(window)DestroyWindow(window);break;}
            if(dispatch_window_fullscreen_key(GetAncestor(window,GA_ROOT),message))continue;
            if(input&&message.message==WM_KEYDOWN&&message.wParam==VK_RETURN){SendMessageW(window,WM_COMMAND,IDOK,0);continue;}
            if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    }
    ~Dialog(){if(worker.joinable()){worker.request_stop();worker.join();}if(window&&IsWindow(window))DestroyWindow(window);if(font)DeleteObject(font);}
};
}
struct CppProjectView::Impl {
    struct Context {Impl* self;bool configuration;bool tabs=false;bool actions=false;};
    HWND parent=nullptr,host=nullptr,configuration=nullptr,source=nullptr,include_source=nullptr,create_file=nullptr,delete_file=nullptr,history_button=nullptr;
    HWND target=nullptr,options=nullptr,build_type=nullptr,configure=nullptr,build=nullptr,tab_strip=nullptr,actions_strip=nullptr;std::array<HWND,3> config_labels{};
    Context project_context{this,false},config_context{this,true},tabs_context{this,false,true},actions_context{this,false,true,true};windows::CodeEditor& editor;curriculum::Block original;CppProjectCallbacks callbacks;
    curriculum::CppProjectFiles files;std::vector<std::string> sources;std::map<std::string,std::string> indentation;std::map<std::string,std::int64_t> cursors;
    std::string selected,language;bool switching=true;int scale=100,tab_offset=0,tab_extent=0;HFONT font=nullptr,form_font=nullptr;std::vector<HWND> tabs,dialogs;std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    POINT tab_start{},action_start{};int tab_drag_offset=0,action_offset=0,action_extent=0,action_drag_offset=0;bool tab_down=false,tab_dragging=false,action_down=false,action_dragging=false;
    windows::CppProjectHistory history;
    int px(int n)const{return MulDiv(n,static_cast<int>(GetDpiForWindow(host)),96);}
    std::string ui(const char* es,const char* en,const char* fr)const{return pick(language,es,en,fr);}
    static std::int64_t cursor(const JsonObject& values,const std::string& path){const auto key=winrt::to_hstring(path);if(!values.HasKey(key))return 0;const auto number=values.GetNamedNumber(key);if(number<0||number>INT32_MAX||number!=static_cast<std::int64_t>(number))throw std::runtime_error("Invalid project cursor");return static_cast<std::int64_t>(number);}
    Impl(HWND owner,const curriculum::Block& block,HWND source_window,windows::CodeEditor& adapter,std::string_view saved,std::string_view saved_cmake,std::filesystem::path history_root,std::string identity,std::string locale,CppProjectCallbacks events)
        :parent(owner),source(source_window),editor(adapter),original(block),callbacks(std::move(events)),files(block.project_files),sources(block.source_files),language(std::move(locale)),history(std::move(history_root),identity){
        if(files.empty())throw std::invalid_argument("Project controls require authored projectFiles");if(original.cmake)for(const auto& [path,code]:files)if(curriculum::is_cpp_source_file(path))sources.push_back(path);
        for(const auto& [path,code]:files)indentation[path]=state::make_indentation_metadata(code,windows::predefined_code_document(code).indentation_starts);
        selected=std::ranges::any_of(files,[](const auto& entry){return entry.first=="main.cpp";})?"main.cpp":files.front().first;
        if(!saved.empty()){const auto value=JsonObject::Parse(winrt::to_hstring(saved));curriculum::CppProjectFiles restored;std::vector<std::string> selected_sources;
            for(const auto& item:value.GetNamedArray(L"files")){const auto entry=item.GetObject();restored.emplace_back(string(entry,L"path"),string(entry,L"code"));}curriculum::validate_cpp_project_files(restored);
            std::set<std::string> unique;for(const auto& item:value.GetNamedArray(L"sources")){auto path=winrt::to_string(item.GetString());if(!unique.insert(path).second||!curriculum::is_cpp_source_file(path)||std::ranges::none_of(restored,[&](const auto& entry){return entry.first==path;}))throw std::runtime_error("Invalid saved source selection");selected_sources.push_back(std::move(path));}
            files=std::move(restored);sources=std::move(selected_sources);indentation.clear();cursors.clear();const auto units=value.GetNamedObject(L"indentationV2",JsonObject{}),positions=value.GetNamedObject(L"cursors",JsonObject{});
            for(const auto& [path,code]:files){indentation[path]=string(units,winrt::to_hstring(path));cursors[path]=cursor(positions,path);}const auto active=string(value,L"selected");selected=std::ranges::any_of(files,[&](const auto& entry){return entry.first==active;})?active:files.front().first;
        }
        WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=proc;type.hInstance=GetModuleHandleW(nullptr);type.hCursor=LoadCursorW(nullptr,IDC_ARROW);type.lpszClassName=project_class;
        if(!RegisterClassExW(&type)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register project controls");
        host=CreateWindowExW(WS_EX_CONTROLPARENT,project_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),&project_context);if(!host)throw std::runtime_error("Cannot create project controls");reader_theme::apply(host,reader_theme::Surface::card);
        tab_strip=CreateWindowExW(WS_EX_CONTROLPARENT,project_class,L"",WS_CHILD|WS_VISIBLE|WS_HSCROLL,0,0,1,1,host,nullptr,GetModuleHandleW(nullptr),&tabs_context);if(!tab_strip)throw std::runtime_error("Cannot create project file tabs");reader_theme::apply(tab_strip,reader_theme::Surface::card);
        actions_strip=CreateWindowExW(WS_EX_CONTROLPARENT,project_class,L"",WS_CHILD|WS_VISIBLE|WS_HSCROLL,0,0,1,1,host,reinterpret_cast<HMENU>(304),GetModuleHandleW(nullptr),&actions_context);if(!actions_strip)throw std::runtime_error("Cannot create project action scrolling");reader_theme::apply(actions_strip,reader_theme::Surface::card);
        include_source=control(host,L"BUTTON","",BS_AUTOCHECKBOX|WS_TABSTOP,303);create_file=control(actions_strip,L"BUTTON","",BS_PUSHBUTTON|WS_TABSTOP,300);delete_file=control(actions_strip,L"BUTTON","",BS_PUSHBUTTON|WS_TABSTOP,301);history_button=control(actions_strip,L"BUTTON","",BS_PUSHBUTTON|WS_TABSTOP,302);
        for(const auto item:{create_file,delete_file,history_button})SetWindowSubclass(item,tab_proc,19,reinterpret_cast<DWORD_PTR>(this));
        if(original.cmake){configuration=CreateWindowExW(WS_EX_CONTROLPARENT,project_class,L"",WS_CHILD|WS_VISIBLE,0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),&config_context);if(!configuration)throw std::runtime_error("Cannot create CMake controls");reader_theme::apply(configuration,reader_theme::Surface::canvas);
            for(std::size_t i=0;i<config_labels.size();++i)config_labels[i]=control(configuration,L"STATIC","",0,410+static_cast<int>(i));
            target=control(configuration,MSFTEDIT_CLASS,"",ES_AUTOHSCROLL|WS_TABSTOP,400);options=control(configuration,MSFTEDIT_CLASS,"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,402);
            build_type=control(configuration,L"COMBOBOX","",CBS_DROPDOWNLIST|WS_TABSTOP,404);for(const auto* name:{L"Debug",L"Release",L"RelWithDebInfo",L"MinSizeRel"})SendMessageW(build_type,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
            configure=control(configuration,L"BUTTON","",BS_PUSHBUTTON|WS_TABSTOP,405);build=control(configuration,L"BUTTON","",BS_PUSHBUTTON|WS_TABSTOP,406);
            const auto settings=saved_cmake.empty()?JsonObject{}:JsonObject::Parse(winrt::to_hstring(saved_cmake));std::string option_text;for(const auto& [key,value]:original.cmake->options){if(!option_text.empty())option_text+='\n';option_text+=key+'='+value;}
            set_edit_text(target,string(settings,L"target",original.cmake->target));set_edit_text(options,string(settings,L"optionsText",option_text));const auto type_name=wide(string(settings,L"buildType",original.cmake->build_type));const auto at=SendMessageW(build_type,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(type_name.c_str()));SendMessageW(build_type,CB_SETCURSEL,at==CB_ERR?0:at,0);
        }
        set_language(language);set_text_scale(100);show_selected();switching=false;save();
    }
    ~Impl(){*alive=false;while(!dialogs.empty()){const auto item=dialogs.back();dialogs.pop_back();if(IsWindow(item))DestroyWindow(item);}if(configuration&&IsWindow(configuration))DestroyWindow(configuration);if(host&&IsWindow(host))DestroyWindow(host);if(font)DeleteObject(font);if(form_font)DeleteObject(form_font);}
    HWND control(HWND owner,const wchar_t* type,std::string_view value,DWORD style,int id){const auto label=wide(value);auto item=CreateWindowExW(0,type,label.c_str(),WS_CHILD|WS_VISIBLE|style,0,0,1,1,owner,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);if(!item)throw std::runtime_error("Cannot create project action");reader_theme::apply(item,owner==configuration?reader_theme::Surface::canvas:reader_theme::Surface::card);
        if(std::wstring_view(type)==MSFTEDIT_CLASS){SendMessageW(item,EM_SETEVENTMASK,0,ENM_CHANGE);SendMessageW(item,EM_EXLIMITTEXT,0,16*1024*1024);}return item;}
    bool capture(){if(switching)return false;const auto document=editor.document();auto current=std::ranges::find(files,selected,[](const auto& item)->const std::string&{return item.first;});if(current==files.end())throw std::runtime_error("Selected project file disappeared");const bool changed=current->second!=document.text;current->second=document.text;indentation[selected]=state::make_indentation_metadata(document.text,document.indentation_starts);cursors[selected]=document.selection_start;save();return changed;}
    void show_selected(){const auto before=switching;switching=true;const auto& code=std::ranges::find(files,selected,[](const auto& item)->const std::string&{return item.first;})->second;
        editor.reset({code,state::indentation_units(code,indentation[selected]),cursors[selected],cursors[selected]});
        for(const auto item:tabs)DestroyWindow(item);tabs.clear();for(std::size_t i=0;i<files.size();++i){const auto tab=control(tab_strip,L"BUTTON",files[i].first==selected?"["+files[i].first+"]":files[i].first,BS_PUSHBUTTON|WS_TABSTOP,1000+static_cast<int>(i));tabs.push_back(tab);SetWindowSubclass(tab,tab_proc,19,reinterpret_cast<DWORD_PTR>(this));}
        ShowWindow(include_source,!original.cmake&&curriculum::is_cpp_source_file(selected)?SW_SHOWNOACTIVATE:SW_HIDE);SendMessageW(include_source,BM_SETCHECK,std::ranges::find(sources,selected)!=sources.end()?BST_CHECKED:BST_UNCHECKED,0);
        switching=before;set_text_scale(scale);RECT area{};GetClientRect(host,&area);layout(area.right,false,true);save();
    }
    void save(){if(!callbacks.save)return;JsonObject value;JsonArray entries,selected_sources;JsonObject positions,units;for(const auto& [path,code]:files){JsonObject entry;entry.SetNamedValue(L"path",JsonValue::CreateStringValue(winrt::to_hstring(path)));entry.SetNamedValue(L"code",JsonValue::CreateStringValue(winrt::to_hstring(code)));entries.Append(entry);positions.SetNamedValue(winrt::to_hstring(path),JsonValue::CreateNumberValue(static_cast<double>(cursors[path])));units.SetNamedValue(winrt::to_hstring(path),JsonValue::CreateStringValue(winrt::to_hstring(indentation[path])));}
        for(const auto& path:sources)selected_sources.Append(JsonValue::CreateStringValue(winrt::to_hstring(path)));value.SetNamedValue(L"files",entries);value.SetNamedValue(L"sources",selected_sources);value.SetNamedValue(L"selected",JsonValue::CreateStringValue(winrt::to_hstring(selected)));value.SetNamedValue(L"cursors",positions);value.SetNamedValue(L"indentationV2",units);
        JsonObject config;if(configuration){config.SetNamedValue(L"target",JsonValue::CreateStringValue(winrt::to_hstring(edit_text(target))));config.SetNamedValue(L"optionsText",JsonValue::CreateStringValue(winrt::to_hstring(edit_text(options))));config.SetNamedValue(L"buildType",JsonValue::CreateStringValue(winrt::to_hstring(winrt::to_string(native_text(build_type)))));}
        callbacks.save(winrt::to_string(value.Stringify()),configuration?winrt::to_string(config.Stringify()):std::string{});
    }
    void notify(bool changed){save();if(callbacks.changed&&!switching)callbacks.changed(changed);}
    void set_language(std::string locale){language=std::move(locale);const auto label=[](HWND item,const std::string& text){if(item)SetWindowTextW(item,wide(text).c_str());};
        label(create_file,ui("Crear archivo","Create file","Créer un fichier"));label(delete_file,ui("Eliminar archivo","Delete file","Supprimer le fichier"));label(history_button,ui("Historial","History","Historique"));label(include_source,ui("Compilar este archivo fuente","Compile this source file","Compiler ce fichier source"));
        if(configuration){label(config_labels[0],ui("Objetivo CMake","CMake target","Cible CMake"));label(config_labels[1],ui("Opciones del proyecto (CLAVE=valor, una por línea)","Project options (NAME=value, one per line)","Options du projet (NOM=valeur, une par ligne)"));label(config_labels[2],ui("Tipo de construcción","Build type","Type de construction"));label(configure,ui("Configurar y generar","Configure and generate","Configurer et générer"));label(build,ui("Construir objetivo","Build target","Construire la cible"));}
    }
    void set_text_scale(int value){const auto previous=switching;switching=true;
        struct RestoreSwitch{bool& flag;bool value;~RestoreSwitch(){flag=value;}} restore{switching,previous};
        scale=std::clamp(value,50,300);const auto next=CreateFontW(-px(MulDiv(14,scale,100)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");if(next){const auto old=font;font=next;for(const auto item:tabs)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);for(const auto item:{include_source,create_file,delete_file,history_button,configure,build})if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(old)DeleteObject(old);}
        const auto form=CreateFontW(-px(MulDiv(18,scale,100)),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");if(form){const auto old=form_font;form_font=form;for(const auto item:{target,options,build_type})if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(form_font),TRUE);for(const auto item:config_labels)if(item)SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(form_font),TRUE);if(old)DeleteObject(old);}}
    int label_height(HWND item,int width)const{const auto dc=GetDC(item);const auto previous=SelectObject(dc,form_font);const auto text=native_text(item);RECT measured{0,0,std::max(1,width),0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,previous);ReleaseDC(item,dc);return std::max(px(MulDiv(26,scale,100)),static_cast<int>(measured.bottom)+px(4));}
    int tab_width(std::size_t index)const{const auto dc=GetDC(host);const auto previous=SelectObject(dc,font);SIZE extent{};const auto text=wide(files[index].first==selected?"["+files[index].first+"]":files[index].first);GetTextExtentPoint32W(dc,text.c_str(),static_cast<int>(text.size()),&extent);SelectObject(dc,previous);ReleaseDC(host,dc);return std::max(px(72),static_cast<int>(extent.cx)+px(28));}
    int action_width(HWND item)const{const auto dc=GetDC(item);const auto previous=SelectObject(dc,font);SIZE extent{};const auto caption=native_text(item);GetTextExtentPoint32W(dc,caption.c_str(),static_cast<int>(caption.size()),&extent);SelectObject(dc,previous);ReleaseDC(item,dc);return std::max(px(64),static_cast<int>(extent.cx)+px(32));}
    void layout_tabs(bool actions=false){const auto strip=actions?actions_strip:tab_strip;if(!strip)return;auto& offset=actions?action_offset:tab_offset;auto& extent=actions?action_extent:tab_extent;
        const auto items=actions?std::vector<HWND>{create_file,delete_file,history_button}:tabs;const auto item_width=[&](std::size_t i){return actions?action_width(items[i]):tab_width(i);};
        RECT area{};GetClientRect(strip,&area);extent=0;for(std::size_t i=0;i<items.size();++i)extent+=item_width(i)+px(8);if(extent)extent-=px(8);offset=std::clamp(offset,0,std::max(0,extent-static_cast<int>(area.right)));
        SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS};info.nMax=std::max(0,extent-1);info.nPage=static_cast<UINT>(std::max(0L,area.right));info.nPos=offset;SetScrollInfo(strip,SB_HORZ,&info,TRUE);reader_theme::refresh_scrollbars(strip);
        int x=-offset;const auto height=px(std::max(40,MulDiv(40,scale,100)));for(std::size_t i=0;i<items.size();++i){const auto width=item_width(i);MoveWindow(items[i],x,0,width,height,TRUE);x+=width+px(8);}}
    int layout(int width,bool config,bool arrange){const auto gap=px(8),height=px(std::max(40,MulDiv(40,scale,100)));int y=gap;
        const auto place=[&](HWND item,int x,int top,int w,int h){if(arrange)MoveWindow(item,x,top,std::max(1,w),std::max(1,h),TRUE);};
        if(config){for(std::size_t i=0;i<config_labels.size();++i){const auto label_size=label_height(config_labels[i],width);place(config_labels[i],0,y,width,label_size);y+=label_size+px(4);const auto field=i==0?target:i==1?options:build_type;const int field_height=i==1?px(MulDiv(80,scale,100)):height;place(field,0,y,width,field_height+(i==2?px(160):0));y+=field_height+gap;}for(const auto item:{configure,build}){place(item,0,y,width,height);y+=height+gap;}return y;}
        int total=0;for(std::size_t i=0;i<tabs.size();++i)total+=tab_width(i)+gap;if(total)total-=gap;const auto scrollbar=total>width-gap*2?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0;place(tab_strip,gap,y,width-gap*2,height+scrollbar);y+=height+scrollbar+gap;
        if(!original.cmake&&curriculum::is_cpp_source_file(selected)){place(include_source,gap,y,width-gap*2,height);y+=height+gap;}
        const int action_width_total=action_width(create_file)+action_width(delete_file)+action_width(history_button)+gap*2;
        const auto action_bar=action_width_total>width-gap*2?GetSystemMetricsForDpi(SM_CYHSCROLL,GetDpiForWindow(host)):0;
        place(actions_strip,gap,y,width-gap*2,height+action_bar);if(arrange)layout_tabs(true);return y+height+action_bar+gap;
    }
    void snapshot(windows::CppRunRequest& request){capture();request.project_files=files;request.source_files=original.cmake?std::vector<std::string>{}:sources;
        if(configuration){curriculum::CmakeConfig value;value.target=edit_text(target);value.options=curriculum::parse_cmake_options(edit_text(options));value.build_type=winrt::to_string(native_text(build_type));curriculum::validate_cmake_config(value);request.cmake=std::move(value);}else request.cmake.reset();}
    void reset(){const auto before=switching;switching=true;files=original.project_files;sources=original.source_files;indentation.clear();cursors.clear();if(original.cmake)for(const auto& [path,code]:files)if(curriculum::is_cpp_source_file(path))sources.push_back(path);
        for(const auto& [path,code]:files)indentation[path]=state::make_indentation_metadata(code,windows::predefined_code_document(code).indentation_starts);selected=std::ranges::any_of(files,[](const auto& entry){return entry.first=="main.cpp";})?"main.cpp":files.front().first;
        if(configuration){set_edit_text(target,original.cmake->target);std::string text;for(const auto& [key,value]:original.cmake->options){if(!text.empty())text+='\n';text+=key+'='+value;}set_edit_text(options,text);const auto name=wide(original.cmake->build_type);SendMessageW(build_type,CB_SETCURSEL,SendMessageW(build_type,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(name.c_str())),0);}show_selected();switching=before;notify(true);}
    void add_file();void open_history();
    static LRESULT CALLBACK tab_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data){auto& self=*reinterpret_cast<Impl*>(data);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,tab_proc,19);return DefSubclassProc(item,message,wp,lp);}
        const bool actions=GetParent(item)==self.actions_strip;const auto strip=actions?self.actions_strip:self.tab_strip;
        auto& start=actions?self.action_start:self.tab_start;auto& drag_offset=actions?self.action_drag_offset:self.tab_drag_offset;
        auto& offset=actions?self.action_offset:self.tab_offset;auto& down=actions?self.action_down:self.tab_down;auto& dragging=actions?self.action_dragging:self.tab_dragging;
        POINT point{static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};if(message==WM_LBUTTONDOWN){MapWindowPoints(item,strip,&point,1);start=point;drag_offset=offset;down=true;dragging=false;}
        else if(message==WM_MOUSEMOVE&&down&&(wp&MK_LBUTTON)){MapWindowPoints(item,strip,&point,1);if(std::abs(point.x-start.x)>self.px(6)){dragging=true;SendMessageW(item,WM_CANCELMODE,0,0);SetCapture(strip);offset=drag_offset-(point.x-start.x);self.layout_tabs(actions);return 0;}}
        else if(message==WM_LBUTTONUP){const bool dragged=dragging;down=false;dragging=false;if(dragged){SendMessageW(item,WM_CANCELMODE,0,0);return 0;}}
        return DefSubclassProc(item,message,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* context=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){context=static_cast<Context*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(context));}if(!context)return DefWindowProcW(window,message,wp,lp);auto& self=*context->self;
        try{LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;if(message==WM_DRAWITEM&&reader_theme::draw_button(*reinterpret_cast<DRAWITEMSTRUCT*>(lp)))return TRUE;
            if(message==WM_ERASEBKGND){RECT rect{};GetClientRect(window,&rect);FillRect(reinterpret_cast<HDC>(wp),&rect,reader_theme::brush(context->configuration?reader_theme::Surface::canvas:reader_theme::Surface::card));return TRUE;}
            if(message==WM_SIZE){RECT rect{};GetClientRect(window,&rect);if(context->tabs)self.layout_tabs(context->actions);else self.layout(rect.right,context->configuration,true);return 0;}
            if(context->tabs){auto& offset=context->actions?self.action_offset:self.tab_offset;auto& start=context->actions?self.action_start:self.tab_start;
                auto& drag_offset=context->actions?self.action_drag_offset:self.tab_drag_offset;auto& down=context->actions?self.action_down:self.tab_down;auto& dragging=context->actions?self.action_dragging:self.tab_dragging;
                if(message==WM_HSCROLL){SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,SB_HORZ,&info);int wanted=offset;switch(LOWORD(wp)){case SB_LINELEFT:wanted-=self.px(36);break;case SB_LINERIGHT:wanted+=self.px(36);break;case SB_PAGELEFT:wanted-=static_cast<int>(info.nPage);break;case SB_PAGERIGHT:wanted+=static_cast<int>(info.nPage);break;case SB_THUMBPOSITION:case SB_THUMBTRACK:wanted=info.nTrackPos;break;case SB_LEFT:wanted=0;break;case SB_RIGHT:wanted=info.nMax;break;}offset=wanted;self.layout_tabs(context->actions);return 0;}
                if(message==WM_MOUSEHWHEEL){offset+=static_cast<short>(HIWORD(wp))*self.px(36)/WHEEL_DELTA;self.layout_tabs(context->actions);return 0;}
                if(message==WM_LBUTTONDOWN){start={static_cast<short>(LOWORD(lp)),static_cast<short>(HIWORD(lp))};drag_offset=offset;down=true;dragging=false;SetCapture(window);return 0;}
                if(message==WM_MOUSEMOVE&&down&&(wp&MK_LBUTTON)){const auto x=static_cast<short>(LOWORD(lp));if(dragging||std::abs(x-start.x)>self.px(6)){dragging=true;offset=drag_offset-(x-start.x);self.layout_tabs(context->actions);}return 0;}
                if(message==WM_LBUTTONUP||message==WM_CAPTURECHANGED||message==WM_CANCELMODE){down=false;dragging=false;if(GetCapture()==window)ReleaseCapture();return 0;}
            }
            if(message==WM_COMMAND&&!self.switching){const auto id=LOWORD(wp),event=HIWORD(wp);if(context->configuration){if((id==400||id==402)&&event==EN_CHANGE){self.notify(true);return 0;}if(id==404&&event==CBN_SELCHANGE){self.notify(true);return 0;}if(event==BN_CLICKED&&(id==405||id==406)){if(self.callbacks.run)self.callbacks.run(id==405?windows::CppRunRequest::Action::configure:windows::CppRunRequest::Action::build);return 0;}}
                else if(event==BN_CLICKED){if(id>=1000&&static_cast<std::size_t>(id-1000)<self.files.size()){self.capture();self.selected=self.files[id-1000].first;self.show_selected();self.notify(false);SetFocus(self.source);return 0;}
                    if(id==303){self.capture();const bool included=SendMessageW(self.include_source,BM_GETCHECK,0,0)==BST_CHECKED;std::erase(self.sources,self.selected);if(included)self.sources.push_back(self.selected);self.notify(true);return 0;}
                    if(id==300){self.add_file();return 0;}if(id==301){if(self.files.size()==1)show_settings_notice(self.host,self.ui("Conserva al menos un archivo en el proyecto.","Keep at least one file in the project.","Conservez au moins un fichier dans le projet."));else{self.capture();const auto removed=self.selected;std::erase_if(self.files,[&](const auto& entry){return entry.first==removed;});std::erase(self.sources,removed);self.indentation.erase(removed);self.cursors.erase(removed);self.selected=self.files.front().first;self.show_selected();self.notify(true);}return 0;}if(id==302){self.open_history();return 0;}}}
        }catch(const std::exception& failure){internal_message_box(window,wide(failure.what()).c_str(),L"C++",MB_OK|MB_ICONERROR);}
        catch(const winrt::hresult_error& failure){internal_message_box(window,failure.message().c_str(),L"C++",MB_OK|MB_ICONERROR);}return DefWindowProcW(window,message,wp,lp);
    }
};
void CppProjectView::Impl::add_file(){const auto life=alive;Dialog dialog;dialog.title=ui("Crear archivo","Create file","Créer un fichier");dialog.close_text=ui("Cancelar","Cancel","Annuler");dialog.primary_text=ui("Aceptar","OK","OK");dialog.create(host,dialogs);
    dialog.input=dialog.control(MSFTEDIT_CLASS,"",ES_AUTOHSCROLL|WS_TABSTOP,501);set_edit_hint(dialog.input,"archivo.cpp");dialog.error=dialog.control(L"STATIC","",0,502);reader_theme::set_text_color(dialog.error,RGB(244,100,100));
    dialog.accept=[&,life]{if(!*life)return true;try{capture();const auto path=edit_text(dialog.input);curriculum::validate_cpp_project_path(path);auto candidate=files;candidate.emplace_back(path,"");curriculum::validate_cpp_project_files(candidate);files=std::move(candidate);indentation[path]=state::make_indentation_metadata("",{});if(curriculum::is_cpp_source_file(path))sources.push_back(path);selected=path;show_selected();notify(true);return true;}catch(const std::exception&){SetWindowTextW(dialog.error,wide(ui("Usa un nombre único y una ruta relativa dentro del proyecto.","Use a unique name and a relative path inside the project.","Utilisez un nom unique et un chemin relatif dans le projet.")).c_str());return false;}};dialog.run();}
void CppProjectView::Impl::open_history(){
    const auto life=alive;capture();
    Dialog root;root.title=ui("Historial","History","Historique");root.close_text=ui("Cerrar","Close","Fermer");
    root.primary_text=ui("Registrar revisión","Record revision","Enregistrer une révision");root.create(host,dialogs);
    root.heading=root.control(MSFTEDIT_CLASS,"",ES_MULTILINE|ES_READONLY,504);
    root.body=create_card_list(root.window,505,card_activated);SendMessageW(root.body,WM_SETFONT,reinterpret_cast<WPARAM>(root.font),TRUE);
    std::vector<windows::CppHistorySummary> revisions;
    const auto heading_style=[](HWND item){CHARFORMAT2W style{};style.cbSize=sizeof(style);style.dwMask=CFM_SIZE;style.yHeight=15*14;
        SendMessageW(item,EM_SETSEL,0,-1);SendMessageW(item,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&style));SendMessageW(item,EM_SETSEL,0,0);};
    const auto refresh=[&]{
        if(!*life)return;
        root.async([&,history_copy=history](std::stop_token stop)->std::function<void()>{
            auto loaded=history_copy.summaries(stop);
            return [&,loaded=std::move(loaded)]{
                if(!*life)return;revisions=loaded;
                set_rich_text(root.heading,revisions.empty()
                    ?ui("Todavía no hay revisiones. Registra los archivos actuales para poder compararlos o recuperar después un archivo concreto. El borrador se guarda por separado.",
                        "There are no revisions yet. Record the current files to compare them or recover an individual file later. The draft is saved separately.",
                        "Aucune révision pour le moment. Enregistrez les fichiers actuels pour les comparer ou récupérer un fichier précis plus tard. Le brouillon est sauvegardé séparément.")
                    :ui("Elige una revisión para comparar sus archivos con el borrador actual. Recuperar un archivo conserva los demás cambios. Una revisión guardada no es un programa construido.",
                        "Choose a revision to compare its files with the current draft. Recovering one file preserves other changes. A saved revision is not a built program.",
                        "Choisissez une révision pour comparer ses fichiers au brouillon actuel. Récupérer un fichier conserve les autres modifications. Une révision enregistrée n’est pas un programme construit."),{},false,RichTextRole::body,language);
                heading_style(root.heading);SendMessageW(root.body,LB_RESETCONTENT,0,0);
                for(const auto& revision:revisions){const auto label=wide(revision.label+"\n"+revision.id.substr(0,12)+" · "+local_date(revision.created_at,language));
                    SendMessageW(root.body,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
                root.layout();
            };
        });
    };
    root.accept=[&,life]{
        if(!*life)return true;
        Dialog prompt;prompt.title=root.primary_text;prompt.close_text=ui("Cancelar","Cancel","Annuler");prompt.primary_text=root.primary_text;
        prompt.create(root.window,dialogs);prompt.input=prompt.control(MSFTEDIT_CLASS,"",ES_AUTOHSCROLL|WS_TABSTOP,501);
        set_edit_hint(prompt.input,ui("Describe esta revisión","Describe this revision","Décrivez cette révision"));
        prompt.error=prompt.control(L"STATIC","",0,502);reader_theme::set_text_color(prompt.error,RGB(244,100,100));
        prompt.accept=[&,life]{
            if(!*life)return true;
            auto label=native_text(prompt.input);while(!label.empty()&&std::iswspace(label.front()))label.erase(label.begin());
            while(!label.empty()&&std::iswspace(label.back()))label.pop_back();
            if(label.empty()||label.size()>160){SetWindowTextW(prompt.error,wide(ui("Escribe una descripción de 1 a 160 caracteres.",
                "Enter a description of 1 to 160 characters.","Saisissez une description de 1 à 160 caractères.")).c_str());return false;}
            capture();prompt.async([&,history_copy=history,current=files,selected_sources=sources,units=indentation,name=utf8(label)](std::stop_token)->std::function<void()>{
                const auto saved=history_copy.record(current,selected_sources,name,units);
                const auto evidence=history_copy.recorded_evidence(saved);
                return [&,life,evidence]{
                    if(!*life)return;
                    if(callbacks.recorded_revision)callbacks.recorded_revision(evidence);
                    if(prompt.window)DestroyWindow(prompt.window);
                };
            });return false;
        };
        prompt.run();if(*life)refresh();return false;
    };
    root.activate=[&,life](int index){
        if(!*life||index<0||static_cast<std::size_t>(index)>=revisions.size())return;
        capture();root.async([&,history_copy=history,current=files,id=revisions[index].id](std::stop_token)->std::function<void()>{
            auto revision=history_copy.load(id);auto statuses=history_copy.statuses(current,revision);
            return [&,life,revision=std::move(revision),statuses=std::move(statuses)]{
                if(!*life)return;
                Dialog choices;choices.title=ui("Archivos de la revisión","Revision files","Fichiers de la révision");choices.close_text=root.close_text;
                choices.create(root.window,dialogs);choices.heading=choices.control(MSFTEDIT_CLASS,"",ES_MULTILINE|ES_READONLY,504);
                set_rich_text(choices.heading,revision.label+" · "+revision.id.substr(0,12)+"\n"+ui("Estado del borrador respecto a esta revisión:",
                    "Draft status relative to this revision:","État du brouillon par rapport à cette révision :"),{},false,RichTextRole::body,language);heading_style(choices.heading);
                choices.body=create_card_list(choices.window,505,card_activated);SendMessageW(choices.body,WM_SETFONT,reinterpret_cast<WPARAM>(choices.font),TRUE);
                for(const auto& item:statuses){const auto label=wide(item.path+"\n"+(item.change==windows::CppHistoryChange::unchanged?ui("Sin cambios","Unchanged","Inchangé")
                    :item.change==windows::CppHistoryChange::added?ui("Añadido en el borrador","Added in the draft","Ajouté au brouillon")
                    :item.change==windows::CppHistoryChange::removed?ui("Eliminado del borrador","Deleted from the draft","Supprimé du brouillon")
                    :ui("Modificado en el borrador","Modified in the draft","Modifié dans le brouillon")));
                    SendMessageW(choices.body,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));}
                choices.activate=[&,life](int file_index){
                    if(!*life||file_index<0||static_cast<std::size_t>(file_index)>=statuses.size())return;
                    capture();const auto status=statuses[file_index];
                    choices.async([&,history_copy=history,current=files,status,revision,locale=language](std::stop_token)->std::function<void()>{
                        const auto translate=[&](const char* es,const char* en,const char* fr){return pick(locale,es,en,fr);};
                        const auto verified=history_copy.diff_verified(status.path,current,revision);
                        const auto& differences=verified.lines;
                        std::string text;if(status.change==windows::CppHistoryChange::added)text=translate(
                            "Esta revisión no contiene este archivo. Recuperar su estado lo quitará únicamente del borrador actual.\n",
                            "This revision does not contain this file. Recovering its state will remove only this file from the current draft.\n",
                            "Cette révision ne contient pas ce fichier. Récupérer son état retirera uniquement ce fichier du brouillon actuel.\n");
                        text+=translate("− revisión registrada\n+ borrador actual\n","− recorded revision\n+ current draft\n","− révision enregistrée\n+ brouillon actuel\n");
                        for(const auto& line:differences){text+=line.type==windows::CppHistoryLineType::context?"  ":line.type==windows::CppHistoryLineType::added?"+ ":"− ";text+=line.text;
                            if(line.type!=windows::CppHistoryLineType::context&&(line.ending=="\r"||line.ending=="\r\n"))text+=line.ending=="\r\n"?" ⏎ CRLF":" ⏎ CR";
                            text+='\n';if(line.ending.empty())text+=translate("(Sin salto de línea final)\n","(No final newline)\n","(Pas de saut de ligne final)\n");}
                        return [&,life,status,revision,text=std::move(text),evidence=verified.evidence]{
                            if(!*life)return;
                            Dialog comparison;comparison.title=status.path;comparison.close_text=root.close_text;
                            comparison.primary_text=status.change==windows::CppHistoryChange::added
                                ?ui("Quitar este archivo del borrador","Remove this file from the draft","Retirer ce fichier du brouillon")
                                :ui("Recuperar este archivo","Recover this file","Récupérer ce fichier");
                            comparison.create(choices.window,dialogs);comparison.body=comparison.control(MSFTEDIT_CLASS,"",ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_HSCROLL|ES_AUTOHSCROLL|WS_TABSTOP,506);
                            SendMessageW(comparison.body,EM_EXLIMITTEXT,0,64*1024*1024);
                            set_rich_text(comparison.body,text,{},true);
                            CHARFORMAT2W format{};format.cbSize=sizeof(format);format.dwMask=CFM_SIZE|CFM_COLOR;format.yHeight=15*14;format.crTextColor=RGB(225,225,235);
                            SendMessageW(comparison.body,EM_SETSEL,0,-1);SendMessageW(comparison.body,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
                            SendMessageW(comparison.body,EM_SETSEL,0,0);SendMessageW(comparison.body,EM_SETBKGNDCOLOR,0,RGB(31,35,48));SendMessageW(comparison.body,EM_SETTARGETDEVICE,0,1);
                            EnableWindow(comparison.primary,status.change!=windows::CppHistoryChange::unchanged);
                            // Observation of the real diff window, not an award
                            // of curricular completion or evidence of a rerun.
                            if(callbacks.compared_file)callbacks.compared_file(evidence);
                            comparison.accept=[&,life,status,revision]{
                                if(!*life)return true;capture();const bool existed=std::ranges::any_of(files,[&](const auto& item){return item.first==status.path;});
                                comparison.async([&,history_copy=history,current=files,status,revision,existed,locale=language](std::stop_token)->std::function<void()>{
                                    auto recovery=history_copy.restore_file_verified(status.path,current,revision);
                                    if(!recovery.evidence.changed)throw std::runtime_error(pick(locale,
                                        "Esta versión ya coincide con el archivo guardado. Compara de nuevo antes de recuperar.",
                                        "The current file already matches this revision. Compare again before recovering.",
                                        "Le fichier actuel correspond déjà à cette révision. Comparez à nouveau avant de récupérer."));
                                    if(recovery.files.empty())throw std::runtime_error(pick(locale,"Conserva al menos un archivo en el proyecto.","Keep at least one file in the project.","Conservez au moins un fichier dans le projet."));
                                    return [&,life,status,revision,existed,recovery=std::move(recovery)]{
                                        if(!*life)return;files=std::move(recovery.files);
                                        if(std::ranges::any_of(files,[&](const auto& item){return item.first==status.path;})){
                                            const auto units=revision.indentation.find(status.path);indentation[status.path]=units==revision.indentation.end()?std::string{}:units->second;
                                            if(!existed&&std::ranges::find(revision.sources,status.path)!=revision.sources.end())sources.push_back(status.path);selected=status.path;
                                        }else{std::erase(sources,status.path);indentation.erase(status.path);cursors.erase(status.path);if(selected==status.path)selected=files.front().first;}
                                        show_selected();notify(true);
                                        // The callback follows the actual recovered draft save; it
                                        // never certifies comparison, execution or curricular progress.
                                        if(callbacks.restored_file)callbacks.restored_file(recovery.evidence);
                                        if(choices.window)DestroyWindow(choices.window);
                                    };
                                });return false;
                            };
                            comparison.run();
                        };
                    });
                };
                choices.run();if(*life)refresh();
            };
        });
    };
    root.initialize=refresh;root.run();
}
CppProjectView::CppProjectView(HWND parent,const curriculum::Block& block,HWND source,windows::CodeEditor& editor,std::string_view saved,std::string_view saved_cmake,std::filesystem::path history,std::string identity,std::string locale,CppProjectCallbacks callbacks)
    :impl_(std::make_unique<Impl>(parent,block,source,editor,saved,saved_cmake,std::move(history),std::move(identity),std::move(locale),std::move(callbacks))){}
CppProjectView::~CppProjectView()=default;
HWND CppProjectView::window()const{return impl_->host;}
HWND CppProjectView::configuration_window()const{return impl_->configuration;}
int CppProjectView::height_for_width(int width){const auto height=impl_->layout(width,false,true);SetWindowPos(impl_->host,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);return height;}
int CppProjectView::configuration_height_for_width(int width){if(!impl_->configuration)return 0;const auto height=impl_->layout(width,true,true);SetWindowPos(impl_->configuration,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);return height;}
bool CppProjectView::loading()const{return impl_->switching;}
bool CppProjectView::capture(){return impl_->capture();}
void CppProjectView::snapshot(windows::CppRunRequest& request){impl_->snapshot(request);}
void CppProjectView::reset(){impl_->reset();}
void CppProjectView::set_language(std::string language){impl_->set_language(std::move(language));}
void CppProjectView::set_text_scale(int scale){impl_->set_text_scale(scale);}
void CppProjectView::set_availability(bool busy,bool configured,bool built){if(impl_->configuration){EnableWindow(impl_->configure,!busy);EnableWindow(impl_->build,!busy&&configured);}(void)built;}
std::string CppProjectView::selected_file()const{return impl_->selected;}
}

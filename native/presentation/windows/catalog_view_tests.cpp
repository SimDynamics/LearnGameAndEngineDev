#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/card_list.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace lgaed;
using namespace presentation;
void check(bool condition,std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }
void pump() { MSG message{}; while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
int px(HWND window,int value){return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96);}
RECT relative_bounds(HWND item,HWND parent){RECT value{};GetWindowRect(item,&value);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&value),2);return value;}
std::wstring caption(HWND item){std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item))+1,L'\0');text.resize(GetWindowTextW(item,text.data(),static_cast<int>(text.size())));return text;}
void check_scrollbar_track(HWND list,std::string_view phase){
    SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_PAGE|SIF_POS};
    check(GetScrollInfo(list,SB_VERT,&range)!=FALSE&&range.nMax>=static_cast<int>(range.nPage),
        std::string("The catalog lost its actual scroll range after ")+std::string(phase));
    check(!(GetWindowLongPtrW(list,GWL_STYLE)&WS_VSCROLL),
        std::string("The catalog exposed its vertical scrollbar after ")+std::string(phase));
    RECT bounds{},client{};GetWindowRect(list,&bounds);GetClientRect(list,&client);
    check(client.right==bounds.right-bounds.left,"A hidden catalog scrollbar still reserves a blank vertical strip");
}
std::pair<COLORREF,bool> card_colors(HWND view,HWND list,std::size_t index,bool* percentage_ink=nullptr){
    RECT client{};GetClientRect(list,&client);
    const int width=static_cast<int>(client.right),height=static_cast<int>(SendMessageW(list,LB_GETITEMHEIGHT,index,0));
    const auto screen=GetDC(view),dc=CreateCompatibleDC(screen);
    BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);format.bmiHeader.biWidth=width;format.bmiHeader.biHeight=-height;
    format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
    const auto bitmap=CreateDIBSection(screen,&format,DIB_RGB_COLORS,&pixels,nullptr,0);ReleaseDC(view,screen);
    check(dc&&bitmap&&pixels,"Create the actual completed-catalog-card drawing surface");
    const auto previous=SelectObject(dc,bitmap);
    DRAWITEMSTRUCT item{};item.CtlType=ODT_LISTBOX;item.CtlID=4105;item.itemID=static_cast<UINT>(index);
    item.hwndItem=list;item.hDC=dc;item.rcItem={0,0,width,height};
    SendMessageW(view,WM_DRAWITEM,4105,reinterpret_cast<LPARAM>(&item));GdiFlush();
    const auto background=GetPixel(dc,px(view,5),px(view,5));
    bool green_ordinal=false;
    const std::uint32_t wanted=(105u<<16)|(220u<<8)|145u;
    const auto right=std::min(width,px(view,350)),bottom=std::min(height,px(view,62));
    for(int y=px(view,20);y<bottom&&!green_ordinal;++y)for(int x=px(view,20);x<right;++x)
        if((static_cast<const std::uint32_t*>(pixels)[static_cast<std::size_t>(y)*width+x]&0xffffffu)==wanted){green_ordinal=true;break;}
    if(percentage_ink){
        *percentage_ink=false;const auto face=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(list,WM_GETFONT,0,0)));
        TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,face);
        const auto end=height-px(view,14+20+8+9),start=std::max(0,end-static_cast<int>(metrics.tmHeight));
        for(int y=start;y<end&&!*percentage_ink;++y)for(int x=std::max(0,width-px(view,70));x<width-px(view,20);++x)
            if((static_cast<const std::uint32_t*>(pixels)[static_cast<std::size_t>(y)*width+x]&0xffffffu)==0xffffffu){*percentage_ink=true;break;}
    }
    SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
    return {background,green_ordinal};
}
std::wstring row_caption(HWND list,std::size_t index){
    const auto length=SendMessageW(list,LB_GETTEXTLEN,index,0);check(length!=LB_ERR,"Read real catalog card text length");
    std::wstring result(static_cast<std::size_t>(length)+1,L'\0');
    const auto read=SendMessageW(list,LB_GETTEXT,index,reinterpret_cast<LPARAM>(result.data()));check(read!=LB_ERR,"Read real catalog card text");result.resize(static_cast<std::size_t>(read));return result;
}
void location_notifications(HWND host,const std::filesystem::path& root){
    using curriculum::CatalogLocation;
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"catalog-notification-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false});
    check(!std::filesystem::exists(environment.paths.root),"The notification QA profile already exists");
    initialize_profile_directories(environment.paths);
    struct Observation{
        HWND view;
        CatalogLocation location;
        state::StateData saved;
        reader_theme::Palette palette;
        std::wstring title;
        LRESULT rows;
        bool back_enabled;
    };
    std::vector<Observation> events;
    const auto create=[&]{
        return create_catalog_view(host,environment,root,[](const auto&){},{},{},{},[&](HWND view){
            state::StateStore store(environment.paths.root);
            events.push_back({view,catalog_location(view),store.load(),reader_theme::palette(view),
                caption(GetDlgItem(view,4108)),SendMessageW(GetDlgItem(view,4105),LB_GETCOUNT,0,0),
                IsWindowEnabled(GetDlgItem(view,4101))!=FALSE});
        });
    };
    auto view=create();
    const auto expect=[&](std::size_t count,const CatalogLocation& location){
        check(events.size()==count,"Catalog navigation did not notify synchronously exactly once");
        const auto& event=events.back();
        check(event.view==view&&event.location==location&&catalog_location(view)==location,
            "The location callback observed the preceding catalog destination");
        check(state::string_value(event.saved,"native_windows","catalog/locale","es")==location.locale&&
            state::string_value(event.saved,"native_windows","catalog/route")==location.route&&
            state::string_value(event.saved,"native_windows","catalog/course")==location.course&&
            state::string_value(event.saved,"native_windows","catalog/unit")==location.unit,
            "The location callback ran before the destination was persisted");
        check(event.rows>0&&!event.title.empty()&&event.back_enabled==!location.route.empty(),
            "The location callback ran before the new catalog controls were applied");
        check(event.palette.high_contrast||event.palette.accent==reader_theme::appearance_accent(event.saved,location.route),
            "The location callback observed the preceding screen accent");
        if(location.route.empty())check(event.title==L"LearnGameAndEngineDev","The home notification retained the route heading");
    };
    const CatalogLocation home{"es",{},{},{}},programming{"es","programming",{},{}},math{"es","math_physics",{},{}};
    expect(1,home);
    {state::StateStore store(environment.paths.root);check(store.empty(),"An initial home notification wrote the empty profile");}
    navigate_catalog(view,programming);expect(2,programming);
    const auto saved=events.back().saved;
    refresh_catalog_view(view);
    check(events.size()==2,"Refreshing the same catalog location emitted a navigation event");
    {state::StateStore store(environment.paths.root);check(store.load()==saved,"The read-only notification refresh changed saved data");}
    navigate_catalog(view,programming);
    check(events.size()==2,"Re-rendering the same location emitted a navigation event");
    // This is the real Back button route. No message pump or timer may be
    // needed for a consumer to see home's accent after the click returns.
    SendMessageW(GetDlgItem(view,4101),BM_CLICK,0,0);expect(3,home);
    check(!navigate_catalog_back(view)&&events.size()==3,"Back at home emitted a location change");
    navigate_catalog(view,math);expect(4,math);
    navigate_catalog_home(view);expect(5,home);
    bool rejected=false;
    try{navigate_catalog(view,{"es","missing-qa-route",{},{}});}catch(const std::exception&){rejected=true;}
    check(rejected&&events.size()==5&&catalog_location(view)==home,"A rejected destination emitted a location event");
    navigate_catalog(view,programming);expect(6,programming);
    state::StateStore before(environment.paths.root);const auto restored=before.load();
    DestroyWindow(view);view=create();expect(7,programming);
    {state::StateStore after(environment.paths.root);check(after.load()==restored,"Restoring the callback's location rewrote saved data");}
    DestroyWindow(view);
}
// Exercise the error boundary with a private, minimal catalogue. The real
// curriculum and student profile never become test inputs to be rewritten.
void malformed_progress_keeps_other_units_available(HWND host) {
    const auto unique=L"lgaed-catalog-error-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64());
    const auto root=std::filesystem::temp_directory_path()/unique;
    check(!std::filesystem::exists(root),"The isolated catalog fixture path is occupied");
    struct TemporaryFixture {
        std::filesystem::path root;
        ~TemporaryFixture(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
    } fixture{root};
    const auto put=[&](std::string_view relative,std::string_view json){
        const auto destination=root/"es"/std::filesystem::path(relative);
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream out(destination,std::ios::binary);
        check(out.good(),"Cannot create isolated catalog fixture");
        out.write(json.data(),static_cast<std::streamsize>(json.size()));
        check(out.good(),"Cannot write isolated catalog fixture");
    };
    put("programming/courses.json",R"json([{"id":"cpp_fundamentals","number":1,"title":"Curso QA"}])json");
    put("programming/cpp_fundamentals/course_cpp_fundamentals.json",
        R"json({"id":"cpp_fundamentals","title":"Curso QA","units":[{"id":"cpp_fundamentals__defectos","number":1,"title":"Unidad con error"},{"id":"cpp_fundamentals__siguiente","number":2,"title":"Unidad disponible"}]})json");
    put("programming/cpp_fundamentals/defectos/unit_defectos.json",
        R"json({"courseId":"cpp_fundamentals","unitId":"cpp_fundamentals__defectos","title":"Unidad con error","lessons":[{"id":"cpp_fundamentals__defectos__roto","number":1,"title":"Leccion rota"},{"id":"cpp_fundamentals__defectos__valido","number":2,"title":"Leccion valida"}]})json");
    put("programming/cpp_fundamentals/siguiente/unit_siguiente.json",
        R"json({"courseId":"cpp_fundamentals","unitId":"cpp_fundamentals__siguiente","title":"Unidad disponible","lessons":[{"id":"cpp_fundamentals__siguiente__sano","number":1,"title":"Leccion sana"}]})json");
    put("programming/cpp_fundamentals/defectos/lesson_roto.json",
        R"json({"id":"cpp_fundamentals__defectos__roto","title":"Leccion rota","pages":[]})json");
    constexpr std::string_view valid=R"json({"id":"cpp_fundamentals__defectos__valido","title":"Leccion valida","pages":[{"blocks":[{"type":"paragraph","text":"Contenido valido."}]}]})json";
    put("programming/cpp_fundamentals/defectos/lesson_valido.json",valid);
    put("programming/cpp_fundamentals/siguiente/lesson_sano.json",
        R"json({"id":"cpp_fundamentals__siguiente__sano","title":"Leccion sana","pages":[{"blocks":[{"type":"paragraph","text":"Contenido valido."}]}]})json");
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"catalog-isolation-"+unique,false});
    check(!std::filesystem::exists(environment.paths.root),
        "The isolated catalog QA profile already exists");
    initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);
    auto data=store.load();
    data.preferences["learning_progress"]["lesson_cpp_fundamentals__defectos__roto_started"]=true;
    data.preferences["cpp_lesson_drafts"]["isolated-test-source"]=std::string("preserve");
    store.save(data);
    const auto before=store.load();
    auto view=create_catalog_view(host,environment,root,[](const auto&){});
    MoveWindow(view,0,0,px(view,950),px(view,760),TRUE);
    navigate_catalog(view,{"es","programming","cpp_fundamentals",{}});
    auto list=GetDlgItem(view,4105);
    check(SendMessageW(list,LB_GETCOUNT,0,0)==2,"The malformed lesson hid indexed unit cards");
    check(row_caption(list,0).find(L"progreso parcial")!=std::wstring::npos&&
        row_caption(list,0).find(L" — Abrir")==std::wstring::npos,
        "The broken unit was misrepresented as fully measured or just Abrir");
    check(row_caption(list,1).find(L"0 de 1 lecci")!=std::wstring::npos,
        "The intact following unit lost its completed/total counter");
    const auto notice=caption(GetDlgItem(view,4109));
    check(notice.find(L"No se pudo calcular")!=std::wstring::npos&&
        notice.find(L".json")==std::wstring::npos&&
        notice.find(L"\\") == std::wstring::npos,
        "The unit subtitle exposed a private error path or hid partial-progress status");
    navigate_catalog(view,{"es","programming","cpp_fundamentals","cpp_fundamentals__defectos"});
    list=GetDlgItem(view,4105);
    check(SendMessageW(list,LB_GETCOUNT,0,0)==2&&
        row_caption(list,0).find(L"Progreso de esta lecci")!=std::wstring::npos&&
        row_caption(list,1).find(L"0 de 1 p")!=std::wstring::npos,
        "One invalid lesson prevented its valid sibling from displaying progress");
    DestroyWindow(view);
    {
        // An unrelated UI/callback error can still reach the window procedure.
        // Never print its potentially private source path in the course header.
        auto protected_view=create_catalog_view(host,environment,root,
            [](const std::filesystem::path&){
                throw std::runtime_error("C:/private/student/lesson.json: Curriculum: malformed");
            });
        MoveWindow(protected_view,0,0,px(protected_view,950),px(protected_view,760),TRUE);
        navigate_catalog(protected_view,{"es","programming","cpp_fundamentals",
            "cpp_fundamentals__siguiente"});
        auto choices=GetDlgItem(protected_view,4105);
        check(SendMessageW(choices,LB_SETCURSEL,0,0)==0,
            "Select the known valid lesson for error-presentation QA");
        SendMessageW(choices,WM_KEYDOWN,VK_RETURN,1);pump();
        check(caption(GetDlgItem(protected_view,4109))==
            L"No se pudo actualizar el catálogo. Intenta abrirlo de nuevo.",
            "The catalog displayed raw exception details or an internal asset path");
        DestroyWindow(protected_view);
    }
    const auto after=store.load();
    check(after.preferences.at("learning_progress")==before.preferences.at("learning_progress")&&
        after.preferences.at("cpp_lesson_drafts")==before.preferences.at("cpp_lesson_drafts"),
        "A malformed catalog lesson modified student progress or drafts");
}

LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) { return DefWindowProcW(window,message,wparam,lparam); }
}
int main() {
    using curriculum::CatalogLocation;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW type{}; type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LgaedCatalogTests";
        check(RegisterClassW(&type) != 0,"Register native catalog QA");
        const auto environment = resolve_windows_environment({ProfileId::Codex,L"catalog-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()),false});
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists"); initialize_profile_directories(environment.paths);
        const auto root = std::filesystem::path(build::source_root) / "app/src/main/assets/content";
        const auto host = CreateWindowExW(0,type.lpszClassName,L"Catalog QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host != nullptr,"Create catalog QA host");
        MoveWindow(host,0,0,px(host,1000),px(host,850),TRUE);
        malformed_progress_keeps_other_units_available(host);
        {
            // The last card can end before the visible viewport. No Windows
            // default white rectangle may remain under the Android-style cards.
            auto empty_list=create_card_list(host,4998,WM_APP+300);
            reader_theme::apply(empty_list,reader_theme::Surface::screen);
            MoveWindow(empty_list,0,0,px(empty_list,240),px(empty_list,180),TRUE);
            const wchar_t* label=L"Tarjeta de prueba";
            check(SendMessageW(empty_list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label))!=LB_ERR,
                "Create one themed catalog card");
            InvalidateRect(empty_list,nullptr,TRUE);
            UpdateWindow(empty_list);
            const auto dc=GetDC(empty_list);
            check(dc!=nullptr,"Inspect the empty catalog viewport");
            const auto actual=GetPixel(dc,px(empty_list,30),px(empty_list,140));
            const auto expected=reader_theme::background(empty_list);
            ReleaseDC(empty_list,dc);
            DestroyWindow(empty_list);
            check(actual==expected,
                "The empty trailing catalog viewport kept a white/system background");
        }
        std::filesystem::path opened; unsigned launches = 0, options = 0,smo_launches=0;bool all_actions=false;
        auto create = [&] {
            auto view = create_catalog_view(host,environment,root,[&](const auto& path) { opened = path; ++launches; },
                [&](CatalogAction action,const CatalogLocation&) { if (action == CatalogAction::settings) ++options;if(action==CatalogAction::smo)++smo_launches; },
                [&](CatalogAction action,const CatalogLocation&) { return all_actions||action == CatalogAction::settings; });
            MoveWindow(view,0,0,px(view,980),px(view,800),TRUE); return view;
        };
        auto view = create(); ShowWindow(host,SW_SHOWNOACTIVATE); pump();
        auto list = GetDlgItem(view,4105);
        check(list && SendMessageW(list,LB_GETCOUNT,0,0) == 3,"Home must have two real routes and only the functional options action");
        check(!IsWindowVisible(GetDlgItem(view,4106)) && !IsWindowVisible(GetDlgItem(view,4107)) && !IsWindowVisible(GetDlgItem(view,4104)),"Mobile-style home gained a permanent Open/Info/language toolbar");
        {
            const auto title=GetDlgItem(view,4108),subtitle=GetDlgItem(view,4109),heading=GetDlgItem(view,4113);
            const auto title_bounds=relative_bounds(title,view),subtitle_bounds=relative_bounds(subtitle,view),heading_bounds=relative_bounds(heading,view),list_bounds=relative_bounds(list,view);
            RECT first{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));
            LOGFONTW title_face{},heading_face{},subtitle_face{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(title,WM_GETFONT,0,0)),sizeof(title_face),&title_face);
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)),sizeof(heading_face),&heading_face);
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(subtitle,WM_GETFONT,0,0)),sizeof(subtitle_face),&subtitle_face);
            check(-title_face.lfHeight==MulDiv(32,static_cast<int>(GetDpiForWindow(view)),96)&&-heading_face.lfHeight==MulDiv(22,static_cast<int>(GetDpiForWindow(view)),96)
                &&std::wstring(title_face.lfFaceName)==L"Roboto"&&std::wstring(heading_face.lfFaceName)==L"Roboto",
                "Catalog headings no longer match Android Roboto sp sizes at the actual DPI");
            check(subtitle_face.lfWeight==FW_NORMAL&&-subtitle_face.lfHeight==px(view,17),
                "The Android17sp normal subtitle became an unwanted bold heading");
            check(title_bounds.top==px(view,54)&&subtitle_bounds.top==title_bounds.bottom+px(view,8)
                &&heading_bounds.top==subtitle_bounds.bottom+px(view,36)&&list_bounds.top+first.top==heading_bounds.bottom+px(view,16)
                &&title_bounds.bottom<=subtitle_bounds.top&&subtitle_bounds.bottom<=heading_bounds.top&&heading_bounds.bottom<list_bounds.top+first.top,
                "Home headings do not preserve the Android XML margins and measured text height");
            check(SendMessageW(list,LB_GETITEMHEIGHT,0,0)==px(view,118)&&SendMessageW(list,LB_GETITEMHEIGHT,2,0)==px(view,82),
                "Home cards do not preserve Android's 100/64-DIP minimum height and 18-DIP separation");
        }
        {
            state::StateStore store(environment.paths.root); check(store.empty(),"Merely showing home made an empty profile nonempty");
        }
        {
            MoveWindow(view,0,0,px(view,640),px(view,300),TRUE);pump();
            const auto heading=GetDlgItem(view,4108);
            const auto before=relative_bounds(heading,view);RECT first{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));
            SendMessageW(heading,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);pump();
            const auto shift=card_list_scroll_offset(list);const auto after=relative_bounds(heading,view);RECT shifted{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&shifted));
            check(shift>0&&before.top-after.top==shift&&first.top-shifted.top==shift&&launches==0,
                "The home heading and route cards did not share wheel scrolling exactly as Android");
            set_card_list_scroll_offset(list,0);
            SendMessageW(heading,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,10));
            SendMessageW(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(30,0));
            SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(30,0));pump();
            check(card_list_scroll_offset(list)>0&&launches==0,"Dragging a catalog heading opened a course or failed to move its viewport");
            set_card_list_scroll_offset(list,0);MoveWindow(view,0,0,px(view,980),px(view,800),TRUE);pump();
        }
        {
            const auto original=reader_theme::palette(view).accent;
            reader_theme::set_accent(view,RGB(0,0,0));
            RECT first{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));RECT client{};GetClientRect(list,&client);
            const int width=static_cast<int>(client.right),height=static_cast<int>(first.bottom-first.top);
            const auto screen=GetDC(view),dc=CreateCompatibleDC(screen);BITMAPINFO format{};format.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);format.bmiHeader.biWidth=width;format.bmiHeader.biHeight=-height;
            format.bmiHeader.biPlanes=1;format.bmiHeader.biBitCount=32;format.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
            const auto bitmap=CreateDIBSection(screen,&format,DIB_RGB_COLORS,&pixels,nullptr,0);
            ReleaseDC(view,screen);check(dc&&bitmap,"Create the real home-card drawing surface");
            const auto previous=SelectObject(dc,bitmap);
            DRAWITEMSTRUCT card{};card.CtlType=ODT_LISTBOX;card.CtlID=4105;card.itemID=0;card.hwndItem=list;card.hDC=dc;card.rcItem={0,0,width,height};
            SendMessageW(view,WM_DRAWITEM,4105,reinterpret_cast<LPARAM>(&card));
            bool visible_ink=false;
            GdiFlush();for(std::size_t index=0;index<static_cast<std::size_t>(width)*static_cast<std::size_t>(height);++index){const auto color=static_cast<const std::uint32_t*>(pixels)[index];
                if(GetRValue(color)>128&&GetGValue(color)>128&&GetBValue(color)>128){visible_ink=true;break;}}
            SelectObject(dc,previous);DeleteObject(bitmap);DeleteDC(dc);
            const bool high_contrast=reader_theme::palette(view).high_contrast;reader_theme::set_accent(view,original);
            check(high_contrast||visible_ink,"A valid black home-card accent made the actual route text illegible");
        }
        SendMessageW(list,LB_SETCURSEL,2,0); SendMessageW(list,WM_KEYDOWN,VK_RETURN,1); pump();
        check(options == 1 && launches == 0,"Home tool action is decorative or opens curricular content");
        SendMessageW(list,LB_SETTOPINDEX,0,0);
        RECT route_card{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&route_card));
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,route_card.top+35));
        SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,route_card.top+35)); pump();
        check(catalog_location(view).route == "programming" && catalog_location(view).course.empty(),"One ordinary click did not open its route card");
        navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0) == 44,"Native catalog hid future indexed courses");
        check(caption(GetDlgItem(view,4109))==L"Ruta de programación, sistemas, gráficos y motores.",
            "The course selector replaced Android's route description with an invented count");
        {
            state::StateStore store(environment.paths.root);const auto original=store.load();
            for(const auto locale:{"es","en","fr"}){
                // Navigation legitimately saves catalog positions. Each test
                // preference edit must load that latest version, just like a
                // real settings window; the original snapshot is not a lease
                // to overwrite later state after the view has saved it.
                auto localized=store.load();localized.preferences["native_windows"]["ui_language"]=std::string(locale);store.save(localized);
                const CatalogLocation location{locale,"programming",{},{}},single{locale,"programming","systems_architecture_toolchain",{}};
                all_actions=true;navigate_catalog(view,{locale,{},{},{}});
                const auto compiler=std::string_view(locale)=="fr"?L"Ouvrir le prototype C++":std::string_view(locale)=="en"?L"Open C++ Prototype":L"Abrir prototipo C++";
                const auto subtitle=std::string_view(locale)=="fr"?L"Apprenez la programmation, l’informatique graphique, les moteurs, les mathématiques et la physique depuis leurs fondements.":
                    std::string_view(locale)=="en"?L"Learn programming, graphics, engines, mathematics, and physics from the ground up.":L"Aprende programación, gráficos, motores, matemáticas y física desde sus fundamentos.";
                const auto smo=std::string_view(locale)=="fr"?L"Laboratoire SMO":std::string_view(locale)=="en"?L"SMO Laboratory":L"Laboratorio SMO";
                check(SendMessageW(list,LB_GETCOUNT,0,0)==7&&row_caption(list,5)==compiler&&row_caption(list,6)==smo&&caption(GetDlgItem(view,4109))==subtitle,
                    "Home paraphrased original Android text or omitted a functional original action");
                const auto before_smo=smo_launches,before_lessons=launches;SendMessageW(list,LB_SETCURSEL,6,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,1);pump();
                check(smo_launches==before_smo+1&&launches==before_lessons,"The new SMO action did not reach its actual tool callback");
                all_actions=false;
                navigate_catalog(view,location);const auto courses=windows::load_catalog(root,location);
                const auto future=std::ranges::find_if(courses.entries,[](const auto& entry){return !entry.available;});
                check(future!=courses.entries.end(),"The real indexed catalog has no future-course fixture");
                const auto future_index=static_cast<std::size_t>(future-courses.entries.begin());
                const auto expected=std::string_view(locale)=="fr"?L"En développement · Bientôt disponible":std::string_view(locale)=="en"?L"In Development · Coming Soon":L"En desarrollo · Próximamente disponible";
                check(row_caption(list,future_index).ends_with(expected),"A future course was mislabeled as a missing installed package or lost its original translation");
                if(!reader_theme::palette(view).high_contrast){bool ready_ink=false,future_ink=true;(void)card_colors(view,list,0,&ready_ink);(void)card_colors(view,list,future_index,&future_ink);
                    check(ready_ink&&!future_ink,"A future course displayed a percentage or the real available-course percentage disappeared");}
                const auto before_notice=caption(GetDlgItem(view,4109));const auto launches_before=launches;
                SendMessageW(list,LB_SETCURSEL,future_index,0);SendMessageW(list,WM_KEYDOWN,VK_RETURN,1);pump();
                check(catalog_location(view)==location&&launches==launches_before&&caption(GetDlgItem(view,4109))==before_notice,
                    "Opening a future course navigated or replaced the fixed route description with a permanent Spanish error");
                navigate_catalog(view,single);
                const auto singular=std::string_view(locale)=="fr"?L"0 sur 1 leçon terminée":std::string_view(locale)=="en"?L"0 of 1 lesson completed":L"0 de 1 lección completada";
                check(row_caption(list,0).ends_with(singular),"A one-lesson unit lost Android's localized singular progress text");
            }
            auto restored=store.load();
            if(const auto previous=state::find(original,"native_windows","ui_language"))restored.preferences["native_windows"]["ui_language"]=*previous;
            else restored.preferences["native_windows"].erase("ui_language");
            store.save(restored);navigate_catalog(view,{"es","programming",{},{} });
        }
        {
            // A guard closes an unexpected legacy popup instead of letting a
            // regression hang inside TrackPopupMenu's nested message loop.
            const auto before=catalog_location(view);const auto actions=options;
            constexpr UINT_PTR guard=0x1042;
            SetTimer(host,guard,40,[](HWND target,UINT,UINT_PTR, DWORD){SetPropW(target,L"UnexpectedCatalogPopup",reinterpret_cast<HANDLE>(1));EndMenu();});
            SendMessageW(view,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(list),MAKELPARAM(-1,-1));KillTimer(host,guard);
            const auto unexpected=RemovePropW(host,L"UnexpectedCatalogPopup");
            check(!unexpected&&catalog_location(view)==before&&options==actions,"Right-click introduced non-Android catalog options or reset actions");
        }
        const auto row_height=SendMessageW(list,LB_GETITEMHEIGHT,0,0);
        check(row_height>=px(view,145)&&row_height<px(view,210),
            "Course cards no longer use Android's content-measured 20-DIP padding and 21sp titles");
        RECT first_card{},second_card{};
        SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first_card));
        SendMessageW(list,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&second_card));
        check(first_card.bottom==second_card.top && first_card.bottom-first_card.top==row_height,
            "Course cards overlap at the actual monitor DPI");
        {
            const auto title=GetDlgItem(view,4108);const auto before=relative_bounds(title,view);
            SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0);pump();
            const auto after=relative_bounds(title,view);
            check(card_list_scroll_offset(list)>0&&before.top==after.top,
                "The Android course selector's fixed heading moved with its RecyclerView cards");
            set_card_list_scroll_offset(list,0);
        }
        {
            const auto courses=windows::load_catalog(root,{"es","programming"});
            const auto scope=windows::catalog_scope(root,{"es","programming",courses.entries.front().id});
            state::StateStore store(environment.paths.root);const auto original=store.load();auto completed=original;
            for(const auto& id:scope.lessons)completed.preferences["learning_progress"]["lesson_"+id+"_completed"]=true;
            store.save(completed);refresh_catalog_view(view);pump();
            if(!reader_theme::palette(view).high_contrast){
                const auto [background,ordinal]=card_colors(view,list,0);
                check(background==RGB(29,78,52)&&ordinal,
                    "Completed course cards do not preserve Android's exact green background/ordinal colors");
            }
            state::StateStore reset(environment.paths.root);(void)reset.load();reset.save(original);
            refresh_catalog_view(view);pump();
            if(!reader_theme::palette(view).high_contrast){
                const auto [background,ordinal]=card_colors(view,list,0);
                check(background==RGB(32,33,38)&&!ordinal,
                    "A reused unfinished card retained completed green styling");
            }
        }
        {
            // A range change must not expose the native scrollbar, even at
            // 225% DPI. Wheel/drag use that same native range while it is hidden.
            RECT work{},original_host{};check(SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0)!=FALSE,"Read the visible catalog test area");
            GetWindowRect(host,&original_host);const auto original_view=relative_bounds(view,host);
            MoveWindow(host,work.left,work.top,work.right-work.left,work.bottom-work.top,TRUE);
            RECT host_client{};GetClientRect(host,&host_client);
            const int width=std::min(px(view,980),static_cast<int>(host_client.right));
            const int height=std::min(px(view,800),static_cast<int>(host_client.bottom));
            check(width>0&&height>0,"The visible catalog host has no usable client area");
            MoveWindow(view,0,0,width,height,TRUE);pump();
            refresh_catalog_view(view);check_scrollbar_track(list,"refreshing the indexed list");
            const auto original_height=SendMessageW(list,LB_GETITEMHEIGHT,0,0);
            SendMessageW(list,LB_SETITEMHEIGHT,0,original_height+1);check_scrollbar_track(list,"changing a native row height");
            SendMessageW(list,LB_SETITEMHEIGHT,0,original_height);
            SendMessageW(list,LB_SETTOPINDEX,15,0);check_scrollbar_track(list,"changing the native top index");
            SendMessageW(list,LB_SETTOPINDEX,0,0);
            MoveWindow(host,original_host.left,original_host.top,original_host.right-original_host.left,original_host.bottom-original_host.top,TRUE);
            MoveWindow(view,original_view.left,original_view.top,original_view.right-original_view.left,original_view.bottom-original_view.top,TRUE);pump();
        }
        {
            std::vector<LRESULT> wide_heights;for(WPARAM index=0;index<44;++index)wide_heights.push_back(SendMessageW(list,LB_GETITEMHEIGHT,index,0));
            MoveWindow(view,0,0,px(view,640),px(view,600),TRUE);pump();bool wrapped=false;
            for(std::size_t index=0;index<wide_heights.size();++index){const auto height=SendMessageW(list,LB_GETITEMHEIGHT,index,0);
                check(height>=wide_heights[index],"Narrowing the catalog clipped a course title or metadata instead of measuring its height");wrapped=wrapped||height>wide_heights[index];}
            SendMessageW(list,LB_SETTOPINDEX,0,0);
            const auto title=relative_bounds(GetDlgItem(view,4108),view),description=relative_bounds(GetDlgItem(view,4109),view),cards=relative_bounds(list,view);
            RECT first{};SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));
            check(wrapped&&title.bottom<=description.top&&description.bottom<cards.top+first.top&&cards.bottom>cards.top,
                "The compact catalog does not measure wrapped headings and cards in their shared viewport");
            MoveWindow(view,0,0,px(view,980),px(view,800),TRUE);pump();
        }
        SendMessageW(list,LB_SETTOPINDEX,15,0); SendMessageW(list,LB_SETCURSEL,17,0);
        set_card_list_scroll_offset(list,card_list_scroll_offset(list)+px(view,13));
        const auto saved_pixels=card_list_scroll_offset(list);
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(4105,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        const auto top = SendMessageW(list,LB_GETTOPINDEX,0,0);
        {
            const auto dpi=static_cast<int>(GetDpiForWindow(view));const auto scale=[dpi](int value){return MulDiv(value,dpi,96);};
            const auto old_list=relative_bounds(list,view);
            // Repainting the shared viewport must remove old pixels, including
            // its heading area, when returning from the scrolled course list.
            const auto sample=old_list.top+scale(12),sample_x=old_list.right-scale(8);
            const auto dc=GetDCEx(list,nullptr,DCX_CACHE);check(dc!=nullptr,"Acquire the actual old-list drawing surface");
            RECT seed{0,0,old_list.right-old_list.left,scale(24)};
            FillRect(dc,&seed,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));ReleaseDC(list,dc);
            const auto before=GetDCEx(view,nullptr,DCX_CACHE);const auto seeded=GetPixel(before,sample_x,sample);ReleaseDC(view,before);
            check(seeded==RGB(255,255,255),"The repaint fixture did not create its real old-list damage");
            navigate_catalog_home(view);pump();
            const auto home_list=relative_bounds(GetDlgItem(view,4105),view),heading=relative_bounds(GetDlgItem(view,4113),view);
            check(sample>=home_list.top&&sample<home_list.bottom,
                "The repaint fixture no longer samples the home viewport replacing the old course list");
            // Home's scrolling child now covers the full viewport. Use the same
            // unclipped surface as the damage fixture; GetDC(view) excludes it.
            const auto canvas=GetDCEx(view,nullptr,DCX_CACHE);check(canvas!=nullptr,"Acquire the repainted home drawing surface");
            const auto actual=GetPixel(canvas,sample_x,sample);ReleaseDC(view,canvas);
            check(actual!=CLR_INVALID,"The repaint fixture sampled a clipped or inaccessible pixel");
            check(actual==reader_theme::background(view),"Returning from courses left old list pixels in the exposed home canvas");
        }
        navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCURSEL,0,0) == 17 && SendMessageW(list,LB_GETTOPINDEX,0,0) == top,"Returning lost list selection/scroll");
        check(card_list_scroll_offset(list)==saved_pixels,"Returning discarded the partial pixel position of the shared catalog viewport");
        const auto before = catalog_location(view);
        SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0); pump();
        check(catalog_location(view) == before && launches == 0,"Mouse wheel opened a selected course");
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,160));
        SendMessageW(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(40,20));
        SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,20)); pump();
        check(catalog_location(view) == before && launches == 0,"Dragging the catalog triggered card activation");
        // A malformed progress asset must never leave later units with an
        // orphaned "Abrir" detail or a raw local JSON path in the subtitle.
        navigate_catalog(view,{"es","programming","cpp_fundamentals",{}});list=GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0)==12,"The course lost indexed units");
        for(const auto index:{10,11}) {
            const auto displayed=row_caption(list,index);
            check(displayed.find(L" — Abrir")==std::wstring::npos&&
                displayed.find(L"progreso parcial")==std::wstring::npos&&
                displayed.find(L"0 de ")!=std::wstring::npos,
                "U11/U12 did not calculate authentic progress after the historic-format repair");
        }
        check(caption(GetDlgItem(view,4109))==L"Unidades",
            "The restored unit menu still displayed an error instead of its ordinary subtitle");
        navigate_catalog(view,{"es","programming","cpp_fundamentals","cpp_fundamentals__programas_y_ejecucion"}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0) == 5,"Indexed lesson list missing");
        check(caption(GetDlgItem(view,4114))==L"Unidad 1"&&caption(GetDlgItem(view,4109))==L"Lecciones"
            &&IsWindowVisible(GetDlgItem(view,4102))&&IsWindowVisible(GetDlgItem(view,4103)),
            "The unit screen lacks Android's ordinal, Lessons subtitle, Home icon or Settings icon");
        const auto unit_options=options;SendMessageW(view,WM_COMMAND,MAKEWPARAM(4103,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(view,4103)));pump();
        check(options==unit_options+1,"The unit Settings icon opened a generic popup instead of the real scoped Settings action");
        SendMessageW(list,LB_SETCURSEL,1,0); SendMessageW(list,WM_KEYDOWN,VK_RETURN,1); pump();
        check(launches == 1 && opened.filename() == "lesson_primer_programa_y_salida.json","Enter opens another lesson or invents its path");
        const auto unit_location = catalog_location(view);
        DestroyWindow(view); view = create(); pump(); list = GetDlgItem(view,4105);
        check(catalog_location(view) == unit_location && SendMessageW(list,LB_GETCURSEL,0,0) == 1,"Recreating lost the real catalog parent/selection");
        {
            SendMessageW(GetDlgItem(view,4101),BM_CLICK,0,0);pump();
            check(catalog_location(view)==curriculum::catalog_parent(unit_location),"The existing Back icon did not use the catalog's real parent route");
            navigate_catalog(view,unit_location);check(navigate_catalog_back(view),"Back rejected a real lesson-list parent");
            check(catalog_location(view)==curriculum::catalog_parent(unit_location),"Back from lessons skipped the original units level");
            check(navigate_catalog_back(view)&&catalog_location(view)==CatalogLocation{"es","programming",{},{}},"Back from units skipped the original courses level");
            SendMessageW(view,WM_KEYDOWN,VK_ESCAPE,0);pump();check(catalog_location(view)==CatalogLocation{"es",{},{},{}},"Escape did not return the courses list to the real routes root");
            list=GetDlgItem(view,4105);SendMessageW(list,LB_SETCURSEL,2,0);SetFocus(list);
            const auto focused=GetFocus();const auto selection=SendMessageW(list,LB_GETCURSEL,0,0),position=SendMessageW(list,LB_GETTOPINDEX,0,0);
            state::StateStore before_store(environment.paths.root);const auto before_root=before_store.load();
            check(!navigate_catalog_back(view),"Back claimed to navigate above the routes root");SendMessageW(view,WM_KEYDOWN,VK_ESCAPE,0);pump();
            state::StateStore after_store(environment.paths.root);
            check(GetFocus()==focused&&SendMessageW(list,LB_GETCURSEL,0,0)==selection&&SendMessageW(list,LB_GETTOPINDEX,0,0)==position&&after_store.load()==before_root,"Back at the routes root changed focus, position or saved data");
            navigate_catalog(view,unit_location);list=GetDlgItem(view,4105);
            check(SendMessageW(list,LB_GETCURSEL,0,0)==1&&launches==1,"Back navigation lost the original lesson selection or launched content");
        }
        {
            state::StateStore store(environment.paths.root); auto data = store.load();
            const auto lesson = "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida";
            state::set_saved_page(data,lesson,3); data.preferences["learning_progress"][std::string("lesson_") + lesson + "_completed"] = true;
            data.preferences["native_windows"]["ui_language"] = std::string("fr");
            data.preferences["native_windows"]["content_language"] = std::string("es"); store.save(data);
        }
        refresh_catalog_view(view);
        check(catalog_location(view).locale == "es","Changing interface language changed the curriculum language");
        wchar_t caption[64]{}; GetWindowTextW(GetDlgItem(view,4106),caption,64);
        check(std::wstring(caption) == L"Continuer","Reopening ignored progress or independent French interface");
        {
            state::StateStore store(environment.paths.root); const auto data = store.load();
            check(state::saved_page(data,"cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida") == 3,"Catalog refresh reset lesson position");
        }
        {
            // A hidden catalogue is refreshed after each live Settings change.
            // It shares the profile with the still-alive lesson's StateStore.
            const auto lesson=windows::load_lesson(root/L"es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_texto_secuencia_y_formato.json","es");
            const auto reader=create_lesson_view(host,environment,lesson);check(flush_lesson_view(reader),"Cannot flush the concurrent reader fixture");
            const auto reader_before=read_lesson_state(reader);state::StateStore saved(environment.paths.root);const auto persisted=saved.load();
            list=GetDlgItem(view,4105);SendMessageW(list,LB_SETCURSEL,2,0);set_card_list_scroll_offset(list,px(view,60));
            const auto location=catalog_location(view);const auto selection=SendMessageW(list,LB_GETCURSEL,0,0);const auto scroll=card_list_scroll_offset(list);
            refresh_catalog_view(view);pump();
            check(catalog_location(view)==location&&SendMessageW(list,LB_GETCURSEL,0,0)==selection&&card_list_scroll_offset(list)==scroll,
                "Refreshing a hidden catalogue reset its live location, selection or scroll");
            state::StateStore after(environment.paths.root);check(after.load()==persisted,"Refreshing a catalogue wrote the lesson's shared profile");
            update_lesson_preferences(reader,[](auto& data){data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{120};});
            check(state::integer_value(read_lesson_state(reader),"lesson_text_size_settings","global_scale")==120,
                "A catalogue refresh invalidated the live lesson store before the next Settings save");
            check(read_lesson_state(reader).preferences["learning_progress"]==reader_before.preferences.at("learning_progress"),"A catalogue refresh or subsequent setting changed progress");
            state::StateStore other(environment.paths.root);auto external=other.load();external.preferences["native_windows"]["external_writer"]=true;other.save(external);
            bool rejected=false;try{update_lesson_preferences(reader,[](auto& data){data.preferences["lesson_text_size_settings"]["global_scale"]=std::int64_t{125};});}catch(const std::exception&){rejected=true;}
            state::StateStore verify(environment.paths.root);check(rejected&&verify.load()==external,"The catalogue fix bypassed rejection of a real stale external writer");
            DestroyWindow(reader);
        }
        DestroyWindow(view);location_notifications(host,root);
        DestroyWindow(host); UnregisterClassW(type.lpszClassName,type.hInstance);
        std::cout << "Native catalog: real home/actions, indices, keyboard/card ownership, mouse wheel/drag, list restoration, profile state and independent UI language: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

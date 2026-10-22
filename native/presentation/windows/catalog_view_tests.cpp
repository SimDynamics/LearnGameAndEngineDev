#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cstdint>
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
void check_scrollbar_track(HWND list,std::string_view phase){
    SCROLLBARINFO scrollbar{sizeof(scrollbar)};
    check(GetScrollBarInfo(list,OBJID_VSCROLL,&scrollbar)!=FALSE&&
        !(scrollbar.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)),
        "The catalog scrollbar regression did not expose its real vertical scrollbar");
    if(reader_theme::palette(list).high_contrast)return;
    RECT bounds{};GetWindowRect(list,&bounds);const auto dc=GetWindowDC(list);
    check(dc!=nullptr,"Acquire the real catalog nonclient drawing surface");
    const int sample_x=scrollbar.rcScrollBar.left-bounds.left+1;
    const int sample_y=(scrollbar.rcScrollBar.top+scrollbar.rcScrollBar.bottom)/2-bounds.top;
    const auto track=GetPixel(dc,sample_x,sample_y);RECT clip{};const auto clip_kind=GetClipBox(dc,&clip);ReleaseDC(list,dc);
    RECT parent_client{},work_area{};GetClientRect(GetParent(list),&parent_client);
    MapWindowPoints(GetParent(list),nullptr,reinterpret_cast<POINT*>(&parent_client),2);
    check(SystemParametersInfoW(SPI_GETWORKAREA,0,&work_area,0)!=FALSE,"Read the visible scrollbar sample area");
    const POINT screen_sample{bounds.left+sample_x,bounds.top+sample_y};
    check(PtInRect(&parent_client,screen_sample)!=FALSE,"The catalog scrollbar sample is outside its parent's actual client area");
    check(PtInRect(&work_area,screen_sample)!=FALSE,"The catalog scrollbar sample is outside the visible desktop area");
    const auto expected=reader_theme::background(list);
    if(track!=expected){
        RECT parent{},work{};GetWindowRect(GetParent(list),&parent);SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        const auto rgb=[](COLORREF value){std::cerr<<'('<<static_cast<unsigned>(GetRValue(value))<<','<<static_cast<unsigned>(GetGValue(value))<<','<<static_cast<unsigned>(GetBValue(value))<<')';};
        const auto rect=[](const RECT& value){std::cerr<<'['<<value.left<<','<<value.top<<','<<value.right<<','<<value.bottom<<']';};
        std::cerr<<"Catalog scrollbar diagnostic after "<<phase<<": actual=";rgb(track);std::cerr<<" actual-raw="<<track<<" expected=";rgb(expected);
        std::cerr<<" applied="<<reader_theme::applied(list)<<" dpi="<<GetDpiForWindow(list)<<" style="<<GetWindowLongPtrW(list,GWL_STYLE)
            <<" sample-window=("<<sample_x<<','<<sample_y<<") sample-screen=("<<sample_x+bounds.left<<','<<sample_y+bounds.top<<") window=";rect(bounds);
        std::cerr<<" parent=";rect(parent);std::cerr<<" work=";rect(work);std::cerr<<" bar=";rect(scrollbar.rcScrollBar);
        std::cerr<<" line="<<scrollbar.dxyLineButton<<" thumb=("<<scrollbar.xyThumbTop<<','<<scrollbar.xyThumbBottom<<") flags=";
        for(const auto flag:scrollbar.rgstate)std::cerr<<flag<<',';
        std::cerr<<" clip-kind="<<clip_kind<<" clip=";rect(clip);
        // Diagnostic only: retain the pixel captured before this direct refresh
        // as the oracle, even when manually repainting fixes the observed band.
        reader_theme::refresh_scrollbars(list);const auto refreshed_dc=GetWindowDC(list);
        const auto refreshed=refreshed_dc?GetPixel(refreshed_dc,sample_x,sample_y):CLR_INVALID;
        if(refreshed_dc)ReleaseDC(list,refreshed_dc);std::cerr<<" direct-refresh=";rgb(refreshed);std::cerr<<" direct-raw="<<refreshed<<'\n';
    }
    check(track!=CLR_INVALID,"The catalog scrollbar sample is not a readable real window pixel");
    check(track==expected,std::string("The catalog scrollbar retained a native white strip after ")+std::string(phase));
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
        std::filesystem::path opened; unsigned launches = 0, options = 0;
        auto create = [&] {
            auto view = create_catalog_view(host,environment,root,[&](const auto& path) { opened = path; ++launches; },
                [&](CatalogAction action,const CatalogLocation&) { if (action == CatalogAction::settings) ++options; },
                [](CatalogAction action,const CatalogLocation&) { return action == CatalogAction::settings; });
            MoveWindow(view,0,0,px(view,980),px(view,800),TRUE); return view;
        };
        auto view = create(); ShowWindow(host,SW_SHOWNOACTIVATE); pump();
        auto list = GetDlgItem(view,4105);
        const auto home_list_bounds=relative_bounds(list,view);
        check(list && SendMessageW(list,LB_GETCOUNT,0,0) == 3,"Home must have two real routes and only the functional options action");
        check(!IsWindowVisible(GetDlgItem(view,4106)) && !IsWindowVisible(GetDlgItem(view,4107)) && !IsWindowVisible(GetDlgItem(view,4104)),"Mobile-style home gained a permanent Open/Info/language toolbar");
        {
            const auto title=GetDlgItem(view,4108),subtitle=GetDlgItem(view,4109),heading=GetDlgItem(view,4113);
            const auto title_bounds=relative_bounds(title,view),subtitle_bounds=relative_bounds(subtitle,view),heading_bounds=relative_bounds(heading,view),list_bounds=relative_bounds(list,view);
            LOGFONTW title_face{},heading_face{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(title,WM_GETFONT,0,0)),sizeof(title_face),&title_face);
            GetObjectW(reinterpret_cast<HFONT>(SendMessageW(heading,WM_GETFONT,0,0)),sizeof(heading_face),&heading_face);
            check(-title_face.lfHeight==MulDiv(32,static_cast<int>(GetDpiForWindow(view)),72)&&-heading_face.lfHeight==MulDiv(22,static_cast<int>(GetDpiForWindow(view)),72),
                "Catalog headings no longer use the Android-derived typography and Settings point conversion");
            check(title_bounds.top==px(view,82)&&subtitle_bounds.top>=px(view,149)&&heading_bounds.top>=px(view,224)&&list_bounds.top>=px(view,283)
                &&title_bounds.bottom<=subtitle_bounds.top&&subtitle_bounds.bottom<=heading_bounds.top&&heading_bounds.bottom<list_bounds.top,
                "Measured home headings overlap or ignore the intended client-area proportions");
            check(SendMessageW(list,LB_GETITEMHEIGHT,0,0)>=px(view,152)&&SendMessageW(list,LB_GETITEMHEIGHT,2,0)>=px(view,93),
                "Route/action cards lost their readable body height and separate vertical spacing");
        }
        {
            state::StateStore store(environment.paths.root); check(store.empty(),"Merely showing home made an empty profile nonempty");
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
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,35));
        SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,35)); pump();
        check(catalog_location(view).route == "programming" && catalog_location(view).course.empty(),"One ordinary click did not open its route card");
        navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0) == 44,"Native catalog hid future indexed courses");
        const auto row_height=SendMessageW(list,LB_GETITEMHEIGHT,0,0);
        check(row_height>=MulDiv(210,static_cast<int>(GetDpiForWindow(view)),96),
            "High-DPI course cards lost their measured height");
        RECT first_card{},second_card{};
        SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first_card));
        SendMessageW(list,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&second_card));
        check(first_card.bottom==second_card.top && first_card.bottom-first_card.top==row_height,
            "Course cards overlap at the actual monitor DPI");
        {
            // Keep the real nonclient scrollbar on screen even at 225% DPI.
            // Range changes must repaint it themselves; do not call the theme
            // painter or force WM_NCPAINT before inspecting the actual pixels.
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
            const auto title=relative_bounds(GetDlgItem(view,4108),view),description=relative_bounds(GetDlgItem(view,4109),view),cards=relative_bounds(list,view);
            check(wrapped&&title.bottom<=description.top&&description.bottom<cards.top&&cards.bottom>cards.top,
                "The compact catalog does not grow wrapped titles while keeping header and scroll area separate");
            MoveWindow(view,0,0,px(view,980),px(view,800),TRUE);pump();
        }
        SendMessageW(list,LB_SETTOPINDEX,15,0); SendMessageW(list,LB_SETCURSEL,17,0);
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(4105,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        const auto top = SendMessageW(list,LB_GETTOPINDEX,0,0);
        {
            const auto dpi=static_cast<int>(GetDpiForWindow(view));const auto scale=[dpi](int value){return MulDiv(value,dpi,96);};
            auto old_list=relative_bounds(list,view);
            // Header heights now follow the measured title. Construct the same
            // historical 24-DIP exposed band using the actual child window,
            // rather than assuming that two particular titles place it there.
            const auto previous_top=home_list_bounds.top-scale(24);
            SetWindowPos(list,nullptr,old_list.left,previous_top,old_list.right-old_list.left,old_list.bottom-previous_top,
                SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|SWP_NOREDRAW);pump();old_list=relative_bounds(list,view);
            const auto sample=home_list_bounds.top-scale(12),sample_x=old_list.left+scale(8);
            const auto dc=GetDCEx(list,nullptr,DCX_CACHE);check(dc!=nullptr,"Acquire the actual old-list drawing surface");
            RECT seed{0,0,old_list.right-old_list.left,scale(24)};
            FillRect(dc,&seed,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));ReleaseDC(list,dc);
            const auto before=GetDCEx(view,nullptr,DCX_CACHE);const auto seeded=GetPixel(before,sample_x,sample);ReleaseDC(view,before);
            check(seeded==RGB(255,255,255),"The repaint fixture did not create its real old-list damage");
            navigate_catalog_home(view);pump();
            const auto home_list=relative_bounds(GetDlgItem(view,4105),view),heading=relative_bounds(GetDlgItem(view,4113),view);
            check(sample>=old_list.top&&sample<old_list.bottom&&sample>heading.bottom&&sample<home_list.top,
                "The repaint fixture no longer samples actual exposed parent canvas");
            const auto canvas=GetDC(view);const auto actual=GetPixel(canvas,sample_x,sample);ReleaseDC(view,canvas);
            check(actual==reader_theme::palette(view).canvas,"Returning from courses left old list pixels in the exposed home canvas");
        }
        navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCURSEL,0,0) == 17 && SendMessageW(list,LB_GETTOPINDEX,0,0) == top,"Returning lost list selection/scroll");
        const auto before = catalog_location(view);
        SendMessageW(list,WM_MOUSEWHEEL,MAKEWPARAM(0,-WHEEL_DELTA),0); pump();
        check(catalog_location(view) == before && launches == 0,"Mouse wheel opened a selected course");
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,160));
        SendMessageW(list,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(40,20));
        SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,20)); pump();
        check(catalog_location(view) == before && launches == 0,"Dragging the catalog triggered card activation");
        navigate_catalog(view,{"es","programming","cpp_fundamentals","cpp_fundamentals__programas_y_ejecucion"}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0) == 5,"Indexed lesson list missing");
        SendMessageW(list,LB_SETCURSEL,1,0); SendMessageW(list,WM_KEYDOWN,VK_RETURN,1); pump();
        check(launches == 1 && opened.filename() == "lesson_primer_programa_y_salida.json","Enter opens another lesson or invents its path");
        const auto unit_location = catalog_location(view);
        DestroyWindow(view); view = create(); pump(); list = GetDlgItem(view,4105);
        check(catalog_location(view) == unit_location && SendMessageW(list,LB_GETCURSEL,0,0) == 1,"Recreating lost the real catalog parent/selection");
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
        DestroyWindow(view); DestroyWindow(host); UnregisterClassW(type.lpszClassName,type.hInstance);
        std::cout << "Native catalog: real home/actions, indices, keyboard/card ownership, mouse wheel/drag, list restoration, profile state and independent UI language: PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

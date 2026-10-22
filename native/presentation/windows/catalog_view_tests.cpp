#include "presentation/windows/catalog_view.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windowsx.h>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace presentation;
void check(bool condition,std::string_view message) { if (!condition) throw std::runtime_error(std::string(message)); }
void pump() { MSG message{}; while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
LRESULT CALLBACK host_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) { return DefWindowProcW(window,message,wparam,lparam); }
}
int main() {
    using curriculum::CatalogLocation;
    try {
        WNDCLASSW type{}; type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"LgaedCatalogTests";
        check(RegisterClassW(&type) != 0,"Register native catalog QA");
        const auto environment = resolve_windows_environment({ProfileId::Codex,L"catalog-view-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()),false});
        check(!std::filesystem::exists(environment.paths.root),"QA profile already exists"); initialize_profile_directories(environment.paths);
        const auto root = std::filesystem::path(build::source_root) / "app/src/main/assets/content";
        const auto host = CreateWindowExW(0,type.lpszClassName,L"Catalog QA",WS_OVERLAPPEDWINDOW,0,0,1000,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        check(host != nullptr,"Create catalog QA host");
        std::filesystem::path opened; unsigned launches = 0, options = 0;
        auto create = [&] {
            auto view = create_catalog_view(host,environment,root,[&](const auto& path) { opened = path; ++launches; },
                [&](CatalogAction action,const CatalogLocation&) { if (action == CatalogAction::settings) ++options; },
                [](CatalogAction action,const CatalogLocation&) { return action == CatalogAction::settings; });
            MoveWindow(view,0,0,980,800,TRUE); return view;
        };
        auto view = create(); ShowWindow(host,SW_SHOWNOACTIVATE); pump();
        auto list = GetDlgItem(view,4105);
        check(list && SendMessageW(list,LB_GETCOUNT,0,0) == 3,"Home must have two real routes and only the functional options action");
        check(!IsWindowVisible(GetDlgItem(view,4106)) && !IsWindowVisible(GetDlgItem(view,4107)) && !IsWindowVisible(GetDlgItem(view,4104)),"Mobile-style home gained a permanent Open/Info/language toolbar");
        {
            state::StateStore store(environment.paths.root); check(store.empty(),"Merely showing home made an empty profile nonempty");
        }
        SendMessageW(list,LB_SETCURSEL,2,0); SendMessageW(list,WM_KEYDOWN,VK_RETURN,1); pump();
        check(options == 1 && launches == 0,"Home tool action is decorative or opens curricular content");
        SendMessageW(list,LB_SETTOPINDEX,0,0);
        SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,35));
        SendMessageW(list,WM_LBUTTONUP,0,MAKELPARAM(40,35)); pump();
        check(catalog_location(view).route == "programming" && catalog_location(view).course.empty(),"One ordinary click did not open its route card");
        navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
        check(SendMessageW(list,LB_GETCOUNT,0,0) == 44,"Native catalog hid future indexed courses");
        SendMessageW(list,LB_SETTOPINDEX,15,0); SendMessageW(list,LB_SETCURSEL,17,0);
        SendMessageW(view,WM_COMMAND,MAKEWPARAM(4105,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
        const auto top = SendMessageW(list,LB_GETTOPINDEX,0,0);
        navigate_catalog_home(view); navigate_catalog(view,{"es","programming",{},{}}); list = GetDlgItem(view,4105);
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

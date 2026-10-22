#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/icons.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
using namespace curriculum;
constexpr wchar_t catalog_class[] = L"LearnGameAndEngineDev.CatalogView";
constexpr int back_id = 4101, home_id = 4102, menu_id = 4103, language_id = 4104;
constexpr int list_id = 4105, open_id = 4106, info_id = 4107, title_id = 4108, status_id = 4109;
constexpr int settings_id = 4110, audio_id = 4111;
constexpr int reset_id = 4112;
constexpr int routes_heading_id = 4113;
constexpr UINT activate_message = WM_APP + 141;
struct CatalogView;
struct ListGesture { bool down = false, dragging = false; POINT start{}; int top = 0, item = -1; };
LRESULT CALLBACK catalog_proc(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK list_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,UINT_PTR,DWORD_PTR data) {
    auto& gesture = *reinterpret_cast<ListGesture*>(data);
    if (message == WM_KEYDOWN && wparam == VK_RETURN) { PostMessageW(GetParent(window),activate_message,0,0); return 0; }
    if (message == WM_LBUTTONDOWN) {
        const auto hit = SendMessageW(window,LB_ITEMFROMPOINT,0,lparam);
        if (HIWORD(hit)) return DefSubclassProc(window,message,wparam,lparam);
        gesture = {true,false,{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)},static_cast<int>(SendMessageW(window,LB_GETTOPINDEX,0,0)),LOWORD(hit)};
        SetFocus(window); SetCapture(window); SendMessageW(window,LB_SETCURSEL,gesture.item,0);
        SendMessageW(GetParent(window),WM_COMMAND,MAKEWPARAM(list_id,LBN_SELCHANGE),reinterpret_cast<LPARAM>(window));
        return 0;
    }
    if (message == WM_MOUSEMOVE && gesture.down) {
        const auto displacement = gesture.start.y - GET_Y_LPARAM(lparam);
        if (std::abs(displacement) > GetSystemMetrics(SM_CYDRAG) ||
            std::abs(gesture.start.x - GET_X_LPARAM(lparam)) > GetSystemMetrics(SM_CXDRAG)) gesture.dragging = true;
        if (gesture.dragging) {
            const auto height = std::max<LRESULT>(1,SendMessageW(window,LB_GETITEMHEIGHT,gesture.top,0));
            const auto count = SendMessageW(window,LB_GETCOUNT,0,0);
            if (count > 0) SendMessageW(window,LB_SETTOPINDEX,std::clamp(static_cast<int>(gesture.top) + static_cast<int>(displacement) / static_cast<int>(height),0,static_cast<int>(count - 1)),0);
        }
        return 0;
    }
    if (message == WM_LBUTTONUP && gesture.down) {
        const auto hit = SendMessageW(window,LB_ITEMFROMPOINT,0,lparam);
        const bool click = !gesture.dragging && !HIWORD(hit) && LOWORD(hit) == gesture.item;
        gesture.down = false; if (GetCapture() == window) ReleaseCapture();
        if (click) PostMessageW(GetParent(window),activate_message,0,0);
        return 0;
    }
    if (message == WM_CAPTURECHANGED || message == WM_CANCELMODE || message == WM_MOUSEWHEEL) gesture.down = false;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window,list_proc,1);
    return DefSubclassProc(window,message,wparam,lparam);
}
struct CatalogView {
    HWND window = nullptr, title = nullptr, status = nullptr, list = nullptr, back = nullptr, home = nullptr;
    HWND menu = nullptr, language = nullptr, open_button = nullptr, info_button = nullptr, routes_heading = nullptr;
    HFONT font = nullptr, heading_font = nullptr;
    WindowsEnvironment environment;
    std::filesystem::path root;
    CatalogPage page;
    state::StateData progress;
    std::function<void(const std::filesystem::path&)> on_open;
    CatalogActionHandler on_action;
    CatalogActionAvailability can_action;
    bool updating = false, owned_by_window = false;
    std::vector<CatalogAction> home_actions;
    std::vector<CatalogProgress> completion;
    std::vector<CatalogScope> scopes;
    std::string ui_locale = "es";
    CatalogLocation menu_context;
    ListGesture gesture;

    CatalogView(const WindowsEnvironment& env,std::filesystem::path content,
        std::function<void(const std::filesystem::path&)> callback,CatalogActionHandler action,CatalogActionAvailability availability)
        : environment(env),root(std::move(content)),on_open(std::move(callback)),on_action(std::move(action)),can_action(std::move(availability)) {
        if (!on_open) throw std::invalid_argument("El catálogo necesita una acción para abrir lecciones.");
    }
    ~CatalogView() { if (font) DeleteObject(font); if (heading_font) DeleteObject(heading_font); }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    std::string ui(std::string_view es,std::string_view en,std::string_view fr) const {
        return std::string(ui_locale == "fr" ? fr : ui_locale == "en" ? en : es);
    }
    COLORREF accent() const { return reader_theme::palette().high_contrast ? reader_theme::palette().accent :
        page.location.route == "programming" ? RGB(217,43,70) : RGB(77,211,224); }
    std::string action_label(CatalogAction action) const {
        switch (action) {
        case CatalogAction::settings: return ui("Opciones globales","Global options","Options générales");
        case CatalogAction::audio: return "Audio";
        case CatalogAction::calculator: return ui("Abrir calculadora científica","Open scientific calculator","Ouvrir la calculatrice scientifique");
        case CatalogAction::compiler: return ui("Laboratorio C++","C++ laboratory","Laboratoire C++");
        case CatalogAction::graphics: return ui("Motor gráfico","Graphics engine","Moteur graphique");
        }
        return {};
    }
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int id) {
        auto item = CreateWindowExW(0,type,text,WS_CHILD | WS_VISIBLE | style,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if (!item) throw std::runtime_error("No se pudo crear el catálogo nativo.");
        reader_theme::apply(item,reader_theme::Surface::canvas); return item;
    }
    state::StateData load_state() const { state::StateStore store(environment.paths.root); return store.load(); }
    void write_position(bool include_current = true) {
        state::StateStore store(environment.paths.root); auto data = store.load();
        auto& prefs = data.preferences["native_windows"];
        const auto key = catalog_position_key(page.location);
        const auto selection = SendMessageW(list,LB_GETCURSEL,0,0), top = SendMessageW(list,LB_GETTOPINDEX,0,0);
        if (selection != LB_ERR) prefs[key + "/selection"] = static_cast<std::int64_t>(selection);
        if (top != LB_ERR) prefs[key + "/top"] = static_cast<std::int64_t>(top);
        if (include_current) {
            prefs["catalog/locale"] = page.location.locale; prefs["catalog/route"] = page.location.route;
            prefs["catalog/course"] = page.location.course; prefs["catalog/unit"] = page.location.unit;
            prefs["content_language"] = page.location.locale;
        }
        store.save(data);
    }
    CatalogLocation restored_location() {
        progress = load_state();
        CatalogLocation result;
        result.locale = state::string_value(progress,"native_windows","content_language",
            state::string_value(progress,"native_windows","catalog/locale","es"));
        result.route = state::string_value(progress,"native_windows","catalog/route");
        result.course = state::string_value(progress,"native_windows","catalog/course");
        result.unit = state::string_value(progress,"native_windows","catalog/unit");
        try { validate_catalog_location(result); (void)windows::load_catalog(root,result); }
        catch (...) { result = {}; }
        return result;
    }
    std::optional<std::size_t> selected() const {
        const auto index = SendMessageW(list,LB_GETCURSEL,0,0);
        if (index == LB_ERR || static_cast<std::size_t>(index) >= page.entries.size() + home_actions.size()) return {};
        return static_cast<std::size_t>(index);
    }
    std::string label(const CatalogEntry& entry) const {
        return (entry.number ? std::to_string(entry.number) + ". " : "") + entry.title;
    }
    std::string detail(const CatalogEntry& entry) const {
        if (!entry.available) return ui("Contenido todavía no instalado","Content not installed yet","Contenu pas encore installé");
        if (catalog_level(page.location) == CatalogLevel::lessons) {
            if (catalog_lesson_completed(progress,entry.id)) return ui("Completada","Completed","Terminée");
            if (catalog_lesson_started(progress,entry.id)) return ui("En curso · continuar","In progress · continue","En cours · continuer");
            return ui("Abrir lección","Open lesson","Ouvrir la leçon");
        }
        if (catalog_level(page.location) == CatalogLevel::routes) return ui("Ver cursos","View courses","Voir les cours");
        const auto index = static_cast<std::size_t>(&entry - page.entries.data());
        if (index < completion.size()) {
            const auto snapshot = completion[index];
            return std::to_string(snapshot.completed) + ui(" de "," of "," sur ") + std::to_string(snapshot.total) +
                ui(" lecciones completadas"," lessons completed"," leçons terminées");
        }
        return ui("Abrir","Open","Ouvrir");
    }
    void update_selection() {
        const auto index = selected(); EnableWindow(open_button,index.has_value()); EnableWindow(info_button,index.has_value());
        bool resume = false;
        if (index && *index < page.entries.size() && catalog_level(page.location) == CatalogLevel::lessons)
            resume = catalog_lesson_started(progress,page.entries[*index].id);
        SetWindowTextW(open_button,wide(resume ? ui("Continuar","Continue","Continuer") : ui("Abrir","Open","Ouvrir")).c_str());
    }
    void render(CatalogLocation location,bool remember = true) {
        auto loaded = windows::load_catalog(root,location);
        if (remember && list) write_position(false);
        updating = true; page = std::move(loaded); progress = load_state();
        struct Restore { bool& updating; ~Restore() { updating = false; } } restore{updating};
        home_actions.clear(); completion.clear(); scopes.clear();
        wchar_t system[LOCALE_NAME_MAX_LENGTH]{}; GetUserDefaultLocaleName(system,LOCALE_NAME_MAX_LENGTH);
        const auto system_language = utf8(system).substr(0,2);
        ui_locale = state::string_value(progress,"native_windows","ui_language",system_language == "es" || system_language == "fr" ? system_language : "en");
        if (ui_locale != "es" && ui_locale != "fr" && ui_locale != "en") ui_locale = "en";
        menu_context = page.location;
        for (const auto& entry : page.entries) {
            CatalogScope scope;
            if (entry.available && catalog_level(location) != CatalogLevel::routes) {
                if (catalog_level(location) == CatalogLevel::lessons) scope.lessons.push_back(entry.id);
                else scope = windows::catalog_scope(root,catalog_child(location,entry.id));
            }
            completion.push_back(catalog_progress(progress,scope)); scopes.push_back(std::move(scope));
        }
        if (catalog_level(location) == CatalogLevel::routes) for (const auto action :
            {CatalogAction::settings,CatalogAction::calculator,CatalogAction::graphics,CatalogAction::compiler})
            if (action_available(action)) home_actions.push_back(action);
        SetWindowTextW(back,wide(ui("Volver","Back","Retour")).c_str()); SetWindowTextW(home,wide(ui("Inicio","Home","Accueil")).c_str());
        SetWindowTextW(menu,wide(ui("Menú","Menu","Menu")).c_str()); SetWindowTextW(info_button,wide(ui("Información","Information","Informations")).c_str());
        const auto display_title = catalog_level(location) == CatalogLevel::routes ? "LearnGameAndEngineDev" :
            catalog_level(location) == CatalogLevel::courses ? location.route == "programming" ? ui("Programación Avanzada y Arquitectura de Motores",
            "Advanced Programming and Engine Architecture","Programmation avancée et architecture des moteurs") : ui("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique") : page.title;
        SetWindowTextW(title,wide(display_title).c_str());
        const auto status_text = std::to_string(page.entries.size()) + (catalog_level(location) == CatalogLevel::routes ? ui(" rutas"," paths"," parcours") :
            catalog_level(location) == CatalogLevel::courses ? ui(" cursos"," courses"," cours") : catalog_level(location) == CatalogLevel::units ? ui(" unidades"," units"," unités") : ui(" lecciones"," lessons"," leçons"));
        SetWindowTextW(status,wide(catalog_level(location) == CatalogLevel::routes ? ui("Aprende programación, gráficos, motores, matemáticas y física desde sus fundamentos.",
            "Learn programming, graphics, engines, mathematics and physics from their foundations.",
            "Apprenez la programmation, les graphismes, les moteurs, les mathématiques et la physique depuis leurs bases.") : status_text).c_str());
        SetWindowTextW(routes_heading,wide(ui("Rutas de aprendizaje","Learning paths","Parcours d'apprentissage")).c_str());
        SendMessageW(list,LB_RESETCONTENT,0,0);
        for (const auto& entry : page.entries) {
            const auto caption = wide(label(entry) + " — " + detail(entry));
            if (SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(caption.c_str())) == LB_ERR)
                throw std::runtime_error("No se pudo presentar una entrada curricular.");
        }
        for (const auto action : home_actions) {
            const auto caption = wide(action_label(action)); SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(caption.c_str()));
        }
        const auto key = catalog_position_key(location);
        const auto selection = state::integer_value(progress,"native_windows",key + "/selection");
        const auto top = state::integer_value(progress,"native_windows",key + "/top");
        const auto count = page.entries.size() + home_actions.size();
        if (count) SendMessageW(list,LB_SETCURSEL,static_cast<WPARAM>(std::clamp(selection,std::int64_t{0},static_cast<std::int64_t>(count - 1))),0);
        if (count) SendMessageW(list,LB_SETTOPINDEX,static_cast<WPARAM>(std::clamp(top,std::int64_t{0},static_cast<std::int64_t>(count - 1))),0);
        const auto locale = location.locale == "en" ? 1 : location.locale == "fr" ? 2 : 0;
        SendMessageW(language,CB_SETCURSEL,locale,0);
        EnableWindow(back,catalog_level(location) != CatalogLevel::routes);
        updating = false; update_selection(); resize();
        if (remember) write_position();
    }
    void initialize(HWND target) {
        window = target; reader_theme::apply(window,reader_theme::Surface::canvas);
        back = control(L"BUTTON",L"Volver",WS_TABSTOP | BS_PUSHBUTTON,back_id);
        home = control(L"BUTTON",L"Inicio",WS_TABSTOP | BS_PUSHBUTTON,home_id);
        menu = control(L"BUTTON",L"Menú",WS_TABSTOP | BS_PUSHBUTTON,menu_id);
        language = control(L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,language_id);
        for (const auto text : {L"Español",L"English",L"Français"}) SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        title = control(L"STATIC",L"",SS_LEFT,title_id); status = control(L"STATIC",L"",SS_LEFT,status_id);
        routes_heading = control(L"STATIC",L"",SS_LEFT,routes_heading_id);
        list = control(L"LISTBOX",L"",WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWVARIABLE | LBS_NOINTEGRALHEIGHT,list_id);
        reader_theme::apply(list,reader_theme::Surface::field);
        SetWindowSubclass(list,list_proc,1,reinterpret_cast<DWORD_PTR>(&gesture));
        open_button = control(L"BUTTON",L"Abrir",WS_TABSTOP | BS_PUSHBUTTON,open_id); reader_theme::primary_button(open_button);
        info_button = control(L"BUTTON",L"Información",WS_TABSTOP | BS_PUSHBUTTON,info_id);
        ShowWindow(open_button,SW_HIDE); ShowWindow(info_button,SW_HIDE); ShowWindow(language,SW_HIDE);
        update_font(); resize(); render(restored_location(),false);
    }
    void update_font() {
        const auto next = CreateFontW(-MulDiv(12,static_cast<int>(GetDpiForWindow(window)),72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (!next) return;
        for (const auto item : {back,home,menu,language,title,status,list,open_button,info_button,routes_heading}) if (item)
            SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if (font) DeleteObject(font); font = next;
        const auto heading = CreateFontW(-px(28),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (heading) { SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE); if (heading_font) DeleteObject(heading_font); heading_font = heading; }
    }
    int row_height(std::size_t index) const {
        if (catalog_level(page.location) == CatalogLevel::routes) return px(index < page.entries.size() ? 96 : 68);
        if (index >= page.entries.size()) return px(78);
        RECT area{}; GetClientRect(list,&area); area.right = std::max(px(80),static_cast<int>(area.right) - px(36)); area.left = 0;
        const auto dc = GetDC(list); const auto previous = SelectObject(dc,font);
        const auto text = wide(label(page.entries[index]));
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&area,DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc,previous); ReleaseDC(list,dc);
        return std::max(px(130),static_cast<int>(area.bottom) + px(85));
    }
    void resize() {
        if (!list) return;
        RECT area{}; GetClientRect(window,&area); const int margin = px(24), available = std::max(px(100),static_cast<int>(area.right) - margin * 2);
        const bool root_page = catalog_level(page.location) == CatalogLevel::routes;
        ShowWindow(back,root_page ? SW_HIDE : SW_SHOW); ShowWindow(home,SW_HIDE); ShowWindow(menu,root_page ? SW_HIDE : SW_SHOW);
        ShowWindow(routes_heading,root_page ? SW_SHOW : SW_HIDE);
        MoveWindow(back,margin,px(20),px(72),px(42),TRUE); MoveWindow(home,margin + px(94),px(16),px(84),px(36),TRUE);
        MoveWindow(menu,static_cast<int>(area.right) - margin - px(84),px(16),px(84),px(36),TRUE);
        MoveWindow(language,static_cast<int>(area.right) - margin - px(225),px(18),px(130),px(180),TRUE);
        MoveWindow(title,margin,root_page ? px(70) : px(90),available,px(70),TRUE); MoveWindow(status,margin,root_page ? px(145) : px(165),available,px(48),TRUE);
        MoveWindow(routes_heading,margin,px(205),available,px(32),TRUE);
        const int list_top = root_page ? px(250) : px(225);
        SetWindowPos(list,nullptr,margin,list_top,available,std::max(px(40),static_cast<int>(area.bottom) - list_top - px(24)),
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
        MoveWindow(open_button,margin,std::max(px(200),static_cast<int>(area.bottom) - px(56)),px(150),px(38),TRUE);
        MoveWindow(info_button,margin + px(162),std::max(px(200),static_cast<int>(area.bottom) - px(56)),px(150),px(38),TRUE);
        for (std::size_t index = 0; index < page.entries.size() + home_actions.size(); ++index) SendMessageW(list,LB_SETITEMHEIGHT,index,row_height(index));
        RedrawWindow(list,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    void information() {
        const auto index = selected();
        const auto text = index ? *index < page.entries.size() ? label(page.entries[*index]) + "\n\n" + detail(page.entries[*index])
            : action_label(home_actions[*index - page.entries.size()]) : page.title;
        MessageBoxW(window,wide(text).c_str(),wide(ui("Información","Information","Informations")).c_str(),MB_OK | MB_ICONINFORMATION);
    }
    void activate() {
        const auto index = selected(); if (!index) return;
        if (*index >= page.entries.size()) { const auto action = home_actions[*index - page.entries.size()]; if (action_available(action)) on_action(action,page.location); return; }
        const auto entry = page.entries[*index];
        if (!entry.available) { SetWindowTextW(status,L"Este contenido todavía no está instalado. Se conserva su lugar en el curso."); return; }
        write_position();
        if (catalog_level(page.location) == CatalogLevel::lessons) {
            const auto callback = on_open; callback(entry.asset); return;
        }
        render(catalog_child(page.location,entry.id)); SetFocus(list);
    }
    bool action_available(CatalogAction action) const { return on_action && can_action && can_action(action,page.location); }
    CatalogLocation entry_context() const {
        const auto index = selected();
        if (!index || *index >= page.entries.size()) return page.location;
        auto context = page.location;
        if (catalog_level(context) == CatalogLevel::lessons) context.lesson = page.entries[*index].id;
        else context = catalog_child(context,page.entries[*index].id);
        return context;
    }
    void reset_progress() {
        const auto scope = windows::catalog_scope(root,menu_context);
        const auto message = ui("¿Reiniciar el progreso y las respuestas de este contenido? Los programas guardados y los recursos instalados se conservarán.",
            "Reset progress and answers for this content? Saved programs and installed resources will be kept.",
            "Réinitialiser la progression et les réponses de ce contenu ? Les programmes sauvegardés et les ressources installées seront conservés.");
        if (MessageBoxW(window,wide(message).c_str(),wide(ui("Reiniciar progreso","Reset progress","Réinitialiser la progression")).c_str(),MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) != IDYES) return;
        state::StateStore store(environment.paths.root); auto data = store.load(); reset_catalog_progress(data,scope); store.save(data);
        render(page.location);
    }
    void popup(POINT point,bool contextual = false) {
        menu_context = contextual ? entry_context() : page.location;
        const auto handle = CreatePopupMenu(); if (!handle) return;
        AppendMenuW(handle,MF_STRING | (selected() ? 0 : MF_GRAYED),open_id,wide(ui("Abrir / continuar","Open / continue","Ouvrir / continuer")).c_str());
        AppendMenuW(handle,MF_STRING,info_id,wide(ui("Información","Information","Informations")).c_str()); AppendMenuW(handle,MF_SEPARATOR,0,nullptr);
        AppendMenuW(handle,MF_STRING,home_id,wide(ui("Inicio","Home","Accueil")).c_str());
        AppendMenuW(handle,MF_STRING | (catalog_level(page.location) == CatalogLevel::routes ? MF_GRAYED : 0),back_id,wide(ui("Volver","Back","Retour")).c_str());
        if (on_action && can_action && can_action(CatalogAction::settings,menu_context)) AppendMenuW(handle,MF_STRING,settings_id,wide(ui("Opciones","Options","Options")).c_str());
        if (on_action && can_action && can_action(CatalogAction::audio,menu_context)) AppendMenuW(handle,MF_STRING,audio_id,L"Audio");
        if (!menu_context.route.empty())
            AppendMenuW(handle,MF_STRING,reset_id,wide(ui("Reiniciar progreso…","Reset progress…","Réinitialiser la progression…")).c_str());
        const auto id = TrackPopupMenuEx(handle,TPM_RETURNCMD | TPM_RIGHTBUTTON,point.x,point.y,window,nullptr); DestroyMenu(handle);
        if (id) command(static_cast<int>(id),BN_CLICKED);
    }
    void command(int id,int notification) {
        if (updating) return;
        if (id == list_id) {
            if (notification == LBN_SELCHANGE) { update_selection(); write_position(); }
            if (notification == LBN_DBLCLK) activate();
            return;
        }
        if (id == language_id && notification == CBN_SELCHANGE) {
            const auto selected = SendMessageW(language,CB_GETCURSEL,0,0);
            if (selected < 0 || selected > 2) return;
            auto location = page.location; location.locale = selected == 1 ? "en" : selected == 2 ? "fr" : "es";
            render(location); return;
        }
        if (notification != BN_CLICKED) return;
        if (id == open_id) activate();
        else if (id == info_id) information();
        else if (id == back_id) render(catalog_parent(page.location));
        else if (id == home_id) { CatalogLocation location; location.locale = page.location.locale; render(location); }
        else if (id == menu_id) { RECT area{}; GetWindowRect(menu,&area); popup({area.left,area.bottom}); }
        else if (id == settings_id && on_action && can_action && can_action(CatalogAction::settings,menu_context)) on_action(CatalogAction::settings,menu_context);
        else if (id == audio_id && on_action && can_action && can_action(CatalogAction::audio,menu_context)) on_action(CatalogAction::audio,menu_context);
        else if (id == reset_id) reset_progress();
    }
    void draw(const DRAWITEMSTRUCT& item) {
        if (item.itemID >= page.entries.size() + home_actions.size()) return;
        const auto palette = reader_theme::palette(); const bool selected = (item.itemState & ODS_SELECTED) != 0;
        if (catalog_level(page.location) == CatalogLevel::routes) {
            auto card = item.rcItem; InflateRect(&card,-px(4),-px(9));
            const auto fill = CreateSolidBrush(palette.high_contrast ? palette.card : RGB(77,211,224)); FillRect(item.hDC,&card,fill); DeleteObject(fill);
            const auto old_font = SelectObject(item.hDC,font); SetBkMode(item.hDC,TRANSPARENT);
            SetTextColor(item.hDC,palette.high_contrast ? palette.text : RGB(10,15,17));
            const auto text = wide(item.itemID < page.entries.size() ? page.entries[item.itemID].title : action_label(home_actions[item.itemID - page.entries.size()]));
            InflateRect(&card,-px(14),-px(8)); DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&card,DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (selected && (item.itemState & ODS_FOCUS)) DrawFocusRect(item.hDC,&card);
            SelectObject(item.hDC,old_font); return;
        }
        FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::canvas));
        auto card = item.rcItem; card.top += px(6); card.bottom -= px(6);
        const auto background = CreateSolidBrush(palette.card); FillRect(item.hDC,&card,background); DeleteObject(background);
        const auto old_font = SelectObject(item.hDC,font); SetBkMode(item.hDC,TRANSPARENT); SetTextColor(item.hDC,palette.text);
        auto bounds = card; InflateRect(&bounds,-px(14),-px(10));
        const auto entry = page.entries[item.itemID];
        SetTextColor(item.hDC,accent());
        const auto ordinal = wide((catalog_level(page.location) == CatalogLevel::courses ? ui("Curso ","Course ","Cours ") :
            catalog_level(page.location) == CatalogLevel::units ? ui("Unidad ","Unit ","Unité ") : ui("Lección ","Lesson ","Leçon ")) +
            std::to_string(entry.number) + ui(" de "," of "," sur ") + std::to_string(page.entries.size()));
        DrawTextW(item.hDC,ordinal.c_str(),static_cast<int>(ordinal.size()),&bounds,DT_SINGLELINE | DT_NOPREFIX);
        bounds.top += px(26); SetTextColor(item.hDC,palette.text);
        const auto entry_title = wide(entry.title);
        DrawTextW(item.hDC,entry_title.c_str(),static_cast<int>(entry_title.size()),&bounds,DT_WORDBREAK | DT_NOPREFIX);
        bounds = card; bounds.left += px(14); bounds.right -= px(14); bounds.top = bounds.bottom - px(43);
        SetTextColor(item.hDC,palette.muted);
        const auto description = wide(detail(page.entries[item.itemID]));
        bounds.right -= px(52);
        DrawTextW(item.hDC,description.c_str(),static_cast<int>(description.size()),&bounds,DT_SINGLELINE | DT_NOPREFIX);
        const auto snapshot = completion[item.itemID]; const auto percentage = snapshot.total ? snapshot.completed * 100 / snapshot.total : 0;
        bounds.right += px(52); const auto percent = std::to_wstring(percentage) + L"%";
        DrawTextW(item.hDC,percent.c_str(),static_cast<int>(percent.size()),&bounds,DT_SINGLELINE | DT_RIGHT | DT_NOPREFIX);
        auto meter = card; meter.left += px(14); meter.right -= px(14); meter.top = meter.bottom - px(12); meter.bottom = meter.top + px(3);
        const auto track = CreateSolidBrush(palette.outline); FillRect(item.hDC,&meter,track); DeleteObject(track);
        meter.right = meter.left + MulDiv(meter.right - meter.left,static_cast<int>(percentage),100);
        const auto fill = CreateSolidBrush(accent()); FillRect(item.hDC,&meter,fill); DeleteObject(fill);
        if (item.itemState & ODS_FOCUS) { auto focus = item.rcItem; InflateRect(&focus,-2,-2); DrawFocusRect(item.hDC,&focus); }
        SelectObject(item.hDC,old_font);
    }
};
CatalogView* view(HWND window) {
    return reinterpret_cast<CatalogView*>(GetWindowLongPtrW(window,GWLP_USERDATA));
}
LRESULT CALLBACK catalog_proc(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    auto* self = view(window);
    if (message == WM_NCCREATE) { self = static_cast<CatalogView*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(window,message,wparam,lparam);
    try {
        LRESULT color = 0; if (reader_theme::control_color(message,wparam,lparam,color)) return color;
        switch (message) {
        case WM_CREATE: self->initialize(window); return 0;
        case WM_SIZE: self->resize(); return 0;
        case WM_DPICHANGED_AFTERPARENT: self->update_font(); self->resize(); return 0;
        case WM_COMMAND: self->command(LOWORD(wparam),HIWORD(wparam)); return 0;
        case activate_message: self->activate(); return 0;
        case WM_MEASUREITEM: {
            auto& item = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (item.CtlID == list_id) { item.itemHeight = self->row_height(item.itemID); return TRUE; } break;
        }
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (item.CtlID == list_id) { self->draw(item); return TRUE; }
            if (item.CtlID == back_id) {
                const auto fill = CreateSolidBrush(self->accent()); FillRect(item.hDC,&item.rcItem,fill); DeleteObject(fill);
                auto bounds = item.rcItem; InflateRect(&bounds,-self->px(20),-self->px(10)); draw_icon(item.hDC,bounds,Icon::back,reader_theme::palette().text); return TRUE;
            }
            if (reader_theme::draw_button(item)) return TRUE; break;
        }
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
            if (point.x == -1 && point.y == -1) { RECT area{}; GetWindowRect(self->list,&area); point = {area.left + 20,area.top + 20}; }
            self->popup(point,true); return 0;
        }
        case WM_ERASEBKGND: { RECT area{}; GetClientRect(window,&area); FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::canvas)); return 1; }
        case WM_NCDESTROY: SetWindowLongPtrW(window,GWLP_USERDATA,0); if (self->owned_by_window) delete self; break;
        }
    } catch (const std::exception& error) {
        if (message == WM_CREATE) { OutputDebugStringW(wide(error.what()).c_str()); return -1; }
        SetWindowTextW(self->status,wide(error.what()).c_str());
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
}
HWND create_catalog_view(HWND parent,const WindowsEnvironment& environment,const std::filesystem::path& root,
    std::function<void(const std::filesystem::path&)> on_open,CatalogActionHandler on_action,CatalogActionAvailability can_action) {
    static const bool registered = [] {
        WNDCLASSW type{}; type.lpfnWndProc = catalog_proc; type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName = catalog_class;
        return RegisterClassW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) throw std::runtime_error("No se pudo registrar el catálogo.");
    auto data = std::make_unique<CatalogView>(environment,root,std::move(on_open),std::move(on_action),std::move(can_action));
    const auto window = CreateWindowExW(WS_EX_CONTROLPARENT,catalog_class,L"Catálogo curricular",WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0,0,1,1,parent,nullptr,GetModuleHandleW(nullptr),data.get());
    if (!window) throw std::runtime_error("No se pudo abrir el catálogo curricular.");
    data->owned_by_window = true;
    data.release(); return window;
}
curriculum::CatalogLocation catalog_location(HWND window) {
    const auto self = view(window); if (!self) throw std::invalid_argument("Catálogo inexistente."); return self->page.location;
}
void navigate_catalog(HWND window,const curriculum::CatalogLocation& location) {
    auto* self = view(window); if (!self) throw std::invalid_argument("Catálogo inexistente."); self->render(location);
}
void refresh_catalog_view(HWND window) {
    auto* self = view(window); if (!self) return;
    auto location = self->page.location;
    location.locale = state::string_value(self->load_state(),"native_windows","content_language",location.locale);
    self->render(location);
}
void navigate_catalog_home(HWND window) { auto location = catalog_location(window); location.route.clear(); location.course.clear(); location.unit.clear(); navigate_catalog(window,location); }
} // namespace lgaed::presentation

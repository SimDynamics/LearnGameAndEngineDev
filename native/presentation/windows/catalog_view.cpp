#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/card_list.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/settings_view.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"

#include <commctrl.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
using namespace curriculum;
constexpr wchar_t catalog_class[] = L"LearnGameAndEngineDev.CatalogView";
constexpr int back_id = 4101, home_id = 4102, menu_id = 4103, language_id = 4104;
constexpr int list_id = 4105, open_id = 4106, info_id = 4107, title_id = 4108, status_id = 4109;
constexpr int routes_heading_id = 4113;
constexpr int scope_ordinal_id = 4114;
constexpr UINT activate_message = WM_APP + 141;
constexpr UINT scroll_message=WM_APP+142;
struct CatalogView;
LRESULT CALLBACK catalog_proc(HWND,UINT,WPARAM,LPARAM);
struct CatalogView {
    HWND window = nullptr, title = nullptr, status = nullptr, list = nullptr, back = nullptr, home = nullptr;
    HWND menu = nullptr, language = nullptr, open_button = nullptr, info_button = nullptr, routes_heading = nullptr;
    HWND scope_ordinal=nullptr;
    HFONT font = nullptr, heading_font = nullptr,card_font = nullptr,section_font = nullptr,action_font = nullptr,route_font = nullptr,subtitle_font = nullptr,ordinal_font=nullptr,scope_font=nullptr;
    WindowsEnvironment environment;
    std::filesystem::path root;
    CatalogPage page;
    state::StateData progress;
    std::function<void(const std::filesystem::path&)> on_open;
    CatalogActionHandler on_action;
    CatalogActionAvailability can_action;
    CatalogMathValidator validate_math;
    CatalogLocationChanged location_changed;
    std::map<std::string,bool> math_validation;
    bool updating = false, owned_by_window = false, rendered = false;
    std::vector<CatalogAction> home_actions;
    std::vector<CatalogProgress> completion;
    std::vector<CatalogScope> scopes;
    std::string ui_locale = "es";
    struct HeaderPlacement{HWND item;int x,y,width,height;};
    std::vector<HeaderPlacement> header;
    void position_header(int offset){
        for(const auto& item:header)SetWindowPos(item.item,nullptr,item.x,item.y-offset,item.width,item.height,
            SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS);
    }
    static LRESULT CALLBACK header_proc(HWND item,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
        auto& self=*reinterpret_cast<CatalogView*>(context);
        if(message==WM_NCDESTROY){RemoveWindowSubclass(item,header_proc,12);return DefSubclassProc(item,message,wp,lp);}
        if(catalog_level(self.page.location)!=CatalogLevel::routes)return DefSubclassProc(item,message,wp,lp);
        if(message==WM_MOUSEWHEEL)return SendMessageW(self.list,message,wp,lp);
        if(message==WM_LBUTTONDOWN||message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
            POINT at{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(item,self.list,&at,1);
            return SendMessageW(self.list,message,wp,MAKELPARAM(at.x,at.y));
        }
        return DefSubclassProc(item,message,wp,lp);
    }

    CatalogView(const WindowsEnvironment& env,std::filesystem::path content,
        std::function<void(const std::filesystem::path&)> callback,CatalogActionHandler action,CatalogActionAvailability availability,CatalogMathValidator math,
        CatalogLocationChanged changed)
        : environment(env),root(std::move(content)),on_open(std::move(callback)),on_action(std::move(action)),can_action(std::move(availability)),validate_math(std::move(math)),location_changed(std::move(changed)) {
        if (!on_open) throw std::invalid_argument("El catálogo necesita una acción para abrir lecciones.");
    }
    ~CatalogView() { for(auto face:{font,heading_font,card_font,section_font,action_font,route_font,subtitle_font,ordinal_font,scope_font})if(face)DeleteObject(face); }
    int px(int value) const { return MulDiv(value,static_cast<int>(GetDpiForWindow(window)),96); }
    std::string ui(std::string_view es,std::string_view en,std::string_view fr) const {
        return std::string(ui_locale == "fr" ? fr : ui_locale == "en" ? en : es);
    }
    COLORREF accent() const { return reader_theme::palette(window).accent; }
    std::string action_label(CatalogAction action) const {
        switch (action) {
        case CatalogAction::settings: return ui("Opciones globales","Global settings","Options générales");
        case CatalogAction::audio: return "Audio";
        case CatalogAction::calculator: return ui("Abrir calculadora científica","Open Scientific Calculator","Ouvrir la calculatrice scientifique");
        case CatalogAction::compiler: return ui("Abrir prototipo C++","Open C++ Prototype","Ouvrir le prototype C++");
        case CatalogAction::graphics: return ui("Motor gráfico","Graphics engine","Moteur graphique");
        case CatalogAction::smo: return ui("Laboratorio SMO","SMO Laboratory","Laboratoire SMO");
        }
        return {};
    }
    HWND control(const wchar_t* type,const wchar_t* text,DWORD style,int id) {
        auto item = CreateWindowExW(0,type,text,WS_CHILD | WS_VISIBLE | style,0,0,1,1,window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if (!item) throw std::runtime_error("No se pudo crear el catálogo nativo.");
        reader_theme::apply(item,reader_theme::Surface::screen); return item;
    }
    state::StateData load_state() const { state::StateStore store(environment.paths.root); return store.load(); }
    void write_position(bool include_current = true) {
        state::StateStore store(environment.paths.root); auto data = store.load();
        auto& prefs = data.preferences["native_windows"];
        const auto key = catalog_position_key(page.location);
        const auto selection = SendMessageW(list,LB_GETCURSEL,0,0), top = SendMessageW(list,LB_GETTOPINDEX,0,0);
        if (selection != LB_ERR) prefs[key + "/selection"] = static_cast<std::int64_t>(selection);
        if (top != LB_ERR) prefs[key + "/top"] = static_cast<std::int64_t>(top);
        prefs[key+"/scroll"]=static_cast<std::int64_t>(card_list_scroll_offset(list));
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
        if (!entry.available) return development_notice();
        if (catalog_level(page.location) == CatalogLevel::routes) return ui("Ver cursos","View courses","Voir les cours");
        const auto index = static_cast<std::size_t>(&entry - page.entries.data());
        if (index < completion.size()) {
            const auto snapshot = completion[index];
            const bool lesson=catalog_level(page.location)==CatalogLevel::lessons;
            if(catalog_level(page.location)==CatalogLevel::courses&&snapshot.total==0)return development_notice();
            const bool singular=snapshot.total==1||(ui_locale=="fr"&&snapshot.total==0);
            return std::to_string(snapshot.completed) + ui(" de "," of "," sur ") + std::to_string(snapshot.total) +
                (lesson?(singular?ui(" página completada"," page completed"," page terminée"):ui(" páginas completadas"," pages completed"," pages terminées")):
                    (singular?ui(" lección completada"," lesson completed"," leçon terminée"):ui(" lecciones completadas"," lessons completed"," leçons terminées"))) +
                (snapshot.pending_validation?ui(" · verificando…"," · checking…"," · vérification…"):"");
        }
        return ui("Abrir","Open","Ouvrir");
    }
    std::string development_notice() const {
        return ui("En desarrollo · Próximamente disponible","In Development · Coming Soon","En développement · Bientôt disponible");
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
        const bool changed = !rendered || page.location != loaded.location;
        if (remember && list) write_position(false);
        updating = true; page = std::move(loaded); progress = load_state();
        reader_theme::set_accent(window,reader_theme::appearance_accent(progress,page.location.route));
        struct Restore { bool& updating; ~Restore() { updating = false; } } restore{updating};
        home_actions.clear(); completion.clear(); scopes.clear();
        ui_locale = interface_language(progress);
        for (const auto& entry : page.entries) {
            CatalogScope scope;
            if (entry.available && catalog_level(location) != CatalogLevel::routes) {
                if (catalog_level(location) == CatalogLevel::lessons) scope.lessons.push_back(entry.id);
                else scope = windows::catalog_scope(root,catalog_child(location,entry.id));
            }
            completion.push_back(windows::catalog_entry_progress(root,location,entry,progress,
                [&](const Block& block,const state::Response& response,std::string_view locale)->std::optional<bool> {
                    if(!validate_math)return std::nullopt;
                    const auto frame=[](std::string_view value){return std::to_string(value.size())+':'+std::string(value);};
                    const auto identity=state::sha256("catalog-math-v1/"+frame(locale)+frame(block.raw_json)+frame(response.raw_json));
                    if(const auto found=math_validation.find(identity);found!=math_validation.end())return found->second;
                    const auto verified=validate_math(block,response,locale);
                    if(verified){if(math_validation.size()>=2048)math_validation.erase(math_validation.begin());math_validation.emplace(identity,*verified);}
                    return verified;
                })); scopes.push_back(std::move(scope));
        }
        if (catalog_level(location) == CatalogLevel::routes) for (const auto action :
            {CatalogAction::settings,CatalogAction::calculator,CatalogAction::graphics,CatalogAction::compiler,CatalogAction::smo})
            if (action_available(action)) home_actions.push_back(action);
        SetWindowTextW(back,wide(ui("Volver","Back","Retour")).c_str()); SetWindowTextW(home,wide(ui("Inicio","Home","Accueil")).c_str());
        SetWindowTextW(menu,wide(ui("Menú","Menu","Menu")).c_str()); SetWindowTextW(info_button,wide(ui("Información","Information","Informations")).c_str());
        const auto display_title = catalog_level(location) == CatalogLevel::routes ? "LearnGameAndEngineDev" :
            catalog_level(location) == CatalogLevel::courses ? location.route == "programming" ? ui("Programación Avanzada y Arquitectura de Motores",
            "Advanced Programming and Engine Architecture","Programmation avancée et architecture des moteurs") : ui("Matemáticas y Física","Mathematics and Physics","Mathématiques et physique") : page.title;
        SetWindowTextW(title,wide(display_title).c_str());
        const auto level=catalog_level(location);
        const auto status_text = level==CatalogLevel::courses ? location.route=="programming" ?
            ui("Ruta de programación, sistemas, gráficos y motores.","A learning path covering programming, systems, graphics, and engines.","Un parcours consacré à la programmation, aux systèmes, à l’informatique graphique et aux moteurs.") :
            ui("Ruta progresiva de matemáticas y física.","A progressive learning path in mathematics and physics.","Un parcours progressif en mathématiques et en physique.") :
            level==CatalogLevel::units ? ui("Unidades","Units","Unités") : ui("Lecciones","Lessons","Leçons");
        SetWindowTextW(status,wide(catalog_level(location) == CatalogLevel::routes ? ui("Aprende programación, gráficos, motores, matemáticas y física desde sus fundamentos.",
            "Learn programming, graphics, engines, mathematics, and physics from the ground up.",
            "Apprenez la programmation, l’informatique graphique, les moteurs, les mathématiques et la physique depuis leurs fondements.") : status_text).c_str());
        SetWindowTextW(routes_heading,wide(ui("Rutas de aprendizaje","Learning Paths","Parcours d’apprentissage")).c_str());
        if(level==CatalogLevel::units||level==CatalogLevel::lessons){
            const auto parent_page=windows::load_catalog(root,catalog_parent(location));
            const auto id=level==CatalogLevel::units?location.course:location.unit;
            const auto entry=std::find_if(parent_page.entries.begin(),parent_page.entries.end(),[&](const auto& value){return value.id==id;});
            const auto number=entry==parent_page.entries.end()?0:entry->number;
            const auto ordinal=level==CatalogLevel::units?ui("Curso ","Course ","Cours ")+std::to_string(number)+ui(" de "," of "," sur ")+std::to_string(parent_page.entries.size()):
                ui("Unidad ","Unit ","Unité ")+std::to_string(number);
            SetWindowTextW(scope_ordinal,wide(ordinal).c_str());
            reader_theme::set_text_color(scope_ordinal,reader_theme::accent_ink(window,reader_theme::background(window)));
        }
        update_font();
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
        const auto group=progress.preferences.find("native_windows");
        if(group!=progress.preferences.end()&&group->second.contains(key+"/scroll"))
            set_card_list_scroll_offset(list,static_cast<int>(std::clamp(state::integer_value(progress,"native_windows",key+"/scroll"),std::int64_t{0},std::int64_t{INT_MAX/2})));
        if (remember) write_position();
        rendered = true;
        if (changed && location_changed) location_changed(window);
    }
    void initialize(HWND target) {
        window = target; reader_theme::apply(window,reader_theme::Surface::screen);
        back = control(L"BUTTON",L"Volver",WS_TABSTOP | BS_PUSHBUTTON,back_id);
        home = control(L"BUTTON",L"Inicio",WS_TABSTOP | BS_PUSHBUTTON,home_id);
        menu = control(L"BUTTON",L"Menú",WS_TABSTOP | BS_PUSHBUTTON,menu_id);
        language = control(L"COMBOBOX",L"",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,language_id);
        for (const auto text : {L"Español",L"English",L"Français"}) SendMessageW(language,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        title = control(L"STATIC",L"",SS_LEFT,title_id); status = control(L"STATIC",L"",SS_LEFT,status_id);
        routes_heading = control(L"STATIC",L"",SS_LEFT,routes_heading_id);
        scope_ordinal=control(L"STATIC",L"",SS_LEFT,scope_ordinal_id);
        reader_theme::set_text_color(title,RGB(255,255,255));reader_theme::set_text_color(status,RGB(174,180,190));
        reader_theme::set_text_color(routes_heading,RGB(255,255,255));
        reader_theme::icon_button(home);reader_theme::icon_button(menu);
        list = create_card_list(window,list_id,activate_message);
        reader_theme::apply(list,reader_theme::Surface::screen);
        for(const auto item:{title,status,routes_heading,scope_ordinal})
            SetWindowSubclass(item,header_proc,12,reinterpret_cast<DWORD_PTR>(this));
        open_button = control(L"BUTTON",L"Abrir",WS_TABSTOP | BS_PUSHBUTTON,open_id); reader_theme::primary_button(open_button);
        info_button = control(L"BUTTON",L"Información",WS_TABSTOP | BS_PUSHBUTTON,info_id);
        ShowWindow(open_button,SW_HIDE); ShowWindow(info_button,SW_HIDE); ShowWindow(language,SW_HIDE);
        update_font(); resize(); render(restored_location(),false);
    }
    void update_font() {
        const auto next = CreateFontW(-MulDiv(18,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if (!next) return;
        for (const auto item : {back,home,menu,language,title,status,list,open_button,info_button,routes_heading,scope_ordinal}) if (item)
            SendMessageW(item,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
        if (font) DeleteObject(font); font = next;
        const bool home_page=catalog_level(page.location)==CatalogLevel::routes;
        const auto heading = CreateFontW(-MulDiv(home_page?32:catalog_level(page.location)==CatalogLevel::courses?28:30,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if (heading) { SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(heading),TRUE); if (heading_font) DeleteObject(heading_font); heading_font = heading; }
        const auto face=[&](HFONT& target,int size,int weight){const auto next_face=CreateFontW(-MulDiv(size,static_cast<int>(GetDpiForWindow(window)),96),0,0,0,weight,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
            if(next_face){if(target)DeleteObject(target);target=next_face;}};
        face(card_font,catalog_level(page.location)==CatalogLevel::lessons?20:21,FW_BOLD);face(section_font,22,FW_BOLD);face(action_font,18,FW_MEDIUM);face(route_font,20,FW_MEDIUM);face(subtitle_font,home_page?17:16,FW_NORMAL);
        face(ordinal_font,18,FW_BOLD);face(scope_font,16,FW_BOLD);
        if(!home_page&&catalog_level(page.location)!=CatalogLevel::courses)face(subtitle_font,18,FW_NORMAL);
        SendMessageW(routes_heading,WM_SETFONT,reinterpret_cast<WPARAM>(section_font),TRUE);
        SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(subtitle_font),TRUE);
        SendMessageW(scope_ordinal,WM_SETFONT,reinterpret_cast<WPARAM>(scope_font),TRUE);
    }
    int text_height(HFONT face,std::wstring_view text,int width) const {
        const auto dc=GetDC(window);const auto previous=SelectObject(dc,face?face:font);
        const auto spacing=face==route_font||face==action_font?MulDiv(face==route_font?20:18,static_cast<int>(GetDpiForWindow(window)),960):0;
        const auto previous_spacing=SetTextCharacterExtra(dc,spacing);
        RECT measured{0,0,std::max(1,width),0};DrawTextW(dc,text.data(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
        TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SetTextCharacterExtra(dc,previous_spacing);SelectObject(dc,previous);ReleaseDC(window,dc);
        return std::max(static_cast<int>(metrics.tmHeight),static_cast<int>(measured.bottom));
    }
    int caption_height(HWND control,HFONT face,int width) const {
        std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(control))+1,L'\0');text.resize(GetWindowTextW(control,text.data(),static_cast<int>(text.size())));
        return text_height(face,text,width);
    }
    int row_height(std::size_t index) const {
        RECT area{};GetClientRect(list,&area);const auto width=std::max(px(80),static_cast<int>(area.right)-px(40));
        if (catalog_level(page.location) == CatalogLevel::routes) {
            const bool route=index<page.entries.size();const auto caption=wide(route?page.entries[index].title:action_label(home_actions[index-page.entries.size()]));
            return std::max(px(route?100:64),text_height(route?route_font:action_font,caption,width)+px(route?40:32))+px(18);
        }
        if (index >= page.entries.size()) return px(78);
        const bool lesson=catalog_level(page.location)==CatalogLevel::lessons;
        return px(40+7+(lesson?18:16)+9+(lesson?6:8)+14)+text_height(ordinal_font,L"0",width)+
            text_height(card_font,wide(page.entries[index].title),width)+text_height(font,wide(detail(page.entries[index])),std::max(px(40),width-px(70)));
    }
    void resize() {
        if (!list) return;
        RECT area{}; GetClientRect(window,&area); const int margin = px(24), available = std::max(px(100),static_cast<int>(area.right) - margin * 2);
        const bool root_page = catalog_level(page.location) == CatalogLevel::routes;
        const bool scoped=catalog_level(page.location)==CatalogLevel::units||catalog_level(page.location)==CatalogLevel::lessons;
        ShowWindow(back,root_page ? SW_HIDE : SW_SHOW);ShowWindow(home,scoped?SW_SHOW:SW_HIDE);ShowWindow(menu,scoped?SW_SHOW:SW_HIDE);
        ShowWindow(scope_ordinal,scoped?SW_SHOW:SW_HIDE);
        ShowWindow(routes_heading,root_page ? SW_SHOW : SW_HIDE);
        header.clear();
        header.push_back({back,margin,px(20),px(scoped?56:88),px(scoped?56:48)});
        header.push_back({home,static_cast<int>(area.right)-margin-px(112),px(20),px(56),px(56)});
        header.push_back({menu,static_cast<int>(area.right)-margin-px(56),px(20),px(56),px(56)});
        MoveWindow(language,static_cast<int>(area.right) - margin - px(225),px(18),px(130),px(180),TRUE);
        const int ordinal_top=px(96),ordinal_height=caption_height(scope_ordinal,scope_font,available);
        header.push_back({scope_ordinal,margin,ordinal_top,available,ordinal_height});
        const int title_top=root_page?px(54):scoped?ordinal_top+ordinal_height+px(6):px(86),title_height=caption_height(title,heading_font,available);
        const int status_top=title_top+title_height+px(8),status_height=caption_height(status,subtitle_font,available);
        const int heading_top=status_top+status_height+px(36),heading_height=caption_height(routes_heading,section_font,available);
        header.push_back({title,margin,title_top,available,title_height});header.push_back({status,margin,status_top,available,status_height});
        header.push_back({routes_heading,margin,heading_top,available,heading_height});
        const int list_top=root_page?heading_top+heading_height+px(16):status_top+status_height+px(24);
        const auto viewport_top=root_page?0:list_top;
        SetWindowPos(list,HWND_BOTTOM,margin,viewport_top,available,std::max(px(40),static_cast<int>(area.bottom)-viewport_top-px(24)),
            SWP_NOACTIVATE | SWP_NOCOPYBITS | SWP_NOREDRAW);
        set_card_list_header(list,root_page?list_top:0,root_page?scroll_message:0);
        MoveWindow(open_button,margin,std::max(px(200),static_cast<int>(area.bottom) - px(56)),px(150),px(38),TRUE);
        MoveWindow(info_button,margin + px(162),std::max(px(200),static_cast<int>(area.bottom) - px(56)),px(150),px(38),TRUE);
        for (std::size_t index = 0; index < page.entries.size() + home_actions.size(); ++index) SendMessageW(list,LB_SETITEMHEIGHT,index,row_height(index));
        position_header(root_page?card_list_scroll_offset(list):0);
        // Moving the list between course and home layouts exposes part of its
        // old client rectangle. Repaint that parent area as well as the rows.
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    void activate() {
        const auto index = selected(); if (!index) return;
        if (*index >= page.entries.size()) { const auto action = home_actions[*index - page.entries.size()]; if (action_available(action)) on_action(action,page.location); return; }
        const auto entry = page.entries[*index];
        if (!entry.available) { show_settings_notice(window,development_notice()); return; }
        write_position();
        if (catalog_level(page.location) == CatalogLevel::lessons) {
            const auto callback = on_open; callback(entry.asset); return;
        }
        render(catalog_child(page.location,entry.id)); SetFocus(list);
    }
    bool navigate_back(){
        if(catalog_level(page.location)==CatalogLevel::routes)return false;
        render(catalog_parent(page.location));SetFocus(list);return true;
    }
    bool action_available(CatalogAction action) const { return on_action && can_action && can_action(action,page.location); }
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
        else if (id == back_id) (void)navigate_back();
        else if (id == home_id) { CatalogLocation location; location.locale = page.location.locale; render(location); }
        else if (id == menu_id && on_action && can_action && can_action(CatalogAction::settings,page.location)) on_action(CatalogAction::settings,page.location);
    }
    void draw(const DRAWITEMSTRUCT& item) {
        if (item.itemID >= page.entries.size() + home_actions.size()) return;
        const auto palette = reader_theme::palette(window);
        if (catalog_level(page.location) == CatalogLevel::routes) {
            FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::screen));
            const bool route=item.itemID<page.entries.size();auto card=item.rcItem;card.bottom-=px(18);
            const auto fill = CreateSolidBrush(palette.high_contrast ? palette.card : palette.accent);
            const auto previous_fill=SelectObject(item.hDC,fill),previous_pen=SelectObject(item.hDC,GetStockObject(NULL_PEN));
            RoundRect(item.hDC,card.left,card.top,card.right,card.bottom,px(8),px(8));
            SelectObject(item.hDC,previous_pen);SelectObject(item.hDC,previous_fill);DeleteObject(fill);
            const auto face=route?route_font:action_font;const auto old_font = SelectObject(item.hDC,face?face:font); SetBkMode(item.hDC,TRANSPARENT);
            const auto old_spacing=SetTextCharacterExtra(item.hDC,MulDiv(route?20:18,static_cast<int>(GetDpiForWindow(window)),960));
            SetTextColor(item.hDC,reader_theme::accent_foreground(window));
            const auto text = wide(item.itemID < page.entries.size() ? page.entries[item.itemID].title : action_label(home_actions[item.itemID - page.entries.size()]));
            auto focus_bounds=card;InflateRect(&card,-px(20),-px(route?20:16));auto measured=card;
            DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            card.top+=std::max<LONG>(0,(card.bottom-card.top-(measured.bottom-measured.top))/2);
            DrawTextW(item.hDC,text.c_str(),static_cast<int>(text.size()),&card,DT_CENTER|DT_WORDBREAK|DT_NOPREFIX);
            if(item.itemState&ODS_FOCUS)reader_theme::paint_keyboard_focus(list,item.hDC,focus_bounds,reader_theme::accent_foreground(window));
            SetTextCharacterExtra(item.hDC,old_spacing);SelectObject(item.hDC,old_font); return;
        }
        FillRect(item.hDC,&item.rcItem,reader_theme::brush(reader_theme::Surface::screen));
        auto card = item.rcItem;card.bottom-=px(14);
        const auto snapshot = completion[item.itemID];
        const bool finished=snapshot.total>0&&snapshot.completed==snapshot.total&&!snapshot.pending_validation;
        if(finished&&!palette.high_contrast){const auto fill=CreateSolidBrush(reader_theme::completion_background(RGB(32,33,38)));FillRect(item.hDC,&card,fill);DeleteObject(fill);}
        else FillRect(item.hDC,&card,reader_theme::brush(reader_theme::Surface::catalog_card));
        const auto old_font = SelectObject(item.hDC,ordinal_font?ordinal_font:font); SetBkMode(item.hDC,TRANSPARENT); SetTextColor(item.hDC,palette.text);
        auto bounds = card; InflateRect(&bounds,-px(20),-px(20));
        const auto entry = page.entries[item.itemID];
        SetTextColor(item.hDC,finished&&!palette.high_contrast?RGB(105,220,145):reader_theme::accent_ink(window,palette.card));
        const auto ordinal = wide((catalog_level(page.location) == CatalogLevel::courses ? ui("Curso ","Course ","Cours ") :
            catalog_level(page.location) == CatalogLevel::units ? ui("Unidad ","Unit ","Unité ") : ui("Lección ","Lesson ","Leçon ")) +
            std::to_string(entry.number) + (catalog_level(page.location)==CatalogLevel::courses?ui(" de "," of "," sur ")+std::to_string(page.entries.size()):""));
        DrawTextW(item.hDC,ordinal.c_str(),static_cast<int>(ordinal.size()),&bounds,DT_SINGLELINE | DT_NOPREFIX);
        bounds.top += text_height(ordinal_font,ordinal,bounds.right-bounds.left)+px(7); SetTextColor(item.hDC,palette.high_contrast?palette.text:RGB(255,255,255));
        SelectObject(item.hDC,card_font?card_font:font);
        const auto entry_title = wide(entry.title);
        const auto title_height=text_height(card_font,entry_title,bounds.right-bounds.left);
        bounds.bottom=bounds.top+title_height;
        DrawTextW(item.hDC,entry_title.c_str(),static_cast<int>(entry_title.size()),&bounds,DT_WORDBREAK | DT_NOPREFIX);
        const auto title_bottom=bounds.bottom;
        bounds=card;InflateRect(&bounds,-px(20),-px(20));bounds.right-=px(70);
        const auto description = wide(detail(page.entries[item.itemID]));
        const auto detail_height=text_height(font,description,bounds.right-bounds.left);
        const bool lesson=catalog_level(page.location)==CatalogLevel::lessons;
        bounds.top=std::max(title_bottom+px(lesson?18:16),card.bottom-px(20+(lesson?6:8)+9)-detail_height);bounds.bottom=bounds.top+detail_height;
        SelectObject(item.hDC,font);
        SetTextColor(item.hDC,palette.high_contrast?palette.muted:lesson?RGB(174,180,190):RGB(208,208,213));
        DrawTextW(item.hDC,description.c_str(),static_cast<int>(description.size()),&bounds,DT_WORDBREAK | DT_NOPREFIX);
        const auto percentage = snapshot.percentage();
        bounds.left=bounds.right+px(12);bounds.right=card.right-px(20);const auto percent = snapshot.pending_validation?std::wstring(L"…"):std::to_wstring(percentage) + L"%";
        SetTextColor(item.hDC,palette.high_contrast?palette.text:RGB(255,255,255));
        if(entry.available&&!(catalog_level(page.location)==CatalogLevel::courses&&snapshot.total==0))
            DrawTextW(item.hDC,percent.c_str(),static_cast<int>(percent.size()),&bounds,DT_SINGLELINE | DT_RIGHT | DT_NOPREFIX);
        auto meter = card; meter.left += px(20); meter.right -= px(20);meter.bottom-=px(20);meter.top=meter.bottom-px(lesson?6:8);
        const auto track = CreateSolidBrush(palette.high_contrast?palette.outline:RGB(48,50,58)); FillRect(item.hDC,&meter,track); DeleteObject(track);
        meter.right = meter.left + MulDiv(meter.right - meter.left,static_cast<int>(percentage),100);
        const auto fill = CreateSolidBrush(accent()); FillRect(item.hDC,&meter,fill); DeleteObject(fill);
        if(item.itemState&ODS_FOCUS)reader_theme::paint_keyboard_focus(list,item.hDC,card,palette.muted);
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
        case scroll_message:self->position_header(static_cast<int>(wparam));return 0;
        case WM_MOUSEWHEEL:if(catalog_level(self->page.location)==CatalogLevel::routes)return SendMessageW(self->list,message,wparam,lparam);break;
        case WM_DPICHANGED_AFTERPARENT: self->update_font(); self->resize(); return 0;
        case WM_COMMAND: self->command(LOWORD(wparam),HIWORD(wparam)); return 0;
        case WM_KEYDOWN: if(wparam==VK_ESCAPE&&self->navigate_back())return 0;break;
        case activate_message: self->activate(); return 0;
        case WM_MEASUREITEM: {
            auto& item = *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
            if (measure_native_menu(item)) return TRUE;
            if (item.CtlID == list_id) { item.itemHeight = self->row_height(item.itemID); return TRUE; } break;
        }
        case WM_DRAWITEM: {
            const auto& item = *reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (draw_native_menu(item)) return TRUE;
            if (item.CtlID == list_id) { self->draw(item); return TRUE; }
            if (item.CtlID == back_id) {
                const auto fill = CreateSolidBrush(self->accent()); FillRect(item.hDC,&item.rcItem,fill); DeleteObject(fill);
                auto bounds = item.rcItem; InflateRect(&bounds,-self->px(20),-self->px(10)); draw_icon(item.hDC,bounds,Icon::back,reader_theme::accent_foreground(window)); return TRUE;
            }
            if(item.CtlID==home_id||item.CtlID==menu_id){
                (void)reader_theme::draw_button(item);auto bounds=item.rcItem;InflateRect(&bounds,-self->px(14),-self->px(14));
                const auto colors=reader_theme::palette(window);
                draw_icon(item.hDC,bounds,item.CtlID==home_id?Icon::home:Icon::settings,colors.high_contrast?colors.text:RGB(255,255,255));return TRUE;
            }
            if (reader_theme::draw_button(item)) return TRUE; break;
        }
        case WM_MENUCHAR: {
            LRESULT result = 0;
            if (native_menu_character(reinterpret_cast<HMENU>(lparam),static_cast<wchar_t>(LOWORD(wparam)),result)) return result;
            break;
        }
        // Android catalog cards have only their normal activation. Scoped
        // options remain on the original Settings icon, not an extra menu.
        case WM_CONTEXTMENU:return 0;
        case WM_ERASEBKGND: { RECT area{}; GetClientRect(window,&area); FillRect(reinterpret_cast<HDC>(wparam),&area,reader_theme::brush(reader_theme::Surface::screen)); return 1; }
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
    std::function<void(const std::filesystem::path&)> on_open,CatalogActionHandler on_action,CatalogActionAvailability can_action,
    CatalogMathValidator validate_math,CatalogLocationChanged location_changed) {
    static const bool registered = [] {
        WNDCLASSW type{}; type.lpfnWndProc = catalog_proc; type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr,IDC_ARROW); type.lpszClassName = catalog_class;
        return RegisterClassW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }();
    if (!registered) throw std::runtime_error("No se pudo registrar el catálogo.");
    auto data = std::make_unique<CatalogView>(environment,root,std::move(on_open),std::move(on_action),std::move(can_action),std::move(validate_math),std::move(location_changed));
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
    const auto previous=self->page.location;
    const auto selection=SendMessageW(self->list,LB_GETCURSEL,0,0),top=SendMessageW(self->list,LB_GETTOPINDEX,0,0);
    const auto scroll=card_list_scroll_offset(self->list);
    // Refresh is a read, including while the catalogue is hidden behind a live
    // lesson. Saving here invalidates that lesson's optimistic StateStore and
    // makes the very next live Settings change look like an external writer.
    self->render(location,false);
    if(location==previous){
        SendMessageW(self->list,LB_SETCURSEL,selection,0);SendMessageW(self->list,LB_SETTOPINDEX,top,0);
        set_card_list_scroll_offset(self->list,scroll);self->update_selection();
    }
}
void navigate_catalog_home(HWND window) { auto location = catalog_location(window); location.route.clear(); location.course.clear(); location.unit.clear(); navigate_catalog(window,location); }
bool navigate_catalog_back(HWND window){
    if(!IsWindow(window))return false;wchar_t name[96]{};GetClassNameW(window,name,96);if(std::wstring_view(name)!=catalog_class)return false;
    auto* self=view(window);return self&&self->navigate_back();
}
} // namespace lgaed::presentation

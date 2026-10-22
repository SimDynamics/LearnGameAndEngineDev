#include "core/profile.hpp"
#include "core/learning_flow.hpp"
#include "platform/windows/environment.hpp"
#include "native_build_config.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/native_menu.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/rich_text.hpp"

#include <windows.h>
#include <shellapi.h>
#include <ole2.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {

constexpr wchar_t window_class[] = L"LearnGameAndEngineDev.NativeShell";
constexpr UINT_PTR smoke_timer = 1;
constexpr DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr UINT open_lesson_message=WM_APP+230,return_message=WM_APP+231,settings_message=WM_APP+232,close_settings_message=WM_APP+233,tool_message=WM_APP+234;
constexpr UINT lesson_finished_message=WM_APP+235;

struct WindowState {
    lgaed::WindowsEnvironment environment;
    bool painted = false;
    int exit_code = 0;
    HWND lesson = nullptr;
    HWND catalog = nullptr,settings=nullptr,tool=nullptr;
    std::shared_ptr<lgaed::windows::NarrationWorker> narrator;
    std::unique_ptr<lgaed::presentation::LessonReader> reader;
    std::filesystem::path pending_lesson;
    lgaed::curriculum::CatalogLocation context;
    lgaed::presentation::CatalogAction pending_action=lgaed::presentation::CatalogAction::settings;
    std::string startup_error;
};

lgaed::state::StateData preferences(WindowState& state){
    if(state.lesson)return lgaed::presentation::read_lesson_state(state.lesson);
    lgaed::state::StateStore store(state.environment.paths.root);return store.load();
}
void update_preferences(WindowState& state,const lgaed::presentation::PreferencesMutation& change){
    if(state.lesson)lgaed::presentation::update_lesson_preferences(state.lesson,change);
    else{lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();change(data);store.save(data);}
}

std::wstring widen_error(const std::exception& error) {
    const std::string message(error.what());
    const int size = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return L"No se pudo iniciar la base nativa Windows.";
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, result.data(), size);
    result.pop_back();
    return result;
}

void layout_controls(HWND window, WindowState& state) {
    RECT area{};GetClientRect(window,&area);
    for(const auto view:{state.catalog,state.lesson,state.settings,state.tool})if(view)MoveWindow(view,0,0,area.right,area.bottom,TRUE);
}

// Navigation has one current destination. The lesson's existing store owns its
// page, drafts and bounded reading positions; no second history is created.
void remember_open_lesson(WindowState& state,std::string_view lesson_id){
    lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();
    const auto previous=lgaed::state::string_value(data,"native_windows","navigation/lesson");
    if(previous==lesson_id)return;
    if(lesson_id.empty())data.preferences["native_windows"].erase("navigation/lesson");
    else data.preferences["native_windows"]["navigation/lesson"]=std::string(lesson_id);
    store.save(data);
}

void show_catalog(HWND window,WindowState& state){
    if(state.settings){DestroyWindow(state.settings);state.settings=nullptr;}
    if(state.tool){DestroyWindow(state.tool);state.tool=nullptr;}
    if(state.lesson){state.reader.reset();if(!lgaed::presentation::flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
    remember_open_lesson(state,{});
    lgaed::presentation::refresh_catalog_view(state.catalog);ShowWindow(state.catalog,SW_SHOW);layout_controls(window,state);SetFocus(state.catalog);
}
void open_lesson(HWND window,WindowState& state,lgaed::curriculum::Lesson lesson){
    using namespace lgaed::presentation;
    const auto lesson_title=lesson.title;
    const auto ui_locale=interface_language(preferences(state));
    const auto tr=[&](const char* es,const char* en,const char* fr){return std::string(ui_locale=="fr"?fr:ui_locale=="en"?en:es);};
    std::string lesson_label=tr("Lección","Lesson","Leçon");
    const auto context=catalog_location(state.catalog);
    if(!context.unit.empty()){
        const auto page=lgaed::windows::load_catalog(std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",context);
        for(const auto& entry:page.entries)if(entry.id==lesson.id){lesson_label+=' '+std::to_string(entry.number);break;}
    }
    if(state.lesson){state.reader.reset();if(!flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
    remember_open_lesson(state,lesson.id);
    state.lesson=create_lesson_view(window,state.environment,std::move(lesson));ShowWindow(state.catalog,SW_HIDE);
    LessonViewHooks hooks;hooks.lesson_label=lesson_label;hooks.back_to_catalog=[window](HWND){PostMessageW(window,return_message,0,0);};
    hooks.validate_math=[backend=state.narrator](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){
        return backend->validate_math(block,answers,locale,stop);};
    hooks.content_changed=[&state](HWND){if(state.reader)state.reader->content_changed();};
    hooks.feedback_changed=[&state](HWND,std::size_t block,bool correct){if(state.reader)state.reader->feedback(block,correct);};
    hooks.before_page_change=[&state](HWND){if(state.reader)state.reader->pause();};
    hooks.before_destroy=[&state](HWND){if(state.reader)state.reader->pause();};
    hooks.lesson_completed=[window](HWND){PostMessageW(window,lesson_finished_message,0,0);};
    hooks.reader_requested=[&state](HWND){if(state.reader)state.reader->toggle_controls();};
    hooks.settings_requested=[window,&state](HWND){state.pending_action=CatalogAction::settings;state.context=lgaed::presentation::catalog_location(state.catalog);const auto current=lgaed::presentation::lesson_page_state(state.lesson);
        if(current.lesson_id=="native_cpp_laboratory")state.context={current.locale};else state.context.lesson=current.lesson_id;PostMessageW(window,settings_message,0,0);};
    hooks.language_requested=[&state](HWND owner){
        using namespace lgaed::presentation;
        const auto selected=interface_language(preferences(state));
        const auto menu=CreatePopupMenu();if(!menu)return;
        constexpr std::array<const wchar_t*,3> labels{L"Español",L"English",L"Français"};
        constexpr std::array<const char*,3> locales{"es","en","fr"};
        for(std::size_t index=0;index<labels.size();++index)
            AppendMenuW(menu,MF_STRING|(selected==locales[index]?MF_CHECKED:0),8101+index,labels[index]);
        RECT anchor{};const auto button=GetDlgItem(owner,112);
        GetWindowRect(button?button:owner,&anchor);
        const auto style=prepare_native_menu(menu,owner);
        const auto command=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,anchor.left,anchor.bottom,owner,nullptr);
        DestroyMenu(menu);
        if(command<8101||command>8103)return;
        const auto locale=std::string(locales[command-8101]);if(locale==selected)return;
        // This shortcut changes only menu/control language. The original
        // curriculum, narration language, editor and playback stay in place.
        update_preferences(state,[&](auto& data){data.preferences["native_windows"]["ui_language"]=locale;});
        if(state.reader)state.reader->settings_changed();
    };
    // Attempts are a route-specific status in the catalog; the native personal
    // build does not invent a second commercial heart balance.
    hooks.attempts_requested=[&state,lesson_title](HWND owner){const auto current=lgaed::presentation::lesson_page_state(state.lesson);
        const auto popup_locale=lgaed::presentation::interface_language(preferences(state));
        const auto content=lesson_title+'\n'+(popup_locale=="fr"?"Page ":popup_locale=="en"?"Page ":"Página ")+std::to_string(current.index+1)+(popup_locale=="fr"?" sur ":popup_locale=="en"?" of ":" de ")+std::to_string(current.count);
        MessageBoxW(owner,lgaed::presentation::wide(content).c_str(),popup_locale=="fr"?L"Progression de la leçon":popup_locale=="en"?L"Lesson progress":L"Progreso de la lección",MB_OK|MB_ICONINFORMATION);};
    set_lesson_view_hooks(state.lesson,std::move(hooks));
    state.reader=std::make_unique<LessonReader>(state.lesson,state.environment,state.narrator,[window]{PostMessageW(window,settings_message,0,0);});
    layout_controls(window,state);SetFocus(state.lesson);
}
void open_settings(HWND window,WindowState& state){
    using namespace lgaed::presentation;if(state.settings)return;
    if(state.lesson&&!flush_lesson_view(state.lesson))return;
    SettingsAccess access;access.read=[&state]{return preferences(state);};access.update=[&state](const PreferencesMutation& change){update_preferences(state,change);};
    access.changed=[&state]{if(state.reader)state.reader->settings_changed();};access.close=[window]{PostMessageW(window,close_settings_message,0,0);};
    const auto context=state.context;
    access.course_id=context.course;access.route_id=context.route;access.programming=context.route=="programming";
    const auto locale=interface_language(preferences(state));
    const auto tr=[&](const char* es,const char* en,const char* fr){return std::string(locale=="fr"?fr:locale=="en"?en:es);};
    const auto content=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content";
    const auto catalogue_locale=lgaed::state::string_value(preferences(state),"native_windows","content_language","es");
    for(const auto& entry:lgaed::windows::load_catalog(content,{catalogue_locale,"programming"}).entries)
        access.programming_course_ids.push_back(entry.id);
    for(const auto& entry:lgaed::windows::load_catalog(content,{catalogue_locale,"math_physics"}).entries)
        access.math_course_ids.push_back(entry.id);
    access.reset_route=[window,&state,content,catalogue_locale,locale](std::string_view route){
        if(route!="programming"&&route!="math_physics")throw std::invalid_argument("Unknown reset route");
        const auto scope=lgaed::windows::catalog_scope(content,{catalogue_locale,std::string(route)});
        const auto prompt=locale=="fr"?L"Réinitialiser uniquement la progression et les réponses de ce parcours ? Les programmes, les ressources et l’autre parcours seront conservés.":locale=="en"?L"Reset only this learning path’s progress and answers? Programs, resources and the other path will be kept.":L"¿Reiniciar únicamente el progreso y las respuestas de esta ruta? Se conservarán los programas, los recursos y la otra ruta.";
        const auto title=locale=="fr"?L"Réinitialiser le parcours":locale=="en"?L"Reset learning path":L"Reiniciar ruta";
        if(MessageBoxW(window,prompt,title,MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
        if(state.reader)state.reader->pause();
        if(state.lesson){state.reader.reset();if(!flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
        lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();
        lgaed::curriculum::reset_catalog_progress(data,scope);store.save(data);PostMessageW(window,close_settings_message,0,0);
    };
    if(state.pending_action==CatalogAction::audio)access.scope=SettingsScope::audio;
    else if(!context.lesson.empty())access.scope=SettingsScope::lesson;
    else if(!context.unit.empty())access.scope=SettingsScope::unit;
    else if(!context.course.empty())access.scope=SettingsScope::course;
    if(access.scope!=SettingsScope::global&&access.scope!=SettingsScope::audio){
        auto parent=context;std::string target;
        if(access.scope==SettingsScope::lesson){target=parent.lesson;parent.lesson.clear();access.subtitle=tr("Ajustes de la lección","Lesson settings","Réglages de la leçon");access.reset_label=tr("Reiniciar lección","Reset lesson","Réinitialiser la leçon");}
        if(access.scope==SettingsScope::unit){target=parent.unit;parent.unit.clear();access.subtitle=tr("Ajustes de la unidad","Unit settings","Réglages de l’unité");access.reset_label=tr("Reiniciar unidad","Reset unit","Réinitialiser l’unité");}
        if(access.scope==SettingsScope::course){target=parent.course;parent.course.clear();access.subtitle=tr("Ajustes del curso","Course settings","Réglages du cours");access.reset_label=tr("Reiniciar curso","Reset course","Réinitialiser le cours");}
        const auto page=lgaed::windows::load_catalog(std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",parent);
        for(const auto& entry:page.entries)if(entry.id==target){access.title=entry.title;
            access.eyebrow=(access.scope==SettingsScope::lesson?tr("Lección ","Lesson ","Leçon "):access.scope==SettingsScope::unit?tr("Unidad ","Unit ","Unité "):tr("Curso ","Course ","Cours "))+std::to_string(entry.number);break;}
    }
    if(!context.route.empty()){
        const auto scope=lgaed::windows::catalog_scope(std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",context);
        access.reset_scope=[window,&state,scope,locale]{
            const auto prompt=locale=="fr"?L"Réinitialiser uniquement la progression et les réponses de ce contenu ? Les programmes et les ressources installées seront conservés.":locale=="en"?L"Reset only this content’s progress and answers? Programs and installed resources will be kept.":L"¿Reiniciar únicamente el progreso y las respuestas de este contenido? Se conservarán los programas y recursos instalados.";
            const auto title=locale=="fr"?L"Réinitialiser la progression":locale=="en"?L"Reset progress":L"Reiniciar progreso";
            if(MessageBoxW(window,prompt,title,MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
            if(state.reader)state.reader->pause();if(state.lesson){state.reader.reset();if(!flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
            lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();lgaed::curriculum::reset_catalog_progress(data,scope);store.save(data);PostMessageW(window,close_settings_message,0,0);};
    }
    state.settings=create_settings_view(window,std::move(access));ShowWindow(state.catalog,SW_HIDE);if(state.lesson)ShowWindow(state.lesson,SW_HIDE);if(state.tool)ShowWindow(state.tool,SW_HIDE);layout_controls(window,state);SetFocus(state.settings);
}

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state) {
        return DefWindowProcW(window, message, wparam, lparam);
    }
    LRESULT themed{};
    if (lgaed::presentation::reader_theme::control_color(message, wparam, lparam, themed)) return themed;
    switch (message) {
    case WM_ERASEBKGND: {
        RECT area{}; GetClientRect(window, &area);
        FillRect(reinterpret_cast<HDC>(wparam), &area,
            lgaed::presentation::reader_theme::brush(lgaed::presentation::reader_theme::Surface::canvas));
        return 1;
    }
    case WM_GETMINMAXINFO: {
        const auto dpi = GetDpiForWindow(window);
        RECT minimum{0, 0, MulDiv(640, static_cast<int>(dpi), 96),
            MulDiv(390, static_cast<int>(dpi), 96)};
        if (AdjustWindowRectExForDpi(&minimum, window_style, FALSE, 0, dpi)) {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        }
        return 0;
    }
    case WM_CREATE: {
        {
            try {
                // Defaults affect only absent menu/content preferences. Imported
                // speech choices, drafts and reading positions remain intact.
                lgaed::state::StateStore initial(state->environment.paths.root);auto defaults=initial.load();
                const auto original=defaults;wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
                const auto locale=GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH)?std::wstring(name):L"";
                const std::string fallback=locale.starts_with(L"es")?"es":locale.starts_with(L"fr")?"fr":"en";
                for(const auto key:{"ui_language","content_language"})
                    if(!lgaed::state::find(defaults,"native_windows",key))defaults.preferences["native_windows"][key]=fallback;
                if(defaults!=original)initial.save(defaults);
                state->narrator=std::make_shared<lgaed::windows::NarrationWorker>(std::filesystem::path(lgaed::build::source_root)/L"out/narration/runtime.json");
                state->catalog=lgaed::presentation::create_catalog_view(window,state->environment,
                    std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",
                    [window,state](const auto& path){state->pending_lesson=path;PostMessageW(window,open_lesson_message,0,0);},
                    [window,state](auto action,const auto& context){state->context=context;state->pending_action=action;
                        PostMessageW(window,action==lgaed::presentation::CatalogAction::settings||action==lgaed::presentation::CatalogAction::audio?settings_message:tool_message,0,0);},
                    [](auto,const auto&){return true;});
                layout_controls(window, *state);
                if(!state->environment.options.smoke_test){
                    const auto active=lgaed::state::string_value(defaults,"native_windows","navigation/lesson");
                    if(active=="native_cpp_laboratory"){
                        state->pending_action=lgaed::presentation::CatalogAction::compiler;
                        PostMessageW(window,tool_message,0,0);
                    }else if(const auto entry=lgaed::windows::active_catalog_lesson(
                        std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",
                        lgaed::presentation::catalog_location(state->catalog),active)){
                        state->pending_lesson=entry->asset;PostMessageW(window,open_lesson_message,0,0);
                    }else if(!active.empty())remember_open_lesson(*state,{});
                }
                return 0;
            } catch (const std::exception& error) {
                state->startup_error = error.what();
                return -1;
            }
        }
    }
    case open_lesson_message:
        try{const auto locale=lgaed::presentation::catalog_location(state->catalog).locale;
            auto path=state->pending_lesson;const auto content=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content";
            const auto relative=path.lexically_relative(content);const auto actual=relative.empty()?locale:relative.begin()->string();
            open_lesson(window,*state,lgaed::windows::load_lesson(path,actual));}
        catch(const std::exception& error){show_catalog(window,*state);MessageBoxW(window,widen_error(error).c_str(),L"Abrir lección",MB_OK|MB_ICONERROR);}
        return 0;
    case return_message:show_catalog(window,*state);return 0;
    case lesson_finished_message:{
        const auto data=preferences(*state);
        auto where=lgaed::presentation::catalog_location(state->catalog);
        const auto flow=lgaed::curriculum::learning_flow(data,where.course);
        where.lesson=lgaed::presentation::lesson_page_state(state->lesson).lesson_id;
        if(flow.between_lessons&&!where.unit.empty()){
            const auto root=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content";
            if(const auto next=lgaed::windows::next_catalog_lesson(root,where,flow.between_units,flow.between_courses)){
                auto list=*next;list.lesson.clear();const auto catalog=lgaed::windows::load_catalog(root,list);
                const auto entry=std::ranges::find_if(catalog.entries,[&](const auto& value){return value.id==next->lesson;});
                if(entry==catalog.entries.end()||!entry->available){show_catalog(window,*state);return 0;}
                state->pending_lesson=entry->asset;lgaed::presentation::navigate_catalog(state->catalog,list);
                PostMessageW(window,open_lesson_message,0,0);return 0;
            }
        }
        show_catalog(window,*state);return 0;
    }
    case settings_message:
        try{open_settings(window,*state);}catch(const std::exception& error){MessageBoxW(window,widen_error(error).c_str(),L"Opciones",MB_OK|MB_ICONERROR);}return 0;
    case close_settings_message:{if(state->settings){DestroyWindow(state->settings);state->settings=nullptr;}
        if(state->lesson){ShowWindow(state->lesson,SW_SHOW);if(state->reader)state->reader->settings_changed();}
        else if(state->tool)ShowWindow(state->tool,SW_SHOW);
        else{auto where=lgaed::presentation::catalog_location(state->catalog);const auto data=preferences(*state);where.locale=lgaed::state::string_value(data,"native_windows","content_language",where.locale);lgaed::presentation::navigate_catalog(state->catalog,where);ShowWindow(state->catalog,SW_SHOW);}
        layout_controls(window,*state);return 0;}
    case tool_message:
        try{using lgaed::presentation::CatalogAction;
            if(state->tool){DestroyWindow(state->tool);state->tool=nullptr;}
            if(state->pending_action==CatalogAction::compiler){lgaed::curriculum::Lesson lab;lab.id="native_cpp_laboratory";
                const auto data=preferences(*state);const auto ui=lgaed::presentation::interface_language(data);
                lab.title=ui=="fr"?"Laboratoire C++":ui=="en"?"C++ laboratory":"Laboratorio C++";
                lab.locale=lgaed::state::string_value(data,"native_windows","content_language","es");
                lab.source_path=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content"/lab.locale/L"programming";
                lgaed::curriculum::Block compiler;compiler.type=lgaed::curriculum::BlockType::cpp_compiler;compiler.stable_id="native_lab_editor";compiler.initial_code="#include <iostream>\n\nint main() {\n    std::cout << \"Hola\" << '\\n';\n    return 0;\n}\n";compiler.standard_input="";
                lgaed::curriculum::Page page;page.stable_id="native_lab";page.blocks.push_back(std::move(compiler));lab.pages.push_back(std::move(page));open_lesson(window,*state,std::move(lab));}
            else{ShowWindow(state->catalog,SW_HIDE);state->tool=state->pending_action==CatalogAction::calculator?
                    lgaed::presentation::create_calculator_view(window,state->environment,[window]{PostMessageW(window,return_message,0,0);}):
                    lgaed::presentation::create_graphics_view(window,state->environment,[window]{PostMessageW(window,return_message,0,0);});layout_controls(window,*state);}}
        catch(const std::exception& error){MessageBoxW(window,widen_error(error).c_str(),L"Herramientas",MB_OK|MB_ICONERROR);}return 0;
    case WM_SIZE:
        layout_controls(window, *state);
        return 0;
    case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<const RECT*>(lparam);
        SetWindowPos(window, nullptr, suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        layout_controls(window, *state);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (!state->painted) {
            state->painted = true;
            try {
                lgaed::write_qa_window_record(state->environment, GetCurrentProcessId(),
                    GetDpiForWindow(window), state->environment.options.smoke_test);
                if (state->environment.options.smoke_test
                    && !SetTimer(window, smoke_timer, 3000, nullptr)) {
                    throw std::runtime_error("Cannot schedule smoke-test window closure");
                }
            } catch (const std::exception&) {
                state->exit_code = 3;
                PostMessageW(window, WM_CLOSE, 0, 0);
            }
        }
        return 0;
    }
    case WM_TIMER:
        if (wparam == smoke_timer) {
            KillTimer(window, smoke_timer);
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        if(state->reader)state->reader->pause();
        if (state->lesson && !lgaed::presentation::flush_lesson_view(state->lesson)) return 0;
        state->reader.reset();
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(state->exit_code);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    bool qa_requested = false;
    try {
        struct OleApartment {
            OleApartment() { if (FAILED(OleInitialize(nullptr))) throw std::runtime_error("Cannot initialize native Windows controls"); }
            ~OleApartment() { OleUninitialize(); }
        } apartment;
        static_assert(sizeof(void*) == 8, "The native application requires a 64-bit target");
        const auto default_profile = lgaed::parse_profile(lgaed::build::default_profile);
        if (!default_profile) {
            throw std::runtime_error("The configured default profile is invalid");
        }
        int count = 0;
        auto** values = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!values) {
            throw std::runtime_error("Cannot parse the Unicode command line");
        }
        const auto free_arguments = [](wchar_t** value) { LocalFree(value); };
        const std::unique_ptr<wchar_t*, decltype(free_arguments)> arguments(values, free_arguments);
        std::vector<std::wstring_view> options;
        for (int i = 1; i < count; ++i) {
            options.emplace_back(values[i]);
            if (options.back() == L"--smoke-test" || options.back() == L"--qa-instance") {
                qa_requested = true;
            }
        }
        auto environment = lgaed::resolve_windows_environment(
            lgaed::parse_launch_options(options, *default_profile));
        lgaed::initialize_profile_directories(environment.paths);
        WindowState state{std::move(environment)};

        WNDCLASSEXW definition{};
        definition.cbSize = sizeof(definition);
        definition.lpfnWndProc = window_procedure;
        definition.hInstance = instance;
        definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        definition.hbrBackground = lgaed::presentation::reader_theme::brush(
            lgaed::presentation::reader_theme::Surface::canvas);
        definition.lpszClassName = window_class;
        if (!RegisterClassExW(&definition)) {
            throw std::runtime_error("Cannot register the native application window");
        }
        auto title = L"LearnGameAndEngineDev — "
            + std::wstring(lgaed::profile_name(state.environment.options.profile));
        if (state.environment.options.qa_instance) {
            title += L" (QA " + *state.environment.options.qa_instance + L")";
        }
        const auto initial_dpi = GetDpiForSystem();
        RECT initial_size{0, 0, MulDiv(900, static_cast<int>(initial_dpi), 96),
            MulDiv(state.environment.options.smoke_test ? 440 : 720, static_cast<int>(initial_dpi), 96)};
        if (!AdjustWindowRectExForDpi(&initial_size, window_style, FALSE, 0, initial_dpi)) {
            throw std::runtime_error("Cannot calculate the initial DPI-aware window size");
        }
        const auto window = CreateWindowExW(0, window_class, title.c_str(), window_style,
            CW_USEDEFAULT, CW_USEDEFAULT, initial_size.right - initial_size.left,
            initial_size.bottom - initial_size.top, nullptr, nullptr, instance, &state);
        if (!window) {
            throw std::runtime_error(state.startup_error.empty() ? "Cannot create the native application window" : state.startup_error);
        }
        ShowWindow(window, show_command);
        UpdateWindow(window);
        MSG message{};
        BOOL result = 0;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result == -1 ? 4 : static_cast<int>(message.wParam);
    } catch (const std::exception& error) {
        const auto text = widen_error(error);
        if (qa_requested) {
            OutputDebugStringW(text.c_str());
        } else {
            MessageBoxW(nullptr, text.c_str(), L"LearnGameAndEngineDev", MB_OK | MB_ICONERROR);
        }
        return 2;
    }
}

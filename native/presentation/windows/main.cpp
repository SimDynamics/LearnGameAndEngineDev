#include "core/profile.hpp"
#include "core/learning_flow.hpp"
#include "core/attempts.hpp"
#include "platform/windows/environment.hpp"
#include "native_build_config.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/code_editor.hpp"
#include "presentation/windows/catalog_view.hpp"
#include "presentation/windows/settings_view.hpp"
#include "presentation/windows/audio_course_view.hpp"
#include "presentation/windows/narration_licenses.hpp"
#include "platform/windows/narration_storage.hpp"
#include "platform/windows/system_speech.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/tools_view.hpp"
#include "presentation/windows/smo_view.hpp"
#include "presentation/windows/math_puzzle_view.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/native_menu.hpp"
#include "presentation/windows/internal_window.hpp"
#include "presentation/windows/math_expression_editor.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/audio_generation.hpp"
#include "presentation/windows/rich_text.hpp"

#include <windows.h>
#include <shellapi.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {

constexpr wchar_t window_class[] = L"LearnGameAndEngineDev.NativeShell";
constexpr UINT_PTR smoke_timer = 1;
constexpr UINT_PTR audio_context_timer = 2;
constexpr DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr UINT open_lesson_message=WM_APP+230,return_message=WM_APP+231,settings_message=WM_APP+232,close_settings_message=WM_APP+233,tool_message=WM_APP+234;
constexpr UINT lesson_finished_message=WM_APP+235;
constexpr UINT application_language_message=WM_APP+237;
constexpr UINT lesson_option_message=WM_APP+238;

struct WindowState {
    lgaed::WindowsEnvironment environment;
    bool painted = false;
    int exit_code = 0;
    HWND lesson = nullptr;
    HWND catalog = nullptr,settings=nullptr,tool=nullptr;
    std::shared_ptr<lgaed::windows::NarrationWorker> narrator;
    std::unique_ptr<lgaed::windows::AudioGeneration> generation;
    bool graphics_active=false;
    std::optional<lgaed::curriculum::CatalogLocation> audio_location;
    std::optional<std::size_t> audio_page;
    bool audio_graphics=false;
    std::unique_ptr<lgaed::presentation::LessonReader> reader;
    std::filesystem::path pending_lesson;
    lgaed::curriculum::CatalogLocation context;
    lgaed::presentation::CatalogAction pending_action=lgaed::presentation::CatalogAction::settings;
    std::string startup_error;
    HWND settings_lesson=nullptr,settings_focus=nullptr;
    std::optional<std::size_t> pending_option_page;
    std::optional<lgaed::presentation::LessonOptionAction> pending_option_action;
    ~WindowState(){
        // Also covers a failed startup or an external DestroyWindow path. The
        // service must unblock users before member destruction joins them.
        if(narrator)narrator->shutdown();
        generation.reset();reader.reset();
    }
};

lgaed::state::StateData preferences(WindowState& state){
    if(state.lesson)return lgaed::presentation::read_lesson_state(state.lesson);
    lgaed::state::StateStore store(state.environment.paths.root);return store.load();
}
void update_audio_context(WindowState&,bool force=false);
void update_preferences(WindowState& state,const lgaed::presentation::PreferencesMutation& change){
    if(state.lesson)lgaed::presentation::update_lesson_preferences(state.lesson,change);
    else{lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();change(data);store.save(data);}
    update_audio_context(state,true);
}
void update_audio_context(WindowState& state,bool force){
    if(!state.generation||!state.catalog)return;
    auto where=lgaed::presentation::catalog_location(state.catalog);
    std::optional<std::size_t> page;
    if(state.lesson){const auto current=lgaed::presentation::lesson_page_state(state.lesson);
        if(current.lesson_id!="native_cpp_laboratory"){where.lesson=current.lesson_id;page=current.index;}}
    // Navigation polling is only an inexpensive HWND/context comparison. Do not
    // reread/hash the student's complete store on every UI timer tick.
    if(!force&&state.audio_location==where&&state.audio_page==page&&state.audio_graphics==state.graphics_active)return;
    state.audio_location=where;state.audio_page=page;state.audio_graphics=state.graphics_active;
    const auto data=preferences(state);
    const auto raw_limit=lgaed::state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes",-1);
    state.narrator->set_storage_budget(raw_limit<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(raw_limit));
    where.locale=lgaed::state::string_value(data,"native_windows","content_language",where.locale);
    auto selection=lgaed::windows::audio_generation_selection(data,std::move(where),page);
    if(state.graphics_active)selection.enabled=false;
    state.generation->update(std::move(selection));
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
    for(const auto view:{state.catalog,state.lesson,state.tool,state.settings})if(view)MoveWindow(view,0,0,area.right,area.bottom,TRUE);
    lgaed::presentation::layout_internal_windows(window);
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
    state.graphics_active=false;
    if(state.settings){DestroyWindow(state.settings);state.settings=nullptr;}
    if(state.tool){DestroyWindow(state.tool);state.tool=nullptr;}
    if(state.lesson){state.reader.reset();if(!lgaed::presentation::flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
    remember_open_lesson(state,{});
    lgaed::presentation::refresh_catalog_view(state.catalog);ShowWindow(state.catalog,SW_SHOW);layout_controls(window,state);SetFocus(state.catalog);
}
void navigate_back(HWND window,WindowState& state){
    // IsDialogMessage turns Escape over ordinary buttons into IDCANCEL;
    // editors that request the key use their existing local escape route.
    // Both paths retain the current view's normal save/close policy.
    if(const auto keyboard=lgaed::presentation::math_keyboard_window(window);keyboard&&IsWindowVisible(keyboard)){
        lgaed::presentation::hide_math_keyboard();return;
    }
    if(const auto internal=lgaed::presentation::active_internal_window(window)){
        PostMessageW(internal,WM_CLOSE,0,0);return;
    }
    if(state.settings&&IsWindow(state.settings)){
        PostMessageW(state.settings,WM_CLOSE,0,0);return;
    }
    if(state.lesson){(void)lgaed::presentation::navigate_lesson_back(state.lesson);return;}
    if(state.tool){PostMessageW(window,return_message,0,0);return;}
    if(state.catalog)(void)lgaed::presentation::navigate_catalog_back(state.catalog);
}
bool escape_has_local_popup(HWND window){
    GUITHREADINFO info{sizeof(info)};
    if(GetGUIThreadInfo(GetCurrentThreadId(),&info)&&
       (info.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE)))return true;
    for(auto item=GetFocus();item&&item!=window;item=GetParent(item)){
        wchar_t name[64]{};GetClassNameW(item,name,64);
        if(std::wstring_view(name)==L"ComboBox"&&SendMessageW(item,CB_GETDROPPEDSTATE,0,0))return true;
    }
    return false;
}
void open_lesson(HWND window,WindowState& state,lgaed::curriculum::Lesson lesson){
    using namespace lgaed::presentation;
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
    LessonViewHooks hooks;hooks.lesson_label=lesson_label;hooks.back_to_catalog=[window](HWND){PostMessageW(window,return_message,0,0);};
    hooks.validate_math=[backend=state.narrator](std::string_view block,std::string_view answers,std::string_view locale,std::stop_token stop){
        return backend->validate_math(block,answers,locale,stop);};
    hooks.content_changed=[&state](HWND){if(state.reader)state.reader->content_changed();update_audio_context(state);};
    hooks.priority_work=[backend=state.narrator](std::stop_token stop)->std::shared_ptr<void>{
        return std::make_shared<lgaed::windows::NarrationPriorityLease>(backend->heavy_work_lease(stop));};
    hooks.feedback_changed=[&state](HWND,std::size_t block,bool correct){if(state.reader)state.reader->feedback(block,correct);};
    hooks.before_page_change=[&state](HWND){if(state.reader)state.reader->pause();};
    hooks.before_destroy=[&state](HWND){if(state.reader)state.reader->pause();};
    hooks.lesson_completed=[window](HWND){PostMessageW(window,lesson_finished_message,0,0);};
    hooks.reader_requested=[&state](HWND){if(state.reader)state.reader->toggle_controls();};
    hooks.settings_requested=[window,&state](HWND){state.pending_action=CatalogAction::settings;state.context=lgaed::presentation::catalog_location(state.catalog);const auto current=lgaed::presentation::lesson_page_state(state.lesson);
        if(current.lesson_id=="native_cpp_laboratory")state.context={current.locale};else state.context.lesson=current.lesson_id;PostMessageW(window,settings_message,0,0);};
    hooks.language_requested=[&state](HWND owner){
        using namespace lgaed::presentation;
        const auto data=preferences(state);
        const auto locale=choose_interface_language(owner,data);if(!locale)return;
        auto candidate=data;apply_application_language(candidate,*locale);
        if(candidate==data)return;
        update_preferences(state,[&](auto& updated){apply_application_language(updated,*locale);});
        if(state.reader)state.reader->pause();
        PostMessageW(GetAncestor(owner,GA_ROOT),application_language_message,0,0);
        if(state.settings)refresh_settings_language(state.settings);
    };
    hooks.attempts_requested=[&state,route=context.route.empty()?std::string("programming"):context.route](HWND owner){
        toggle_attempt_details(owner,[&state]{return preferences(state);},route);};
    hooks.math_editor_request=[backend=state.narrator](std::string_view request){
        const auto command=winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(request)).GetNamedString(L"command",L"");
        return command==L"math_keyboard"?backend->math_keyboard(request):backend->math_editor(request);
    };
    hooks.calculator_requested=[&state](HWND owner,HWND field){
        if(!IsWindow(field)||!IsWindowEnabled(field)||!IsChild(owner,field))return;
        const auto page=lesson_page_state(owner);
        CalculatorPreferences access;
        access.read=[owner]{return read_lesson_state(owner);};
        access.update=[owner](const auto& update){if(IsWindow(owner))update_lesson_preferences(owner,update);};
        (void)open_calculator_window(owner,state.environment,state.narrator,[owner,field,page](std::string_view latex){
            if(!IsWindow(owner)||!IsWindow(field)||!IsChild(owner,field))return;
            const auto current=lesson_page_state(owner);
            if(current.lesson_id!=page.lesson_id||current.page_id!=page.page_id)return;
            (void)apply_math_calculator_result(field,latex);
        },std::move(access));
    };
    const bool laboratory=lesson.id=="native_cpp_laboratory";
    state.lesson=create_lesson_view(window,state.environment,std::move(lesson),std::move(hooks));ShowWindow(state.catalog,SW_HIDE);
    if(!laboratory)state.reader=std::make_unique<LessonReader>(state.lesson,state.environment,state.narrator,[window,&state]{
        state.pending_action=CatalogAction::audio;state.context=catalog_location(state.catalog);PostMessageW(window,settings_message,0,0);});
    if(state.settings&&IsWindow(state.settings)){
        state.settings_lesson=state.lesson;ShowWindow(state.lesson,SW_HIDE);
        if(state.reader)state.reader->suspend_preparation(true);
    }
    layout_controls(window,state);SetFocus(state.settings?state.settings:state.lesson);
}
void open_settings(HWND window,WindowState& state){
    using namespace lgaed::presentation;if(state.settings&&IsWindow(state.settings)){ShowWindow(state.settings,SW_SHOW);SetFocus(state.settings);return;}
    if(state.lesson&&!flush_lesson_view(state.lesson))return;
    hide_math_keyboard();
    if(state.reader)state.reader->pause();
    state.settings_lesson=state.lesson;state.settings_focus=GetFocus();state.pending_option_page.reset();state.pending_option_action.reset();
    SettingsAccess access;access.read=[&state]{return preferences(state);};access.update=[&state](const PreferencesMutation& change){update_preferences(state,change);};
    const auto initial_preferences=preferences(state);
    access.changed=[window,&state,previous_ui=interface_language(initial_preferences),previous_content=lgaed::state::string_value(initial_preferences,"native_windows","content_language","es")]()mutable{
        update_audio_context(state,true);if(state.reader)state.reader->settings_changed();
        const auto data=preferences(state);const auto ui=interface_language(data),content=lgaed::state::string_value(data,"native_windows","content_language","es");
        if(ui!=previous_ui||content!=previous_content){previous_ui=ui;previous_content=content;PostMessageW(window,application_language_message,0,0);}
    };access.close=[window]{PostMessageW(window,close_settings_message,0,0);};
    access.unsaved_discarded=[window](std::string_view message){show_settings_notice(window,message);};
    access.storage_snapshot=[&state]{
        const auto data=preferences(state);const auto raw=lgaed::state::integer_value(data,"lesson_narration_settings","offline_audio_budget_bytes",-1);
        const auto budget=raw<0?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(raw);
        const auto engine=lgaed::state::string_value(data,"lesson_narration_settings","engine","KOKORO");
        return engine=="WINDOWS"||engine=="ANDROID"?lgaed::windows::system_speech_storage_snapshot(state.environment.paths.cache,budget):
            lgaed::windows::narration_storage_snapshot(state.environment.paths.cache/L"kokoro",budget);
    };
    access.generation_status=[&state]{
        const auto data=preferences(state);const auto locale=interface_language(data);
        const auto tr=[&](const char* es,const char* en,const char* fr){return std::string(locale=="fr"?fr:locale=="en"?en:es);};
        if(!state.generation||!lgaed::windows::audio_generation_selection(data,{locale}).enabled)
            return tr("Generación anticipada desactivada.","Advance audio generation is disabled.","La génération anticipée des audios est désactivée.");
        const auto progress=state.generation->status();
        if(!progress.error.empty())return tr("No se ha podido continuar la generación de audios: ","Audio generation could not continue: ","La génération des audios ne peut pas continuer : ")+progress.error;
        if(progress.finished)return tr("Audios preparados para la voz, el motor y el idioma seleccionados.","Audio is ready for the selected voice, engine and language.","Les audios sont prêts pour la voix, le moteur et la langue sélectionnés.");
        if(!progress.total)return tr("Preparando el catálogo de audios…","Preparing the audio catalog…","Préparation du catalogue audio…");
        return tr("Páginas de audio preparadas: ","Audio pages ready: ","Pages audio prêtes : ")+std::to_string(progress.completed)+" / "+std::to_string(progress.total)+
            (progress.running?tr(" · Generando en segundo plano."," · Generating in the background."," · Génération en arrière-plan."):tr(" · Esperando para continuar."," · Waiting to continue."," · En attente de reprise."));
    };
    access.open_licenses=[window,&state]{(void)open_narration_licenses(state.settings?state.settings:window,interface_language(preferences(state)));};
    access.open_audio=[window,&state](std::string_view route){
        CourseAudioAccess audio;audio.worker=state.narrator;audio.content=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content";audio.cache=state.environment.paths.cache/L"kokoro";
        audio.profile=[&state]{auto data=preferences(state);CourseAudioProfile profile;profile.locale=lgaed::state::string_value(data,"native_windows","content_language",interface_language(data));
            // This resource manager belongs to the explicitly labelled Kokoro
            // block. Selecting Windows playback must not reinterpret its files
            // or feed Windows token IDs into the neural coverage planner.
            data.preferences["lesson_narration_settings"]["engine"]=std::string("KOKORO");
            const auto selection=lgaed::windows::audio_generation_selection(data,{profile.locale});profile.language=selection.language;profile.ui_language=interface_language(data);profile.voices=selection.voices;profile.engine="KOKORO";
            const auto enabled=lgaed::state::find(data,"lesson_narration_settings","enabled");profile.enabled=!enabled||!std::holds_alternative<bool>(*enabled)||std::get<bool>(*enabled);return profile;};
        audio.paused=[&state](const CourseAudioRef& course){const auto data=preferences(state);const auto value=lgaed::state::find(data,"lesson_narration_settings","kokoro_paused_course:"+course.route+":"+course.course);return value&&std::holds_alternative<bool>(*value)&&std::get<bool>(*value);};
        audio.pause=[&state](const CourseAudioRef& course,bool pause){update_preferences(state,[&](auto& data){data.preferences["lesson_narration_settings"]["kokoro_paused_course:"+course.route+":"+course.course]=pause;});};
        audio.idle_maintenance=[window,&state]()->std::shared_ptr<void>{
            if(state.reader){const auto reading=state.reader->snapshot();if(reading.playing||reading.preparing||reading.allocated)return {};}
            auto lease=state.narrator->try_idle_maintenance_lease();if(!lease)return {};
            auto token=std::make_shared<lgaed::windows::NarrationPriorityLease>(std::move(*lease));
            const auto lesson=state.lesson,settings=state.settings;const bool lesson_enabled=lesson&&IsWindowEnabled(lesson),settings_enabled=settings&&IsWindowEnabled(settings);
            if(lesson)EnableWindow(lesson,FALSE);if(settings)EnableWindow(settings,FALSE);
            const auto identity=token.get();
            return std::shared_ptr<void>(identity,[token=std::move(token),lesson,settings,lesson_enabled,settings_enabled](void*)mutable{
                token.reset();if(IsWindow(lesson))EnableWindow(lesson,lesson_enabled);if(IsWindow(settings))EnableWindow(settings,settings_enabled);});
        };
        (void)open_course_audio_window(state.settings?state.settings:window,std::string(route),std::move(audio));
    };
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
        const auto text=settings_reset_text(SettingsScope::global,locale,route);
        if(lgaed::presentation::internal_message_box(state.settings?state.settings:window,wide(text.confirmation).c_str(),wide(text.title).c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
        if(state.reader)state.reader->pause();
        if(state.lesson){state.reader.reset();if(!flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
        lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();
        lgaed::curriculum::reset_catalog_progress(data,scope);store.save(data);show_settings_notice(state.settings?state.settings:window,text.success);
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
            access.eyebrow=(access.scope==SettingsScope::lesson?tr("Lección ","Lesson ","Leçon "):access.scope==SettingsScope::unit?tr("Unidad ","Unit ","Unité "):tr("Curso ","Course ","Cours "))+std::to_string(entry.number);
            if(access.scope==SettingsScope::course)access.eyebrow+=tr(" de "," of "," sur ")+std::to_string(page.entries.size());
            break;}
    }
    if(!context.route.empty()){
        const auto scope=lgaed::windows::catalog_scope(std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",context);
        access.reset_scope=[window,&state,scope,locale,kind=access.scope]{
            const auto text=settings_reset_text(kind,locale);
            if(lgaed::presentation::internal_message_box(state.settings?state.settings:window,wide(text.confirmation).c_str(),wide(text.title).c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
            if(state.reader)state.reader->pause();if(state.lesson){state.reader.reset();if(!flush_lesson_view(state.lesson))return;DestroyWindow(state.lesson);state.lesson=nullptr;}
            lgaed::state::StateStore store(state.environment.paths.root);auto data=store.load();lgaed::curriculum::reset_catalog_progress(data,scope);store.save(data);show_settings_notice(window,text.success);PostMessageW(window,close_settings_message,0,0);};
    }
    if(state.lesson){
        const auto options=lesson_options_snapshot(state.lesson);
        access.current_page=options.current_page;
        for(const auto index:options.available_pages)access.lesson_pages.emplace_back(index,tr("Página ","Page ","Page ")+std::to_string(index+1));
        access.navigate_page=[window](std::size_t page){PostMessageW(window,lesson_option_message,0,static_cast<LPARAM>(page));};
        access.import_state=[window]{PostMessageW(window,lesson_option_message,1,0);};
        access.page_top=[window]{PostMessageW(window,lesson_option_message,2,0);};
        access.page_bottom=[window]{PostMessageW(window,lesson_option_message,3,0);};
        if(options.finish_visible)access.finish=[window]{PostMessageW(window,lesson_option_message,4,0);};
        access.finish_enabled=options.finish_enabled;
    }
    state.settings=create_settings_view(window,std::move(access));
    if(state.reader)state.reader->suspend_preparation(true);
    for(const auto view:{state.catalog,state.lesson,state.tool})if(view)ShowWindow(view,SW_HIDE);
    layout_controls(window,state);ShowWindow(state.settings,SW_SHOW);SetFocus(state.settings);
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
    case WM_COMMAND:
        if(LOWORD(wparam)==IDCANCEL){navigate_back(window,*state);return 0;}
        break;
    case WM_KEYDOWN:
        if(wparam==VK_ESCAPE){navigate_back(window,*state);return 0;}
        break;
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
                state->generation=std::make_unique<lgaed::windows::AudioGeneration>(state->narrator,
                    std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",state->environment.paths.cache/L"kokoro",state->environment.paths.root);
                SetTimer(window,audio_context_timer,400,nullptr);
                state->catalog=lgaed::presentation::create_catalog_view(window,state->environment,
                    std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content",
                    [window,state](const auto& path){state->pending_lesson=path;PostMessageW(window,open_lesson_message,0,0);},
                    [window,state](auto action,const auto& context){state->context=context;state->pending_action=action;
                        PostMessageW(window,action==lgaed::presentation::CatalogAction::settings||action==lgaed::presentation::CatalogAction::audio?settings_message:tool_message,0,0);},
                    [](auto,const auto&){return true;},
                    [backend=state->narrator](const lgaed::curriculum::Block& block,const lgaed::state::Response& response,
                        std::string_view locale)->std::optional<bool>{
                        using namespace winrt::Windows::Data::Json;
                        try{
                            const auto saved=JsonObject::Parse(winrt::to_hstring(response.raw_json));
                            if(!response.correct||!saved.GetNamedBoolean(L"correct",false)||
                                !saved.HasKey(L"responses")||!saved.HasKey(L"contentFingerprint"))return false;
                            const auto fingerprint=saved.GetNamedString(L"contentFingerprint");
                            if(fingerprint.empty())return false;
                            const auto reply=JsonObject::Parse(winrt::to_hstring(backend->validate_math(block.raw_json,
                                winrt::to_string(saved.GetNamedObject(L"responses").Stringify()),locale)));
                            return reply.GetNamedBoolean(L"evaluable",false)&&reply.GetNamedBoolean(L"correct",false)&&
                                fingerprint==reply.GetNamedString(L"contentFingerprint",L"missing");
                        }catch(const winrt::hresult_error& error){OutputDebugStringW(error.message().c_str());return std::nullopt;}
                        catch(const std::exception& error){OutputDebugStringW(widen_error(error).c_str());return std::nullopt;}
                    });
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
        catch(const std::exception& error){show_catalog(window,*state);lgaed::presentation::internal_message_box(window,widen_error(error).c_str(),L"Abrir lección",MB_OK|MB_ICONERROR);}
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
    case application_language_message:{
        try{using namespace lgaed::presentation;const auto data=preferences(*state);const auto locale=lgaed::state::string_value(data,"native_windows","content_language",interface_language(data));
            auto where=catalog_location(state->catalog);const auto prior_locale=where.locale;
            if(state->lesson){const auto current=lesson_page_state(state->lesson);
                if(current.locale==locale||where.locale==locale){refresh_catalog_view(state->catalog);return 0;}
                if(current.lesson_id=="native_cpp_laboratory")return 0;
                where.locale=locale;where.lesson.clear();
                const auto content=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content";
                const auto entries=lgaed::windows::load_catalog(content,where).entries;
                const auto entry=std::ranges::find_if(entries,[&](const auto& candidate){return candidate.id==current.lesson_id;});
                if(entry==entries.end()||!entry->available)throw std::runtime_error("The translated lesson is not available");
                const auto relative=entry->asset.lexically_relative(content);
                const auto asset_locale=relative.empty()?locale:relative.begin()->string();
                auto lesson=lgaed::windows::load_lesson(entry->asset,asset_locale);if(state->reader)state->reader->pause();
                navigate_catalog(state->catalog,where);open_lesson(window,*state,std::move(lesson));
                const auto reopened=lesson_page_state(state->lesson);if(current.index<reopened.count&&current.index!=reopened.index)(void)navigate_lesson_view(state->lesson,current.index);
            }else if(prior_locale!=locale){where.locale=locale;navigate_catalog(state->catalog,where);}else refresh_catalog_view(state->catalog);
            update_audio_context(*state,true);
        }catch(const std::exception& error){lgaed::presentation::show_settings_notice(window,error.what());}return 0;
    }
    case settings_message:
        try{open_settings(window,*state);}catch(const std::exception& error){lgaed::presentation::internal_message_box(window,widen_error(error).c_str(),L"Opciones",MB_OK|MB_ICONERROR);}return 0;
    case lesson_option_message:{
        if(!state->settings||!state->lesson||state->settings_lesson!=state->lesson)return 0;
        if(wparam==0)state->pending_option_page=static_cast<std::size_t>(lparam);
        else if(wparam<=4){const std::array actions{lgaed::presentation::LessonOptionAction::import_android_state,lgaed::presentation::LessonOptionAction::page_top,lgaed::presentation::LessonOptionAction::page_bottom,lgaed::presentation::LessonOptionAction::finish};state->pending_option_action=actions[wparam-1];}
        // Let Settings run its normal draft/discard policy, then execute only
        // after its posted close has returned from the originating callback.
        PostMessageW(state->settings,WM_CLOSE,0,0);return 0;
    }
    case close_settings_message:{if(state->settings){DestroyWindow(state->settings);state->settings=nullptr;}
        if(state->lesson){ShowWindow(state->lesson,SW_SHOW);if(state->reader){state->reader->settings_changed();state->reader->suspend_preparation(false);}}
        else if(state->tool)ShowWindow(state->tool,SW_SHOW);
        else{auto where=lgaed::presentation::catalog_location(state->catalog);const auto data=preferences(*state);where.locale=lgaed::state::string_value(data,"native_windows","content_language",where.locale);lgaed::presentation::navigate_catalog(state->catalog,where);ShowWindow(state->catalog,SW_SHOW);}
        layout_controls(window,*state);
        const auto page=std::exchange(state->pending_option_page,{});const auto action=std::exchange(state->pending_option_action,{});
        const auto lesson=std::exchange(state->settings_lesson,nullptr);
        const auto focus=std::exchange(state->settings_focus,nullptr);
        if(lesson&&lesson==state->lesson){SetFocus(lesson);if(page)(void)lgaed::presentation::navigate_lesson_view(lesson,*page);if(action)(void)lgaed::presentation::invoke_lesson_option(lesson,*action);}
        else SetFocus(state->tool?state->tool:state->catalog);
        if(!page&&!action&&IsWindow(focus)&&IsWindowVisible(focus)&&IsWindowEnabled(focus))SetFocus(focus);
        update_audio_context(*state,true);return 0;}
    case tool_message:
        try{using lgaed::presentation::CatalogAction;
            state->graphics_active=state->pending_action==CatalogAction::graphics||state->pending_action==CatalogAction::smo;
            update_audio_context(*state);
            if(state->tool){DestroyWindow(state->tool);state->tool=nullptr;}
            if(state->pending_action==CatalogAction::compiler){lgaed::curriculum::Lesson lab;lab.id="native_cpp_laboratory";
                const auto data=preferences(*state);const auto ui=lgaed::presentation::interface_language(data);
                lab.title=ui=="fr"?"Prototype du moteur C++":ui=="en"?"C++ Engine Prototype":"Prototipo del motor C++";
                lab.locale=lgaed::state::string_value(data,"native_windows","content_language","es");
                lab.source_path=std::filesystem::path(lgaed::build::source_root)/L"app/src/main/assets/content"/lab.locale/L"programming";
                lgaed::curriculum::Block compiler;compiler.type=lgaed::curriculum::BlockType::cpp_compiler;compiler.stable_id="native_lab_editor";compiler.initial_code="#include <iostream>\n\nusing namespace std;\n\nint main()\n{\n    \n}";
                lgaed::curriculum::Page page;page.stable_id="native_lab";page.blocks.push_back(std::move(compiler));lab.pages.push_back(std::move(page));open_lesson(window,*state,std::move(lab));}
            else if(state->pending_action==CatalogAction::smo){ShowWindow(state->catalog,SW_HIDE);
                state->tool=lgaed::presentation::create_smo_view(window,state->environment,[window]{PostMessageW(window,return_message,0,0);});layout_controls(window,*state);}
            else{ShowWindow(state->catalog,SW_HIDE);state->tool=state->pending_action==CatalogAction::calculator?
                    lgaed::presentation::create_calculator_view(window,state->environment,[window]{PostMessageW(window,return_message,0,0);},state->narrator):
                    lgaed::presentation::create_graphics_view(window,state->environment,[window]{PostMessageW(window,return_message,0,0);});layout_controls(window,*state);}}
        catch(const std::exception& error){if(state->pending_action==lgaed::presentation::CatalogAction::smo){show_catalog(window,*state);update_audio_context(*state);}
            lgaed::presentation::internal_message_box(window,widen_error(error).c_str(),L"Herramientas",MB_OK|MB_ICONERROR);}return 0;
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
        if(wparam==audio_context_timer){try{update_audio_context(*state);}catch(const std::exception& error){OutputDebugStringW(widen_error(error).c_str());}return 0;}
        if (wparam == smoke_timer) {
            KillTimer(window, smoke_timer);
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_CLOSE:
        if(state->reader)state->reader->pause();
        if (state->lesson && !lgaed::presentation::flush_lesson_view(state->lesson)) return 0;
        KillTimer(window,audio_context_timer);
        if(state->narrator)state->narrator->shutdown();
        state->generation.reset();
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
            const auto target=GetAncestor(message.hwnd,GA_ROOT);
            // Native RichEdit fields can request all keys, including the
            // read-only lesson header. Keep the Android-style Back gesture
            // at the root, after any local popup and before dialog dispatch.
            if(target==window&&message.message==WM_KEYDOWN&&message.wParam==VK_ESCAPE&&!escape_has_local_popup(window)){
                if(!(message.lParam&(1LL<<30)))navigate_back(window,state);
                continue;
            }
            const auto internal=lgaed::presentation::active_internal_window(window);
            const bool settings=state.settings&&IsWindowVisible(state.settings);
            const auto dialog=target==window?(internal?internal:settings?state.settings:window):nullptr;
            if(target==window&&!internal&&!settings&&state.reader&&state.reader->dispatch_shortcut(message))continue;
            if(lgaed::presentation::dispatch_lesson_enter(message))continue;
            if(lgaed::windows::CodeEditor::handle_key_message(message))continue;
            if (!dialog||!IsDialogMessageW(dialog,&message)) {
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

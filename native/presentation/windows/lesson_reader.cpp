#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/reader_shortcut_focus.hpp"
#include "core/narration_speed.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/rich_text.hpp"
#include "presentation/windows/programming_technical_ranges.hpp"
#include "platform/windows/narration_player.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/narration_preferences.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"
#include "core/narration_state.hpp"
#include <commctrl.h>
#include <objbase.h>
#include <bcrypt.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <iterator>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace lgaed::presentation {
namespace {
constexpr wchar_t bar_class[]=L"LearnGameAndEngineDev.ReaderBar";
constexpr UINT tick_timer=901;
constexpr UINT layout_message=WM_APP+0x172;
constexpr int button_base=6500;
constexpr int bar_width=344,bar_height=56;
bool promoted_pointer_mouse(){
    // Documented Win32 tablet signature: promoted pen/touch messages retain
    // this origin, while native mouse messages have no tablet signature.
    // https://learn.microsoft.com/windows/win32/tablet/system-events-and-mouse-messages
    return (static_cast<ULONG_PTR>(GetMessageExtraInfo())&0xffffff00u)==0xff515700u;
}
std::string guid(){GUID id{};CoCreateGuid(&id);wchar_t value[40]{};StringFromGUID2(id,value,40);return utf8(value);}
bool setting_bool(const state::StateData& data,const char* key,bool fallback){const auto value=state::find(data,"lesson_narration_settings",key);return value&&std::holds_alternative<bool>(*value)?std::get<bool>(*value):fallback;}
// Stop callbacks can write to the host's pipe. Temporary navigation must not
// run those callbacks or join an in-flight preparation on the UI thread.
class PreparationRetirement {
    std::mutex mutex;std::condition_variable changed;
    std::deque<std::jthread> pending;bool closing=false;
    std::condition_variable& generation_changed;
    std::jthread worker;
    void run(){for(;;){std::jthread task;
        {std::unique_lock lock(mutex);changed.wait(lock,[&]{return closing||!pending.empty();});
            if(pending.empty())return;task=std::move(pending.front());pending.pop_front();}
        task.request_stop();generation_changed.notify_all();task.join();
    }}
public:
    explicit PreparationRetirement(std::condition_variable& wake):generation_changed(wake),worker([this]{run();}){}
    ~PreparationRetirement(){{const std::lock_guard lock(mutex);closing=true;}changed.notify_all();if(worker.joinable())worker.join();}
    void retire(std::jthread& task){{const std::lock_guard lock(mutex);pending.emplace_back(std::move(task));}changed.notify_all();}
};
}
struct LessonReader::Impl {
    struct AudioReadLease{HANDLE file=INVALID_HANDLE_VALUE;~AudioReadLease(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}};
    struct Part{LessonReadingRegion region;windows::NarrationSegment sentence;std::optional<windows::NarrationAudio> audio;std::size_t occurrence=0;std::string audio_fingerprint;std::shared_ptr<AudioReadLease> playback_lease;};
    struct LegacyPlan{std::string key;std::vector<Part> parts;bool editorial_formatting=false,previous_context_policy=false;std::vector<std::string> revised_regions;};
    struct Result{std::uint64_t epoch=0;std::vector<Part> parts;std::vector<LegacyPlan> legacy;std::string key,error,audio_fingerprint;std::size_t index=0;std::optional<windows::NarrationAudio> audio;bool planned=false;
        bool playback_validation=false,recording_changed=false;std::uint64_t validation_serial=0;std::shared_ptr<AudioReadLease> playback_lease;};
    // Android's answered-block priority is playback permission, not listening
    // evidence. It is reconstructed from the same bounded page bookmark.
    struct AnswerPriority{std::size_t block=0,allowed_through=0;};
    struct PriorityBoundary{std::size_t block=0;std::string part;bool after=false;};
    HWND view=nullptr,window=nullptr;WindowsEnvironment environment;std::shared_ptr<windows::NarrationWorker> backend;
    std::function<void()> settings;std::array<HWND,7> buttons{};HWND status=nullptr;HFONT font=nullptr;UINT font_dpi=0;
    std::vector<LessonReadingRegion> regions;std::vector<Part> parts;std::map<std::string,std::unique_ptr<ReadingShade>> shades;
    std::map<std::string,std::size_t> shaded_ends;
    std::string shaded_part;std::int64_t shaded_offset=0;
    std::string page_id,resume_page_id,signature,key,owner,language="es",voice="ef_dora",engine="KOKORO",error,configuration_error,ui_locale="es";
    std::string previous_part,previous_key;std::vector<std::string> previously_heard;double previous_offset=0;std::optional<std::size_t> feedback_target;
    std::vector<std::pair<std::string,double>> previous_positions;
    std::optional<AnswerPriority> answer_priority;
    std::optional<PriorityBoundary> previous_priority;
    narration::Position position;windows::NarrationPlayer player;bool enabled=true,wanted=false,planning=false,has_plan=false,finished=false,waiting_gate=false,visible=true;
    bool manually_paused=true;
    bool restored_bookmark=false,resume_needs_replay=false,explicit_resume_requested=false;
    double rate=1;std::uint64_t epoch=0;std::atomic<std::size_t> priority{0};std::jthread generation;
    std::atomic<bool> advance_generation{true};std::condition_variable generation_changed;std::mutex generation_mutex;
    bool preparation_suspended=false;
    std::unique_ptr<PreparationRetirement> retiring_preparation;
    std::mutex result_mutex;std::vector<Result> results;ULONGLONG checkpoint_at=0;
    std::map<std::string,std::vector<std::pair<double,double>>> listened;
    std::map<std::string,std::string> listened_recordings;
    double last_played_offset=0;
    std::jthread playback_verification;std::atomic<std::uint64_t> playback_validation_serial{0};bool validating_playback=false;
    POINT drag_start{},bar_start{},floating_position{};RECT floating_bounds{};
    std::optional<bool> painted_playing;
    double painted_rate=-1;
    std::string status_text;
    ULONGLONG drag_at=0;bool dragging=false,drag_moved=false,drag_requires_hold=false,drag_rejected=false,positioned=false;HWND pressed_button=nullptr;
    Impl(HWND target,const WindowsEnvironment& env,std::shared_ptr<windows::NarrationWorker> worker,std::function<void()> action):view(target),environment(env),backend(std::move(worker)),settings(std::move(action)){}
    ~Impl(){stop();if(status&&IsWindow(status))DestroyWindow(status);if(window&&IsWindow(window))DestroyWindow(window);shades.clear();if(font)DeleteObject(font);}
    int px(int value)const{return MulDiv(value,static_cast<int>(GetDpiForWindow(view)),96);}
    state::StateData data()const{return read_lesson_state(view);}
    std::string tr(const char* es,const char* en,const char* fr)const{return ui_locale=="fr"?fr:ui_locale=="en"?en:es;}
    void mutate(const std::function<void(state::StateData&)>& update){if(IsWindow(view))update_lesson_preferences(view,update);}
    void configure(){const auto preferences=data();ui_locale=interface_language(preferences);enabled=setting_bool(preferences,"enabled",true);visible=setting_bool(preferences,"controls_visible",true);
        advance_generation.store(setting_bool(preferences,"kokoro_advance_generation",true));generation_changed.notify_all();
        engine=state::string_value(preferences,"lesson_narration_settings","engine","KOKORO");
        const auto page=lesson_page_state(view);
        language=windows::narration_language_tag(page.narration_language).value_or(
            windows::selected_narration_language(preferences,page.locale));
        configuration_error.clear();
        if(engine=="WINDOWS"||engine=="ANDROID")
            voice=state::string_value(preferences,"lesson_narration_settings","windows_voice:"+language);
        else {
            try{voice=windows::selected_kokoro_voice(preferences,language,std::filesystem::path(build::source_root)/L"out/narration/runtime.json");}
            catch(const std::exception& failure){
                voice.clear();
                configuration_error=tr("No se pudo cargar el catálogo de voces de Kokoro.","The Kokoro voice catalogue could not be loaded.","Impossible de charger le catalogue de voix Kokoro.");
                OutputDebugStringW(wide(failure.what()).c_str());
            }
        }
        if(engine=="WINDOWS"||engine=="ANDROID"){
            const auto installed=windows::system_voices();const auto selected=std::ranges::find_if(installed,[&](const auto& item){
                return (voice.empty()||item.id==voice)&&windows::system_voice_matches(item.language,language);});
            if(selected==installed.end())configuration_error=voice.empty()?tr("No hay una voz de Windows instalada para este idioma.","No Windows voice is installed for this language.","Aucune voix Windows n’est installée pour cette langue."):tr("La voz de Windows seleccionada no está disponible para este idioma.","The selected Windows voice is unavailable for this language.","La voix Windows sélectionnée n’est pas disponible pour cette langue.");
            else voice=selected->id;
        }
        const auto stored=state::find(preferences,"lesson_narration_settings","rate");rate=stored&&std::holds_alternative<double>(*stored)?narration::clamp_playback_rate(std::get<double>(*stored)):1;
        if(window){const std::array labels{tr("Retroceder 3 segundos","Back 3 seconds","Reculer de 3 secondes"),tr("Retroceder 5 segundos","Back 5 seconds","Reculer de 5 secondes"),tr("Adelantar 5 segundos","Forward 5 seconds","Avancer de 5 secondes"),tr("Adelantar 3 segundos","Forward 3 seconds","Avancer de 3 secondes"),tr("Reproducir o pausar","Play or pause","Lire ou mettre en pause"),tr("Velocidad de lectura","Playback speed","Vitesse de lecture"),tr("Reiniciar lectura","Restart reading","Recommencer la lecture")};for(std::size_t i=0;i<buttons.size();++i)if(buttons[i])SetWindowTextW(buttons[i],wide(labels[i]).c_str());ShowWindow(window,enabled&&visible?SW_SHOW:SW_HIDE);attach_lesson_reader_bar(view,window,enabled&&visible?bar_height:0);} }
    void save(bool ended=false){if(key.empty()||owner.empty()||!has_plan)return;
        if(player.allocated())position.milliseconds=static_cast<std::int64_t>(std::llround(player.position()));position.finished=ended;
        mutate([&](state::StateData& preferences){auto history=narration::read_history(preferences);history=narration::checkpoint(std::move(history),key,owner,position);narration::write_history(preferences,history);});
        if(ended)owner.clear();}
    void acquire(){if(key.empty()||parts.empty())return;owner=guid();mutate([&](state::StateData& preferences){auto history=narration::begin(narration::read_history(preferences),key,owner,position,{},resume_page_id);narration::write_history(preferences,history);});}
    void prioritize(std::size_t index){if(priority.exchange(index)!=index)generation_changed.notify_all();}
    void cancel_generation(){if(generation.joinable()){generation.request_stop();generation_changed.notify_all();generation.join();}}
    void cancel_playback_verification(){
        ++playback_validation_serial;validating_playback=false;
        if(playback_verification.joinable()){
            // This file-only task installs no IPC cancellation callbacks.
            playback_verification.request_stop();
            if(!retiring_preparation)retiring_preparation=std::make_unique<PreparationRetirement>(generation_changed);
            retiring_preparation->retire(playback_verification);
        }
        {const std::lock_guard lock(result_mutex);std::erase_if(results,[](const auto& result){return result.playback_validation;});}
        if(!player.allocated())for(auto& part:parts)part.playback_lease.reset();
    }
    void release_player(){if(player.allocated()){const auto offset=player.position();if(player.ready())heard_until(offset);
        position.milliseconds=static_cast<std::int64_t>(std::llround(player.release()));}cancel_playback_verification();}
    void stop(){if(IsWindow(view)&&has_plan){try{release_player();save(finished);}catch(...) {}}
        try{(void)player.release();}catch(...){}cancel_playback_verification();
        wanted=false;cancel_generation();retiring_preparation.reset();}
    void pause(){release_player();wanted=false;manually_paused=true;save(finished);paint_progress();update_buttons();}
    void set_controls_visible(bool show){
        if(!enabled||visible==show)return;
        // Match the Android header action. Visibility does not select a new
        // engine, regenerate a plan or provide another playback mechanism.
        if(!show)pause();
        mutate([show](state::StateData& preferences){preferences.preferences["lesson_narration_settings"]["controls_visible"]=show;});
        visible=show;
        ShowWindow(status,show&&!status_text.empty()?SW_SHOWNOACTIVATE:SW_HIDE);
        if(!show){dragging=false;if(GetCapture()==window)ReleaseCapture();}
        if(window){ShowWindow(window,show?SW_SHOWNOACTIVATE:SW_HIDE);attach_lesson_reader_bar(view,window,show?bar_height:0);}
    }
    void update_font(){
        const auto dpi=GetDpiForWindow(view);if(font&&font_dpi==dpi)return;
        // Android's rate label is 16sp bold. Keep its DPI-scaled size when the
        // window moves monitors, just as the existing icons and bar already do.
        const auto next=CreateFontW(-MulDiv(16,static_cast<int>(dpi),96),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        if(!next)return;const auto previous=font;font=next;font_dpi=dpi;
        for(const auto button:buttons)if(button)SendMessageW(button,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if(status)SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if(previous)DeleteObject(previous);
    }
    void initialize(){WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=bar_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
        window=CreateWindowExW(0,bar_class,L"Controles de lectura",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,0,0,view,nullptr,cls.hInstance,this);
        if(!window)throw std::runtime_error("No se pudo abrir la barra de lectura.");
        update_font();
        static constexpr const wchar_t* labels[]{L"Retroceder 3 segundos",L"Retroceder 5 segundos",L"Adelantar 5 segundos",L"Adelantar 3 segundos",L"Reproducir o pausar",L"Velocidad de lectura",L"Reiniciar lectura"};
        for(std::size_t i=0;i<buttons.size();++i){buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(button_base+i)),cls.hInstance,nullptr);SendMessageW(buttons[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);SetWindowSubclass(buttons[i],button_proc,1,reinterpret_cast<DWORD_PTR>(this));}
        status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_CENTER,0,0,0,0,view,nullptr,cls.hInstance,nullptr);SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);reader_theme::apply(status,reader_theme::Surface::card);
        SetTimer(window,tick_timer,30,nullptr);configure();changed();}
    void layout(){update_font();RECT area{};GetClientRect(window,&area);const int size=std::max(px(22),(static_cast<int>(area.right)-px(8))/7),width=size*7,left=std::max(0,(static_cast<int>(area.right)-width)/2);
        for(std::size_t i=0;i<buttons.size();++i)MoveWindow(buttons[i],left+static_cast<int>(i)*size,px(4),size,std::max(1,static_cast<int>(area.bottom)-px(8)),TRUE);
        const auto shape=CreateRoundRectRgn(0,0,area.right+1,area.bottom+1,area.bottom,area.bottom);
        if(shape&&!SetWindowRgn(window,shape,TRUE))DeleteObject(shape);}
    void place(const RECT& available){floating_bounds=available;const auto width=std::min<LONG>(px(bar_width),std::max<LONG>(1,available.right-available.left));
        const auto height=std::min<LONG>(px(bar_height),std::max<LONG>(1,available.bottom-available.top));
        auto point=positioned?floating_position:POINT{available.right-width,available.bottom-height};
        point.x=std::clamp<LONG>(point.x,available.left,std::max(available.left,available.right-width));
        point.y=std::clamp<LONG>(point.y,available.top,std::max(available.top,available.bottom-height));
        if(positioned)floating_position=point;
        RECT before{};GetWindowRect(window,&before);MapWindowPoints(nullptr,view,reinterpret_cast<POINT*>(&before),2);
        if(before.left!=point.x||before.top!=point.y||before.right-before.left!=width||before.bottom-before.top!=height)
            SetWindowPos(window,HWND_TOP,point.x,point.y,width,height,SWP_NOACTIVATE|SWP_NOCOPYBITS);
        else SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);
        MoveWindow(status,point.x,std::max<LONG>(available.top,point.y-px(32)),width,px(28),FALSE);
        ShowWindow(status,visible&&enabled&&!status_text.empty()?SW_SHOWNOACTIVATE:SW_HIDE);}
    void paint(){PAINTSTRUCT update{};const auto dc=BeginPaint(window,&update);RECT area{};GetClientRect(window,&area);const auto palette=reader_theme::palette(window);
        const auto fill=CreateSolidBrush(palette.card);const auto border=GetStockObject(NULL_PEN);
        const auto old_fill=SelectObject(dc,fill),old_pen=SelectObject(dc,border);
        RoundRect(dc,area.left,area.top,area.right,area.bottom,area.bottom,area.bottom);
        SelectObject(dc,old_fill);SelectObject(dc,old_pen);DeleteObject(fill);EndPaint(window,&update);}
    void update_buttons(){if(!window)return;
        if(!painted_playing||*painted_playing!=wanted){InvalidateRect(buttons[4],nullptr,FALSE);painted_playing=wanted;}
        if(painted_rate!=rate){InvalidateRect(buttons[5],nullptr,FALSE);painted_rate=rate;}
        const auto label=!error.empty()?error:planning?tr("Preparando lectura…","Preparing narration…","Préparation de la lecture…"):finished?tr("Lectura finalizada","Reading complete","Lecture terminée"):resume_needs_replay&&!wanted?tr("Reproduce la explicación pendiente.","Play the remaining explanation.","Écoutez l’explication en attente."):waiting_gate?tr("Responde el ejercicio para continuar","Answer the exercise to continue","Répondez à l’exercice pour continuer"):wanted&&!player.ready()?tr("Preparando audio…","Preparing audio…","Préparation de l’audio…"):std::string{};
        if(label!=status_text){status_text=label;SetWindowTextW(status,wide(label).c_str());
            ShowWindow(status,enabled&&visible&&!label.empty()?SW_SHOWNOACTIVATE:SW_HIDE);}}
    void push(Result result){const std::lock_guard lock(result_mutex);results.push_back(std::move(result));}
    static bool audio_file(const windows::NarrationAudio& audio){std::error_code file_status_error;return std::filesystem::is_regular_file(audio.wav,file_status_error);}
    static std::string recording_fingerprint(std::string_view page,const Part& part,const windows::NarrationAudio& audio,std::string_view wav_hash){
        std::string material="lgaed-heard-prefix-v1";
        const auto append=[&](std::string_view value){material+=std::to_string(value.size())+':';material.append(value);};
        append(page);append(part_id(part));append(part.sentence.text);append(audio.key);
        // This runs once in the preparation thread, never in a UI checkpoint.
        // Neither a cache filename nor a bookmark offset proves which PCM ran.
        append(wav_hash);return state::sha256(material);
    }
    static std::string locked_recording_hash(HANDLE file,std::stop_token stop){
        struct Hash{BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE value=nullptr;
            ~Hash(){if(value)BCryptDestroyHash(value);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}} hash;
        if(BCryptOpenAlgorithmProvider(&hash.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0
            ||BCryptCreateHash(hash.algorithm,&hash.value,nullptr,0,nullptr,0,0)<0)
            throw std::runtime_error("No se pudo comprobar la grabación de voz.");
        std::array<std::uint8_t,64*1024> buffer{};
        while(!stop.stop_requested()){
            DWORD count=0;if(!ReadFile(file,buffer.data(),static_cast<DWORD>(buffer.size()),&count,nullptr))
                throw std::runtime_error("No se pudo leer la grabación de voz.");
            if(!count)break;
            if(BCryptHashData(hash.value,buffer.data(),count,0)<0)throw std::runtime_error("No se pudo verificar la grabación de voz.");
        }
        if(stop.stop_requested())return {};
        std::array<std::uint8_t,32> bytes{};
        if(BCryptFinishHash(hash.value,bytes.data(),static_cast<ULONG>(bytes.size()),0)<0)
            throw std::runtime_error("No se pudo completar la comprobación de voz.");
        constexpr char digits[]="0123456789abcdef";std::string result;
        for(const auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}return result;
    }
    void validate_for_playback(std::size_t at){
        if(validating_playback||preparation_suspended||at>=parts.size()||!parts[at].audio)return;
        cancel_playback_verification();validating_playback=true;
        const auto serial=playback_validation_serial.load(),current_epoch=epoch;const auto page_key=key;
        auto candidate=parts[at];candidate.playback_lease.reset();
        playback_verification=std::jthread([this,at,serial,current_epoch,page_key,candidate=std::move(candidate)](std::stop_token stop){
            Result result;result.epoch=current_epoch;result.index=at;result.playback_validation=true;result.validation_serial=serial;
            result.audio_fingerprint=candidate.audio_fingerprint;
            try{
                state::require_safe_path(candidate.audio->wav);
                auto lease=std::make_shared<AudioReadLease>();
                lease->file=CreateFileW(state::native_io_path(candidate.audio->wav).c_str(),GENERIC_READ,FILE_SHARE_READ,
                    nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
                if(lease->file==INVALID_HANDLE_VALUE){const auto code=GetLastError();
                    if(code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND)result.recording_changed=true;
                    else throw std::runtime_error("No se pudo abrir la grabación para comprobarla.");
                }else{
                    state::require_unredirected_handle(lease->file,candidate.audio->wav);
                    const auto hash=locked_recording_hash(lease->file,stop);if(stop.stop_requested())return;
                    result.recording_changed=recording_fingerprint(page_key,candidate,*candidate.audio,hash)!=candidate.audio_fingerprint;
                    if(!result.recording_changed)result.playback_lease=std::move(lease);
                }
            }catch(const std::exception& failure){result.error=failure.what();}
            const std::lock_guard lock(result_mutex);
            if(!stop.stop_requested()&&playback_validation_serial.load()==serial)results.push_back(std::move(result));
        });
    }
    void prepare_audio(std::vector<Part> planned,std::uint64_t current_epoch,const std::string& lang,const std::string& selected_voice,
            const std::string& selected_engine,const std::string& page_key,std::stop_token stop,windows::NarrationPriorityLease page_priority){
        std::vector<bool> ready;ready.reserve(planned.size());for(const auto& part:planned)ready.push_back(part.audio&&audio_file(*part.audio));
        while(!stop.stop_requested()){const auto requested=std::min(priority.load(),planned.size()-1);auto index=requested;
            if(ready[index]){const auto found=std::ranges::find(ready,false);if(found==ready.end())break;
                if(!advance_generation.load()){page_priority={};std::unique_lock lock(generation_mutex);generation_changed.wait(lock,[&]{return stop.stop_requested()||advance_generation.load()||!ready[std::min(priority.load(),planned.size()-1)];});
                    lock.unlock();if(!stop.stop_requested())page_priority=backend->current_page_lease(stop);continue;}
                index=static_cast<std::size_t>(found-ready.begin());}
            auto audio=selected_engine=="WINDOWS"||selected_engine=="ANDROID"?
                windows::synthesize_system_speech(planned[index].sentence,lang,selected_voice,environment.paths.cache,stop):
                backend->prepare(planned[index].sentence,lang,selected_voice,environment.paths.cache/L"kokoro",0,stop);ready[index]=true;
            if(stop.stop_requested())return;
            Result result;result.epoch=current_epoch;result.index=index;
            result.audio_fingerprint=recording_fingerprint(page_key,planned[index],audio,state::sha256(state::read_bytes(audio.wav)));
            result.audio=std::move(audio);push(std::move(result));}
    }
    void start_audio_preparation(){
        if(preparation_suspended||parts.empty())return;
        const auto current_epoch=epoch;auto snapshot=parts;for(auto& part:snapshot)part.playback_lease.reset();
        const auto lang=language,selected_voice=voice,selected_engine=engine,page_key=key;
        generation=std::jthread([this,current_epoch,snapshot,lang,selected_voice,selected_engine,page_key](std::stop_token stop){
            try{prepare_audio(snapshot,current_epoch,lang,selected_voice,selected_engine,page_key,stop,backend->current_page_lease(stop));}
            catch(const std::exception& failure){if(!stop.stop_requested()){Result result;result.epoch=current_epoch;result.error=failure.what();push(std::move(result));}}
        });
    }
    void suspend_preparation(bool suspended){
        if(preparation_suspended==suspended)return;
        preparation_suspended=suspended;
        if(suspended){
            cancel_playback_verification();
            ++epoch;planning=false;
            if(generation.joinable()){
                if(!retiring_preparation)retiring_preparation=std::make_unique<PreparationRetirement>(generation_changed);
                retiring_preparation->retire(generation);
            }
        }else if(enabled){
            if(!has_plan){signature.clear();changed();}
            else if(std::ranges::any_of(parts,[](const auto& part){return !part.audio||!audio_file(*part.audio);}))start_audio_preparation();
        }
        update_buttons();
    }
    void recover_audio(std::size_t at){
        // Settings can delete a course's recordings while the released reader
        // still has their metadata. Reuse its one preparation worker without
        // rebuilding the script, changing its key or moving its bookmark.
        parts[at].audio.reset();
        if(generation.joinable()){
            if(!retiring_preparation)retiring_preparation=std::make_unique<PreparationRetirement>(generation_changed);
            retiring_preparation->retire(generation);
        }
        ++epoch;prioritize(at);start_audio_preparation();update_buttons();
    }
    static std::string part_id(const Part& part){return (part.region.anchor.empty()?part.region.key:part.region.anchor)+'\0'+part.sentence.prepared+'\0'+std::to_string(part.occurrence);}
    static bool same_previous_context_passage(const Part& before,const Part& after){
        // Only the explicit C++ transition is historical here. Exact source
        // ownership authenticates the passage, never its new recording offset.
        return (before.region.reading_context=="auto"||before.region.reading_context=="cpp_previous_parentheses")&&after.region.reading_context=="cpp"
            &&before.region.math_mode=="auto"&&after.region.math_mode=="auto"
            &&(before.region.anchor.empty()?before.region.key:before.region.anchor)
                ==(after.region.anchor.empty()?after.region.key:after.region.anchor)
            &&before.region.source_text==after.region.source_text
            &&before.sentence.start==after.sentence.start&&before.sentence.end==after.sentence.end
            &&before.sentence.prepared!=after.sentence.prepared;
    }
    // This alias is limited to authenticated pre-return reading archives. A
    // complete region must have exactly the same visible text; only inline
    // wrappers actually hidden by the renderer may differ. Partial sentence
    // strings are insufficient because a sentence may begin inside a wrapper.
    static bool same_editorial_passage(const Part& before,const Part& after){
        if(before.region.anchor.empty()||before.region.anchor!=after.region.anchor
            ||before.region.feedback||after.region.feedback||before.region.is_exercise||after.region.is_exercise)return false;
        struct Visible{std::wstring text;std::vector<std::size_t> positions;};
        const auto project=[](std::string_view source){
            const auto text=wide(source);const auto wrappers=technical_prose::inline_code_spans(text).wrappers;
            Visible result;result.positions.reserve(text.size()+1);std::size_t wrapper=0;
            for(std::size_t index=0;index<text.size();++index){
                result.positions.push_back(result.text.size());
                while(wrapper<wrappers.size()&&wrappers[wrapper].second<=index)++wrapper;
                if(wrapper<wrappers.size()&&wrappers[wrapper].first<=index&&index<wrappers[wrapper].second)continue;
                result.text+=text[index];
            }
            result.positions.push_back(result.text.size());return result;
        };
        const auto old=project(before.region.source_text),now=project(after.region.source_text);
        return old.text==now.text&&before.sentence.start<=before.sentence.end&&after.sentence.start<=after.sentence.end
            &&before.sentence.end<old.positions.size()&&after.sentence.end<now.positions.size()
            &&old.positions[before.sentence.start]==now.positions[after.sentence.start]
            &&old.positions[before.sentence.end]==now.positions[after.sentence.end];
    }
    void changed(){if(!IsWindow(view))return;const auto page=lesson_page_state(view);const auto next_page=page.lesson_id+'/'+page.locale+'/'+page.page_id;
        const auto next_regions=lesson_reading_regions(view);std::string next_signature=next_page;
        for(const auto& region:next_regions)next_signature+='\0'+region.key+'\0'+region.source_text
            +'\0'+region.reading_context+'\0'+region.math_mode;
        if(next_signature==signature){regions=next_regions;for(auto& part:parts)for(const auto& region:regions)if(region.key==part.region.key){part.region=region;break;}return;}
        // An automatic partial plan during asynchronous answer restoration has
        // no user-owned position. It must not supersede the stored full-page
        // bookmark when the verified feedback finishes restoring.
        if(has_plan&&(!owner.empty()||restored_bookmark||position.finished)&&static_cast<std::size_t>(position.segment)<parts.size()){previous_part=part_id(parts[static_cast<std::size_t>(position.segment)]);release_player();paint_progress();previous_offset=static_cast<double>(position.milliseconds);save(finished);previous_key=key;
            previous_positions.clear();for(std::size_t i=0;i<=current();++i)previous_positions.emplace_back(part_id(parts[i]),i==current()?previous_offset:parts[i].audio?parts[i].audio->duration_ms:0.);
            previously_heard.clear();for(std::size_t i=0;i<parts.size()&&i<static_cast<std::size_t>(position.heard_through);++i)if(parts[i].region.feedback&&parts[i].region.correct)previously_heard.push_back(part_id(parts[i]));}
        if(has_plan&&answer_priority&&!parts.empty()){
            const auto boundary=std::min(answer_priority->allowed_through,parts.size()-1);
            previous_priority=PriorityBoundary{answer_priority->block,part_id(parts[boundary]),answer_priority->allowed_through>=parts.size()};
        }
        answer_priority.reset();const bool same_page=next_page==page_id;
        if(!same_page){wanted=false;previous_part.clear();previous_key.clear();previously_heard.clear();previous_positions.clear();previous_priority.reset();previous_offset=0;feedback_target.reset();listened.clear();listened_recordings.clear();shades.clear();shaded_ends.clear();shaded_part.clear();shaded_offset=0;}
        cancel_generation();++epoch;signature=next_signature;page_id=next_page;resume_page_id=page.narration_page_identity.empty()?next_page:page.narration_page_identity;regions=next_regions;parts.clear();key.clear();owner.clear();position={};finished=false;waiting_gate=false;has_plan=false;error.clear();planning=false;
        restored_bookmark=false;resume_needs_replay=false;explicit_resume_requested=false;
        std::erase_if(shades,[&](const auto& item){return std::ranges::none_of(regions,[&](const auto& region){return item.first==region.key&&item.second->matches(region.target,region.source_text)
                &&!(feedback_target&&region.feedback&&region.block_index==*feedback_target);});});
        std::erase_if(shaded_ends,[&](const auto& item){return !shades.contains(item.first);});
        for(const auto& region:regions)if(!region.passive_label&&!shades.contains(region.key)){
            auto shade=std::make_unique<ReadingShade>(region.target,region.source_text);shade->read_through(0);
            shades.emplace(region.key,std::move(shade));shaded_ends[region.key]=0;
        }
        configure();if(!enabled){for(auto& [id,shade]:shades)shade->restore();return;}
        if(!configuration_error.empty()){error=configuration_error;update_buttons();return;}
        if(engine!="KOKORO"&&engine!="AUTO"&&engine!="WINDOWS"&&engine!="ANDROID"){error="Motor de narración no reconocido.";update_buttons();return;}
        if(regions.empty()||preparation_suspended)return;planning=true;prioritize(0);const auto current_epoch=epoch;const auto snapshot=regions;const auto lang=language,selected_voice=voice;const auto current_page=resume_page_id;
        const auto selected_engine=engine;
        const auto history=narration::read_history(data());
        auto retired_archives=retired_lesson_reading_archives(view);
        std::erase_if(retired_archives,[&](const auto& archived){return std::ranges::none_of(history.entries,[&](const auto& entry){return entry.page==archived.page_identity;});});
        const bool has_bookmark=std::ranges::any_of(history.entries,[&](const auto& entry){return entry.page==current_page;});
        const auto editorial_archives=has_bookmark?editorial_lesson_reading_archives(view):std::vector<LessonReadingArchive>{};
        const auto legacy_title=page.index>0&&(has_bookmark||!retired_archives.empty())?page.lesson_title:std::string{};
        const auto legacy_snapshot=has_bookmark?legacy_lesson_reading_regions(view):std::vector<LessonReadingRegion>{};
        const auto pre_caption_snapshot=has_bookmark?pre_caption_lesson_reading_regions(view):std::vector<LessonReadingRegion>{};
        generation=std::jthread([this,current_epoch,snapshot,lang,selected_voice,current_page,selected_engine,legacy_title,legacy_snapshot,pre_caption_snapshot,retired_archives,editorial_archives,history](std::stop_token stop){
            try{auto page_priority=backend->current_page_lease(stop);
                std::vector<Part> planned;std::vector<windows::NarrationSegment> sentences;
                for(const auto& region:snapshot){std::map<std::string,std::size_t> occurrences;for(auto sentence:backend->plan(region.source_text,lang,stop,region.reading_context,region.math_mode)){
                    const auto occurrence=occurrences[sentence.prepared]++;sentences.push_back(sentence);planned.push_back({region,std::move(sentence),{},occurrence});} }
                if(planned.empty())throw std::runtime_error("No hay texto narrable en esta página.");const auto resume_key=backend->identity(current_page,lang,selected_voice,sentences,stop,selected_engine=="WINDOWS"||selected_engine=="ANDROID"?"ANDROID":"KOKORO");
                Result plan;plan.epoch=current_epoch;plan.parts=planned;plan.key=resume_key;plan.planned=true;
                if(!history.entries.empty()&&std::ranges::none_of(history.entries,[&](const auto& entry){return entry.key==resume_key;})){
                    const auto register_legacy=[&](std::vector<Part> old,const std::string& old_page,bool editorial_formatting,bool previous_context_policy,const std::vector<std::string>& revised_regions){
                        std::vector<windows::NarrationSegment> script;for(const auto& part:old)script.push_back(part.sentence);
                        const auto old_key=backend->identity(old_page,lang,selected_voice,script,stop,selected_engine=="WINDOWS"||selected_engine=="ANDROID"?"ANDROID":"KOKORO");
                        if(old_key!=resume_key&&std::ranges::none_of(plan.legacy,[&](const auto& legacy){return legacy.key==old_key;})
                            &&std::ranges::any_of(history.entries,[&](const auto& entry){return entry.key==old_key;}))
                            plan.legacy.push_back({old_key,std::move(old),editorial_formatting,previous_context_policy,revised_regions});
                    };
                    std::map<std::pair<std::string,std::string>,std::vector<windows::NarrationSegment>> previous_context_plans;
                    const auto add_legacy=[&](const std::vector<Part>& old,const std::string& old_page,bool editorial_formatting=false,const std::vector<std::string>& revised_regions=std::vector<std::string>{}){
                        register_legacy(old,old_page,editorial_formatting,false,revised_regions);
                        if(std::ranges::none_of(old,[](const auto& part){return part.region.reading_context=="cpp"&&part.region.math_mode=="auto";}))return;
                        // Reconstruct both known previous policies, including
                        // authenticated title/editorial archives. The later
                        // C++ policy already spoke numeric code expressions;
                        // treating it as auto would lose that full script key.
                        // Explicit math modes retain their current meaning.
                        for(const auto context:{"cpp_previous_parentheses","auto"}){
                            std::vector<Part> previous;
                            for(std::size_t i=0;i<old.size();){const auto& part=old[i];
                                if(part.region.reading_context!="cpp"||part.region.math_mode!="auto"){previous.push_back(part);++i;continue;}
                                auto origin=part.region;origin.reading_context=context;
                                auto [cached,inserted]=previous_context_plans.try_emplace(std::make_pair(origin.reading_context,origin.source_text));
                                if(inserted)cached->second=backend->plan(origin.source_text,lang,stop,origin.reading_context,origin.math_mode);
                                std::map<std::string,std::size_t> occurrences;
                                for(auto sentence:cached->second){
                                    const auto occurrence=occurrences[sentence.prepared]++;previous.push_back({origin,std::move(sentence),{},occurrence});}
                                do{++i;}while(i<old.size()&&old[i].region.key==part.region.key&&old[i].region.source_text==part.region.source_text);
                            }
                            register_legacy(std::move(previous),old_page,editorial_formatting,true,revised_regions);
                        }
                    };
                    add_legacy(planned,current_page);
                    std::vector<Part> prefix;
                    if(!legacy_title.empty()){
                        LessonReadingRegion origin;origin.key=current_page+"/legacy-repeated-title";origin.source_text=legacy_title;
                        std::map<std::string,std::size_t> occurrences;for(auto sentence:backend->plan(legacy_title,lang,stop,origin.reading_context,origin.math_mode)){
                            const auto occurrence=occurrences[sentence.prepared]++;prefix.push_back({origin,std::move(sentence),{},occurrence});}
                        auto old=prefix;old.insert(old.end(),planned.begin(),planned.end());add_legacy(std::move(old),current_page);
                    }
                    std::vector<LessonReadingArchive> prior_scripts;
                    prior_scripts.push_back({current_page,pre_caption_snapshot});prior_scripts.push_back({current_page,legacy_snapshot});
                    prior_scripts.insert(prior_scripts.end(),retired_archives.begin(),retired_archives.end());
                    const auto first_editorial=prior_scripts.size();
                    prior_scripts.insert(prior_scripts.end(),editorial_archives.begin(),editorial_archives.end());
                    for(std::size_t prior_index=0;prior_index<prior_scripts.size();++prior_index){const auto& prior=prior_scripts[prior_index];const auto& prior_snapshot=prior.regions;
                    if(prior_snapshot.empty())continue;
                    const bool old_programming=prior.page_identity!=current_page||prior_snapshot.size()!=snapshot.size()||!std::ranges::equal(prior_snapshot,snapshot,[](const auto& a,const auto& b){return a.key==b.key&&a.source_text==b.source_text&&a.reading_context==b.reading_context&&a.math_mode==b.math_mode;});
                    if(old_programming&&!prior_snapshot.empty()){
                        std::vector<Part> old;
                        for(const auto& region:prior_snapshot){
                            const auto current=std::ranges::find_if(snapshot,[&](const auto& item){return item.key==region.key&&item.source_text==region.source_text&&item.reading_context==region.reading_context&&item.math_mode==region.math_mode;});
                            if(current!=snapshot.end()){for(const auto& part:planned)if(part.region.key==region.key)old.push_back(part);}
                            else{std::map<std::string,std::size_t> occurrences;for(auto sentence:backend->plan(region.source_text,lang,stop,region.reading_context,region.math_mode)){
                                const auto occurrence=occurrences[sentence.prepared]++;old.push_back({region,std::move(sentence),{},occurrence});}}
                        }
                        add_legacy(old,prior.page_identity,prior_index>=first_editorial,prior.revised_regions);if(!prefix.empty()){auto with_title=prefix;with_title.insert(with_title.end(),old.begin(),old.end());add_legacy(std::move(with_title),prior.page_identity,prior_index>=first_editorial,prior.revised_regions);}
                    }
                    }
                }
                push(std::move(plan));
                prepare_audio(std::move(planned),current_epoch,lang,selected_voice,selected_engine,resume_key,stop,std::move(page_priority));
            }catch(const std::exception& failure){if(!stop.stop_requested()){Result result;result.epoch=current_epoch;result.error=failure.what();push(std::move(result));}}
        });update_buttons();}
    void receive(){std::vector<Result> pending;{const std::lock_guard lock(result_mutex);pending.swap(results);}if(pending.empty())return;for(auto& result:pending){if(result.epoch!=epoch)continue;
        if(result.playback_validation){
            if(result.validation_serial!=playback_validation_serial.load()||result.index!=current())continue;
            validating_playback=false;
            if(playback_verification.joinable()){
                if(!retiring_preparation)retiring_preparation=std::make_unique<PreparationRetirement>(generation_changed);
                retiring_preparation->retire(playback_verification);
            }
            if(result.index>=parts.size()||parts[result.index].audio_fingerprint!=result.audio_fingerprint)continue;
            if(!result.error.empty()){error=result.error;wanted=false;cancel_playback_verification();update_buttons();continue;}
            if(result.recording_changed){recover_audio(result.index);continue;}
            parts[result.index].playback_lease=std::move(result.playback_lease);
            continue;
        }
        if(!result.error.empty()){error=result.error;planning=false;wanted=false;
            try{(void)player.release();}catch(...){}cancel_playback_verification();continue;}
        if(result.planned){parts=std::move(result.parts);key=result.key;has_plan=true;planning=false;position={};
            restored_bookmark=false;resume_needs_replay=false;
            const auto history=narration::read_history(data());if(const auto saved=narration::restore(history,key,parts.size())){
                position=*saved;restored_bookmark=!position.finished;}
            else for(const auto& legacy:result.legacy)if(const auto legacy_saved=narration::restore(history,legacy.key,legacy.parts.size())){
                position=*legacy_saved;restored_bookmark=!position.finished;
                const auto old_at=static_cast<std::size_t>(legacy_saved->segment);bool found_position=false,preserve_recording=false;
                if(!legacy.parts[old_at].region.retired_successor.empty()){
                    const auto& successor=legacy.parts[old_at].region.retired_successor;
                    const auto active=std::ranges::find_if(parts,[&](const auto& part){return part.region.anchor.starts_with(successor);});
                    if(active!=parts.end()){position.segment=active-parts.begin();position.milliseconds=0;position.finished=false;found_position=true;}
                }
                for(std::size_t previous=found_position?0:old_at+1;previous>0;--previous){const auto& old_part=legacy.parts[previous-1];
                    auto found=std::ranges::find_if(parts,[&](const auto& part){return part_id(part)==part_id(old_part);});
                    if(found==parts.end()&&legacy.previous_context_policy)
                        found=std::ranges::find_if(parts,[&](const auto& part){return same_previous_context_passage(old_part,part);});
                    if(found==parts.end()&&legacy.editorial_formatting)
                        found=std::ranges::find_if(parts,[&](const auto& part){return same_editorial_passage(old_part,part);});
                    if(found==parts.end())continue;
                    const auto mapped=static_cast<std::size_t>(found-parts.begin());found_position=true;
                    if(part_id(*found)!=part_id(old_part))position.heard_prefix.reset();
                    if(previous-1==old_at){position.segment=static_cast<std::int64_t>(mapped);preserve_recording=part_id(*found)==part_id(old_part);}
                    else{position.segment=static_cast<std::int64_t>(std::min(mapped+1,parts.size()-1));position.milliseconds=0;position.finished=false;}
                    break;
                }
                if(!found_position){position.segment=0;position.milliseconds=0;position.finished=false;}
                if(legacy.previous_context_policy&&!preserve_recording){
                    position.milliseconds=0;position.finished=false;position.heard_prefix.reset();
                }
                if(position.finished&&static_cast<std::size_t>(position.segment)+1<parts.size()&&
                    std::ranges::all_of(parts.begin()+position.segment+1,parts.end(),[](const auto& part){return part.region.passive_label;})){
                    // A previously completed page stays completed when only a
                    // missing spoken UI heading was appended. Keep the stored
                    // terminal position valid on a second restore; do not
                    // invent a recording offset for the newly added label.
                    position.segment=static_cast<std::int64_t>(parts.size()-1);position.milliseconds=0;
                }
                position.heard_through=0;
                for(std::size_t i=0;i<parts.size();++i)if(parts[i].region.feedback&&parts[i].region.correct){
                    const auto old=std::ranges::find_if(legacy.parts,[&](const auto& part){return part_id(part)==part_id(parts[i]);});
                    if(old==legacy.parts.end()||old-legacy.parts.begin()>=legacy_saved->heard_through)break;
                    position.heard_through=static_cast<std::int64_t>(i+1);
                }
                // A changed mathematical card is not formatting-equivalent.
                // Revisit its new speech only when the old pause reached or
                // passed that exact region. Earlier compatible offsets remain.
                std::optional<std::size_t> revised_boundary;
                for(const auto& region:legacy.revised_regions){
                    const auto before=std::ranges::find_if(legacy.parts,[&](const auto& part){return part.region.key==region;});
                    if(before==legacy.parts.end()||static_cast<std::size_t>(before-legacy.parts.begin())>old_at)continue;
                    const auto after=std::ranges::find_if(parts,[&](const auto& part){return part.region.key==region;});
                    if(after==parts.end())continue;
                    const auto boundary=static_cast<std::size_t>(after-parts.begin());
                    if(!revised_boundary||boundary<*revised_boundary)revised_boundary=boundary;
                }
                if(revised_boundary){
                    position.segment=static_cast<std::int64_t>(*revised_boundary);position.milliseconds=0;position.finished=false;
                    position.heard_through=std::min(position.heard_through,position.segment);position.heard_prefix.reset();
                    restored_bookmark=true;
                }
                if(legacy.previous_context_policy){
                    // A compatible later pause, finished or not, cannot skip
                    // changed successful feedback earlier in the page. Keep
                    // compatible pauses before it, but revisit its new audio
                    // without inheriting the old recording's listening credit.
                    const auto restored_at=static_cast<std::size_t>(position.segment);
                    for(std::size_t i=0;i<=restored_at&&i<parts.size();++i)if(parts[i].region.feedback&&parts[i].region.correct
                        &&std::ranges::any_of(legacy.parts,[&](const auto& old){return same_previous_context_passage(old,parts[i]);})){
                        position.segment=static_cast<std::int64_t>(i);position.milliseconds=0;position.finished=false;
                        position.heard_through=std::min(position.heard_through,position.segment);position.heard_prefix.reset();break;
                    }
                }
                if(legacy.previous_context_policy)restored_bookmark=!position.finished;
                // Relabel the verified old entry in place. Its recency and the
                // other two page slots are unchanged; no parallel history is kept.
                mutate([&](state::StateData& preferences){auto old=narration::read_history(preferences);
                    for(auto& entry:old.entries)if(entry.key==legacy.key){entry.key=key;entry.position=position;entry.page=resume_page_id;}
                    if(old.active_key==legacy.key)old.active_key=key;narration::write_history(preferences,old);});break;
            }
            if(!previous_part.empty()){
                // An edited answer removes its old feedback from the visible
                // script. Restore the nearest surviving real segment rather
                // than falling through to position zero and replaying a page.
                for(auto candidate=previous_positions.rbegin();candidate!=previous_positions.rend();++candidate){
                    const auto found=std::ranges::find_if(parts,[&](const auto& part){return part_id(part)==candidate->first;});
                    if(found==parts.end())continue;
                    position.segment=found-parts.begin();position.milliseconds=static_cast<std::int64_t>(candidate->second);position.finished=false;
                    if(candidate->first!=previous_part&&!feedback_target)wanted=false;
                    break;
                }
            }
            if(!previous_key.empty()&&previous_key!=key){
                position.heard_through=0;for(std::size_t i=0;i<parts.size();++i)if(parts[i].region.feedback&&parts[i].region.correct){if(std::ranges::find(previously_heard,part_id(parts[i]))==previously_heard.end())break;position.heard_through=static_cast<std::int64_t>(i+1);}
                mutate([&](state::StateData& preferences){auto old=narration::read_history(preferences);const auto existing=std::ranges::find(old.entries,key,&narration::Bookmark::key);
                    if(existing!=old.entries.end())std::erase_if(old.entries,[&](const auto& entry){return entry.key==previous_key;});
                    else for(auto& entry:old.entries)if(entry.key==previous_key){entry.key=key;entry.position=position;}
                    if(old.active_key==previous_key)old.active_key=key;narration::write_history(preferences,old);});
            }
            if(previous_priority){if(const auto first=correct_feedback_start(previous_priority->block)){
                auto boundary=*first;
                if(const auto found=std::ranges::find_if(parts,[&](const auto& part){return part_id(part)==previous_priority->part;});found!=parts.end())
                    boundary=static_cast<std::size_t>(found-parts.begin())+(previous_priority->after?1:0);
                answer_priority=AnswerPriority{previous_priority->block,std::max(*first,boundary)};
            }}
            if(!feedback_target&&!answer_priority)restore_answered_priority();
            bool fresh_feedback_selected=false;
            if(feedback_target){const auto found=std::ranges::find_if(parts,[&](const auto& part){return part.region.feedback&&part.region.block_index==*feedback_target;});
                if(found!=parts.end()){
                    fresh_feedback_selected=true;
                    position.segment=found-parts.begin();position.milliseconds=0;position.finished=false;
                    position.heard_prefix.reset();restored_bookmark=false;resume_needs_replay=false;
                    // Reuse the recording, never listening credit from an older
                    // submission that happened to generate the same verdict.
                    position.heard_through=std::min(position.heard_through,position.segment);
                    for(const auto& part:parts)if(part.region.feedback&&part.region.block_index==*feedback_target){
                        listened.erase(part_id(part));listened_recordings.erase(part_id(part));}
                    if(found->region.correct&&found->region.solved)answer_priority=AnswerPriority{*feedback_target,static_cast<std::size_t>(position.segment)};
                    else if(answer_priority&&answer_priority->block==*feedback_target)answer_priority.reset();
                }feedback_target.reset();}
            if(!fresh_feedback_selected)constrain_required_compiler_bookmark();
            previous_part.clear();previous_key.clear();previously_heard.clear();previous_positions.clear();previous_priority.reset();finished=position.finished;prioritize(static_cast<std::size_t>(position.segment));
            // A fresh answer invalidates previous hearing even when the reader
            // stays paused and the learner closes before pressing Play.
            if(fresh_feedback_selected){acquire();save(false);}
            else if(wanted&&!finished)acquire();
        }else if(result.audio&&result.index<parts.size()){
            auto& part=parts[result.index];const bool replaced=!part.audio_fingerprint.empty()
                &&part.audio_fingerprint!=result.audio_fingerprint;
            if(replaced&&result.index==current()&&player.allocated())release_player();
            if(replaced){listened.erase(part_id(part));listened_recordings.erase(part_id(part));}
            part.audio=std::move(result.audio);part.audio_fingerprint=std::move(result.audio_fingerprint);
            if(result.index==current())restore_current_prefix(replaced);
        }
        }paint_progress();update_buttons();}
    std::size_t current()const{return parts.empty()?0:std::min(static_cast<std::size_t>(std::max<std::int64_t>(0,position.segment)),parts.size()-1);}
    static std::int64_t duration_microseconds(const Part& part){
        if(!part.audio||!std::isfinite(part.audio->duration_ms)||part.audio->duration_ms<=0
            ||part.audio->duration_ms>=static_cast<double>(std::numeric_limits<std::int64_t>::max())/1000.)return 0;
        return static_cast<std::int64_t>(std::floor(part.audio->duration_ms*1000.));
    }
    std::optional<narration::HeardPrefix> empty_certificate(std::size_t at)const{
        if(at>=parts.size()||parts[at].audio_fingerprint.empty())return {};
        const auto duration=duration_microseconds(parts[at]);if(duration<=0)return {};
        return narration::HeardPrefix{parts[at].audio_fingerprint,0,duration};
    }
    void merge_heard(const Part& part,double start,double end){
        if(!std::isfinite(start)||!std::isfinite(end)||start<0||end<=start)return;
        if(part.audio_fingerprint.empty())return;const auto id=part_id(part);
        auto& recording=listened_recordings[id];if(recording!=part.audio_fingerprint){listened.erase(id);recording=part.audio_fingerprint;}
        auto& ranges=listened[id];ranges.push_back({start,end});std::ranges::sort(ranges);
        std::vector<std::pair<double,double>> merged;
        for(const auto& range:ranges){if(!merged.empty()&&range.first<=merged.back().second+1)
            merged.back().second=std::max(merged.back().second,range.second);else merged.push_back(range);}
        ranges=std::move(merged);
    }
    void restore_current_prefix(bool replaced=false){
        if(parts.empty())return;const auto& part=parts[current()];const auto certificate=empty_certificate(current());
        if(!certificate)return;
        if(narration::heard_prefix_matches(position,certificate->fingerprint,certificate->duration_microseconds)){
            if(position.heard_prefix->microseconds>0)
                merge_heard(part,0,position.heard_prefix->microseconds/1000.);
            resume_needs_replay=false;
        }else if((restored_bookmark||replaced)&&part.region.feedback&&part.region.correct&&part.region.solved){
            // Keep the paused offset untouched. Only explicit Play can recover
            // an old/mismatched certificate by replaying missing explanation.
            resume_needs_replay=true;
        }else position.heard_prefix=certificate;
    }
    void bind_preview_certificate(){
        const auto certificate=empty_certificate(current());if(!certificate){position.heard_prefix.reset();return;}
        if(!narration::heard_prefix_matches(position,certificate->fingerprint,certificate->duration_microseconds))
            position.heard_prefix=certificate;
    }
    void recover_explicit_resume(){
        if(!explicit_resume_requested||parts.empty())return;
        if(!parts[current()].audio||!parts[current()].playback_lease)return;
        explicit_resume_requested=false;
        if(!resume_needs_replay)return;
        const auto& part=parts[current()];const auto first=correct_feedback_start(part.region.block_index);
        if(first){const auto missing=std::max(*first,static_cast<std::size_t>(std::max<std::int64_t>(0,position.heard_through)));
            if(missing<=current()&&missing<parts.size()&&parts[missing].region.block_index==part.region.block_index
                &&parts[missing].region.feedback&&parts[missing].region.correct){
                if(missing!=current())parts[current()].playback_lease.reset();
                position.segment=static_cast<std::int64_t>(missing);position.milliseconds=0;position.finished=false;
                position.heard_prefix.reset();bind_preview_certificate();prioritize(missing);save(false);
            }
        }
        restored_bookmark=false;resume_needs_replay=false;
    }
    void constrain_required_compiler_bookmark(){
        const auto at=current();
        for(std::size_t first=0;first<parts.size();){
            auto last=first;
            while(last+1<parts.size()&&parts[last+1].region.block_index==parts[first].region.block_index)++last;
            const bool pending=std::ranges::any_of(parts.begin()+first,parts.begin()+last+1,[](const auto& part){
                return part.region.required_compiler&&!part.region.solved;
            });
            if(pending&&(at>last||(position.finished&&at>=first))){
                // A new prerequisite can leave the exact same spoken script
                // and recording key. Its old finished/late bookmark is still
                // historical data, never permission to skip the pending run.
                // Adjust only this in-memory position; opening the lesson must
                // not rewrite the student's three saved page slots.
                position.segment=static_cast<std::int64_t>(first);position.milliseconds=0;position.finished=false;
                position.heard_through=std::min(position.heard_through,position.segment);
                position.heard_prefix.reset();restored_bookmark=false;resume_needs_replay=false;
                answer_priority.reset();wanted=false;waiting_gate=false;return;
            }
            first=last+1;
        }
    }
    std::optional<std::size_t> correct_feedback_start(std::size_t block)const{
        for(std::size_t i=0;i<parts.size();++i)if(parts[i].region.block_index==block&&parts[i].region.feedback&&parts[i].region.correct&&parts[i].region.solved)return i;
        return {};
    }
    bool passes_prefix(std::size_t destination,bool manual_forward=false)const{
        const auto active=answer_priority?correct_feedback_start(answer_priority->block):std::optional<std::size_t>{};
        const bool within_feedback=active&&destination<parts.size()&&parts[destination].region.feedback
            &&parts[destination].region.block_index==answer_priority->block;
        for(std::size_t i=0;i<destination&&i<parts.size();++i){const auto& part=parts[i];if(!part.region.is_exercise)continue;
            // A fresh answer may read its complete feedback. It cannot grant
            // permission to continue past a different pending compiler.
            if(active&&i<*active&&(!part.region.required_compiler||within_feedback))continue;
            const bool last=i+1>=parts.size()||parts[i+1].region.block_index!=part.region.block_index;
            if(!last)continue;if(!part.region.solved)return false;
            const auto permitted=active?static_cast<std::int64_t>(answer_priority->allowed_through):position.heard_through;
            if(part.region.feedback&&part.region.correct&&permitted<=static_cast<std::int64_t>(i)&&!(active&&manual_forward))return false;
        }return true;
    }
    bool can_enter(std::size_t destination,bool manual_forward=false)const{
        return destination<=current()||passes_prefix(destination,manual_forward);
    }
    void restore_answered_priority(){
        const auto at=current();if(passes_prefix(at))return;
        std::optional<AnswerPriority> restored;
        for(std::size_t i=0;i<=at&&i<parts.size();++i){const auto& part=parts[i];
            if(part.region.feedback&&part.region.correct&&part.region.solved)restored=AnswerPriority{part.region.block_index,at};
            const bool last=i+1>=parts.size()||parts[i+1].region.block_index!=part.region.block_index;
            if(restored&&last&&part.region.is_exercise&&!part.region.solved&&i<at)restored.reset();
        }
        answer_priority=restored;
    }
    void play_current(){if(!wanted||!has_plan||finished||parts.empty()||player.allocated()||preparation_suspended)return;
        auto at=current();prioritize(at);if(!parts[at].audio)return;
        if(!parts[at].playback_lease){validate_for_playback(at);return;}
        recover_explicit_resume();at=current();prioritize(at);if(!parts[at].audio)return;
        if(!parts[at].playback_lease){validate_for_playback(at);return;}
        last_played_offset=static_cast<double>(position.milliseconds);player.play(parts[at].audio->wav,last_played_offset,rate);update_buttons();}
    void heard_until(double end){if(parts.empty()||!std::isfinite(end)||end<last_played_offset)return;
        const auto& part=parts[current()];if(!part.audio)return;
        end=std::clamp(end,0.,part.audio->duration_ms);merge_heard(part,last_played_offset,end);last_played_offset=end;
        if(auto certificate=empty_certificate(current())){
            const auto known=listened.find(part_id(part));
            const auto recording=listened_recordings.find(part_id(part));
            if(known!=listened.end()&&recording!=listened_recordings.end()&&recording->second==part.audio_fingerprint
                &&!known->second.empty()&&known->second.front().first<=1)
                certificate->microseconds=std::min(certificate->duration_microseconds,
                    static_cast<std::int64_t>(std::floor(known->second.front().second*1000.)));
            position.heard_prefix=std::move(certificate);
        }
        restored_bookmark=false;
    }
    bool feedback_fully_heard(std::size_t at)const{const auto id=part_id(parts[at]);const auto found=listened.find(id);
        const auto recording=listened_recordings.find(id);
        return found!=listened.end()&&!found->second.empty()&&recording!=listened_recordings.end()&&recording->second==parts[at].audio_fingerprint
        &&parts[at].audio&&found->second.front().first<=1&&found->second.front().second+1>=parts[at].audio->duration_ms;}
    void credit_heard_feedback(){
        // This persisted boundary means every preceding correct-feedback part
        // has really played. A later heard part cannot cover an earlier seek.
        auto through=static_cast<std::size_t>(std::max<std::int64_t>(0,position.heard_through));
        while(through<parts.size()){
            if(parts[through].region.feedback&&parts[through].region.correct&&!feedback_fully_heard(through))break;
            ++through;
        }
        position.heard_through=static_cast<std::int64_t>(through);
    }
    void tick(){receive();if(!has_plan||!enabled)return;play_current();player.tick();if(player.ready()){
            const auto actual=player.position();heard_until(actual);position.milliseconds=static_cast<std::int64_t>(std::llround(actual));paint_progress();
            if(GetTickCount64()-checkpoint_at>1200){save();checkpoint_at=GetTickCount64();}}
        if(wanted&&player.ended()){
            const auto at=current();heard_until(parts[at].audio->duration_ms);position.milliseconds=static_cast<std::int64_t>(std::llround(parts[at].audio->duration_ms));(void)player.release();parts[at].playback_lease.reset();
            credit_heard_feedback();
            if(answer_priority&&parts[at].region.feedback&&parts[at].region.correct)
                answer_priority->allowed_through=std::max(answer_priority->allowed_through,at+1);
            const bool wrong=parts[at].region.feedback&&!parts[at].region.correct;
            const bool end_of_wrong=wrong&&(at+1==parts.size()||!parts[at+1].region.feedback||parts[at+1].region.block_index!=parts[at].region.block_index);
            const bool pending_feedback=parts[at].region.feedback&&parts[at].region.correct&&position.heard_through<=static_cast<std::int64_t>(at);
            const bool unanswered=parts[at].region.is_exercise&&!parts[at].region.solved;
            if(end_of_wrong){wanted=false;waiting_gate=false;save();}
            else if(at+1==parts.size()&&(!pending_feedback||(answer_priority&&answer_priority->allowed_through>at))&&!unanswered
                &&passes_prefix(parts.size())){finished=true;wanted=false;save(true);}
            else if(at+1==parts.size()||(!wrong&&!can_enter(at+1))){wanted=false;waiting_gate=true;save();}
            else{position.segment=static_cast<std::int64_t>(at+1);position.milliseconds=0;position.heard_prefix.reset();
                bind_preview_certificate();save();play_current();}
            paint_progress();update_buttons();}}
    void paint_progress(){if(!enabled||!has_plan)return;std::map<std::string,std::size_t> ends;for(const auto& region:regions)ends[region.key]=0;
        if(has_plan&&!parts.empty()){const auto at=current();for(std::size_t i=0;i<at;++i)ends[parts[i].region.key]=std::max(ends[parts[i].region.key],parts[i].sentence.end);
            const auto& part=parts[at];auto end=part.sentence.start;
            if(part.audio){const auto offset=static_cast<double>(position.milliseconds);
                for(const auto& timing:part.audio->timings)if(timing.start_ms<=offset)end=std::max(end,part.sentence.start+timing.end);
                if(finished||offset+1>=part.audio->duration_ms)end=part.sentence.end;}
            else if(position.milliseconds>0&&shaded_part==part_id(part)&&shaded_offset==position.milliseconds)
                if(const auto old=shaded_ends.find(part.region.key);old!=shaded_ends.end())
                end=std::max(end,std::min(old->second,part.sentence.end));
            ends[part.region.key]=std::max(ends[part.region.key],end);
            shaded_part=part_id(part);shaded_offset=position.milliseconds;
        }
        for(auto& [id,shade]:shades){shade->read_through(ends[id]);shaded_ends[id]=ends[id];}}
    void toggle(){if(!enabled)return;if(wanted){pause();return;}if(finished){update_buttons();return;}
        if(!error.empty()){signature.clear();changed();if(!error.empty())return;}wanted=true;manually_paused=false;waiting_gate=false;
        explicit_resume_requested=true;if(has_plan){acquire();play_current();}update_buttons();}
    void seek(int seconds){if(!has_plan||parts.empty())return;const bool resume=wanted;release_player();
        auto at=current();double offset=static_cast<double>(position.milliseconds)+seconds*1000.;
        auto last=parts.size()-1;
        const bool guide=parts[at].region.feedback&&!parts[at].region.correct;
        if(guide){
            const auto block=parts[at].region.block_index;last=at;
            while(last+1<parts.size()&&parts[last+1].region.feedback&&parts[last+1].region.block_index==block)++last;
        }
        while(offset<0&&at>0){--at;if(!parts[at].audio){offset=0;break;}offset+=parts[at].audio->duration_ms;}
        while(at<parts.size()&&parts[at].audio&&offset>=parts[at].audio->duration_ms){if(at==last){offset=parts[at].audio->duration_ms;break;}
            if(!guide&&!can_enter(at+1,true)){offset=std::min(offset,parts[at].audio->duration_ms);break;}offset-=parts[at].audio->duration_ms;++at;}
        if(answer_priority&&seconds>0)answer_priority->allowed_through=std::max(answer_priority->allowed_through,at);
        position=narration::seek(position,static_cast<std::int64_t>(at),static_cast<std::int64_t>(std::max(0.,offset)));finished=false;prioritize(at);wanted=resume;waiting_gate=false;
        // Explicit preview cancels legacy recovery. A zero certificate records
        // known identity, never a prefix inferred from the new visual offset.
        restored_bookmark=false;resume_needs_replay=false;explicit_resume_requested=false;bind_preview_certificate();
        if(owner.empty())acquire();save();paint_progress();play_current();update_buttons();}
    void restart(){if(!has_plan)return;release_player();listened.clear();listened_recordings.clear();answer_priority.reset();previous_priority.reset();position={};
        restored_bookmark=false;resume_needs_replay=false;explicit_resume_requested=false;bind_preview_certificate();
        finished=false;wanted=true;manually_paused=false;waiting_gate=false;acquire();save();paint_progress();play_current();update_buttons();}
    void feedback(std::size_t block,bool correct){
        const bool resume=!manually_paused&&!finished;
        // An incorrect guide interrupts the original permission; it does not
        // revoke a different, previously answered block's valid priority.
        if(!correct){if(answer_priority&&answer_priority->block==block)answer_priority.reset();
            if(previous_priority&&previous_priority->block==block)previous_priority.reset();}
        feedback_target=block;wanted=resume;finished=false;signature.clear();changed();}
    void settings_changed(){const auto old_language=language,old_voice=voice,old_engine=engine;const auto old_enabled=enabled;configure();
        if(!enabled){pause();cancel_generation();for(auto& [id,shade]:shades)shade->restore();}
        else if(language!=old_language||voice!=old_voice||engine!=old_engine||!old_enabled){pause();cancel_generation();has_plan=false;parts.clear();previous_key.clear();previous_part.clear();previously_heard.clear();answer_priority.reset();previous_priority.reset();listened.clear();listened_recordings.clear();signature.clear();changed();}
        else if(player.allocated())player.set_rate(rate);update_buttons();}
    void change_playback_rate(double value){
        value=narration::clamp_playback_rate(value);if(rate==value)return;
        rate=value;mutate([&](state::StateData& preferences){preferences.preferences["lesson_narration_settings"]["rate"]=rate;});
        if(player.allocated())player.set_rate(rate);update_buttons();
    }
    void step_playback_rate(bool faster){
        const auto& values=narration::playback_rates;
        if(faster){const auto next=std::upper_bound(values.begin(),values.end(),rate);change_playback_rate(next==values.end()?values.front():*next);}
        else{const auto next=std::lower_bound(values.begin(),values.end(),rate);change_playback_rate(next==values.begin()?values.back():*std::prev(next));}
    }
    bool dispatch_shortcut(const MSG& message){
        const bool speed_key=message.wParam==VK_F9||message.wParam==VK_F10;
        const bool arrow_key=message.wParam==VK_LEFT||message.wParam==VK_UP||message.wParam==VK_DOWN||message.wParam==VK_RIGHT;
        const bool down=message.message==WM_KEYDOWN||message.message==WM_SYSKEYDOWN;
        const bool up=message.message==WM_KEYUP||message.message==WM_SYSKEYUP;
        if(speed_key){if(!down&&!up)return false;}
        // With no focused child, USER sends function keys to the active root
        // as WM_SYSKEYDOWN even without Alt. The modifier/context guards below
        // still reserve actual Alt combinations for their normal system action.
        else if(!down)return false;
        std::size_t index=0;switch(message.wParam){
        case VK_F1:index=4;break;case VK_F2:index=6;break;
        case VK_F5:case VK_LEFT:index=0;break;case VK_F6:case VK_UP:index=1;break;
        case VK_F7:case VK_DOWN:index=2;break;case VK_F8:case VK_RIGHT:index=3;break;
        case VK_F9:case VK_F10:index=5;break;
        default:return false;
        }
        for(const auto modifier:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN})
            if(GetKeyState(modifier)&0x8000)return false;
        if((message.message==WM_SYSKEYDOWN||message.message==WM_SYSKEYUP)&&(message.lParam&(1LL<<29)))return false;
        const auto root=GetAncestor(view,GA_ROOT);
        if(!enabled||!visible||!IsWindowVisible(view)||!IsWindowVisible(window)||
            !IsWindowEnabled(root)||!IsWindowEnabled(view)||!IsWindowEnabled(window)||
            !message.hwnd||(message.hwnd!=root&&message.hwnd!=view&&!IsChild(view,message.hwnd)))return false;
        // Lesson prose is also RichEdit, but it is not an answer/code editor.
        // Only unselected live narration targets may yield their arrow keys;
        // native inputs, source viewers and interactive controls retain them.
        if(arrow_key){
            const auto reading_text=[&](HWND target){return std::ranges::any_of(regions,
                [target](const auto& region){return region.target==target;});};
            if(reader_shortcuts::owns_arrow_keys(GetFocus(),view,window,message,reading_text)||
                reader_shortcuts::owns_arrow_keys(message.hwnd,view,window,message,reading_text))return false;
        }
        if(speed_key){
            // F10's unmodified system-key release must not reach USER's menu
            // activation. Clicks and repeated keys share the same speed cycle.
            if(down&&IsWindowEnabled(buttons[5]))step_playback_rate(message.wParam==VK_F10);
            return true;
        }
        // Bit 30 distinguishes autorepeat from a new press. Consume repeats so
        // F1 cannot alternate play/pause or fall through to native Help handling.
        if(!(message.lParam&(1LL<<30))&&IsWindowEnabled(buttons[index]))
            SendMessageW(buttons[index],BM_CLICK,0,0);
        return true;
    }
    static LRESULT CALLBACK button_proc(HWND button,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR context){
        auto& self=*reinterpret_cast<Impl*>(context);
        if(message==BM_CLICK){
            // The floating bar owns mouse capture, so the BUTTON never receives
            // the native pressed state used by BM_CLICK's simulated DOWN/UP.
            // Deliver its one normal activation without re-entering that stream.
            if(IsWindowEnabled(button)&&IsWindowEnabled(self.window))
                SendMessageW(self.window,WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(button),BN_CLICKED),reinterpret_cast<LPARAM>(button));
            return 0;
        }
        if(message==WM_LBUTTONDOWN){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};MapWindowPoints(button,self.window,&point,1);
            SendMessageW(self.window,message,wp,MAKELPARAM(point.x,point.y));self.pressed_button=button;return 0;}
        if(message==WM_NCDESTROY)RemoveWindowSubclass(button,button_proc,1);
        return DefSubclassProc(button,message,wp,lp);
    }
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window=window;}
        if(!self)return DefWindowProcW(window,message,wp,lp);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        try{switch(message){
        case WM_SIZE:case WM_DPICHANGED_AFTERPARENT:self->layout();return 0;
        case layout_message:if(lp)self->place(*reinterpret_cast<const RECT*>(lp));return 0;
        case WM_TIMER:if(wp==tick_timer)self->tick();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->paint();return 0;
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED){const auto id=LOWORD(wp)-button_base;switch(id){case 0:self->seek(-3);break;case 1:self->seek(-5);break;case 2:self->seek(5);break;case 3:self->seek(3);break;case 4:self->toggle();break;
            case 5:self->step_playback_rate(true);break;
            case 6:self->restart();break;default:break;}return 0;}break;
        case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);const auto index=static_cast<std::size_t>(item.CtlID-button_base);if(index>=self->buttons.size())return FALSE;
            auto bounds=item.rcItem;const auto palette=reader_theme::palette(window);FillRect(item.hDC,&bounds,reader_theme::brush(reader_theme::Surface::card));InflateRect(&bounds,-self->px(11),-self->px(11));
            static constexpr Icon icons[]{Icon::rewind3,Icon::rewind5,Icon::forward5,Icon::forward3,Icon::speaker,Icon::play,Icon::reset};
            if(index==5){const auto previous=SelectObject(item.hDC,self->font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,reader_theme::accent_ink(window,palette.card));wchar_t caption[32]{};std::swprintf(caption,std::size(caption),L"%gX",self->rate);auto caption_bounds=item.rcItem;DrawTextW(item.hDC,caption,-1,&caption_bounds,DT_SINGLELINE|DT_CENTER|DT_VCENTER);SelectObject(item.hDC,previous);}
            else draw_icon(item.hDC,bounds,icons[index],index==4?(self->wanted?palette.accent:RGB(255,255,255)):index==6?RGB(170,174,183):RGB(255,255,255));
            if(item.itemState&ODS_FOCUS)reader_theme::paint_keyboard_focus(item.hwndItem,item.hDC,item.rcItem,palette.muted);return TRUE;}
        case WM_LBUTTONDOWN:{self->drag_start={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&self->drag_start);RECT rect{};GetWindowRect(window,&rect);self->bar_start={rect.left,rect.top};ScreenToClient(self->view,&self->bar_start);self->drag_at=GetTickCount64();self->dragging=true;self->drag_moved=false;self->drag_rejected=false;self->drag_requires_hold=promoted_pointer_mouse();self->pressed_button=nullptr;SetCapture(window);return 0;}
        case WM_MOUSEMOVE:if(self->dragging&&(wp&MK_LBUTTON)){
            if(self->drag_rejected)return 0;
            if(promoted_pointer_mouse()!=self->drag_requires_hold){self->drag_rejected=true;return 0;}
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);
            if(!self->drag_moved&&std::max(std::abs(point.x-self->drag_start.x),std::abs(point.y-self->drag_start.y))<self->px(6))return 0;
            if(self->drag_requires_hold&&GetTickCount64()-self->drag_at<90){self->drag_rejected=true;return 0;}
            self->drag_moved=true;RECT bar{};GetWindowRect(window,&bar);const auto x=self->bar_start.x+point.x-self->drag_start.x,y=self->bar_start.y+point.y-self->drag_start.y;
            const auto& area=self->floating_bounds;
            self->floating_position={std::clamp<LONG>(x,area.left,std::max<LONG>(area.left,area.right-(bar.right-bar.left))),std::clamp<LONG>(y,area.top,std::max<LONG>(area.top,area.bottom-(bar.bottom-bar.top)))};self->positioned=true;
            SetWindowPos(window,HWND_TOP,self->floating_position.x,self->floating_position.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOCOPYBITS);}return 0;
        case WM_LBUTTONUP:{const auto button=self->pressed_button;const bool click=self->dragging&&!self->drag_moved&&!self->drag_rejected&&button;self->dragging=false;self->pressed_button=nullptr;if(GetCapture()==window)ReleaseCapture();
            if(click){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);RECT bounds{};GetWindowRect(button,&bounds);if(PtInRect(&bounds,point))SendMessageW(button,BM_CLICK,0,0);}return 0;}
        case WM_CANCELMODE:case WM_CAPTURECHANGED:self->dragging=false;self->pressed_button=nullptr;if(GetCapture()==window)ReleaseCapture();return 0;
        case WM_NCDESTROY:KillTimer(window,tick_timer);self->window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        default:break;}}
        catch(const std::exception& failure){self->error=failure.what();self->wanted=false;try{(void)self->player.release();}catch(...){}
            self->cancel_playback_verification();self->update_buttons();}
        return DefWindowProcW(window,message,wp,lp);
    }
};
void layout_lesson_reader_bar(HWND bar,const RECT& available){if(IsWindow(bar))SendMessageW(bar,layout_message,0,reinterpret_cast<LPARAM>(&available));}
LessonReader::LessonReader(HWND view,const WindowsEnvironment& env,std::shared_ptr<windows::NarrationWorker> worker,std::function<void()> settings):impl_(std::make_unique<Impl>(view,env,std::move(worker),std::move(settings))){impl_->initialize();}
LessonReader::~LessonReader()=default;
void LessonReader::content_changed(){impl_->changed();}
void LessonReader::feedback(std::size_t block,bool correct){impl_->feedback(block,correct);}
void LessonReader::toggle(){impl_->toggle();}
void LessonReader::pause(){impl_->pause();}
void LessonReader::set_controls_visible(bool show){impl_->set_controls_visible(show);}
void LessonReader::toggle_controls(){impl_->set_controls_visible(!impl_->visible);}
bool LessonReader::controls_visible()const{return impl_->enabled&&impl_->visible;}
void LessonReader::settings_changed(){impl_->settings_changed();}
void LessonReader::suspend_preparation(bool suspended){impl_->suspend_preparation(suspended);}
void LessonReader::seek_seconds(int seconds){impl_->seek(seconds);}
void LessonReader::restart(){impl_->restart();}
bool LessonReader::dispatch_shortcut(const MSG& message){return impl_->dispatch_shortcut(message);}
HWND LessonReader::bar()const{return impl_->window;}
LessonReaderSnapshot LessonReader::snapshot()const{
    const auto& self=*impl_;LessonReaderSnapshot value;
    value.playing=self.wanted&&self.player.ready();value.allocated=self.player.allocated();value.planned=self.has_plan;
    value.preparing=self.planning||self.validating_playback;value.finished=self.finished;value.waiting_gate=self.waiting_gate;value.index=self.current();
    value.preparation_suspended=self.preparation_suspended;
    value.offset=self.player.allocated()?static_cast<std::int64_t>(std::llround(self.player.position())):self.position.milliseconds;
    value.rate=self.rate;value.playback_rate=self.player.ready()?self.player.playback_rate():0;
    value.heard_through=self.position.heard_through;value.error=self.error;value.key=self.key;
    for(const auto& part:self.parts){LessonReaderSegmentSnapshot segment;
        segment.region=part.region.key;segment.source=part.sentence.text;segment.spoken=part.sentence.spoken;segment.target=part.region.target;
        segment.block=part.region.block_index;segment.start=part.sentence.start;segment.end=part.sentence.end;
        segment.feedback=part.region.feedback;segment.correct=part.region.correct;segment.available=part.audio.has_value();
        if(part.audio){segment.duration_ms=part.audio->duration_ms;segment.timings=part.audio->timings;segment.recording=part.audio->wav;}
        value.segments.push_back(std::move(segment));
    }
    return value;
}
}

#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include "presentation/windows/reading_shade.hpp"
#include "presentation/windows/reader_theme.hpp"
#include "presentation/windows/ui_language.hpp"
#include "presentation/windows/icons.hpp"
#include "presentation/windows/rich_text.hpp"
#include "platform/windows/narration_player.hpp"
#include "platform/windows/system_speech.hpp"
#include "platform/windows/state_store.hpp"
#include "core/narration_state.hpp"
#include <commctrl.h>
#include <objbase.h>
#include <windowsx.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
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
std::string guid(){GUID id{};CoCreateGuid(&id);wchar_t value[40]{};StringFromGUID2(id,value,40);return utf8(value);}
bool setting_bool(const state::StateData& data,const char* key,bool fallback){const auto value=state::find(data,"lesson_narration_settings",key);return value&&std::holds_alternative<bool>(*value)?std::get<bool>(*value):fallback;}
}
struct LessonReader::Impl {
    struct Part{LessonReadingRegion region;windows::NarrationSegment sentence;std::optional<windows::NarrationAudio> audio;std::size_t occurrence=0;};
    struct Result{std::uint64_t epoch=0;std::vector<Part> parts;std::string key,error,legacy_key;std::size_t index=0,omitted_prefix=0;std::optional<windows::NarrationAudio> audio;bool planned=false;};
    HWND view=nullptr,window=nullptr;WindowsEnvironment environment;std::shared_ptr<windows::NarrationWorker> backend;
    std::function<void()> settings;std::array<HWND,7> buttons{};HWND status=nullptr;HFONT font=nullptr;
    std::vector<LessonReadingRegion> regions;std::vector<Part> parts;std::map<std::string,std::unique_ptr<ReadingShade>> shades;
    std::string page_id,resume_page_id,signature,key,owner,language="es",voice="ef_dora",engine="KOKORO",error,configuration_error,ui_locale="es";
    std::string previous_part,previous_key;std::vector<std::string> previously_heard;double previous_offset=0;std::optional<std::size_t> feedback_target;
    narration::Position position;windows::NarrationPlayer player;bool enabled=true,wanted=false,planning=false,has_plan=false,finished=false,waiting_gate=false,visible=true;
    double rate=1;std::uint64_t epoch=0;std::atomic<std::size_t> priority{0};std::jthread generation;
    std::atomic<bool> advance_generation{true};std::condition_variable generation_changed;std::mutex generation_mutex;
    std::mutex result_mutex;std::vector<Result> results;ULONGLONG checkpoint_at=0;
    std::map<std::string,std::vector<std::pair<double,double>>> listened;
    double last_played_offset=0;
    POINT drag_start{},bar_start{},floating_position{};RECT floating_bounds{};
    std::optional<bool> painted_playing;
    double painted_rate=-1;
    std::string status_text;
    ULONGLONG drag_at=0;bool dragging=false,positioned=false;
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
        language=page.narration_language.empty()?state::string_value(preferences,"lesson_narration_settings","language",page.locale):page.narration_language;
        if(language!="es"&&language!="fr"&&language!="en-US"&&language!="en-GB")language=language=="en"?"en-US":"es";
        const auto fallback=language=="fr"?"ff_siwis":language=="en-US"?"af_heart":language=="en-GB"?"bf_emma":"ef_dora";
        voice=engine=="WINDOWS"||engine=="ANDROID"?state::string_value(preferences,"lesson_narration_settings","windows_voice:"+language):
            state::string_value(preferences,"lesson_narration_settings","kokoro_voice:"+language,fallback);
        configuration_error.clear();
        if(engine=="WINDOWS"||engine=="ANDROID"){
            const auto installed=windows::system_voices();const auto selected=std::ranges::find_if(installed,[&](const auto& item){
                return (voice.empty()||item.id==voice)&&windows::system_voice_matches(item.language,language);});
            if(selected==installed.end())configuration_error=voice.empty()?tr("No hay una voz de Windows instalada para este idioma.","No Windows voice is installed for this language.","Aucune voix Windows n’est installée pour cette langue."):tr("La voz de Windows seleccionada no está disponible para este idioma.","The selected Windows voice is unavailable for this language.","La voix Windows sélectionnée n’est pas disponible pour cette langue.");
            else voice=selected->id;
        }
        const auto stored=state::find(preferences,"lesson_narration_settings","rate");rate=stored&&std::holds_alternative<double>(*stored)?std::clamp(std::get<double>(*stored),.5,2.):1;
        if(window){const std::array labels{tr("Retroceder 3 segundos","Back 3 seconds","Reculer de 3 secondes"),tr("Retroceder 5 segundos","Back 5 seconds","Reculer de 5 secondes"),tr("Adelantar 5 segundos","Forward 5 seconds","Avancer de 5 secondes"),tr("Adelantar 3 segundos","Forward 3 seconds","Avancer de 3 secondes"),tr("Reproducir o pausar","Play or pause","Lire ou mettre en pause"),tr("Velocidad de lectura","Playback speed","Vitesse de lecture"),tr("Reiniciar lectura","Restart reading","Recommencer la lecture")};for(std::size_t i=0;i<buttons.size();++i)if(buttons[i])SetWindowTextW(buttons[i],wide(labels[i]).c_str());ShowWindow(window,enabled&&visible?SW_SHOW:SW_HIDE);attach_lesson_reader_bar(view,window,enabled&&visible?bar_height:0);} }
    void save(bool ended=false){if(key.empty()||owner.empty()||!has_plan)return;
        if(player.allocated())position.milliseconds=static_cast<std::int64_t>(std::llround(player.position()));position.finished=ended;
        mutate([&](state::StateData& preferences){auto history=narration::read_history(preferences);history=narration::checkpoint(std::move(history),key,owner,position);narration::write_history(preferences,history);});
        if(ended)owner.clear();}
    void acquire(){if(key.empty()||parts.empty())return;owner=guid();mutate([&](state::StateData& preferences){auto history=narration::begin(narration::read_history(preferences),key,owner,position,{},resume_page_id);narration::write_history(preferences,history);});}
    void prioritize(std::size_t index){if(priority.exchange(index)!=index)generation_changed.notify_all();}
    void cancel_generation(){if(generation.joinable()){generation.request_stop();generation_changed.notify_all();generation.join();}}
    void release_player(){if(player.allocated()){const auto offset=player.position();if(player.ready())heard_until(offset);
        position.milliseconds=static_cast<std::int64_t>(std::llround(player.release()));}}
    void stop(){if(IsWindow(view)&&has_plan){try{release_player();save(finished);}catch(...) {}}(void)player.release();wanted=false;cancel_generation();}
    void pause(){release_player();wanted=false;save(finished);paint_progress();update_buttons();}
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
    void initialize(){WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=bar_class;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassExW(&cls);
        window=CreateWindowExW(0,bar_class,L"Controles de lectura",WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,0,0,view,nullptr,cls.hInstance,this);
        if(!window)throw std::runtime_error("No se pudo abrir la barra de lectura.");
        font=CreateFontW(-px(16),0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Roboto");
        static constexpr const wchar_t* labels[]{L"Retroceder 3 segundos",L"Retroceder 5 segundos",L"Adelantar 5 segundos",L"Adelantar 3 segundos",L"Reproducir o pausar",L"Velocidad de lectura",L"Reiniciar lectura"};
        for(std::size_t i=0;i<buttons.size();++i){buttons[i]=CreateWindowExW(0,L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(button_base+i)),cls.hInstance,nullptr);SendMessageW(buttons[i],WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);}
        status=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_CENTER,0,0,0,0,view,nullptr,cls.hInstance,nullptr);SendMessageW(status,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);reader_theme::apply(status,reader_theme::Surface::card);
        SetTimer(window,tick_timer,30,nullptr);configure();changed();}
    void layout(){RECT area{};GetClientRect(window,&area);const int size=std::max(px(22),(static_cast<int>(area.right)-px(8))/7),width=size*7,left=std::max(0,(static_cast<int>(area.right)-width)/2);
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
        const auto label=!error.empty()?error:planning?tr("Preparando lectura…","Preparing narration…","Préparation de la lecture…"):finished?tr("Lectura finalizada","Reading complete","Lecture terminée"):waiting_gate?tr("Responde el ejercicio para continuar","Answer the exercise to continue","Répondez à l’exercice pour continuer"):wanted&&!player.ready()?tr("Preparando audio…","Preparing audio…","Préparation de l’audio…"):std::string{};
        if(label!=status_text){status_text=label;SetWindowTextW(status,wide(label).c_str());
            ShowWindow(status,enabled&&visible&&!label.empty()?SW_SHOWNOACTIVATE:SW_HIDE);}}
    void push(Result result){const std::lock_guard lock(result_mutex);results.push_back(std::move(result));}
    static std::string part_id(const Part& part){return part.region.key+'\0'+part.sentence.prepared+'\0'+std::to_string(part.occurrence);}
    void changed(){if(!IsWindow(view))return;const auto page=lesson_page_state(view);const auto next_page=page.lesson_id+'/'+page.locale+'/'+page.page_id;
        const auto next_regions=lesson_reading_regions(view);std::string next_signature=next_page;
        for(const auto& region:next_regions)next_signature+='\0'+region.key+'\0'+region.source_text;
        if(next_signature==signature){regions=next_regions;for(auto& part:parts)for(const auto& region:regions)if(region.key==part.region.key){part.region=region;break;}return;}
        if(has_plan&&static_cast<std::size_t>(position.segment)<parts.size()){previous_part=part_id(parts[static_cast<std::size_t>(position.segment)]);release_player();previous_offset=static_cast<double>(position.milliseconds);save(finished);previous_key=key;
            previously_heard.clear();for(std::size_t i=0;i<parts.size()&&i<static_cast<std::size_t>(position.heard_through);++i)if(parts[i].region.feedback&&parts[i].region.correct)previously_heard.push_back(part_id(parts[i]));}
        if(next_page!=page_id){wanted=false;previous_part.clear();previous_key.clear();previously_heard.clear();previous_offset=0;feedback_target.reset();listened.clear();}
        cancel_generation();++epoch;signature=next_signature;page_id=next_page;resume_page_id=page.narration_page_identity.empty()?next_page:page.narration_page_identity;regions=next_regions;parts.clear();key.clear();owner.clear();position={};finished=false;waiting_gate=false;has_plan=false;error.clear();planning=false;
        std::erase_if(shades,[&](const auto& item){return std::ranges::none_of(regions,[&](const auto& region){return item.first==region.key&&item.second->matches(region.target,region.source_text);});});
        for(const auto& region:regions)if(!shades.contains(region.key))shades.emplace(region.key,std::make_unique<ReadingShade>(region.target,region.source_text));
        configure();if(!enabled){for(auto& [id,shade]:shades)shade->restore();return;}
        for(auto& [id,shade]:shades)shade->read_through(0);
        if(!configuration_error.empty()){error=configuration_error;update_buttons();return;}
        if(engine!="KOKORO"&&engine!="AUTO"&&engine!="WINDOWS"&&engine!="ANDROID"){error="Motor de narración no reconocido.";update_buttons();return;}
        if(regions.empty())return;planning=true;prioritize(0);const auto current_epoch=epoch;const auto snapshot=regions;const auto lang=language,selected_voice=voice;const auto current_page=resume_page_id;
        const auto selected_engine=engine;
        const auto history=narration::read_history(data());
        const auto legacy_title=page.index>0&&std::ranges::any_of(history.entries,[&](const auto& entry){return entry.page==current_page;})?page.lesson_title:std::string{};
        generation=std::jthread([this,current_epoch,snapshot,lang,selected_voice,current_page,selected_engine,legacy_title](std::stop_token stop){
            try{std::vector<Part> planned;std::vector<windows::NarrationSegment> sentences;
                for(const auto& region:snapshot){std::map<std::string,std::size_t> occurrences;for(auto sentence:backend->plan(region.source_text,lang,stop)){
                    const auto occurrence=occurrences[sentence.prepared]++;sentences.push_back(sentence);planned.push_back({region,std::move(sentence),{},occurrence});} }
                if(planned.empty())throw std::runtime_error("No hay texto narrable en esta página.");const auto resume_key=backend->identity(current_page,lang,selected_voice,sentences,stop,selected_engine=="WINDOWS"||selected_engine=="ANDROID"?"ANDROID":"KOKORO");
                Result plan;plan.epoch=current_epoch;plan.parts=planned;plan.key=resume_key;plan.planned=true;
                // Older Windows builds incorrectly repeated the lesson title on
                // every page. Only an exact old script-key match permits the
                // removal of that prefix from an existing bounded bookmark.
                if(!legacy_title.empty()){
                    auto legacy=backend->plan(legacy_title,lang,stop);plan.omitted_prefix=legacy.size();legacy.insert(legacy.end(),sentences.begin(),sentences.end());
                    plan.legacy_key=backend->identity(current_page,lang,selected_voice,legacy,stop,selected_engine=="WINDOWS"||selected_engine=="ANDROID"?"ANDROID":"KOKORO");
                }
                push(std::move(plan));
                std::vector<bool> ready(planned.size(),false);const auto cores=std::max(1u,std::thread::hardware_concurrency());
                while(!stop.stop_requested()){const auto requested=std::min(priority.load(),planned.size()-1);auto index=requested;
                    if(ready[index]){const auto found=std::ranges::find(ready,false);if(found==ready.end())break;
                        if(!advance_generation.load()){std::unique_lock lock(generation_mutex);generation_changed.wait(lock,[&]{return stop.stop_requested()||advance_generation.load()||!ready[std::min(priority.load(),planned.size()-1)];});continue;}
                        index=static_cast<std::size_t>(found-ready.begin());}
                    auto audio=selected_engine=="WINDOWS"||selected_engine=="ANDROID"?
                        windows::synthesize_system_speech(planned[index].sentence,lang,selected_voice,environment.paths.cache,stop):
                        backend->prepare(planned[index].sentence,lang,selected_voice,environment.paths.cache/L"kokoro",std::max(1u,cores>2?cores-1:cores),stop);ready[index]=true;
                    Result result;result.epoch=current_epoch;result.index=index;result.audio=std::move(audio);push(std::move(result));}
            }catch(const std::exception& failure){if(!stop.stop_requested()){Result result;result.epoch=current_epoch;result.error=failure.what();push(std::move(result));}}
        });update_buttons();}
    void receive(){std::vector<Result> pending;{const std::lock_guard lock(result_mutex);pending.swap(results);}if(pending.empty())return;for(auto& result:pending){if(result.epoch!=epoch)continue;
        if(!result.error.empty()){error=result.error;planning=false;wanted=false;(void)player.release();continue;}
        if(result.planned){parts=std::move(result.parts);key=result.key;has_plan=true;planning=false;position={};
            const auto history=narration::read_history(data());if(const auto saved=narration::restore(history,key,parts.size()))position=*saved;
            else if(!result.legacy_key.empty())if(const auto legacy_saved=narration::restore(history,result.legacy_key,parts.size()+result.omitted_prefix)){
                position=*legacy_saved;const auto removed=static_cast<std::int64_t>(result.omitted_prefix);
                if(position.segment<removed){position.milliseconds=0;position.finished=false;}
                position.segment=std::max<std::int64_t>(0,position.segment-removed);position.heard_through=std::max<std::int64_t>(0,position.heard_through-removed);
                mutate([&](state::StateData& preferences){auto old=narration::read_history(preferences);
                    for(auto& entry:old.entries)if(entry.key==result.legacy_key){entry.key=key;entry.position=position;}
                    if(old.active_key==result.legacy_key)old.active_key=key;narration::write_history(preferences,old);});
            }
            if(!previous_part.empty()){const auto found=std::ranges::find_if(parts,[&](const auto& part){return part_id(part)==previous_part;});if(found!=parts.end()){position.segment=found-parts.begin();position.milliseconds=static_cast<std::int64_t>(previous_offset);position.finished=false;}}
            if(!previous_key.empty()&&previous_key!=key){
                position.heard_through=0;for(std::size_t i=0;i<parts.size();++i)if(parts[i].region.feedback&&parts[i].region.correct){if(std::ranges::find(previously_heard,part_id(parts[i]))==previously_heard.end())break;position.heard_through=static_cast<std::int64_t>(i+1);}
                mutate([&](state::StateData& preferences){auto old=narration::read_history(preferences);const auto existing=std::ranges::find(old.entries,key,&narration::Bookmark::key);
                    if(existing!=old.entries.end())std::erase_if(old.entries,[&](const auto& entry){return entry.key==previous_key;});
                    else for(auto& entry:old.entries)if(entry.key==previous_key){entry.key=key;entry.position=position;}
                    if(old.active_key==previous_key)old.active_key=key;narration::write_history(preferences,old);});
            }
            if(feedback_target){const auto found=std::ranges::find_if(parts,[&](const auto& part){return part.region.feedback&&part.region.block_index==*feedback_target;});if(found!=parts.end()){position.segment=found-parts.begin();position.milliseconds=0;position.finished=false;}feedback_target.reset();}
            previous_part.clear();previous_key.clear();previously_heard.clear();finished=position.finished;prioritize(static_cast<std::size_t>(position.segment));if(wanted&&!finished)acquire();
        }else if(result.audio&&result.index<parts.size())parts[result.index].audio=std::move(result.audio);
        }paint_progress();update_buttons();}
    std::size_t current()const{return parts.empty()?0:std::min(static_cast<std::size_t>(std::max<std::int64_t>(0,position.segment)),parts.size()-1);}
    bool can_enter(std::size_t destination)const{
        if(destination<=current())return true;
        for(std::size_t i=0;i<destination&&i<parts.size();++i){const auto& part=parts[i];if(!part.region.is_exercise)continue;
            const bool last=i+1>=parts.size()||parts[i+1].region.block_index!=part.region.block_index;
            if(!last)continue;if(!part.region.solved)return false;
            if(part.region.feedback&&part.region.correct&&position.heard_through<=static_cast<std::int64_t>(i))return false;
        }return true;
    }
    void play_current(){if(!wanted||!has_plan||finished||parts.empty()||player.allocated())return;const auto at=current();prioritize(at);if(!parts[at].audio)return;
        last_played_offset=static_cast<double>(position.milliseconds);player.play(parts[at].audio->wav,last_played_offset,rate);update_buttons();}
    void heard_until(double end){if(parts.empty()||end<last_played_offset)return;auto& ranges=listened[part_id(parts[current()])];ranges.push_back({last_played_offset,end});std::ranges::sort(ranges);
        std::vector<std::pair<double,double>> merged;for(const auto& range:ranges){if(!merged.empty()&&range.first<=merged.back().second+1)merged.back().second=std::max(merged.back().second,range.second);else merged.push_back(range);}ranges=std::move(merged);last_played_offset=end;}
    bool feedback_fully_heard(std::size_t at)const{const auto found=listened.find(part_id(parts[at]));return found!=listened.end()&&!found->second.empty()
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
            const auto at=current();heard_until(parts[at].audio->duration_ms);position.milliseconds=static_cast<std::int64_t>(std::llround(parts[at].audio->duration_ms));(void)player.release();
            credit_heard_feedback();
            const bool wrong=parts[at].region.feedback&&!parts[at].region.correct;
            const bool end_of_wrong=wrong&&(at+1==parts.size()||!parts[at+1].region.feedback||parts[at+1].region.block_index!=parts[at].region.block_index);
            const bool pending_feedback=parts[at].region.feedback&&parts[at].region.correct&&position.heard_through<=static_cast<std::int64_t>(at);
            const bool unanswered=parts[at].region.is_exercise&&!parts[at].region.solved;
            if(end_of_wrong){wanted=false;waiting_gate=false;save();}
            else if(at+1==parts.size()&&!pending_feedback&&!unanswered){finished=true;wanted=false;save(true);}
            else if(at+1==parts.size()||!can_enter(at+1)){wanted=false;waiting_gate=true;save();}
            else{position.segment=static_cast<std::int64_t>(at+1);position.milliseconds=0;save();play_current();}
            paint_progress();update_buttons();}}
    void paint_progress(){if(!enabled)return;std::map<std::string,std::size_t> ends;for(const auto& region:regions)ends[region.key]=0;
        if(has_plan&&!parts.empty()){const auto at=current();for(std::size_t i=0;i<at;++i)ends[parts[i].region.key]=std::max(ends[parts[i].region.key],parts[i].sentence.end);
            const auto& part=parts[at];auto end=part.sentence.start;
            if(part.audio){const auto offset=static_cast<double>(position.milliseconds);
                for(const auto& timing:part.audio->timings)if(timing.start_ms<=offset)end=std::max(end,part.sentence.start+timing.end);
                if(finished||offset+1>=part.audio->duration_ms)end=part.sentence.end;}
            ends[part.region.key]=std::max(ends[part.region.key],end);}
        for(auto& [id,shade]:shades)shade->read_through(ends[id]);}
    void toggle(){if(!enabled)return;if(wanted){pause();return;}if(finished){update_buttons();return;}
        if(!error.empty()){signature.clear();changed();if(!error.empty())return;}wanted=true;waiting_gate=false;if(has_plan){acquire();play_current();}update_buttons();}
    void seek(int seconds){if(!has_plan||parts.empty())return;const bool resume=wanted;release_player();
        auto at=current();double offset=static_cast<double>(position.milliseconds)+seconds*1000.;
        while(offset<0&&at>0){--at;if(!parts[at].audio){offset=0;break;}offset+=parts[at].audio->duration_ms;}
        while(at<parts.size()&&parts[at].audio&&offset>=parts[at].audio->duration_ms){if(at+1==parts.size()){offset=parts[at].audio->duration_ms;break;}
            if(!can_enter(at+1)){offset=std::min(offset,parts[at].audio->duration_ms);break;}offset-=parts[at].audio->duration_ms;++at;}
        position=narration::seek(position,static_cast<std::int64_t>(at),static_cast<std::int64_t>(std::max(0.,offset)));finished=false;prioritize(at);wanted=resume;waiting_gate=false;
        if(owner.empty())acquire();save();paint_progress();play_current();update_buttons();}
    void restart(){if(!has_plan)return;(void)player.release();listened.clear();position={};finished=false;wanted=true;waiting_gate=false;acquire();save();paint_progress();play_current();update_buttons();}
    void feedback(std::size_t block,bool){feedback_target=block;wanted=true;finished=false;signature.clear();changed();}
    void settings_changed(){const auto old_language=language,old_voice=voice,old_engine=engine;const auto old_enabled=enabled;configure();
        if(!enabled){pause();cancel_generation();for(auto& [id,shade]:shades)shade->restore();}
        else if(language!=old_language||voice!=old_voice||engine!=old_engine||!old_enabled){pause();cancel_generation();has_plan=false;parts.clear();previous_key.clear();previous_part.clear();previously_heard.clear();listened.clear();signature.clear();changed();}
        else if(player.allocated())player.set_rate(rate);update_buttons();}
    static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp){auto* self=reinterpret_cast<Impl*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));self->window=window;}
        if(!self)return DefWindowProcW(window,message,wp,lp);LRESULT themed{};if(reader_theme::control_color(message,wp,lp,themed))return themed;
        try{switch(message){
        case WM_SIZE:self->layout();return 0;
        case layout_message:if(lp)self->place(*reinterpret_cast<const RECT*>(lp));return 0;
        case WM_TIMER:if(wp==tick_timer)self->tick();return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:self->paint();return 0;
        case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED){const auto id=LOWORD(wp)-button_base;switch(id){case 0:self->seek(-3);break;case 1:self->seek(-5);break;case 2:self->seek(5);break;case 3:self->seek(3);break;case 4:self->toggle();break;
            case 5:{static constexpr double values[]{.5,1,1.5,2};auto it=std::ranges::find(values,self->rate);const auto index=it==std::end(values)?1:static_cast<std::size_t>(it-std::begin(values));self->rate=values[(index+1)%4];self->mutate([&](state::StateData& preferences){preferences.preferences["lesson_narration_settings"]["rate"]=self->rate;});if(self->player.allocated())self->player.set_rate(self->rate);self->update_buttons();break;}
            case 6:self->restart();break;default:break;}return 0;}break;
        case WM_DRAWITEM:{const auto& item=*reinterpret_cast<DRAWITEMSTRUCT*>(lp);const auto index=static_cast<std::size_t>(item.CtlID-button_base);if(index>=self->buttons.size())return FALSE;
            auto bounds=item.rcItem;const auto palette=reader_theme::palette(window);FillRect(item.hDC,&bounds,reader_theme::brush(reader_theme::Surface::card));InflateRect(&bounds,-self->px(11),-self->px(11));
            static constexpr Icon icons[]{Icon::rewind3,Icon::rewind5,Icon::forward5,Icon::forward3,Icon::speaker,Icon::play,Icon::reset};
            if(index==5){const auto previous=SelectObject(item.hDC,self->font);SetBkMode(item.hDC,TRANSPARENT);SetTextColor(item.hDC,reader_theme::accent_ink(window,palette.card));const auto caption=self->rate==.5?L"0.5X":self->rate==1?L"1X":self->rate==1.5?L"1.5X":L"2X";auto caption_bounds=item.rcItem;DrawTextW(item.hDC,caption,-1,&caption_bounds,DT_SINGLELINE|DT_CENTER|DT_VCENTER);SelectObject(item.hDC,previous);}
            else draw_icon(item.hDC,bounds,icons[index],index==4?(self->wanted?palette.accent:RGB(255,255,255)):index==6?RGB(170,174,183):RGB(255,255,255));
            if(item.itemState&ODS_FOCUS)reader_theme::paint_keyboard_focus(item.hwndItem,item.hDC,item.rcItem,palette.muted);return TRUE;}
        case WM_LBUTTONDOWN:{self->drag_start={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&self->drag_start);RECT rect{};GetWindowRect(window,&rect);self->bar_start={rect.left,rect.top};ScreenToClient(self->view,&self->bar_start);self->drag_at=GetTickCount64();self->dragging=true;SetCapture(window);return 0;}
        case WM_MOUSEMOVE:if(self->dragging&&(wp&MK_LBUTTON)&&GetTickCount64()-self->drag_at>=90){POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);RECT bar{};GetWindowRect(window,&bar);const auto x=self->bar_start.x+point.x-self->drag_start.x,y=self->bar_start.y+point.y-self->drag_start.y;
            const auto& area=self->floating_bounds;
            self->floating_position={std::clamp<LONG>(x,area.left,std::max<LONG>(area.left,area.right-(bar.right-bar.left))),std::clamp<LONG>(y,area.top,std::max<LONG>(area.top,area.bottom-(bar.bottom-bar.top)))};self->positioned=true;
            SetWindowPos(window,HWND_TOP,self->floating_position.x,self->floating_position.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOCOPYBITS);}return 0;
        case WM_LBUTTONUP:case WM_CAPTURECHANGED:self->dragging=false;if(GetCapture()==window)ReleaseCapture();return 0;
        case WM_NCDESTROY:KillTimer(window,tick_timer);self->window=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);break;
        default:break;}}
        catch(const std::exception& failure){self->error=failure.what();self->wanted=false;try{(void)self->player.release();}catch(...){}self->update_buttons();}
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
void LessonReader::seek_seconds(int seconds){impl_->seek(seconds);}
void LessonReader::restart(){impl_->restart();}
HWND LessonReader::bar()const{return impl_->window;}
LessonReaderSnapshot LessonReader::snapshot()const{
    const auto& self=*impl_;LessonReaderSnapshot value;
    value.playing=self.wanted&&self.player.ready();value.allocated=self.player.allocated();value.planned=self.has_plan;
    value.preparing=self.planning;value.finished=self.finished;value.waiting_gate=self.waiting_gate;value.index=self.current();
    value.offset=self.player.allocated()?static_cast<std::int64_t>(std::llround(self.player.position())):self.position.milliseconds;
    value.heard_through=self.position.heard_through;value.error=self.error;value.key=self.key;
    for(const auto& part:self.parts){LessonReaderSegmentSnapshot segment;
        segment.region=part.region.key;segment.source=part.sentence.text;segment.spoken=part.sentence.spoken;segment.target=part.region.target;
        segment.block=part.region.block_index;segment.start=part.sentence.start;segment.end=part.sentence.end;
        segment.feedback=part.region.feedback;segment.correct=part.region.correct;segment.available=part.audio.has_value();
        if(part.audio){segment.duration_ms=part.audio->duration_ms;segment.timings=part.audio->timings;}
        value.segments.push_back(std::move(segment));
    }
    return value;
}
}

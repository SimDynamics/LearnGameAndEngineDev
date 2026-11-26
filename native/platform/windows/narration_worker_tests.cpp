#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <future>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
using winrt::Windows::Data::Json::JsonObject;
JsonObject json(std::string_view value) { return JsonObject::Parse(winrt::to_hstring(value)); }
void cancelled_inflight_protocol(const std::filesystem::path& root,const std::filesystem::path& python,const std::filesystem::path& build_directory){
    using namespace lgaed;
    check(std::filesystem::is_regular_file(python)&&std::filesystem::is_directory(build_directory),"Protocol test requires its configured Python and existing build directory");
    const auto runtime=build_directory/L"narration-protocol-fixture-runtime.json";
    JsonObject config;config.Insert(L"java",winrt::Windows::Data::Json::JsonValue::CreateStringValue(python.native()));
    winrt::Windows::Data::Json::JsonArray arguments;
    arguments.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"-u"));
    arguments.Append(winrt::Windows::Data::Json::JsonValue::CreateStringValue((root/L"native/platform/windows/narration_worker_protocol_fixture.py").native()));
    config.Insert(L"arguments",arguments);
    {std::ofstream output(runtime,std::ios::binary|std::ios::trunc);check(static_cast<bool>(output),"Cannot write controlled protocol configuration");output<<winrt::to_string(config.Stringify());check(static_cast<bool>(output),"Cannot save controlled protocol configuration");}
    windows::NarrationWorker worker(runtime);std::stop_source stopped;
    auto calculation=std::async(std::launch::async,[&]{
        try{(void)worker.math_calculate(R"({"latex":"1+1"})",stopped.get_token());return false;}
        catch(const std::exception& error){return std::string_view(error.what())=="Lectura cancelada";}
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool observed=false;
    do{observed=json(worker.math_editor(R"({"action":"probe"})")).GetNamedBoolean(L"observed");}
    while(!observed&&std::chrono::steady_clock::now()<deadline);
    check(observed,"Controlled fixture never observed the in-flight request");
    stopped.request_stop();
    const bool returned_before_release=calculation.wait_for(std::chrono::milliseconds(500))==std::future_status::ready;
    const auto cancellation=json(worker.math_editor(R"({"action":"probe"})"));
    const auto release=json(worker.math_editor(R"({"action":"release"})"));
    const bool rejected=calculation.get();
    check(returned_before_release,"An in-flight mathematical cancellation waited for its explicitly withheld response");
    check(rejected&&cancellation.GetNamedBoolean(L"cancelled"),"Mathematical cancellation did not reach the intended in-flight request");
    check(release.GetNamedBoolean(L"released"),"A late cancelled reply was routed to the release request");
    for(const auto* marker:{L"next-request-one",L"next-request-two"}){
        JsonObject request;request.Insert(L"action",winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"echo"));request.Insert(L"marker",winrt::Windows::Data::Json::JsonValue::CreateStringValue(marker));
        check(json(worker.math_editor(winrt::to_string(request.Stringify()))).GetNamedString(L"marker")==marker,"A late cancelled reply corrupted subsequent request identities");
    }
    std::cout<<"PASS controlled IPC in-flight mathematical cancellation, withheld late reply and subsequent request identities (not an audio benchmark)\n";
}
void priority_protocol(const std::filesystem::path& build_directory) {
    using namespace lgaed::windows;
    NarrationWorker worker(build_directory/L"narration-protocol-fixture-runtime.json");
    auto maintenance=worker.try_idle_maintenance_lease();
    check(maintenance.has_value()&&!worker.try_idle_maintenance_lease(),"Idle cache maintenance did not own the existing exclusion atomically");
    maintenance.reset();
    const auto name=L"Local\\LGAED-Narration-Priority-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    struct Event{HANDLE value;~Event(){if(value)CloseHandle(value);}} observed{CreateEventW(nullptr,TRUE,FALSE,name.c_str())};
    check(observed.value!=nullptr,"Cannot create the controlled inference observation event");
    NarrationSegment segment;segment.text="cpu-priority-event:"+winrt::to_string(name);segment.prepared=segment.text;
    auto generate=[&](std::stop_token stop={}) {
        return std::async(std::launch::async,[&,stop]{try {
            (void)worker.prepare(segment,"es","ef_dora",build_directory,0,stop,NarrationCpuClass::background);return false;
        }catch(const NarrationPreempted&){return !stop.stop_requested();
        }catch(const std::exception& error){return std::string_view(error.what())=="Lectura cancelada";}});
    };
    auto page=worker.current_page_lease();
    check(!worker.try_idle_maintenance_lease(),"Maintenance acquired a worker while a current page owned preparation");
    auto background=generate();
    check(json(worker.math_editor(R"({"action":"echo","marker":"current-page-owned"})")).GetNamedString(L"marker")==L"current-page-owned", "Current page reservation blocked the independent mathematical channel");
    check(WaitForSingleObject(observed.value,0)==WAIT_TIMEOUT,"Background inference ran while a current page reservation was held");
    page={};
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Background inference did not resume after page preparation released priority");
    check(!worker.try_idle_maintenance_lease()&&background.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready,
        "An idle-only maintenance request interrupted active inference");
    page=worker.current_page_lease();
    check(background.wait_for(std::chrono::seconds(1))==std::future_status::ready&&background.get(),"Current page preemption did not cancel and acknowledge the actual background request");
    check(json(worker.math_editor(R"({"action":"echo","marker":"after-current-preemption"})")).GetNamedString(L"marker")==L"after-current-preemption", "Background cancellation corrupted the following foreground request");

    std::stop_source abandoned;
    auto cancelled_waiter=generate(abandoned.get_token());
    abandoned.request_stop();
    check(cancelled_waiter.wait_for(std::chrono::seconds(1))==std::future_status::ready&&cancelled_waiter.get(),"A queued background request could not be cancelled while a current page retained priority");
    page={};
    check(ResetEvent(observed.value),"Cannot reset the controlled observation event");
    auto before_math=generate();
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Background inference did not recover after a cancelled queued request");
    check(json(worker.math_editor(R"({"action":"echo","marker":"math-preemption"})")).GetNamedString(L"marker")==L"math-preemption", "Interactive mathematics did not preempt background synthesis");
    check(before_math.wait_for(std::chrono::seconds(1))==std::future_status::ready&&before_math.get(),"Interactive mathematics did not acknowledge background cancellation");
    check(json(worker.curriculum_plan("{}",0,"es","ef_dora","es")).GetNamedArray(L"segments").Size()==0,"Background curriculum planning did not recover after foreground work");
    std::cout<<"PASS controlled IPC current-page reservation, background resume, acknowledged foreground/mathematical preemption and cancellable queued work (not an audio benchmark)\n";
}
void heavy_priority_protocol(const std::filesystem::path& build_directory) {
    using namespace lgaed::windows;
    NarrationWorker worker(build_directory/L"narration-protocol-fixture-runtime.json");
    const auto name=L"Local\\LGAED-Narration-Heavy-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    struct Event{HANDLE value;~Event(){if(value)CloseHandle(value);}} observed{CreateEventW(nullptr,TRUE,FALSE,name.c_str())};
    check(observed.value!=nullptr,"Cannot observe controlled heavy-work exclusion");
    NarrationSegment segment;segment.text="cpu-priority-event:"+winrt::to_string(name);segment.prepared=segment.text;
    auto page=worker.current_page_lease();std::stop_source cancel_page;
    auto foreground=std::async(std::launch::async,[&]{try {
        (void)worker.prepare(segment,"es","ef_dora",build_directory,0,cancel_page.get_token());return false;
    }catch(const NarrationPreempted&){return false; // internal preemption must never escape the current page
    }catch(const std::exception& error){return std::string_view(error.what())=="Lectura cancelada";}});
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Controlled current-page inference never started");
    auto heavy=worker.heavy_work_lease(); // must not wait for the retained page lease
    check(foreground.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready,"Heavy work leaked a preparation error to the current-page reader");
    check(ResetEvent(observed.value),"Cannot reset the heavy-work observation event");
    auto nested=worker.heavy_work_lease();heavy={};
    check(WaitForSingleObject(observed.value,0)==WAIT_TIMEOUT,"Inference resumed while a nested heavy-work exclusion remained active");
    nested={};
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Current-page inference did not resume automatically after heavy work released exclusion");
    cancel_page.request_stop();
    check(foreground.wait_for(std::chrono::seconds(1))==std::future_status::ready&&foreground.get(),"Cancelling the resumed page did not preserve its original cancellation contract");
    page={};
    check(ResetEvent(observed.value),"Cannot reset the background heavy-work observation event");
    auto background=std::async(std::launch::async,[&]{try {
        (void)worker.prepare(segment,"es","ef_dora",build_directory,0,{},NarrationCpuClass::background);return false;
    }catch(const NarrationPreempted&){return true;}});
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Background inference did not start before heavy work");
    heavy=worker.heavy_work_lease();
    check(background.wait_for(std::chrono::seconds(1))==std::future_status::ready&&background.get(),"Heavy work did not cancel and acknowledge active background inference");
    heavy={};
    check(json(worker.math_editor(R"({"action":"echo","marker":"after-heavy-work"})")).GetNamedString(L"marker")==L"after-heavy-work", "Heavy-work cleanup left subsequent mathematical requests blocked");
    std::cout<<"PASS controlled IPC heavy-work exclusion cancels/acknowledges actual inference, does not wait for page leases, suspends foreground retry, preserves nested exclusion and resumes without a reader error (not an audio benchmark)\n";
}
void calculator_heavy_protocol(const std::filesystem::path& build_directory) {
    using namespace lgaed::windows;
    NarrationWorker worker(build_directory/L"narration-protocol-fixture-runtime.json");
    const auto name=L"Local\\LGAED-Narration-Calculator-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    struct Event{HANDLE value;~Event(){if(value)CloseHandle(value);}} observed{CreateEventW(nullptr,TRUE,FALSE,name.c_str())};
    check(observed.value!=nullptr,"Cannot observe calculator synthesis exclusion");
    NarrationSegment segment;segment.text="cpu-priority-event:"+winrt::to_string(name);segment.prepared=segment.text;
    std::stop_source stop;
    auto foreground=std::async(std::launch::async,[&]{try {
        (void)worker.prepare(segment,"es","ef_dora",build_directory,0,stop.get_token());return false;
    }catch(const std::exception& error){return std::string_view(error.what())=="Lectura cancelada";}});
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Controlled inference did not start before calculator evaluation");
    auto calculation=std::async(std::launch::async,[&]{return worker.math_calculate(R"({"latex":"1+1"})");});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool calculating=false;
    do{calculating=json(worker.math_editor(R"({"action":"probe"})")).GetNamedString(L"pendingCommand")==L"math_calculate";}
    while(!calculating&&std::chrono::steady_clock::now()<end);
    check(calculating,"Mathematical evaluation did not acquire acknowledged heavy-work exclusion");
    check(foreground.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready,"Calculator preemption leaked a current-page reader error");
    check(ResetEvent(observed.value),"Cannot reset the calculator inference observation event");
    check(json(worker.math_editor(R"({"action":"echo","marker":"editor-during-calculation"})")).GetNamedString(L"marker")==L"editor-during-calculation", "Heavy calculator evaluation blocked the independent structured editor");
    check(WaitForSingleObject(observed.value,0)==WAIT_TIMEOUT,"Inference resumed while calculator evaluation still held exclusion");
    (void)worker.math_editor(R"({"action":"release"})");
    check(calculation.wait_for(std::chrono::seconds(1))==std::future_status::ready,"Controlled calculator evaluation did not finish");
    (void)calculation.get();
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Current-page synthesis did not resume after mathematical evaluation");
    stop.request_stop();
    check(foreground.wait_for(std::chrono::seconds(1))==std::future_status::ready&&foreground.get(),"Resumed synthesis could not be cancelled after mathematical evaluation");
    std::cout<<"PASS controlled IPC calculator exclusion pauses current synthesis, preserves independent expression editing and resumes the same page after evaluation (not an audio benchmark)\n";
}
void recording_key_protocol(const std::filesystem::path& build_directory){
    using namespace lgaed::windows;NarrationWorker worker(build_directory/L"narration-protocol-fixture-runtime.json");
    const auto missing=worker.plan("controlled-missing-recording-key","es");
    check(missing.size()==1&&missing.front().recording_key.empty(),"A controlled missing recording key became a fabricated cache identity");
    bool rejected=false;try{(void)worker.plan("controlled-invalid-recording-key","es");}
    catch(const std::runtime_error& error){rejected=std::string_view(error.what())=="Invalid narration recording key";}
    check(rejected,"A descriptive/phony transport key was accepted as a recording identity");
    check(json(worker.math_editor(R"({"action":"echo","marker":"recording-key-validation"})")).GetNamedString(L"marker")==L"recording-key-validation",
        "Rejecting an invalid recording key broke following request routing");
}
void recovery_protocol(const std::filesystem::path& build_directory){
    using namespace lgaed::windows;
    using namespace winrt::Windows::Data::Json;
    NarrationWorker worker(build_directory/L"narration-protocol-fixture-runtime.json");
    const auto pid=[&]{
        return json(worker.math_editor(R"({"action":"echo","marker":"same-generation"})")).GetNamedNumber(L"processId");
    };
    const auto marker_root=build_directory/(L"recovery-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    check(std::filesystem::create_directory(marker_root),"Create an isolated process-recovery fixture directory");
    struct Cleanup {std::filesystem::path root;~Cleanup(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}} cleanup{marker_root};
    const auto request=[&](std::string_view mode,const std::filesystem::path& marker){
        JsonObject doc;doc.Insert(L"qaTransport",JsonValue::CreateStringValue(winrt::to_hstring(mode)));
        doc.Insert(L"markerFile",JsonValue::CreateStringValue(marker.native()));
        return winrt::to_string(doc.Stringify());
    };
    auto previous=pid();
    // A genuine process exit and an uncorrelated legacy error both require
    // a fresh generation, not a five-minute wait or a new application reader.
    for(const auto* mode:{"exit-once","unidentified-once"}){
        const auto marker=marker_root/winrt::to_hstring(mode).c_str();
        const auto reply=json(worker.curriculum_plan(request(mode,marker),0,"es","ef_dora","es"));
        check(std::filesystem::is_regular_file(marker)&&reply.GetNamedNumber(L"processId")!=previous,
            "A safe planning retry reused the failed process or lost its own reply");
        previous=pid();
        check(previous==reply.GetNamedNumber(L"processId"),"The replacement generation was not retained");
    }
    bool rejected=false;
    try{(void)worker.curriculum_plan(request("error",marker_root/L"error"),0,"es","ef_dora","es");}
    catch(const std::exception& error){rejected=std::string_view(error.what()).find("correlated-test-error")!=std::string_view::npos;}
    check(rejected&&pid()==previous,"A correlated application error was lost or restarted a healthy process");
    // A dead JVM can have another call waiting on its independent editor lane.
    auto calculation=std::async(std::launch::async,[&]{
        try{(void)worker.math_calculate(R"({"latex":"1+1"})");return false;}
        catch(const std::exception&){return true;}
    });
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    bool observed=false;
    do{observed=json(worker.math_editor(R"({"action":"probe"})")).GetNamedBoolean(L"observed");}
    while(!observed&&std::chrono::steady_clock::now()<deadline);
    if(!observed){worker.shutdown();(void)calculation.get();throw std::runtime_error("The independent calculation was not pending");}
    const auto plan=worker.page_plan(request("exit-once",marker_root/L"concurrent-exit"),0,"es","ef_dora","es");
    check(plan.page_id=="protocol-fixture/es/page"&&plan.revision==std::string(64,'a'),
        "The same current-page request failed to recover its authenticated identity");
    check(calculation.wait_for(std::chrono::seconds(5))==std::future_status::ready&&calculation.get(),
        "An old generation retained a pending independent calculation");
    previous=pid();
    JsonObject mutation;mutation.Insert(L"action",JsonValue::CreateStringValue(L"terminate"));
    mutation.Insert(L"markerFile",JsonValue::CreateStringValue((marker_root/L"mutation").native()));
    rejected=false;try{(void)worker.math_editor(winrt::to_string(mutation.Stringify()));}
    catch(const std::exception&){rejected=true;}
    check(rejected&&std::filesystem::is_regular_file(marker_root/L"mutation")&&pid()!=previous,
        "A stateful editor failure was replayed or prevented subsequent recovery");
    const auto repeat=marker_root/L"repeat";
    rejected=false;try{(void)worker.curriculum_plan(request("always-exit",repeat),0,"es","ef_dora","es");}
    catch(const std::exception&){rejected=true;}
    const auto bytes=state::read_bytes(repeat);
    check(rejected&&std::ranges::count(bytes,static_cast<std::uint8_t>('\n'))==2,
        "Safe request recovery was not bounded to one retry");
    const auto after=json(worker.curriculum_plan("{}",0,"es","ef_dora","es"));
    check(after.GetNamedArray(L"segments").Size()==0,"A failed retry poisoned later planning");
    worker.shutdown();
    rejected=false;try{(void)worker.curriculum_plan("{}",0,"es","ef_dora","es");}
    catch(const std::exception&){rejected=true;}
    check(rejected,"Terminal shutdown incorrectly started another worker");
    std::cout<<"PASS real Win32 child exit, uncorrelated error, bounded safe retry, generation-scoped replies, concurrent failure and terminal shutdown (no PCM claim)\n";
}
void page_revision_cache(lgaed::windows::NarrationWorker& worker,const std::filesystem::path& cache){
    using namespace lgaed;using namespace windows;
    const auto plan_voice=[&](std::string_view source,std::string_view voice,bool previous=false){
        return worker.plan(source,"es",{},"auto","auto",NarrationOutputDataPolicy::current,{},NarrationSemanticPolicy::current,previous,voice);};
    const auto captions=worker.page_plan(R"({"id":"native-recording-key","pages":[{"blocks":[{"type":"code","language":"cpp","code":"int valor = 1;"}]}]})",0,"es","em_santa","es");
    const auto caption=std::ranges::find_if(captions.segments,[](const auto& value){return value.text=="Ejemplo de código C más más.";});
    check(caption!=captions.segments.end()&&caption->recording_key.size()==64,"Canonical planning lost the selected-voice caption recording key");
    const auto visible_caption=plan_voice(caption->text,"em_santa"),other_voice=plan_voice(caption->text,"ef_dora");
    check(visible_caption.size()==1&&visible_caption.front().prepared!=caption->prepared&&
        visible_caption.front().recording_key==caption->recording_key,
        "Equivalent raw/prepared caption payloads failed to share their exact selected-voice PCM identity");
    check(other_voice.size()==1&&other_voice.front().recording_key!=caption->recording_key,
        "Recording identity did not retain the explicitly selected voice");
    const auto prose=plan_voice("El tipo int conserva una unidad.","ef_dora"),historical=plan_voice("El tipo int conserva una unidad.","ef_dora",true);
    check(prose.size()==1&&historical.size()==1&&prose.front().spoken==historical.front().spoken&&
        prose.front().prepared!=historical.front().prepared&&prose.front().recording_key!=historical.front().recording_key,
        "Equal stripped words erased distinct authenticated pronunciation metadata");
    const auto output=cache/(L"page-revision-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    check(!std::filesystem::exists(output),"This regression requires a fresh private QA directory");
    const auto lesson=[](std::string_view ending,bool move_puzzle,bool revise_choice=false,bool emphasize=false){
        const std::string intro=emphasize
            ?R"({"type":"paragraph","text":"La puerta está aquí.","emphasis":[{"text":"puerta","style":"bold"}]})"
            :R"({"type":"paragraph","text":"La puerta está aquí."})";
        const std::string option=revise_choice?"Una llave.":"Una caja.";
        const std::string feedback=revise_choice?"Sigue brillando.":"Conserva ese brillo.";
        const std::string puzzle=R"({"type":"multiple_choice","question":"¿Qué objeto brilla?","options":["Un aro.",")"+
            option+R"("],"correctIndex":0,"explanation":"El aro refleja la luz. )"+feedback+R"("})";
        const std::string tail=R"({"type":"paragraph","text":"El ejemplo termina. )"+std::string(ending)+R"("})";
        return R"({"id":"native-page-revision","pages":[{"blocks":[)"+intro+","+
            (move_puzzle?tail+","+puzzle:puzzle+","+tail)+R"(]}],"stateLayout":{"version":2,"current":[{"id":"page","blocks":["intro",)"+
            (move_puzzle?R"("tail","puzzle")":R"("puzzle","tail")")+R"(]}]}})";
    };
    using FileSeal=std::pair<std::string,std::filesystem::file_time_type>;
    std::map<std::string,NarrationAudio> by_source;
    std::map<std::filesystem::path,FileSeal> seals;
    const auto seal=[](const auto& path){return FileSeal{state::sha256(state::read_bytes(path)),std::filesystem::last_write_time(path)};};
    const auto prepare=[&](NarrationWorker& backend,const auto& plan,const auto& session){
        std::vector<NarrationAudio> audio;
        for(auto sentence:plan.segments){
            sentence.cache_namespace=session.cache_namespace();
            auto result=backend.prepare(sentence,"es","ef_dora",output,2,session.stop_token());
            check(result.cache_hit==(result.synthesis_threads==0),"Cache status disagrees with actual Kokoro inference");
            if(const auto known=by_source.find(sentence.text);known!=by_source.end()){
                check(result.cache_hit&&result.key==known->second.key&&result.wav==known->second.wav,
                    "An unchanged sentence was synthesized or copied to a new cache key");
            }else{
                check(result.synthesis_threads>0,"A genuinely new sentence did not run Kokoro in the private cache");
                by_source.emplace(sentence.text,result);seals.emplace(result.wav,seal(result.wav));
            }
            audio.push_back(std::move(result));
        }
        return audio;
    };
    const auto misses=[](const auto& results){return std::ranges::count_if(results,[](const auto& item){return item.synthesis_threads>0;});};
    const auto before=worker.page_plan(lesson("Guarda el aro.",false),0,"es","ef_dora","es");
    auto original=worker.begin_page(output,before.page_id,"es","ef_dora","KOKORO",before,NarrationCpuClass::background);
    check(original.has_value(),"Cannot begin the original page generation");
    const auto initial=prepare(worker,before,*original);
    check(initial.size()==10&&misses(initial)==10&&by_source.contains("¿Qué objeto brilla?")&&
        by_source.contains("Correcto El aro refleja la luz.")&&by_source.contains("Conserva ese brillo.")&&initial.back().source=="Guarda el aro.",
        "The initial page must narrate the question, options, feedback and final paragraph");
    check(original->commit(initial),"Original complete page was not committed");
    original.reset();
    // Recreate the exact layout of this same page under the previous app:
    // real recordings in their original namespace, a format-1 completion and
    // no persisted seed. Only files inside this test's fresh directory change.
    std::filesystem::path manifest_path;
    for(const auto& file:std::filesystem::directory_iterator(output/L"page-audio")){
        if(file.path().extension()==L".json")manifest_path=file.path();
    }
    check(!manifest_path.empty(),"Cannot find the private baseline manifest");
    const auto read_manifest=[&]{
        const auto bytes=state::read_bytes(manifest_path);
        return json(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
    };
    auto legacy=read_manifest();
    check(legacy.GetNamedString(L"pcmSeedRevision")==winrt::to_hstring(before.revision),"Unexpected original PCM address");
    legacy.Insert(L"format",winrt::Windows::Data::Json::JsonValue::CreateNumberValue(1));legacy.Remove(L"pcmSeedRevision");
    {std::ofstream stream(manifest_path,std::ios::binary|std::ios::trunc);stream<<winrt::to_string(legacy.Stringify());
        stream.flush();check(static_cast<bool>(stream),"Cannot write the private legacy manifest");}
    auto seed=manifest_path;seed.replace_extension(L".pcm-seed");
    check(std::filesystem::remove(seed),"Cannot reproduce an installed cache that predates persisted seeds");
    // A fresh process requests the edited page directly, as after a software
    // update and a simultaneous edit by the course author.
    NarrationWorker reopened(std::filesystem::path(build::source_root)/L"out/narration/runtime.json");
    const auto after=reopened.page_plan(lesson("Empieza otro turno.",false),0,"es","ef_dora","es");
    check(before.page_id==after.page_id&&before.revision!=after.revision,"The final edit did not revise the page script");
    check(reopened.published_page_revision(output,after.page_id,"es","ef_dora","KOKORO")==before.revision&&
        !reopened.published_page_revision(output,after.page_id,"es","em_santa","KOKORO"),"Legacy publication lost its page or voice owner");
    auto edited=reopened.begin_page(output,after.page_id,"es","ef_dora","KOKORO",after,NarrationCpuClass::background);
    check(edited.has_value(),"Cannot begin the edited page generation");
    const auto changed=prepare(reopened,after,*edited);
    check(misses(changed)==1&&changed.back().source=="Empieza otro turno."&&!changed.back().cache_hit&&edited->commit(changed),
        "Editing the final sentence must synthesize precisely that sentence");
    const auto reordered=reopened.page_plan(lesson("Empieza otro turno.",true),0,"es","ef_dora","es");
    auto latest=reopened.begin_page(output,reordered.page_id,"es","ef_dora","KOKORO",reordered,NarrationCpuClass::current_page);
    check(latest&&edited->stop_token().stop_requested()&&!edited->current()&&after.revision!=reordered.revision,
        "A new foreground order did not supersede the background session");
    check(!reopened.begin_page(output,after.page_id,"es","ef_dora","KOKORO",after,NarrationCpuClass::background),
        "Foreground and background acquired the same page simultaneously");
    reopened.set_storage_budget(0);
    const auto ordered=prepare(reopened,reordered,*latest);
    check(ordered.size()==10&&ordered[2].source=="Empieza otro turno."&&ordered.back().source=="Conserva ese brillo."&&misses(ordered)==0,
        "Moving the puzzle must update the narrated order with zero syntheses");
    check(latest->commit(ordered)&&!edited->commit(changed),"An obsolete completion overwrote the current ordered script");
    const auto proof=read_manifest();
    check(proof.GetNamedNumber(L"format")==2&&proof.GetNamedString(L"revision")==winrt::to_hstring(reordered.revision)&&
        reopened.published_page_revision(output,reordered.page_id,"es","ef_dora","KOKORO")==reordered.revision,
        "Cannot authenticate the new ordered completion");
    const auto entries=proof.GetNamedArray(L"segments");
    check(entries.Size()==ordered.size(),"The published order is incomplete");
    for(std::uint32_t i=0;i<entries.Size();++i)check(entries.GetObjectAt(i).GetNamedString(L"key")==winrt::to_hstring(ordered[i].key),
        "The completion manifest retained the earlier puzzle position");
    bool incomplete_rejected=false;try{(void)latest->commit({});}catch(const std::invalid_argument&){incomplete_rejected=true;}
    check(incomplete_rejected,"An incomplete generation published a complete page manifest");
    reopened.set_storage_budget(std::numeric_limits<std::uint64_t>::max());
    const auto quiz=reopened.page_plan(lesson("Empieza otro turno.",true,true),0,"es","ef_dora","es");
    auto quiz_session=reopened.begin_page(output,quiz.page_id,"es","ef_dora","KOKORO",quiz,NarrationCpuClass::current_page);
    check(quiz_session.has_value()&&quiz.revision!=reordered.revision,"The revised option and feedback did not change the script");
    const auto corrected=prepare(reopened,quiz,*quiz_session);
    check(corrected.size()==10&&misses(corrected)==2&&by_source.contains("Una llave.")&&by_source.contains("Sigue brillando.")&&quiz_session->commit(corrected),
        "Editing one option and one feedback sentence must synthesize exactly those two utterances");
    reopened.set_storage_budget(0);
    const auto visual=reopened.page_plan(lesson("Empieza otro turno.",true,true,true),0,"es","ef_dora","es");
    auto visual_session=reopened.begin_page(output,visual.page_id,"es","ef_dora","KOKORO",visual,NarrationCpuClass::current_page);
    check(visual_session.has_value()&&visual.revision!=quiz.revision,"The visual source change must be detected");
    const auto emphasized=prepare(reopened,visual,*visual_session);
    check(emphasized.size()==10&&misses(emphasized)==0&&visual_session->commit(emphasized),
        "Changing only the paragraph's visual emphasis must not run the synthesizer");
    for(const auto& [path,original_seal]:seals)check(seal(path)==original_seal,"Editing or reordering rewrote surviving audio");
    std::cout<<"PASS real Kokoro: 10 baseline syntheses, 1 for an edited final sentence after legacy reopen, 0 after moving the puzzle, 2 for a changed option and feedback, 0 for visual emphasis; WAV hashes, timestamps and ordered manifest verified\n";
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace lgaed;
    try {
        windows::ensure_winrt_apartment();
        const auto root = std::filesystem::path(build::source_root);
        if(argc==3&&std::wstring_view(argv[1])==L"--page-revision"){
            const auto cache=std::filesystem::path(argv[2]).lexically_normal();
            check(cache.is_absolute(),"The page-revision test requires an absolute isolated QA cache");
            const auto relative=cache.lexically_relative(std::filesystem::path(L"C:\\LGAED-QA"));
            check(!relative.empty()&&!relative.is_absolute()&&std::ranges::none_of(relative,[](const auto& part){return part==L"..";}),
                "The page-revision test cache must stay inside C:\\LGAED-QA");
            state::require_safe_path(cache);std::filesystem::create_directories(state::native_io_path(cache));
            windows::NarrationWorker worker(root/L"out/narration/runtime.json");
            page_revision_cache(worker,cache);return 0;
        }
        const bool protocol_only=argc==4&&std::wstring_view(argv[1])==L"--protocol-only";
        check(argc==3||protocol_only,"The narration test requires CMake's Python executable and existing build directory arguments");
        const auto python=std::filesystem::path(argv[protocol_only?2:1]);
        const auto build_directory=std::filesystem::path(argv[protocol_only?3:2]);
        cancelled_inflight_protocol(root,python,build_directory);
        priority_protocol(build_directory);
        heavy_priority_protocol(build_directory);
        calculator_heavy_protocol(build_directory);
        recording_key_protocol(build_directory);
        recovery_protocol(build_directory);
        if(protocol_only)return 0;
        windows::NarrationWorker worker(root / "out/narration/runtime.json");
        const std::string source = "La línea #include y std::cout muestran una salida.";
        const auto segments = worker.plan(source,"es");
        check(segments.size() == 1 && segments.front().text == source,"Worker lost the original source sentence");
        const auto& segment = segments.front();
        check(!segment.spoken.empty() && segment.spoken_ranges.size() == segment.displayed_ranges.size() &&
              segment.spoken_ranges.size() > 5,"Worker omitted real UTF16 source projections");
        const auto identity = worker.identity("native-real-kokoro-page","es","ef_dora",segments);
        check(identity.size() == 64 && worker.identity("native-real-kokoro-page","es","ef_dora",segments) == identity,
              "Native bridge changed the original resume identity");
        check(worker.identity("another-native-page","es","ef_dora",segments) != identity,
              "Different pages share their original resume identity");
        check(worker.identity("native-real-kokoro-page","es","ef_dora",segments,{},"ANDROID") != identity,
              "The original system-voice identity collided with Kokoro");
        const auto cache = root / "out/qa/narration-native-tests";
        const auto audio = worker.prepare(segment,"es","ef_dora",cache,2);
        check(audio.source == source && audio.duration_ms > 200 && audio.key.size() == 64,
              "Real Kokoro audio lost its original source or cache key");
        check(audio.timings.size() > 5,"Real Kokoro did not return measured word timings");
        double previous = -1; std::size_t last_source = 0;
        for (const auto& row : audio.timings) {
            check(row.start < row.end && row.end <= winrt::to_hstring(source).size(),"Word timing is outside the original source");
            check(std::isfinite(row.start_ms) && std::isfinite(row.end_ms) && row.start_ms >= previous &&
                  row.start_ms < row.end_ms && row.end_ms <= audio.duration_ms,"Word timing is outside real playback");
            previous = row.start_ms; last_source = std::max(last_source,row.end);
        }
        check(last_source == winrt::to_hstring(source).size(),"Technical symbols left the sentence unfinished");
        const auto before = state::read_bytes(audio.wav);
        const auto timestamp = std::filesystem::last_write_time(audio.wav);
        const auto repeated = worker.prepare(segment,"es","ef_dora",cache,2);
        check(repeated.cache_hit && repeated.key == audio.key && repeated.wav == audio.wav,
              "The native client resynthesized a valid installed recording");
        check(state::read_bytes(repeated.wav) == before && std::filesystem::last_write_time(repeated.wav) == timestamp,
              "A cache hit modified the existing waveform");
        check(repeated.timings.size() == audio.timings.size(),"A cache hit lost its exact word timings");

        // Exact source JSON is deliberately not parsed by WinRT on its way to
        // the original rational evaluator: these adjacent integers exceed 2^53.
        constexpr std::string_view block = R"json({"type":"math_puzzle","schemaVersion":1,"prompt":"Conserva el entero exacto.","responses":[{"id":"number","type":"math_numeric_input","label":"Número","initial":9007199254740993,"validator":{"type":"numeric","expected":9007199254740993}}],"feedback":{"correct":"El entero coincide.","incorrect":"Comprueba la unidad final."}})json";
        const auto description_raw = worker.describe_math(block,"es");
        check(description_raw.find("9007199254740993") != std::string::npos,"UI shape rounded the original exact integer");
        check(description_raw.find("validator") == std::string::npos && description_raw.find("expected") == std::string::npos,
              "The visible mathematical description disclosed a hidden answer");
        const auto correct = json(worker.validate_math(block,R"json({"number":{"type":"text","value":"9007199254740993"}})json","es"));
        check(correct.GetNamedBoolean(L"evaluable") && correct.GetNamedBoolean(L"correct"),"Exact original mathematical answer failed through the native bridge");
        const auto wrong = json(worker.validate_math(block,R"json({"number":{"type":"text","value":"9007199254740992"}})json","es"));
        check(wrong.GetNamedBoolean(L"evaluable") && !wrong.GetNamedBoolean(L"correct"),"Adjacent exact integers were rounded into the same answer");
        const auto incomplete = json(worker.validate_math(block,"{}","fr"));
        check(!incomplete.GetNamedBoolean(L"evaluable") && !incomplete.GetNamedBoolean(L"correct"),"An incomplete answer was charged as a wrong evaluated answer");
        std::stop_source cancelled; cancelled.request_stop(); bool rejected = false;
        try { (void)worker.plan("Lectura cancelada.","es",cancelled.get_token()); }
        catch (const std::exception&) { rejected = true; }
        check(rejected,"A cancelled native request was executed");
        check(!worker.plan("La lectura continúa.","es").empty(),"Cancellation broke subsequent planning");
        {
            // One worker process must route out-of-order mathematical replies
            // without consuming an ONNX reply or rebuilding its shared engine.
            auto voice=std::async(std::launch::async,[&]{return worker.prepare(segment,"es","ef_dora",cache,2);});
            std::vector<std::future<std::string>> editors;
            for(int index=0;index<12;++index)editors.push_back(std::async(std::launch::async,[&,index]{
                return worker.math_editor("{\"editorId\":\"native-concurrent-"+std::to_string(index)+"\",\"action\":\"create\",\"latex\":\""+std::to_string(100+index)+"\"}");
            }));
            for(int index=0;index<12;++index){const auto editor=json(editors[index].get());
                check(editor.GetNamedString(L"latex")==winrt::to_hstring(std::to_string(100+index)),
                    "Concurrent mathematical replies were routed to another request");
                (void)worker.math_editor("{\"editorId\":\"native-concurrent-"+std::to_string(index)+"\",\"action\":\"close\"}");
            }
            const auto retained=voice.get();check(retained.cache_hit&&retained.key==audio.key&&retained.wav==audio.wav,
                "Editing mathematics changed or regenerated a compatible Kokoro recording");
            std::stop_source stopped;stopped.request_stop();bool cancelled_editor=false;
            try{(void)worker.math_editor(R"({"editorId":"cancelled","action":"create"})",stopped.get_token());}
            catch(const std::exception&){cancelled_editor=true;}
            check(cancelled_editor,"A cancelled mathematical request was executed");
            check(!worker.plan("La lectura continúa.","es").empty(),"Mathematical cancellation interfered with narration");
            std::cout<<"PASS one-process concurrent mathematical/audio replies, cache reuse and independent cancellation\n";
        }
        std::cout << "PASS native real Kokoro plan/prepare/identity/cache/word timings and exact mathematical bridge\n";
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << "FAIL native narration worker: " << winrt::to_string(error.message()) << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL native narration worker: " << error.what() << '\n';
    }
    return 1;
}

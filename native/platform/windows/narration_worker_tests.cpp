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
    auto background=generate();
    check(json(worker.math_editor(R"({"action":"echo","marker":"current-page-owned"})")).GetNamedString(L"marker")==L"current-page-owned", "Current page reservation blocked the independent mathematical channel");
    check(WaitForSingleObject(observed.value,0)==WAIT_TIMEOUT,"Background inference ran while a current page reservation was held");
    page={};
    check(WaitForSingleObject(observed.value,5000)==WAIT_OBJECT_0,"Background inference did not resume after page preparation released priority");
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
}

int wmain(int argc,wchar_t** argv) {
    using namespace lgaed;
    try {
        windows::ensure_winrt_apartment();
        const auto root = std::filesystem::path(build::source_root);
        check(argc==3,"The narration test requires CMake's Python executable and existing build directory arguments");
        cancelled_inflight_protocol(root,argv[1],argv[2]);
        priority_protocol(argv[2]);
        heavy_priority_protocol(argv[2]);
        calculator_heavy_protocol(argv[2]);
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

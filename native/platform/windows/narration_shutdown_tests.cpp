#include "platform/windows/audio_generation.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "native_build_config.hpp"
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <latch>
#include <stdexcept>
#include <thread>

namespace {
using namespace lgaed;
using namespace windows;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Predicate> void eventually(Predicate predicate,const char* message){
    const auto deadline=std::chrono::steady_clock::now()+5s;
    do{if(predicate())return;std::this_thread::sleep_for(10ms);}while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error(message);
}
std::vector<JsonObject> records(const std::filesystem::path& log){
    std::ifstream input(log);std::string line;std::vector<JsonObject> result;
    while(std::getline(input,line))try{result.push_back(JsonObject::Parse(winrt::to_hstring(line)));}
        catch(const winrt::hresult_error&){break;} // A live final line may be partial.
    return result;
}
bool observed(const std::filesystem::path& log,const wchar_t* command){
    for(const auto& row:records(log))if(row.GetNamedString(L"command")==command)return true;
    return false;
}
struct Process {
    HANDLE handle=nullptr;
    DWORD pid=0;
    ~Process(){if(handle)CloseHandle(handle);}
    void capture(const std::filesystem::path& log){
        for(const auto& row:records(log))if(row.GetNamedString(L"command")==L"started"){
            pid=static_cast<DWORD>(row.GetNamedNumber(L"pid"));
            check(static_cast<DWORD>(row.GetNamedNumber(L"parentPid"))==GetCurrentProcessId(),
                "The controlled worker was not the directly launched child");
            handle=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            check(handle!=nullptr,"Cannot observe the test-owned worker process");return;
        }
        throw std::runtime_error("Fixture omitted its process identity");
    }
    bool exited()const{return handle&&WaitForSingleObject(handle,0)==WAIT_OBJECT_0;}
    bool exited_by(std::chrono::steady_clock::time_point deadline)const{
        if(exited())return true;
        // TerminateJobObject initiates termination asynchronously. Account for
        // completion within the SAME global shutdown/join deadline, never a
        // fresh timeout after shutdown returns or an exit-code-only shortcut.
        const auto now=std::chrono::steady_clock::now();
        const auto remaining=now<deadline?std::chrono::ceil<std::chrono::milliseconds>(deadline-now).count():0;
        DWORD code=STILL_ACTIVE;GetExitCodeProcess(handle,&code);
        const auto wait=WaitForSingleObject(handle,static_cast<DWORD>(remaining));
        std::cerr<<"Worker completion pid="<<pid<<" initialExitCode="<<code<<" wait="<<wait
            <<" waitedMs="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-now).count()<<'\n';
        return wait==WAIT_OBJECT_0;
    }
};
struct Fixture {
    std::filesystem::path directory,log,runtime;
    std::shared_ptr<NarrationWorker> worker;
    Fixture(const std::filesystem::path& root,const std::filesystem::path& python,
            std::filesystem::path where,bool hold_input=false):directory(std::move(where)),
            log(directory/L"transport.jsonl"),runtime(directory/L"runtime.json"){
        check(std::filesystem::create_directories(directory),"Shutdown fixture requires a new isolated directory");
        JsonObject config;config.Insert(L"java",JsonValue::CreateStringValue(python.native()));
        JsonArray arguments;
        for(const auto& value:{std::wstring(L"-u"),(root/L"native/platform/windows/audio_generation_protocol_fixture.py").native(),
                log.native(),std::wstring(L"--ignore-cancel"),std::wstring(L"--ignore-shutdown")})
            arguments.Append(JsonValue::CreateStringValue(value));
        if(hold_input)arguments.Append(JsonValue::CreateStringValue(L"--hold-input-after-prepare"));
        config.Insert(L"arguments",arguments);
        {std::ofstream output(runtime,std::ios::binary);output<<winrt::to_string(config.Stringify());
            check(static_cast<bool>(output),"Cannot save the isolated shutdown transport");}
        worker=std::make_shared<NarrationWorker>(runtime);
    }
    ~Fixture(){worker->shutdown();}
};
// Declare after futures/coordinators so failure cleanup unblocks them before
// their destructors join. It owns no process outside this worker's Job Object.
struct ShutdownGuard{std::shared_ptr<NarrationWorker> worker;~ShutdownGuard(){worker->shutdown();}};
template<class Function> bool rejected(Function&& action){
    try{action();return false;}catch(const std::exception&){return true;}
}
void verify_terminal(NarrationWorker& worker){
    worker.shutdown();worker.shutdown();
    check(rejected([&]{(void)worker.curriculum_plan(R"({"id":"after-close"})",0,"es","ef_dora","es");}),
        "A closed worker accepted or restarted a normal request");
    check(rejected([&]{(void)worker.current_page_lease();})&&rejected([&]{(void)worker.heavy_work_lease();}),
        "A closed worker granted an arbitration lease");
    check(!worker.try_idle_maintenance_lease(),"A closed worker granted cache-maintenance ownership");
}
void coordinator_shutdown(const std::filesystem::path& root,const std::filesystem::path& python,
        const std::filesystem::path& base){
    Fixture target(root,python,base/L"coordinator"),independent(root,python,base/L"independent");
    const auto marker=target.directory/L"existing-cache-artifact.bin";
    {std::ofstream output(marker,std::ios::binary);output<<"Existing test-owned cache bytes must survive shutdown.\n";}
    const auto before=state::read_bytes(marker);const auto modified=std::filesystem::last_write_time(marker);
    // Start a second owned process and prove closing the first cannot affect it.
    (void)independent.worker->curriculum_plan(R"({"id":"independent-before"})",0,"es","ef_dora","es");
    Process other;other.capture(independent.log);
    std::unique_ptr<AudioGeneration> coordinator;
    std::future<bool> waiter;
    ShutdownGuard cleanup{target.worker};
    coordinator=std::make_unique<AudioGeneration>(target.worker,root/L"app/src/main/assets/content",target.directory,target.directory);
    AudioGenerationSelection selection;selection.location={"es"};
    selection.voices={{"es","ef_dora"},{"fr","ff_siwis"},{"en-US","af_heart"},{"en-GB","bf_emma"}};
    coordinator->update(selection);
    eventually([&]{return observed(target.log,L"prepare");},"Coordinator never submitted its real in-flight request");
    Process process;process.capture(target.log);
    waiter=std::async(std::launch::async,[&]{return rejected([&]{(void)target.worker->current_page_lease();});});
    eventually([&]{return observed(target.log,L"cancel");},"Priority cancellation did not reach the controlled child");
    check(waiter.wait_for(50ms)!=std::future_status::ready,"Normal inference cancellation stopped waiting for its original acknowledgement");
    const auto started=std::chrono::steady_clock::now();
    target.worker->shutdown();coordinator.reset();
    check(waiter.wait_for(1s)==std::future_status::ready&&waiter.get(),"Shutdown did not release the queued arbitration caller");
    check(process.exited_by(started+2s),"The closed worker child survived its owned job shutdown");
    check(std::chrono::steady_clock::now()-started<2s,"Shutdown/coordinator join waited for unacknowledged inference");
    verify_terminal(*target.worker);
    (void)independent.worker->curriculum_plan(R"({"id":"independent-after"})",0,"es","ef_dora","es");
    check(!other.exited(),"Shutting down one worker terminated an independent worker");
    check(state::read_bytes(marker)==before&&std::filesystem::last_write_time(marker)==modified,
        "Service shutdown changed or deleted existing cache data");
    std::cout<<"PASS real coordinator/child shutdown ignores cancellation acknowledgement, releases arbitration, preserves cache bytes and isolates a second worker\n";
}
void blocked_transport_shutdown(const std::filesystem::path& root,const std::filesystem::path& python,
        const std::filesystem::path& base){
    Fixture target(root,python,base/L"blocked-input",true);
    std::stop_source stop;
    NarrationSegment segment;segment.text="Controlled transport only";segment.prepared=segment.text;
    std::promise<void> entered;auto entered_future=entered.get_future();
    std::future<bool> inference,writer,cancellation;
    ShutdownGuard cleanup{target.worker};
    inference=std::async(std::launch::async,[&]{return rejected([&]{
        (void)target.worker->prepare(segment,"es","ef_dora",target.directory,1,stop.get_token());});});
    eventually([&]{return observed(target.log,L"input_held");},"Child did not enter its deterministic non-reading state");
    Process process;process.capture(target.log);
    writer=std::async(std::launch::async,[&]{entered.set_value();return rejected([&]{
        (void)target.worker->math_editor("{\"action\":\"echo\",\"marker\":\""+std::string(1024*1024,'x')+"\"}");});});
    check(entered_future.wait_for(1s)==std::future_status::ready,"Concurrent pipe writer did not start");
    check(writer.wait_for(50ms)!=std::future_status::ready,"Non-reading child unexpectedly completed a large request");
    cancellation=std::async(std::launch::async,[&]{return stop.request_stop();});
    const auto started=std::chrono::steady_clock::now();target.worker->shutdown();
    check(inference.wait_for(1s)==std::future_status::ready&&inference.get(),"Shutdown left inference waiting for its reply");
    check(writer.wait_for(1s)==std::future_status::ready&&writer.get(),"Shutdown waited behind a blocked transport writer");
    check(cancellation.wait_for(1s)==std::future_status::ready,"Shutdown left a synchronous cancellation callback blocked");
    (void)cancellation.get();
    check(process.exited_by(started+2s)&&std::chrono::steady_clock::now()-started<2s,"Blocked-transport shutdown exceeded its bound or left a child alive");
    verify_terminal(*target.worker);
    std::cout<<"PASS real non-reading child and large blocked pipe writer are interrupted without a shutdown RPC or stuck stop callback\n";
}
void start_shutdown_race(const std::filesystem::path& root,const std::filesystem::path& python,
        const std::filesystem::path& base){
    Fixture unused(root,python,base/L"never-started");verify_terminal(*unused.worker);
    check(!std::filesystem::exists(unused.log),"Closing an unused service launched a child");
    for(int iteration=0;iteration<4;++iteration){
        Fixture target(root,python,base/(L"race-"+std::to_wstring(iteration)));
        std::latch ready(2);std::future<bool> request,closing;ShutdownGuard cleanup{target.worker};
        request=std::async(std::launch::async,[&]{ready.arrive_and_wait();
            return rejected([&]{(void)target.worker->curriculum_plan(R"({"id":"race"})",0,"es","ef_dora","es");});});
        closing=std::async(std::launch::async,[&]{ready.arrive_and_wait();target.worker->shutdown();return true;});
        check(closing.wait_for(2s)==std::future_status::ready&&closing.get(),"Start/close race blocked shutdown");
        check(request.wait_for(1s)==std::future_status::ready,"Start/close race stranded its first request");(void)request.get();
        verify_terminal(*target.worker);
    }
    std::cout<<"PASS unused/idempotent terminal shutdown and concurrent first-request/start races never restart a closed service\n";
}
}
int wmain(int argc,wchar_t** argv){
    try{
        ensure_winrt_apartment();check(argc==3,"Shutdown test needs configured Python and an existing build directory");
        const auto root=std::filesystem::path(build::source_root),python=std::filesystem::path(argv[1]),build_directory=std::filesystem::path(argv[2]);
        check(std::filesystem::is_regular_file(python)&&std::filesystem::is_directory(build_directory),"Shutdown test inputs are missing");
        const auto base=build_directory/(L"narration-shutdown-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        check(std::filesystem::create_directory(base),"Shutdown test requires a new output directory");
        coordinator_shutdown(root,python,base);blocked_transport_shutdown(root,python,base);start_shutdown_race(root,python,base);
        return 0;
    }catch(const winrt::hresult_error& error){std::cerr<<"FAIL shutdown: "<<winrt::to_string(error.message())<<'\n';return 1;
    }catch(const std::exception& error){std::cerr<<"FAIL shutdown: "<<error.what()<<'\n';return 1;}
}

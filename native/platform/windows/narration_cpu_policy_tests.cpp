#include "platform/windows/narration_cpu_policy.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
unsigned cores(KAFFINITY mask){return std::popcount(static_cast<unsigned long long>(mask));}
std::vector<GROUP_AFFINITY> threads(DWORD process) {
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0)};
    check(snapshot.value!=INVALID_HANDLE_VALUE,"Cannot inspect actual worker threads");
    THREADENTRY32 entry{};entry.dwSize=sizeof(entry);
    check(Thread32First(snapshot.value,&entry)!=FALSE,"Cannot enumerate actual worker threads");
    std::vector<GROUP_AFFINITY> result;
    do {if(entry.th32OwnerProcessID==process) {
        Handle thread{OpenThread(THREAD_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ThreadID)};
        GROUP_AFFINITY affinity{};
        check(thread.value&&GetThreadGroupAffinity(thread.value,&affinity),"Cannot inspect a persistent worker thread affinity");
        result.push_back(affinity);
    }} while(Thread32Next(snapshot.value,&entry));
    return result;
}
template<class Predicate> void eventually(Predicate predicate,const char* message) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    do {if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(10));} while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error(message);
}
int child(int argc,wchar_t** argv) {
    check(argc==4,"Invalid CPU worker fixture arguments");
    const HANDLE stop=reinterpret_cast<HANDLE>(std::stoull(argv[2]));
    const HANDLE expand=reinterpret_cast<HANDLE>(std::stoull(argv[3]));
    std::vector<std::jthread> pool;
    auto grow=[&]{for(int index=0;index<6;++index)pool.emplace_back([&]{while(WaitForSingleObject(stop,2)==WAIT_TIMEOUT){
        // Real threads, not fake CPU counters. They remain resident across both
        // scope transitions, just as reusable inference pools do.
        YieldProcessor();
    }});};
    grow();
    const HANDLE events[]{stop,expand};
    if(WaitForMultipleObjects(2,events,FALSE,15000)==WAIT_OBJECT_0+1)grow();
    WaitForSingleObject(stop,15000);
    return 0;
}
void scenario(bool inherited_restriction) {
    using namespace lgaed::windows;
    DWORD_PTR ui_before=0,system=0;
    check(GetProcessAffinityMask(GetCurrentProcess(),&ui_before,&system),"Cannot capture the UI test affinity");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle stop{CreateEventW(&security,TRUE,FALSE,nullptr)},expand{CreateEventW(&security,TRUE,FALSE,nullptr)};
    Handle job{CreateJobObjectW(nullptr,nullptr)};
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    check(job.value&&SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Cannot create the isolated CPU fixture job");
    wchar_t path[32768]{};check(GetModuleFileNameW(nullptr,path,32768)>0,"Cannot find the CPU fixture executable");
    auto command=std::wstring(L"\"")+path+L"\" --cpu-worker "+std::to_wstring(reinterpret_cast<std::uintptr_t>(stop.value))+L" "+std::to_wstring(reinterpret_cast<std::uintptr_t>(expand.value));
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION info{};
    check(CreateProcessW(path,command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&info),"Cannot launch the separate CPU fixture process");
    Handle process{info.hProcess},first_thread{info.hThread};
    check(AssignProcessToJobObject(job.value,process.value),"Cannot assign the CPU fixture to its own job");
    DWORD_PTR initial=0;
    check(GetProcessAffinityMask(process.value,&initial,&system),"Cannot capture the actual worker process affinity");
    if(inherited_restriction) {
        // Leave up to three eligible CPUs rather than assume dense IDs 0..N.
        DWORD_PTR subset=0;
        for(unsigned bit=0;bit<sizeof(DWORD_PTR)*8&&cores(subset)<3;++bit)if(initial&(DWORD_PTR{1}<<bit))subset|=DWORD_PTR{1}<<bit;
        check(SetProcessAffinityMask(process.value,subset),"Cannot prepare an inherited worker-only restriction");initial=subset;
    }
    NarrationCpuPolicy policy(process.value,job.value);
    check(policy.available_processors()>0&&policy.inference_threads(NarrationCpuClass::current_page)==policy.available_processors(),"Foreground capacity used an invented CPU count");
    check(policy.inference_threads(NarrationCpuClass::background)<=2,"Background inference requests more than two usable CPUs");
    check(ResumeThread(first_thread.value)!=static_cast<DWORD>(-1),"Cannot resume the CPU fixture");
    eventually([&]{return threads(info.dwProcessId).size()>=7;},"Resident worker pool never started");
    {
        NarrationCpuScope scope(policy,NarrationCpuClass::background);
        DWORD_PTR capped=0;
        check(GetProcessAffinityMask(process.value,&capped,&system)&&cores(capped)<=2&&(capped&initial)==capped,"Actual background process is not hard-limited to eligible two CPUs");
        const auto persistent=threads(info.dwProcessId);
        check(std::ranges::all_of(persistent,[&](const auto& value){return value.Mask==capped;}),"An existing persistent pool thread escaped the hard CPU mask");
        check(SetEvent(expand.value),"Cannot create new worker pool threads");
        eventually([&]{const auto values=threads(info.dwProcessId);return values.size()>=13&&std::ranges::all_of(values,[&](const auto& value){return value.Mask==capped;});},"A new pool thread escaped the background hard CPU mask");
        scope.finish();
    }
    DWORD_PTR restored=0,ui_after=0;
    check(GetProcessAffinityMask(process.value,&restored,&system)&&restored==initial,"Foreground did not restore the exact inherited worker eligibility");
    check(std::ranges::all_of(threads(info.dwProcessId),[&](const auto& value){return value.Mask==initial;}),"A persistent pool thread remained restricted after foreground restoration");
    check(GetProcessAffinityMask(GetCurrentProcess(),&ui_after,&system)&&ui_after==ui_before,"The native UI process affinity changed");
    // Exception unwinding uses the same restoration, without recreating pools.
    try {NarrationCpuScope scope(policy,NarrationCpuClass::background);throw 1;}catch(int){}
    check(GetProcessAffinityMask(process.value,&restored,&system)&&restored==initial,"Cancellation/exception left a background CPU restriction");
    check(SetEvent(stop.value)&&WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0,"Cannot shut down the isolated CPU fixture");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1&&std::wstring_view(argv[1])==L"--cpu-worker")return child(argc,argv);
        scenario(false);scenario(true);
        std::cout<<"PASS real worker process hard two-CPU scope, existing/new persistent pool threads, exact inherited restore, exception cleanup, native UI unchanged (not a throughput benchmark)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

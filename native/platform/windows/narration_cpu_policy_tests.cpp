#include "platform/windows/narration_cpu_policy.hpp"
#include <tlhelp32.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
unsigned cores(KAFFINITY mask){return std::popcount(static_cast<unsigned long long>(mask));}
GROUP_AFFINITY affinity(USHORT group,KAFFINITY mask) {GROUP_AFFINITY value{};value.Group=group;value.Mask=mask;return value;}
bool same_affinities(std::vector<GROUP_AFFINITY> a,std::vector<GROUP_AFFINITY> b) {
    const auto by_group=[](const auto& x,const auto& y){return x.Group<y.Group;};
    std::sort(a.begin(),a.end(),by_group);std::sort(b.begin(),b.end(),by_group);
    return a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.Group==y.Group&&x.Mask==y.Mask;});
}
class ApiFixture final:public lgaed::windows::narration_cpu_detail::CpuBinding {
public:
    std::vector<USHORT> groups{0,1};
    std::vector<GROUP_AFFINITY> active{affinity(0,~KAFFINITY{}),affinity(1,~KAFFINITY{})};
    std::vector<GROUP_AFFINITY> job=active,written;
    std::optional<std::vector<GROUP_AFFINITY>> eligible;
    KAFFINITY process_mask=~KAFFINITY{},written_process=0;
    unsigned process_reads=0,process_writes=0,group_writes=0,mask_writes=0;
    unsigned fail_mask_writes=0;
    bool fail_group_write=false;
    std::vector<USHORT> written_groups;
    std::vector<USHORT> process_groups() override {return groups;}
    std::vector<GROUP_AFFINITY> active_affinities() override {return active;}
    std::vector<GROUP_AFFINITY> job_affinities() override {return job;}
    KAFFINITY process_affinity() override {++process_reads;check(groups.size()==1,"Legacy primary-group mask queried for a multi-group process");return process_mask;}
    std::optional<std::vector<GROUP_AFFINITY>> cpu_set_eligibility() override {return eligible;}
    KAFFINITY background_processors(const GROUP_AFFINITY& target) override {
        auto bits=target.Mask,selected=KAFFINITY{};
        for(unsigned index=0;index<2&&bits;++index){const auto bit=bits&(~bits+1);selected|=bit;bits&=~bit;}
        return selected;
    }
    void set_process_affinity(KAFFINITY value) override {++process_writes;written_process=value;}
    void set_job_groups(const std::vector<USHORT>& value) override {
        ++group_writes;if(fail_group_write)throw std::runtime_error("Fixture group write failed");written_groups=value;
    }
    void set_job_affinities(const std::vector<GROUP_AFFINITY>& value) override {
        ++mask_writes;if(fail_mask_writes){--fail_mask_writes;throw std::runtime_error("Fixture mask write failed");}written=value;
    }
};
void api_decisions() {
    using namespace lgaed::windows;
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        NarrationCpuPolicy policy(std::move(link));
        check(policy.available_processors()==128&&api.process_reads==0,"A two-group topology was reduced to its primary mask");
        policy.apply(NarrationCpuClass::background);
        check(api.process_writes==0&&api.written.size()==1&&cores(api.written.front().Mask)==2,"Multi-group background did not use a hard job mask");
        policy.restore();
        check(same_affinities(api.written,api.active)&&api.written_groups==api.groups,"Multi-group restoration lost a group or mask");
    }
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        api.groups={2,7};api.active={affinity(2,0xff),affinity(7,~KAFFINITY{})};api.job.clear();
        api.eligible=std::vector{affinity(7,KAFFINITY{0x90})};
        NarrationCpuPolicy policy(std::move(link));
        check(policy.available_processors()==2&&api.process_reads==0,"Non-primary inherited CPU Sets were not respected");
        policy.apply(NarrationCpuClass::background);
        check(api.written.size()==1&&api.written.front().Group==7&&api.written.front().Mask==0x90,"Sparse group-relative background CPU IDs changed");
        policy.apply(NarrationCpuClass::current_page);
        check(same_affinities(api.written,api.active),"Empty job baseline was restored as a soft CPU Set restriction");
    }
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        api.job={affinity(1,KAFFINITY{1}<<63)};
        NarrationCpuPolicy policy(std::move(link));
        check(policy.available_processors()==1&&policy.inference_threads(NarrationCpuClass::background)==1,"A one-CPU inherited group requested two inference threads");
        policy.apply(NarrationCpuClass::background);policy.restore();
        check(same_affinities(api.written,api.job),"Restoration broadened an inherited hard job restriction");
    }
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        api.groups={0};api.active={affinity(0,0xff)};api.job=api.active;api.process_mask=0x54;
        NarrationCpuPolicy policy(std::move(link));
        check(api.process_reads==1&&policy.available_processors()==3,"Single-group inherited process restriction was lost");
        policy.apply(NarrationCpuClass::background);policy.restore();
        check(api.process_writes==2&&api.written_process==0x54&&api.mask_writes==0,"Single-group restoration changed its inherited process mask");
    }
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        NarrationCpuPolicy policy(std::move(link));
        api.fail_mask_writes=1;bool failed=false;
        try{policy.apply(NarrationCpuClass::background);}catch(const std::runtime_error&){failed=true;}
        check(failed&&api.group_writes==2&&same_affinities(api.written,api.active),"Partial job setter failure did not restore the full inherited domain");
        policy.apply(NarrationCpuClass::background);policy.restore();
        check(same_affinities(api.written,api.active),"A recovered job transition cannot be used again");
        api.fail_mask_writes=2;failed=false;
        try{policy.apply(NarrationCpuClass::background);}catch(const std::runtime_error& error){failed=std::string_view(error.what()).find("after a failed limit")!=std::string_view::npos;}
        check(failed,"A failed rollback was hidden from the caller");
        policy.restore();
        check(same_affinities(api.written,api.active),"A failed rollback lost the pending restoration state");
        policy.apply(NarrationCpuClass::background);api.fail_mask_writes=1;failed=false;
        try{policy.restore();}catch(const std::runtime_error&){failed=true;}
        check(failed,"An explicit restoration failure was hidden");
        const auto writes=api.mask_writes;policy.apply(NarrationCpuClass::background);
        check(api.mask_writes==writes+2&&api.written.size()==1&&cores(api.written.front().Mask)==2,"A partial restoration was mistaken for an enforced background cap");
        policy.restore();
    }
    {
        auto link=std::make_unique<ApiFixture>();auto& api=*link;
        NarrationCpuPolicy policy(std::move(link));policy.apply(NarrationCpuClass::background);
        api.active={affinity(1,0xff)};policy.restore();
        check(same_affinities(api.written,api.active),"Restoration re-enabled inactive CPUs or lost the surviving group");
    }
    {
        auto link=std::make_unique<ApiFixture>();link->eligible=std::vector<GROUP_AFFINITY>{};bool failed=false;
        try{NarrationCpuPolicy policy(std::move(link));}catch(const std::runtime_error&){failed=true;}
        check(failed,"An explicitly empty CPU Set eligibility was replaced by all host processors");
    }
    std::cout<<"PASS injected Win32 API decisions: 128 logical CPUs / two groups, sparse groups, empty job snapshot, hard/soft eligibility, single-group inheritance, partial setter/rollback failures, inactive CPUs (not physical >64-CPU validation)\n";
}
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
void scenario(bool inherited_restriction,bool force_job=false,bool nested_job=false) {
    using namespace lgaed::windows;
    const auto own_groups=[] {
        USHORT count=GetMaximumProcessorGroupCount();std::vector<USHORT> values(count);
        check(GetProcessGroupAffinity(GetCurrentProcess(),&count,values.data()),"Cannot inspect UI test groups");values.resize(count);return values;
    };
    const auto ui_groups=own_groups();
    DWORD_PTR ui_before=0,system=0;
    if(ui_groups.size()==1)check(GetProcessAffinityMask(GetCurrentProcess(),&ui_before,&system),"Cannot capture the UI test affinity");
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
    Handle ancestor;
    GROUP_AFFINITY ancestor_mask{};
    if(nested_job) {
        // This is a REAL nested job on the physical host, not a synthetic
        // second processor group. Use nonadjacent eligible logical CPU IDs.
        GROUP_AFFINITY inherited{};
        check(GetThreadGroupAffinity(first_thread.value,&inherited),"Cannot inspect inherited fixture primary group");
        ancestor_mask.Group=inherited.Group;
        for(unsigned bit=0;bit<sizeof(KAFFINITY)*8&&cores(ancestor_mask.Mask)<3;bit+=2)
            if(inherited.Mask&(KAFFINITY{1}<<bit))ancestor_mask.Mask|=KAFFINITY{1}<<bit;
        for(unsigned bit=0;bit<sizeof(KAFFINITY)*8&&cores(ancestor_mask.Mask)<3;++bit)
            if(inherited.Mask&(KAFFINITY{1}<<bit))ancestor_mask.Mask|=KAFFINITY{1}<<bit;
        check(ancestor_mask.Mask!=0,"No inherited CPUs for the ancestor job fixture");
        ancestor.value=CreateJobObjectW(nullptr,nullptr);
        check(ancestor.value&&SetInformationJobObject(ancestor.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Cannot create the fixture ancestor job");
        auto group=ancestor_mask.Group;
        check(SetInformationJobObject(ancestor.value,JobObjectGroupInformation,&group,sizeof(group))&&
            SetInformationJobObject(ancestor.value,JobObjectGroupInformationEx,&ancestor_mask,sizeof(ancestor_mask)),"Cannot set the ancestor's sparse hard affinity");
        check(AssignProcessToJobObject(ancestor.value,process.value),"Cannot assign the fixture to its ancestor job");
    }
    check(AssignProcessToJobObject(job.value,process.value),"Cannot assign the CPU fixture to its own job");
    auto binding=narration_cpu_detail::native_binding(process.value,job.value,force_job);
    const auto worker_groups=binding->process_groups();
    const bool single_group=worker_groups.size()==1;
    const bool job_backend=force_job||!single_group;
    DWORD_PTR initial=0;
    if(single_group)check(GetProcessAffinityMask(process.value,&initial,&system),"Cannot capture the actual worker process affinity");
    if(inherited_restriction) {
        // Leave up to three eligible CPUs rather than assume dense IDs 0..N.
        if(!single_group) {
            const auto masks=binding->active_affinities();
            const auto first=std::ranges::find_if(masks,[&](const auto& item){return std::ranges::find(worker_groups,item.Group)!=worker_groups.end();});
            check(first!=masks.end(),"No active inherited group for fixture restriction");initial=first->Mask;
        }
        DWORD_PTR subset=0;
        for(unsigned bit=0;bit<sizeof(DWORD_PTR)*8&&cores(subset)<3;++bit)if(initial&(DWORD_PTR{1}<<bit))subset|=DWORD_PTR{1}<<bit;
        if(single_group)check(SetProcessAffinityMask(process.value,subset),"Cannot prepare an inherited worker-only restriction");
        else {
            binding->set_job_groups({worker_groups.front()});binding->set_job_affinities({affinity(worker_groups.front(),subset)});
        }
        initial=subset;
    }
    auto original_hard=binding->active_affinities();
    const auto original_job=binding->job_affinities();
    std::erase_if(original_hard,[&](auto& item) {
        if(std::ranges::find(worker_groups,item.Group)==worker_groups.end())return true;
        if(single_group)item.Mask&=initial;
        if(!original_job.empty()){
            const auto mask=std::ranges::find_if(original_job,[&](const auto& value){return value.Group==item.Group;});
            item.Mask&=mask==original_job.end()?0:mask->Mask;
        }
        return !item.Mask;
    });
    if(nested_job)check(original_hard.size()==1&&original_hard.front().Group==ancestor_mask.Group&&
        original_hard.front().Mask==ancestor_mask.Mask,"The captured inherited domain ignored the real ancestor job's sparse mask");
    NarrationCpuPolicy policy(std::move(binding));
    check(policy.available_processors()>0&&policy.inference_threads(NarrationCpuClass::current_page)==policy.available_processors(),"Foreground capacity used an invented CPU count");
    check(policy.inference_threads(NarrationCpuClass::background)<=2,"Background inference requests more than two usable CPUs");
    check(ResumeThread(first_thread.value)!=static_cast<DWORD>(-1),"Cannot resume the CPU fixture");
    eventually([&]{return threads(info.dwProcessId).size()>=7;},"Resident worker pool never started");
    {
        NarrationCpuScope scope(policy,NarrationCpuClass::background);
        DWORD_PTR capped=0;
        USHORT capped_group=0;
        if(job_backend) {
            const auto masks=narration_cpu_detail::native_binding(process.value,job.value)->job_affinities();
            check(masks.size()==1,"Actual job hard CPU cap spans several groups");capped=masks.front().Mask;capped_group=masks.front().Group;
        } else check(GetProcessAffinityMask(process.value,&capped,&system),"Cannot inspect actual background process affinity");
        if(!job_backend)capped_group=worker_groups.front();
        const auto inherited=std::ranges::find_if(original_hard,[&](const auto& value){return value.Group==capped_group;});
        check(capped&&cores(capped)<=2&&inherited!=original_hard.end()&&(capped&inherited->Mask)==capped,"Actual background process is not hard-limited to eligible two CPUs");
        const auto persistent=threads(info.dwProcessId);
        check(std::ranges::all_of(persistent,[&](const auto& value){return value.Group==capped_group&&value.Mask==capped;}),"An existing persistent pool thread escaped the hard CPU mask");
        check(SetEvent(expand.value),"Cannot create new worker pool threads");
        eventually([&]{const auto values=threads(info.dwProcessId);return values.size()>=13&&std::ranges::all_of(values,[&](const auto& value){return value.Group==capped_group&&value.Mask==capped;});},"A new pool thread escaped the background hard CPU mask");
        scope.finish();
    }
    DWORD_PTR restored=0,ui_after=0;
    const auto check_restored=[&] {
        if(job_backend)check(same_affinities(narration_cpu_detail::native_binding(process.value,job.value)->job_affinities(),original_hard),"Foreground did not restore all inherited job groups/masks");
        else check(GetProcessAffinityMask(process.value,&restored,&system)&&restored==initial,"Foreground did not restore the exact inherited worker eligibility");
        // A single-group affinity is fully observable here. On multi-group
        // hosts GetThreadGroupAffinity exposes only the primary group, so the
        // group-aware job snapshot above is the authoritative OS assertion.
        if(single_group)check(std::ranges::all_of(threads(info.dwProcessId),[&](const auto& value){return value.Mask==initial;}),"A persistent pool thread remained restricted after foreground restoration");
    };
    check_restored();
    if(nested_job) {
        GROUP_AFFINITY unchanged{};DWORD bytes=0;
        check(QueryInformationJobObject(ancestor.value,JobObjectGroupInformationEx,&unchanged,sizeof(unchanged),&bytes)&&
            bytes==sizeof(unchanged)&&unchanged.Group==ancestor_mask.Group&&unchanged.Mask==ancestor_mask.Mask,"The child's CPU policy modified its ancestor's affinity");
    }
    check(own_groups()==ui_groups,"The native UI process group eligibility changed");
    if(ui_groups.size()==1)check(GetProcessAffinityMask(GetCurrentProcess(),&ui_after,&system)&&ui_after==ui_before,"The native UI process affinity changed");
    // Exception unwinding uses the same restoration, without recreating pools.
    try {NarrationCpuScope scope(policy,NarrationCpuClass::background);throw 1;}catch(int){}
    check_restored();
    check(SetEvent(stop.value)&&WaitForSingleObject(process.value,5000)==WAIT_OBJECT_0,"Cannot shut down the isolated CPU fixture");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc>1&&std::wstring_view(argv[1])==L"--cpu-worker")return child(argc,argv);
        api_decisions();
        std::cout<<"HOST actual processor groups="<<GetActiveProcessorGroupCount()<<", logical CPUs="<<GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)<<'\n';
        scenario(false);scenario(true);scenario(false,true);scenario(false,true,true);
        std::cout<<"PASS real process and JobObjectGroupInformationEx hard two-CPU scopes, sparse nested ancestor job, existing/new persistent pool threads, exact inherited restore, exception cleanup, native UI unchanged (not a throughput benchmark)\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}

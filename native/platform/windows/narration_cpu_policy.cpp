#include "platform/windows/narration_cpu_policy.hpp"
#include <algorithm>
#include <bit>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>

namespace lgaed::windows {
namespace {
[[noreturn]] void failed(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),operation);
}
std::vector<GROUP_AFFINITY> job_affinities(HANDLE job) {
    std::vector<GROUP_AFFINITY> result(GetMaximumProcessorGroupCount());
    DWORD bytes = 0;
    if (!QueryInformationJobObject(job,JobObjectGroupInformationEx,result.data(),
            static_cast<DWORD>(result.size()*sizeof(GROUP_AFFINITY)),&bytes))
        failed("Cannot query the worker job CPU groups");
    if (bytes%sizeof(GROUP_AFFINITY)) throw std::runtime_error("Invalid worker job CPU group data");
    result.resize(bytes/sizeof(GROUP_AFFINITY));
    std::erase_if(result,[](const auto& affinity){return affinity.Mask==0;});
    return result;
}
std::vector<GROUP_AFFINITY> process_affinities(HANDLE process) {
    USHORT count=GetMaximumProcessorGroupCount();
    std::vector<USHORT> groups(count);
    if (!GetProcessGroupAffinity(process,&count,groups.data())) failed("Cannot query the worker CPU groups");
    groups.resize(count);
    DWORD bytes=0;
    GetLogicalProcessorInformationEx(RelationGroup,nullptr,&bytes);
    if (GetLastError()!=ERROR_INSUFFICIENT_BUFFER) failed("Cannot size active processor group data");
    std::vector<std::byte> storage(bytes);
    auto* information=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
    if (!GetLogicalProcessorInformationEx(RelationGroup,information,&bytes)) failed("Cannot query active processor groups");
    std::vector<GROUP_AFFINITY> result;
    for (USHORT group:groups) {
        if (group>=information->Group.ActiveGroupCount) throw std::runtime_error("Worker belongs to an inactive processor group");
        GROUP_AFFINITY affinity{};affinity.Group=group;
        affinity.Mask=information->Group.GroupInfo[group].ActiveProcessorMask;
        if (affinity.Mask) result.push_back(affinity);
    }
    // This query also captures an affinity inherited from a restricted launcher
    // or its enclosing job. It is not equivalent to the machine's CPU count.
    DWORD_PTR process_mask=0,system_mask=0;
    if (!GetProcessAffinityMask(process,&process_mask,&system_mask)) failed("Cannot query inherited worker affinity");
    if (result.size()==1) result.front().Mask &= process_mask;
    // Preserve explicit Process Default CPU Set selections and OS reservations
    // too. They are soft constraints, not a substitute for our hard background
    // mask, but they still describe CPUs this process was intended to use.
    ULONG required_sets=0;
    if (!GetProcessDefaultCpuSets(process,nullptr,0,&required_sets)&&GetLastError()!=ERROR_INSUFFICIENT_BUFFER)
        failed("Cannot size the worker default CPU Sets");
    std::vector<ULONG> selected(required_sets);
    if (required_sets&&!GetProcessDefaultCpuSets(process,selected.data(),required_sets,&required_sets))
        failed("Cannot query the worker default CPU Sets");
    std::sort(selected.begin(),selected.end());
    ULONG cpu_bytes=0;
    if (!GetSystemCpuSetInformation(nullptr,0,&cpu_bytes,process,0)&&GetLastError()!=ERROR_INSUFFICIENT_BUFFER)
        failed("Cannot size worker CPU Set eligibility");
    std::vector<std::byte> cpu_storage(cpu_bytes);
    if (cpu_bytes&&!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(cpu_storage.data()),cpu_bytes,&cpu_bytes,process,0))
        failed("Cannot query worker CPU Set eligibility");
    std::vector<GROUP_AFFINITY> eligible;
    for (std::size_t offset=0;offset<cpu_bytes;) {
        const auto* item=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(cpu_storage.data()+offset);
        if (!item->Size||item->Size>cpu_bytes-offset) throw std::runtime_error("Invalid worker CPU Set data");
        if (item->Type==CpuSetInformation&&(!item->CpuSet.Allocated||item->CpuSet.AllocatedToTargetProcess)&&
            (selected.empty()||std::binary_search(selected.begin(),selected.end(),item->CpuSet.Id))) {
            auto found=std::ranges::find_if(eligible,[&](const auto& value){return value.Group==item->CpuSet.Group;});
            if (found==eligible.end()) {GROUP_AFFINITY value{};value.Group=item->CpuSet.Group;eligible.push_back(value);found=std::prev(eligible.end());}
            found->Mask|=KAFFINITY{1}<<item->CpuSet.LogicalProcessorIndex;
        }
        offset+=item->Size;
    }
    if (cpu_bytes||!selected.empty()) for (auto& value:result) {
        const auto ceiling=std::ranges::find_if(eligible,[&](const auto& item){return item.Group==value.Group;});
        value.Mask &= ceiling==eligible.end()?0:ceiling->Mask;
    }
    std::erase_if(result,[](const auto& value){return value.Mask==0;});
    return result;
}
unsigned processor_count(const std::vector<GROUP_AFFINITY>& values) {
    unsigned result=0;
    for (const auto& value:values) result+=std::popcount(static_cast<unsigned long long>(value.Mask));
    return result;
}
std::vector<GROUP_AFFINITY> intersection(std::vector<GROUP_AFFINITY> values,const std::vector<GROUP_AFFINITY>& ceilings) {
    if (!ceilings.empty()) for (auto& value:values) {
        const auto other=std::ranges::find_if(ceilings,[&](const auto& item){return item.Group==value.Group;});
        value.Mask &= other==ceilings.end()?0:other->Mask;
    }
    std::erase_if(values,[](const auto& value){return value.Mask==0;});
    return values;
}
void set_job_affinities(HANDLE job,const std::vector<GROUP_AFFINITY>& values) {
    std::vector<USHORT> groups;
    for (const auto& value:values) groups.push_back(value.Group);
    if (!SetInformationJobObject(job,JobObjectGroupInformation,groups.data(),static_cast<DWORD>(groups.size()*sizeof(USHORT))))
        failed("Cannot set the worker job processor groups");
    if (!SetInformationJobObject(job,JobObjectGroupInformationEx,const_cast<GROUP_AFFINITY*>(values.data()),
            static_cast<DWORD>(values.size()*sizeof(GROUP_AFFINITY)))) failed("Cannot set the worker job hard CPU affinity");
}
KAFFINITY two_processors(const GROUP_AFFINITY& target) {
    KAFFINITY mask=0;
    // Prefer different physical cores when SMT is present. Two logical CPUs
    // on one physical core would unnecessarily serialize background inference.
    DWORD bytes=0;
    GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes);
    if (GetLastError()!=ERROR_INSUFFICIENT_BUFFER) failed("Cannot size physical processor topology");
    std::vector<std::byte> storage(bytes);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
            reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data()),&bytes))
        failed("Cannot query physical processor topology");
    for (std::size_t offset=0;offset<bytes&&std::popcount(static_cast<unsigned long long>(mask))<2;) {
        const auto* item=reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(storage.data()+offset);
        if (!item->Size||item->Size>bytes-offset) throw std::runtime_error("Invalid physical processor topology");
        for (WORD index=0;index<item->Processor.GroupCount;++index) {
            const auto& group=item->Processor.GroupMask[index];
            const auto eligible=group.Group==target.Group?(group.Mask&target.Mask):0;
            if (eligible) {mask|=eligible&(~eligible+1);break;}
        }
        offset+=item->Size;
    }
    for (unsigned bit=0;bit<sizeof(KAFFINITY)*8&&std::popcount(static_cast<unsigned long long>(mask))<2;++bit)
        if (target.Mask&(KAFFINITY{1}<<bit)) mask|=KAFFINITY{1}<<bit;
    return mask;
}
}

struct NarrationCpuPolicy::Impl {
    HANDLE process,job;
    std::vector<GROUP_AFFINITY> available,original_job;
    DWORD_PTR original_process_mask=0;
    unsigned processors=0;
    bool changed=false,single_group=false;
    std::mutex mutex;
    Impl(HANDLE worker,HANDLE worker_job):process(worker),job(worker_job) {
        if (!process||process==GetCurrentProcess()||GetProcessId(process)==GetCurrentProcessId())
            throw std::invalid_argument("The narration CPU policy requires the separate worker process");
        BOOL assigned=FALSE;
        if (!job||!IsProcessInJob(process,job,&assigned)||!assigned)
            throw std::invalid_argument("The narration worker must belong to its private job");
        USHORT group_count=GetMaximumProcessorGroupCount();
        std::vector<USHORT> process_groups(group_count);
        if (!GetProcessGroupAffinity(process,&group_count,process_groups.data())) failed("Cannot capture inherited worker groups");
        single_group=group_count==1;
        original_job=job_affinities(job);
        available=intersection(process_affinities(process),original_job);
        DWORD_PTR system_mask=0;
        if (!GetProcessAffinityMask(process,&original_process_mask,&system_mask)) failed("Cannot capture the worker affinity");
        processors=processor_count(available);
        if (!processors) throw std::runtime_error("The worker has no usable inherited processors");
        // The job snapshot is the group-aware authority on multi-group hosts.
        // Never manufacture access to groups absent from this assigned job.
        if (!single_group&&original_job.empty())
            throw std::runtime_error("Cannot safely capture the worker job's multi-group affinity");
    }
    void restore_locked() {
        if (!changed) return;
        if (single_group) {
            DWORD_PTR now=0,active=0;
            if (!GetProcessAffinityMask(process,&now,&active)) failed("Cannot query active worker processors during restoration");
            const auto restored=original_process_mask&active;
            if (!restored) throw std::runtime_error("No inherited worker processors remain active");
            if (!SetProcessAffinityMask(process,restored)) failed("Cannot restore the worker CPU affinity");
        } else {
            set_job_affinities(job,original_job);
        }
        changed=false;
    }
};
NarrationCpuPolicy::NarrationCpuPolicy(HANDLE process,HANDLE job):impl_(std::make_unique<Impl>(process,job)) {}
NarrationCpuPolicy::~NarrationCpuPolicy() {try{restore();}catch(...) {}}
unsigned NarrationCpuPolicy::available_processors() const noexcept {return impl_->processors;}
unsigned NarrationCpuPolicy::inference_threads(NarrationCpuClass work) const noexcept {
    return work==NarrationCpuClass::background?std::min(2u,impl_->processors):impl_->processors;
}
std::vector<GROUP_AFFINITY> NarrationCpuPolicy::available_affinities() const {return impl_->available;}
void NarrationCpuPolicy::apply(NarrationCpuClass work) {
    const std::lock_guard lock(impl_->mutex);
    if (work==NarrationCpuClass::current_page) {impl_->restore_locked();return;}
    if (impl_->changed) return;
    // Choose two eligible logical processors in one group. A real hard mask is
    // used rather than CPU Sets, whose soft assignments can be ignored by OS.
    auto target=*std::ranges::max_element(impl_->available,{},[](const auto& item){return std::popcount(static_cast<unsigned long long>(item.Mask));});
    target.Mask=two_processors(target);
    if (impl_->single_group) {
        if (!SetProcessAffinityMask(impl_->process,target.Mask)) failed("Cannot enforce the background worker two-CPU limit");
    } else {
        try {set_job_affinities(impl_->job,{target});}
        catch (...) {try{set_job_affinities(impl_->job,impl_->original_job);}catch(...){} throw;}
    }
    impl_->changed=true;
}
void NarrationCpuPolicy::restore() {const std::lock_guard lock(impl_->mutex);impl_->restore_locked();}
NarrationCpuScope::NarrationCpuScope(NarrationCpuPolicy& policy,NarrationCpuClass work):policy_(&policy) {policy_->apply(work);}
NarrationCpuScope::~NarrationCpuScope() {try{finish();}catch(...) {}}
void NarrationCpuScope::finish() {if (policy_) {policy_->restore();policy_=nullptr;}}
}

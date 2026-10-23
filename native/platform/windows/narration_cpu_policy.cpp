#include "platform/windows/narration_cpu_policy.hpp"
#include <algorithm>
#include <bit>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>

namespace lgaed::windows {
namespace {
[[noreturn]] void failed(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),operation);
}
std::vector<GROUP_AFFINITY> read_job_affinities(HANDLE job) {
    std::vector<GROUP_AFFINITY> result(GetMaximumProcessorGroupCount());
    DWORD bytes = 0;
    if (!QueryInformationJobObject(job,JobObjectGroupInformationEx,result.data(),
            static_cast<DWORD>(result.size()*sizeof(GROUP_AFFINITY)),&bytes))
        failed("Cannot query the worker job CPU groups");
    if (bytes>result.size()*sizeof(GROUP_AFFINITY)||bytes%sizeof(GROUP_AFFINITY))
        throw std::runtime_error("Invalid worker job CPU group data");
    result.resize(bytes/sizeof(GROUP_AFFINITY));
    std::erase_if(result,[](const auto& affinity){return affinity.Mask==0;});
    return result;
}
std::vector<USHORT> read_process_groups(HANDLE process) {
    USHORT count=GetMaximumProcessorGroupCount();
    std::vector<USHORT> groups(count);
    if (!GetProcessGroupAffinity(process,&count,groups.data())) failed("Cannot query the worker CPU groups");
    if (!count||count>groups.size()) throw std::runtime_error("Invalid inherited worker CPU groups");
    groups.resize(count);
    return groups;
}
std::vector<GROUP_AFFINITY> read_active_affinities() {
    DWORD bytes=0;
    GetLogicalProcessorInformationEx(RelationGroup,nullptr,&bytes);
    if (GetLastError()!=ERROR_INSUFFICIENT_BUFFER) failed("Cannot size active processor group data");
    std::vector<std::byte> storage(bytes);
    auto* information=reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(storage.data());
    if (!GetLogicalProcessorInformationEx(RelationGroup,information,&bytes)) failed("Cannot query active processor groups");
    std::vector<GROUP_AFFINITY> result;
    for (USHORT group=0;group<information->Group.ActiveGroupCount;++group) {
        GROUP_AFFINITY affinity{};affinity.Group=group;
        affinity.Mask=information->Group.GroupInfo[group].ActiveProcessorMask;
        if (affinity.Mask) result.push_back(affinity);
    }
    return result;
}
std::optional<std::vector<GROUP_AFFINITY>> read_cpu_set_eligibility(HANDLE process) {
    // Preserve explicit Process Default CPU Set selections and OS reservations
    // too. They are soft constraints, not a substitute for our hard background
    // mask, but they still describe CPUs this process was intended to use.
    ULONG required_sets=0;
    if (!GetProcessDefaultCpuSets(process,nullptr,0,&required_sets)&&GetLastError()!=ERROR_INSUFFICIENT_BUFFER)
        failed("Cannot size the worker default CPU Sets");
    std::vector<ULONG> selected(required_sets);
    if (required_sets&&!GetProcessDefaultCpuSets(process,selected.data(),required_sets,&required_sets))
        failed("Cannot query the worker default CPU Sets");
    if (required_sets>selected.size()) throw std::runtime_error("Worker CPU Set selection changed during capture");
    selected.resize(required_sets);
    std::sort(selected.begin(),selected.end());
    ULONG cpu_bytes=0;
    if (!GetSystemCpuSetInformation(nullptr,0,&cpu_bytes,process,0)&&GetLastError()!=ERROR_INSUFFICIENT_BUFFER)
        failed("Cannot size worker CPU Set eligibility");
    std::vector<std::byte> cpu_storage(cpu_bytes);
    if (cpu_bytes&&!GetSystemCpuSetInformation(reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(cpu_storage.data()),cpu_bytes,&cpu_bytes,process,0))
        failed("Cannot query worker CPU Set eligibility");
    if (cpu_bytes>cpu_storage.size()) throw std::runtime_error("Worker CPU Set data changed during capture");
    std::vector<GROUP_AFFINITY> eligible;
    for (std::size_t offset=0;offset<cpu_bytes;) {
        const auto* item=reinterpret_cast<const SYSTEM_CPU_SET_INFORMATION*>(cpu_storage.data()+offset);
        if (!item->Size||item->Size>cpu_bytes-offset) throw std::runtime_error("Invalid worker CPU Set data");
        if (item->Type==CpuSetInformation&&(!item->CpuSet.Allocated||item->CpuSet.AllocatedToTargetProcess)&&
            (selected.empty()||std::binary_search(selected.begin(),selected.end(),item->CpuSet.Id))) {
            if (item->CpuSet.LogicalProcessorIndex>=sizeof(KAFFINITY)*8)
                throw std::runtime_error("Invalid group-relative CPU Set index");
            auto found=std::ranges::find_if(eligible,[&](const auto& value){return value.Group==item->CpuSet.Group;});
            if (found==eligible.end()) {GROUP_AFFINITY value{};value.Group=item->CpuSet.Group;eligible.push_back(value);found=std::prev(eligible.end());}
            found->Mask|=KAFFINITY{1}<<item->CpuSet.LogicalProcessorIndex;
        }
        offset+=item->Size;
    }
    if (!cpu_bytes&&selected.empty()) return std::nullopt;
    return eligible;
}
unsigned processor_count(const std::vector<GROUP_AFFINITY>& values) {
    unsigned result=0;
    for (const auto& value:values) result+=std::popcount(static_cast<unsigned long long>(value.Mask));
    return result;
}
std::vector<GROUP_AFFINITY> intersection(std::vector<GROUP_AFFINITY> values,const std::vector<GROUP_AFFINITY>& ceilings) {
    for (auto& value:values) {
        const auto other=std::ranges::find_if(ceilings,[&](const auto& item){return item.Group==value.Group;});
        value.Mask &= other==ceilings.end()?0:other->Mask;
    }
    std::erase_if(values,[](const auto& value){return value.Mask==0;});
    return values;
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
class NativeCpuBinding final:public narration_cpu_detail::CpuBinding {
public:
    NativeCpuBinding(HANDLE process,HANDLE job,bool job_only):process_(process),job_(job),job_only_(job_only) {
        if (!process||process==GetCurrentProcess()||GetProcessId(process)==GetCurrentProcessId())
            throw std::invalid_argument("The narration CPU policy requires the separate worker process");
        BOOL assigned=FALSE;
        if (!job||!IsProcessInJob(process,job,&assigned)||!assigned)
            throw std::invalid_argument("The narration worker must belong to its private job");
    }
    std::vector<USHORT> process_groups() override {return read_process_groups(process_);}
    std::vector<GROUP_AFFINITY> active_affinities() override {return read_active_affinities();}
    std::vector<GROUP_AFFINITY> job_affinities() override {return read_job_affinities(job_);}
    KAFFINITY process_affinity() override {
        DWORD_PTR process_mask=0,system_mask=0;
        if (!GetProcessAffinityMask(process_,&process_mask,&system_mask)) failed("Cannot capture inherited worker affinity");
        return process_mask&system_mask;
    }
    std::optional<std::vector<GROUP_AFFINITY>> cpu_set_eligibility() override {return read_cpu_set_eligibility(process_);}
    KAFFINITY background_processors(const GROUP_AFFINITY& value) override {return two_processors(value);}
    void set_process_affinity(KAFFINITY value) override {
        if (!SetProcessAffinityMask(process_,value)) failed("Cannot set the worker hard CPU affinity");
    }
    void set_job_groups(const std::vector<USHORT>& groups) override {
        if (!SetInformationJobObject(job_,JobObjectGroupInformation,const_cast<USHORT*>(groups.data()),
                static_cast<DWORD>(groups.size()*sizeof(USHORT)))) failed("Cannot set the worker job processor groups");
    }
    void set_job_affinities(const std::vector<GROUP_AFFINITY>& values) override {
        if (!SetInformationJobObject(job_,JobObjectGroupInformationEx,const_cast<GROUP_AFFINITY*>(values.data()),
                static_cast<DWORD>(values.size()*sizeof(GROUP_AFFINITY)))) failed("Cannot set the worker job hard CPU affinity");
    }
    bool job_only() const noexcept override {return job_only_;}
private:
    HANDLE process_,job_;
    bool job_only_;
};
}

std::unique_ptr<narration_cpu_detail::CpuBinding> narration_cpu_detail::native_binding(HANDLE process,HANDLE job,bool job_only) {
    return std::make_unique<NativeCpuBinding>(process,job,job_only);
}

struct NarrationCpuPolicy::Impl {
    std::unique_ptr<narration_cpu_detail::CpuBinding> binding;
    std::vector<GROUP_AFFINITY> available,original_hard;
    GROUP_AFFINITY background{};
    unsigned processors=0,background_threads=0;
    bool changed=false,background_applied=false,use_job=false;
    std::mutex mutex;
    explicit Impl(std::unique_ptr<narration_cpu_detail::CpuBinding> value):binding(std::move(value)) {
        if (!binding) throw std::invalid_argument("The narration CPU policy needs an affinity binding");
        auto groups=binding->process_groups();
        std::sort(groups.begin(),groups.end());
        if (groups.empty()||std::adjacent_find(groups.begin(),groups.end())!=groups.end())
            throw std::runtime_error("Invalid inherited worker CPU groups");
        original_hard=binding->active_affinities();
        std::erase_if(original_hard,[&](const auto& item){return !item.Mask||!std::binary_search(groups.begin(),groups.end(),item.Group);});
        if (original_hard.size()!=groups.size()) throw std::runtime_error("Worker belongs to an inactive processor group");
        // GetProcessAffinityMask describes only one primary group, and can
        // legitimately return zero for a multi-group process. Never query it
        // as the authority for a process spanning groups (Microsoft Win32).
        if (groups.size()==1) original_hard.front().Mask &= binding->process_affinity();
        const auto original_job=binding->job_affinities();
        if (!original_job.empty()) original_hard=intersection(original_hard,original_job);
        // An empty job snapshot has no explicit group ceiling. Its restoration
        // domain is the captured inherited hard eligibility, NOT CPU Sets and
        // not every processor on the host. No empty-list setter is required.
        std::erase_if(original_hard,[](const auto& item){return !item.Mask;});
        available=original_hard;
        if (const auto eligible=binding->cpu_set_eligibility()) available=intersection(available,*eligible);
        processors=processor_count(available);
        if (!processors) throw std::runtime_error("The worker has no usable inherited processors");
        use_job=groups.size()>1||binding->job_only();
        background=*std::ranges::max_element(available,{},[](const auto& item){return std::popcount(static_cast<unsigned long long>(item.Mask));});
        background.Mask=binding->background_processors(background);
        const auto source=std::ranges::find_if(available,[&](const auto& item){return item.Group==background.Group;});
        background_threads=processor_count({background});
        if (!background_threads||background_threads>2||(background.Mask&source->Mask)!=background.Mask)
            throw std::runtime_error("Invalid background worker hard CPU selection");
    }
    void set_job(const std::vector<GROUP_AFFINITY>& values) {
        std::vector<USHORT> groups;
        for (const auto& value:values) groups.push_back(value.Group);
        binding->set_job_groups(groups);
        binding->set_job_affinities(values);
    }
    void restore_locked() {
        if (!changed) return;
        // A failed restoration may already have changed the job's group list.
        // Do not subsequently treat that partial state as an enforced cap.
        background_applied=false;
        const auto restored=intersection(original_hard,binding->active_affinities());
        if (restored.empty()) throw std::runtime_error("No inherited worker processors remain active");
        if (use_job) set_job(restored);
        else binding->set_process_affinity(restored.front().Mask);
        changed=false;
    }
};
NarrationCpuPolicy::NarrationCpuPolicy(HANDLE process,HANDLE job):NarrationCpuPolicy(narration_cpu_detail::native_binding(process,job)) {}
NarrationCpuPolicy::NarrationCpuPolicy(std::unique_ptr<narration_cpu_detail::CpuBinding> binding):impl_(std::make_unique<Impl>(std::move(binding))) {}
NarrationCpuPolicy::~NarrationCpuPolicy() {try{restore();}catch(...) {}}
unsigned NarrationCpuPolicy::available_processors() const noexcept {return impl_->processors;}
unsigned NarrationCpuPolicy::inference_threads(NarrationCpuClass work) const noexcept {
    return work==NarrationCpuClass::background?impl_->background_threads:impl_->processors;
}
std::vector<GROUP_AFFINITY> NarrationCpuPolicy::available_affinities() const {return impl_->available;}
void NarrationCpuPolicy::apply(NarrationCpuClass work) {
    const std::lock_guard lock(impl_->mutex);
    if (work==NarrationCpuClass::current_page) {impl_->restore_locked();return;}
    if (impl_->background_applied) return;
    // A previous partial setter/rollback failure must first be repaired. A
    // dirty policy can never be mistaken for a successfully enforced cap.
    if (impl_->changed) impl_->restore_locked();
    impl_->changed=true;
    try {
        if (impl_->use_job) impl_->set_job({impl_->background});
        else impl_->binding->set_process_affinity(impl_->background.Mask);
        impl_->background_applied=true;
    } catch (...) {
        const auto original=std::current_exception();
        try {impl_->restore_locked();}
        catch (const std::exception& rollback) {
            // Retain changed=true so explicit restore/destruction can retry;
            // callers receive the failed rollback instead of running uncapped.
            throw std::runtime_error(std::string("Cannot restore worker CPU affinity after a failed limit: ")+rollback.what());
        }
        std::rethrow_exception(original);
    }
}
void NarrationCpuPolicy::restore() {const std::lock_guard lock(impl_->mutex);impl_->restore_locked();}
NarrationCpuScope::NarrationCpuScope(NarrationCpuPolicy& policy,NarrationCpuClass work):policy_(&policy) {policy_->apply(work);}
NarrationCpuScope::~NarrationCpuScope() {try{finish();}catch(...) {}}
void NarrationCpuScope::finish() {if (policy_) {policy_->restore();policy_=nullptr;}}
}

#pragma once
#include <windows.h>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace lgaed::windows {
enum class NarrationCpuClass { current_page, background };

namespace narration_cpu_detail {
// The production binding uses Win32 hard-affinity APIs. Keeping the topology
// reads and writes behind one small boundary also permits explicit multi-group
// decision/failure tests on a machine that physically has only one group.
class CpuBinding {
public:
    virtual ~CpuBinding() = default;
    virtual std::vector<USHORT> process_groups() = 0;
    virtual std::vector<GROUP_AFFINITY> active_affinities() = 0;
    virtual std::vector<GROUP_AFFINITY> job_affinities() = 0;
    virtual KAFFINITY process_affinity() = 0;
    virtual std::optional<std::vector<GROUP_AFFINITY>> cpu_set_eligibility() = 0;
    virtual KAFFINITY background_processors(const GROUP_AFFINITY&) = 0;
    virtual void set_process_affinity(KAFFINITY) = 0;
    virtual void set_job_groups(const std::vector<USHORT>&) = 0;
    virtual void set_job_affinities(const std::vector<GROUP_AFFINITY>&) = 0;
    virtual bool job_only() const noexcept { return false; }
};
// job_only exercises the group-aware OS path even on a single-group host; it
// does not manufacture processors or change the machine topology.
std::unique_ptr<CpuBinding> native_binding(HANDLE process, HANDLE job, bool job_only = false);
}

// A hard scheduler boundary for the dedicated JVM process, never the native UI.
// Construct after assigning the suspended worker to its private job, before it
// runs. Both borrowed handles must outlive this object. The caller serializes
// inference and releases a scope only after completion/cancellation is acked.
class NarrationCpuPolicy {
public:
    NarrationCpuPolicy(HANDLE worker_process, HANDLE worker_job);
    explicit NarrationCpuPolicy(std::unique_ptr<narration_cpu_detail::CpuBinding>);
    ~NarrationCpuPolicy();
    NarrationCpuPolicy(const NarrationCpuPolicy&) = delete;
    NarrationCpuPolicy& operator=(const NarrationCpuPolicy&) = delete;

    [[nodiscard]] unsigned available_processors() const noexcept;
    [[nodiscard]] unsigned inference_threads(NarrationCpuClass) const noexcept;
    [[nodiscard]] std::vector<GROUP_AFFINITY> available_affinities() const;
    // Background is <=2 logical processors, including existing/new ONNX/G2P
    // pool threads. Foreground restores the captured inherited eligibility.
    // No provider/session/priority/CPU-rate changes are made here.
    void apply(NarrationCpuClass);
    void restore();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class NarrationCpuScope {
public:
    NarrationCpuScope(NarrationCpuPolicy& policy, NarrationCpuClass work);
    ~NarrationCpuScope();
    NarrationCpuScope(const NarrationCpuScope&) = delete;
    NarrationCpuScope& operator=(const NarrationCpuScope&) = delete;
    // Call at the acknowledged end of a request to surface restoration errors.
    // The destructor is an additional best-effort guard during exception unwind.
    void finish();
private:
    NarrationCpuPolicy* policy_;
};
}

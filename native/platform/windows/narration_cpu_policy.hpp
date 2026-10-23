#pragma once
#include <windows.h>
#include <cstddef>
#include <memory>
#include <vector>

namespace lgaed::windows {
enum class NarrationCpuClass { current_page, background };

// A hard scheduler boundary for the dedicated JVM process, never the native UI.
// Construct after assigning the suspended worker to its private job, before it
// runs. Both borrowed handles must outlive this object. The caller serializes
// inference and releases a scope only after completion/cancellation is acked.
class NarrationCpuPolicy {
public:
    NarrationCpuPolicy(HANDLE worker_process, HANDLE worker_job);
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

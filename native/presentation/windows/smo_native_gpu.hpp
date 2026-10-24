#pragma once
#include "core/smo_native.hpp"
#include "presentation/windows/smo_compute.hpp"
#include <functional>

namespace lgaed::presentation {
enum class SmoNativePreparationStage {source_generation,kernel_creation};
struct SmoNativePreparationEvent {
    smo::NativeAction action=smo::NativeAction::reset;
    SmoNativePreparationStage stage=SmoNativePreparationStage::source_generation;
    bool beginning=true;
    // Host wall time of the operation only, valid on its completion event.
    // The observer's own time is excluded; this is never a GPU timestamp.
    double wall_ms=0;
};
struct SmoNativeGpuOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device{true,{}};
    std::uint32_t items=1;
    smo::NativeScalarProfile scalar_profile=smo::NativeScalarProfile::ordinary_i32;
    // Optional initialization observer. Empty by default: no preparation
    // clocks, events or extra kernel compilation occur on the normal path.
    std::function<void(const SmoNativePreparationEvent&)> preparation;
};
struct SmoNativeGpuExecution {
    smo::NativeAction action=smo::NativeAction::reset;
    bool committed=false;
    std::uint64_t revision=0;
    double gpu_ms=0,wall_ms=0,status_readback_ms=0;
    std::vector<smo::NativeDiagnostic> diagnostics;
};
struct SmoNativeGpuSnapshot {
    std::uint64_t revision=0;
    std::vector<smo::NativeFrame> items;
};
struct SmoNativeGpuRealSnapshot {
    std::uint64_t revision=0;
    std::vector<smo::NativeRealFrame32> items;
};
struct SmoNativeGpuFrameView {
    SmoBuffer vertices=0,faces=0,frame=0;
    std::uint32_t vertex_word_offset=0,face_word_offset=0,frame_word_offset=0;
    std::uint32_t vertex_count=0,face_count=0;
    smo::NativeScalarProfile scalar_profile=smo::NativeScalarProfile::ordinary_i32;
    std::uint64_t revision=0;
};
// All three outputs are lowered from the same authored SSA graph. SPIR-V is
// generated directly in memory: no SDK, external process, intermediate file,
// opcode buffer or CPU runtime arithmetic evaluation is required. binary32
// explicitly uses hardware floating quantities; I32 remains integer-only.
[[nodiscard]] SmoKernelSource smo_native_kernel(const smo::NativeProgram&,smo::NativeAction);
class SmoNativeGpuProgram {
public:
    [[nodiscard]] static SmoNativeGpuProgram compile(std::string_view,const SmoNativeGpuOptions& = {},std::stop_token = {},smo::Notation = {});
    // Also accepts an independently authored control graph for GPU tests.
    [[nodiscard]] static SmoNativeGpuProgram from_program(smo::NativeProgram,const SmoNativeGpuOptions& = {},std::stop_token = {});
    SmoNativeGpuProgram(SmoNativeGpuProgram&&) noexcept;
    SmoNativeGpuProgram& operator=(SmoNativeGpuProgram&&) noexcept;
    ~SmoNativeGpuProgram();
    SmoNativeGpuProgram(const SmoNativeGpuProgram&)=delete;
    SmoNativeGpuProgram& operator=(const SmoNativeGpuProgram&)=delete;
    [[nodiscard]] SmoNativeGpuExecution reset(bool timed=true);
    [[nodiscard]] SmoNativeGpuExecution frame(bool timed=true);
    [[nodiscard]] SmoNativeGpuExecution step(std::span<const std::int32_t> dt,bool timed=true);
    [[nodiscard]] SmoNativeGpuExecution step_binary32(std::span<const std::uint32_t> dt_bits,bool timed=true);
    [[nodiscard]] SmoNativeGpuExecution step_elapsed(std::span<const std::uint64_t> nanoseconds,bool timed=true);
    // Explicit runtime inputs, item-major/state-slot-major. Upload is outside
    // action GPU timings and does not evaluate any expression on the CPU.
    void seed_states(std::span<const std::int32_t> values);
    void seed_binary32_states(std::span<const std::uint32_t> bits);
    [[nodiscard]] SmoNativeGpuSnapshot inspect()const;
    [[nodiscard]] SmoNativeGpuRealSnapshot inspect_binary32()const;
    [[nodiscard]] SmoNativeGpuFrameView frame_view(std::uint32_t item=0)const;
    [[nodiscard]] SmoCompute& compute(); //borrowed; renderer must die first
    [[nodiscard]] const smo::NativeProgram& program()const;
    [[nodiscard]] SmoComputeInfo info()const;
    [[nodiscard]] std::uint32_t item_count()const;
    [[nodiscard]] std::uint64_t revision()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
    explicit SmoNativeGpuProgram(std::unique_ptr<Impl>);
};
} //namespace lgaed::presentation

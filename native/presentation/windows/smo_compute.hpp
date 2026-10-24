#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::presentation {
enum class SmoComputeApi { direct3d11, direct3d12, vulkan, opengl };
enum class SmoComputePreparationStage {
    context_creation,device_configuration,shader_compilation,program_linking,subroutine_bindings
};
struct SmoComputePreparationEvent {
    SmoComputePreparationStage stage=SmoComputePreparationStage::context_creation;
    std::string kernel_name;
    std::size_t source_bytes=0;
    // Host wall time of the named preparation phase, never a GPU timestamp.
    // The observer is called synchronously after successful completion; its
    // own duration is excluded. Empty observers do not add clocks/events.
    double wall_ms=0;
};
struct SmoComputeOptions {
    bool software=false;
    std::string adapter_name;
    // Optional OpenGL cold/warm preparation evidence. It does not alter source,
    // optimization flags or driver caches; other backends retain their path.
    std::function<void(const SmoComputePreparationEvent&)> preparation;
};
struct SmoComputeInfo {
    std::string api,adapter,driver;
    bool software=false,timestamps=false;
    std::uint32_t vendor_id=0,device_id=0;
};
using SmoBuffer=std::uint32_t;
using SmoKernel=std::uint32_t;
struct SmoGlSubroutineBinding {std::string uniform,function;};
struct SmoKernelSource {
    std::string name,hlsl,glsl;
    std::vector<std::uint32_t> spirv;
    // Optional author-time DXBC, with the same strict IEEE/O3 settings as the
    // runtime compiler. Keep the existing API-specific shader models separate.
    std::vector<std::byte> dxbc;   // cs_5_0: Direct3D11
    std::vector<std::byte> dxbc51; // cs_5_1: Direct3D12
    // Explicitly opt in only after verifying that this cs_5_0 artifact uses
    // the shared b0/u0..u7, space0 ABI supported by the Direct3D12 root.
    // The artifact keeps its real shader model; cs_5_1 always takes priority.
    bool dxbc50_d3d12_compatible=false;
    // Explicit GLSL subroutine bindings. Inactive uniforms are ignored; every
    // active compute-stage location must have a named, valid implementation.
    std::vector<SmoGlSubroutineBinding> gl_subroutines;
};
struct SmoDispatch {
    SmoKernel kernel=0;
    // All eight bindings are read/write raw uint buffers, u0..u7 in HLSL,
    // storage bindings0..7 in GLSL/SPIR-V. ID0 means an unused dummy buffer.
    std::array<SmoBuffer,8> buffers{};
    std::array<std::uint32_t,16> constants{};
    std::array<std::uint32_t,3> groups{1,1,1};
};
// One synchronous, cancellable GPU executor; no rendering loop, sleep-based
// timing, state evolution or implicit math is owned by an API adapter.
class SmoCompute {
public:
    virtual ~SmoCompute()=default;
    [[nodiscard]] virtual SmoComputeInfo info()const=0;
    [[nodiscard]] virtual SmoBuffer buffer(std::size_t bytes,std::span<const std::byte> initial={})=0;
    [[nodiscard]] virtual SmoKernel kernel(const SmoKernelSource&)=0;
    virtual void write(SmoBuffer,std::span<const std::byte>)=0;
    [[nodiscard]] virtual std::vector<std::byte> read(SmoBuffer)=0;
    // Submit every dispatch with correct UAV/storage ordering. If timed=true,
    // return GPU milliseconds covering the entire sequence, including passes.
    // Unsupported/invalid GPU clocks return NaN, never a CPU-time substitute.
    [[nodiscard]] virtual double execute(std::span<const SmoDispatch>,bool timed=true)=0;
};
[[nodiscard]] std::unique_ptr<SmoCompute> create_smo_d3d11_compute(const SmoComputeOptions&,std::stop_token={});
[[nodiscard]] std::unique_ptr<SmoCompute> create_smo_d3d12_compute(const SmoComputeOptions&,std::stop_token={});
[[nodiscard]] std::unique_ptr<SmoCompute> create_smo_vulkan_compute(const SmoComputeOptions&,std::stop_token={});
[[nodiscard]] std::unique_ptr<SmoCompute> create_smo_opengl_compute(const SmoComputeOptions&,std::stop_token={});
}

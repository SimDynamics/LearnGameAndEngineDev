#pragma once
#include "core/smo.hpp"
#include "presentation/windows/smo_compute.hpp"
#include <functional>

namespace lgaed::presentation {
// Experimental deferred channel contract, deliberately separate from multiply
// or divide-by-zero: an inactive optional statement has no observable value,
// alternative, error or destination write. All preceding statements still run.
// The source compiler rejects effects/unsupported instructions and references
// to the optional destination after its guarded assignment.
struct SmoLazyTaskOptions {
    SmoComputeApi api=SmoComputeApi::direct3d11;
    SmoComputeOptions device{true,{}};
    std::uint32_t items=4096,active_percent=50,rounds=9,frames=4,warmups=2;
    bool grouped=true;
    std::string source; //empty chooses the real SMO sample below
};
struct SmoLazyTaskVariant {
    std::string name;
    // Full frame = clear/update/project + image readback. State/counter QA
    // readbacks are outside this interval. Conversion is entirely on GPU;
    // conversion_ms records CPU conversion cost (zero in this pipeline).
    double gpu_ms=0,frame_gpu_ms=0,frame_ms=0,readback_ms=0,conversion_ms=0;
    std::vector<double> gpu_samples,frame_gpu_samples,frame_samples;
    // Logical source-IR operations, not an ISA instruction/performance count.
    std::uint64_t output_hash=0,image_hash=0,executed_instructions=0,writes=0;
    bool verified=false,timing_valid=false;
};
struct SmoLazyTaskReport {
    SmoComputeInfo device;
    std::string source,source_hash,contract;
    std::uint32_t items=0,active_items=0,frames=0,optional_instructions=0,required_instructions=0;
    bool grouped=false,equivalent=false;
    double preparation_ms=0,source_compile_ms=0,shader_prepare_ms=0,upload_ms=0,validation_ms=0,warmup_ms=0;
    std::uint64_t buffer_bytes=0,process_peak_working_set=0,verified_words=0;
    std::array<SmoLazyTaskVariant,3> variants;
    std::uint32_t width=320,height=200;
    std::vector<std::uint32_t> preview_bgra;
};
// Uses the existing SMO parser. Every literal is radix12; no source expression
// is evaluated by a CPU interpreter to produce GPU state updates.
[[nodiscard]] std::string smo_lazy_task_source();
[[nodiscard]] SmoLazyTaskReport run_smo_lazy_task(const SmoLazyTaskOptions& = {},std::stop_token = {},
    std::function<void(unsigned)> progress={});
[[nodiscard]] SmoKernelSource smo_lazy_task_kernel(unsigned mode);
// CPU-only compiler/contract tests; GPU tests add exact independent checking.
[[nodiscard]] std::size_t smo_lazy_task_contract_checks();
}

#pragma once
#include "core/smo.hpp"
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_float12.hpp"

namespace lgaed::presentation {
inline constexpr std::uint32_t smo_vm_result_words=13,smo_vm_vertex_words=18,smo_vm_face_words=3,smo_vm_frame_words=40;
inline constexpr std::uint32_t smo_vm_prepare=19,smo_vm_finalize=20,smo_vm_elapsed=21,smo_vm_kernel_count=22;
// Each successful transaction selects a new GPU bank. No coordinate, rotation,
// color, arithmetic state or expression result is converted to host FP32/FP64.
// Frame bank: rotation[3] at0, color[3] at18 (six uint words per F12), then
// vertex count36, face count37, draw-request38, accumulated diagnostics39.
struct SmoGpuFrame {
    SmoBuffer vertices=0,faces=0,frame=0;
    std::uint32_t vertex_word_offset=0,face_word_offset=0,frame_word_offset=0;
    std::uint32_t vertex_count=0,face_count=0,diagnostic=0;
    bool draw_requested=false;
    std::uint64_t revision=0;
    bool operator==(const SmoGpuFrame&)const=default;
};
// Root's shader factory supplies opcode-specific sources/assets. Opcodes0..18
// follow smo::IrOpcode.19 prepares a work bank;20 validates it;21 converts an
// elapsed uint64 nanosecond count to F12 dt ENTIRELY ON THE GPU.
[[nodiscard]] SmoKernelSource smo_program_vm_kernel(unsigned opcode);
class SmoGpuProgram {
public:
    // Experimental profiles are explicit per program. Profile0 preserves the
    // current rules; the ordinary laboratory does not opt in to variants.
    [[nodiscard]] static SmoGpuProgram compile(std::string_view,SmoComputeApi,const SmoComputeOptions& = {},std::stop_token = {},smo::Notation = {},std::uint32_t rule_profile=0);
    SmoGpuProgram(SmoGpuProgram&&) noexcept;
    SmoGpuProgram& operator=(SmoGpuProgram&&) noexcept;
    ~SmoGpuProgram();
    SmoGpuProgram(const SmoGpuProgram&)=delete;
    SmoGpuProgram& operator=(const SmoGpuProgram&)=delete;
    void reset();
    [[nodiscard]] SmoGpuFrame frame(); //draw only
    [[nodiscard]] SmoGpuFrame step(SmoF12 dt); //step(0) still executes update
    [[nodiscard]] SmoGpuFrame step_elapsed(std::uint64_t nanoseconds);
    [[nodiscard]] SmoGpuFrame current_frame()const;
    [[nodiscard]] SmoF12Result variableExact(std::string_view)const;
    [[nodiscard]] SmoCompute& compute(); //same owner thread, e.g. GPU renderer
    [[nodiscard]] SmoComputeInfo info()const;
    [[nodiscard]] const smo::ProgramIr& ir()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit SmoGpuProgram(std::unique_ptr<Impl>);
};
// VM bindings: b0 literals(F12/6words), b1 states(two banks/13words each),
// b2 stack(13words each), b3 vertices(two banks), b4 faces(two banks),
// b5 frames(two banks/40words each), b6 control32words, b7 unused.
// Control: error0, numeric diagnostic1, line2,column3,pc4,stack-size5;
// dt16..21,elapsed-low22,elapsed-high23. Only bank-success changes visibility.
// Constants: operand0,line1,column2,pc3,work-bank4,old-bank5,state-count6,
// stack-capacity7,vertex-capacity8,face-capacity9,literal-count10,reset11,
// updating12,rule-profile13. Timing is host ns only; opcode21 converts both
// words to one exact Wide integer before a single F12 normalization on GPU.
} //namespace lgaed::presentation

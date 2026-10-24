#pragma once
#include "core/smo.hpp"
#include <span>
#include <limits>

namespace lgaed::smo {
// Explicit native profiles, separate from the F12 SMO rules. Existing APIs
// retain ordinary_i32 by default. Quantities occupy one uint32 transport word:
// signed-int bits in I32, IEEE754 bits in ordinary_binary32. No digit packing
// or runtime radix-emulation is involved in either native profile.
inline constexpr std::uint32_t native_no_value=0xffffffffu,native_frame_words=10u,native_status_words=4u;
enum class NativeAction:std::uint32_t {reset=0,frame=1,step=2};
enum class NativeOpcode:std::uint32_t {
    constant=0,load_state=1,load_dt=2,positive=3,negate=4,add=5,subtract=6,
    multiply=7,divide=8,power=9,store_state=10,vertex=11,face=12,
    rotation=13,color=14,draw=15
};
// load_state reads the immutable PREVIOUS committed bank. Later reads of an
// authored variable use its current SSA value ID, not a mutable-memory reload.
// This preserves the graph's meaning independently of store instruction order.
enum class NativeScalarProfile:std::uint32_t {ordinary_i32=0,ordinary_binary32=1};
enum class NativeError:std::uint32_t {none=0,overflow=1,division_zero=2,fraction=3,exponent=4,index=5,color=6,geometry=7,time=8,nonfinite=9,underflow=10};
struct NativeInstruction {
    NativeOpcode opcode=NativeOpcode::constant;
    std::uint32_t result=native_no_value;
    std::array<std::uint32_t,3> arguments{native_no_value,native_no_value,native_no_value};
    // Constant: exact scalar bits according to NativeProgram.scalarProfile.
    // State load/store: slot index.
    // Vertex/face: fixed authored index. Other opcodes use0.
    std::uint32_t immediate=0,line=1,column=1;
};
struct NativeGraph {
    std::vector<NativeInstruction> instructions;
    std::uint32_t value_count=0;
};
struct NativeProgram {
    ProgramIr ir;
    std::array<NativeGraph,3> actions;
    std::string identity;
    NativeScalarProfile scalarProfile=NativeScalarProfile::ordinary_i32;
};
[[nodiscard]] NativeProgram native_compile(std::string_view,Notation = {},NativeScalarProfile = NativeScalarProfile::ordinary_i32);
[[nodiscard]] std::string native_hlsl(const NativeProgram&,NativeAction);
[[nodiscard]] std::string native_glsl(const NativeProgram&,NativeAction);
// A leading sign is accepted here; the source lowerer folds only an immediate
// unary minus with magnitude2^31, so INT_MIN has no out-of-range intermediate.
[[nodiscard]] std::int32_t native_parse_integer(std::string_view,Notation = {});
// A new, explicitly less precise graphics profile: literals are converted
// from exact radix12 rationals to IEEE754 binary32 bits with ONE ties-even
// rounding during compilation. Runtime quantities are finite NORMALs or ±0;
// NaN/Inf, subnormals and nonzero values lost to zero are diagnosed.
[[nodiscard]] std::uint32_t native_parse_binary32(std::string_view,Notation = {});
// CPU oracle only: uint64 count -> one exact binary32 RNE, then CPU binary32
// division by1e9. Runtime receives ns in b6 and converts on the GPU; its final
// division follows the graphics API's native precision and is not promised
// to match the CPU division in the last bits.
[[nodiscard]] std::uint32_t native_elapsed_binary32(std::uint64_t nanoseconds);
struct NativeFrame {
    std::vector<std::int32_t> states;
    std::vector<std::array<std::int32_t,3>> vertices;
    std::vector<std::array<std::uint32_t,3>> faces;
    std::array<std::int32_t,3> rotation{},color{1,1,1};
    bool draw_requested=false;
    bool operator==(const NativeFrame&)const=default;
};
struct NativeDiagnostic {
    NativeError code=NativeError::none;
    std::uint32_t line=0,column=0,instruction=0;
    bool operator==(const NativeDiagnostic&)const=default;
};
struct NativeExecution {NativeFrame frame;NativeDiagnostic diagnostic;};
[[nodiscard]] NativeExecution native_reference(const NativeProgram&,NativeAction,const NativeFrame& previous={},std::int32_t dt=0);
struct NativeRealFrame32 {
    std::vector<std::uint32_t> states;
    std::vector<std::array<std::uint32_t,3>> vertices;
    std::vector<std::array<std::uint32_t,3>> faces;
    std::array<std::uint32_t,3> rotation{},color{0x3f800000u,0x3f800000u,0x3f800000u};
    bool draw_requested=false;
    bool operator==(const NativeRealFrame32&)const=default;
};
struct NativeRealExecution {NativeRealFrame32 frame;NativeDiagnostic diagnostic;};
[[nodiscard]] NativeRealExecution native_reference_real(const NativeProgram&,NativeAction,const NativeRealFrame32& previous={},std::uint32_t dt_bits=0);
// Backend contract for BOTH generated sources and the direct SPIR-V emitter:
// one invocation per item, one compiled kernel per action (no interpreter).
// Params[0]=item-count,[1]=work-bank,[2]=old-bank;16uint/64B as SmoCompute.
// Binary32 only: Params[3]=elapsed-mode0 raw-dt in b5,1 uint64 ns pairs in b6.
// GPU count conversion has one integer-defined RNE; the subsequent seconds
// scaling uses native API division precision, independently of the CPU oracle.
// b0 states:2banks*items*state-count words; b1 vertices:2banks*items*V*3;
// b2 faces:2banks*items*F*3; b3 frames:2banks*items*10 words;
// b4 status:items*4(code,line,column,instruction); b5 dt:items raw scalar words;
// binary32 elapsed mode b6 contains low/high nanosecond words per item.
// Frame: rotation0..2,color3..5,vertex-count6,face-count7,draw8,reserved0 at9.
// Nonreset actions copy old geometry/states to work before authored stores.
// Each failed item reports its first error. Runner commits NO bank if any
// item fails, preserving the entire previously committed batch.
// I32: / requires nonzero divisor and exact integer result; power exponent0..64.
// Binary32: finite normals/±0, strict separate binary32 operations; ordinary
// power integral exponent[-64,64]. A negative exponent first reciprocates the
// base, then uses the same seven-step binary-power tree as positive exponents.
// Both: ordinary x^0=1; pinv/alternativa remain explicitly unsupported.
// Fractional quantities are outside I32 only; a spelling with zero fractional
// digits, e.g.1.0 or.0, is exactly integral and accepted in either profile.
} //namespace lgaed::smo

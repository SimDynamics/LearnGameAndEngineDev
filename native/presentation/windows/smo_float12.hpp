#pragma once
#include "presentation/windows/smo_duodecimal.hpp"

namespace lgaed::presentation {
// VERSIONED SOFTWARE RADIX12 FLOATING PROFILE. Physical transport remains six
// uint words; numerical meaning differs from the old fixed D12 packets.
// digits.xyz = three little-endian radix20736 coefficient limbs;
// digits.w = tag | uint(E+192). Value=(-1)^negative * C *12^E.
// E is a POSITIONAL encoding scale, not the SMO pdir operation/exponent rule.
using SmoF12=SmoD12;
using SmoF12Result=SmoD12Result;
using SmoF12Observer=SmoD12Observer;
inline constexpr std::uint32_t smo_f12_precision=12u,smo_f12_radix=20736u;
inline constexpr std::uint32_t smo_f12_tag=0xf1200000u,smo_f12_tag_mask=0xfffffe00u;
inline constexpr std::int32_t smo_f12_min_exponent=-192,smo_f12_max_exponent=192;
inline constexpr std::uint64_t smo_f12_min_coefficient=743008370688ull;
inline constexpr std::uint64_t smo_f12_max_coefficient=8916100448255ull;
// Canonical nonzero C has twelve radix12 significant digits. Canonical zero
// has C=0,E=0,negative=0,WITH its version tag. A default SmoD12{} is not F12.
[[nodiscard]] bool smo_f12_valid(SmoF12);
[[nodiscard]] SmoF12 smo_f12_zero();
[[nodiscard]] SmoF12 smo_f12_uint(std::uint32_t);
[[nodiscard]] SmoF12 smo_f12_literal(std::uint32_t whole_packed,std::uint32_t fraction_packed,std::uint32_t fraction_count,std::uint32_t negative=0);
[[nodiscard]] SmoF12 smo_f12_parse(std::string_view); //expanded base12, standalone@; no e-notation
[[nodiscard]] std::string smo_f12_format(SmoF12);
[[nodiscard]] std::uint64_t smo_f12_coefficient(SmoF12);
[[nodiscard]] std::int32_t smo_f12_exponent(SmoF12);
[[nodiscard]] int smo_f12_compare(SmoF12,SmoF12); //2 means invalid
[[nodiscard]] SmoF12 smo_f12_abs(SmoF12);
[[nodiscard]] SmoF12 smo_f12_negate(SmoF12);
[[nodiscard]] SmoF12 smo_f12_add(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_sub(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_mul(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_div(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_pdir(SmoF12,SmoF12 exponent);
[[nodiscard]] SmoF12 smo_f12_pinv(SmoF12,SmoF12 exponent);
[[nodiscard]] SmoF12 smo_f12_sqrt(SmoF12);
[[nodiscard]] SmoF12 smo_f12_min(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_max(SmoF12,SmoF12);
[[nodiscard]] SmoF12 smo_f12_clamp(SmoF12,SmoF12 low,SmoF12 high);
// These graphics composites use an EXPLICIT NEW scalar-RNE operation tree.
// They do not claim the fixed engine's exact widened one-round fusion.
// Intermediate positional exponents remain internal until the final return.
[[nodiscard]] SmoF12 smo_f12_discriminant(SmoF12 h,SmoF12 a,SmoF12 c);
[[nodiscard]] SmoF12 smo_f12_magnitude2(SmoF12 x,SmoF12 y,SmoF12 z);
[[nodiscard]] SmoF12 smo_f12_norm3(SmoF12 x,SmoF12 y,SmoF12 z);
[[nodiscard]] SmoF12Result smo_f12_evaluate(std::uint32_t operation,SmoF12,SmoF12,std::uint32_t profile=0);
void smo_f12_observe(SmoF12Observer) noexcept;
// Final display conversion only, ties-to-even, no new INEXACT flag.
[[nodiscard]] std::uint32_t smo_f12_round_uint(SmoF12,std::uint32_t& diagnostic);
// Exact binary32 bit decomposition, integer radix12 expansion, ONE RNE.
// Nonfinite source bits produce a fatal domain diagnostic; -0 is canonical0.
[[nodiscard]] SmoF12 smo_f12_from_binary32_bits(std::uint32_t);
// The ONLY floating host bridge, for post-measurement comparisons/display.
// It never feeds engine arithmetic. Invalid packets return quiet NaN.
[[nodiscard]] long double smo_f12_comparison_value(SmoF12);
}

#pragma once
#include "presentation/windows/smo_rule_contract.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace lgaed::presentation {
// Software radix12 arithmetic; the GPU still executes binary uint instructions.
// Four little-endian limbs, each a coefficient0..20735 in base12^4. The first
// two limbs are fractional: quantum12^-8, finite magnitude<12^8. No floating
// arithmetic participates in arithmetic, powers, square root, parse or format.
inline constexpr std::uint32_t smo_d12_radix=20736u;
inline constexpr std::uint64_t smo_d12_scale=429981696ull;
inline constexpr std::uint64_t smo_d12_max_raw=184884258895036415ull;
enum SmoD12Diagnostic:std::uint32_t {
    smo_d12_ok=0,smo_d12_overflow=1u,smo_d12_underflow=2u,smo_d12_domain=4u,
    smo_d12_unresolved=8u,smo_d12_encoding=16u,smo_d12_operation=32u,
    smo_d12_profile=64u,smo_d12_exponent=128u,
    smo_d12_fatal_mask=0xffffu,smo_d12_inexact=1u<<16
};
struct SmoD12 {
    std::array<std::uint32_t,4> digits{};
    std::uint32_t negative=0,diagnostic=0;
    bool operator==(const SmoD12&)const=default;
};
struct SmoD12Result {SmoD12 primary,alternative;std::uint32_t flags=0;};
static_assert(sizeof(SmoD12)==24&&sizeof(SmoD12Result)==52);
enum class SmoD12Operation:std::uint32_t {add=0,subtract=1,multiply=2,divide=3,direct_power=4,inverted_power=5,square_root=9};
[[nodiscard]] bool smo_d12_valid(SmoD12);
[[nodiscard]] SmoD12 smo_d12_zero();
[[nodiscard]] SmoD12 smo_d12_uint(std::uint32_t whole_magnitude);
// Both arguments are at most eight packed base12 digit nibbles. Fractional
// packed digits are right aligned and fraction_count is0..8. Example .01:
// literal(0,0x01,2,0);12.5:literal(0x10,0x6,1,0). No '@' positional digit.
[[nodiscard]] SmoD12 smo_d12_literal(std::uint32_t whole_packed,std::uint32_t fraction_packed,std::uint32_t fraction_count,std::uint32_t negative=0);
[[nodiscard]] SmoD12 smo_d12_parse(std::string_view); //existing0/#/~ and standalone@ notation
[[nodiscard]] std::string smo_d12_format(SmoD12);
// Invalid operands compare as2, never silently equal. Arithmetic propagates
// fatal diagnostics. Inexact is a NONFATAL warning and is always retained.
[[nodiscard]] int smo_d12_compare(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_abs(SmoD12);
[[nodiscard]] SmoD12 smo_d12_negate(SmoD12);
[[nodiscard]] SmoD12 smo_d12_add(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_sub(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_mul(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_div(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_pdir(SmoD12,SmoD12 exponent);
[[nodiscard]] SmoD12 smo_d12_pinv(SmoD12,SmoD12 exponent);
// Principal nonnegative fixed-radix12 square root as explicit graphics support;
// negative arguments remain outside its provisional domain.
[[nodiscard]] SmoD12 smo_d12_sqrt(SmoD12);
// Fused graphics primitive: a>0; compute |h|^2-a*c in exact double-scale
// integer intermediates, then quantize ONCE. Generic pdir and its underflow
// diagnostics are unchanged. A nonzero final result rounded to 0 is still fatal.
[[nodiscard]] SmoD12 smo_d12_discriminant(SmoD12 h,SmoD12 a,SmoD12 c);
// Geometric magnitude operations retain all three raw squares until the final
// narrowing. magnitude2 quantizes sum(raw^2)/scale once; norm3 rounds the
// integer root of sum(raw^2) directly, without a premature narrow square.
// Generic powers and strict underflow of an unrepresentable FINAL magnitude2
// result remain unchanged. Norm3 propagates all input warnings/errors.
[[nodiscard]] SmoD12 smo_d12_magnitude2(SmoD12 x,SmoD12 y,SmoD12 z);
[[nodiscard]] SmoD12 smo_d12_norm3(SmoD12 x,SmoD12 y,SmoD12 z);
[[nodiscard]] SmoD12 smo_d12_min(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_max(SmoD12,SmoD12);
[[nodiscard]] SmoD12 smo_d12_clamp(SmoD12,SmoD12 low,SmoD12 high);
// Same profile bit layout as smo_rule_contract.hpp. Convenience helpers above
// consume only primary under profile0, exactly as the current interpreter.
// Result flags use has_primary/has_alternative/unresolved/experimental.
[[nodiscard]] SmoD12Result smo_d12_evaluate(std::uint32_t operation,SmoD12,SmoD12,std::uint32_t profile=0);
using SmoD12Observer=std::function<void(std::uint32_t,SmoD12,SmoD12,std::uint32_t,SmoD12Result)>;
// Optional CPU-oracle diagnostics only, empty by default and local to the
// calling thread. A fatal primary evaluation invokes it once; nested observer
// evaluations cannot recurse into it. No history/files are retained, and
// observer exceptions never alter the numerical result. Pass{} to clear it.
void smo_d12_observe(SmoD12Observer) noexcept;
}

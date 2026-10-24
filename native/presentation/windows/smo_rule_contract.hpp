#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::presentation {
// Experimental GPU support ABI v1. Profile zero preserves the current SMO
// rules on the supported FP32 normal/zero domain. It does NOT modify the FP64
// interpreter or promise identical range/rounding. Subnormals are diagnosed:
// portable HLSL/GLSL hardware may flush them, which must not invent exact zero.
enum class SmoRuleOperation:std::uint32_t {
    add=0,subtract=1,multiply=2,divide=3,direct_power=4,inverted_power=5,
    base12_add=6,base12_scale=7,base12_rational=8
};
enum SmoRuleProfile:std::uint32_t {
    smo_rule_default=0,
    smo_rule_conventional_sign=1u<<0,
    smo_rule_multiply_zero_conserves=1u<<1,
    // Bits2..3 are one enum field:0 conserve numerator,1 choose zero,
    //2 leave division by zero UNRESOLVED (never evaluate native a/0),3 invalid.
    smo_rule_divide_zero_empty=1u<<2,
    smo_rule_divide_zero_unresolved=2u<<2,
    smo_rule_divide_zero_mask=3u<<2,
    smo_rule_power_zero_conserves=1u<<4,
    smo_rule_unit_exponent_one=1u<<5,
    smo_rule_profile_mask=(1u<<6)-1
};
enum SmoRuleFlag:std::uint32_t {
    smo_rule_has_primary=1u<<0,smo_rule_has_alternative=1u<<1,
    smo_rule_unresolved=1u<<2,smo_rule_has_packed=1u<<3,
    smo_rule_has_packed_alternative=1u<<4,smo_rule_has_rational=1u<<5,
    // Numerical FP32 view of an exact packed/rational result. The exact
    // integer/digits remain authoritative, never the rounded float view.
    smo_rule_representation_view=1u<<6,smo_rule_experimental=1u<<7
};
enum SmoRuleDiagnostic:std::uint32_t {
    smo_rule_ok=0,smo_rule_nonfinite_input=1u<<0,smo_rule_invalid_profile=1u<<1,
    smo_rule_domain=1u<<2,smo_rule_exponent=1u<<3,smo_rule_overflow=1u<<4,
    smo_rule_underflow=1u<<5,smo_rule_invalid_digit=1u<<6,
    smo_rule_packed_overflow=1u<<7,smo_rule_division_unresolved=1u<<8,
    smo_rule_zero_denominator=1u<<9,smo_rule_subnormal=1u<<10,
    smo_rule_missing_alternative=1u<<11,smo_rule_invalid_operation=1u<<12,
    smo_rule_invalid_selection=1u<<13,smo_rule_factor=1u<<14
};
inline constexpr std::uint32_t smo_rule_nan=0x7fc00000u;
inline constexpr std::uint32_t smo_rule_packed_max_value=429981695u; //12^8-1
// Exactly eight little-endian uint words, layout-identical in SSBO/raw UAV.
// Float operands are explicit IEEE bits, so invalid/subnormal input can be
// diagnosed BEFORE a device's floating arithmetic flushes or changes it.
struct SmoRuleInput {
    std::uint32_t operation=0,profile=0,left_bits=0,right_bits=0;
    std::uint32_t packed_left=0,packed_right=0,factor=0,selection=0;
};
// flags determine whether values exist. Absent numeric values use a NaN
// sentinel, not a fabricated answer. No result is usable when diagnostic!=0.
// Base12 packed digits are eight 4-bit nibbles, least significant first,
// each 0..11. Rational numerator/denominator use the SAME packed encoding.
struct SmoRuleResult {
    std::uint32_t primary_bits=smo_rule_nan,alternative_bits=smo_rule_nan,flags=0,diagnostic=0;
    std::uint32_t packed_primary=0,packed_alternative=0,numerator=0,denominator=0;
};
static_assert(sizeof(SmoRuleInput)==32&&sizeof(SmoRuleResult)==32);
struct SmoRuleCase {std::string name;SmoRuleInput input;};
struct SmoRuleNamedProfile {std::string name;std::uint32_t profile=0;};

[[nodiscard]] SmoRuleInput smo_rule_input(SmoRuleOperation,float left,float right,std::uint32_t profile=0,std::uint32_t selection=0);
// Text uses the existing SMO base12 parser/notation. The resulting support
// magnitudes are IEEE FP32, never a claim of native radix12 GPU hardware.
[[nodiscard]] SmoRuleInput smo_rule_input12(SmoRuleOperation,std::string_view left,std::string_view right,std::uint32_t profile=0,std::uint32_t selection=0);
// Independent C++ FP32 oracle; diagnostic results are returned, not swallowed.
[[nodiscard]] SmoRuleResult smo_rule_reference(const SmoRuleInput&);
[[nodiscard]] std::vector<SmoRuleCase> smo_rule_cases();
[[nodiscard]] std::vector<SmoRuleNamedProfile> smo_rule_profiles();
[[nodiscard]] std::string smo_rule_profile_name(std::uint32_t);
[[nodiscard]] std::string smo_rule_operation_name(SmoRuleOperation);
[[nodiscard]] std::string smo_rule_diagnostic_name(std::uint32_t);
[[nodiscard]] std::uint32_t smo_rule_pack12(std::uint32_t ordinary_integer);
[[nodiscard]] std::uint32_t smo_rule_unpack12(std::uint32_t packed_digits);
[[nodiscard]] std::string smo_rule_format12(std::uint32_t packed_digits);
// selection0 evaluates the ordinary default. selection1 explicitly selects
// an existing alternative and consumes it, matching alternativa(expr).
// Selection1 on a result without an alternative is a diagnostic.
[[nodiscard]] inline float smo_rule_primary(const SmoRuleResult& result){return std::bit_cast<float>(result.primary_bits);}
[[nodiscard]] inline float smo_rule_alternative(const SmoRuleResult& result){return std::bit_cast<float>(result.alternative_bits);}
}

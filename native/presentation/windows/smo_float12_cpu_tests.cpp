#include "presentation/windows/smo_float12.hpp"
#include "presentation/windows/smo_duodecimal_scene.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <intrin.h>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::presentation;
using U64=unsigned __int64;
constexpr U64 radix=20736,minimum=743008370688ull,limit=8916100448256ull;
constexpr std::uint32_t tag=0xf1200000u;
std::size_t checks=0;
void check(bool condition,std::string_view message) {
    ++checks;if(!condition)throw std::runtime_error(std::string(message));
}
U64 power12(unsigned exponent) {U64 result=1;for(unsigned i=0;i<exponent;++i) {
    if(result>std::numeric_limits<U64>::max()/12)throw std::logic_error("Integer oracle power exceeded its declared bound.");result*=12;
}return result;}
U64 coefficient(SmoF12 value) {return (static_cast<U64>(value.digits[2])*radix+value.digits[1])*radix+value.digits[0];}
int exponent(SmoF12 value) {return static_cast<int>(value.digits[3]&511u)-192;}
SmoF12 packet(U64 value,int scale,bool negative=false,std::uint32_t diagnostic=0) {
    if((value&&(value<minimum||value>=limit))||scale< -192||scale>192)throw std::logic_error("Noncanonical test fixture.");
    SmoF12 result;result.negative=value&&negative?1u:0u;result.diagnostic=diagnostic;
    if(!value)scale=0;
    for(unsigned i=0;i<3;++i) {result.digits[i]=static_cast<std::uint32_t>(value%radix);value/=radix;}
    result.digits[3]=tag|static_cast<std::uint32_t>(scale+192);return result;
}
SmoF12 small(unsigned value,bool negative=false) {
    if(!value)return packet(0,0);
    U64 c=value;int e=0;while(c<minimum) {c*=12;--e;}return packet(c,e,negative);
}
SmoF12 shifted(SmoF12 value,int places) {return packet(coefficient(value),exponent(value)+places,value.negative!=0,value.diagnostic);}
bool equal_value(SmoF12 a,SmoF12 b) {
    return coefficient(a)==coefficient(b)&&exponent(a)==exponent(b)&&a.negative==b.negative;
}
void canonical(SmoF12 value,std::string_view message) {
    check(smo_f12_valid(value),message);
    check((value.digits[3]&0xfffffe00u)==tag&&(value.digits[3]&511u)<=384u,"Wrong F12 version/exponent encoding.");
    for(unsigned i=0;i<3;++i)check(value.digits[i]<radix,"A significand limb escaped radix20736.");
    const auto c=coefficient(value);
    check(c?(c>=minimum&&c<limit&&value.negative<=1u):(exponent(value)==0&&value.negative==0u),"Noncanonical F12 significand or zero.");
    check(smo_f12_coefficient(value)==c&&smo_f12_exponent(value)==exponent(value),"Public F12 decoding differs from the transport contract.");
}
void exact(SmoF12 observed,SmoF12 expected,std::string_view message) {
    canonical(observed,message);check(observed==expected,message);
}
void fatal(SmoF12 observed,std::uint32_t kind,std::string_view message) {
    check(!smo_f12_valid(observed)&&(observed.diagnostic&kind)!=0u,message);
}

// Independent bounded rational oracle: binary128 intrinsics calculate exact
// integer numerators; no floating arithmetic or production F12 operation is
// used to select an expected digit, exponent or rounding direction.
struct Wide {U64 low=0,high=0;};
Wide multiply(U64 a,U64 b) {Wide result;result.low=_umul128(a,b,&result.high);return result;}
int compare(Wide a,Wide b) {if(a.high!=b.high)return a.high<b.high?-1:1;return a.low<b.low?-1:a.low>b.low?1:0;}
Wide add(Wide a,Wide b) {const auto low=a.low;a.low+=b.low;a.high+=b.high+static_cast<U64>(a.low<low);return a;}
Wide subtract(Wide a,Wide b) {const auto borrow=a.low<b.low;a.low-=b.low;a.high-=b.high+static_cast<U64>(borrow);return a;}
SmoF12 rounded_quotient(Wide n,U64 denominator,int e,bool negative,std::uint32_t inherited=0) {
    if(!denominator||n.high>=denominator)throw std::logic_error("Rational oracle division exceeded its bounded quotient.");
    U64 remainder=0;auto c=_udiv128(n.high,n.low,denominator,&remainder);
    if(remainder) {inherited|=smo_d12_inexact;const auto twice=remainder*2;
        if(twice>denominator||(twice==denominator&&(c&1)))++c;}
    if(c==limit) {c/=12;++e;}
    return packet(c,e,negative,inherited);
}
SmoF12 normalize(Wide magnitude,int e,bool negative,std::uint32_t inherited=0) {
    if(!magnitude.low&&!magnitude.high)return packet(0,0,false,inherited);
    U64 divisor=1;unsigned shift=0;
    while(compare(magnitude,multiply(limit,divisor))>=0) {divisor*=12;++shift;
        if(shift>13)throw std::logic_error("Addition oracle exceeded its exact alignment bound.");}
    if(shift)return rounded_quotient(magnitude,divisor,e+static_cast<int>(shift),negative,inherited);
    if(magnitude.high)throw std::logic_error("Small oracle coefficient does not fit uint64.");
    auto c=magnitude.low;while(c<minimum) {c*=12;--e;}
    return packet(c,e,negative,inherited);
}
SmoF12 sum_oracle(SmoF12 a,SmoF12 b,bool subtract_right=false) {
    if(subtract_right&&coefficient(b))b.negative^=1u;
    const auto inherited=a.diagnostic|b.diagnostic;
    if(!coefficient(a)) {b.diagnostic=inherited;return b;}
    if(!coefficient(b)) {a.diagnostic=inherited;return a;}
    const auto low=std::min(exponent(a),exponent(b));
    const auto shift_a=static_cast<unsigned>(exponent(a)-low),shift_b=static_cast<unsigned>(exponent(b)-low);
    if(shift_a>13||shift_b>13)throw std::logic_error("Use explicit rounding-boundary expectations for distant exponents.");
    const auto left=multiply(coefficient(a),power12(shift_a)),right=multiply(coefficient(b),power12(shift_b));
    if(a.negative==b.negative)return normalize(add(left,right),low,a.negative!=0,inherited);
    if(compare(left,right)>=0)return normalize(subtract(left,right),low,a.negative!=0,inherited);
    return normalize(subtract(right,left),low,b.negative!=0,inherited);
}
SmoF12 product_oracle(SmoF12 a,SmoF12 b) {
    const auto inherited=a.diagnostic|b.diagnostic;
    if(!coefficient(a)||!coefficient(b))return packet(0,0,false,inherited);
    const auto p=multiply(coefficient(a),coefficient(b));
    const unsigned shift=compare(p,multiply(minimum,limit))>=0?12u:11u;
    return rounded_quotient(p,power12(shift),exponent(a)+exponent(b)+static_cast<int>(shift),a.negative||b.negative,inherited);
}
SmoF12 quotient_oracle(SmoF12 a,SmoF12 b) {
    if(!coefficient(b))throw std::logic_error("Zero-policy tests do not use a rational division oracle.");
    const auto inherited=a.diagnostic|b.diagnostic;
    if(!coefficient(a))return packet(0,0,false,inherited);
    const unsigned shift=coefficient(a)>=coefficient(b)?11u:12u;
    return rounded_quotient(multiply(coefficient(a),power12(shift)),coefficient(b),
        exponent(a)-exponent(b)-static_cast<int>(shift),a.negative||b.negative,inherited);
}
SmoF12 sqrt_oracle(SmoF12 input) {
    const auto shift=exponent(input)%2==0?12u:11u;
    const auto n=multiply(coefficient(input),power12(shift));
    U64 low=0,high=limit,root=0;
    while(low<=high) {const auto middle=(low+high)/2;
        if(compare(multiply(middle,middle),n)<=0) {root=middle;low=middle+1;}else high=middle-1;}
    const auto remainder=subtract(n,multiply(root,root));auto diagnostic=input.diagnostic;
    if(remainder.low||remainder.high)diagnostic|=smo_d12_inexact;
    if(remainder.high||remainder.low>root)++root;
    auto e=(exponent(input)-static_cast<int>(shift))/2;
    if(root==limit) {root/=12;++e;}
    return packet(root,e,false,diagnostic);
}

// Tiny exact integer utility used ONLY to audit binary32 conversion. It holds
// cross-products for all binary32 extrema and avoids a floating tolerance or a
// binary floating conversion masquerading as the expected radix12 value.
struct UInt256 {std::array<std::uint32_t,8> limb{};};
UInt256 integer256(U64 value) {UInt256 result;result.limb[0]=static_cast<std::uint32_t>(value);result.limb[1]=static_cast<std::uint32_t>(value>>32);return result;}
void multiply_small(UInt256& value,std::uint32_t factor) {
    U64 carry=0;for(auto& limb:value.limb) {const auto product=static_cast<U64>(limb)*factor+carry;limb=static_cast<std::uint32_t>(product);carry=product>>32;}
    if(carry)throw std::logic_error("Binary bridge oracle exceeded256 bits.");
}
void times_power(UInt256& value,unsigned base,unsigned count) {for(unsigned i=0;i<count;++i)multiply_small(value,base);}
int compare(UInt256 a,UInt256 b) {for(unsigned i=8;i>0;--i)if(a.limb[i-1]!=b.limb[i-1])return a.limb[i-1]<b.limb[i-1]?-1:1;return 0;}
void check_binary_bridge(std::uint32_t bits) {
    const auto sign=bits>>31,field=(bits>>23)&255u,fraction=bits&0x7fffffu;
    const auto actual=smo_f12_from_binary32_bits(bits);
    if(field==255u) {fatal(actual,smo_d12_domain,"Nonfinite binary32 bits became an engine quantity.");return;}
    const U64 numerator=field?(fraction|0x800000u):fraction;
    if(!numerator) {exact(actual,packet(0,0),"Binary signed zero was not canonicalized.");return;}
    canonical(actual,"A finite binary32 value was rejected by the expanded radix12 range.");
    check(coefficient(actual)!=0&&actual.negative==sign,"A nonzero binary32 value lost magnitude or sign.");
    const int binary_exponent=field?static_cast<int>(field)-150:-149;
    const int radix_exponent=exponent(actual);
    const unsigned binary_denominator=static_cast<unsigned>(std::max(0,-binary_exponent));
    const unsigned radix_denominator=static_cast<unsigned>(std::max(0,-radix_exponent));
    auto exact_scaled=integer256(numerator*24);
    times_power(exact_scaled,2,static_cast<unsigned>(binary_exponent+static_cast<int>(binary_denominator)));
    times_power(exact_scaled,12,radix_denominator);
    const auto boundary=[&](U64 value) {auto result=integer256(value);times_power(result,12,static_cast<unsigned>(radix_exponent+static_cast<int>(radix_denominator)));
        times_power(result,2,binary_denominator);return result;};
    const auto c=coefficient(actual);
    // At a power-of12 the predecessor's ULP is twelve times smaller. Thus the
    // lower midpoint is C-1/24, not C-1/2. All comparisons are exact integers.
    const auto lower=boundary(c*24-(c==minimum?1u:12u)),upper=boundary(c*24+12),center=boundary(c*24);
    const auto lo=compare(exact_scaled,lower),hi=compare(exact_scaled,upper);
    check(lo>=0&&hi<=0,"Binary32 bridge did not choose the nearest radix12 significand.");
    if(lo==0||hi==0)check((c&1)==0,"Binary32 bridge broke ties-to-even.");
    check(((actual.diagnostic&smo_d12_inexact)!=0)==(compare(exact_scaled,center)!=0),"Binary32 bridge INEXACT does not describe the exact source bits.");
}

void encoding_and_notation() {
    exact(smo_f12_zero(),packet(0,0),"F12 zero lacks its own version tag.");
    const std::pair<std::string_view,SmoF12> literals[]{
        {"0",packet(0,0)},{"-0",packet(0,0)},{"1",small(1)},{"-2",small(2,true)},
        {"#",small(10)},{"~",small(11)},{"10",small(12)},{"@",small(12)},
        {"0.6",packet(minimum*6,-12)},{"0.4",packet(minimum*4,-12)},
        {"0.3",packet(minimum*3,-12)},{"0.01",packet(minimum,-13)},
        {"1.6",packet(minimum*3/2,-11)}
    };
    for(const auto& [text,expected]:literals) {
        exact(smo_f12_parse(text),expected,"An exact positional radix12 literal changed.");
    }
    exact(smo_f12_parse("1.6"),packet(minimum*3/2,-11),"One and a half was parsed in decimal instead of radix12.");
    exact(smo_f12_literal(0x10,0x6,1),packet(minimum*25/24,-10),"Packed literal12.5 has a wrong magnitude.");
    for(const auto value:{0u,1u,12u,255u,std::numeric_limits<std::uint32_t>::max()})
        exact(smo_f12_uint(value),small(value),"Integer construction did not retain its exact magnitude.");
    check(smo_f12_format(small(12))=="10"&&smo_f12_format(small(10))=="#"&&
        smo_f12_format(small(11))=="~"&&smo_f12_format(packet(0,0))=="0","Canonical digit spelling changed.");
    check(smo_f12_format(smo_f12_parse("1.6"))=="1.6","Formatting hid a positional fraction.");
    const auto maximum=packet(limit-1,192),least=packet(minimum,-192);
    const std::string maximum_text=std::string(12,'~')+std::string(192,'0');
    const std::string least_text="0."+std::string(180,'0')+"1";
    check(smo_f12_format(maximum)==maximum_text&&smo_f12_format(least)==least_text,"Expanded formatting lost an exponent extreme.");
    exact(smo_f12_parse(maximum_text),maximum,"Largest finite F12 value did not parse exactly.");
    exact(smo_f12_parse(least_text),least,"Smallest normal F12 value did not parse exactly.");
    for(const auto& [text,value]:literals)
        exact(smo_f12_parse(smo_f12_format(value)),value,"A canonical expanded F12 value did not round trip.");
    for(const auto text:{"", ".", "1e2", "1@", "@1", "@@", "@.6", "1..2", "+", "1_"}) {
        bool rejected=false;try {(void)smo_f12_parse(text);}catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"An ambiguous or invalid numeral was silently accepted.");
    }
    check(!smo_f12_valid(SmoD12{})&&!smo_f12_valid(smo_d12_uint(1))&&!smo_d12_valid(small(1)),
        "The fixed and floating radix12 packets can be silently mixed.");
    fatal(smo_f12_add(smo_d12_uint(1),small(1)),smo_d12_encoding,"Arithmetic reinterpreted a fixed D12 operand.");
    for(unsigned kind=0;kind<6;++kind) {
        auto invalid=small(1);
        if(kind==0)invalid.digits[3]^=0x200u;
        if(kind==1)invalid.digits[3]=tag|511u;
        if(kind==2)invalid.digits[0]=20736u;
        if(kind==3)invalid.negative=2;
        if(kind==4)invalid=packet(0,0),invalid.negative=1;
        if(kind==5)invalid=packet(0,0),invalid.digits[3]=tag|193u;
        check(!smo_f12_valid(invalid)&&smo_f12_compare(invalid,small(1))==2,"A malformed packet was treated as a quantity.");
    }
    auto not_normal=small(1);not_normal.digits[0]=1;not_normal.digits[1]=not_normal.digits[2]=0;
    check(!smo_f12_valid(not_normal),"A nonzero subnormal coefficient bypassed canonical normalization.");
    check(smo_f12_compare(packet(0,0),least)<0&&smo_f12_compare(packet(0,0),packet(minimum,-192,true))>0,
        "Zero comparison ordered its canonical exponent instead of its value.");
    // The new precision is explicitly different from the old fixed profile.
    // 10000.00000001 needs13 significant digits and is not exactly F12-p12.
    exact(smo_f12_parse("10000.00000001"),packet(minimum,-7,false,smo_d12_inexact),
        "The new representation falsely claimed exact retention of all fixed-point values.");
}

void rounding_boundaries() {
    const auto half=packet(minimum*6,-12);
    exact(smo_f12_add(packet(minimum+2,0),half),packet(minimum+2,0,false,smo_d12_inexact),"Addition did not round an even tie down.");
    exact(smo_f12_add(packet(minimum+3,0),half),packet(minimum+4,0,false,smo_d12_inexact),"Addition did not round an odd tie up.");
    exact(smo_f12_sub(packet(minimum+2,0),half),packet(minimum+2,0,false,smo_d12_inexact),"Subtraction lost the even endpoint of a tie.");
    exact(smo_f12_sub(packet(minimum+3,0),half),packet(minimum+2,0,false,smo_d12_inexact),"Subtraction chose the odd endpoint of a tie.");
    exact(smo_f12_add(packet(limit-1,0),half),packet(minimum,1,false,smo_d12_inexact),"RNE carry did not renormalize the next power of12.");
    exact(smo_f12_add(packet(minimum+3,0,true),packet(minimum*6,-12,true)),
        packet(minimum+4,0,true,smo_d12_inexact),"Negative ties were rounded asymmetrically.");
    const auto boundary=packet(minimum,0),gap13=packet(limit-1,-13),gap14=packet(limit-1,-14);
    exact(smo_f12_sub(boundary,gap13),packet(limit-1,-1,false,smo_d12_inexact),
        "Gap13 subtraction crossed a radix12 boundary using the wrong lower ULP.");
    exact(smo_f12_sub(boundary,gap14),packet(minimum,0,false,smo_d12_inexact),
        "Gap14 subtraction changed a significand despite a provably negligible operand.");
    exact(smo_f12_add(boundary,gap14),packet(minimum,0,false,smo_d12_inexact),"Distant addition invented significant digits.");
    exact(smo_f12_add(packet(minimum,0,true),gap13),packet(limit-1,-1,true,smo_d12_inexact),"A negative power-of12 boundary was rounded wrongly.");
    exact(smo_f12_sub(boundary,boundary),packet(0,0),"Exact cancellation did not produce the tagged canonical zero.");
    exact(smo_f12_sub(packet(minimum+1,0),boundary),small(1),"Close cancellation failed to renormalize all lost leading digits.");
    auto bad=gap14;bad.diagnostic=smo_d12_domain;
    fatal(smo_f12_add(boundary,bad),smo_d12_domain,"The distant-exponent shortcut hid an invalid operand.");
    auto warning=gap14;warning.diagnostic=smo_d12_inexact;
    exact(smo_f12_add(boundary,warning),packet(minimum,0,false,smo_d12_inexact),"An ignored small operand lost its warning.");
    exact(smo_f12_parse("1."+std::string(11,'0')+"6"),packet(minimum,-11,false,smo_d12_inexact),"Literal even tie was not rounded to even.");
    exact(smo_f12_parse("1."+std::string(10,'0')+"16"),packet(minimum+2,-11,false,smo_d12_inexact),"Literal odd tie was not rounded to even.");
    exact(smo_f12_parse(std::string(12,'~')+".6"),packet(minimum,1,false,smo_d12_inexact),"Literal rounding carry broke canonicalization.");
}

void rational_arithmetic() {
    std::vector<SmoF12> values{packet(0,0)};
    for(const auto c:{minimum,minimum+1,minimum+2,minimum*6,limit-1})for(const auto e:{-5,0,7})for(const bool negative:{false,true})
        values.push_back(packet(c,e,negative));
    for(const auto a:values)for(const auto b:values) {
        exact(smo_f12_add(a,b),sum_oracle(a,b),"F12 sum differs from its exact aligned integer oracle.");
        exact(smo_f12_sub(a,b),sum_oracle(a,b,true),"F12 subtraction differs from its exact aligned integer oracle.");
        exact(smo_f12_mul(a,b),product_oracle(a,b),"F12 product differs from one exact rational RNE.");
        if(coefficient(b))exact(smo_f12_div(a,b),quotient_oracle(a,b),"F12 quotient differs from one exact rational RNE.");
    }
    for(const auto text:{".6","1","2","3","4","10","100"}) {
        const auto value=smo_f12_parse(text);exact(smo_f12_sqrt(value),sqrt_oracle(value),"Square root differs from integer midpoint rounding.");
    }
    const auto old_left=smo_d12_parse("-0.0001971#"),old_right=smo_d12_parse("0.00002345");
    fatal(smo_d12_mul(old_left,old_right),smo_d12_underflow,"The old fixed-point range limit was erased.");
    const auto left=smo_f12_parse("-0.0001971#"),right=smo_f12_parse("0.00002345");
    // Exact:37318*3941/12^16=147070238/12^16. Padding four
    // duodecimal zeros gives its twelve-digit significand, with NO rounding.
    exact(smo_f12_mul(left,right),packet(147070238ull*power12(4),-20,true),
        "The real frame6680 product became zero or acquired a wrong rounded magnitude.");
}

void rule_policies_and_powers() {
    const auto zero=packet(0,0),one=small(1),two=small(2),minus_two=small(2,true),three=small(3);
    exact(smo_f12_mul(minus_two,small(3,true)),small(6,true),"The default absorbing negative multiplication rule changed.");
    exact(smo_f12_div(small(6,true),small(3,true)),minus_two,"The default absorbing negative division rule changed.");
    exact(smo_f12_evaluate(2,minus_two,small(3,true),smo_rule_conventional_sign).primary,small(6),
        "The isolated conventional-sign profile did not remain explicit.");
    exact(smo_f12_evaluate(3,small(6,true),small(3,true),smo_rule_conventional_sign).primary,two,
        "Conventional division failed to apply its selected sign profile.");
    for(const auto operands:{std::pair{minus_two,zero},std::pair{zero,minus_two},std::pair{zero,zero}}) {
        const auto other=coefficient(operands.first)?operands.first:operands.second;
        const auto primary=smo_f12_evaluate(2,operands.first,operands.second);
        check(primary.flags==(smo_rule_has_primary|smo_rule_has_alternative),"Multiplication by zero lost its explicit two-branch result.");
        exact(primary.primary,zero,"Default multiplication by zero must choose zero.");
        exact(primary.alternative,other,"Multiplication by zero lost the other quantity as its alternative.");
        const auto conserve=smo_f12_evaluate(2,operands.first,operands.second,smo_rule_multiply_zero_conserves);
        check(conserve.flags==(smo_rule_has_primary|smo_rule_has_alternative|smo_rule_experimental),"A zero-policy variant was adopted without its experimental flag.");
        exact(conserve.primary,other,"The isolated conserving multiplication profile selected the wrong branch.");
        exact(conserve.alternative,zero,"The conserving multiplication profile lost its zero alternative.");
    }
    for(const auto numerator:{zero,minus_two}) {
        const auto ordinary=smo_f12_evaluate(3,numerator,zero);
        check(ordinary.flags==(smo_rule_has_primary|smo_rule_has_alternative),"Division by zero lost its branch flags.");
        exact(ordinary.primary,numerator,"Default division by zero must conserve its numerator.");
        exact(ordinary.alternative,zero,"Default division by zero lost its zero alternative.");
        const auto empty=smo_f12_evaluate(3,numerator,zero,smo_rule_divide_zero_empty);
        exact(empty.primary,zero,"The isolated empty-division profile chose a quantity.");
        exact(empty.alternative,numerator,"The empty-division profile lost the original numerator.");
        const auto omitted=smo_f12_evaluate(3,numerator,zero,smo_rule_divide_zero_unresolved);
        check((omitted.flags&smo_rule_has_primary)==0&&(omitted.flags&smo_rule_unresolved)!=0&&
            (omitted.flags&smo_rule_experimental)!=0,"An unresolved expression fabricated an evaluated result.");
        fatal(omitted.primary,smo_d12_unresolved,"An unresolved division silently became a number.");
    }
    const auto zero_numerator=smo_f12_evaluate(3,zero,three);
    exact(zero_numerator.primary,zero,"Zero divided by a nonzero quantity changed.");
    check(zero_numerator.flags==smo_rule_has_primary,"A resolved nonzero denominator invented an alternative.");

    const int exponents[]{3,2,1,0,-1,-2,-3};
    const std::string_view direct[]{"8","4","2","0","-2","-4","-8"};
    const std::string_view inverted[]{"0.6","1","2","0","-2","-1","-0.6"};
    for(std::size_t i=0;i<std::size(exponents);++i) {
        const auto n=small(static_cast<unsigned>(exponents[i]<0?-exponents[i]:exponents[i]),exponents[i]<0);
        exact(smo_f12_pdir(two,n),smo_f12_parse(direct[i]),"A direct-power default table entry changed.");
        exact(smo_f12_pinv(two,n),smo_f12_parse(inverted[i]),"An inverted-power default table entry changed.");
    }
    for(const auto operation:{4u,5u}) {
        const auto at_zero=smo_f12_evaluate(operation,two,zero);
        exact(at_zero.primary,zero,"Power at exponent zero changed the default policy.");
        exact(at_zero.alternative,two,"Power at exponent zero lost its base alternative.");
        check(at_zero.flags==(smo_rule_has_primary|smo_rule_has_alternative),"Power-zero alternatives lost their branch flags.");
        const auto conserving=smo_f12_evaluate(operation,two,zero,smo_rule_power_zero_conserves);
        exact(conserving.primary,two,"The isolated conserving power-zero profile chose zero.");
        exact(conserving.alternative,zero,"The isolated conserving power profile lost the zero branch.");
        for(const auto n:{one,small(1,true)}) {
            const auto experimental=smo_f12_evaluate(operation,two,n,smo_rule_unit_exponent_one);
            exact(experimental.primary,n,"The explicitly selected unit-exponent variant did not choose signed one.");
            check(experimental.flags==(smo_rule_has_primary|smo_rule_experimental),"Unit-exponent experimentation altered default flags.");
        }
        for(const auto base:{zero,minus_two})fatal(smo_f12_evaluate(operation,base,zero).primary,smo_d12_domain,
            "A zero-exponent shortcut accepted an unsupported nonpositive power base.");
        fatal(smo_f12_evaluate(operation,two,smo_f12_parse("0.6")).primary,smo_d12_exponent,"A fractional exponent was silently interpreted as an integer.");
        fatal(smo_f12_evaluate(operation,two,smo_f12_parse("55")).primary,smo_d12_exponent,"Exponent65 exceeded the stated bounded prototype.");
    }
    for(const bool negative:{false,true}) {
        const auto n=small(64,negative);
        exact(smo_f12_pdir(small(12),n),packet(minimum,53,negative),"Direct exponent64 was confused with decimal notation or sign inversion.");
        exact(smo_f12_pinv(small(12),n),packet(minimum,-73,negative),"Inverted exponent64 selected the wrong reciprocal degree.");
    }
    exact(smo_f12_sqrt(zero),zero,"The explicit geometric square root of zero changed.");
    fatal(smo_f12_sqrt(minus_two),smo_d12_domain,"A negative square root escaped its declared domain.");
    fatal(smo_f12_evaluate(99,two,three).primary,smo_d12_operation,"An unsupported operation became a numeric answer.");
    for(const auto profile:{128u,static_cast<unsigned>(smo_rule_divide_zero_mask)})
        fatal(smo_f12_evaluate(2,two,three,profile).primary,smo_d12_profile,"An invalid profile was silently applied.");
    // This is intentionally NOT a ring. The counterexample must remain
    // visible rather than justifying ordinary distributive rewrites in SMO.
    exact(smo_f12_mul(minus_two,smo_f12_sub(one,one)),zero,"The default zero branch changed in a distributive counterexample.");
    exact(smo_f12_add(smo_f12_mul(minus_two,one),smo_f12_mul(minus_two,small(1,true))),small(4,true),
        "The default negative rule was rewritten to force ordinary distributivity.");
}

void range_and_internal_exponents() {
    const auto maximum=packet(limit-1,192),least=packet(minimum,-192),twelve=small(12);
    fatal(smo_f12_mul(maximum,twelve),smo_d12_overflow,"An actual exponent overflow was hidden.");
    fatal(smo_f12_div(least,twelve),smo_d12_underflow,"An actual exponent underflow became an exact zero.");
    fatal(smo_f12_mul(least,smo_f12_parse("0.6")),smo_d12_underflow,"A value below the normalized range lost its underflow diagnostic.");
    // Half of the top exponent's coefficient ULP: its rounding carry is out
    // of range, even though neither operand is out of range individually.
    fatal(smo_f12_add(maximum,packet(minimum*6,180)),smo_d12_overflow,"Rounding carry bypassed the final exponent limit.");
    fatal(smo_f12_parse("1"+std::string(204,'0')),smo_d12_overflow,"Literal overflow was silently saturated.");
    fatal(smo_f12_parse("0."+std::string(181,'0')+"1"),smo_d12_underflow,"Literal underflow was silently rounded to zero.");
    const auto tiny=packet(minimum,-111); //12^-100
    exact(smo_f12_pinv(tiny,small(4)),packet(minimum,189),
        "The internal denominator12^-200 was rejected although its reciprocal12^200 is representable.");
    exact(smo_f12_pinv(tiny,small(4,true)),packet(minimum,189,true),
        "A signed inverted power changed its internally unbounded exponent policy.");
    fatal(smo_f12_pdir(tiny,small(2)),smo_d12_underflow,"An unrepresentable FINAL direct power was hidden by wider internal exponents.");
    // No intermediate precision increase: operations still perform the
    // declared twelve-digit RNE, independently of postponed exponent checks.
    const auto repeating=quotient_oracle(small(1),small(5));
    exact(smo_f12_div(small(1),small(5)),repeating,"Expanded exponent range silently increased mantissa precision.");
    check((repeating.diagnostic&smo_d12_inexact)!=0,"A repeating radix12 quotient falsely claimed exactness.");
}

void geometry_composites() {
    const auto zero=packet(0,0),one=small(1),three=small(3),four=small(4),five=small(5);
    exact(smo_f12_magnitude2(three,four,zero),small(25),"The3-4-5 squared magnitude changed.");
    exact(smo_f12_norm3(three,four,zero),five,"The3-4-5 norm was not exact.");
    exact(smo_f12_norm3(small(3,true),small(4,true),zero),five,"The geometric norm incorrectly used the generic absorbing sign rule for squares.");
    const auto tiny=packet(minimum,-161); //12^-150
    exact(smo_f12_norm3(smo_f12_negate(tiny),zero,zero),tiny,"A one-axis norm introduced a needless square underflow or rounding.");
    fatal(smo_f12_magnitude2(tiny,zero,zero),smo_d12_underflow,"An unrepresentable FINAL squared magnitude lost its underflow diagnostic.");
    const auto huge=packet(minimum,139),huge_three=shifted(three,150),huge_four=shifted(four,150);
    exact(smo_f12_norm3(huge_three,huge_four,zero),shifted(five,150),
        "The norm rejected internal12^300 squares although its final12^150 scale fits.");
    fatal(smo_f12_magnitude2(huge,zero,zero),smo_d12_overflow,"An unrepresentable FINAL large squared magnitude lost its overflow diagnostic.");
    exact(smo_f12_discriminant(huge,huge,huge),zero,"Exact cancellation was rejected before a composite's final exponent check.");
    exact(smo_f12_discriminant(zero,one,one),small(1,true),"The signed discriminant changed its explicit subtraction tree.");
    fatal(smo_f12_discriminant(one,zero,one),smo_d12_domain,"A nonpositive ray coefficient escaped its declared precondition.");
    auto warning_zero=zero;warning_zero.diagnostic=smo_d12_inexact;
    auto warning_five=five;warning_five.diagnostic=smo_d12_inexact;
    exact(smo_f12_norm3(three,four,warning_zero),warning_five,"A zero axis lost its input warning in the norm shortcut.");
    auto warning_one=one;warning_one.diagnostic=smo_d12_inexact;
    exact(smo_f12_min(warning_one,small(2)),warning_one,"Minimum lost an input warning.");
    exact(smo_f12_max(warning_one,small(2)),packet(coefficient(small(2)),exponent(small(2)),false,smo_d12_inexact),"Maximum lost a warning from the unselected operand.");
    exact(smo_f12_clamp(small(3),zero,small(2)),small(2),"Clamp did not keep its upper boundary.");
    fatal(smo_f12_clamp(one,small(2),zero),smo_d12_domain,"An inverted clamp interval was silently used.");
}

void binary_and_display_bridges() {
    const std::uint32_t finite[]{0u,0x80000000u,0x3f800000u,0x3f000000u,0x3fc00000u,0x40000000u,
        0x3eaaaaabu,0x3f7fffffu,0x3f800001u,0x00800000u,0x00000001u,0x007fffffu,0x7f7fffffu};
    for(const auto bits:finite) {check_binary_bridge(bits);check_binary_bridge(bits|0x80000000u);}
    for(const auto bits:{0x7f800000u,0xff800000u,0x7fc00000u,0x7f800001u})check_binary_bridge(bits);
    for(const auto entry:{std::pair<std::string_view,unsigned>{"0.6",0},{"1.6",2},{"2.6",2},{"3.6",4},{"#.6",10},{"~.6",12},{"193",255}}) {
        std::uint32_t diagnostic=0;
        check(smo_f12_round_uint(smo_f12_parse(entry.first),diagnostic)==entry.second&&diagnostic==0,
            "Final display conversion broke exact ties-to-even or introduced a new arithmetic warning.");
    }
    auto warned=smo_f12_parse("1.6");warned.diagnostic=smo_d12_inexact;
    std::uint32_t inherited=0;
    check(smo_f12_round_uint(warned,inherited)==2&&inherited==smo_d12_inexact,"Display conversion dropped an existing warning.");
    for(const auto text:{"-1","194"}) {
        std::uint32_t diagnostic=0;(void)smo_f12_round_uint(smo_f12_parse(text),diagnostic);
        check((diagnostic&smo_d12_domain)!=0,"An out-of-channel-range quantity was silently clipped.");
    }
    // These checks concern only the explicitly floating DISPLAY bridge.
    // No engine arithmetic expectation or rounding oracle uses a float.
    check(smo_f12_comparison_value(smo_f12_parse("1.6"))==1.5L&&
        smo_f12_comparison_value(smo_f12_parse("-0.6"))==-0.5L,"The display-only host bridge changed an exactly binary-representable fraction.");
    check(std::isnan(smo_f12_comparison_value(SmoD12{})),"An invalid display bridge result was fabricated as a quantity.");
}

void scene_trajectory() {
    // Optional long CPU proof, deliberately outside the default CTest path.
    // Every quantity remains F12. Integer frame counters, hashes and pixel
    // indices are transport/control values, not floating physical quantities.
    constexpr std::uint32_t steps=7000;
    const SmoD12SceneWorkload work{16,64,36};
    auto state=smo_duodecimal_scene_initial(work,true);
    std::uint64_t contacts=0,changed_frames=0,ray_tests=0,inexact_scalars=0;
    const auto validate_body=[&](const SmoD12SceneBody& body,std::uint32_t frame,std::size_t index) {
        try {
            check((body.diagnostic&smo_d12_fatal_mask)==0,"The physical state has a fatal diagnostic.");
            for(const auto& value:body.position) {canonical(value,"A position lost canonical F12 encoding.");inexact_scalars+=(value.diagnostic&smo_d12_inexact)!=0;}
            canonical(body.radius,"A radius lost canonical F12 encoding.");
            for(const auto& value:body.velocity) {canonical(value,"A velocity lost canonical F12 encoding.");inexact_scalars+=(value.diagnostic&smo_d12_inexact)!=0;}
            check(std::all_of(body.reserved.begin(),body.reserved.end(),[](std::uint32_t word){return word==0;}),"A reserved body word changed.");
        }catch(const std::exception& failure) {
            throw std::runtime_error("Trajectory frame "+std::to_string(frame)+", body "+std::to_string(index)+
                ", diagnostic "+std::to_string(body.diagnostic)+": "+failure.what());
        }
    };
    const auto validate_ray=[&](std::uint32_t frame,std::uint32_t index) {
        const auto pixel=smo_duodecimal_scene_ray_reference(state,index,work,true);
        try {
            check((pixel.diagnostic&smo_d12_fatal_mask)==0,"A ray acquired a fatal diagnostic.");
            for(const auto& value:pixel.color)canonical(value,"A ray color lost canonical F12 encoding.");
            canonical(pixel.distance,"A ray distance lost canonical F12 encoding.");
            check(pixel.hit==std::numeric_limits<std::uint32_t>::max()||pixel.hit<work.bodies,"A ray returned an invalid body index.");
            check(pixel.shadow<=1&&pixel.tests>=work.bodies&&pixel.tests<=work.bodies*2-1,"Ray shadow/test metadata escaped its declared bounds.");
            check((pixel.bgra&0xff000000u)==0xff000000u,"A final ray has not completed opaque RGB packing.");
            check(std::all_of(pixel.reserved.begin(),pixel.reserved.end(),[](std::uint32_t word){return word==0;}),"A reserved ray word changed.");
            ray_tests+=pixel.tests;
        }catch(const std::exception& failure) {
            throw std::runtime_error("Trajectory frame "+std::to_string(frame)+", pixel "+std::to_string(index)+
                ", diagnostic "+std::to_string(pixel.diagnostic)+": "+failure.what());
        }
    };
    check(state.size()==work.bodies,"Initialization changed the trajectory's body count.");
    for(std::size_t index=0;index<state.size();++index)validate_body(state[index],0,index);
    for(std::uint32_t frame=1;frame<=steps;++frame) {
        auto next=smo_duodecimal_scene_step_reference(state,true);
        check(next.size()==state.size(),"A physical update changed the number of bodies.");
        bool changed=false;
        for(std::size_t index=0;index<next.size();++index) {
            validate_body(next[index],frame,index);contacts+=next[index].contacts;
            for(unsigned axis=0;axis<3;++axis)changed=changed||!equal_value(next[index].position[axis],state[index].position[axis])||
                !equal_value(next[index].velocity[axis],state[index].velocity[axis]);
        }
        changed_frames+=changed;state=std::move(next);
        // Match the GPU harness's one-based completed-frame numbering.
        // Pixel indices, independently, remain zero based.
        if(frame==15)validate_ray(frame,987);
        if(frame==45)validate_ray(frame,532);
        if(frame==6680)validate_ray(frame,0);
        if(frame%1000==0)std::cout<<"Trajectory: "<<frame<<"/"<<steps<<" physical updates, contacts="<<contacts<<'\n'<<std::flush;
    }
    for(std::uint32_t pixel=0;pixel<work.width*work.height;++pixel)validate_ray(steps,pixel);
    check(contacts>0,"The long physical trajectory never exercised a contact.");
    check(changed_frames==steps,"At least one trajectory frame did not change a position or velocity.");
    check(ray_tests>0,"The optional trajectory never exercised the real ray equations.");
    std::cout<<"Trajectory PASS: steps="<<steps<<", bodies="<<work.bodies<<", changed_frames="<<changed_frames
        <<", contacts="<<contacts<<", final_pixels="<<work.width*work.height<<", ray_tests="<<ray_tests
        <<", observed_inexact_scalars="<<inexact_scalars<<"; no fatal values.\n";
}
}

int main(int argc,char** argv) {
    try {
        const bool trajectory=argc==2&&std::string_view(argv[1])=="trajectory";
        if(argc!=1&&!trajectory)throw std::invalid_argument("Usage: smo_float12_cpu_tests [trajectory]");
        encoding_and_notation();rounding_boundaries();rational_arithmetic();
        rule_policies_and_powers();range_and_internal_exponents();geometry_composites();binary_and_display_bridges();
        if(trajectory)scene_trajectory();
        std::cout<<"PASS: floating radix12 CPU, "<<checks<<" exact integer/rational checks.\n";return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL: floating radix12 CPU after "<<checks<<" checks: "<<error.what()<<'\n';return 1;
    }
}

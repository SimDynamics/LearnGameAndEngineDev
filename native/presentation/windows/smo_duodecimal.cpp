#include "presentation/windows/smo_duodecimal.hpp"
#include "core/smo.hpp"

#include <algorithm>
#include <intrin.h>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using U64=unsigned __int64;
struct Wide {U64 low=0,high=0;};
constexpr U64 radix=smo_d12_radix,scale=smo_d12_scale,maximum=smo_d12_max_raw;
thread_local SmoD12Observer diagnostic_observer;
thread_local bool notifying_observer=false;
SmoD12 fail(std::uint32_t diagnostic){SmoD12 result;result.diagnostic=diagnostic;return result;}
bool empty(const SmoD12& value){return (value.digits[0]|value.digits[1]|value.digits[2]|value.digits[3])==0;}
std::uint32_t diagnostics(const SmoD12& value){auto diagnostic=value.diagnostic;
    if(value.negative>1||(empty(value)&&value.negative!=0))diagnostic|=smo_d12_encoding;
    for(const auto limb:value.digits)if(limb>=radix)diagnostic|=smo_d12_encoding;return diagnostic;}
U64 raw(const SmoD12& value){U64 result=0;for(int i=3;i>=0;--i)result=result*radix+value.digits[static_cast<std::size_t>(i)];return result;}
SmoD12 from_raw(U64 magnitude,std::uint32_t negative,std::uint32_t diagnostic=0){if(magnitude>maximum)return fail(diagnostic|smo_d12_overflow);
    SmoD12 result;result.negative=magnitude?negative:0;result.diagnostic=diagnostic;
    for(auto& limb:result.digits){limb=static_cast<std::uint32_t>(magnitude%radix);magnitude/=radix;}return result;}
Wide product(U64 a,U64 b){Wide result;result.low=_umul128(a,b,&result.high);return result;}
int compare(Wide a,Wide b){if(a.high!=b.high)return a.high<b.high?-1:1;if(a.low!=b.low)return a.low<b.low?-1:1;return 0;}
Wide subtract(Wide a,Wide b){const auto borrow=a.low<b.low;a.low-=b.low;a.high-=b.high+static_cast<U64>(borrow);return a;}
Wide add(Wide a,Wide b){const auto original=a.low;a.low+=b.low;a.high+=b.high+static_cast<U64>(a.low<original);return a;}
SmoD12 quotient(Wide numerator,U64 denominator,std::uint32_t negative,std::uint32_t diagnostic,bool nonzero){
    if(denominator==0)return fail(diagnostic|smo_d12_unresolved);
    if(numerator.high>=denominator)return fail(diagnostic|smo_d12_overflow);
    U64 remainder=0;auto value=_udiv128(numerator.high,numerator.low,denominator,&remainder);
    if(value>maximum)return fail(diagnostic|smo_d12_overflow);
    if(remainder){diagnostic|=smo_d12_inexact;const auto twice=remainder*2;
        if(twice>denominator||(twice==denominator&&(value&1)))++value;}
    if(nonzero&&value==0)return fail(diagnostic|smo_d12_underflow);return from_raw(value,negative,diagnostic);
}
SmoD12 magnitude_product(SmoD12 a,SmoD12 b,std::uint32_t negative){const auto left=raw(a),right=raw(b);
    return quotient(product(left,right),scale,negative,a.diagnostic|b.diagnostic,left!=0&&right!=0);}
SmoD12 magnitude_divide(SmoD12 a,SmoD12 b,std::uint32_t negative){return quotient(product(raw(a),scale),raw(b),negative,a.diagnostic|b.diagnostic,!empty(a));}
SmoD12 positive_power(SmoD12 base,unsigned degree){auto result=from_raw(scale,0,base.diagnostic);
    for(unsigned i=0;i<degree;++i){result=magnitude_product(result,base,0);if(!smo_d12_valid(result))return result;}return result;}
Wide squares_sum(SmoD12 x,SmoD12 y,SmoD12 z){return add(add(product(raw(x),raw(x)),product(raw(y),raw(y))),product(raw(z),raw(z)));}
SmoD12 square_root(Wide radicand,std::uint32_t diagnostic,U64 search_bound){
    // A radix8-limb overflow already implies an unrepresentable norm. The
    // independent binary128 oracle can detect it without losing that carry.
    if(compare(radicand,product(maximum+1,maximum+1))>=0)return fail(diagnostic|smo_d12_overflow);
    U64 low=0,high=search_bound,root=0;
    while(low<=high){const auto middle=(low+high)/2;
        if(compare(product(middle,middle),radicand)<=0){root=middle;low=middle+1;}else high=middle-1;}
    const auto remainder=subtract(radicand,product(root,root));auto flags=diagnostic;
    if(remainder.low||remainder.high)flags|=smo_d12_inexact;
    if(remainder.high||remainder.low>root)++root;return from_raw(root,0,flags);
}
int digit(char c){if(c>='0'&&c<='9')return c-'0';const smo::Notation notation;if(c==notation.ten)return 10;if(c==notation.eleven)return 11;return -1;}
}

bool smo_d12_valid(SmoD12 value){return (diagnostics(value)&smo_d12_fatal_mask)==0;}
SmoD12 smo_d12_zero(){return {};}
SmoD12 smo_d12_uint(std::uint32_t whole){if(whole>=scale)return fail(smo_d12_overflow);return from_raw(static_cast<U64>(whole)*scale,0);}
SmoD12 smo_d12_literal(std::uint32_t whole,std::uint32_t fraction,std::uint32_t count,std::uint32_t negative){
    if(count>8||negative>1)return fail(smo_d12_encoding);
    if(count<8&&(fraction>>(4*count))!=0)return fail(smo_d12_encoding);
    try{const auto integral=smo_rule_unpack12(whole);U64 fractional=smo_rule_unpack12(fraction);
        for(auto i=count;i<8;++i)fractional*=12;return from_raw(static_cast<U64>(integral)*scale+fractional,negative);
    }catch(const std::invalid_argument&){return fail(smo_d12_encoding);}
}
SmoD12 smo_d12_parse(std::string_view text){if(text.empty())throw std::invalid_argument("Literal duodecimal vacío.");std::uint32_t negative=0;
    if(text.front()=='-'||text.front()=='+'){negative=text.front()=='-'?1u:0u;text.remove_prefix(1);}
    if(text.empty())throw std::invalid_argument("Faltan cifras duodecimales.");const smo::Notation notation;
    if(text.size()==1&&text.front()==notation.twelve_alias)return from_raw(12*scale,negative);
    U64 integral=0,fractional=0;bool point=false,seen=false,nonzero=false;unsigned fractional_count=0;int rounding_digit=-1;bool rounding_tail=false;
    for(const auto c:text){if(c=='.'){if(point)throw std::invalid_argument("El literal duodecimal contiene dos puntos de fracción.");point=true;continue;}
        const auto value=digit(c);if(value<0)throw std::invalid_argument("Cifra duodecimal inválida o alias no independiente.");seen=true;nonzero=nonzero||value!=0;
        if(!point){integral=integral*12+static_cast<U64>(value);if(integral>=scale)return fail(smo_d12_overflow);}
        else if(fractional_count<8){fractional=fractional*12+static_cast<U64>(value);++fractional_count;}
        else if(rounding_digit<0)rounding_digit=value;else rounding_tail=rounding_tail||value!=0;
    }
    if(!seen)throw std::invalid_argument("El literal duodecimal necesita cifras.");
    for(auto i=fractional_count;i<8;++i)fractional*=12;
    auto value=integral*scale+fractional;std::uint32_t diagnostic=0;
    if(rounding_digit>0||rounding_tail){diagnostic|=smo_d12_inexact;
        if(rounding_digit>6||(rounding_digit==6&&(rounding_tail||(value&1))))++value;}
    if(nonzero&&value==0)return fail(diagnostic|smo_d12_underflow);return from_raw(value,negative,diagnostic);
}
std::string smo_d12_format(SmoD12 value){if(!smo_d12_valid(value))throw std::invalid_argument("No se puede mostrar un diagnóstico como un número duodecimal.");
    const auto magnitude=raw(value),whole=magnitude/scale;auto fraction=magnitude%scale;const smo::Notation notation;
    auto integer=whole;std::string result;do{const auto d=static_cast<unsigned>(integer%12);result.push_back(d<10?static_cast<char>('0'+d):d==10?notation.ten:notation.eleven);integer/=12;}while(integer);
    std::reverse(result.begin(),result.end());if(fraction){result+='.';U64 place=scale;
        for(unsigned i=0;i<8&&fraction;++i){place/=12;const auto d=static_cast<unsigned>(fraction/place);fraction%=place;
            result.push_back(d<10?static_cast<char>('0'+d):d==10?notation.ten:notation.eleven);}}
    if(value.negative)result.insert(result.begin(),'-');return result;
}
int smo_d12_compare(SmoD12 a,SmoD12 b){if(!smo_d12_valid(a)||!smo_d12_valid(b))return 2;
    if(a.negative!=b.negative)return a.negative?-1:1;const auto left=raw(a),right=raw(b);const auto order=left<right?-1:left>right?1:0;return a.negative?-order:order;}
SmoD12 smo_d12_abs(SmoD12 a){const auto diagnostic=diagnostics(a);if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);a.negative=0;return a;}
SmoD12 smo_d12_negate(SmoD12 a){const auto diagnostic=diagnostics(a);if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);if(!empty(a))a.negative^=1;return a;}
SmoD12 smo_d12_add(SmoD12 a,SmoD12 b){const auto diagnostic=diagnostics(a)|diagnostics(b);if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);
    const auto left=raw(a),right=raw(b);if(a.negative==b.negative)return from_raw(left+right,a.negative,diagnostic);
    return left>=right?from_raw(left-right,a.negative,diagnostic):from_raw(right-left,b.negative,diagnostic);}
SmoD12 smo_d12_sub(SmoD12 a,SmoD12 b){return smo_d12_add(a,smo_d12_negate(b));}
SmoD12 smo_d12_sqrt(SmoD12 a){const auto diagnostic=diagnostics(a);if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);
    if(a.negative)return fail(diagnostic|smo_d12_domain);
    return square_root(product(raw(a),scale),diagnostic,U64{1}<<44);
}
SmoD12 smo_d12_discriminant(SmoD12 h,SmoD12 a,SmoD12 c){const auto diagnostic=diagnostics(h)|diagnostics(a)|diagnostics(c);
    if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);
    if(a.negative||empty(a))return fail(diagnostic|smo_d12_domain);
    const auto squared=product(raw(h),raw(h)),weighted=product(raw(a),raw(c));Wide magnitude;std::uint32_t negative=0;
    if(c.negative)magnitude=add(squared,weighted);
    else if(compare(squared,weighted)>=0)magnitude=subtract(squared,weighted);
    else{magnitude=subtract(weighted,squared);negative=1;}
    return quotient(magnitude,scale,negative,diagnostic,magnitude.low!=0||magnitude.high!=0);
}
SmoD12 smo_d12_magnitude2(SmoD12 x,SmoD12 y,SmoD12 z){const auto diagnostic=diagnostics(x)|diagnostics(y)|diagnostics(z);
    if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);const auto sum=squares_sum(x,y,z);
    return quotient(sum,scale,0,diagnostic,sum.low!=0||sum.high!=0);
}
SmoD12 smo_d12_norm3(SmoD12 x,SmoD12 y,SmoD12 z){const auto diagnostic=diagnostics(x)|diagnostics(y)|diagnostics(z);
    if(diagnostic&smo_d12_fatal_mask)return fail(diagnostic);return square_root(squares_sum(x,y,z),diagnostic,maximum);
}
namespace {
SmoD12Result evaluate_rule(std::uint32_t operation,SmoD12 a,SmoD12 b,std::uint32_t profile){
    SmoD12Result result;result.primary=fail(smo_d12_operation);result.alternative=fail(smo_d12_domain);result.flags=profile?smo_rule_experimental:0;
    if((profile&~smo_rule_profile_mask)||(profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask){result.primary=fail(smo_d12_profile);return result;}
    const auto diagnostic=diagnostics(a)|diagnostics(b);if(diagnostic&smo_d12_fatal_mask){result.primary=fail(diagnostic);return result;}a.diagnostic=diagnostic;b.diagnostic=diagnostic;
    if(operation==0)result.primary=smo_d12_add(a,b);
    else if(operation==1)result.primary=smo_d12_sub(a,b);
    else if(operation==2){if(empty(a)||empty(b)){const auto other=empty(a)?b:a;auto zero=from_raw(0,0,diagnostic);
            result.primary=(profile&smo_rule_multiply_zero_conserves)?other:zero;result.alternative=(profile&smo_rule_multiply_zero_conserves)?zero:other;result.flags|=smo_rule_has_alternative;
        }else result.primary=magnitude_product(a,b,(profile&smo_rule_conventional_sign)?(a.negative^b.negative):(a.negative|b.negative));}
    else if(operation==3){if(empty(b)){const auto policy=profile&smo_rule_divide_zero_mask;if(policy==smo_rule_divide_zero_unresolved){result.primary=fail(diagnostic|smo_d12_unresolved);result.flags|=smo_rule_unresolved;return result;}
            const auto zero=from_raw(0,0,diagnostic);result.primary=policy==smo_rule_divide_zero_empty?zero:a;result.alternative=policy==smo_rule_divide_zero_empty?a:zero;result.flags|=smo_rule_has_alternative;
        }else if(empty(a))result.primary=from_raw(0,0,diagnostic);
        else result.primary=magnitude_divide(a,b,(profile&smo_rule_conventional_sign)?(a.negative^b.negative):(a.negative|b.negative));}
    else if(operation==4||operation==5){if(a.negative||empty(a)){result.primary=fail(diagnostic|smo_d12_domain);return result;}
        if(b.digits[0]||b.digits[1]||b.digits[3]||b.digits[2]>64){result.primary=fail(diagnostic|smo_d12_exponent);return result;}const auto exponent=b.digits[2];
        if(exponent==0){const auto zero=from_raw(0,0,diagnostic);result.primary=(profile&smo_rule_power_zero_conserves)?a:zero;result.alternative=(profile&smo_rule_power_zero_conserves)?zero:a;result.flags|=smo_rule_has_alternative;}
        else if(exponent==1&&(profile&smo_rule_unit_exponent_one))result.primary=from_raw(scale,b.negative,diagnostic);
        else{const auto degree=operation==4?exponent:exponent>=2?exponent-2:1;auto magnitude=positive_power(a,degree);
            if(!smo_d12_valid(magnitude)){result.primary=magnitude;return result;}
            if(operation==5&&exponent>2)magnitude=magnitude_divide(from_raw(scale,0),magnitude,0);
            result.primary=magnitude;if(!empty(result.primary))result.primary.negative=b.negative;}
    }else if(operation==9)result.primary=smo_d12_sqrt(a);
    if(smo_d12_valid(result.primary))result.flags|=smo_rule_has_primary;return result;
}
}
void smo_d12_observe(SmoD12Observer observer)noexcept{diagnostic_observer=std::move(observer);}
SmoD12Result smo_d12_evaluate(std::uint32_t operation,SmoD12 a,SmoD12 b,std::uint32_t profile){
    const auto result=evaluate_rule(operation,a,b,profile);
    if((result.primary.diagnostic&smo_d12_fatal_mask)&&diagnostic_observer&&!notifying_observer){
        notifying_observer=true;
        try{
            // Keep the current callback alive if it clears/replaces itself.
            const auto callback=diagnostic_observer;callback(operation,a,b,profile,result);
        }catch(...){/* Observation must never change arithmetic or its errors. */}
        notifying_observer=false;
    }
    return result;
}
SmoD12 smo_d12_mul(SmoD12 a,SmoD12 b){return smo_d12_evaluate(2,a,b).primary;}
SmoD12 smo_d12_div(SmoD12 a,SmoD12 b){return smo_d12_evaluate(3,a,b).primary;}
SmoD12 smo_d12_pdir(SmoD12 a,SmoD12 exponent){return smo_d12_evaluate(4,a,exponent).primary;}
SmoD12 smo_d12_pinv(SmoD12 a,SmoD12 exponent){return smo_d12_evaluate(5,a,exponent).primary;}
SmoD12 smo_d12_min(SmoD12 a,SmoD12 b){const auto order=smo_d12_compare(a,b);if(order==2)return fail(diagnostics(a)|diagnostics(b));auto result=order>0?b:a;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_max(SmoD12 a,SmoD12 b){const auto order=smo_d12_compare(a,b);if(order==2)return fail(diagnostics(a)|diagnostics(b));auto result=order<0?b:a;result.diagnostic|=a.diagnostic|b.diagnostic;return result;}
SmoD12 smo_d12_clamp(SmoD12 a,SmoD12 low,SmoD12 high){const auto order=smo_d12_compare(low,high);if(order==2||order>0)return fail(diagnostics(a)|diagnostics(low)|diagnostics(high)|smo_d12_domain);return smo_d12_min(smo_d12_max(a,low),high);}
}

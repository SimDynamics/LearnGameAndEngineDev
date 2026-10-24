#include "presentation/windows/smo_float12.hpp"
#include "core/smo.hpp"

#include <algorithm>
#include <intrin.h>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace lgaed::presentation {
namespace {
using U64=unsigned __int64;
constexpr U64 radix=smo_f12_radix,minimum=smo_f12_min_coefficient,limit=smo_f12_max_coefficient+1;
struct Wide {U64 low=0,high=0;};
struct Number {U64 coefficient=0;std::int64_t exponent=0;std::uint32_t negative=0,diagnostic=0;};
thread_local SmoF12Observer observer;
thread_local bool observing=false;

Number error(std::uint32_t diagnostic){Number result;result.diagnostic=diagnostic;return result;}
bool fatal(Number value){return (value.diagnostic&smo_d12_fatal_mask)!=0;}
Wide multiply(U64 a,U64 b){Wide result;result.low=_umul128(a,b,&result.high);return result;}
Wide multiply_small(Wide a,std::uint32_t b){Wide result;U64 carry=0;result.low=_umul128(a.low,b,&carry);result.high=a.high*b+carry;return result;}
Wide add_wide(Wide a,Wide b){const auto before=a.low;a.low+=b.low;a.high+=b.high+static_cast<U64>(a.low<before);return a;}
Wide subtract_wide(Wide a,Wide b){const auto borrow=a.low<b.low;a.low-=b.low;a.high-=b.high+static_cast<U64>(borrow);return a;}
int compare_wide(Wide a,Wide b){if(a.high!=b.high)return a.high<b.high?-1:1;return a.low<b.low?-1:a.low>b.low?1:0;}
Wide divide_small(Wide a,std::uint32_t denominator,U64& remainder){Wide quotient;quotient.high=a.high/denominator;
    quotient.low=_udiv128(a.high%denominator,a.low,denominator,&remainder);return quotient;}
U64 power12(unsigned count){U64 result=1;for(unsigned i=0;i<count;++i)result*=12;return result;}
Wide shifted(U64 coefficient,unsigned count){Wide result{coefficient,0};for(unsigned i=0;i<count;++i)result=multiply_small(result,12);return result;}
Number carry(Number value){if(value.coefficient==limit){value.coefficient=minimum;++value.exponent;}return value;}
Number normalize(Wide value,std::int64_t exponent,std::uint32_t negative,std::uint32_t diagnostic){
    if(value.low==0&&value.high==0)return Number{0,0,0,diagnostic};
    U64 guard=0;bool sticky=false;
    while(value.high!=0||value.low>=limit){U64 remainder=0;value=divide_small(value,12,remainder);sticky=sticky||guard!=0;guard=remainder;++exponent;}
    while(value.low<minimum){value.low*=12;--exponent;}
    Number result{value.low,exponent,negative,diagnostic};
    if(guard!=0||sticky){result.diagnostic|=smo_d12_inexact;if(guard>6||(guard==6&&(sticky||(result.coefficient&1))))++result.coefficient;}
    return carry(result);
}
Number integer(U64 value,std::uint32_t diagnostic=0){return normalize({value,0},0,0,diagnostic);}
Number decode(SmoF12 value){auto diagnostic=value.diagnostic;const auto encoded=value.digits[3]&0x1ffu;
    if((value.digits[3]&smo_f12_tag_mask)!=smo_f12_tag||encoded>384u||value.negative>1u)diagnostic|=smo_d12_encoding;
    for(std::size_t i=0;i<3;++i)if(value.digits[i]>=radix)diagnostic|=smo_d12_encoding;
    if(diagnostic&smo_d12_fatal_mask)return error(diagnostic);
    const U64 coefficient=value.digits[0]+static_cast<U64>(value.digits[1])*radix+static_cast<U64>(value.digits[2])*radix*radix;
    const auto exponent=static_cast<std::int64_t>(encoded)+smo_f12_min_exponent;
    if(coefficient==0){if(exponent!=0||value.negative!=0)diagnostic|=smo_d12_encoding;}
    else if(coefficient<minimum||coefficient>=limit)diagnostic|=smo_d12_encoding;
    if(diagnostic&smo_d12_fatal_mask)return error(diagnostic);
    return {coefficient,exponent,value.negative,diagnostic};
}
SmoF12 encode(Number value){SmoF12 result;result.digits[3]=smo_f12_tag|192u;result.diagnostic=value.diagnostic;
    if(fatal(value))return result;
    if(value.coefficient==0)return result;
    if(value.exponent<smo_f12_min_exponent){result.diagnostic|=smo_d12_underflow;return result;}
    if(value.exponent>smo_f12_max_exponent){result.diagnostic|=smo_d12_overflow;return result;}
    if(value.coefficient<minimum||value.coefficient>=limit||value.negative>1){result.diagnostic|=smo_d12_encoding;return result;}
    result.digits[3]=smo_f12_tag|static_cast<std::uint32_t>(value.exponent-smo_f12_min_exponent);result.negative=value.negative;
    for(std::size_t i=0;i<3;++i){result.digits[i]=static_cast<std::uint32_t>(value.coefficient%radix);value.coefficient/=radix;}return result;
}
int compare_magnitude(Number a,Number b){if(a.coefficient==0||b.coefficient==0)return a.coefficient==b.coefficient?0:a.coefficient==0?-1:1;
    if(a.exponent!=b.exponent)return a.exponent<b.exponent?-1:1;return a.coefficient<b.coefficient?-1:a.coefficient>b.coefficient?1:0;}
int compare_number(Number a,Number b){if(a.negative!=b.negative)return a.negative?-1:1;const auto order=compare_magnitude(a,b);return a.negative?-order:order;}
Number sum(Number a,Number b){const auto diagnostic=a.diagnostic|b.diagnostic;if(diagnostic&smo_d12_fatal_mask)return error(diagnostic);
    if(a.coefficient==0){b.diagnostic=diagnostic;return b;}if(b.coefficient==0){a.diagnostic=diagnostic;return a;}
    const auto difference=a.exponent>=b.exponent?a.exponent-b.exponent:b.exponent-a.exponent;
    if(difference>13){auto result=a.exponent>b.exponent?a:b;result.diagnostic=diagnostic|smo_d12_inexact;return result;}
    const auto exponent=std::min(a.exponent,b.exponent);
    const auto left=shifted(a.coefficient,static_cast<unsigned>(a.exponent-exponent)),right=shifted(b.coefficient,static_cast<unsigned>(b.exponent-exponent));
    if(a.negative==b.negative)return normalize(add_wide(left,right),exponent,a.negative,diagnostic);
    const auto order=compare_wide(left,right);return order>=0?normalize(subtract_wide(left,right),exponent,a.negative,diagnostic):normalize(subtract_wide(right,left),exponent,b.negative,diagnostic);
}
Number negate(Number value){if(value.coefficient)value.negative^=1;return value;}
Number difference(Number a,Number b){return sum(a,negate(b));}
Number product(Number a,Number b,bool conventional=false){const auto diagnostic=a.diagnostic|b.diagnostic;if(diagnostic&smo_d12_fatal_mask)return error(diagnostic);
    if(a.coefficient==0||b.coefficient==0)return Number{0,0,0,diagnostic};
    const auto negative=conventional?(a.negative^b.negative):(a.negative|b.negative);
    return normalize(multiply(a.coefficient,b.coefficient),a.exponent+b.exponent,negative,diagnostic);
}
Number quotient(Number a,Number b,bool conventional=false){const auto diagnostic=a.diagnostic|b.diagnostic;if(diagnostic&smo_d12_fatal_mask)return error(diagnostic);
    if(b.coefficient==0){a.diagnostic=diagnostic;return a;}if(a.coefficient==0)return Number{0,0,0,diagnostic};
    const unsigned shift=a.coefficient>=b.coefficient?11u:12u;const auto numerator=shifted(a.coefficient,shift);U64 remainder=0;
    const auto coefficient=_udiv128(numerator.high,numerator.low,b.coefficient,&remainder);
    Number result{coefficient,a.exponent-b.exponent-shift,conventional?(a.negative^b.negative):(a.negative|b.negative),diagnostic};
    if(remainder){result.diagnostic|=smo_d12_inexact;const auto twice=remainder*2;
        if(twice>b.coefficient||(twice==b.coefficient&&(result.coefficient&1)))++result.coefficient;}
    return carry(result);
}
Number square_root(Number a){if(fatal(a))return a;if(a.negative)return error(a.diagnostic|smo_d12_domain);if(a.coefficient==0)return a;
    const unsigned shift=a.exponent%2==0?12u:11u;const auto radicand=shifted(a.coefficient,shift);U64 low=minimum,high=limit-1,root=minimum;
    while(low<=high){const auto middle=(low+high)/2;if(compare_wide(multiply(middle,middle),radicand)<=0){root=middle;low=middle+1;}else high=middle-1;}
    const auto remainder=subtract_wide(radicand,multiply(root,root));Number result{root,(a.exponent-shift)/2,0,a.diagnostic};
    if(remainder.low||remainder.high)result.diagnostic|=smo_d12_inexact;
    if(remainder.high||remainder.low>root)++result.coefficient;return carry(result);
}
bool exponent_value(Number value,unsigned& magnitude){if(fatal(value)||compare_magnitude(value,integer(64))>0)return false;
    if(value.coefficient==0){magnitude=0;return true;}if(value.exponent>=0)return false;
    const auto count=static_cast<unsigned>(-value.exponent);if(count>12)return false;const auto denominator=power12(count);
    if(value.coefficient%denominator)return false;magnitude=static_cast<unsigned>(value.coefficient/denominator);return magnitude<=64;
}
Number positive_power(Number base,unsigned exponent){auto result=integer(1,base.diagnostic);
    for(unsigned i=0;i<exponent;++i)result=product(result,base);return result;
}
Number scalar_square(Number value){if(value.coefficient==0)return value;value.negative=0;return product(value,value);}
Number magnitude_squared(Number x,Number y,Number z){return sum(sum(scalar_square(x),scalar_square(y)),scalar_square(z));}
void combine(Number& a,Number& b){const auto diagnostic=a.diagnostic|b.diagnostic;a.diagnostic=diagnostic;b.diagnostic=diagnostic;}
void combine(Number& a,Number& b,Number& c){const auto diagnostic=a.diagnostic|b.diagnostic|c.diagnostic;a.diagnostic=diagnostic;b.diagnostic=diagnostic;c.diagnostic=diagnostic;}
SmoF12Result evaluate_rule(std::uint32_t operation,Number a,Number b,std::uint32_t profile){SmoF12Result result;
    result.primary=encode(error(smo_d12_operation));result.alternative=encode(error(smo_d12_domain));result.flags=profile?smo_rule_experimental:0u;
    if((profile&~smo_rule_profile_mask)||(profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask){result.primary=encode(error(smo_d12_profile));return result;}
    combine(a,b);if(fatal(a)){result.primary=encode(a);return result;}Number primary=error(smo_d12_operation),alternative=error(smo_d12_domain);bool has_alternative=false;
    if(operation==0)primary=sum(a,b);
    else if(operation==1)primary=difference(a,b);
    else if(operation==2){if(a.coefficient==0||b.coefficient==0){const auto other=a.coefficient==0?b:a;const Number zero{0,0,0,a.diagnostic};
            primary=(profile&smo_rule_multiply_zero_conserves)?other:zero;alternative=(profile&smo_rule_multiply_zero_conserves)?zero:other;has_alternative=true;
        }else primary=product(a,b,(profile&smo_rule_conventional_sign)!=0);}
    else if(operation==3){if(b.coefficient==0){const auto policy=profile&smo_rule_divide_zero_mask;
            if(policy==smo_rule_divide_zero_unresolved){result.primary=encode(error(a.diagnostic|smo_d12_unresolved));result.flags|=smo_rule_unresolved;return result;}
            const Number zero{0,0,0,a.diagnostic};primary=policy==smo_rule_divide_zero_empty?zero:a;alternative=policy==smo_rule_divide_zero_empty?a:zero;has_alternative=true;
        }else primary=quotient(a,b,(profile&smo_rule_conventional_sign)!=0);}
    else if(operation==4||operation==5){if(a.negative||a.coefficient==0){result.primary=encode(error(a.diagnostic|smo_d12_domain));return result;}
        unsigned exponent=0;if(!exponent_value(b,exponent)){result.primary=encode(error(a.diagnostic|smo_d12_exponent));return result;}
        if(exponent==0){const Number zero{0,0,0,a.diagnostic};primary=(profile&smo_rule_power_zero_conserves)?a:zero;alternative=(profile&smo_rule_power_zero_conserves)?zero:a;has_alternative=true;}
        else if(exponent==1&&(profile&smo_rule_unit_exponent_one)){primary=integer(1,a.diagnostic);primary.negative=b.negative;}
        else{const auto degree=operation==4?exponent:exponent>=2?exponent-2:1;primary=positive_power(a,degree);
            if(operation==5&&exponent>2)primary=quotient(integer(1),primary);primary.negative=b.negative;}
    }else if(operation==9)primary=square_root(a);
    result.primary=encode(primary);result.alternative=encode(alternative);
    if(has_alternative)result.flags|=smo_rule_has_alternative;if(smo_f12_valid(result.primary))result.flags|=smo_rule_has_primary;return result;
}
int numeral(char c){if(c>='0'&&c<='9')return c-'0';const smo::Notation notation;if(c==notation.ten)return 10;if(c==notation.eleven)return 11;return -1;}
Number digits_number(std::vector<unsigned> digits,std::int64_t scale,std::uint32_t negative){while(!digits.empty()&&digits.back()==0)digits.pop_back();if(digits.empty())return {};
    const auto count=digits.size();U64 coefficient=0;
    for(std::size_t i=0;i<12;++i)coefficient=coefficient*12+(i<count?digits[count-1-i]:0u);
    Number result{coefficient,scale+static_cast<std::int64_t>(count)-12,negative,0};
    if(count>12){const auto guard=digits[count-13];bool sticky=false;for(std::size_t i=0;i+13<count;++i)sticky=sticky||digits[i]!=0;
        if(guard||sticky){result.diagnostic|=smo_d12_inexact;if(guard>6||(guard==6&&(sticky||(coefficient&1))))++result.coefficient;}}
    return carry(result);
}
void multiply_digits(std::vector<unsigned>& digits,unsigned factor){unsigned carry_value=0;for(auto& digit:digits){const auto total=digit*factor+carry_value;digit=total%12;carry_value=total/12;}
    while(carry_value){digits.push_back(carry_value%12);carry_value/=12;}}
}

bool smo_f12_valid(SmoF12 value){return !fatal(decode(value));}
SmoF12 smo_f12_zero(){return encode({});}
SmoF12 smo_f12_uint(std::uint32_t value){return encode(integer(value));}
SmoF12 smo_f12_literal(std::uint32_t whole,std::uint32_t fraction,std::uint32_t count,std::uint32_t negative){if(count>8||negative>1)return encode(error(smo_d12_encoding));
    if(count<8&&(fraction>>(4*count))!=0)return encode(error(smo_d12_encoding));
    try{const auto integral=smo_rule_unpack12(whole),fractional=smo_rule_unpack12(fraction);const auto coefficient=static_cast<U64>(integral)*power12(count)+fractional;
        return encode(normalize({coefficient,0},-static_cast<std::int64_t>(count),negative,0));
    }catch(const std::invalid_argument&){return encode(error(smo_d12_encoding));}
}
SmoF12 smo_f12_parse(std::string_view text){if(text.empty())throw std::invalid_argument("Literal base12 vacío.");std::uint32_t negative=0;
    if(text.front()=='-'||text.front()=='+'){negative=text.front()=='-'?1u:0u;text.remove_prefix(1);}if(text.empty())throw std::invalid_argument("Faltan cifras base12.");
    const smo::Notation notation;if(text.size()==1&&text.front()==notation.twelve_alias){auto result=integer(12);result.negative=negative;return encode(result);}
    std::vector<unsigned> digits;bool point=false;std::int64_t fractional_count=0;
    for(const auto c:text){if(c=='.'){if(point)throw std::invalid_argument("Dos puntos de fracción base12.");point=true;continue;}
        const auto digit=numeral(c);if(digit<0)throw std::invalid_argument("Cifra base12 inválida o alias no independiente.");digits.push_back(static_cast<unsigned>(digit));if(point)++fractional_count;}
    if(digits.empty())throw std::invalid_argument("El literal base12 necesita cifras.");std::reverse(digits.begin(),digits.end());return encode(digits_number(std::move(digits),-fractional_count,negative));
}
std::string smo_f12_format(SmoF12 value){const auto number=decode(value);if(fatal(number))throw std::invalid_argument("El diagnóstico no es un número F12.");if(number.coefficient==0)return "0";
    const smo::Notation notation;auto coefficient=number.coefficient;std::string digits;
    do{const auto digit=static_cast<unsigned>(coefficient%12);digits.push_back(digit<10?static_cast<char>('0'+digit):digit==10?notation.ten:notation.eleven);coefficient/=12;}while(coefficient);
    std::reverse(digits.begin(),digits.end());const auto point=static_cast<std::int64_t>(digits.size())+number.exponent;std::string result;
    if(point<=0)result="0."+std::string(static_cast<std::size_t>(-point),'0')+digits;
    else if(point>=static_cast<std::int64_t>(digits.size()))result=digits+std::string(static_cast<std::size_t>(point-static_cast<std::int64_t>(digits.size())),'0');
    else{result=digits;result.insert(static_cast<std::size_t>(point),1,'.');}
    if(result.find('.')!=result.npos){while(result.back()=='0')result.pop_back();if(result.back()=='.')result.pop_back();}
    if(number.negative)result.insert(result.begin(),'-');return result;
}
std::uint64_t smo_f12_coefficient(SmoF12 value){const auto result=decode(value);if(fatal(result))throw std::invalid_argument("Paquete F12 inválido.");return result.coefficient;}
std::int32_t smo_f12_exponent(SmoF12 value){const auto result=decode(value);if(fatal(result))throw std::invalid_argument("Paquete F12 inválido.");return static_cast<std::int32_t>(result.exponent);}
int smo_f12_compare(SmoF12 a,SmoF12 b){const auto left=decode(a),right=decode(b);return fatal(left)||fatal(right)?2:compare_number(left,right);}
SmoF12 smo_f12_abs(SmoF12 value){auto number=decode(value);number.negative=0;return encode(number);}
SmoF12 smo_f12_negate(SmoF12 value){return encode(negate(decode(value)));}
SmoF12Result smo_f12_evaluate(std::uint32_t operation,SmoF12 a,SmoF12 b,std::uint32_t profile){const auto result=evaluate_rule(operation,decode(a),decode(b),profile);
    if((result.primary.diagnostic&smo_d12_fatal_mask)&&observer&&!observing){observing=true;try{const auto callback=observer;callback(operation,a,b,profile,result);}catch(...){}observing=false;}return result;
}
void smo_f12_observe(SmoF12Observer callback)noexcept{observer=std::move(callback);}
SmoF12 smo_f12_add(SmoF12 a,SmoF12 b){return smo_f12_evaluate(0,a,b).primary;}
SmoF12 smo_f12_sub(SmoF12 a,SmoF12 b){return smo_f12_evaluate(1,a,b).primary;}
SmoF12 smo_f12_mul(SmoF12 a,SmoF12 b){return smo_f12_evaluate(2,a,b).primary;}
SmoF12 smo_f12_div(SmoF12 a,SmoF12 b){return smo_f12_evaluate(3,a,b).primary;}
SmoF12 smo_f12_pdir(SmoF12 a,SmoF12 b){return smo_f12_evaluate(4,a,b).primary;}
SmoF12 smo_f12_pinv(SmoF12 a,SmoF12 b){return smo_f12_evaluate(5,a,b).primary;}
SmoF12 smo_f12_sqrt(SmoF12 a){return smo_f12_evaluate(9,a,smo_f12_zero()).primary;}
SmoF12 smo_f12_min(SmoF12 a,SmoF12 b){auto left=decode(a),right=decode(b);combine(left,right);if(fatal(left))return encode(left);return encode(compare_number(left,right)>0?right:left);}
SmoF12 smo_f12_max(SmoF12 a,SmoF12 b){auto left=decode(a),right=decode(b);combine(left,right);if(fatal(left))return encode(left);return encode(compare_number(left,right)<0?right:left);}
SmoF12 smo_f12_clamp(SmoF12 value,SmoF12 low,SmoF12 high){auto number=decode(value),lower=decode(low),upper=decode(high);combine(number,lower,upper);
    if(fatal(number))return encode(number);if(compare_number(lower,upper)>0)return encode(error(number.diagnostic|smo_d12_domain));
    if(compare_number(number,lower)<0)return encode(lower);if(compare_number(number,upper)>0)return encode(upper);return encode(number);
}
SmoF12 smo_f12_discriminant(SmoF12 h,SmoF12 a,SmoF12 c){auto projection=decode(h),coefficient=decode(a),constant_term=decode(c);combine(projection,coefficient,constant_term);
    if(fatal(projection))return encode(projection);if(coefficient.negative||coefficient.coefficient==0)return encode(error(projection.diagnostic|smo_d12_domain));
    return encode(difference(scalar_square(projection),product(coefficient,constant_term)));
}
SmoF12 smo_f12_magnitude2(SmoF12 x,SmoF12 y,SmoF12 z){auto a=decode(x),b=decode(y),c=decode(z);combine(a,b,c);if(fatal(a))return encode(a);return encode(magnitude_squared(a,b,c));}
SmoF12 smo_f12_norm3(SmoF12 x,SmoF12 y,SmoF12 z){auto a=decode(x),b=decode(y),c=decode(z);combine(a,b,c);if(fatal(a))return encode(a);
    const unsigned axes=static_cast<unsigned>(a.coefficient!=0)+static_cast<unsigned>(b.coefficient!=0)+static_cast<unsigned>(c.coefficient!=0);
    if(axes<=1){auto result=a.coefficient?a:b.coefficient?b:c;result.negative=0;return encode(result);}return encode(square_root(magnitude_squared(a,b,c)));
}
std::uint32_t smo_f12_round_uint(SmoF12 value,std::uint32_t& diagnostic){auto number=decode(value);diagnostic|=number.diagnostic;if(fatal(number))return 0;
    if(number.negative||compare_magnitude(number,integer(255))>0){diagnostic|=smo_d12_domain;return 0;}if(number.coefficient==0)return 0;
    if(number.exponent>=0){diagnostic|=smo_d12_domain;return 0;}const auto places=-number.exponent;if(places>12)return 0;
    const auto denominator=power12(static_cast<unsigned>(places));auto whole=number.coefficient/denominator;const auto remainder=number.coefficient%denominator;
    if(remainder*2>denominator||(remainder*2==denominator&&(whole&1)))++whole;return static_cast<std::uint32_t>(std::min<U64>(whole,255));
}
SmoF12 smo_f12_from_binary32_bits(std::uint32_t bits){const auto biased=(bits>>23)&255u,mantissa=bits&0x007fffffu,negative=bits>>31;
    if(biased==255)return encode(error(smo_d12_domain));const auto significand=biased?mantissa|0x00800000u:mantissa;if(significand==0)return smo_f12_zero();
    int exponent=biased?static_cast<int>(biased)-127-23:-149;std::vector<unsigned> digits;auto value=significand;
    do{digits.push_back(value%12);value/=12;}while(value);std::int64_t scale_exponent=0;
    if(exponent>=0){for(int i=0;i<exponent;++i)multiply_digits(digits,2);}
    else{const auto denominator_power=-exponent,count=(denominator_power+1)/2;for(int i=0;i<count;++i)multiply_digits(digits,3);
        if(denominator_power%2)multiply_digits(digits,2);scale_exponent=-count;}
    return encode(digits_number(std::move(digits),scale_exponent,negative));
}
long double smo_f12_comparison_value(SmoF12 value){const auto number=decode(value);if(fatal(number))return std::numeric_limits<long double>::quiet_NaN();
    auto result=static_cast<long double>(number.coefficient);if(number.exponent>=0){for(std::int64_t i=0;i<number.exponent;++i)result*=12.L;}
    else for(std::int64_t i=0;i>number.exponent;--i)result/=12.L;return number.negative?-result:result;
}
}

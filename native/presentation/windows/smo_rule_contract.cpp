#include "presentation/windows/smo_rule_contract.hpp"
#include "core/smo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
using Op=SmoRuleOperation;
constexpr std::uint32_t exponent_mask=0x7f800000u,mantissa_mask=0x007fffffu;
bool nonfinite(std::uint32_t bits){return (bits&exponent_mask)==exponent_mask;}
bool subnormal(std::uint32_t bits){return (bits&exponent_mask)==0&&(bits&mantissa_mask)!=0;}
SmoRuleResult failure(std::uint32_t diagnostic,std::uint32_t profile,bool unresolved=false){SmoRuleResult result;
    result.diagnostic=diagnostic;result.flags=(profile?smo_rule_experimental:0u)|(unresolved?smo_rule_unresolved:0u);return result;}
std::uint32_t bits(float value){return value==0?0u:std::bit_cast<std::uint32_t>(value);}
SmoRuleResult number(double value,std::uint32_t profile,bool nonzero=false){
    if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())return failure(smo_rule_overflow,profile);
    const auto narrowed=static_cast<float>(value);
    if((nonzero&&narrowed==0)||subnormal(std::bit_cast<std::uint32_t>(narrowed)))return failure(smo_rule_underflow,profile);
    SmoRuleResult result;result.primary_bits=bits(narrowed);result.flags=smo_rule_has_primary|(profile?smo_rule_experimental:0u);return result;
}
SmoRuleResult pair(float primary,float alternative,std::uint32_t profile){auto result=number(primary,profile);
    result.alternative_bits=bits(alternative);result.flags|=smo_rule_has_alternative;return result;}
SmoRuleResult select(SmoRuleResult result,std::uint32_t selection,std::uint32_t profile){
    if(result.diagnostic)return result;
    if(selection==0)return result;
    if(!(result.flags&smo_rule_has_alternative))return failure(smo_rule_missing_alternative,profile);
    result.primary_bits=result.alternative_bits;result.alternative_bits=smo_rule_nan;result.flags&=~smo_rule_has_alternative;
    if(result.flags&smo_rule_has_packed_alternative){result.packed_primary=result.packed_alternative;result.flags|=smo_rule_has_packed;}
    else result.flags&=~smo_rule_has_packed;
    result.packed_alternative=0;result.flags&=~smo_rule_has_packed_alternative;return result;
}
SmoRuleResult packed_number(std::uint32_t packed,std::uint32_t value,std::uint32_t profile){auto result=number(value,profile);
    result.packed_primary=packed;result.flags|=smo_rule_has_packed|smo_rule_representation_view;return result;}
bool packed_valid(std::uint32_t packed){for(unsigned i=0;i<8;++i)if(((packed>>(i*4))&15u)>11)return false;return true;}
SmoRuleResult representation(const SmoRuleInput& input){const auto operation=static_cast<Op>(input.operation);const auto profile=input.profile;
    if(!packed_valid(input.packed_left)||(operation!=Op::base12_scale&&!packed_valid(input.packed_right)))return failure(smo_rule_invalid_digit,profile);
    const auto left=smo_rule_unpack12(input.packed_left);
    if(operation==Op::base12_scale){if(input.factor>11)return failure(smo_rule_factor,profile);
        if(left==0||input.factor==0){const auto other=left==0?input.factor:left;const auto conserved=smo_rule_pack12(other);
            const bool conserve=(profile&smo_rule_multiply_zero_conserves)!=0;
            auto result=packed_number(conserve?conserved:0,conserve?other:0,profile);
            result.alternative_bits=bits(static_cast<float>(conserve?0:other));result.packed_alternative=conserve?0:conserved;
            result.flags|=smo_rule_has_alternative|smo_rule_has_packed_alternative;return result;
        }
        const auto product=static_cast<std::uint64_t>(left)*input.factor;
        if(product>smo_rule_packed_max_value)return failure(smo_rule_packed_overflow,profile);
        return packed_number(smo_rule_pack12(static_cast<std::uint32_t>(product)),static_cast<std::uint32_t>(product),profile);
    }
    const auto right=smo_rule_unpack12(input.packed_right);
    if(operation==Op::base12_add){const auto sum=static_cast<std::uint64_t>(left)+right;
        if(sum>smo_rule_packed_max_value)return failure(smo_rule_packed_overflow,profile);
        return packed_number(smo_rule_pack12(static_cast<std::uint32_t>(sum)),static_cast<std::uint32_t>(sum),profile);
    }
    // This operation represents an exact bounded unsigned rational. It is not
    // the scalar SMO divide operation and never fabricates a rational a/0.
    if(right==0)return failure(smo_rule_zero_denominator|smo_rule_division_unresolved,profile,true);
    const auto divisor=std::gcd(left,right);const auto numerator=left/divisor,denominator=right/divisor;
    // Match the support view's two FP32 conversions; exact packed fields remain
    // authoritative even when integers exceed FP32's exact-integer range.
    const float view=static_cast<float>(numerator)/static_cast<float>(denominator);
    auto result=number(view,profile);result.numerator=smo_rule_pack12(numerator);result.denominator=smo_rule_pack12(denominator);
    result.flags|=smo_rule_has_rational|smo_rule_representation_view;return result;
}
}

SmoRuleInput smo_rule_input(Op operation,float left,float right,std::uint32_t profile,std::uint32_t selection){
    SmoRuleInput result;result.operation=static_cast<std::uint32_t>(operation);result.profile=profile;
    result.left_bits=std::bit_cast<std::uint32_t>(left);result.right_bits=std::bit_cast<std::uint32_t>(right);result.selection=selection;return result;
}
SmoRuleInput smo_rule_input12(Op operation,std::string_view left,std::string_view right,std::uint32_t profile,std::uint32_t selection){
    const auto parse=[](std::string_view text){const auto value=smo::parse_number(text).value;
        if(std::abs(value)>std::numeric_limits<float>::max()||(value!=0&&std::abs(value)<std::numeric_limits<float>::min()))
            throw std::range_error("El literal SMO queda fuera del dominio normal FP32 del adaptador GPU.");
        return static_cast<float>(value);};return smo_rule_input(operation,parse(left),parse(right),profile,selection);
}
SmoRuleResult smo_rule_reference(const SmoRuleInput& input){const auto profile=input.profile;
    if((profile&~smo_rule_profile_mask)||(profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)return failure(smo_rule_invalid_profile,profile);
    if(input.selection>1)return failure(smo_rule_invalid_selection,profile);
    if(input.operation>static_cast<std::uint32_t>(Op::base12_rational))return failure(smo_rule_invalid_operation,profile);
    if(input.operation>=static_cast<std::uint32_t>(Op::base12_add))return select(representation(input),input.selection,profile);
    if(nonfinite(input.left_bits)||nonfinite(input.right_bits))return failure(smo_rule_nonfinite_input,profile);
    if(subnormal(input.left_bits)||subnormal(input.right_bits))return failure(smo_rule_subnormal,profile);
    const auto a=std::bit_cast<float>(input.left_bits),b=std::bit_cast<float>(input.right_bits);SmoRuleResult result;
    const auto operation=static_cast<Op>(input.operation);
    if(operation==Op::add)result=number(static_cast<double>(a)+b,profile,a!=-b);
    else if(operation==Op::subtract)result=number(static_cast<double>(a)-b,profile,a!=b);
    else if(operation==Op::multiply){if(a==0||b==0){const auto other=a==0?b:a;
            result=(profile&smo_rule_multiply_zero_conserves)?pair(other,0,profile):pair(0,other,profile);
        }else{const auto negative=(profile&smo_rule_conventional_sign)?((a<0)!=(b<0)):(a<0||b<0);
            result=number((negative?-1.0:1.0)*std::abs(static_cast<double>(a))*std::abs(static_cast<double>(b)),profile,true);}
    }else if(operation==Op::divide){if(b==0){const auto policy=profile&smo_rule_divide_zero_mask;
            if(policy==smo_rule_divide_zero_unresolved)return failure(smo_rule_division_unresolved,profile,true);
            result=policy==smo_rule_divide_zero_empty?pair(0,a,profile):pair(a,0,profile);
        }else if(a==0)result=number(0,profile);
        else{const auto negative=(profile&smo_rule_conventional_sign)?((a<0)!=(b<0)):(a<0||b<0);
            result=number((negative?-1.0:1.0)*std::abs(static_cast<double>(a))/std::abs(static_cast<double>(b)),profile,true);}
    }else{
        if(!(a>0))return failure(smo_rule_domain,profile);
        if(std::trunc(b)!=b||std::abs(b)>64)return failure(smo_rule_exponent,profile);
        const auto exponent=static_cast<unsigned>(std::abs(b));
        if(exponent==0)result=(profile&smo_rule_power_zero_conserves)?pair(a,0,profile):pair(0,a,profile);
        else if(exponent==1&&(profile&smo_rule_unit_exponent_one))result=number(b<0?-1.0:1.0,profile);
        else{
            const auto degree=operation==Op::direct_power?exponent:exponent>=2?exponent-2:1;
            const auto power=std::pow(static_cast<double>(a),static_cast<double>(degree));
            const auto denominator=number(power,profile,true);if(denominator.diagnostic)return denominator;
            const auto value=operation==Op::direct_power?power:exponent==1?static_cast<double>(a):exponent==2?1.0:1.0/power;
            result=number(b<0?-value:value,profile,true);
        }
    }
    return select(result,input.selection,profile);
}
std::uint32_t smo_rule_pack12(std::uint32_t value){if(value>smo_rule_packed_max_value)throw std::out_of_range("El entero no cabe en ocho cifras de base doce.");
    std::uint32_t packed=0;for(unsigned i=0;i<8;++i){packed|=(value%12)<<(i*4);value/=12;}return packed;}
std::uint32_t smo_rule_unpack12(std::uint32_t packed){if(!packed_valid(packed))throw std::invalid_argument("Una cifra empaquetada de base doce supera once.");
    std::uint32_t value=0;for(int i=7;i>=0;--i)value=value*12+((packed>>(i*4))&15u);return value;}
std::string smo_rule_format12(std::uint32_t packed){(void)smo_rule_unpack12(packed);const smo::Notation notation;std::string result;bool started=false;
    for(int i=7;i>=0;--i){const auto digit=(packed>>(i*4))&15u;if(!started&&digit==0&&i!=0)continue;started=true;
        result.push_back(digit<10?static_cast<char>('0'+digit):digit==10?notation.ten:notation.eleven);}return result;}
std::string smo_rule_operation_name(Op operation){switch(operation){case Op::add:return "suma";case Op::subtract:return "resta";
    case Op::multiply:return "producto";case Op::divide:return "división";case Op::direct_power:return "pdir";case Op::inverted_power:return "pinv";
    case Op::base12_add:return "base12_suma";case Op::base12_scale:return "base12_escala";case Op::base12_rational:return "base12_racional";}return "operación desconocida";}
std::string smo_rule_profile_name(std::uint32_t profile){if(profile==0)return "SMO actual";
    if((profile&~smo_rule_profile_mask)||(profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)return "perfil inválido";
    std::string result="EXPERIMENTAL";if(profile&smo_rule_conventional_sign)result+="; signos convencionales";
    if(profile&smo_rule_multiply_zero_conserves)result+="; producto cero conserva cantidad";
    if((profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_empty)result+="; división cero elige cero";
    if((profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_unresolved)result+="; división cero sin resolver";
    if(profile&smo_rule_power_zero_conserves)result+="; potencia cero conserva base";
    if(profile&smo_rule_unit_exponent_one)result+="; exponentes uno y menos uno dan uno y menos uno";return result;}
std::vector<SmoRuleNamedProfile> smo_rule_profiles(){std::vector<SmoRuleNamedProfile> result;
    for(const auto profile:{0u,1u,2u,4u,8u,16u,32u,33u,55u})result.push_back({smo_rule_profile_name(profile),profile});return result;}
std::string smo_rule_diagnostic_name(std::uint32_t diagnostic){if(diagnostic==0)return "válido";std::string result;
    const std::pair<std::uint32_t,const char*> labels[]={{smo_rule_nonfinite_input,"entrada no finita"},{smo_rule_invalid_profile,"perfil inválido"},
        {smo_rule_domain,"dominio no definido"},{smo_rule_exponent,"exponente no entero o fuera del límite"},{smo_rule_overflow,"fuera de rango FP32"},
        {smo_rule_underflow,"resultado no nulo fuera del mínimo normal FP32"},{smo_rule_invalid_digit,"cifra base12 inválida"},{smo_rule_packed_overflow,"más de ocho cifras base12"},
        {smo_rule_division_unresolved,"división por cero sin resolver; no evaluada"},{smo_rule_zero_denominator,"denominador racional cero"},
        {smo_rule_subnormal,"entrada subnormal no portable"},{smo_rule_missing_alternative,"no existe alternativa"},{smo_rule_invalid_operation,"operación inválida"},
        {smo_rule_invalid_selection,"selección inválida"},{smo_rule_factor,"factor de escala fuera de 0..11"}};
    for(const auto& [flag,label]:labels)if(diagnostic&flag){if(!result.empty())result+="; ";result+=label;}return result;}
std::vector<SmoRuleCase> smo_rule_cases(){std::vector<SmoRuleCase> result;
    const auto append=[&](std::string name,SmoRuleInput input){result.push_back({std::move(name),input});};
    const auto scalar=[&](Op operation,float a,float b,std::uint32_t profile=0,std::uint32_t selection=0){
        append(smo_rule_operation_name(operation)+"/perfil="+std::to_string(profile)+"/"+smo::format_number(a)+","+smo::format_number(b)+(selection?"/alternativa":""),
            smo_rule_input(operation,a,b,profile,selection));};
    for(std::uint32_t profile=0;profile<=smo_rule_profile_mask;++profile){if((profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)continue;
        for(const auto operation:{Op::add,Op::subtract,Op::multiply,Op::divide})for(const auto a:{-3.f,-1.f,0.f,1.f,3.f})for(const auto b:{-3.f,-1.f,0.f,1.f,3.f})scalar(operation,a,b,profile);
        for(const auto operation:{Op::multiply,Op::divide,Op::direct_power,Op::inverted_power}){scalar(operation,2,0,profile,1);scalar(operation,0,0,profile,1);}
        for(const auto operation:{Op::direct_power,Op::inverted_power})for(const auto a:{.5f,1.f,2.f,3.f,12.f})for(const auto n:{-64.f,-3.f,-2.f,-1.f,0.f,1.f,2.f,3.f,64.f})scalar(operation,a,n,profile);
    }
    for(const auto operation:{Op::direct_power,Op::inverted_power})for(int exponent=-64;exponent<=64;++exponent)for(const auto base:{2.f,12.f})scalar(operation,base,static_cast<float>(exponent));
    for(const auto operation:{Op::add,Op::subtract,Op::multiply,Op::divide,Op::direct_power,Op::inverted_power}){
        auto invalid=smo_rule_input(operation,1,1);invalid.left_bits=0x7fc00000u;append("NaN no se evalúa",invalid);
        invalid.left_bits=0x7f800000u;append("infinito no se evalúa",invalid);invalid.left_bits=1;append("subnormal explícito",invalid);
        scalar(operation,1,1,0,1);
    }
    for(const auto operation:{Op::direct_power,Op::inverted_power})for(const auto a:{-2.f,0.f,2.f})for(const auto n:{.5f,65.f,-65.f})scalar(operation,a,n);
    scalar(Op::multiply,std::numeric_limits<float>::max(),2);scalar(Op::divide,std::numeric_limits<float>::max(),.5f);
    scalar(Op::add,std::numeric_limits<float>::max(),std::numeric_limits<float>::max());scalar(Op::subtract,std::numeric_limits<float>::max(),-std::numeric_limits<float>::max());
    scalar(Op::multiply,std::numeric_limits<float>::min(),.5f);scalar(Op::divide,std::numeric_limits<float>::min(),2);
    scalar(Op::subtract,std::nextafter(std::numeric_limits<float>::min(),1.f),std::numeric_limits<float>::min());
    auto invalid=smo_rule_input(Op::add,1,1,0x100);append("perfil desconocido",invalid);invalid.profile=12;append("división política reservada",invalid);
    invalid.profile=0;invalid.selection=2;append("selección desconocida",invalid);invalid.selection=0;invalid.operation=99;append("operación desconocida",invalid);
    for(const auto profile:{0u,2u})for(const auto a:{0u,1u,11u,12u,143u,144u,123456u,smo_rule_packed_max_value}){
        for(const auto b:{0u,1u,11u,12u,144u,smo_rule_packed_max_value})for(const auto operation:{Op::base12_add,Op::base12_rational}){
            SmoRuleInput input;input.operation=static_cast<std::uint32_t>(operation);input.profile=profile;input.packed_left=smo_rule_pack12(a);input.packed_right=smo_rule_pack12(b);
            append(smo_rule_operation_name(operation)+"/"+smo_rule_format12(input.packed_left)+","+smo_rule_format12(input.packed_right),input);}
        for(std::uint32_t factor=0;factor<=12;++factor){SmoRuleInput input;input.operation=static_cast<std::uint32_t>(Op::base12_scale);input.profile=profile;
            input.packed_left=smo_rule_pack12(a);input.factor=factor;append("base12 escala con acarreo",input);
            if(a==0||factor==0){input.selection=1;append("base12 escala alternativa explícita",input);}}
    }
    for(const auto operation:{Op::base12_add,Op::base12_scale,Op::base12_rational}){SmoRuleInput input;input.operation=static_cast<std::uint32_t>(operation);input.packed_left=0xc;
        input.packed_right=1;input.factor=1;append("dígito doce rechazado",input);input.packed_left=1;input.packed_right=0xf;
        if(operation!=Op::base12_scale)append("segundo dígito inválido",input);}
    append("literal12 base10 real",smo_rule_input12(Op::direct_power,"10","2"));append("alias12 real",smo_rule_input12(Op::inverted_power,"@","3"));
    return result;
}
}

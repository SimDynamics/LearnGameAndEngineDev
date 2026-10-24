#include "presentation/windows/smo_rule_contract.hpp"
#include "presentation/windows/smo_float12.hpp"
#include "core/smo.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
std::uint64_t checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int operation(int a,int b,std::uint32_t profile){
    const auto result=smo_rule_reference(smo_rule_input(SmoRuleOperation::multiply,static_cast<float>(a),static_cast<float>(b),profile));
    check(result.diagnostic==0&&(result.flags&smo_rule_has_primary),"A bounded exact product lost its primary value.");
    return static_cast<int>(smo_rule_primary(result));
}
int independent_product(int a,int b,std::uint32_t profile){
    if(a==0||b==0)return profile&smo_rule_multiply_zero_conserves?(a==0?b:a):0;
    const auto magnitude=(a<0?-a:a)*(b<0?-b:b);
    const bool negative=profile&smo_rule_conventional_sign?((a<0)!=(b<0)):(a<0||b<0);
    return negative?-magnitude:magnitude;
}
void law_domains(){
    for(const auto profile:{0u,1u,2u,3u}){
        std::uint64_t distribution_counterexamples=0;
        for(int a=-3;a<=3;++a)for(int b=-3;b<=3;++b){
            check(operation(a,b,profile)==independent_product(a,b,profile),"The independent sign/zero contract changed.");
            check(operation(a,b,profile)==operation(b,a,profile),"The scalar primary product is not commutative.");
            const int identity=profile&smo_rule_multiply_zero_conserves?0:1;
            check(operation(a,identity,profile)==a,"The profile's declared primary identity changed.");
            for(int c=-3;c<=3;++c){
                check(operation(operation(a,b,profile),c,profile)==operation(a,operation(b,c,profile),profile),
                    "The exact primary product lost associativity in its bounded domain.");
                if(operation(a,b+c,profile)!=operation(a,b,profile)+operation(a,c,profile))++distribution_counterexamples;
            }
        }
        check(profile==1u?distribution_counterexamples==0:distribution_counterexamples>0,
            "A changed algebra was silently treated as an ordinary distributive product.");
        std::cout<<"law_profile="<<profile<<" exact_primary_associative=1 commutative=1 identity="
            <<(profile&smo_rule_multiply_zero_conserves?0:1)<<" distributivity_counterexamples="<<distribution_counterexamples<<'\n';
    }
    // A useful distinct operator: signed magnitudes with a sticky inhibition
    // flag. This is not an Euclidean dot product and never changes geometry.
    for(int a=1;a<=3;++a)for(int b=1;b<=3;++b)for(int c=1;c<=3;++c)for(unsigned flags=0;flags<8;++flags){
        const auto left=operation(operation(flags&1?-a:a,flags&2?-b:b,0),flags&4?-c:c,0);
        check(left==(flags?-a*b*c:a*b*c),"Sticky inhibition lost a negative input or changed its magnitude.");
    }
    const auto paused_position=12;
    check(paused_position+operation(7,0,1)==paused_position,"A conventional zero time step moved a particle.");
    check(paused_position+operation(7,0,3)!=paused_position,
        "The zero-conserving operator was incorrectly called an equivalent pause in velocity integration.");
    const auto left=lgaed::smo::multiply(lgaed::smo::multiply(2,0).value,3);
    const auto right=lgaed::smo::multiply(2,lgaed::smo::multiply(0,3).value);
    check(left.value==right.value&&left.alternative!=right.alternative,
        "Primary associativity was incorrectly promoted to equivalence of complete alternative values.");
    const auto retained_left=smo_rule_reference(smo_rule_input(SmoRuleOperation::multiply,6,0,2));
    const auto retained_right=smo_rule_reference(smo_rule_input(SmoRuleOperation::multiply,2,3,2));
    check(retained_left.primary_bits==retained_right.primary_bits&&retained_left.flags!=retained_right.flags,
        "Zero-conserving reassociation lost an observable alternative.");
    std::cout<<"sticky_inhibition_primary=(-1)^OR(signs)*product(magnitudes); zero_identity_is_a_distinct_operator; "
        <<"zero_dt_counterexample=12+mul_conserve(7,0)=19 (pause requires12).\n";
}
struct Rational{std::int64_t numerator,denominator;};
bool equal(Rational a,Rational b){return a.numerator*b.denominator==b.numerator*a.denominator;}
Rational reduced(Rational value){const auto gcd=std::gcd(value.numerator,value.denominator);return {value.numerator/gcd,value.denominator/gcd};}
Rational sum(Rational a,Rational b){return reduced({a.numerator*b.denominator+b.numerator*a.denominator,a.denominator*b.denominator});}
void fraction_grid(){
    // Independent exact integer/rational oracle, not a binary floating view.
    // Values x/S are authored on a 12^k grid. The output grid is refined rather
    // than rounded: 12*S. No per-operation string conversion or decimal value
    // takes part. The ordinary optimized control uses the SAME integer scale.
    constexpr std::array<std::int64_t,9> values{-10000,-145,-12,-1,0,1,12,145,10000};
    std::int64_t scale=1;
    for(unsigned level=0;level<4;++level,scale*=12){
        for(const auto a:values)for(const auto b:values){
            for(const auto divisor:{2,3,4,6,12}){
                const auto numerator=(divisor-1)*a+b;
                const Rational general{numerator,divisor*scale};
                const auto common_numerator=(12/divisor)*numerator;
                const Rational duodecimal_grid{common_numerator,12*scale};
                check(equal(general,duodecimal_grid),"A half/third/quarter/sixth interpolation changed its exact result.");
                check(equal(reduced(general),duodecimal_grid),"An exact rational control disagrees with the common scale.");
            }
            // dt=1/12: x/S + (v/S)*(1/12). Keep the refined scale explicitly.
            check(equal({12*a+b,12*scale},sum({a,scale},{b,12*scale})),"An exact grid integration changed its rational sum.");
            for(const auto c:values){
                check(equal({a+b+c,3*scale},{4*(a+b+c),12*scale}),"The barycentre lost an exact third.");
            }
        }
    }
    // Five is not a divisor of a power of12: treating it as an integer scale
    // shortcut would truncate. No hypothesis is approved by widening errors.
    check(!equal({1,5},{12/5,12}),"A fifth was silently advertised as exact on a one-digit duodecimal grid.");
    const auto third=smo_f12_parse("0.4"),sixth=smo_f12_parse("0.2");
    check(smo_f12_div(smo_f12_uint(1),smo_f12_uint(3))==third,"F12 did not retain the exact third.");
    check(smo_f12_add(sixth,sixth)==third,"Two exact sixths did not produce one third.");
    check(smo_f12_mul(third,smo_f12_uint(3))==smo_f12_uint(1),"The exact third did not reconstruct unity.");
    check(smo_f12_evaluate(3,smo_f12_uint(1),smo_f12_uint(5)).primary.diagnostic&smo_d12_inexact,
        "The nonterminating fifth lost its explicit inexact flag.");
    std::cout<<"exact_grid_cases: interpolation denominators2/3/4/6/12, barycentres, signed positions and refinements; "
        <<"same optimized integer control, no radix-only speed claim. Fifth remains inexact.\n";
}
}
int main(){try{law_domains();fraction_grid();std::cout<<"PASS "<<checks<<" exact law/domain checks; no GPU timing, FPS or universal algebra claim.\n";return 0;}
catch(const std::exception& error){std::cerr<<"FAIL SMO law exploration: "<<error.what()<<'\n';return 1;}}

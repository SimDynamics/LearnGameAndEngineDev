#include "presentation/windows/smo_rule_contract.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace lgaed::presentation;
using Operation=SmoRuleOperation;
std::uint64_t checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}

// Independent exact oracle. All quantities below are signed powers of two or
// zero, with exponents bounded to16. Integer rational products fit int64_t;
// neither pow(), floating arithmetic nor a rounded tolerance is the oracle.
struct Rational {std::int64_t numerator=0,denominator=1;};
Rational reduce(Rational value){const auto divisor=std::gcd(value.numerator,value.denominator);
    return {value.numerator/divisor,value.denominator/divisor};}
bool same(Rational a,Rational b){return a.numerator*b.denominator==b.numerator*a.denominator;}
Rational power_of_two(int exponent,bool negative=false){check(exponent>=-16&&exponent<=16,"The exact oracle exceeded its declared domain.");
    Rational value=exponent>=0?Rational{std::int64_t{1}<<exponent,1}:Rational{1,std::int64_t{1}<<-exponent};
    if(negative)value.numerator=-value.numerator;return value;}
Rational expected_power(Operation operation,int exponent,std::uint32_t profile){
    if(exponent==0)return profile&smo_rule_power_zero_conserves?Rational{2,1}:Rational{};
    const auto magnitude=exponent<0?-exponent:exponent;
    if(magnitude==1&&(profile&smo_rule_unit_exponent_one))return {exponent<0?-1:1,1};
    const int degree=operation==Operation::direct_power?magnitude:magnitude==1?1:2-magnitude;
    return power_of_two(degree,exponent<0);
}
Rational expected_product(Rational a,Rational b,std::uint32_t profile){
    if(a.numerator==0||b.numerator==0)return profile&smo_rule_multiply_zero_conserves?(a.numerator==0?b:a):Rational{};
    const bool negative=profile&smo_rule_conventional_sign?((a.numerator<0)!=(b.numerator<0)):(a.numerator<0||b.numerator<0);
    auto value=reduce({(a.numerator<0?-a.numerator:a.numerator)*(b.numerator<0?-b.numerator:b.numerator),a.denominator*b.denominator});
    if(negative)value.numerator=-value.numerator;return value;
}
std::uint32_t bits(Rational value){value=reduce(value);if(value.numerator==0)return 0u;
    const bool negative=value.numerator<0;auto numerator=negative?-value.numerator:value.numerator;
    auto denominator=value.denominator;int exponent=0;
    while(numerator>1&&(numerator%2)==0){numerator/=2;++exponent;}
    while(denominator>1&&(denominator%2)==0){denominator/=2;--exponent;}
    check(numerator==1&&denominator==1&&exponent>=-126&&exponent<=127,"The oracle result is not a normal exact binary power.");
    return (negative?0x80000000u:0u)|(static_cast<std::uint32_t>(exponent+127)<<23u);
}
SmoRuleResult power(Operation operation,int exponent,std::uint32_t profile){
    return smo_rule_reference(smo_rule_input(operation,2.0f,static_cast<float>(exponent),profile));
}
SmoRuleResult product(const SmoRuleResult& a,const SmoRuleResult& b,std::uint32_t profile){
    SmoRuleInput input{};input.operation=static_cast<std::uint32_t>(Operation::multiply);input.profile=profile;
    input.left_bits=a.primary_bits;input.right_bits=b.primary_bits;return smo_rule_reference(input);
}
void usable(const SmoRuleResult& value){check(value.diagnostic==0&&(value.flags&smo_rule_has_primary),"An exact bounded power/product lost its primary result.");}
constexpr std::array<std::uint32_t,8> profiles{0u,2u,16u,18u,32u,34u,48u,50u};

void pointwise_contract(){
    for(const auto profile:profiles)for(const auto operation:{Operation::direct_power,Operation::inverted_power})for(int exponent=-16;exponent<=16;++exponent){
        const auto actual=power(operation,exponent,profile);usable(actual);
        check(actual.primary_bits==bits(expected_power(operation,exponent,profile)),"A power disagrees with the exact integer/rational oracle.");
        if(exponent==0){
            check((actual.flags&smo_rule_has_alternative)!=0,"A zero exponent lost its explicit alternative.");
            const auto other=profile&smo_rule_power_zero_conserves?Rational{}:Rational{2,1};
            check(actual.alternative_bits==bits(other),"A zero-exponent alternative changed.");
            auto selected=smo_rule_input(operation,2.0f,0.0f,profile,1u);const auto alternative=smo_rule_reference(selected);usable(alternative);
            check(alternative.primary_bits==bits(other)&&(alternative.flags&smo_rule_has_alternative)==0,"Selecting an alternative did not consume that alternative.");
        }else{
            check((actual.flags&smo_rule_has_alternative)==0,"A nonzero exponent unexpectedly invented an alternative.");
            const auto selected=smo_rule_reference(smo_rule_input(operation,2.0f,static_cast<float>(exponent),profile,1u));
            check((selected.diagnostic&smo_rule_missing_alternative)!=0&&(selected.flags&smo_rule_has_primary)==0,"A missing alternative became a fabricated number.");
        }
    }
}

void direct_law_domains(){
    for(const auto profile:profiles){std::uint64_t holds=0,counterexamples=0,positive_counterexamples=0;
        for(int n=0;n<=8;++n)for(int m=0;m<=8;++m){
            const auto left=power(Operation::direct_power,n+m,profile),right=product(power(Operation::direct_power,n,profile),power(Operation::direct_power,m,profile),profile);
            usable(left);usable(right);
            const auto expected_left=expected_power(Operation::direct_power,n+m,profile);
            const auto expected_right=expected_product(expected_power(Operation::direct_power,n,profile),expected_power(Operation::direct_power,m,profile),profile);
            check(left.primary_bits==bits(expected_left)&&right.primary_bits==bits(expected_right),"The direct-law operands differ from the independent exact oracle.");
            const bool equal=left.primary_bits==right.primary_bits;
            check(equal==same(expected_left,expected_right),"A direct-power law was misclassified.");
            if(equal)++holds;else{++counterexamples;if(n>0&&m>0)++positive_counterexamples;}
        }
        if((profile&smo_rule_unit_exponent_one)==0)check(positive_counterexamples==0,"The positive-index exact direct law failed.");
        else check(positive_counterexamples>0,"The unit-exponent variant was silently called the usual direct-power law.");
        if(profile==smo_rule_multiply_zero_conserves)check(counterexamples==0,"The zero-primary/zero-identity direct extension failed.");
        else check(counterexamples>0,"A semantically different zero/unit profile was incorrectly declared a universal power law.");
        std::cout<<"direct_profile="<<profile<<" base=2 n,m=0..8 primary_law_holds="<<holds<<" expected_counterexamples="<<counterexamples
            <<" positive_index_counterexamples="<<positive_counterexamples<<'\n';
    }
    const auto default_left=power(Operation::direct_power,1,0),default_right=product(power(Operation::direct_power,0,0),power(Operation::direct_power,1,0),0);
    check(default_left.primary_bits==bits({2,1})&&default_right.primary_bits==0,"The documented default zero-exponent counterexample changed.");
    // Even the successful primary extension does not preserve full Number:
    // pdir(2,1) has no alternative, but mul_conserve(pdir(2,0),pdir(2,1)) does.
    const auto extended_left=power(Operation::direct_power,1,2),extended_right=product(power(Operation::direct_power,0,2),power(Operation::direct_power,1,2),2);
    check(extended_left.primary_bits==extended_right.primary_bits&&(extended_left.flags&smo_rule_has_alternative)==0&&(extended_right.flags&smo_rule_has_alternative)!=0,
        "A primary-only homomorphism was incorrectly promoted to alternative-preserving equivalence.");
}

void inverted_law_domains(){
    for(const auto profile:profiles){std::uint64_t interior=0,extended_counterexamples=0;
        // pinv(2,n)=2^(2-n) on n>=2. Therefore the correct index on
        // multiplication is n+m-2, not the ordinary n+m exponent law.
        for(int n=2;n<=8;++n)for(int m=2;m<=8;++m){
            const auto left=power(Operation::inverted_power,n+m-2,profile),right=product(power(Operation::inverted_power,n,profile),power(Operation::inverted_power,m,profile),profile);
            usable(left);usable(right);const auto expected=expected_product(expected_power(Operation::inverted_power,n,profile),expected_power(Operation::inverted_power,m,profile),profile);
            check(left.primary_bits==bits(expected)&&right.primary_bits==bits(expected),"The exact pinv shifted-index law failed inside n,m>=2.");++interior;
        }
        for(int n=0;n<=8;++n)for(int m=0;m<=8;++m){if(n+m<2)continue;
            const auto left=power(Operation::inverted_power,n+m-2,profile),right=product(power(Operation::inverted_power,n,profile),power(Operation::inverted_power,m,profile),profile);
            usable(left);usable(right);const auto expected_left=expected_power(Operation::inverted_power,n+m-2,profile);
            const auto expected_right=expected_product(expected_power(Operation::inverted_power,n,profile),expected_power(Operation::inverted_power,m,profile),profile);
            check(left.primary_bits==bits(expected_left)&&right.primary_bits==bits(expected_right),"The pinv extension differs from the independent exact oracle.");
            check((left.primary_bits==right.primary_bits)==same(expected_left,expected_right),"A pinv boundary law was misclassified.");
            if(left.primary_bits!=right.primary_bits)++extended_counterexamples;
        }
        check(extended_counterexamples>0,"The zero/unit pinv branches were silently folded into the shifted-index interior law.");
        std::cout<<"inverted_profile="<<profile<<" base=2 n,m=2..8 exact_shifted_law_cases="<<interior
            <<" boundary_counterexamples="<<extended_counterexamples<<'\n';
    }
}

void signed_indices(){
    // Negative direct indices reverse the sign, not the exponent. The usual
    // cancellation law is therefore deliberately inapplicable.
    const auto cancelled=power(Operation::direct_power,0,0),signed_product=product(power(Operation::direct_power,1,0),power(Operation::direct_power,-1,0),0);
    check(cancelled.primary_bits==0&&signed_product.primary_bits==bits({-4,1}),"The signed direct-index counterexample changed.");
    for(const auto operation:{Operation::direct_power,Operation::inverted_power})for(const auto profile:profiles){
        const auto negative=power(operation,-1,profile),positive=power(operation,1,profile);
        usable(negative);usable(positive);
        check((negative.primary_bits^positive.primary_bits)==0x80000000u,"Signed unit indices no longer differ only by sign.");
        const auto absorbing=product(negative,negative,profile&~smo_rule_conventional_sign);
        const auto conventional=product(negative,negative,profile|smo_rule_conventional_sign);
        usable(absorbing);usable(conventional);
        check((absorbing.primary_bits^conventional.primary_bits)==0x80000000u&&(absorbing.primary_bits&0x80000000u),"The authorized sign variant was treated as an equivalent optimization.");
    }
    std::cout<<"signed_index_counterexample: pdir(2,1+(-1))=0 versus mul(pdir(2,1),pdir(2,-1))=-4; "
        <<"unit variants and alternative selection remain explicit semantic choices.\n";
}
}

int main(){try{pointwise_contract();direct_law_domains();inverted_law_domains();signed_indices();
    std::cout<<"PASS "<<checks<<" exact bounded power-law checks; expected counterexamples are contract evidence, not defects; no GPU, timing or universal algebra claim.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL SMO power laws: "<<error.what()<<'\n';return 1;}}

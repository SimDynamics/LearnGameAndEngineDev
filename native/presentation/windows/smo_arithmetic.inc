// Shared HLSL/GLSL conformance arithmetic, ABI smo_rule_contract.hpp v1.
// REAL(uint) and BITS(float) are supplied by the API prefix. FP32 is bounded
// binary SUPPORT, not native radix12 hardware or a complete radix12 engine.
// Actual digit-wise radix12 addition/scale and exact rational representation
// are separate operations; their packed integers remain authoritative.
struct SmoRuleInput {uint operation;uint profile;uint left_bits;uint right_bits;uint packed_left;uint packed_right;uint factor;uint selection;};
struct SmoRuleResult {uint primary_bits;uint alternative_bits;uint flags;uint diagnostic;uint packed_primary;uint packed_alternative;uint numerator;uint denominator;};
// HLSL global const alone is an external uniform ($Globals); its initializer
// is not uploaded by these executors. Keep rule flags as shader literals.
#ifdef SMO_HLSL
static
#endif
const uint SMOR_PRIMARY=1u,SMOR_ALT=2u,SMOR_UNRESOLVED=4u,SMOR_PACKED=8u,SMOR_PACKED_ALT=16u,SMOR_RATIONAL=32u,SMOR_VIEW=64u,SMOR_EXPERIMENTAL=128u;
#ifdef SMO_HLSL
static
#endif
const uint SMOR_NONFINITE=1u,SMOR_PROFILE=2u,SMOR_DOMAIN=4u,SMOR_EXPONENT=8u,SMOR_OVERFLOW=16u,SMOR_UNDERFLOW=32u,
    SMOR_DIGIT=64u,SMOR_PACKED_OVERFLOW=128u,SMOR_DIV_UNRESOLVED=256u,SMOR_DENOMINATOR=512u,SMOR_SUBNORMAL=1024u,
    SMOR_MISSING_ALT=2048u,SMOR_OPERATION=4096u,SMOR_SELECTION=8192u,SMOR_FACTOR=16384u;
SmoRuleResult smo_rule_empty(uint profile){SmoRuleResult r;r.primary_bits=0x7fc00000u;r.alternative_bits=0x7fc00000u;r.flags=profile!=0u?SMOR_EXPERIMENTAL:0u;
    r.diagnostic=0u;r.packed_primary=0u;r.packed_alternative=0u;r.numerator=0u;r.denominator=0u;return r;}
SmoRuleResult smo_rule_fail(uint diagnostic,uint profile){SmoRuleResult r=smo_rule_empty(profile);r.diagnostic=diagnostic;return r;}
bool smo_rule_nonfinite(uint bits){return (bits&0x7f800000u)==0x7f800000u;}
bool smo_rule_subnormal(uint bits){return (bits&0x7f800000u)==0u&&(bits&0x007fffffu)!=0u;}
uint smo_rule_bits(float value){return value==0.0?0u:BITS(value);}
SmoRuleResult smo_rule_number(float value,uint profile,bool nonzero){uint bits=BITS(value);
    if(smo_rule_nonfinite(bits))return smo_rule_fail(SMOR_OVERFLOW,profile);
    if((nonzero&&value==0.0)||smo_rule_subnormal(bits))return smo_rule_fail(SMOR_UNDERFLOW,profile);
    SmoRuleResult r=smo_rule_empty(profile);r.primary_bits=smo_rule_bits(value);r.flags|=SMOR_PRIMARY;return r;}
SmoRuleResult smo_rule_pair(float primary,float alternate,uint profile){SmoRuleResult r=smo_rule_number(primary,profile,false);
    r.alternative_bits=smo_rule_bits(alternate);r.flags|=SMOR_ALT;return r;}
SmoRuleResult smo_rule_select(SmoRuleResult r,uint selection,uint profile){if(r.diagnostic!=0u||selection==0u)return r;
    if((r.flags&SMOR_ALT)==0u)return smo_rule_fail(SMOR_MISSING_ALT,profile);
    r.primary_bits=r.alternative_bits;r.alternative_bits=0x7fc00000u;r.flags&=~SMOR_ALT;
    if((r.flags&SMOR_PACKED_ALT)!=0u){r.packed_primary=r.packed_alternative;r.flags|=SMOR_PACKED;}else r.flags&=~SMOR_PACKED;
    r.packed_alternative=0u;r.flags&=~SMOR_PACKED_ALT;return r;}
bool smo_rule_packed_valid(uint packed_digits){for(uint i=0u;i<8u;++i)if(((packed_digits>>(i*4u))&15u)>11u)return false;return true;}
uint smo_rule_unpack12(uint packed_digits){uint value=0u;for(uint j=0u;j<8u;++j){uint i=7u-j;value=value*12u+((packed_digits>>(i*4u))&15u);}return value;}
uint smo_rule_pack12(uint value){uint packed_digits=0u;for(uint i=0u;i<8u;++i){packed_digits|=(value%12u)<<(i*4u);value/=12u;}return packed_digits;}
SmoRuleResult smo_rule_packed_number(uint packed_digits,uint profile){SmoRuleResult r=smo_rule_number(float(smo_rule_unpack12(packed_digits)),profile,false);
    r.packed_primary=packed_digits;r.flags|=SMOR_PACKED|SMOR_VIEW;return r;}
SmoRuleResult smo_rule_representation(SmoRuleInput item){uint op=item.operation,profile=item.profile;
    if(!smo_rule_packed_valid(item.packed_left)||(op!=7u&&!smo_rule_packed_valid(item.packed_right)))return smo_rule_fail(SMOR_DIGIT,profile);
    if(op==7u){if(item.factor>11u)return smo_rule_fail(SMOR_FACTOR,profile);
        if(item.packed_left==0u||item.factor==0u){uint other=item.packed_left==0u?smo_rule_pack12(item.factor):item.packed_left;
            bool conserve=(profile&2u)!=0u;SmoRuleResult r=smo_rule_packed_number(conserve?other:0u,profile);
            r.packed_alternative=conserve?0u:other;r.alternative_bits=smo_rule_bits(float(smo_rule_unpack12(r.packed_alternative)));r.flags|=SMOR_ALT|SMOR_PACKED_ALT;return r;}
        uint packed_digits=0u,carry=0u;
        for(uint i=0u;i<8u;++i){uint product=((item.packed_left>>(i*4u))&15u)*item.factor+carry;packed_digits|=(product%12u)<<(i*4u);carry=product/12u;}
        if(carry!=0u)return smo_rule_fail(SMOR_PACKED_OVERFLOW,profile);return smo_rule_packed_number(packed_digits,profile);
    }
    if(op==6u){uint packed_digits=0u,carry=0u;
        for(uint i=0u;i<8u;++i){uint sum=((item.packed_left>>(i*4u))&15u)+((item.packed_right>>(i*4u))&15u)+carry;
            packed_digits|=(sum%12u)<<(i*4u);carry=sum/12u;}
        if(carry!=0u)return smo_rule_fail(SMOR_PACKED_OVERFLOW,profile);return smo_rule_packed_number(packed_digits,profile);
    }
    uint numerator=smo_rule_unpack12(item.packed_left),denominator=smo_rule_unpack12(item.packed_right);
    if(denominator==0u){SmoRuleResult r=smo_rule_fail(SMOR_DENOMINATOR|SMOR_DIV_UNRESOLVED,profile);r.flags|=SMOR_UNRESOLVED;return r;}
    uint x=numerator,y=denominator;
    // Euclid is bounded by the 32-bit input domain; no floating calculation
    // participates in exact numerator/denominator reduction.
    for(uint step=0u;step<64u&&y!=0u;++step){uint remainder=x%y;x=y;y=remainder;}
    numerator/=x;denominator/=x;precise float view=float(numerator)/float(denominator);
    SmoRuleResult r=smo_rule_number(view,profile,false);r.numerator=smo_rule_pack12(numerator);r.denominator=smo_rule_pack12(denominator);
    r.flags|=SMOR_RATIONAL|SMOR_VIEW;return r;
}
SmoRuleResult smo_rule_positive_power(float base,uint exponent,uint profile){
    precise float result=1.0;precise float factor=base;uint remaining=exponent;
    for(uint step=0u;step<7u&&remaining!=0u;++step){
        if((remaining&1u)!=0u){result=result*factor;SmoRuleResult tested=smo_rule_number(result,profile,true);if(tested.diagnostic!=0u)return tested;}
        remaining>>=1u;if(remaining!=0u){factor=factor*factor;SmoRuleResult tested=smo_rule_number(factor,profile,true);if(tested.diagnostic!=0u)return tested;}
    }
    return smo_rule_number(result,profile,true);
}
SmoRuleResult smo_rule_evaluate(SmoRuleInput item){uint profile=item.profile;
    if((profile&~63u)!=0u||(profile&12u)==12u)return smo_rule_fail(SMOR_PROFILE,profile);
    if(item.selection>1u)return smo_rule_fail(SMOR_SELECTION,profile);
    if(item.operation>8u)return smo_rule_fail(SMOR_OPERATION,profile);
    if(item.operation>=6u)return smo_rule_select(smo_rule_representation(item),item.selection,profile);
    if(smo_rule_nonfinite(item.left_bits)||smo_rule_nonfinite(item.right_bits))return smo_rule_fail(SMOR_NONFINITE,profile);
    if(smo_rule_subnormal(item.left_bits)||smo_rule_subnormal(item.right_bits))return smo_rule_fail(SMOR_SUBNORMAL,profile);
    float a=REAL(item.left_bits),b=REAL(item.right_bits);SmoRuleResult r=smo_rule_empty(profile);
    if(item.operation==0u){precise float value=a+b;r=smo_rule_number(value,profile,a!=-b);}
    else if(item.operation==1u){precise float value=a-b;r=smo_rule_number(value,profile,a!=b);}
    else if(item.operation==2u){if(a==0.0||b==0.0){float other=a==0.0?b:a;if((profile&2u)!=0u)r=smo_rule_pair(other,0.0,profile);else r=smo_rule_pair(0.0,other,profile);}
        else{bool negative=(profile&1u)!=0u?((a<0.0)!=(b<0.0)):(a<0.0||b<0.0);precise float magnitude=abs(a)*abs(b);
            r=smo_rule_number(negative?-magnitude:magnitude,profile,true);}}
    else if(item.operation==3u){if(b==0.0){uint policy=profile&12u;
            if(policy==8u){r=smo_rule_fail(SMOR_DIV_UNRESOLVED,profile);r.flags|=SMOR_UNRESOLVED;return r;}
            if(policy==4u)r=smo_rule_pair(0.0,a,profile);else r=smo_rule_pair(a,0.0,profile);
        }else if(a==0.0)r=smo_rule_number(0.0,profile,false);
        else{bool negative=(profile&1u)!=0u?((a<0.0)!=(b<0.0)):(a<0.0||b<0.0);precise float magnitude=abs(a)/abs(b);
            r=smo_rule_number(negative?-magnitude:magnitude,profile,true);}}
    else{if(!(a>0.0))return smo_rule_fail(SMOR_DOMAIN,profile);
        if(trunc(b)!=b||abs(b)>64.0)return smo_rule_fail(SMOR_EXPONENT,profile);uint exponent=uint(abs(b));
        if(exponent==0u){if((profile&16u)!=0u)r=smo_rule_pair(a,0.0,profile);else r=smo_rule_pair(0.0,a,profile);}
        else if(exponent==1u&&(profile&32u)!=0u)r=smo_rule_number(b<0.0?-1.0:1.0,profile,false);
        else{uint degree=item.operation==4u?exponent:exponent>=2u?exponent-2u:1u;
            SmoRuleResult power=smo_rule_positive_power(a,degree,profile);if(power.diagnostic!=0u)return power;
            precise float value=item.operation==4u?REAL(power.primary_bits):exponent==1u?a:exponent==2u?1.0:1.0/REAL(power.primary_bits);
            r=smo_rule_number(b<0.0?-value:value,profile,true);
        }
    }
    return smo_rule_select(r,item.selection,profile);
}

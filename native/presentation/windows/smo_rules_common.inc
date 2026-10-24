// This control implements the SAME rule policy with ordinary operators.
// It is not ordinary real arithmetic pretending that SMO's different answers
// are interchangeable. Checked result/representation helpers share the ABI.
SmoRuleResult smo_ordinary_representation(SmoRuleInput item){
    uint profile=item.profile,op=item.operation;
    if(!smo_rule_packed_valid(item.packed_left)||(op!=7u&&!smo_rule_packed_valid(item.packed_right)))return smo_rule_fail(SMOR_DIGIT,profile);
    uint a=smo_rule_unpack12(item.packed_left),b=op==7u?item.factor:smo_rule_unpack12(item.packed_right);
    if(op==7u){if(b>11u)return smo_rule_fail(SMOR_FACTOR,profile);
        if(a==0u||b==0u){uint other=a==0u?b:a;bool conserve=(profile&2u)!=0u;
            SmoRuleResult r=smo_rule_packed_number(smo_rule_pack12(conserve?other:0u),profile);
            r.alternative_bits=smo_rule_bits(float(conserve?0u:other));r.packed_alternative=smo_rule_pack12(conserve?0u:other);r.flags|=SMOR_ALT|SMOR_PACKED_ALT;return r;}
        // The overflow guard is part of the ordinary optimized algorithm; it
        // avoids relying on unavailable portable 64-bit integer arithmetic.
        if(a>429981695u/b)return smo_rule_fail(SMOR_PACKED_OVERFLOW,profile);
        return smo_rule_packed_number(smo_rule_pack12(a*b),profile);
    }
    if(op==6u){if(a>429981695u-b)return smo_rule_fail(SMOR_PACKED_OVERFLOW,profile);
        return smo_rule_packed_number(smo_rule_pack12(a+b),profile);}
    if(b==0u){SmoRuleResult r=smo_rule_fail(SMOR_DENOMINATOR|SMOR_DIV_UNRESOLVED,profile);r.flags|=SMOR_UNRESOLVED;return r;}
    uint x=a,y=b;for(uint iteration=0u;iteration<64u&&y!=0u;++iteration){uint remainder=x%y;x=y;y=remainder;}
    a/=x;b/=x;precise float view=float(a)/float(b);SmoRuleResult r=smo_rule_number(view,profile,false);
    r.numerator=smo_rule_pack12(a);r.denominator=smo_rule_pack12(b);r.flags|=SMOR_RATIONAL|SMOR_VIEW;return r;
}
SmoRuleResult smo_ordinary_evaluate(SmoRuleInput item){
    uint profile=item.profile;
    if((profile&~63u)!=0u||(profile&12u)==12u)return smo_rule_fail(SMOR_PROFILE,profile);
    if(item.selection>1u)return smo_rule_fail(SMOR_SELECTION,profile);
    if(item.operation>8u)return smo_rule_fail(SMOR_OPERATION,profile);
    if(item.operation>=6u)return smo_rule_select(smo_ordinary_representation(item),item.selection,profile);
    if(smo_rule_nonfinite(item.left_bits)||smo_rule_nonfinite(item.right_bits))return smo_rule_fail(SMOR_NONFINITE,profile);
    if(smo_rule_subnormal(item.left_bits)||smo_rule_subnormal(item.right_bits))return smo_rule_fail(SMOR_SUBNORMAL,profile);
    float a=REAL(item.left_bits),b=REAL(item.right_bits);SmoRuleResult r=smo_rule_empty(profile);
    if(item.operation==0u){precise float value=a+b;r=smo_rule_number(value,profile,a!=-b);}
    else if(item.operation==1u){precise float value=a-b;r=smo_rule_number(value,profile,a!=b);}
    else if(item.operation==2u){if(a==0.0||b==0.0){float other=a==0.0?b:a;
            if((profile&2u)!=0u)r=smo_rule_pair(other,0.0,profile);else r=smo_rule_pair(0.0,other,profile);}
        else{precise float value=a*b;if((profile&1u)==0u&&a<0.0&&b<0.0)value=-value;r=smo_rule_number(value,profile,true);}}
    else if(item.operation==3u){if(b==0.0){uint policy=profile&12u;
            if(policy==8u){r=smo_rule_fail(SMOR_DIV_UNRESOLVED,profile);r.flags|=SMOR_UNRESOLVED;return r;}
            if(policy==4u)r=smo_rule_pair(0.0,a,profile);else r=smo_rule_pair(a,0.0,profile);}
        else if(a==0.0)r=smo_rule_number(0.0,profile,false);
        else{precise float value=a/b;if((profile&1u)==0u&&a<0.0&&b<0.0)value=-value;r=smo_rule_number(value,profile,true);}}
    else{if(!(a>0.0))return smo_rule_fail(SMOR_DOMAIN,profile);
        if(trunc(b)!=b||abs(b)>64.0)return smo_rule_fail(SMOR_EXPONENT,profile);uint exponent=uint(abs(b));
        if(exponent==0u){if((profile&16u)!=0u)r=smo_rule_pair(a,0.0,profile);else r=smo_rule_pair(0.0,a,profile);}
        else if(exponent==1u&&(profile&32u)!=0u)r=smo_rule_number(b<0.0?-1.0:1.0,profile,false);
        else{uint degree=item.operation==4u?exponent:exponent>=2u?exponent-2u:1u;
            SmoRuleResult power=smo_rule_positive_power(a,degree,profile);if(power.diagnostic!=0u)return power;
            precise float value=item.operation==4u?REAL(power.primary_bits):exponent==1u?a:exponent==2u?1.0:1.0/REAL(power.primary_bits);
            r=smo_rule_number(b<0.0?-value:value,profile,true);}}
    return smo_rule_select(r,item.selection,profile);
}
uint smo_rules_parameter(uint index){return parameters[index/4u][index%4u];}
void smo_rules_main(uint index){if(index>=smo_rules_parameter(0u))return;
    uint base=index*8u;SmoRuleInput item;
    item.operation=word(0u,base);item.profile=word(0u,base+1u);item.left_bits=word(0u,base+2u);item.right_bits=word(0u,base+3u);
    item.packed_left=word(0u,base+4u);item.packed_right=word(0u,base+5u);item.factor=word(0u,base+6u);item.selection=word(0u,base+7u);
#if MODE!=1
    // Benchmarks group an actual uniform operation/profile. The optimized
    // ordinary control receives the SAME specialization opportunity as C.
    // Conformance uses parameter4=0 and validates every per-item policy.
    if(smo_rules_parameter(4u)!=0u){item.operation=smo_rules_parameter(1u);item.profile=smo_rules_parameter(2u);item.selection=smo_rules_parameter(3u);}
#endif
#if MODE==0
    SmoRuleResult result=smo_ordinary_evaluate(item);
#else
    SmoRuleResult result=smo_rule_evaluate(item);
#endif
    put(1u,base,result.primary_bits);put(1u,base+1u,result.alternative_bits);put(1u,base+2u,result.flags);put(1u,base+3u,result.diagnostic);
    put(1u,base+4u,result.packed_primary);put(1u,base+5u,result.packed_alternative);put(1u,base+6u,result.numerator);put(1u,base+7u,result.denominator);
    // A division explicitly left unresolved never writes a numerical state.
    // The prior state remains in its GPU buffer; no native a/0 is evaluated.
    if(result.diagnostic==0u&&(result.flags&SMOR_PRIMARY)!=0u)put(2u,index,result.primary_bits);
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 index:SV_DispatchThreadID){smo_rules_main(index.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){smo_rules_main(gl_GlobalInvocationID.x);}
#endif

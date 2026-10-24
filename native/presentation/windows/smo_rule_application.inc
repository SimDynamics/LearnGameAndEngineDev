// ISOLATED application-policy experiment. This does not add a profile bit to
// the engine/rule ABI. b0 is SmoRuleInput[], b1 is private uint4 results,
// b2 is the actual destination, and b3 is its write-version counter.
// Private flags: 1=value written, 2=operation omitted (no numerical value).
// Parameters: count, operation(2/3), zero policy(0=zero,1=conserve,2=omit),
// sign(0=absorbing,1=ordinary). All variants implement the SAME policy.
uint smo_application_parameter(uint index){return parameters[index/4u][index%4u];}
void smo_application_result(uint index,uint value_bits,uint flags,uint diagnostic,uint version){
    uint at=index*4u;put(1u,at,value_bits);put(1u,at+1u,flags);
    put(1u,at+2u,diagnostic);put(1u,at+3u,version);
}
void smo_application_main(uint index){
    if(index>=smo_application_parameter(0u))return;
    uint at=index*8u,version=word(3u,index),policy=smo_application_parameter(2u);
    uint op=smo_application_parameter(1u),sign_policy=smo_application_parameter(3u);
    uint left_bits=word(0u,at+2u),right_bits=word(0u,at+3u);
    uint diagnostic=0u;
    if(op!=2u&&op!=3u)diagnostic=SMOR_OPERATION;
    else if(policy>2u||sign_policy>1u)diagnostic=SMOR_PROFILE;
    else if(smo_rule_nonfinite(left_bits)||smo_rule_nonfinite(right_bits))diagnostic=SMOR_NONFINITE;
    else if(smo_rule_subnormal(left_bits)||smo_rule_subnormal(right_bits))diagnostic=SMOR_SUBNORMAL;
    if(diagnostic!=0u){smo_application_result(index,0x7fc00000u,0u,diagnostic,version);return;}
    float a=REAL(left_bits),b=REAL(right_bits);
    bool zero_operation=op==2u?(a==0.0||b==0.0):b==0.0;
    if(zero_operation&&policy==2u){
        // In particular, do NOT write the old value back: even its version
        // remains unchanged. This is distinguishable from writing valid zero.
        smo_application_result(index,0x7fc00000u,2u,0u,version);return;
    }
    SmoRuleResult result=smo_rule_empty(0u);
    if(zero_operation){float other=op==2u?(a==0.0?b:a):a;
        result=smo_rule_number(policy==1u?other:0.0,0u,false);
    }else{
#if APP_MODE==0
        // Ordinary native control, optimized for precisely this application
        // contract. No native division by zero can reach this branch.
        precise float value=0.0;
        if(op==2u)value=a*b;else value=a/b;
        if(sign_policy==0u&&a<0.0&&b<0.0)value=-value;
        result=smo_rule_number(value,0u,a!=0.0&&b!=0.0);
#else
        SmoRuleInput item;item.operation=op;item.profile=sign_policy;
#if APP_MODE==1
        // A genuinely dynamic evaluator reads its per-element operation and
        // sign policy. The host verifies that these equal the control's ones.
        item.operation=word(0u,at);item.profile=word(0u,at+1u);
#endif
        item.left_bits=left_bits;item.right_bits=right_bits;
        item.packed_left=0u;item.packed_right=0u;item.factor=0u;item.selection=0u;
        result=smo_rule_evaluate(item);
#endif
    }
    if(result.diagnostic!=0u||(result.flags&SMOR_PRIMARY)==0u){
        smo_application_result(index,0x7fc00000u,0u,result.diagnostic,version);return;
    }
    put(2u,index,result.primary_bits);put(3u,index,version+1u);
    smo_application_result(index,result.primary_bits,1u,0u,version+1u);
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 index:SV_DispatchThreadID){smo_application_main(index.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;
void main(){smo_application_main(gl_GlobalInvocationID.x);}
#endif

// Exact raw-word conformance path for the software duodecimal engine.
// No float load, evaluation or conversion participates in this program.
SmoD12 d12_case_number(uint offset){SmoD12 n;
    n.digits=uint4(word(0u,offset),word(0u,offset+1u),word(0u,offset+2u),word(0u,offset+3u));
    n.negative=word(0u,offset+4u);n.diagnostic=word(0u,offset+5u);return n;}
void d12_case_store(uint offset,SmoD12 n){
    put(1u,offset,n.digits.x);put(1u,offset+1u,n.digits.y);put(1u,offset+2u,n.digits.z);put(1u,offset+3u,n.digits.w);
    put(1u,offset+4u,n.negative);put(1u,offset+5u,n.diagnostic);}
void d12_case_main(uint i){if(i>=parameters[0].x)return;uint offset=i*14u;
    SmoD12 a=d12_case_number(offset+2u),b=d12_case_number(offset+8u);
#if defined(SMO_FLOAT12) && (D12_ONLY_OPERATION==6 || D12_ONLY_OPERATION==7)
    // F12 CONFORMANCE FIXTURES ONLY, not new SMO arithmetic operators.
    // Slot6 transports a raw uint in a.digits.x; slot7 transports packed
    // whole/fraction/count/sign in a.digits.xyzw. These words are constructor
    // arguments, deliberately NOT a canonical F12 numeric packet. Both
    // variant passes exercise the constructor directly, never evaluate(6/7).
    SmoD12Result result;
#if D12_ONLY_OPERATION==6
    result.primary=smo_d12_uint(a.digits.x);
#else
    result.primary=smo_d12_literal(a.digits.x,a.digits.y,a.digits.z,a.digits.w);
#endif
    result.alternative=smo_d12_zero();result.alternative.diagnostic=4u;
    result.flags=smo_d12_valid(result.primary)?1u:0u;
#else
    SmoD12Result result=smo_d12_evaluate(word(0u,offset),a,b,word(0u,offset+1u));
    // Check the default specialized helpers as well as the policy dispatcher.
    // Scene compilation relies on these helpers to avoid expanding every
    // unrelated rule at every geometric operation; values must stay identical.
    if(parameters[0].y==1u&&word(0u,offset+1u)==0u){
#if D12_ONLY_OPERATION==0
        result.primary=smo_d12_add(a,b);
#elif D12_ONLY_OPERATION==1
        result.primary=smo_d12_sub(a,b);
#elif D12_ONLY_OPERATION==2
        result.primary=smo_d12_mul(a,b);
#elif D12_ONLY_OPERATION==3
        result.primary=smo_d12_div(a,b);
#elif D12_ONLY_OPERATION==4
        result.primary=smo_d12_pdir(a,b);
#elif D12_ONLY_OPERATION==5
        result.primary=smo_d12_pinv(a,b);
#elif D12_ONLY_OPERATION==9
        result.primary=smo_d12_sqrt(a);
#endif
    }
#endif
    d12_case_store(i*13u,result.primary);d12_case_store(i*13u+6u,result.alternative);put(1u,i*13u+12u,result.flags);
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){d12_case_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){d12_case_main(gl_GlobalInvocationID.x);}
#endif

// Isolated scaling experiment: exact same F12 multiplication semantics, no
// new SMO rule. A valid positive power of12 changes only positional exponent.
SmoD12 scale12_specialized(SmoD12 value,SmoD12 factor){
    uint diagnostic=smo_d12_diagnostics(value)|smo_d12_diagnostics(factor);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    // A normalized coefficient12^11 is (0,0,1728) in radix12^4 limbs.
    // Derive k from the validated factor itself; host hints cannot change math.
    if(factor.negative==0u&&factor.digits.x==0u&&factor.digits.y==0u&&factor.digits.z==1728u){
        F12Number number=f12_unpack(value);F12Number scale=f12_unpack(factor);
        number.exponent+=scale.exponent+11;number.diagnostic=diagnostic;
        return f12_pack(number);
    }
    // Other signs, zero factors and non-powers keep the current general rule.
    return smo_d12_mul(value,factor);
}
void scale12_main(uint index){if(index>=parameters[0].x)return;uint input_offset=index*16u,output_offset=index*8u;
#if SMO_SCALE12_STAGE==3
    uint different=0u;for(uint part=0u;part<8u;++part)different|=word(1u,output_offset+part)^word(2u,output_offset+part);
    if(different!=0u)atomic_max(parameters[0].y,1u);
#elif SMO_SCALE12_STAGE==2
    // Only a separately prepared uint32-safe integer subset reaches stage2.
    // This is its native fixed-scale control, not an F12 packet conversion.
    uint product=word(0u,input_offset+13u)*word(0u,input_offset+14u);
    put(1u,output_offset,product);for(uint part=1u;part<8u;++part)put(1u,output_offset+part,0u);
#else
    SmoD12 value=smo_d12_load(0u,input_offset),factor=smo_d12_load(0u,input_offset+6u);
#if SMO_SCALE12_STAGE==0
    SmoD12 result=smo_d12_mul(value,factor);
#else
    SmoD12 result=scale12_specialized(value,factor);
#endif
    smo_d12_store(1u,output_offset,result);put(1u,output_offset+6u,0u);put(1u,output_offset+7u,0u);
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){scale12_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){scale12_main(gl_GlobalInvocationID.x);}
#endif

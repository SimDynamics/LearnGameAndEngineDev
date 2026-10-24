// Clean exact-integer representation/grouping experiment. Quantity values
// are either native uint or eight radix12 digits packed directly in nibbles.
// No decode-to-binary, FP view, parser or encode roundtrip exists in a kernel.
uint ra_add(uint a,uint b,inout uint diagnostic){
#if SMO_RADIX_STAGE>=3
    uint result=0u,carry=0u;
    for(uint digit=0u;digit<8u;++digit){uint total=((a>>(digit*4u))&15u)+((b>>(digit*4u))&15u)+carry;
        uint next=total>=12u?1u:0u;uint value=total-next*12u;result|=value<<(digit*4u);carry=next;}
    if(carry!=0u)diagnostic|=1u;return result;
#else
    return a+b;
#endif
}
uint ra_mul(uint a,uint b,inout uint diagnostic){
#if SMO_RADIX_STAGE>=3
    uint result=0u,carry=0u;
    for(uint digit=0u;digit<8u;++digit){uint total=carry;
        for(uint left=0u;left<=digit;++left)total+=((a>>(left*4u))&15u)*((b>>((digit-left)*4u))&15u);
        carry=total/12u;result|=(total-carry*12u)<<(digit*4u);}
    // The prepared domain proves no discarded high products: term bases1..7
    // and exponents2/3, so every intermediate product is at most343.
    if(carry!=0u)diagnostic|=1u;return result;
#else
    return a*b;
#endif
}
uint ra_power(uint value,uint exponent,inout uint diagnostic){uint squared=ra_mul(value,value,diagnostic);
    if(exponent==3u)squared=ra_mul(squared,value,diagnostic);else if(exponent!=2u)diagnostic|=2u;return squared;}
uint3 ra_sum(uint group,inout uint diagnostic){uint3 sum=uint3(0u,0u,0u);
    for(uint term=0u;term<parameters[0].z;++term){uint offset=(group*parameters[0].z+term)*8u;uint power=word(1u,offset+3u);
        uint power_x=ra_power(word(1u,offset),power,diagnostic);uint power_y=ra_power(word(1u,offset+1u),power,diagnostic);uint power_z=ra_power(word(1u,offset+2u),power,diagnostic);
        sum.x=ra_add(sum.x,power_x,diagnostic);sum.y=ra_add(sum.y,power_y,diagnostic);sum.z=ra_add(sum.z,power_z,diagnostic);}
    return sum;}
void ra_store(uint buffer_id,uint index,uint3 value,uint group,uint diagnostic){uint offset=index*8u;
    put(buffer_id,offset,value.x);put(buffer_id,offset+1u,value.y);put(buffer_id,offset+2u,value.z);put(buffer_id,offset+3u,group);
    put(buffer_id,offset+4u,diagnostic);put(buffer_id,offset+5u,0u);put(buffer_id,offset+6u,0u);put(buffer_id,offset+7u,0u);
    // Identical validation in all four SUSTAINED variants, included in their
    // GPU timestamps. No atomic success counter/bottleneck: errors ONLY.
    if(buffer_id==3u&&parameters[0].w!=0u){uint different=0u;for(uint part=0u;part<8u;++part)different|=word(3u,offset+part)^word(4u,offset+part);
        if(different!=0u)atomic_wide(parameters[1].x*2u,1u);}}
void ra_main(uint index){
#if SMO_RADIX_STAGE==1 || SMO_RADIX_STAGE==4
    if(index>=parameters[0].y)return;uint diagnostic=0u;uint3 sum=ra_sum(index,diagnostic);ra_store(2u,index,sum,index,diagnostic);
#else
    if(index>=parameters[0].x)return;uint offset=index*8u,group=word(0u,offset+3u),diagnostic=0u;uint3 sum=uint3(0u,0u,0u);
    if(group>=parameters[0].y)diagnostic|=4u;
    else{
#if SMO_RADIX_STAGE==2 || SMO_RADIX_STAGE==5
        uint cache=group*8u;sum=uint3(word(2u,cache),word(2u,cache+1u),word(2u,cache+2u));diagnostic|=word(2u,cache+4u);
#else
        sum=ra_sum(group,diagnostic);
#endif
    }
    uint3 value=uint3(0u,0u,0u);value.x=ra_add(word(0u,offset),sum.x,diagnostic);value.y=ra_add(word(0u,offset+1u),sum.y,diagnostic);value.z=ra_add(word(0u,offset+2u),sum.z,diagnostic);
#if SMO_RADIX_STAGE<3
    if(value.x>429981695u||value.y>429981695u||value.z>429981695u)diagnostic|=1u;
#endif
    ra_store(3u,index,value,group,diagnostic);
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){ra_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){ra_main(gl_GlobalInvocationID.x);}
#endif

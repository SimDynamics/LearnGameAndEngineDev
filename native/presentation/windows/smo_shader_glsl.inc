#define float3 vec3
#define float4 vec4
#define uint3 uvec3
#define uint4 uvec4
#ifdef SMO_VULKAN
layout(push_constant) uniform Params {uvec4 parameters[4];};
#else
layout(std140,binding=8) uniform Params {uvec4 parameters[4];};
#endif
layout(std430,binding=0) buffer B0 {uint d0[];}; layout(std430,binding=1) buffer B1 {uint d1[];};
layout(std430,binding=2) buffer B2 {uint d2[];}; layout(std430,binding=3) buffer B3 {uint d3[];};
layout(std430,binding=4) buffer B4 {uint d4[];}; layout(std430,binding=5) buffer B5 {uint d5[];};
layout(std430,binding=6) buffer B6 {uint d6[];}; layout(std430,binding=7) buffer B7 {uint d7[];};
uint word(uint s,uint i){if(s==0u)return d0[i];if(s==1u)return d1[i];if(s==2u)return d2[i];if(s==3u)return d3[i];if(s==4u)return d4[i];if(s==5u)return d5[i];if(s==6u)return d6[i];return d7[i];}
void put(uint s,uint i,uint v){if(s==0u)d0[i]=v;else if(s==1u)d1[i]=v;else if(s==2u)d2[i]=v;else if(s==3u)d3[i]=v;else if(s==4u)d4[i]=v;else if(s==5u)d5[i]=v;else if(s==6u)d6[i]=v;else d7[i]=v;}
void atomic_add(uint i,uint v){atomicAdd(d7[i],v);}void atomic_xor(uint i,uint v){atomicXor(d7[i],v);}void atomic_max(uint i,uint v){atomicMax(d7[i],v);}
void atomic_wide(uint i,uint v){uint old=atomicAdd(d7[i],v);if(old>0xffffffffu-v)atomicAdd(d7[i+1u],1u);}
bool finite_value(float v){return !isnan(v)&&!isinf(v);}
#define SHARED shared
#define SYNC barrier()
#define BITS floatBitsToUint
#define REAL uintBitsToFloat
#define ISFINITE finite_value

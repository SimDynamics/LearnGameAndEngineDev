#define float3x float3
cbuffer Params : register(b0) { uint4 parameters[4]; };
RWByteAddressBuffer b0:register(u0); RWByteAddressBuffer b1:register(u1);
RWByteAddressBuffer b2:register(u2); RWByteAddressBuffer b3:register(u3);
RWByteAddressBuffer b4:register(u4); RWByteAddressBuffer b5:register(u5);
RWByteAddressBuffer b6:register(u6); RWByteAddressBuffer b7:register(u7);
uint word(uint s,uint i){uint a=i*4u;if(s==0u)return b0.Load(a);if(s==1u)return b1.Load(a);if(s==2u)return b2.Load(a);if(s==3u)return b3.Load(a);if(s==4u)return b4.Load(a);if(s==5u)return b5.Load(a);if(s==6u)return b6.Load(a);return b7.Load(a);}
void put(uint s,uint i,uint v){uint a=i*4u;if(s==0u)b0.Store(a,v);else if(s==1u)b1.Store(a,v);else if(s==2u)b2.Store(a,v);else if(s==3u)b3.Store(a,v);else if(s==4u)b4.Store(a,v);else if(s==5u)b5.Store(a,v);else if(s==6u)b6.Store(a,v);else b7.Store(a,v);}
void atomic_add(uint i,uint v){uint old;b7.InterlockedAdd(i*4u,v,old);}
void atomic_xor(uint i,uint v){uint old;b7.InterlockedXor(i*4u,v,old);}
void atomic_max(uint i,uint v){uint old;b7.InterlockedMax(i*4u,v,old);}
void atomic_wide(uint i,uint v){uint old;b7.InterlockedAdd(i*4u,v,old);if(old>0xffffffffu-v){uint ignored;b7.InterlockedAdd((i+1u)*4u,1u,ignored);}}
#define SHARED groupshared
#define SYNC GroupMemoryBarrierWithGroupSync()
#define BITS asuint
#define REAL asfloat
#define ISFINITE isfinite
#define SMO_HLSL 1

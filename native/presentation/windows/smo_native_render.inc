// Hardware binary32 preview. This is ORDINARY Euclidean geometry, not the
// provisional SMO sign algebra or the software F12 arithmetic profile.
// Explicit scalar operation trees + precise temporaries prevent FMA changes.
bool nr_valid(float x){uint bits=BITS(x)&0x7fffffffu;return bits<0x7f800000u&&(bits==0u||bits>=0x00800000u);}
void nr_fault(uint diagnostic){uint index=parameters[0].x*parameters[0].y;
#ifdef SMO_HLSL
    uint ignored;b5.InterlockedMax(index*4u,diagnostic,ignored);
#else
    atomicMax(d5[index],diagnostic);
#endif
}
void nr_check(float x,inout uint d){uint bits=BITS(x)&0x7fffffffu;if(bits>=0x7f800000u)d=max(d,9u);else if(bits!=0u&&bits<0x00800000u)d=max(d,10u);}
float nr_add(float a,float b,inout uint d){precise float r=a+b;nr_check(r,d);if(r==0.0&&a!=-b)d=max(d,10u);return r;}
float nr_sub(float a,float b,inout uint d){precise float r=a-b;nr_check(r,d);if(r==0.0&&a!=b)d=max(d,10u);return r;}
float nr_mul(float a,float b,inout uint d){precise float r=a*b;nr_check(r,d);if(r==0.0&&a!=0.0&&b!=0.0)d=max(d,10u);return r;}
float nr_div(float a,float b,inout uint d){if(b==0.0){d=max(d,2u);return 0.0;}precise float r=a/b;nr_check(r,d);if(r==0.0&&a!=0.0)d=max(d,10u);return r;}
float3 nr_load(uint buffer_id,uint offset){return float3(REAL(word(buffer_id,offset)),REAL(word(buffer_id,offset+1u)),REAL(word(buffer_id,offset+2u)));}
void nr_store(uint buffer_id,uint offset,float3 p){put(buffer_id,offset,BITS(p.x));put(buffer_id,offset+1u,BITS(p.y));put(buffer_id,offset+2u,BITS(p.z));}
void nr_check3(float3 p,inout uint d){nr_check(p.x,d);nr_check(p.y,d);nr_check(p.z,d);}
float3 nr_sub3(float3 a,float3 b,inout uint d){float3 result;result.x=nr_sub(a.x,b.x,d);result.y=nr_sub(a.y,b.y,d);result.z=nr_sub(a.z,b.z,d);return result;}
float nr_dot(float3 a,float3 b,inout uint d){float x=nr_mul(a.x,b.x,d),y=nr_mul(a.y,b.y,d),z=nr_mul(a.z,b.z,d);float xy=nr_add(x,y,d);return nr_add(xy,z,d);}
float3 nr_cross(float3 a,float3 b,inout uint d){float3 result;float p=nr_mul(a.y,b.z,d),q=nr_mul(a.z,b.y,d);result.x=nr_sub(p,q,d);
    p=nr_mul(a.z,b.x,d);q=nr_mul(a.x,b.z,d);result.y=nr_sub(p,q,d);p=nr_mul(a.x,b.y,d);q=nr_mul(a.y,b.x,d);result.z=nr_sub(p,q,d);return result;}
uint nr_channel(float color,inout uint d){nr_check(color,d);if(!nr_valid(color)||color<0.0||color>1.0){d=max(d,6u);return 0u;}
    float scaled=nr_mul(color,255.0,d);uint whole=uint(scaled);float remainder=nr_sub(scaled,float(whole),d);if(remainder>0.5||(remainder==0.5&&(whole&1u)!=0u))++whole;return whole;}
void nr_setup(){uint frame=parameters[1].z,d=0u,phase=parameters[1].w;
    if(phase==0u){for(uint axis=0u;axis<3u;++axis){float angle=REAL(word(2u,frame+axis));nr_check(angle,d);float sine=0.0,cosine=1.0;
            if(nr_valid(angle)&&abs(angle)<=1000000.0){sine=sin(angle);cosine=cos(angle);nr_check(sine,d);nr_check(cosine,d);if(sine==0.0&&angle!=0.0&&abs(angle)<1.0)d=max(d,10u);}else d=max(d,7u);
            put(4u,axis*2u,BITS(sine));put(4u,axis*2u+1u,BITS(cosine));}
        uint red=nr_channel(REAL(word(2u,frame+3u)),d),green=nr_channel(REAL(word(2u,frame+4u)),d),blue=nr_channel(REAL(word(2u,frame+5u)),d);
        if(word(2u,frame+6u)!=parameters[0].z||word(2u,frame+7u)!=parameters[0].w||word(2u,frame+9u)!=0u)d=max(d,7u);
        put(4u,6u,0xff000000u|(red<<16u)|(green<<8u)|blue);put(4u,7u,d);put(4u,8u,word(2u,frame+8u));put(5u,parameters[0].x*parameters[0].y,d);
    }else{d=word(4u,7u);for(uint vertex=0u;vertex<parameters[0].z;++vertex)d=max(d,word(3u,vertex*4u+3u));
        for(uint face=0u;face<parameters[0].w;++face){uint at=parameters[1].y+face*3u;if(word(1u,at)>=parameters[0].z||word(1u,at+1u)>=parameters[0].z||word(1u,at+2u)>=parameters[0].z)d=max(d,5u);}
        put(4u,7u,d);put(5u,parameters[0].x*parameters[0].y,d);}}
void nr_transform(uint index){if(index>=parameters[0].z)return;float3 p=nr_load(0u,parameters[1].x+index*3u);uint d=word(4u,7u);nr_check3(p,d);
    if(d==0u){float sx=REAL(word(4u,0u)),cx=REAL(word(4u,1u)),sy=REAL(word(4u,2u)),cy=REAL(word(4u,3u)),sz=REAL(word(4u,4u)),cz=REAL(word(4u,5u));
        float first=nr_mul(p.y,cx,d),second=nr_mul(p.z,sx,d);float y=nr_sub(first,second,d);first=nr_mul(p.y,sx,d);second=nr_mul(p.z,cx,d);float z=nr_add(first,second,d);p.y=y;p.z=z;
        first=nr_mul(p.x,cy,d);second=nr_mul(p.z,sy,d);float x=nr_add(first,second,d);first=nr_mul(p.z,cy,d);second=nr_mul(p.x,sy,d);z=nr_sub(first,second,d);p.x=x;p.z=z;
        first=nr_mul(p.x,cz,d);second=nr_mul(p.y,sz,d);x=nr_sub(first,second,d);first=nr_mul(p.x,sz,d);second=nr_mul(p.y,cz,d);y=nr_add(first,second,d);p.x=x;p.y=y;p.z=nr_add(p.z,4.0,d);}
    nr_store(3u,index*4u,p);put(3u,index*4u+3u,d);}
// Per-face16words: e1[0..2],e2[3..5],t[6..8],q[9..11],numerator12,
// base/edge diagnostic13,q-only diagnostic14,numerator-only diagnostic15.
// q/n faults remain deferred until their original u/v branches are reached.
void nr_prepare_face(uint index){if(index>=parameters[0].w)return;uint offset=index*16u,source=parameters[1].y+index*3u,ia=word(1u,source),ib=word(1u,source+1u),ic=word(1u,source+2u);
    uint d=0u,dq=0u,dn=0u;float3 e1=float3(0.0,0.0,0.0),e2=e1,t=e1,q=e1;float numerator=0.0;
    if(ia<parameters[0].z&&ib<parameters[0].z&&ic<parameters[0].z){float3 a=nr_load(3u,ia*4u),b=nr_load(3u,ib*4u),c=nr_load(3u,ic*4u);nr_check3(a,d);nr_check3(b,d);nr_check3(c,d);
        e1=nr_sub3(b,a,d);e2=nr_sub3(c,a,d);t=-a;q=nr_cross(t,e1,dq);numerator=nr_dot(e2,q,dn);
    }else d=5u;
    nr_store(6u,offset,e1);nr_store(6u,offset+3u,e2);nr_store(6u,offset+6u,t);nr_store(6u,offset+9u,q);put(6u,offset+12u,BITS(numerator));put(6u,offset+13u,d);put(6u,offset+14u,dq);put(6u,offset+15u,dn);}
struct NrHit{float depth;uint hit;uint diagnostic;};
NrHit nr_triangle(float3 direction,uint face){NrHit result;result.depth=0.0;result.hit=0u;
#if SMO_NATIVE_RENDER_STAGE==4
    result.diagnostic=0u;uint at=parameters[1].y+face*3u,ia=word(1u,at),ib=word(1u,at+1u),ic=word(1u,at+2u);
    if(ia>=parameters[0].z||ib>=parameters[0].z||ic>=parameters[0].z){result.diagnostic=5u;return result;}
    float3 a=nr_load(3u,ia*4u),b=nr_load(3u,ib*4u),c=nr_load(3u,ic*4u);nr_check3(a,result.diagnostic);nr_check3(b,result.diagnostic);nr_check3(c,result.diagnostic);
    float3 e1=nr_sub3(b,a,result.diagnostic),e2=nr_sub3(c,a,result.diagnostic);
#else
    uint offset=face*16u;result.diagnostic=word(6u,offset+13u);float3 e1=nr_load(6u,offset),e2=nr_load(6u,offset+3u);
#endif
    float3 p=nr_cross(direction,e2,result.diagnostic);float determinant=nr_dot(e1,p,result.diagnostic);
    if(result.diagnostic==0u&&determinant!=0.0){float inverse=nr_div(1.0,determinant,result.diagnostic);
#if SMO_NATIVE_RENDER_STAGE==4
        float3 t=-a;
#else
        float3 t=nr_load(6u,offset+6u);
#endif
        float tu=nr_dot(t,p,result.diagnostic),u=nr_mul(tu,inverse,result.diagnostic);
        if(nr_valid(u)&&u>=0.0&&u<=1.0){
#if SMO_NATIVE_RENDER_STAGE==4
            float3 q=nr_cross(t,e1,result.diagnostic);
#else
            result.diagnostic=max(result.diagnostic,word(6u,offset+14u));float3 q=nr_load(6u,offset+9u);
#endif
            float v0=nr_dot(direction,q,result.diagnostic),v=nr_mul(v0,inverse,result.diagnostic),sum=nr_add(u,v,result.diagnostic);
            if(nr_valid(v)&&v>=0.0&&sum<=1.0){
#if SMO_NATIVE_RENDER_STAGE==4
                float numerator=nr_dot(e2,q,result.diagnostic);
#else
                result.diagnostic=max(result.diagnostic,word(6u,offset+15u));float numerator=REAL(word(6u,offset+12u));
#endif
                float depth=nr_mul(numerator,inverse,result.diagnostic);
                if(nr_valid(depth)&&depth>=REAL(0x3daaaaabu)&&depth<=64.0){result.depth=depth;result.hit=1u;}}}}
    return result;}
void nr_raster(uint pixel){uint width=parameters[0].x,height=parameters[0].y;if(pixel>=width*height)return;uint d=word(4u,7u),face=0xffffffffu,color=0xff101820u;float depth=0.0;
    if(word(4u,8u)!=0u&&d==0u){uint x=pixel%width,y=pixel/width;float cx=nr_div(float(width),2.0,d),cy=nr_div(float(height),2.0,d),focal=nr_mul(float(min(width,height)),1.5,d);
        float px=nr_add(float(x),0.5,d);px=nr_sub(px,cx,d);px=nr_div(px,focal,d);float py=nr_add(float(y),0.5,d);py=nr_sub(cy,py,d);py=nr_div(py,focal,d);float3 direction=float3(px,py,1.0);
        for(uint i=0u;i<parameters[0].w;++i){NrHit hit=nr_triangle(direction,i);d=max(d,hit.diagnostic);if(hit.hit!=0u&&(face==0xffffffffu||hit.depth<depth)){depth=hit.depth;face=i;}}
        if(face!=0xffffffffu)color=word(4u,6u);}
    if(d!=0u){color=0xffff00ffu;nr_fault(d);}put(5u,pixel,color);}
void nr_main(uint index){
#if SMO_NATIVE_RENDER_STAGE==0
    if(index==0u)nr_setup();
#elif SMO_NATIVE_RENDER_STAGE==1
    nr_transform(index);
#elif SMO_NATIVE_RENDER_STAGE==2 || SMO_NATIVE_RENDER_STAGE==4
    nr_raster(index);
#elif SMO_NATIVE_RENDER_STAGE==3
    nr_prepare_face(index);
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){nr_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){nr_main(gl_GlobalInvocationID.x);}
#endif

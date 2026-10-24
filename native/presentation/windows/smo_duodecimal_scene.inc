// Independent radix12 fixed-point scene. No float, double or native animation
// contributes to its quantities. uint indices/packed final BGRA are not scalar
// SMO values. All literal nibbles below are duodecimal, not decimal fractions.
uint dscene_parameter(uint i){return parameters[i/4u][i%4u];}
SmoD12 dscene_literal(uint whole,uint fraction,uint count){return smo_d12_literal(whole,fraction,count,0u);}
SmoD12 dscene_one(){return dscene_literal(0x1u,0u,0u);}
SmoD12 dscene_dt(){return dscene_literal(0u,0x01u,2u);} // .01 = 1/144
SmoD12 dscene_epsilon(){return dscene_literal(0u,0x0001u,4u);}
SmoD12 dscene_square(SmoD12 value){
    // Generic pdir(0,2) remains undefined. This explicit geometric square
    // extends the nonnegative magnitude operation to its ordinary zero case.
    SmoD12 a=smo_d12_abs(value);
    if(smo_d12_compare(a,smo_d12_zero())==0){SmoD12 z=smo_d12_zero();z.diagnostic=a.diagnostic;return z;}
    return smo_d12_pdir(a,smo_d12_uint(2u));
}
struct DScene3{SmoD12 x;SmoD12 y;SmoD12 z;};
DScene3 dscene_vector(SmoD12 x,SmoD12 y,SmoD12 z){DScene3 r;r.x=x;r.y=y;r.z=z;return r;}
DScene3 dscene_zero3(){return dscene_vector(smo_d12_zero(),smo_d12_zero(),smo_d12_zero());}
DScene3 dscene_load3(uint buffer_id,uint at){return dscene_vector(smo_d12_load(buffer_id,at),smo_d12_load(buffer_id,at+6u),smo_d12_load(buffer_id,at+12u));}
void dscene_store3(uint buffer_id,uint at,DScene3 v){smo_d12_store(buffer_id,at,v.x);smo_d12_store(buffer_id,at+6u,v.y);smo_d12_store(buffer_id,at+12u,v.z);}
#ifdef SMO_HLSL
// Keep one arithmetic callsite inside a real three-axis loop. DXBC's O3
// inliner otherwise duplicates the full checked multi-limb implementation for
// x/y/z at every vector operation. Literal field branches avoid dynamic array
// lvalues in that compiler; neither arithmetic nor its rounding tree changes.
SmoD12 dscene_component(DScene3 a,uint axis){if(axis==0u)return a.x;if(axis==1u)return a.y;return a.z;}
void dscene_set_component(inout DScene3 a,uint axis,SmoD12 value){if(axis==0u)a.x=value;else if(axis==1u)a.y=value;else a.z=value;}
DScene3 dscene_add3(DScene3 a,DScene3 b){DScene3 result=a;[loop]for(uint axis=0u;axis<3u;++axis)dscene_set_component(result,axis,smo_d12_add(dscene_component(a,axis),dscene_component(b,axis)));return result;}
DScene3 dscene_sub3(DScene3 a,DScene3 b){DScene3 result=a;[loop]for(uint axis=0u;axis<3u;++axis)dscene_set_component(result,axis,smo_d12_sub(dscene_component(a,axis),dscene_component(b,axis)));return result;}
DScene3 dscene_scale(DScene3 a,SmoD12 nonnegative){DScene3 result=a;[loop]for(uint axis=0u;axis<3u;++axis)dscene_set_component(result,axis,smo_d12_mul(dscene_component(a,axis),nonnegative));return result;}
#else
DScene3 dscene_add3(DScene3 a,DScene3 b){return dscene_vector(smo_d12_add(a.x,b.x),smo_d12_add(a.y,b.y),smo_d12_add(a.z,b.z));}
DScene3 dscene_sub3(DScene3 a,DScene3 b){return dscene_vector(smo_d12_sub(a.x,b.x),smo_d12_sub(a.y,b.y),smo_d12_sub(a.z,b.z));}
DScene3 dscene_scale(DScene3 a,SmoD12 nonnegative){return dscene_vector(smo_d12_mul(a.x,nonnegative),smo_d12_mul(a.y,nonnegative),smo_d12_mul(a.z,nonnegative));}
#endif
DScene3 dscene_divide(DScene3 a,SmoD12 positive){
    // The experiment fixes this rounding tree: one pinv(d,3), then three
    // products. It does not pretend to be bit-identical to FP32 a/d.
    return dscene_scale(a,smo_d12_pinv(positive,smo_d12_uint(3u)));
}
// Explicit wide geometric compositions. Squared components accumulate exactly
// at scale S^2; magnitude2 rounds once, while norm3 rounds the integer square
// root directly. A representable length need not have representable component
// squares. Generic pdir and its scalar underflow policy remain unchanged.
SmoD12 dscene_magnitude2(DScene3 a){return smo_d12_magnitude2(a.x,a.y,a.z);}
SmoD12 dscene_norm3(DScene3 a){return smo_d12_norm3(a.x,a.y,a.z);}
#ifdef SMO_HLSL
SmoD12 dscene_dot_positive(DScene3 a,DScene3 nonnegative){SmoD12 result=smo_d12_mul(a.x,nonnegative.x);[loop]for(uint axis=1u;axis<3u;++axis)result=smo_d12_add(result,smo_d12_mul(dscene_component(a,axis),dscene_component(nonnegative,axis)));return result;}
#else
SmoD12 dscene_dot_positive(DScene3 a,DScene3 nonnegative){return smo_d12_add(smo_d12_add(smo_d12_mul(a.x,nonnegative.x),smo_d12_mul(a.y,nonnegative.y)),smo_d12_mul(a.z,nonnegative.z));}
#endif
uint dscene_diagnostic3(DScene3 a){return a.x.diagnostic|a.y.diagnostic|a.z.diagnostic;}
bool dscene_valid3(DScene3 a){return smo_d12_valid(a.x)&&smo_d12_valid(a.y)&&smo_d12_valid(a.z);}
#ifdef SMO_HLSL
DScene3 dscene_clamp3(DScene3 a,SmoD12 low,SmoD12 high){DScene3 result=a;[loop]for(uint axis=0u;axis<3u;++axis)dscene_set_component(result,axis,smo_d12_clamp(dscene_component(a,axis),low,high));return result;}
#else
DScene3 dscene_clamp3(DScene3 a,SmoD12 low,SmoD12 high){return dscene_vector(smo_d12_clamp(a.x,low,high),smo_d12_clamp(a.y,low,high),smo_d12_clamp(a.z,low,high));}
#endif
struct DSceneBody{DScene3 p;SmoD12 radius;DScene3 v;uint contacts;uint diagnostic;};
uint dscene_body_diagnostic(DSceneBody b){return b.diagnostic|dscene_diagnostic3(b.p)|b.radius.diagnostic|dscene_diagnostic3(b.v);}
DSceneBody dscene_body(uint buffer_id,uint index){uint at=index*48u;DSceneBody b;b.p=dscene_load3(buffer_id,at);b.radius=smo_d12_load(buffer_id,at+18u);b.v=dscene_load3(buffer_id,at+24u);b.contacts=word(buffer_id,at+42u);b.diagnostic=word(buffer_id,at+43u);return b;}
void dscene_store_body(uint buffer_id,uint index,DSceneBody b){uint at=index*48u;for(uint k=0u;k<48u;++k)put(buffer_id,at+k,0u);dscene_store3(buffer_id,at,b.p);smo_d12_store(buffer_id,at+18u,b.radius);dscene_store3(buffer_id,at+24u,b.v);put(buffer_id,at+42u,b.contacts);put(buffer_id,at+43u,dscene_body_diagnostic(b));}
void dscene_initialize(uint index){if(index>=dscene_parameter(0u))return;DSceneBody b;uint column=index%4u,row=index/4u;
    SmoD12 half_unit=dscene_literal(0u,0x6u,1u),one_half=dscene_literal(0x1u,0x6u,1u);
    b.p=dscene_vector(smo_d12_sub(smo_d12_uint(column),one_half),smo_d12_sub(one_half,smo_d12_uint(row)),
        smo_d12_add(smo_d12_uint(2u),smo_d12_mul(smo_d12_uint(index%3u),half_unit)));
    b.radius=dscene_literal(0u,0x2u,1u);
    SmoD12 vx=dscene_literal(0u,0x06u,2u),vz=dscene_literal(0u,0x04u,2u);
    if(index%2u!=0u)vx=smo_d12_negate(vx);if(column%2u!=0u)vz=smo_d12_negate(vz);
    b.v=dscene_vector(vx,smo_d12_zero(),vz);b.contacts=0u;b.diagnostic=0u;dscene_store_body(1u,index,b);
}
void dscene_integrate(uint index){if(index>=dscene_parameter(0u))return;DSceneBody b=dscene_body(0u,index);
    SmoD12 gravity=smo_d12_negate(dscene_literal(0x1u,0x6u,1u));
    b.v.y=smo_d12_add(b.v.y,smo_d12_mul(gravity,dscene_dt()));b.p=dscene_add3(b.p,dscene_scale(b.v,dscene_dt()));b.contacts=0u;dscene_store_body(1u,index,b);
}
void dscene_collide(uint index){if(index>=dscene_parameter(0u))return;DSceneBody b=dscene_body(0u,index);DScene3 correction=dscene_zero3();b.contacts=0u;
    for(uint other=0u;other<dscene_parameter(0u);++other){if(other==index)continue;DSceneBody c=dscene_body(0u,other);uint invalid=dscene_body_diagnostic(c)&0xffffu;if(invalid!=0u){b.diagnostic|=invalid;continue;}
        DScene3 delta=dscene_sub3(b.p,c.p);SmoD12 distance=dscene_norm3(delta),radius=smo_d12_add(b.radius,c.radius);
        if(!smo_d12_valid(distance)||!smo_d12_valid(radius)){b.diagnostic|=distance.diagnostic|radius.diagnostic;continue;}
        if(smo_d12_compare(distance,radius)>=0)continue;DScene3 normal;
        if(smo_d12_compare(distance,dscene_epsilon())>0)normal=dscene_divide(delta,distance);
        else {SmoD12 axis=dscene_one();if(index<other)axis=smo_d12_negate(axis);normal=dscene_vector(axis,smo_d12_zero(),smo_d12_zero());}
        SmoD12 penetration=smo_d12_sub(radius,distance),gain=dscene_literal(0u,0x3u,1u);
        correction=dscene_add3(correction,dscene_scale(normal,smo_d12_mul(penetration,gain)));++b.contacts;
    }
    SmoD12 length=dscene_norm3(correction),limit=dscene_literal(0u,0x1u,1u);
    if(smo_d12_compare(length,limit)>0)correction=dscene_scale(correction,smo_d12_mul(limit,smo_d12_pinv(length,smo_d12_uint(3u))));
    b.p=dscene_add3(b.p,correction);b.v=dscene_add3(b.v,dscene_divide(correction,dscene_dt()));b.v=dscene_clamp3(b.v,smo_d12_negate(smo_d12_uint(4u)),smo_d12_uint(4u));
    SmoD12 restitution=dscene_literal(0u,0x8u,1u),floor=smo_d12_add(smo_d12_negate(smo_d12_uint(2u)),b.radius),top=smo_d12_sub(smo_d12_uint(2u),b.radius),side=smo_d12_sub(smo_d12_uint(3u),b.radius);
    if(smo_d12_compare(b.p.y,floor)<0){b.p.y=floor;b.v.y=smo_d12_mul(smo_d12_abs(b.v.y),restitution);++b.contacts;}
    if(smo_d12_compare(b.p.y,top)>0){b.p.y=top;b.v.y=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.v.y),restitution));}
    if(smo_d12_compare(b.p.x,smo_d12_negate(side))<0){b.p.x=smo_d12_negate(side);b.v.x=smo_d12_mul(smo_d12_abs(b.v.x),restitution);}
    if(smo_d12_compare(b.p.x,side)>0){b.p.x=side;b.v.x=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.v.x),restitution));}
    SmoD12 near_z=smo_d12_add(dscene_one(),b.radius),far_z=smo_d12_sub(smo_d12_uint(4u),b.radius);
    if(smo_d12_compare(b.p.z,near_z)<0){b.p.z=near_z;b.v.z=smo_d12_mul(smo_d12_abs(b.v.z),restitution);}
    if(smo_d12_compare(b.p.z,far_z)>0){b.p.z=far_z;b.v.z=smo_d12_negate(smo_d12_mul(smo_d12_abs(b.v.z),restitution));}
    b.diagnostic|=dscene_diagnostic3(correction)|length.diagnostic;dscene_store_body(1u,index,b);
}

struct DSceneHit{SmoD12 distance;uint hit;uint diagnostic;};
DSceneHit dscene_intersection(DScene3 origin,DScene3 positive_dir,DSceneBody b){
    DSceneHit result;result.distance=smo_d12_zero();result.hit=0u;result.diagnostic=dscene_body_diagnostic(b);
    if((result.diagnostic&0xffffu)!=0u)return result;
    DScene3 delta=dscene_sub3(origin,b.p);SmoD12 a=dscene_magnitude2(positive_dir),h=dscene_dot_positive(delta,positive_dir);
    SmoD12 c=smo_d12_sub(dscene_magnitude2(delta),dscene_square(b.radius));
    // Explicit widened geometric composition: h^2-a*c is evaluated at scale
    // S^2 and rounded ONCE to D12. A tiny standalone h^2 need not fit D12 if
    // their difference does. Generic pdir underflow remains unchanged, and a
    // nonzero final discriminant that rounds to zero remains a visible error.
    SmoD12 discriminant=smo_d12_discriminant(h,a,c);
    result.diagnostic|=a.diagnostic|h.diagnostic|c.diagnostic|discriminant.diagnostic;
    if(!smo_d12_valid(discriminant)||!smo_d12_valid(a))return result;
    if(smo_d12_compare(a,smo_d12_zero())<=0){result.diagnostic|=4u;return result;}
    if(smo_d12_compare(discriminant,smo_d12_zero())<0)return result;
    SmoD12 root=smo_d12_sqrt(discriminant),inverse=smo_d12_pinv(a,smo_d12_uint(3u));
    SmoD12 t=smo_d12_mul(smo_d12_sub(smo_d12_negate(h),root),inverse);
    if(smo_d12_compare(t,dscene_epsilon())<0)t=smo_d12_mul(smo_d12_add(smo_d12_negate(h),root),inverse);
    result.diagnostic|=root.diagnostic|inverse.diagnostic|t.diagnostic;
    if(smo_d12_valid(t)&&smo_d12_compare(t,dscene_epsilon())>=0){result.distance=t;result.hit=1u;}
    return result;
}
uint dscene_channel(SmoD12 source_color,inout uint diagnostic){
    SmoD12 value=smo_d12_mul(smo_d12_clamp(source_color,smo_d12_zero(),dscene_one()),smo_d12_uint(255u));
    diagnostic|=value.diagnostic;if(!smo_d12_valid(value))return 0u;
    // Final display conversion belongs to the numeric backend. The shader
    // does not inspect radix limbs, permitting a separate FP32 control of
    // this very same scene without replacing its integer D12 arithmetic.
    return min(smo_d12_round_uint(value,diagnostic),255u);
}
struct DScenePixel{DScene3 color;SmoD12 distance;uint hit;uint diagnostic;uint shadow;uint bgra;uint tests;};
DScenePixel dscene_read_pixel(uint index){uint at=index*32u;DScenePixel p;p.color=dscene_load3(1u,at);p.distance=smo_d12_load(1u,at+18u);
    p.hit=word(1u,at+24u);p.diagnostic=word(1u,at+25u);p.shadow=word(1u,at+26u);p.bgra=word(1u,at+27u);p.tests=word(1u,at+28u);return p;}
void dscene_store_pixel_words(uint index,DScene3 color,SmoD12 distance,uint hit,uint diagnostic,uint shadow,uint bgra,uint tests){
    uint at=index*32u;for(uint k=0u;k<32u;++k)put(1u,at+k,0u);
    dscene_store3(1u,at,color);smo_d12_store(1u,at+18u,distance);put(1u,at+24u,hit);put(1u,at+25u,diagnostic);put(1u,at+26u,shadow);put(1u,at+27u,bgra);put(1u,at+28u,tests);
}
void dscene_store_pixel(uint index,DScene3 color,SmoD12 distance,uint hit,uint diagnostic,uint shadow,uint tests){
    diagnostic|=dscene_diagnostic3(color)|distance.diagnostic;uint bgra=0xffff00ffu;
    if((diagnostic&0xffffu)==0u){uint red=dscene_channel(color.x,diagnostic),green=dscene_channel(color.y,diagnostic),blue=dscene_channel(color.z,diagnostic);
        if((diagnostic&0xffffu)==0u)bgra=0xff000000u|(red<<16u)|(green<<8u)|blue;}
    dscene_store_pixel_words(index,color,distance,hit,diagnostic,shadow,bgra,tests);
}
void dscene_trace_primary(uint index){uint width=dscene_parameter(1u),height=dscene_parameter(2u);if(index>=width*height)return;uint x=index%width,y=index/width;
    SmoD12 xcenter=smo_d12_sub(smo_d12_uint(x*2u+1u),smo_d12_uint(width));
    SmoD12 ycenter=smo_d12_sub(smo_d12_uint(height),smo_d12_uint(y*2u+1u));
    SmoD12 ix=smo_d12_pinv(smo_d12_uint(width),smo_d12_uint(3u)),iy=smo_d12_pinv(smo_d12_uint(height),smo_d12_uint(3u));
    DScene3 origin=dscene_vector(smo_d12_mul(smo_d12_mul(xcenter,ix),smo_d12_uint(4u)),
        smo_d12_mul(smo_d12_mul(ycenter,iy),smo_d12_uint(3u)),smo_d12_negate(smo_d12_uint(6u)));
    DScene3 direction=dscene_vector(smo_d12_zero(),smo_d12_zero(),dscene_one());
    DScene3 color=dscene_vector(dscene_literal(0u,0x03u,2u),dscene_literal(0u,0x06u,2u),dscene_literal(0u,0x09u,2u));
    SmoD12 nearest=smo_d12_zero();uint hit=0xffffffffu,diagnostic=dscene_diagnostic3(origin),tests=0u,shadow=0u;
    for(uint candidate=0u;candidate<dscene_parameter(0u);++candidate){++tests;DSceneHit result=dscene_intersection(origin,direction,dscene_body(0u,candidate));diagnostic|=result.diagnostic;
        if((diagnostic&0xffffu)!=0u)break;if(result.hit!=0u&&(hit==0xffffffffu||smo_d12_compare(result.distance,nearest)<0)){nearest=result.distance;hit=candidate;}}
    if(hit!=0xffffffffu&&(diagnostic&0xffffu)==0u){
        // Pending hit: RGB slots temporarily retain the original D12 ray
        // origin. BGRA0 identifies unfinished shading. Do NOT call channel or
        // finalization here: that could add diagnostics before the old order.
        dscene_store_pixel_words(index,origin,nearest,hit,diagnostic,0u,0u,tests);
    }else dscene_store_pixel(index,color,nearest,hit,diagnostic,shadow,tests);
}
void dscene_shade(uint index){if(index>=dscene_parameter(1u)*dscene_parameter(2u))return;DScenePixel pending=dscene_read_pixel(index);
    if(pending.bgra!=0u||pending.hit==0xffffffffu||(pending.diagnostic&0xffffu)!=0u)return;
    DScene3 origin=pending.color,direction=dscene_vector(smo_d12_zero(),smo_d12_zero(),dscene_one()),color;
    SmoD12 nearest=pending.distance;uint hit=pending.hit,diagnostic=pending.diagnostic,tests=pending.tests,shadow=pending.shadow;
    {DSceneBody b=dscene_body(0u,hit);DScene3 at=dscene_add3(origin,dscene_scale(direction,nearest));
        DScene3 normal=dscene_divide(dscene_sub3(at,b.p),b.radius);
        // Point light (6,6,8) is beyond every wall; all three light-direction
        // components are positive. This avoids importing a different rule for
        // generic SMO negative*negative into Euclidean dot products.
        DScene3 light_delta=dscene_sub3(dscene_vector(smo_d12_uint(6u),smo_d12_uint(6u),smo_d12_uint(8u)),at);
        SmoD12 light_distance=dscene_norm3(light_delta);DScene3 light=dscene_divide(light_delta,light_distance);
        DScene3 shadow_origin=dscene_add3(at,dscene_scale(normal,dscene_epsilon()));
        diagnostic|=dscene_diagnostic3(at)|dscene_diagnostic3(normal)|light_distance.diagnostic|dscene_diagnostic3(light)|dscene_diagnostic3(shadow_origin);
        if((diagnostic&0xffffu)==0u)for(uint candidate=0u;candidate<dscene_parameter(0u);++candidate){if(candidate==hit)continue;++tests;
            DSceneHit result=dscene_intersection(shadow_origin,light,dscene_body(0u,candidate));diagnostic|=result.diagnostic;
            if((diagnostic&0xffffu)!=0u)break;if(result.hit!=0u&&smo_d12_compare(result.distance,light_distance)<0){shadow=1u;break;}}
        SmoD12 diffuse=smo_d12_clamp(dscene_dot_positive(normal,light),smo_d12_zero(),dscene_one());
        // Real inverse-square attenuation, not an unexplained pow label:
        // pinv(d,4)=1/d^2 for this positive distance. 100(base12)=144 is gain.
        SmoD12 attenuation=smo_d12_clamp(smo_d12_mul(smo_d12_pinv(light_distance,smo_d12_uint(4u)),dscene_literal(0x100u,0u,0u)),smo_d12_zero(),dscene_one());
        SmoD12 direct=dscene_literal(0u,0x9u,1u);if(shadow!=0u)direct=dscene_literal(0u,0x2u,1u);
        SmoD12 lighting=smo_d12_add(dscene_literal(0u,0x2u,1u),smo_d12_mul(smo_d12_mul(diffuse,attenuation),direct));
        DScene3 pigment=dscene_vector(smo_d12_add(dscene_literal(0u,0x3u,1u),smo_d12_mul(smo_d12_uint(hit%3u),dscene_literal(0u,0x2u,1u))),
            smo_d12_add(dscene_literal(0u,0x4u,1u),smo_d12_mul(smo_d12_uint(hit%4u),dscene_literal(0u,0x1u,1u))),
            smo_d12_add(dscene_literal(0u,0x6u,1u),smo_d12_mul(smo_d12_uint(hit%2u),dscene_literal(0u,0x2u,1u))));
        color=dscene_scale(pigment,lighting);diagnostic|=diffuse.diagnostic|attenuation.diagnostic|lighting.diagnostic|dscene_diagnostic3(color);
    }
    dscene_store_pixel(index,color,nearest,hit,diagnostic,shadow,tests);
}
void dscene_main(uint index){
#if D12_STAGE==0
dscene_initialize(index);
#elif D12_STAGE==1
dscene_integrate(index);
#elif D12_STAGE==2
dscene_collide(index);
#elif D12_STAGE==3
dscene_trace_primary(index);
#elif D12_STAGE==4
dscene_shade(index);
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 index:SV_DispatchThreadID){dscene_main(index.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){dscene_main(gl_GlobalInvocationID.x);}
#endif

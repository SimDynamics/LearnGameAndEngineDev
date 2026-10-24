// One numerical body for HLSL, GLSL and SPIR-V. No native animation supplies
// the output. MODE0=ordinary/all candidates, MODE1=ordinary/group culling,
// MODE2=SMO/group culling. Nonnegative magnitudes/coefficient domains are
// explicit; raw SMO negative*negative remains negative, not a Euclidean dot.
uint par(uint i){return parameters[i/4u][i%4u];}
float val(uint i){return REAL(par(i));}
float loadf(uint s,uint i){return REAL(word(s,i));}
void storef(uint s,uint i,float v){put(s,i,BITS(v));}
float3 load3(uint s,uint i){return float3(loadf(s,i),loadf(s,i+1u),loadf(s,i+2u));}
void store3(uint s,uint i,float3 v){storef(s,i,v.x);storef(s,i+1u,v.y);storef(s,i+2u,v.z);}
// Unrestricted scalar SMO operations are kept separate from the geometric
// primitives below. Stage 5 validates these rules against the interpreter,
// including negative*negative and division by zero; no range assumption is
// permitted in that conformance path.
#if STAGE==5
float scalar_mul(float a,float b){
    // One rounded product in both paths. SMO changes its sign only for two
    // negative factors; zero remains zero. This is the same scalar rule,
    // expressed without needlessly changing all mixed-sign rounding paths.
    precise float result=a*b;
#if MODE==2
    if(a<0.0&&b<0.0)result=-result;
    if(result==0.0)result=0.0;
#endif
    return result;
}
float scalar_div(float a,float b){
#if MODE==2
    if(b==0.0)return a;
#endif
    precise float result=a/b;
#if MODE==2
    if(a<0.0&&b<0.0)result=-result;
    if(result==0.0)result=0.0;
#endif
    return result;
}
#endif
// Refined geometric domains, established by this scene's callers:
//   mul_geometry: finite operands with AT LEAST ONE nonnegative factor.
//   div_geometry_positive: finite numerator and strictly positive divisor.
// Proof: outside zero, SMO's negative-absorbing sign is ordinary XOR whenever
// at most one factor is negative; its default zero product is also zero.
// A positive divisor similarly reduces the scalar quotient to ordinary a/b.
// The discriminant's a*c has positive a and possibly negative c, so the
// product contract deliberately does not constrain just the second factor.
// This scene fixes dt=1/120, positive radii and restitution=.65. Normalization
// and correction division guard their lengths; primary/shadow ray components
// are nonnegative, with zero components excluded before division in ray_box.
// These refinements identify the two IEEE signed-zero representations as the
// same mathematical zero; no geometric caller inspects its sign or divides by
// it. They do NOT extend to arbitrary scalar SMO negative pairs or zero divisors.
// Emit identical FP32 operators in ALL modes. Removing a redundant MODE-only
// guard here preserves the proved scalar value and avoids giving one mode a
// different division/reassociation path during long, sensitive contact chains.
// Cross-API FP32 bit identity is still not promised by these local proofs.
float mul_geometry(float a,float b){precise float result=a*b;return result;}
float div_geometry_positive(float a,float positive){precise float result=a/positive;return result;}
float3 scale(float3 a,float nonnegative){return float3(mul_geometry(a.x,nonnegative),mul_geometry(a.y,nonnegative),mul_geometry(a.z,nonnegative));}
float3 divide3(float3 a,float positive){return float3(div_geometry_positive(a.x,positive),div_geometry_positive(a.y,positive),div_geometry_positive(a.z,positive));}
float magnitude2(float3 a){a=abs(a);precise float result=(mul_geometry(a.x,a.x)+mul_geometry(a.y,a.y))+mul_geometry(a.z,a.z);return result;}
float dot_positive(float3 a,float3 nonnegative){precise float result=(mul_geometry(a.x,nonnegative.x)+mul_geometry(a.y,nonnegative.y))+mul_geometry(a.z,nonnegative.z);return result;}
struct Body{float3 p;float r;float3 v;float contacts;};
Body body(uint s,uint i){uint o=i*8u;Body b;b.p=load3(s,o);b.r=loadf(s,o+3u);b.v=load3(s,o+4u);b.contacts=loadf(s,o+7u);return b;}
void store_body(uint s,uint i,Body b){uint o=i*8u;store3(s,o,b.p);storef(s,o+3u,b.r);store3(s,o+4u,b.v);storef(s,o+7u,b.contacts);}
void integrate(uint i){if(i>=par(0u))return;Body b=body(0u,i);float dt=val(8u);
    // Gravity and a gentle deterministic force maintain dynamic contacts.
    // Trig is common graphics support, not a new SMO sine definition.
    precise float t=float(par(5u))*0.0078125;
    b.v.x+=mul_geometry(sin(t+float(i%17u)*0.125),mul_geometry(0.12,dt));
    b.v.z+=mul_geometry(cos(t+float(i%13u)*0.125),mul_geometry(0.12,dt));
    b.v.y+=mul_geometry(val(9u),dt);b.p+=scale(b.v,dt);b.contacts=0.0;store_body(1u,i,b);
}
void bounds(uint g){if(g>=par(2u))return;float3 lo=float3(1e20,1e20,1e20),hi=-lo;
    uint first=g*par(1u),last=min(par(0u),first+par(1u));for(uint j=first;j<last;++j){Body b=body(0u,j);float r=b.r+0.0001;lo=min(lo,b.p-float3(r,r,r));hi=max(hi,b.p+float3(r,r,r));}
    store3(1u,g*8u,lo);storef(1u,g*8u+3u,0.0);store3(1u,g*8u+4u,hi);storef(1u,g*8u+7u,0.0);
}
bool near_box(float3 p,float radius,uint g){float3 lo=load3(2u,g*8u),hi=load3(2u,g*8u+4u);return p.x+radius>=lo.x&&p.x-radius<=hi.x&&p.y+radius>=lo.y&&p.y-radius<=hi.y&&p.z+radius>=lo.z&&p.z-radius<=hi.z;}
void collide(uint i){if(i>=par(0u))return;Body b=body(1u,i);precise float3 correction=float3(0,0,0);float contacts=0.0;
    for(uint g=0u;g<par(2u);++g){
#if MODE!=0
        if(!near_box(b.p,b.r,g))continue;
#endif
        uint first=g*par(1u),last=min(par(0u),first+par(1u));for(uint j=first;j<last;++j){if(i==j)continue;Body c=body(1u,j);float3 delta=b.p-c.p;float d2=magnitude2(delta),radius=b.r+c.r;
#if STAGE==5
            // Untimed diagnostic only: Stage2's hot path has no trace stores.
            store3(4u,j*24u,delta);storef(4u,j*24u+3u,d2);storef(4u,j*24u+4u,radius);storef(4u,j*24u+5u,mul_geometry(radius,radius));
#endif
            if(d2>=mul_geometry(radius,radius))continue;float d=sqrt(max(0.0,d2));float3 normal=d>0.00001?divide3(delta,d):float3(i<j?-1.0:1.0,0,0);
            correction+=scale(normal,mul_geometry(radius-d,0.25));contacts+=1.0;
#if STAGE==5
            storef(4u,j*24u+6u,d);store3(4u,j*24u+7u,normal);storef(4u,j*24u+10u,radius-d);
            storef(4u,j*24u+11u,mul_geometry(radius-d,0.25));store3(4u,j*24u+12u,scale(normal,mul_geometry(radius-d,0.25)));
            store3(4u,j*24u+15u,correction);storef(4u,j*24u+18u,contacts);storef(4u,j*24u+19u,1.0);
#endif
        }
    }
#if STAGE==5
    store3(4u,par(0u)*24u,correction);storef(4u,par(0u)*24u+3u,sqrt(magnitude2(correction)));
#endif
    float length_correction=sqrt(magnitude2(correction));if(length_correction>0.08)correction=scale(correction,div_geometry_positive(0.08,length_correction));
#if STAGE==5
    store3(4u,par(0u)*24u+4u,correction);storef(4u,par(0u)*24u+7u,length_correction>0.08?div_geometry_positive(0.08,length_correction):1.0);
#endif
    b.p+=correction;b.v+=divide3(correction,val(8u));b.v=clamp(b.v,float3(-4,-4,-4),float3(4,4,4));
    float floor_y=-3.0+b.r,top=3.0-b.r,side=4.8-b.r;
    if(b.p.y<floor_y){b.p.y=floor_y;b.v.y=mul_geometry(abs(b.v.y),val(10u));contacts+=1.0;}
    if(b.p.y>top){b.p.y=top;b.v.y=-mul_geometry(abs(b.v.y),val(10u));}
    if(b.p.x<-side){b.p.x=-side;b.v.x=mul_geometry(abs(b.v.x),val(10u));}if(b.p.x>side){b.p.x=side;b.v.x=-mul_geometry(abs(b.v.x),val(10u));}
    if(b.p.z<0.5+b.r){b.p.z=0.5+b.r;b.v.z=mul_geometry(abs(b.v.z),val(10u));}if(b.p.z>4.8-b.r){b.p.z=4.8-b.r;b.v.z=-mul_geometry(abs(b.v.z),val(10u));}
    b.contacts=contacts;store_body(3u,i,b);
#if STAGE==5
    store3(4u,par(0u)*24u+8u,b.p);storef(4u,par(0u)*24u+11u,b.r);store3(4u,par(0u)*24u+12u,b.v);storef(4u,par(0u)*24u+15u,b.contacts);
#endif
}
// Orthographic primary rays and a positive directional shadow ray form an
// explicit domain where these scalar SMO products match Euclidean support.
bool ray_box(float3 origin,float3 positive_dir,uint g,float limit){float3 lo=load3(1u,g*8u),hi=load3(1u,g*8u+4u);float near_t=0.0,far_t=limit;
    for(uint k=0u;k<3u;++k){if(positive_dir[k]==0.0){if(origin[k]<lo[k]||origin[k]>hi[k])return false;}else{near_t=max(near_t,div_geometry_positive(lo[k]-origin[k],positive_dir[k]));far_t=min(far_t,div_geometry_positive(hi[k]-origin[k],positive_dir[k]));}}
    return far_t>=near_t;
}
float intersection(float3 origin,float3 positive_dir,Body b){float3 delta=origin-b.p;float a=magnitude2(positive_dir),h=dot_positive(delta,positive_dir),c=magnitude2(delta)-mul_geometry(b.r,b.r);
    float discriminant=mul_geometry(abs(h),abs(h))-mul_geometry(a,c);if(discriminant<0.0)return 1e20;float root=sqrt(discriminant),t=div_geometry_positive(-h-root,a);if(t<0.001)t=div_geometry_positive(-h+root,a);return t>=0.001?t:1e20;
}
void trace(uint i){uint pixels=par(3u)*par(4u);if(i>=pixels)return;uint x=i%par(3u),y=i/par(3u);float3 origin=float3((float(x)+0.5)/float(par(3u))*11.0-5.5,3.3-(float(y)+0.5)/float(par(4u))*6.6,-6.0),direction=float3(0,0,1);
    float nearest=1e20;uint hit=0xffffffffu,tests=0u,boxes=0u;
    for(uint g=0u;g<par(2u);++g){
#if MODE!=0
        ++boxes;if(!ray_box(origin,direction,g,nearest))continue;
#endif
        uint first=g*par(1u),last=min(par(0u),first+par(1u));for(uint j=first;j<last;++j){++tests;float t=intersection(origin,direction,body(0u,j));if(t<nearest){nearest=t;hit=j;}}
    }
    float3 color=float3(0.025,0.04,0.07);if(hit!=0xffffffffu){Body b=body(0u,hit);float3 at=origin+scale(direction,nearest),normal=divide3(at-b.p,b.r);
        float3 light=float3(0.4082483,0.8164966,0.4082483);bool shadow=false;float3 shadow_origin=at+scale(normal,0.003);
        for(uint g=0u;g<par(2u)&&!shadow;++g){
#if MODE!=0
            ++boxes;if(!ray_box(shadow_origin,light,g,100.0))continue;
#endif
            uint first=g*par(1u),last=min(par(0u),first+par(1u));for(uint j=first;j<last;++j){if(j==hit)continue;++tests;if(intersection(shadow_origin,light,body(0u,j))<100.0){shadow=true;break;}}
        }
        float lighting=0.35+mul_geometry(max(0.0,dot_positive(normal,light)),shadow?0.15:0.65);
        float3 pigment=float3(0.25+float(hit%13u)*0.055,0.3+float(hit%7u)*0.08,0.4+float(hit%5u)*0.1);color=scale(pigment,lighting);
    }
    uint o=i*8u;store3(2u,o,color);storef(2u,o+3u,hit==0xffffffffu?-1.0:nearest);put(2u,o+4u,tests);put(2u,o+5u,boxes);put(2u,o+6u,hit==0xffffffffu?0u:1u);put(2u,o+7u,0u);
}
#if STAGE==4
// Keep combined counters 0/1; diagnostics 17/18 distinguish state A-B/A-C,
// and 19/20 distinguish pixels A-B/A-C, with the identical existing threshold.
SHARED float errors[64];SHARED uint state_errors[64],image_errors[64],bad_values[64],hashes[64],contacts_sum[64],rays_a[64],rays_b[64],rays_c[64],boxes_b[64],boxes_c[64],hits[64];
SHARED uint state_ab[64],state_ac[64],image_ab[64],image_ac[64];
void compare(uint i,uint local){float error=0.0;uint se=0u,pe=0u,sab=0u,sac=0u,pab=0u,pac=0u,bad=0u,hash=0u,contacts=0u,ta=0u,tb=0u,tc=0u,bb=0u,bc=0u,hit=0u;float tolerance=val(12u);
    if(i<par(0u)){contacts=uint(max(0.0,loadf(0u,i*8u+7u)));for(uint k=0u;k<8u;++k){float a=loadf(0u,i*8u+k),b=loadf(1u,i*8u+k),c=loadf(2u,i*8u+k);float eab=abs(a-b),eac=abs(a-c),e=max(eab,eac);if(!ISFINITE(a)||!ISFINITE(b)||!ISFINITE(c))++bad;error=max(error,e);float threshold=tolerance*max(1.0,abs(a));if(e>threshold)++se;if(eab>threshold)++sab;if(eac>threshold)++sac;}}
    if(i<par(3u)*par(4u)){uint o=i*8u;for(uint k=0u;k<4u;++k){float a=loadf(3u,o+k),b=loadf(4u,o+k),c=loadf(5u,o+k);float eab=abs(a-b),eac=abs(a-c),e=max(eab,eac);if(!ISFINITE(a)||!ISFINITE(b)||!ISFINITE(c))++bad;error=max(error,e);float threshold=tolerance*max(1.0,abs(a));if(e>threshold)++pe;if(eab>threshold)++pab;if(eac>threshold)++pac;hash^=BITS(a)+(i*1664525u+k*1013904223u);}
        ta=word(3u,o+4u);tb=word(4u,o+4u);tc=word(5u,o+4u);bb=word(4u,o+5u);bc=word(5u,o+5u);hit=word(3u,o+6u);}
    errors[local]=error;state_errors[local]=se;image_errors[local]=pe;state_ab[local]=sab;state_ac[local]=sac;image_ab[local]=pab;image_ac[local]=pac;bad_values[local]=bad;hashes[local]=hash;contacts_sum[local]=contacts;rays_a[local]=ta;rays_b[local]=tb;rays_c[local]=tc;boxes_b[local]=bb;boxes_c[local]=bc;hits[local]=hit;SYNC;
    for(uint step=32u;step>0u;step/=2u){if(local<step){errors[local]=max(errors[local],errors[local+step]);state_errors[local]+=state_errors[local+step];image_errors[local]+=image_errors[local+step];state_ab[local]+=state_ab[local+step];state_ac[local]+=state_ac[local+step];image_ab[local]+=image_ab[local+step];image_ac[local]+=image_ac[local+step];bad_values[local]+=bad_values[local+step];hashes[local]^=hashes[local+step];contacts_sum[local]+=contacts_sum[local+step];rays_a[local]+=rays_a[local+step];rays_b[local]+=rays_b[local+step];rays_c[local]+=rays_c[local+step];boxes_b[local]+=boxes_b[local+step];boxes_c[local]+=boxes_c[local+step];hits[local]+=hits[local+step];}SYNC;}
    if(local==0u){atomic_add(0u,state_errors[0]);atomic_add(1u,image_errors[0]);atomic_max(2u,BITS(errors[0]));atomic_add(3u,bad_values[0]);atomic_xor(4u,hashes[0]);atomic_add(5u,contacts_sum[0]);atomic_wide(6u,rays_a[0]);atomic_wide(8u,rays_b[0]);atomic_wide(10u,rays_c[0]);atomic_wide(12u,boxes_b[0]);atomic_wide(14u,boxes_c[0]);atomic_add(16u,hits[0]);atomic_add(17u,state_ab[0]);atomic_add(18u,state_ac[0]);atomic_add(19u,image_ab[0]);atomic_add(20u,image_ac[0]);}
}
#endif
void scene_main(uint i,uint local){
#if STAGE==0
integrate(i);
#elif STAGE==1
bounds(i);
#elif STAGE==2
collide(i);
#elif STAGE==3
trace(i);
#elif STAGE==4
compare(i,local);
#elif STAGE==5
if(par(15u)==1u){if(i==0u)collide(par(7u));}
else if(i<par(0u)){if(par(1u)==1u)storef(1u,i,scalar_mul(loadf(0u,i*2u),loadf(0u,i*2u+1u)));else if(par(1u)==2u)storef(1u,i,scalar_div(loadf(0u,i*2u),loadf(0u,i*2u+1u)));else storef(1u,i,loadf(0u,i)*2.0+1.0);}
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID,uint3 local:SV_GroupThreadID){scene_main(id.x,local.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){scene_main(gl_GlobalInvocationID.x,gl_LocalInvocationID.x);}
#endif

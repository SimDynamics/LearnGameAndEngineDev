// Integer-F12 geometric extension, NOT a change to the program's SMO rules.
// Signed products/quotients below operate on magnitudes and restore XOR signs
// solely for Euclidean transforms, trigonometry and triangle intersections.
// No FP type/instruction, native sin/cos or host projection participates.
struct PrVector {SmoD12 x;SmoD12 y;SmoD12 z;};
struct PrTrig {SmoD12 sine;SmoD12 cosine;uint diagnostic;};
struct PrHit {SmoD12 depth;uint hit;uint diagnostic;};
PrVector pr_vector(SmoD12 x,SmoD12 y,SmoD12 z){PrVector v;v.x=x;v.y=y;v.z=z;return v;}
uint pr_diagnostic(PrVector v){return v.x.diagnostic|v.y.diagnostic|v.z.diagnostic;}
SmoD12 pr_product(SmoD12 a,SmoD12 b){SmoD12 r=smo_d12_mul(smo_d12_abs(a),smo_d12_abs(b));
    if(smo_d12_valid(r)&&!smo_d12_empty(r))r.negative=a.negative^b.negative;return r;}
SmoD12 pr_quotient(SmoD12 a,SmoD12 b){uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);SmoD12 r=smo_d12_failure(diagnostic|4u);
    if((diagnostic&65535u)==0u&&!smo_d12_empty(b)){r=smo_d12_div(smo_d12_abs(a),smo_d12_abs(b));if(smo_d12_valid(r)&&!smo_d12_empty(r))r.negative=a.negative^b.negative;}return r;}
PrVector pr_sub(PrVector a,PrVector b){return pr_vector(smo_d12_sub(a.x,b.x),smo_d12_sub(a.y,b.y),smo_d12_sub(a.z,b.z));}
PrVector pr_negative(PrVector a){return pr_vector(smo_d12_negate(a.x),smo_d12_negate(a.y),smo_d12_negate(a.z));}
SmoD12 pr_dot(PrVector a,PrVector b){return smo_d12_add(smo_d12_add(pr_product(a.x,b.x),pr_product(a.y,b.y)),pr_product(a.z,b.z));}
PrVector pr_cross(PrVector a,PrVector b){return pr_vector(smo_d12_sub(pr_product(a.y,b.z),pr_product(a.z,b.y)),
    smo_d12_sub(pr_product(a.z,b.x),pr_product(a.x,b.z)),smo_d12_sub(pr_product(a.x,b.y),pr_product(a.y,b.x)));}
PrVector pr_load(uint buffer_id,uint offset){return pr_vector(smo_d12_load(buffer_id,offset),smo_d12_load(buffer_id,offset+6u),smo_d12_load(buffer_id,offset+12u));}
void pr_store(uint buffer_id,uint offset,PrVector v){smo_d12_store(buffer_id,offset,v.x);smo_d12_store(buffer_id,offset+6u,v.y);smo_d12_store(buffer_id,offset+12u,v.z);}

SmoD12 pr_pi(){SmoD12 p=smo_d12_zero();p.digits=uint4(16125u,13936u,5428u,0xf12000b5u);p.diagnostic=65536u;return p;}
// pi=3.184809493~9 (twelve radix12 significant digits), independently derived
// by integer Machin16*atan(1/5)-4*atan(1/239). Trig is an explicit approximate
// extension and reports INEXACT for nonzero arguments. Periodicity/reduction
// and the Maclaurin recurrence: https://dlmf.nist.gov/4.14 and /4.19.
uint pr_floor_count(SmoD12 a,inout uint diagnostic){uint result=0u;diagnostic|=smo_d12_diagnostics(a);
    if(smo_d12_valid(a)&&a.negative==0u&&smo_d12_compare(a,smo_d12_uint(1000000u))<=0){F12Number n=f12_unpack(a);F12Wide c=f12_coefficient(n.coefficient);
        if(n.exponent<0)c=f12_right_shift(c,uint(-n.exponent));else c=f12_shift(c,uint(n.exponent));result=c.low.x+c.low.y*20736u;
    }else diagnostic|=4u;return result;}
PrTrig pr_sincos(SmoD12 angle){PrTrig result;result.sine=smo_d12_zero();result.cosine=smo_d12_uint(1u);result.diagnostic=smo_d12_diagnostics(angle);
    if((result.diagnostic&65535u)==0u&&!smo_d12_empty(angle)){
        SmoD12 absolute=smo_d12_abs(angle);result.diagnostic|=65536u;
        if(smo_d12_compare(absolute,smo_d12_uint(1000000u))>0)result.diagnostic|=4u;
        else if(smo_d12_compare(absolute,smo_d12_literal(0u,1u,6u,0u))<=0)result.sine=angle;
        else{SmoD12 pi=pr_pi(),two_pi=smo_d12_add(pi,pi),half_pi=pr_quotient(pi,smo_d12_uint(2u)),x=absolute;
            if(smo_d12_compare(x,pi)>0){SmoD12 cycles=pr_quotient(x,two_pi);uint whole=pr_floor_count(cycles,result.diagnostic);
                x=smo_d12_sub(x,pr_product(two_pi,smo_d12_uint(whole)));if(smo_d12_compare(x,pi)>0)x=smo_d12_sub(x,two_pi);}
            if(angle.negative!=0u)x=smo_d12_negate(x);bool negative_cosine=false;
            if(smo_d12_compare(x,half_pi)>0){x=smo_d12_sub(pi,x);negative_cosine=true;}
            else if(smo_d12_compare(x,smo_d12_negate(half_pi))<0){x=smo_d12_sub(smo_d12_negate(pi),x);negative_cosine=true;}
            result.diagnostic|=x.diagnostic|half_pi.diagnostic;
            if((result.diagnostic&65535u)==0u){SmoD12 squared=pr_product(x,x),sine=x,cosine=smo_d12_uint(1u),sine_term=x,cosine_term=cosine;
                F12_LOOP for(uint n=1u;n<=12u;++n){SmoD12 old_sine=sine,old_cosine=cosine;
                    sine_term=smo_d12_negate(pr_quotient(pr_product(sine_term,squared),smo_d12_uint((2u*n)*(2u*n+1u))));
                    cosine_term=smo_d12_negate(pr_quotient(pr_product(cosine_term,squared),smo_d12_uint((2u*n-1u)*(2u*n))));
                    sine=smo_d12_add(sine,sine_term);cosine=smo_d12_add(cosine,cosine_term);
                    result.diagnostic|=sine.diagnostic|cosine.diagnostic|sine_term.diagnostic|cosine_term.diagnostic;
                    if(smo_d12_compare(old_sine,sine)==0&&smo_d12_compare(old_cosine,cosine)==0)break;}
                result.sine=sine;if(negative_cosine)cosine=smo_d12_negate(cosine);result.cosine=cosine;
            }
        }
    }
    result.sine.diagnostic|=result.diagnostic;result.cosine.diagnostic|=result.diagnostic;return result;}

uint pr_color(uint offset,inout uint diagnostic){uint red=0u,green=0u,blue=0u;
    for(uint axis=0u;axis<3u;++axis){SmoD12 c=smo_d12_load(2u,offset+axis*6u);c=smo_d12_clamp(c,smo_d12_zero(),smo_d12_uint(1u));
        c=smo_d12_mul(c,smo_d12_uint(255u));diagnostic|=c.diagnostic;uint channel=smo_d12_round_uint(c,diagnostic);
        if(axis==0u)red=channel;else if(axis==1u)green=channel;else blue=channel;}
    return 0xff000000u|(red<<16u)|(green<<8u)|blue;}
void pr_setup(){uint phase=parameters[1].w,frame_offset=parameters[1].z,vertex_count=parameters[0].z,face_count=parameters[0].w;
    if(phase==0u){uint diagnostic=word(2u,frame_offset+39u);for(uint axis=0u;axis<3u;++axis){PrTrig trig=pr_sincos(smo_d12_load(2u,frame_offset+axis*6u));
            smo_d12_store(4u,axis*12u,trig.sine);smo_d12_store(4u,axis*12u+6u,trig.cosine);diagnostic|=trig.diagnostic;}
        uint color=pr_color(frame_offset+18u,diagnostic);if(vertex_count>1024u||face_count>2048u||word(2u,frame_offset+36u)!=vertex_count||word(2u,frame_offset+37u)!=face_count)diagnostic|=16u;
        put(4u,36u,color);put(4u,37u,diagnostic);put(4u,38u,word(2u,frame_offset+38u)!=0u?1u:0u);put(4u,39u,0u);
    }else{uint diagnostic=word(4u,37u);
        F12_LOOP for(uint i=0u;i<vertex_count;++i)diagnostic|=word(3u,i*20u+18u);
        F12_LOOP for(uint i=0u;i<face_count;++i){uint at=parameters[1].y+i*3u;
            if(word(1u,at)>=vertex_count||word(1u,at+1u)>=vertex_count||word(1u,at+2u)>=vertex_count)diagnostic|=16u;}
        put(4u,37u,diagnostic);}
}
void pr_transform(uint index){if(index>=parameters[0].z)return;PrVector p=pr_load(0u,parameters[1].x+index*18u);uint diagnostic=pr_diagnostic(p)|word(4u,37u);
    SmoD12 bound=smo_d12_uint(1000000u);if(!smo_d12_valid(p.x)||!smo_d12_valid(p.y)||!smo_d12_valid(p.z)||
        smo_d12_compare(smo_d12_abs(p.x),bound)>0||smo_d12_compare(smo_d12_abs(p.y),bound)>0||smo_d12_compare(smo_d12_abs(p.z),bound)>0)diagnostic|=4u;
    if((diagnostic&65535u)==0u){SmoD12 sx=smo_d12_load(4u,0u),cx=smo_d12_load(4u,6u),sy=smo_d12_load(4u,12u),cy=smo_d12_load(4u,18u),sz=smo_d12_load(4u,24u),cz=smo_d12_load(4u,30u);
        SmoD12 y=smo_d12_sub(pr_product(p.y,cx),pr_product(p.z,sx)),z=smo_d12_add(pr_product(p.y,sx),pr_product(p.z,cx));p.y=y;p.z=z;
        SmoD12 x=smo_d12_add(pr_product(p.x,cy),pr_product(p.z,sy));z=smo_d12_sub(pr_product(p.z,cy),pr_product(p.x,sy));p.x=x;p.z=z;
        x=smo_d12_sub(pr_product(p.x,cz),pr_product(p.y,sz));y=smo_d12_add(pr_product(p.x,sz),pr_product(p.y,cz));p.x=x;p.y=y;p.z=smo_d12_add(p.z,smo_d12_uint(4u));diagnostic|=pr_diagnostic(p);}
    pr_store(3u,index*20u,p);put(3u,index*20u+18u,diagnostic);put(3u,index*20u+19u,0u);}
// Cache80words: e1[0..17],e2[18..35],t[36..53],q[54..71],
// depth numerator[72..77], original vertex diagnostics78, bounds guard79.
// Every arithmetic tree is identical to the unoptimized CPU oracle. q and
// numerator diagnostics MUST NOT enter the frame/global summary: a ray which
// fails u/v must not observe an operation it never reached originally.
void pr_prepare_face(uint index){if(index>=parameters[0].w)return;uint offset=index*80u,source=parameters[1].y+index*3u;
    uint ia=word(1u,source),ib=word(1u,source+1u),ic=word(1u,source+2u),diagnostic=0u,valid=0u;
    PrVector e1=pr_vector(smo_d12_zero(),smo_d12_zero(),smo_d12_zero()),e2=e1,t=e1,q=e1;SmoD12 numerator=smo_d12_zero();
    if(ia<parameters[0].z&&ib<parameters[0].z&&ic<parameters[0].z){PrVector a=pr_load(3u,ia*20u),b=pr_load(3u,ib*20u),c=pr_load(3u,ic*20u);
        diagnostic=pr_diagnostic(a)|pr_diagnostic(b)|pr_diagnostic(c);e1=pr_sub(b,a);e2=pr_sub(c,a);t=pr_negative(a);q=pr_cross(t,e1);numerator=pr_dot(e2,q);valid=1u;
    }else diagnostic|=16u;
    pr_store(6u,offset,e1);pr_store(6u,offset+18u,e2);pr_store(6u,offset+36u,t);pr_store(6u,offset+54u,q);smo_d12_store(6u,offset+72u,numerator);
    put(6u,offset+78u,diagnostic);put(6u,offset+79u,valid);}
PrHit pr_triangle(PrVector direction,uint face){PrHit result;result.depth=smo_d12_zero();result.hit=0u;
#if SMO_RENDER_STAGE==4
    uint source=parameters[1].y+face*3u,ia=word(1u,source),ib=word(1u,source+1u),ic=word(1u,source+2u);result.diagnostic=pr_diagnostic(direction);
    if(ia>=parameters[0].z||ib>=parameters[0].z||ic>=parameters[0].z)result.diagnostic|=16u;
    else{PrVector a=pr_load(3u,ia*20u),b=pr_load(3u,ib*20u),c=pr_load(3u,ic*20u);result.diagnostic|=pr_diagnostic(a)|pr_diagnostic(b)|pr_diagnostic(c);
        PrVector e1=pr_sub(b,a),e2=pr_sub(c,a),p=pr_cross(direction,e2);SmoD12 determinant=pr_dot(e1,p);result.diagnostic|=determinant.diagnostic;
#else
    uint offset=face*80u;result.diagnostic=word(6u,offset+78u)|pr_diagnostic(direction);
    if(word(6u,offset+79u)==0u)result.diagnostic|=16u;
    else{PrVector e1=pr_load(6u,offset),e2=pr_load(6u,offset+18u),p=pr_cross(direction,e2);SmoD12 determinant=pr_dot(e1,p);result.diagnostic|=determinant.diagnostic;
        if((result.diagnostic&65535u)==0u&&!smo_d12_empty(determinant)){SmoD12 inverse=pr_quotient(smo_d12_uint(1u),determinant);PrVector t=pr_load(6u,offset+36u);
#endif
#if SMO_RENDER_STAGE==4
        if((result.diagnostic&65535u)==0u&&!smo_d12_empty(determinant)){SmoD12 inverse=pr_quotient(smo_d12_uint(1u),determinant);PrVector t=pr_negative(a);
#endif
            SmoD12 u=pr_product(pr_dot(t,p),inverse);result.diagnostic|=inverse.diagnostic|u.diagnostic;
            if(smo_d12_valid(u)&&smo_d12_compare(u,smo_d12_zero())>=0&&smo_d12_compare(u,smo_d12_uint(1u))<=0){
#if SMO_RENDER_STAGE==4
                PrVector q=pr_cross(t,e1);
#else
                PrVector q=pr_load(6u,offset+54u);
#endif
                SmoD12 v=pr_product(pr_dot(direction,q),inverse),sum=smo_d12_add(u,v);result.diagnostic|=v.diagnostic|sum.diagnostic;
                if(smo_d12_valid(v)&&smo_d12_compare(v,smo_d12_zero())>=0&&smo_d12_compare(sum,smo_d12_uint(1u))<=0){
#if SMO_RENDER_STAGE==4
                    SmoD12 numerator=pr_dot(e2,q);
#else
                    SmoD12 numerator=smo_d12_load(6u,offset+72u);
#endif
                    SmoD12 depth=pr_product(numerator,inverse);result.diagnostic|=depth.diagnostic;
                    if(smo_d12_valid(depth)&&smo_d12_compare(depth,smo_d12_literal(0u,1u,1u,0u))>=0&&smo_d12_compare(depth,smo_d12_uint(64u))<=0){result.depth=depth;result.hit=1u;}}}}}
    return result;}
void pr_raster(uint pixel){uint width=parameters[0].x,height=parameters[0].y;if(pixel>=width*height)return;
    uint diagnostic=word(4u,37u),face=0xffffffffu,color=0xff101820u;SmoD12 depth=smo_d12_zero();
    if(word(4u,38u)!=0u&&(diagnostic&65535u)==0u){uint x=pixel%width,y=pixel/width;
        SmoD12 half_unit=smo_d12_literal(0u,6u,1u,0u),center_x=pr_quotient(smo_d12_uint(width),smo_d12_uint(2u)),center_y=pr_quotient(smo_d12_uint(height),smo_d12_uint(2u));
        SmoD12 focal=smo_d12_mul(smo_d12_uint(min(width,height)),smo_d12_literal(1u,6u,1u,0u));
        PrVector direction=pr_vector(pr_quotient(smo_d12_sub(smo_d12_add(smo_d12_uint(x),half_unit),center_x),focal),
            pr_quotient(smo_d12_sub(center_y,smo_d12_add(smo_d12_uint(y),half_unit)),focal),smo_d12_uint(1u));diagnostic|=pr_diagnostic(direction);
        F12_LOOP for(uint i=0u;i<parameters[0].w;++i){PrHit hit=pr_triangle(direction,i);diagnostic|=hit.diagnostic;
            if(hit.hit!=0u&&(face==0xffffffffu||smo_d12_compare(hit.depth,depth)<0)){face=i;depth=hit.depth;}}
        if(face!=0xffffffffu)color=word(4u,36u);}
    if((diagnostic&65535u)!=0u)color=0xffff00ffu;
    smo_d12_store(5u,pixel*9u,depth);put(5u,pixel*9u+6u,color);put(5u,pixel*9u+7u,face);put(5u,pixel*9u+8u,diagnostic);}
void pr_main(uint index){
#if SMO_RENDER_STAGE==0
    if(index==0u)pr_setup();
#elif SMO_RENDER_STAGE==1
    pr_transform(index);
#elif SMO_RENDER_STAGE==2 || SMO_RENDER_STAGE==4
    pr_raster(index);
#elif SMO_RENDER_STAGE==3
    pr_prepare_face(index);
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){pr_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){pr_main(gl_GlobalInvocationID.x);}
#endif

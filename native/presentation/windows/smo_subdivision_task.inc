// Real SMO postfix update operations, compiled on the host without evaluating
// per-object animation. All F12 state and arithmetic stay on this GPU.
SmoD12 subdivision_scale(SmoD12 a,SmoD12 b,bool division){
    uint diagnostic=smo_d12_diagnostics(a)|smo_d12_diagnostics(b);
    if((diagnostic&D12_FATAL)!=0u)return smo_d12_failure(diagnostic);
    if(b.negative==0u&&b.digits.x==0u&&b.digits.y==0u&&b.digits.z==1728u){
        F12Number value=f12_unpack(a),factor=f12_unpack(b);
        value.exponent+=(division?-1:1)*(factor.exponent+11);
        value.diagnostic=diagnostic;return f12_pack(value);
    }
    if(division)return smo_d12_div(a,b);return smo_d12_mul(a,b);
}
// This task needs exact grid integers, not the shared COLOR conversion whose
// documented range is 0..255. Reject fractional/out-of-domain values; never
// silently round or clamp a geometric coordinate to an 8-bit channel.
uint subdivision_exact_grid(SmoD12 value,inout uint diagnostic){
    diagnostic|=smo_d12_diagnostics(value);
    if((diagnostic&D12_FATAL)!=0u)return 0u;
    uint bound=parameters[0].w*32u; //<=32*12^6 <12^8 and UINT32_MAX/8
    if(value.negative!=0u||smo_d12_compare(value,smo_d12_uint(bound))>0){diagnostic|=4u;return 0u;}
    if(smo_d12_empty(value))return 0u;
    F12Number n=f12_unpack(value);F12Wide coefficient=f12_coefficient(n.coefficient);
    // A valid canonical nonzero value <=32*12^6 has a negative exponent.
    if(n.exponent>=0){diagnostic|=4u;return 0u;}
    F12_LOOP for(int digit=0;digit< -n.exponent;++digit){uint remainder=0u;coefficient=f12_div_small(coefficient,12u,remainder);if(remainder!=0u)diagnostic|=4u;}
    if((coefficient.low.z|coefficient.low.w|coefficient.high.x|coefficient.high.y|coefficient.high.z|coefficient.high.w)!=0u){diagnostic|=4u;return 0u;}
    uint result=coefficient.low.x+coefficient.low.y*20736u;
    if(result>bound){diagnostic|=4u;return 0u;}return result;
}
// The competitive conventional control receives source-derived affine rows.
// Their common small divisors deserve the same constant-divisor opportunity
// a conventional compiler has; a general integer divider is only fallback.
uint subdivision_grid_divide(uint numerator,uint denominator){
    if(denominator==1u)return numerator;if(denominator==2u)return numerator/2u;
    if(denominator==3u)return numerator/3u;if(denominator==4u)return numerator/4u;
    if(denominator==6u)return numerator/6u;if(denominator==12u)return numerator/12u;
    return numerator/denominator;
}
void subdivision_task(uint id){
    uint count=parameters[0].x,steps=parameters[0].y,op_count=parameters[0].z,scale=parameters[0].w;
#if SMO_SUBDIVISION_STAGE==3
    if(id<parameters[1].x*parameters[1].y)put(7u,id,0xff1e1e1eu);
#else
    if(id>=count)return;
#if SMO_SUBDIVISION_STAGE==6
    // Static lowering of the canonical, HOST-VERIFIED parsed instruction
    // stream. This is exactly the authored operation order, with F12 packets
    // throughout. No affine fusion, alternate representation or removed
    // rounding point is used; only VM dispatch/stack traffic is eliminated.
    SmoD12 x=smo_d12_uint(word(0u,id*2u)),y=smo_d12_uint(word(0u,id*2u+1u));
    SmoD12 fraction_half=smo_d12_load(4u,parameters[1].z*6u),third=smo_d12_load(4u,parameters[1].w*6u);
    SmoD12 sixth=smo_d12_load(4u,parameters[2].x*6u),twelve=smo_d12_load(4u,parameters[2].y*6u);
    F12_LOOP for(uint step=0u;step<steps;++step){
        x=smo_d12_mul(x,fraction_half);x=smo_d12_add(x,third);
        x=subdivision_scale(x,twelve,false);x=subdivision_scale(x,twelve,true);
        y=smo_d12_mul(y,third);y=smo_d12_add(y,sixth);
        y=subdivision_scale(y,twelve,false);y=subdivision_scale(y,twelve,true);
    }
    smo_d12_store(1u,id*12u,x);smo_d12_store(1u,id*12u+6u,y);
#elif SMO_SUBDIVISION_STAGE==0 || SMO_SUBDIVISION_STAGE==1
    SmoD12 states[2];states[0]=smo_d12_uint(word(0u,id*2u));states[1]=smo_d12_uint(word(0u,id*2u+1u));
    SmoD12 stack[8];F12_LOOP for(uint initialize=0u;initialize<8u;++initialize)stack[initialize]=smo_d12_zero();
    F12_LOOP for(uint step=0u;step<steps;++step){uint top=0u;
        F12_LOOP for(uint pc=0u;pc<op_count;++pc){uint opcode=word(2u,pc*2u),argument=word(2u,pc*2u+1u);
            if(opcode==0u)stack[top++]=smo_d12_load(4u,argument*6u);
            else if(opcode==1u)stack[top++]=states[argument];
            else if(opcode==15u){states[argument]=stack[--top];}
            else{SmoD12 right=stack[--top],left=stack[--top],result=smo_d12_zero();
                if(opcode==5u)result=smo_d12_add(left,right);
                else if(opcode==6u)result=smo_d12_sub(left,right);
#if SMO_SUBDIVISION_STAGE==1
                else result=subdivision_scale(left,right,opcode==8u);
#else
                else if(opcode==7u)result=smo_d12_mul(left,right);else result=smo_d12_div(left,right);
#endif
                stack[top++]=result;
            }
        }
    }
    smo_d12_store(1u,id*12u,states[0]);smo_d12_store(1u,id*12u+6u,states[1]);
#elif SMO_SUBDIVISION_STAGE==2
    uint x=word(0u,id*2u)*scale,y=word(0u,id*2u+1u)*scale,diagnostic=0u,base=op_count*2u;
    F12_LOOP for(uint step=0u;step<steps;++step){uint nx=word(2u,base)*x+word(2u,base+1u)*y+word(2u,base+2u)*scale;
        uint ny=word(2u,base+4u)*x+word(2u,base+5u)*y+word(2u,base+6u)*scale;
        uint dx=word(2u,base+3u),dy=word(2u,base+7u);
        uint qx=subdivision_grid_divide(nx,dx),qy=subdivision_grid_divide(ny,dy);
        if(qx*dx!=nx||qy*dy!=ny)diagnostic|=4u;x=qx;y=qy;
    }
    put(3u,id*3u,x);put(3u,id*3u+1u,y);put(3u,id*3u+2u,diagnostic);
#else
    uint gx=0u,gy=0u,diagnostic=0u;
#if SMO_SUBDIVISION_STAGE==4
    SmoD12 x=smo_d12_load(1u,id*12u),y=smo_d12_load(1u,id*12u+6u);
    // This final renderer bridge is measured separately. No binary floating
    // arithmetic is substituted for an intermediate F12 update operation.
    SmoD12 sx=smo_d12_mul(x,smo_d12_uint(scale)),sy=smo_d12_mul(y,smo_d12_uint(scale));
    diagnostic=smo_d12_diagnostics(sx)|smo_d12_diagnostics(sy);
    gx=subdivision_exact_grid(sx,diagnostic);gy=subdivision_exact_grid(sy,diagnostic);
#else
    gx=word(3u,id*3u);gy=word(3u,id*3u+1u);diagnostic=word(3u,id*3u+2u);
#endif
    put(5u,id*3u,gx);put(5u,id*3u+1u,gy);put(5u,id*3u+2u,diagnostic);
    uint px=(gx*8u/scale)%parameters[1].x,py=(gy*8u/scale)%parameters[1].y;
    if(diagnostic==0u)atomic_max(py*parameters[1].x+px,0xff70d58au);
#endif
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){subdivision_task(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){subdivision_task(gl_GlobalInvocationID.x);}
#endif

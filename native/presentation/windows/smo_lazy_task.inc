// Optional-channel task ABI: b0 parsed SMO postfix instructions (uint4), b1
// initial states (six binary32 words/item), b2 committed current states, b3
// eight-word observations, b4 literal bits. No CPU evaluates the updates.
// Modes 0/1 are the same validated SMO instruction stream, eager/deferred.
// Mode2 independently specializes the canonical source without a VM.
bool lazy_valid(float value){uint bits=BITS(value);return (bits&0x7f800000u)!=0x7f800000u&&((bits&0x7f800000u)!=0u||(bits&0x7fffffu)==0u);}
float lazy_mul(float a,float b){precise float result=a*b;if(a<0.0&&b<0.0)result=-result;return result;}
void lazy_main(uint item){
#if SMO_LAZY_MODE==3
    // Real frame target clear. It is included in the full pipeline timestamp.
    if(item<parameters[2].z*parameters[2].w)put(7u,item,0xff1d1d1du);
    return;
#elif SMO_LAZY_MODE==4
    if(item>=parameters[0].x)return;
    uint base=item*6u,w=parameters[2].z,h=parameters[2].w;
    precise float projected_x=REAL(word(2u,base+1u))*float(w-1u);
    precise float projected_y=REAL(word(2u,base+5u))*float(h-1u);
    uint x=uint(clamp(projected_x,0.0,float(w-1u))),y=h-1u-uint(clamp(projected_y,0.0,float(h-1u)));
    uint color=REAL(word(2u,base))==0.0?0xff707070u:0xff4bd5e5u;
    // Atomic maximum makes collisions deterministic across thread schedules.
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int px=int(x)+dx,py=int(y)+dy;
        if(px>=0&&py>=0&&px<int(w)&&py<int(h))atomic_max(uint(py)*w+uint(px),color);}
    return;
#else
    uint count=parameters[0].x;if(item>=count)return;
    uint pc_count=parameters[0].y,guard_begin=parameters[0].z,guard_end=parameters[0].w;
    uint frame_index=parameters[2].x,base=item*6u,out_base=item*8u;
    precise float values[6];for(uint slot=0u;slot<6u;++slot)values[slot]=REAL(word(frame_index==0u?1u:2u,base+slot));
    uint diagnostic=0u,operations=frame_index==0u?0u:word(3u,out_base+4u);
    uint writes=frame_index==0u?0u:word(3u,out_base+5u),version=frame_index==0u?0u:word(3u,out_base+2u);
    uint prior_writes=writes,prior_version=version;
    if(!lazy_valid(values[0]))diagnostic=1u;
    bool channel_active=values[0]!=0.0;
    if(diagnostic==0u){
#if SMO_LAZY_MODE==2
        // Same explicit deferred contract as mode1: the conventional control
        // skips the optional expression before evaluating any of its inputs.
        precise float delta=lazy_mul(values[2],REAL(parameters[1].w));
        precise float shared_value=values[1]+delta;
        precise float required=lazy_mul(shared_value,0.5)+0.25;
        if(!lazy_valid(values[1])||!lazy_valid(values[2])||!lazy_valid(delta)||!lazy_valid(shared_value)||!lazy_valid(required))diagnostic=1u;
        operations+=parameters[2].y;
        values[3]=shared_value;values[4]=required;values[1]=shared_value;
#ifdef SMO_HLSL
        [branch]
#endif
        if(channel_active&&diagnostic==0u){
            precise float value=values[5];
            for(uint iteration=0u;iteration<16u;++iteration){
                precise float a=lazy_mul(value,0.5);precise float b=lazy_mul(shared_value,0.5);
                value=a+b;if(!lazy_valid(a)||!lazy_valid(b)||!lazy_valid(value))diagnostic=1u;
            }
            operations+=guard_end-guard_begin;
            if(diagnostic==0u){values[5]=value;++version;}
        }
        if(diagnostic==0u)writes+=channel_active?4u:3u;
#else
        precise float stack[64];uint top=0u,local_diagnostic=0u;
#ifdef SMO_HLSL
        [loop]
#endif
        for(uint pc=0u;pc<pc_count;++pc){
#if SMO_LAZY_MODE==1
            // This jump is ahead of every exclusively optional expression
            // instruction, including VM variable evaluation and arithmetic.
            // Initial state reads remain necessary for retained observations;
            // it does not claim elimination of every physical memory load.
#ifdef SMO_HLSL
            [branch]
#endif
            if(pc==guard_begin&&!channel_active){pc=guard_end-1u;continue;}
#endif
            uint at=pc*4u,op=word(0u,at),operand=word(0u,at+1u);++operations;
            if(op==0u){float value=REAL(word(4u,operand));if(!lazy_valid(value))local_diagnostic=1u;stack[top++]=value;}
            else if(op==1u){float value=values[operand];if(!lazy_valid(value))local_diagnostic=1u;stack[top++]=value;}
            else if(op==2u)stack[top++]=REAL(parameters[1].w);
            else if(op==5u||op==6u||op==7u){
                precise float b=stack[--top];precise float a=stack[--top];precise float value=0.0;
                if(op==5u)value=a+b;else if(op==6u)value=a-b;else value=lazy_mul(a,b);
                if(!lazy_valid(value))local_diagnostic=1u;stack[top++]=value;
            }else if(op==15u){
                precise float value=stack[--top];
                bool optional=operand==5u;
                if(optional&&!channel_active){
                    // Eager mode does evaluate this expression. Its private
                    // result/error has no observer under the deferred-channel
                    // contract; all required diagnostics remain visible.
                    local_diagnostic=0u;
                }else if(local_diagnostic==0u){values[operand]=value;++writes;if(optional)++version;}
                if(local_diagnostic!=0u){diagnostic=local_diagnostic;break;}
            }
        }
#endif
    }
    if(diagnostic==0u){
        // Only assigned slots are committed. In particular, do not rewrite
        // the previous optional bits when inactive: its write is truly absent.
        put(2u,base+1u,BITS(values[1]));put(2u,base+3u,BITS(values[3]));put(2u,base+4u,BITS(values[4]));
        if(channel_active)put(2u,base+5u,BITS(values[5]));
    }
    else {writes=prior_writes;version=prior_version;}
    // Error batches are not performance inputs. Observations preserve their
    // diagnostics; the runner rejects any unexplained discrepancy.
    put(3u,out_base,BITS(values[4]));put(3u,out_base+1u,BITS(values[5]));
    // The optional write-version is a persistent destination field too. An
    // inactive/failed update must not even rewrite its unchanged value.
    if(channel_active&&diagnostic==0u)put(3u,out_base+2u,version);
    put(3u,out_base+3u,diagnostic);
    put(3u,out_base+4u,operations);put(3u,out_base+5u,writes);
    put(3u,out_base+6u,BITS(values[3]));put(3u,out_base+7u,BITS(values[0]));
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){lazy_main(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;
void main(){lazy_main(gl_GlobalInvocationID.x);}
#endif

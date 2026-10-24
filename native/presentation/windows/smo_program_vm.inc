// Postfix SMO VM. Each compiled opcode executes on the GPU; the host supplies
// only IR, literal packets and dispatch order. No floating types or CPU math.
// See smo_program_gpu.hpp for the versioned F12/buffer/transaction contract.
uint vm_parameter(uint index){return parameters[index/4u][index%4u];}
uint vm_state_offset(uint slot){return (vm_parameter(4u)*vm_parameter(6u)+slot)*13u;}
uint vm_frame_offset(){return vm_parameter(4u)*40u;}
bool vm_ok(){return word(6u,0u)==0u;}
void vm_fail(uint code,uint diagnostic){if(vm_ok()){
    put(6u,0u,code);put(6u,1u,diagnostic);put(6u,2u,vm_parameter(1u));put(6u,3u,vm_parameter(2u));put(6u,4u,vm_parameter(3u));}}
SmoD12 vm_number(uint storage,uint offset){SmoD12 n;
    n.digits=uint4(word(storage,offset),word(storage,offset+1u),word(storage,offset+2u),word(storage,offset+3u));
    n.negative=word(storage,offset+4u);n.diagnostic=word(storage,offset+5u);return n;}
void vm_number_store(uint storage,uint offset,SmoD12 n){put(storage,offset,n.digits.x);put(storage,offset+1u,n.digits.y);put(storage,offset+2u,n.digits.z);
    put(storage,offset+3u,n.digits.w);put(storage,offset+4u,n.negative);put(storage,offset+5u,n.diagnostic);}
SmoD12Result vm_result(uint storage,uint offset){SmoD12Result n;n.primary=vm_number(storage,offset);n.alternative=vm_number(storage,offset+6u);n.flags=word(storage,offset+12u);return n;}
void vm_result_store(uint storage,uint offset,SmoD12Result n){vm_number_store(storage,offset,n.primary);vm_number_store(storage,offset+6u,n.alternative);put(storage,offset+12u,n.flags);}
SmoD12Result vm_plain(SmoD12 value){SmoD12Result n;n.primary=value;n.alternative=smo_d12_failure(4u);n.flags=smo_d12_valid(value)?1u:0u;return n;}
bool vm_primary(SmoD12Result value){uint diagnostic=smo_d12_diagnostics(value.primary);
    bool valid=(value.flags&1u)!=0u&&(diagnostic&65535u)==0u;if(!valid)vm_fail(1u,diagnostic);return valid;}
void vm_push(SmoD12Result value){if(!vm_primary(value))return;uint size=word(6u,5u);
    if(size>=vm_parameter(7u)){vm_fail(2u,0u);return;}vm_result_store(2u,size*13u,value);put(6u,5u,size+1u);
    uint offset=vm_frame_offset()+39u;put(5u,offset,word(5u,offset)|value.primary.diagnostic);}
bool vm_need(uint count){bool valid=word(6u,5u)>=count;if(!valid)vm_fail(2u,0u);return valid;}
SmoD12Result vm_take(){uint size=word(6u,5u)-1u;put(6u,5u,size);return vm_result(2u,size*13u);}
// Face indices are dimensionless integers. Decode the F12 positional scale
// with exact integer division; never truncate or round a fractional index.
bool vm_face_index(SmoD12 value,out uint index){index=0u;uint diagnostic=smo_d12_diagnostics(value);
    if((diagnostic&65535u)!=0u||value.negative!=0u){vm_fail(5u,diagnostic);return false;}
    if(smo_d12_empty(value))return true;
    F12Number n=f12_unpack(value);if(n.exponent>=0||n.exponent< -12){vm_fail(5u,4u);return false;}
    uint4 coefficient=n.coefficient;
    for(int iteration=0;iteration< -n.exponent;++iteration){uint remainder=0u,part=coefficient.z;coefficient.z=part/12u;remainder=part%12u;
        part=remainder*20736u+coefficient.y;coefficient.y=part/12u;remainder=part%12u;
        part=remainder*20736u+coefficient.x;coefficient.x=part/12u;remainder=part%12u;
        if(remainder!=0u){vm_fail(5u,4u);return false;}}
    if((coefficient.y|coefficient.z|coefficient.w)!=0u||coefficient.x>=vm_parameter(8u)){vm_fail(5u,4u);return false;}
    index=coefficient.x;return true;}

void vm_execute(uint id){
#if SMO_VM_OPCODE == 19
    // Prepare a private destination bank. Failure later never changes the
    // bank selected by the host, its state, geometry or previous draw result.
    uint states=vm_parameter(6u)*13u,vertices=vm_parameter(8u)*18u,faces=vm_parameter(9u)*3u;
    uint destination=vm_parameter(4u),previous=vm_parameter(5u);bool reset=vm_parameter(11u)!=0u;
    if(id<states)put(1u,destination*states+id,reset?0u:word(1u,previous*states+id));
    if(!reset){if(id<vertices)put(3u,destination*vertices+id,word(3u,previous*vertices+id));
        if(id<faces)put(4u,destination*faces+id,word(4u,previous*faces+id));}
    if(id==0u){uint offset=destination*40u;for(uint i=0u;i<40u;++i)put(5u,offset+i,0u);
        SmoD12 zero=smo_d12_zero(),one=smo_d12_uint(1u);for(uint axis=0u;axis<3u;++axis){vm_number_store(5u,offset+axis*6u,zero);vm_number_store(5u,offset+18u+axis*6u,one);}
        if(!reset){put(5u,offset+36u,word(5u,previous*40u+36u));put(5u,offset+37u,word(5u,previous*40u+37u));}
        put(6u,5u,0u);
        if(vm_parameter(12u)!=0u){SmoD12 dt=vm_number(6u,16u);uint diagnostic=smo_d12_diagnostics(dt);
            if((diagnostic&65535u)!=0u||dt.negative!=0u)vm_fail(1u,diagnostic|4u);}}
#else
    if(id!=0u||!vm_ok())return;
#if SMO_VM_OPCODE == 20
    if(word(6u,5u)!=0u){vm_fail(2u,0u);return;}uint offset=vm_frame_offset(),vertices=word(5u,offset+36u),faces=word(5u,offset+37u);
    if(vertices<3u||vertices>vm_parameter(8u)||faces==0u||faces>vm_parameter(9u)){vm_fail(7u,0u);return;}
    uint face_start=vm_parameter(4u)*vm_parameter(9u)*3u;for(uint i=0u;i<faces*3u;++i)if(word(4u,face_start+i)>=vertices){vm_fail(5u,4u);return;}
#elif SMO_VM_OPCODE == 21
    // The only time input is an unsigned elapsed count, split into two words.
    // Scaling/division into seconds is F12 arithmetic executed on this GPU.
    // Both *65536 steps stay exact: every limb*65536+carry fits uint32, and
    // the full uint64 magnitude fits five radix20736 limbs. Do not round the
    // high half to F12 before adding low; that would introduce another RNE.
    F12Wide exact=f12_small(word(6u,23u));exact=f12_scale(exact,65536u);exact=f12_scale(exact,65536u);
    exact=f12_add_wide(exact,f12_small(word(6u,22u)));
    SmoD12 count=f12_pack(f12_normalize(exact,0,0u,0u)),dt=smo_d12_div(count,smo_d12_uint(1000000000u));
    uint diagnostic=smo_d12_diagnostics(dt);if((diagnostic&65535u)!=0u){vm_fail(1u,diagnostic);return;}vm_number_store(6u,16u,dt);
#elif SMO_VM_OPCODE == 0
    uint index=vm_parameter(0u);if(index>=vm_parameter(10u)){vm_fail(8u,0u);return;}vm_push(vm_plain(vm_number(0u,index*6u)));
#elif SMO_VM_OPCODE == 1
    uint index=vm_parameter(0u);if(index>=vm_parameter(6u)){vm_fail(8u,0u);return;}SmoD12Result value=vm_result(1u,vm_state_offset(index));
    if((value.flags&1u)==0u){vm_fail(3u,0u);return;}vm_push(value);
#elif SMO_VM_OPCODE == 2
    vm_push(vm_plain(vm_number(6u,16u)));
#elif SMO_VM_OPCODE == 3 || SMO_VM_OPCODE == 4 || SMO_VM_OPCODE == 11
    if(!vm_need(1u))return;SmoD12Result value=vm_take();if(!vm_primary(value))return;
#if SMO_VM_OPCODE == 3
    vm_push(vm_plain(smo_d12_negate(value.primary)));
#elif SMO_VM_OPCODE == 4
    vm_push(vm_plain(value.primary));
#else
    if((value.flags&2u)==0u||!smo_d12_valid(value.alternative)){vm_fail(4u,value.alternative.diagnostic);return;}vm_push(vm_plain(value.alternative));
#endif
#elif SMO_VM_OPCODE >= 5 && SMO_VM_OPCODE <= 10
    if(!vm_need(2u))return;SmoD12Result right=vm_take();SmoD12Result left=vm_take();if(!vm_primary(left)||!vm_primary(right))return;
    SmoD12 a=left.primary,b=right.primary;
    SmoD12Result value=smo_d12_evaluate(uint(SMO_VM_OPCODE-5),a,b,vm_parameter(13u));
    vm_push(value);
#elif SMO_VM_OPCODE == 12 || SMO_VM_OPCODE == 15
    if(!vm_need(1u))return;uint index=vm_parameter(0u);if(index>=vm_parameter(6u)){vm_fail(8u,0u);return;}SmoD12Result value=vm_take();
    if(vm_primary(value))vm_result_store(1u,vm_state_offset(index),value);
#elif SMO_VM_OPCODE == 13 || SMO_VM_OPCODE == 14 || SMO_VM_OPCODE == 16 || SMO_VM_OPCODE == 17
    if(!vm_need(3u))return;SmoD12Result z=vm_take();SmoD12Result y=vm_take();SmoD12Result x=vm_take();if(!vm_primary(x)||!vm_primary(y)||!vm_primary(z))return;
    uint offset=vm_frame_offset();
#if SMO_VM_OPCODE == 13 || SMO_VM_OPCODE == 16
    // Renderer envelope, separate from the wider F12 number domain. Diagnose
    // it at the authored statement rather than losing source location later.
    SmoD12 graphic_limit=smo_d12_uint(1000000u);
    if(smo_d12_compare(smo_d12_abs(x.primary),graphic_limit)>0||smo_d12_compare(smo_d12_abs(y.primary),graphic_limit)>0||smo_d12_compare(smo_d12_abs(z.primary),graphic_limit)>0){vm_fail(9u,0u);return;}
#endif
#if SMO_VM_OPCODE == 13
    uint count=word(5u,offset+36u);if(count>=vm_parameter(8u)){vm_fail(8u,0u);return;}uint start=(vm_parameter(4u)*vm_parameter(8u)+count)*18u;
    vm_number_store(3u,start,x.primary);vm_number_store(3u,start+6u,y.primary);vm_number_store(3u,start+12u,z.primary);put(5u,offset+36u,count+1u);
#elif SMO_VM_OPCODE == 14
    uint count=word(5u,offset+37u);if(count>=vm_parameter(9u)){vm_fail(8u,0u);return;}uint ix=0u,iy=0u,iz=0u;
    if(!vm_face_index(x.primary,ix)||!vm_face_index(y.primary,iy)||!vm_face_index(z.primary,iz))return;
    uint start=(vm_parameter(4u)*vm_parameter(9u)+count)*3u;put(4u,start,ix);put(4u,start+1u,iy);put(4u,start+2u,iz);put(5u,offset+37u,count+1u);
#else
#if SMO_VM_OPCODE == 17
    SmoD12 zero=smo_d12_zero(),one=smo_d12_uint(1u);
    if(smo_d12_compare(x.primary,zero)<0||smo_d12_compare(x.primary,one)>0||smo_d12_compare(y.primary,zero)<0||smo_d12_compare(y.primary,one)>0||smo_d12_compare(z.primary,zero)<0||smo_d12_compare(z.primary,one)>0){vm_fail(6u,4u);return;}
    offset+=18u;
#endif
    vm_number_store(5u,offset,x.primary);vm_number_store(5u,offset+6u,y.primary);vm_number_store(5u,offset+12u,z.primary);
#endif
#elif SMO_VM_OPCODE == 18
    put(5u,vm_frame_offset()+38u,1u);
#else
    vm_fail(8u,0u);
#endif
#endif
}
#ifdef SMO_HLSL
[numthreads(64,1,1)]void CSMain(uint3 id:SV_DispatchThreadID){vm_execute(id.x);}
#else
layout(local_size_x=64,local_size_y=1,local_size_z=1)in;void main(){vm_execute(gl_GlobalInvocationID.x);}
#endif

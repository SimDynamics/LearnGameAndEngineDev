#include "core/smo_native.hpp"
#include "presentation/windows/smo_compute.hpp"
#include "presentation/windows/smo_native_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::smo;
using namespace lgaed::presentation;
using Clock=std::chrono::steady_clock;
using Words=std::vector<std::uint32_t>;
constexpr auto profile=NativeScalarProfile::ordinary_binary32;
std::size_t checks=0;
void require(bool value,std::string_view reason){++checks;if(!value)throw std::runtime_error(std::string(reason));}
enum class Kind {particles,triangles};
std::string_view kind_name(Kind kind){return kind==Kind::particles?"3D particle oscillator":"three-vertex affine transforms (compute only, no raster)";}
float scalar(std::uint32_t bits){return std::bit_cast<float>(bits);}
std::uint32_t word(float value){return std::bit_cast<std::uint32_t>(value);}
float add(float a,float b){volatile float result=a+b;return result;}
float sub(float a,float b){volatile float result=a-b;return result;}
float mul(float a,float b){volatile float result=a*b;return result;}
std::span<const std::byte> bytes(std::span<const std::uint32_t> values){return std::as_bytes(values);}
std::uint64_t hash_bytes(std::span<const std::byte> data){std::uint64_t result=14695981039346656037ull;for(const auto item:data){result^=std::to_integer<unsigned char>(item);result*=1099511628211ull;}return result;}
std::uint32_t read_word(std::span<const std::byte> data,std::size_t index){require(index<data.size()/4,"A raw benchmark diagnostic crossed its GPU buffer.");std::uint32_t value=0;std::memcpy(&value,data.data()+index*4,4);return value;}

struct Builder {
    NativeGraph graph;std::vector<std::uint32_t> current;std::map<std::uint32_t,std::uint32_t> constants;std::uint32_t elapsed=native_no_value;
    explicit Builder(std::size_t states):current(states,native_no_value){}
    std::uint32_t value(NativeOpcode opcode,std::array<std::uint32_t,3> args={native_no_value,native_no_value,native_no_value},std::uint32_t immediate=0){NativeInstruction node;
        node.opcode=opcode;node.result=graph.value_count++;node.arguments=args;node.immediate=immediate;graph.instructions.push_back(node);return node.result;}
    std::uint32_t constant(std::uint32_t bits){if(const auto found=constants.find(bits);found!=constants.end())return found->second;const auto id=value(NativeOpcode::constant,{native_no_value,native_no_value,native_no_value},bits);constants.emplace(bits,id);return id;}
    std::uint32_t load(std::uint32_t slot){if(current.at(slot)==native_no_value)current[slot]=value(NativeOpcode::load_state,{native_no_value,native_no_value,native_no_value},slot);return current[slot];}
    std::uint32_t dt(){if(elapsed==native_no_value)elapsed=value(NativeOpcode::load_dt);return elapsed;}
    std::uint32_t binary(NativeOpcode op,std::uint32_t a,std::uint32_t b){return value(op,{a,b,native_no_value});}
    void store(std::uint32_t slot,std::uint32_t id){NativeInstruction node;node.opcode=NativeOpcode::store_state;node.arguments={id,native_no_value,native_no_value};node.immediate=slot;graph.instructions.push_back(node);current.at(slot)=id;}
};
struct Workload {Kind kind;std::uint32_t repeats=0;std::vector<std::string> names;std::string source;NativeProgram authored,manual;};
Workload workload(Kind kind,std::uint32_t repeats){require(repeats>=1&&repeats<=64,"The benchmark supports1..64 source repetitions within the existing language limits.");Workload result;result.kind=kind;result.repeats=repeats;
    if(kind==Kind::particles)result.names={"x","y","z","vx","vy","vz"};
    else{for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)result.names.push_back("m"+std::to_string(row)+std::to_string(column));
        result.names.insert(result.names.end(),{"tx","ty","tz"});for(const auto* prefix:{"p","o"})for(unsigned vertex=0;vertex<3;++vertex)for(const char axis:{'x','y','z'})result.names.push_back(std::string(prefix)+std::to_string(vertex)+axis);result.names.push_back("age");}
    for(const auto& name:result.names)result.source+="estado "+name+"=0;\n";result.source+="al_actualizar {\n";Builder manual(result.names.size());
    for(std::uint32_t repeat=0;repeat<repeats;++repeat){
        if(kind==Kind::particles){for(std::uint32_t axis=0;axis<3;++axis){const auto& x=result.names[axis];const auto& velocity=result.names[axis+3];result.source+=velocity+"="+velocity+"-"+x+"*dt*#;\n"+x+"="+x+"+"+velocity+"*dt;\n";
                const auto old_velocity=manual.load(axis+3),position=manual.load(axis),dt=manual.dt();const auto partial=manual.binary(NativeOpcode::multiply,position,dt),coefficient=manual.constant(0x41200000u);
                const auto impulse=manual.binary(NativeOpcode::multiply,partial,coefficient);
                const auto new_velocity=manual.binary(NativeOpcode::subtract,old_velocity,impulse);manual.store(axis+3,new_velocity);
                const auto movement=manual.binary(NativeOpcode::multiply,new_velocity,dt);manual.store(axis,manual.binary(NativeOpcode::add,position,movement));}}
        else{for(std::uint32_t vertex=0;vertex<3;++vertex){for(std::uint32_t axis=0;axis<3;++axis){const auto out=21u+vertex*3u+axis;const auto point=12u+vertex*3u;
                    result.source+=result.names[out]+"=(("+result.names[axis*3]+"*"+result.names[point]+"+"+result.names[axis*3+1]+"*"+result.names[point+1]+")+"+result.names[axis*3+2]+"*"+result.names[point+2]+")+"+result.names[9+axis]+";\n";
                    const auto m0=manual.load(axis*3),p0=manual.load(point),first=manual.binary(NativeOpcode::multiply,m0,p0);const auto m1=manual.load(axis*3+1),p1=manual.load(point+1),second=manual.binary(NativeOpcode::multiply,m1,p1);
                    const auto sum=manual.binary(NativeOpcode::add,first,second);const auto m2=manual.load(axis*3+2),p2=manual.load(point+2),third=manual.binary(NativeOpcode::multiply,m2,p2);
                    const auto partial=manual.binary(NativeOpcode::add,sum,third),translation=manual.load(9+axis);manual.store(out,manual.binary(NativeOpcode::add,partial,translation));}
                for(std::uint32_t axis=0;axis<3;++axis){result.source+=result.names[12+vertex*3+axis]+"="+result.names[21+vertex*3+axis]+";\n";manual.store(12+vertex*3+axis,manual.load(21+vertex*3+axis));}}}}
    if(kind==Kind::triangles){result.source+="age=age+dt*.6*10;\n";const auto previous=manual.load(30),dt=manual.dt(),half=manual.constant(0x3f000000u);
        const auto partial=manual.binary(NativeOpcode::multiply,dt,half),twelve=manual.constant(0x41400000u),increment=manual.binary(NativeOpcode::multiply,partial,twelve);manual.store(30,manual.binary(NativeOpcode::add,previous,increment));}
    result.source+="}\nal_dibujar {}\n";result.authored=native_compile(result.source,{},profile);require(result.authored.ir.states==result.names,"The source benchmark changed its state layout.");
    result.manual.scalarProfile=profile;result.manual.ir.states=result.names;result.manual.identity="independent-native-performance-"+std::string(kind==Kind::particles?"particles-":"triangles-")+std::to_string(repeats);
    Builder reset(result.names.size());for(std::uint32_t state=0;state<result.names.size();++state)reset.store(state,reset.constant(0));result.manual.actions[0]=std::move(reset.graph);result.manual.actions[2]=std::move(manual.graph);
    // Match ONLY diagnostic metadata after proving every independently
    // constructed numerical node/edge and instruction ordinal is identical.
    // Otherwise bare manual coordinates could let a backend remove guards'
    // payload-selection work and manufacture a false frontend speed gap.
    for(unsigned action=0;action<3;++action){const auto& authored=result.authored.actions[action];auto& control=result.manual.actions[action];require(authored.value_count==control.value_count&&authored.instructions.size()==control.instructions.size(),"Source/manual numerical graphs differ; diagnostic metadata cannot be equated safely.");
        for(std::size_t at=0;at<authored.instructions.size();++at){const auto& source_node=authored.instructions[at];auto& control_node=control.instructions[at];
            require(source_node.opcode==control_node.opcode&&source_node.result==control_node.result&&source_node.arguments==control_node.arguments&&source_node.immediate==control_node.immediate,
                "Independent manual control differs in numerical dataflow, not merely source coordinates.");control_node.line=source_node.line;control_node.column=source_node.column;}}
    return result;}

struct Inputs {Words states,dt;};
Inputs inputs(const Workload& work,std::uint32_t count){Inputs result;result.states.resize(static_cast<std::size_t>(count)*work.names.size());result.dt.resize(count);std::uint32_t random=0x32a012bdu;
    const auto next=[&]{random=random*1664525u+1013904223u;return random;};
    for(std::uint32_t item=0;item<count;++item){auto at=static_cast<std::size_t>(item)*work.names.size();result.dt[item]=(next()&1u)?0x3c000000u:0x3b800000u;
        if(work.kind==Kind::particles){for(unsigned axis=0;axis<3;++axis){auto n=static_cast<int>(next()%33u)-16;if(n==0)n=1;result.states[at+axis]=word(static_cast<float>(n)/8.0f);result.states[at+axis+3]=word(static_cast<float>(static_cast<int>(next()%17u)-8)/32.0f);}}
        else{for(unsigned axis=0;axis<3;++axis)result.states[at+axis*3+axis]=0x3f800000u;const auto fixed=next()%3u,first=(fixed+1u)%3u,second=(fixed+2u)%3u;
            const auto sine=0x3c000000u|((next()&1u)<<31u);result.states[at+first*3+first]=0x3f7ffe00u;result.states[at+second*3+second]=0x3f7ffe00u;
            result.states[at+first*3+second]=sine^0x80000000u;result.states[at+second*3+first]=sine;
            for(unsigned vertex=0;vertex<3;++vertex)for(unsigned axis=0;axis<3;++axis){auto value=static_cast<int>(next()%17u)-8;if(value==0)value=1;result.states[at+12+vertex*3+axis]=word(static_cast<float>(value)/8.0f);}}}
    return result;}
void cpu_step(const Workload& work,Words& state,std::uint32_t elapsed){const auto dt=scalar(elapsed);
    for(std::uint32_t repeat=0;repeat<work.repeats;++repeat){if(work.kind==Kind::particles){for(unsigned axis=0;axis<3;++axis){const auto position=scalar(state[axis]);const auto velocity=sub(scalar(state[axis+3]),mul(mul(position,dt),10.0f));state[axis+3]=word(velocity);state[axis]=word(add(position,mul(velocity,dt)));}}
        else for(unsigned vertex=0;vertex<3;++vertex){for(unsigned axis=0;axis<3;++axis){const auto point=12+vertex*3;const auto first=mul(scalar(state[axis*3]),scalar(state[point])),second=mul(scalar(state[axis*3+1]),scalar(state[point+1]));
                const auto partial=add(add(first,second),mul(scalar(state[axis*3+2]),scalar(state[point+2])));state[21+vertex*3+axis]=word(add(partial,scalar(state[9+axis])));}
            for(unsigned axis=0;axis<3;++axis)state[12+vertex*3+axis]=state[21+vertex*3+axis];}}
    if(work.kind==Kind::triangles)state[30]=word(add(scalar(state[30]),mul(mul(dt,0.5f),12.0f)));}

struct Artifact {SmoKernelSource shader;std::map<unsigned,std::uint64_t> spirv_ops;std::map<std::string,std::uint64_t> dxbc_ops;std::uint64_t native_hash=0;std::string native_kind;};
Artifact artifact(const NativeProgram& program,SmoComputeApi api){Artifact result;result.shader=smo_native_kernel(program,NativeAction::step);
    for(std::size_t at=5;at<result.shader.spirv.size();){const auto instruction=result.shader.spirv[at],length=instruction>>16,opcode=instruction&0xffffu;require(length&&length<=result.shader.spirv.size()-at,"Malformed benchmark SPIR-V.");++result.spirv_ops[opcode];at+=length;}
    if(api==SmoComputeApi::direct3d11||api==SmoComputeApi::direct3d12){Microsoft::WRL::ComPtr<ID3DBlob> code,errors,assembly;const bool version51=api==SmoComputeApi::direct3d12;
        const auto compiled=D3DCompile(result.shader.hlsl.data(),result.shader.hlsl.size(),result.shader.name.c_str(),nullptr,nullptr,"CSMain",version51?"cs_5_1":"cs_5_0",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf());
        if(FAILED(compiled)){std::string detail="Benchmark native shader compilation failed.";if(errors)detail.append(static_cast<const char*>(errors->GetBufferPointer()),std::min<std::size_t>(errors->GetBufferSize(),4096));throw std::runtime_error(detail);}
        auto& binary=version51?result.shader.dxbc51:result.shader.dxbc;binary.resize(code->GetBufferSize());std::memcpy(binary.data(),code->GetBufferPointer(),binary.size());result.native_hash=hash_bytes(binary);result.native_kind=version51?"DXBC cs_5_1":"DXBC cs_5_0";
        require(SUCCEEDED(D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,assembly.GetAddressOf())),"Could not inspect the exact DXBC submitted to the benchmark GPU.");
        std::istringstream listing(std::string(static_cast<const char*>(assembly->GetBufferPointer()),assembly->GetBufferSize()));std::string line;
        while(std::getline(listing,line)){std::istringstream words(line);std::string op;words>>op;if(!op.empty()&&!op.starts_with("//"))++result.dxbc_ops[op];}
    }else if(api==SmoComputeApi::vulkan){result.native_hash=hash_bytes(std::as_bytes(std::span(result.shader.spirv)));result.native_kind="SPIR-V submitted artifact (not hardware ISA)";}
    else{result.native_hash=hash_bytes(std::as_bytes(std::span(result.shader.glsl)));result.native_kind="GLSL submitted source; driver binary/ISA unavailable through public executor";}
    return result;}
void print_artifacts(const Workload& work,const Artifact& authored,const Artifact& manual){
    const auto graph_hist=[](const NativeGraph& graph){std::map<unsigned,std::uint64_t> result;for(const auto& instruction:graph.instructions)++result[static_cast<unsigned>(instruction.opcode)];return result;};
    const auto source_hist=graph_hist(work.authored.actions[2]),control_hist=graph_hist(work.manual.actions[2]);require(source_hist==control_hist,"Matched numerical graphs produced different SSA opcode counts.");
    std::cout<<"Artifacts source_bytes="<<work.source.size()<<" SSA_values="<<work.authored.actions[2].value_count<<" SSA_instructions="<<work.authored.actions[2].instructions.size()<<" diagnostic_metadata_matched_after_exact_dataflow_proof=true executed_source_numerals="<<(work.kind==Kind::particles?"#(ten)":".6(one_half)*10(twelve)")<<"\nSSA static opcode=count ";
    for(const auto& [opcode,count]:source_hist)std::cout<<opcode<<'='<<count<<',';std::cout<<"\nSPIR-V static float opcodes(source/manual): ";
    for(const unsigned opcode:{127u,129u,131u,133u,136u}){const auto count=[&](const Artifact& data){const auto found=data.spirv_ops.find(opcode);return found==data.spirv_ops.end()?0ull:found->second;};
        std::cout<<opcode<<'='<<count(authored)<<'/'<<count(manual)<<',';require(count(authored)==count(manual),"Source/manual submitted SPIR-V changes the numerical operation count.");}
    std::cout<<"\nActual artifact "<<authored.native_kind<<" FNV1a64 A=0x"<<std::hex<<authored.native_hash<<" B=0x"<<manual.native_hash<<std::dec
        <<" SPIR-V_FNV1a64 A=0x"<<std::hex<<hash_bytes(std::as_bytes(std::span(authored.shader.spirv)))<<" B=0x"<<hash_bytes(std::as_bytes(std::span(manual.shader.spirv)))<<std::dec
        <<" SPIR-V_bytes_equal="<<(authored.shader.spirv==manual.shader.spirv)<<" HLSL_bytes_equal="<<(authored.shader.hlsl==manual.shader.hlsl)<<" GLSL_bytes_equal="<<(authored.shader.glsl==manual.shader.glsl)<<'\n';
    if(!authored.dxbc_ops.empty()){std::cout<<"DXBC arithmetic(source/manual): ";for(const auto* op:{"add","mul","div","mad"}){const auto count=[&](const Artifact& data){const auto found=data.dxbc_ops.find(op);return found==data.dxbc_ops.end()?0ull:found->second;};
            std::cout<<op<<'='<<count(authored)<<'/'<<count(manual)<<',';require(count(authored)==count(manual),"Source/manual actual DXBC changes arithmetic instruction counts.");}std::cout<<"\nDXBC_bytes_equal="<<(authored.shader.dxbc==manual.shader.dxbc&&authored.shader.dxbc51==manual.shader.dxbc51)<<'\n';}
    std::cout<<"Artifact hashes are unmodified artifacts, not GPU machine-ISA hashes. No math guards or source coordinates were stripped.\n";}

struct Advance {double gpu_ms=0,wall_ms=0,status_ms=0;};
struct Visible {std::vector<std::byte> states,frames;};
class FlatProgram {
    std::unique_ptr<SmoCompute> gpu_;SmoKernel kernel_;std::array<SmoBuffer,8> buffers_{};std::vector<std::byte> zero_status_;std::uint32_t items_,states_,bank_=0;
public:
    FlatProgram(const NativeProgram& program,const Artifact& shader,SmoComputeApi api,const SmoComputeOptions& device,const Inputs& data,std::uint32_t items):gpu_(create_smo_compute(api,device)),items_(items),states_(static_cast<std::uint32_t>(program.ir.states.size())){
        require(program.scalarProfile==profile&&program.ir.vertices==0&&program.ir.faces==0&&data.states.size()==static_cast<std::size_t>(items_)*states_&&data.dt.size()==items_,"Flat benchmark inputs do not match their explicit native program.");
        kernel_=gpu_->kernel(shader.shader);Words state(data.states.size()*2u);std::copy(data.states.begin(),data.states.end(),state.begin());std::copy(data.states.begin(),data.states.end(),state.begin()+static_cast<std::ptrdiff_t>(data.states.size()));
        const auto allocate=[&](unsigned binding,const Words& values){buffers_[binding]=gpu_->buffer(values.size()*4u,bytes(values));};allocate(0,state);allocate(1,Words(1,0));allocate(2,Words(1,0));allocate(3,Words(static_cast<std::size_t>(items_)*20u,0));
        allocate(4,Words(static_cast<std::size_t>(items_)*4u,0));allocate(5,data.dt);allocate(6,Words(static_cast<std::size_t>(items_)*2u,0));zero_status_.resize(static_cast<std::size_t>(items_)*16u);}
    SmoComputeInfo info()const{return gpu_->info();}
    Advance advance(bool timed){const auto begin=Clock::now();const auto work=1u-bank_;SmoDispatch call;call.kernel=kernel_;call.buffers=buffers_;call.constants={items_,work,bank_,0};call.groups={(items_+63u)/64u,1,1};
        Advance result;result.gpu_ms=gpu_->execute(std::span(&call,1),timed);const auto read_start=Clock::now();const auto status=gpu_->read(buffers_[4]);result.status_ms=std::chrono::duration<double,std::milli>(Clock::now()-read_start).count();
        require(status.size()==zero_status_.size(),"A native performance action omitted part of its status output.");if(std::memcmp(status.data(),zero_status_.data(),status.size())!=0){
            for(std::uint32_t item=0;item<items_;++item){const auto code=read_word(status,item*4u);if(code){std::ostringstream message;message<<"Native performance action failed: API="<<info().api<<" item="<<item<<" code="<<code<<" line="<<read_word(status,item*4u+1u)<<" column="<<read_word(status,item*4u+2u)<<" instruction="<<read_word(status,item*4u+3u);throw std::runtime_error(message.str());}}
            throw std::runtime_error("A success status retained nonzero diagnostic payload.");}
        bank_=work;result.wall_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();return result;}
    Visible visible(){return {gpu_->read(buffers_[0]),gpu_->read(buffers_[3])};}
    std::span<const std::byte> active_states(const Visible& value)const{const auto stride=static_cast<std::size_t>(items_)*states_*4u;require(value.states.size()==stride*2u,"Raw state readback size changed.");return std::span(value.states).subspan(stride*bank_,stride);}
    std::span<const std::byte> active_frames(const Visible& value)const{const auto stride=static_cast<std::size_t>(items_)*native_frame_words*4u;require(value.frames.size()==stride*2u,"Raw frame readback size changed.");return std::span(value.frames).subspan(stride*bank_,stride);}
};

struct SparseOracle {std::vector<std::uint32_t> indices,dt;std::vector<Words> states;};
SparseOracle oracle(const Workload& work,const Inputs& data,std::uint32_t count){SparseOracle result;result.indices={0,count/2,count-1};std::ranges::sort(result.indices);result.indices.erase(std::unique(result.indices.begin(),result.indices.end()),result.indices.end());
    for(const auto item:result.indices){const auto at=static_cast<std::size_t>(item)*work.names.size();result.states.emplace_back(data.states.begin()+static_cast<std::ptrdiff_t>(at),data.states.begin()+static_cast<std::ptrdiff_t>(at+work.names.size()));result.dt.push_back(data.dt[item]);}return result;}
double verify_pair(FlatProgram& source,FlatProgram& control,const Workload& work,SparseOracle& reference){const auto start=Clock::now();const auto actual=source.visible(),manual=control.visible();const auto a=source.active_states(actual),b=control.active_states(manual),af=source.active_frames(actual),bf=control.active_frames(manual);
    require(a.size()==b.size()&&af.size()==bf.size(),"Source/manual visible raw buffers changed dimensions.");
    if(std::memcmp(a.data(),b.data(),a.size())!=0){for(std::size_t at=0;at<a.size()/4;++at)if(read_word(a,at)!=read_word(b,at)){std::ostringstream message;message<<"Exact native performance output mismatch: item="<<at/work.names.size()<<" state="<<work.names[at%work.names.size()]<<" source=0x"<<std::hex<<read_word(a,at)<<" manual=0x"<<read_word(b,at);throw std::runtime_error(message.str());}}
    require(std::memcmp(af.data(),bf.data(),af.size())==0,"Source/manual committed frame metadata differs.");
    // EVERY item/state/frame word is compared above, without a floating
    // tolerance. An independent CPU oracle also checks three persistent items.
    for(std::size_t sample=0;sample<reference.indices.size();++sample){cpu_step(work,reference.states[sample],reference.dt[sample]);const auto offset=static_cast<std::size_t>(reference.indices[sample])*work.names.size();
        for(std::size_t state=0;state<work.names.size();++state){const auto expected=reference.states[sample][state],value=read_word(a,offset+state);if(expected!=value){std::ostringstream message;
                message<<"Independent CPU/nonfused FP32 oracle mismatch: item="<<reference.indices[sample]<<" state="<<work.names[state]<<" expected=0x"<<std::hex<<expected<<" actual=0x"<<value;throw std::runtime_error(message.str());}}}
    return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
double median(const std::vector<double>& values){if(values.empty()||std::ranges::any_of(values,[](double value){return !std::isfinite(value)||value<=0;}))return std::numeric_limits<double>::quiet_NaN();auto ordered=values;std::ranges::sort(ordered);
    const auto middle=ordered.size()/2;return ordered.size()%2?ordered[middle]:(ordered[middle-1]+ordered[middle])/2.0;}
double total(const std::vector<double>& values){return std::accumulate(values.begin(),values.end(),0.0);}
struct Samples {std::vector<double> gpu,wall,status;void append(Advance sample){gpu.push_back(sample.gpu_ms);wall.push_back(sample.wall_ms);status.push_back(sample.status_ms);}};
struct Options {bool physical=false,sustained=false;std::uint32_t items=129,repeats=4,rounds=3;std::string selector="d3d11",workloads="all";};
std::uint32_t argument(std::string_view text,std::uint32_t maximum,std::string_view label){std::uint32_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    require(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&value>=1&&value<=maximum,std::string(label)+" is outside the declared benchmark budget.");return value;}
void run(const Options& options,SmoComputeApi api,const SmoComputeOptions& device,Kind kind){const auto preparation=Clock::now();const auto work=workload(kind,options.repeats);const auto data=inputs(work,options.items);
    const auto source_artifact=artifact(work.authored,api),control_artifact=artifact(work.manual,api);print_artifacts(work,source_artifact,control_artifact);
    FlatProgram source(work.authored,source_artifact,api,device,data,options.items),control(work.manual,control_artifact,api,device,data,options.items);auto reference=oracle(work,data,options.items);
    require(!options.physical||(!source.info().software&&!control.info().software),"A physical performance request was silently executed on software.");
    require(source.info().adapter==control.info().adapter,"The paired variants selected different physical adapters.");
    std::cout<<"BEGIN native source/manual benchmark api="<<source.info().api<<" adapter="<<std::quoted(source.info().adapter)<<" software="<<source.info().software<<" workload="<<std::quoted(kind_name(kind))
        <<" items="<<options.items<<" repetitions_per_dispatch="<<options.repeats<<" states_per_item="<<work.names.size()<<" preparation_compile_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-preparation).count()<<'\n';
    for(unsigned warmup=0;warmup<2;++warmup){if(warmup&1u){(void)control.advance(false);(void)source.advance(false);}else{(void)source.advance(false);(void)control.advance(false);}(void)verify_pair(source,control,work,reference);}
    Samples authored,native;std::vector<double> proof_ms;std::uint64_t rounds=0;const auto begin=Clock::now();double elapsed=0;
    do{require(rounds<1048576u,"The benchmark sample memory budget was exceeded before completion; no shortened sustained result is valid.");Advance a,b;
        if(rounds&1u){b=control.advance(true);a=source.advance(true);}else{a=source.advance(true);b=control.advance(true);}authored.append(a);native.append(b);
        proof_ms.push_back(verify_pair(source,control,work,reference));++rounds;elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
    }while(rounds<options.rounds||(options.sustained&&elapsed<60.0));
    const auto proof_start=Clock::now();const auto last=source.visible();const auto final_hash=hash_bytes(source.active_states(last));const double final_proof_ms=std::chrono::duration<double,std::milli>(Clock::now()-proof_start).count();
    const auto source_ms=median(authored.gpu),manual_ms=median(native.gpu);const bool valid=std::isfinite(source_ms)&&std::isfinite(manual_ms);
    const auto ratio=valid?source_ms/manual_ms:std::numeric_limits<double>::quiet_NaN();const auto active=valid?(total(authored.gpu)+total(native.gpu))/(elapsed*1000.0):std::numeric_limits<double>::quiet_NaN();
    std::cout<<std::setprecision(12)<<"RESULT api="<<source.info().api<<" workload="<<std::quoted(kind_name(kind))<<" items="<<options.items<<" repetitions="<<options.repeats<<" warmups=2 measured_pairs="<<rounds
        <<" elapsed_wall_seconds="<<elapsed<<" GPU_timing_valid="<<valid<<" source_GPU_median_ms="<<source_ms<<" manual_GPU_median_ms="<<manual_ms<<" source_over_manual_ratio="<<ratio
        <<" measured_compute_query_duty="<<active<<" source_runner_wall_total_ms="<<total(authored.wall)<<" manual_runner_wall_total_ms="<<total(native.wall)
        <<" source_status_readback_total_ms="<<total(authored.status)<<" manual_status_readback_total_ms="<<total(native.status)<<" full_acceptance_readback_and_CPU_oracle_total_ms="<<total(proof_ms)
        <<" final_hash_readback_ms="<<final_proof_ms<<" exact_item_comparisons="<<(rounds+2u)*options.items<<" exact_state_word_comparisons="<<(rounds+2u)*options.items*work.names.size()
        <<" mismatches=0 final_FNV1a64=0x"<<std::hex<<final_hash<<std::dec<<'\n';
    const auto show=[](std::string_view name,const std::vector<double>& samples){std::cout<<name<<"_GPU_ms_first=";for(std::size_t at=0;at<std::min<std::size_t>(16,samples.size());++at)std::cout<<samples[at]<<',';
        std::cout<<" sample_count="<<samples.size()<<" all_samples_used_for_median=true\n";};show("source",authored.gpu);show("manual",native.gpu);
    std::cout<<"Interpretation: identical precision, inputs, checked arithmetic, numerical dataflow and diagnostic metadata; state persists through warmups and all measured pairs. All active raw outputs are verified after every pair. CPU oracle covers three persistent items, not all items.\n"
        <<"GPU timings cover one compute dispatch per variant, including guards/copies/header writes; wall/status/full validation are reported separately. Duty is timed compute/observed wall, not hardware utilization.60s means wall duration, not60s fully occupied GPU.\n"
        <<"Neither source notation nor a timing ratio proves fewer vertices, fewer polygons, higher application FPS or a base12 arithmetic advantage. These are native binary32 operations; no F12/packed12 proxy is used.\n";
}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;Options options;if(argc>1){const std::string_view mode=argv[1];require(mode=="hardware"||mode=="sustained","Usage: smo_native_performance_tests [hardware|sustained [d3d11|d3d12|vulkan|opengl|all] [items1..1048576] [repetitions1..64] [particles|triangles|all]]");
        options.physical=true;options.sustained=mode=="sustained";options.items=options.sustained?262144u:65536u;options.repeats=options.sustained?64u:32u;options.rounds=9;options.selector="all";}
    require(argc<=6,"Too many benchmark arguments.");if(argc>2)options.selector=argv[2];if(argc>3)options.items=argument(argv[3],1048576,"items");if(argc>4)options.repeats=argument(argv[4],64,"repetitions");if(argc>5)options.workloads=argv[5];
    require(options.workloads=="particles"||options.workloads=="triangles"||options.workloads=="all","Unknown benchmark workload.");const bool all=options.selector=="all";std::string adapter;bool executed=false;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(!all&&api!=smo_compute_api(options.selector))continue;SmoComputeOptions device;device.software=!options.physical;device.adapter_name=adapter;
        {auto gpu=create_smo_compute(api,device);if(options.physical){require(!gpu->info().software,"A hardware benchmark selected software.");if(adapter.empty())adapter=gpu->info().adapter;device.adapter_name=adapter;}}
        for(const auto kind:{Kind::particles,Kind::triangles})if(options.workloads=="all"||(kind==Kind::particles?options.workloads=="particles":options.workloads=="triangles"))run(options,api,device,kind);executed=true;}
    require(executed,"No requested native performance API was executed.");std::cout<<"PASS: "<<checks<<" native source/manual correctness and benchmark-contract checks; no fixed speed assertion.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native source/manual performance: "<<error.what()<<'\n';return 1;}}

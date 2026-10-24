#include "presentation/windows/smo_native_gpu.hpp"
#include "presentation/windows/smo_native_render.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "native_build_config.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::smo;
using namespace lgaed::presentation;
constexpr auto real_profile=NativeScalarProfile::ordinary_binary32;
constexpr float original_tolerance=0.0002f;
std::size_t checks=0,raw_reference_differences=0;
float maximum_reference_error=0;
void check(bool condition,std::string_view reason){++checks;if(!condition)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,reason);}
std::uint32_t bits(float value){return std::bit_cast<std::uint32_t>(value);}
float real(std::uint32_t word){return std::bit_cast<float>(word);}
std::string sample(std::string_view name){std::ifstream input(std::filesystem::path(lgaed::build::source_root)/"native"/name,std::ios::binary);
    check(input.is_open(),"The actual authored source sample is missing.");std::ostringstream text;text<<input.rdbuf();return text.str();}
void replace_once(std::string& text,std::string_view before,std::string_view after){const auto at=text.find(before);check(at!=text.npos,"An authored source edit could not locate its instruction.");text.replace(at,before.size(),after);}
void success(const SmoNativeGpuExecution& execution,std::uint32_t items){std::ostringstream message;message<<"Real GPU transaction failed: action="<<static_cast<std::uint32_t>(execution.action)
    <<" revision="<<execution.revision<<" expected_items="<<items<<" diagnostics="<<execution.diagnostics.size();
    const auto bad=std::ranges::find_if(execution.diagnostics,[](const auto& diagnostic){return diagnostic.code!=NativeError::none;});
    if(bad!=execution.diagnostics.end())message<<" item="<<(bad-execution.diagnostics.begin())<<" code="<<static_cast<std::uint32_t>(bad->code)<<" line="<<bad->line<<" column="<<bad->column<<" instruction="<<bad->instruction;
    check(execution.committed&&execution.diagnostics.size()==items&&bad==execution.diagnostics.end(),message.str());}
void exact_frames(const std::vector<NativeRealFrame32>& actual,const std::vector<NativeRealFrame32>& expected,std::string_view reason){check(actual.size()==expected.size(),"A real GPU batch changed its item count.");
    for(std::size_t item=0;item<actual.size();++item)check(actual[item]==expected[item],reason);}
void literal_color(const NativeRealFrame32& frame,const std::array<std::uint32_t,3>& expected,std::string_view context,std::size_t item=0){++checks;if(frame.color==expected)return;
    std::ostringstream message;message<<context<<" item="<<item<<" literal color mismatch actual=(0x"<<std::hex<<frame.color[0]<<",0x"<<frame.color[1]<<",0x"<<frame.color[2]
        <<") expected=(0x"<<expected[0]<<",0x"<<expected[1]<<",0x"<<expected[2]<<')';throw std::runtime_error(message.str());}
void literal_colors(const std::vector<NativeRealFrame32>& frames,const std::array<std::uint32_t,3>& expected,std::string_view context){for(std::size_t item=0;item<frames.size();++item)literal_color(frames[item],expected,context,item);}
void approximate_word(std::uint32_t actual,std::uint32_t expected,std::string_view reason){const float a=real(actual),e=real(expected);
    std::ostringstream detail;detail<<reason<<" actual_raw=0x"<<std::hex<<actual<<" expected_raw=0x"<<expected<<std::dec<<" actual="<<std::setprecision(10)<<a<<" expected="<<e;
    check(std::isfinite(a)&&std::isfinite(e),detail.str()+"; NaN/Inf requires a visible diagnostic.");
    const auto error=std::abs(a-e);maximum_reference_error=std::max(maximum_reference_error,error);
    if(actual!=expected)++raw_reference_differences;
    detail<<" abs_error="<<error<<" allowed="<<original_tolerance*std::max(1.0f,std::abs(e));check(error<=original_tolerance*std::max(1.0f,std::abs(e)),detail.str());}
void approximate_frame(const NativeRealFrame32& actual,const NativeRealFrame32& expected,std::string_view context){check(actual.states.size()==expected.states.size()&&actual.vertices.size()==expected.vertices.size()&&actual.faces==expected.faces&&actual.draw_requested==expected.draw_requested,
        std::string(context)+" categorical geometry/state metadata differs; it cannot use a numeric tolerance.");
    for(std::size_t i=0;i<actual.states.size();++i)approximate_word(actual.states[i],expected.states[i],std::string(context)+" state["+std::to_string(i)+"] exceeds the existing2e-4 reference bound.");
    for(std::size_t i=0;i<actual.vertices.size();++i)for(unsigned axis=0;axis<3;++axis)approximate_word(actual.vertices[i][axis],expected.vertices[i][axis],std::string(context)+" vertex["+std::to_string(i)+"]["+std::to_string(axis)+"] exceeds the existing2e-4 reference bound.");
    for(unsigned axis=0;axis<3;++axis){approximate_word(actual.rotation[axis],expected.rotation[axis],std::string(context)+" rotation["+std::to_string(axis)+"] exceeds the existing2e-4 bound.");
        approximate_word(actual.color[axis],expected.color[axis],std::string(context)+" color["+std::to_string(axis)+"] exceeds the existing2e-4 bound.");}}

// A handwritten SSA control, separate from parsing/lowering SMO source.
// Sharing the final native emitter does not make this a second API backend.
struct Builder {
    NativeGraph graph;std::vector<std::uint32_t> current;
    explicit Builder(std::size_t states):current(states,native_no_value){}
    std::uint32_t value(NativeOpcode opcode,std::array<std::uint32_t,3> args={native_no_value,native_no_value,native_no_value},std::uint32_t immediate=0){
        NativeInstruction instruction;instruction.opcode=opcode;instruction.arguments=args;instruction.immediate=immediate;instruction.result=graph.value_count++;graph.instructions.push_back(instruction);return instruction.result;}
    std::uint32_t constant(std::uint32_t raw){return value(NativeOpcode::constant,{native_no_value,native_no_value,native_no_value},raw);}
    std::uint32_t load(std::uint32_t slot){if(current.at(slot)==native_no_value)current[slot]=value(NativeOpcode::load_state,{native_no_value,native_no_value,native_no_value},slot);return current[slot];}
    std::uint32_t binary(NativeOpcode opcode,std::uint32_t a,std::uint32_t b){return value(opcode,{a,b,native_no_value});}
    void terminal(NativeOpcode opcode,std::array<std::uint32_t,3> args={native_no_value,native_no_value,native_no_value},std::uint32_t immediate=0){
        NativeInstruction instruction;instruction.opcode=opcode;instruction.arguments=args;instruction.immediate=immediate;graph.instructions.push_back(instruction);}
    void store(std::uint32_t slot,std::uint32_t id){terminal(NativeOpcode::store_state,{id,native_no_value,native_no_value},slot);current.at(slot)=id;}
};
void mesh(Builder& builder,bool cube){const std::vector<std::array<float,3>> vertices=cube?std::vector<std::array<float,3>>{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}:
    std::vector<std::array<float,3>>{{-1,-1,0},{1,-1,0},{0,1,0}};
    const std::vector<std::array<std::uint32_t,3>> faces=cube?std::vector<std::array<std::uint32_t,3>>{{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}}:
        std::vector<std::array<std::uint32_t,3>>{{0,1,2}};
    for(std::uint32_t vertex=0;vertex<vertices.size();++vertex)builder.terminal(NativeOpcode::vertex,{builder.constant(bits(vertices[vertex][0])),builder.constant(bits(vertices[vertex][1])),builder.constant(bits(vertices[vertex][2]))},vertex);
    for(std::uint32_t face=0;face<faces.size();++face)builder.terminal(NativeOpcode::face,{builder.constant(bits(static_cast<float>(faces[face][0]))),builder.constant(bits(static_cast<float>(faces[face][1]))),builder.constant(bits(static_cast<float>(faces[face][2])))},face);}
void sample_draw(Builder& builder,bool cube){const auto angle=builder.load(0),zero=builder.constant(0);
    if(cube)builder.terminal(NativeOpcode::rotation,{builder.binary(NativeOpcode::divide,angle,builder.constant(0x40000000u)),angle,zero});
    else builder.terminal(NativeOpcode::rotation,{zero,zero,angle});
    builder.terminal(NativeOpcode::color,cube?std::array{builder.constant(0x3e2aaaabu),builder.constant(0x3f2aaaabu),builder.constant(0x3f800000u)}:
        std::array{builder.constant(0x3f800000u),builder.constant(0x3f000000u),builder.constant(0x3e2aaaabu)});builder.terminal(NativeOpcode::draw);}
NativeProgram manual_sample(bool cube){NativeProgram program;program.scalarProfile=real_profile;program.ir.states={"giro","velocidad"};program.ir.vertices=cube?8u:3u;program.ir.faces=cube?12u:1u;
    program.identity=cube?"manual-native-binary32-cube-v1":"manual-native-binary32-triangle-v1";
    Builder reset(2);mesh(reset,cube);reset.store(0,reset.constant(0));reset.store(1,reset.constant(cube?0x3f000000u:0x3f800000u));sample_draw(reset,cube);program.actions[0]=std::move(reset.graph);
    Builder draw(2);sample_draw(draw,cube);program.actions[1]=std::move(draw.graph);
    Builder step(2);const auto move=step.binary(NativeOpcode::multiply,step.load(1),step.value(NativeOpcode::load_dt));step.store(0,step.binary(NativeOpcode::add,step.load(0),move));sample_draw(step,cube);program.actions[2]=std::move(step.graph);return program;}

std::string batch_source(){return
    "vertice(-1,-1,0);vertice(1,-1,0);vertice(0,1,0);cara(0,1,2);"
    "estado posicion=10.6;estado velocidad=.6;estado desplazamiento=.3;estado y=0;estado doble=0;estado cuadrado=0;estado pasos=0;"
    "al_actualizar{posicion=posicion+velocidad*dt;y=posicion-desplazamiento;doble=y*2;cuadrado=pdir(dt,2);pasos=pasos+1;}"
    "al_dibujar{rotacion(0,0,posicion);color(.2,.8,1);dibujar();}";}
void batch_draw(Builder& builder){const auto zero=builder.constant(0);builder.terminal(NativeOpcode::rotation,{zero,zero,builder.load(0)});
    builder.terminal(NativeOpcode::color,{builder.constant(0x3e2aaaabu),builder.constant(0x3f2aaaabu),builder.constant(0x3f800000u)});builder.terminal(NativeOpcode::draw);}
NativeProgram manual_batch(){NativeProgram program;program.scalarProfile=real_profile;program.ir.states={"posicion","velocidad","desplazamiento","y","doble","cuadrado","pasos"};program.ir.vertices=3;program.ir.faces=1;
    program.identity="manual-native-binary32-independent-batch-v1";Builder reset(7);mesh(reset,false);
    for(std::uint32_t slot=0;slot<7;++slot){constexpr std::array<std::uint32_t,7> values{0x41480000u,0x3f000000u,0x3e800000u,0,0,0,0};reset.store(slot,reset.constant(values[slot]));}
    batch_draw(reset);program.actions[0]=std::move(reset.graph);Builder draw(7);batch_draw(draw);program.actions[1]=std::move(draw.graph);
    Builder step(7);const auto dt=step.value(NativeOpcode::load_dt);step.store(0,step.binary(NativeOpcode::add,step.load(0),step.binary(NativeOpcode::multiply,step.load(1),dt)));
    step.store(3,step.binary(NativeOpcode::subtract,step.load(0),step.load(2)));step.store(4,step.binary(NativeOpcode::multiply,step.load(3),step.constant(0x40000000u)));
    step.store(5,step.binary(NativeOpcode::power,dt,step.constant(0x40000000u)));step.store(6,step.binary(NativeOpcode::add,step.load(6),step.constant(0x3f800000u)));
    batch_draw(step);program.actions[2]=std::move(step.graph);return program;}

void artifact_contract(){const auto program=native_compile(batch_source(),{},real_profile);for(const auto action:{NativeAction::reset,NativeAction::frame,NativeAction::step}){const auto shader=smo_native_kernel(program,action);
    check(!shader.hlsl.empty()&&!shader.glsl.empty()&&shader.spirv.size()>=5&&shader.spirv[0]==0x07230203u,"The real native compiler omitted a genuine API artifact.");bool float32=false;
    for(std::size_t at=5;at<shader.spirv.size();){const auto word=shader.spirv[at],count=word>>16,opcode=word&0xffffu;check(count>0&&count<=shader.spirv.size()-at,"Malformed native binary32 SPIR-V instruction.");
        if(opcode==22u){check(count==3&&shader.spirv[at+2]==32,"The native real shader inserted FP64 or another undeclared floating precision.");float32=true;}at+=count;}
    check(float32&&shader.hlsl.find("smo_f12_")==shader.hlsl.npos&&shader.glsl.find("smo_d12_")==shader.glsl.npos,
        "The explicitly native real profile invoked software duodecimal arithmetic or omitted hardware binary32.");}}

// Independent test-only FP32 ray reference. No face cache and no production
// renderer fallback. Every scalar result is rounded before the next operation;
// the stable fixture has zero rotation and no pixel centers on triangle edges.
float fadd(float a,float b){volatile float result=a+b;return result;}
float fsub(float a,float b){volatile float result=a-b;return result;}
float fmul(float a,float b){volatile float result=a*b;return result;}
float fdiv(float a,float b){volatile float result=a/b;return result;}
struct Vector {float x=0,y=0,z=0;};
Vector sub(Vector a,Vector b){return {fsub(a.x,b.x),fsub(a.y,b.y),fsub(a.z,b.z)};}
Vector cross(Vector a,Vector b){return {fsub(fmul(a.y,b.z),fmul(a.z,b.y)),fsub(fmul(a.z,b.x),fmul(a.x,b.z)),fsub(fmul(a.x,b.y),fmul(a.y,b.x))};}
float dot(Vector a,Vector b){return fadd(fadd(fmul(a.x,b.x),fmul(a.y,b.y)),fmul(a.z,b.z));}
std::uint32_t channel(float value){const float scaled=fmul(value,255.0f);auto whole=static_cast<std::uint32_t>(scaled);const auto remainder=fsub(scaled,static_cast<float>(whole));
    if(remainder>0.5f||(remainder==0.5f&&(whole&1u)))++whole;return whole;}
std::vector<std::array<std::uint32_t,3>> geometry_reference(const NativeRealFrame32& frame){
    const float sx=std::sin(real(frame.rotation[0])),cx=std::cos(real(frame.rotation[0])),sy=std::sin(real(frame.rotation[1])),cy=std::cos(real(frame.rotation[1])),
        sz=std::sin(real(frame.rotation[2])),cz=std::cos(real(frame.rotation[2]));std::vector<std::array<std::uint32_t,3>> result;
    for(const auto& raw:frame.vertices){Vector p{real(raw[0]),real(raw[1]),real(raw[2])};float y=fsub(fmul(p.y,cx),fmul(p.z,sx)),z=fadd(fmul(p.y,sx),fmul(p.z,cx));p.y=y;p.z=z;
        float x=fadd(fmul(p.x,cy),fmul(p.z,sy));z=fsub(fmul(p.z,cy),fmul(p.x,sy));p.x=x;p.z=z;
        x=fsub(fmul(p.x,cz),fmul(p.y,sz));y=fadd(fmul(p.x,sz),fmul(p.y,cz));p.x=x;p.y=y;p.z=fadd(p.z,4.0f);result.push_back({bits(p.x),bits(p.y),bits(p.z)});}
    return result;}
SmoNativeRenderGeometry checked_geometry(SmoNativeRenderer& renderer,const NativeRealFrame32& frame){const auto actual=renderer.inspect_geometry();const auto expected=geometry_reference(frame);
    check(actual.vertices.size()==expected.size()&&actual.diagnostics.size()==expected.size()&&std::ranges::all_of(actual.diagnostics,[](std::uint32_t diagnostic){return diagnostic==0;}),
        "The explicit QA geometry readback omitted vertices or hid a preview diagnostic.");
    for(std::size_t vertex=0;vertex<expected.size();++vertex)for(unsigned axis=0;axis<3;++axis)approximate_word(actual.vertices[vertex][axis],expected[vertex][axis],
        "GPU trigonometry/rotation exceeds the unchanged CPU FP32 reference bound.");return actual;}
std::vector<std::uint32_t> stable_reference(const NativeRealFrame32& frame,std::uint32_t width,std::uint32_t height){
    for(const auto word:frame.rotation)check((word&0x7fffffffu)==0,"The stable raster oracle was accidentally used for a rotated scene.");
    std::vector<Vector> vertices;for(const auto& raw:frame.vertices)vertices.push_back({real(raw[0]),real(raw[1]),fadd(real(raw[2]),4.0f)});
    const auto color=0xff000000u|(channel(real(frame.color[0]))<<16)|(channel(real(frame.color[1]))<<8)|channel(real(frame.color[2]));
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width)*height,0xff101820u);if(!frame.draw_requested)return pixels;
    for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x){
        const float cx=fdiv(static_cast<float>(width),2.0f),cy=fdiv(static_cast<float>(height),2.0f),focal=fmul(static_cast<float>(std::min(width,height)),1.5f);
        const Vector direction{fdiv(fsub(fadd(static_cast<float>(x),0.5f),cx),focal),fdiv(fsub(cy,fadd(static_cast<float>(y),0.5f)),focal),1.0f};
        bool found=false;float nearest=0;
        for(const auto& face:frame.faces){const auto a=vertices.at(face[0]),b=vertices.at(face[1]),c=vertices.at(face[2]);const auto e1=sub(b,a),e2=sub(c,a),p=cross(direction,e2);const float determinant=dot(e1,p);
            if(determinant==0)continue;const float inverse=fdiv(1.0f,determinant);const Vector t{-a.x,-a.y,-a.z};const float u=fmul(dot(t,p),inverse);
            if(u<0||u>1)continue;const Vector q=cross(t,e1);const float v=fmul(dot(direction,q),inverse);if(v<0||fadd(u,v)>1)continue;
            const float depth=fmul(dot(e2,q),inverse);if(depth<real(0x3daaaaabu)||depth>64.0f)continue;if(!found||depth<nearest){found=true;nearest=depth;}}
        if(found)pixels[static_cast<std::size_t>(y)*width+x]=color;}
    return pixels;}
void stable_raster(SmoNativeGpuOptions options){options.items=1;const auto width=options.device.software?64u:128u,height=options.device.software?36u:72u;
    auto program=SmoNativeGpuProgram::compile("vertice(-.3,-.3,0);vertice(.3,-.3,0);vertice(0,.46,0);cara(0,1,2);al_actualizar{}al_dibujar{color(1,.6,.3);dibujar();}",options);
    NativeRealFrame32 manual;manual.vertices={{{0xbe800000u,0xbe800000u,0}},{{0x3e800000u,0xbe800000u,0}},{{0,0x3ec00000u,0}}};manual.faces={{{0,1,2}}};manual.color={0x3f800000u,0x3f000000u,0x3e800000u};manual.draw_requested=true;
    check(program.inspect_binary32().items.at(0)==manual,"The stable authored raster fixture differs from manually specified IEEE words.");SmoNativeRenderer renderer(program.compute(),width,height);
    const auto image=renderer.render(program.frame_view());const auto expected=stable_reference(manual,width,height);check(image.diagnostic==0&&image.pixels.size()==expected.size(),"The stable native raster fixture failed or omitted pixels.");
    std::size_t differences=0,first=0;for(std::size_t pixel=0;pixel<expected.size();++pixel)if(image.pixels[pixel]!=expected[pixel]){if(!differences)first=pixel;++differences;}
    std::ostringstream reason;reason<<"Native FP32 cached raster differs from independent uncached CPU FP32: pixels="<<differences;
    if(differences)reason<<" first="<<first<<" expected_BGRA=0x"<<std::hex<<expected[first]<<" actual_BGRA=0x"<<image.pixels[first];check(differences==0,reason.str());
    check(image.colored_pixels>0&&std::ranges::find(image.pixels,0xffff8040u)!=image.pixels.end(),"Native FP32 color conversion lost exact ties-even BGRA output.");
    renderer.resize(width/2,height/2);const auto reduced_image=renderer.render(program.frame_view());check(reduced_image.width==width/2&&reduced_image.height==height/2&&reduced_image.pixels==stable_reference(manual,width/2,height/2),"Resizing changed the native camera/raster contract.");
    renderer.resize(width,height);check(renderer.render(program.frame_view()).pixels==image.pixels,"Restoring renderer size changed the original native image.");}

std::vector<NativeRealFrame32> independent_batch(SmoNativeGpuOptions options){options.items=options.device.software?129u:65536u;
    auto program=SmoNativeGpuProgram::compile(batch_source(),options);auto control=SmoNativeGpuProgram::from_program(manual_batch(),options);success(program.reset(false),options.items);success(control.reset(false),options.items);
    const auto initial=program.inspect_binary32().items;const auto initial_control=control.inspect_binary32().items;exact_frames(initial,initial_control,"Source and independent manual real GPU control differ before runtime inputs.");
    constexpr std::array<std::uint32_t,3> color_bits{0x3e2aaaabu,0x3f2aaaabu,0x3f800000u};literal_colors(initial,color_bits,"source batch reset");literal_colors(initial_control,color_bits,"manual batch reset");
    std::uint32_t random=0x12f320b1u;const auto next=[&]{random=random*1664525u+1013904223u;return random;};std::vector<std::uint32_t> inputs(static_cast<std::size_t>(options.items)*7);
    for(std::uint32_t item=0;item<options.items;++item){inputs[item*7]=bits(static_cast<float>(static_cast<int>(next()%257u)-128)/8.0f);inputs[item*7+1]=bits(static_cast<float>(next()%9u)/8.0f);
        inputs[item*7+2]=bits(static_cast<float>(static_cast<int>(next()%33u)-16)/8.0f);for(unsigned slot=3;slot<7;++slot)inputs[item*7+slot]=0;}
    program.seed_binary32_states(inputs);control.seed_binary32_states(inputs);success(program.frame(false),options.items);success(control.frame(false),options.items);
    auto expected=program.inspect_binary32().items;const auto seeded_control=control.inspect_binary32().items;literal_colors(expected,color_bits,"source batch frame");literal_colors(seeded_control,color_bits,"manual batch frame");
    exact_frames(expected,seeded_control,"The manual/source controls consumed different raw IEEE inputs.");
    for(unsigned frame=0;frame<4;++frame){std::vector<std::uint32_t> dt(options.items);
        for(auto& word:dt)word=bits(static_cast<float>(next()%17u)/16.0f);dt[frame]=0;
        for(std::uint32_t item=0;item<options.items;++item){auto& state=expected[item].states;const float time=real(dt[item]);volatile float move=real(state[1])*time;
            const float position=real(state[0])+move,y=position-real(state[2]),twice=y*2.0f,square=time*time,ticks=real(state[6])+1.0f;
            state[0]=bits(position);state[3]=bits(y);state[4]=bits(twice);state[5]=bits(square);state[6]=bits(ticks);expected[item].rotation={0,0,bits(position)};}
        success(program.step_binary32(dt),options.items);success(control.step_binary32(dt),options.items);
        const auto actual=program.inspect_binary32().items;const auto manual=control.inspect_binary32().items;
        literal_colors(actual,color_bits,"source batch step");literal_colors(manual,color_bits,"manual batch step");
        exact_frames(actual,manual,"Same-API source/manual native FP32 results differ in exact raw words.");
        exact_frames(actual,expected,"An exactly dyadic native FP32 batch differs from independent nonfused arithmetic.");}
    auto result=program.inspect_binary32().items;success(program.reset(false),options.items);exact_frames(program.inspect_binary32().items,initial,"A real GPU reset retained previously seeded or updated quantities.");
    std::cout<<"PASS independent native FP32 batch api="<<program.info().api<<" adapter="<<std::quoted(program.info().adapter)<<" items="<<options.items<<" frames=4 exact_raw_control=true; not an F12 precision or base12 speed claim.\n";
    return result;}

struct SampleRecord {std::string name;NativeRealFrame32 frame;std::vector<std::uint32_t> pixels;std::vector<std::array<std::uint32_t,3>> transformed;};
void color_diagnostic(std::string_view context,const NativeRealFrame32& frame){std::cout<<"Cube color "<<context<<" raw=(0x"<<std::hex<<frame.color[0]<<",0x"<<frame.color[1]<<",0x"<<frame.color[2]<<std::dec
    <<") values=("<<real(frame.color[0])<<','<<real(frame.color[1])<<','<<real(frame.color[2])<<")\n";}
void cube_snapshot(SmoNativeGpuProgram& program,std::string_view context){const auto snapshot=program.inspect_binary32();const auto view=program.frame_view();
    std::cout<<"Cube snapshot "<<context<<" api="<<program.info().api<<" revision="<<snapshot.revision<<" frame_word_offset="<<view.frame_word_offset
        <<" bank="<<view.frame_word_offset/(program.item_count()*native_frame_words)<<" profile="<<static_cast<std::uint32_t>(program.program().scalarProfile)<<'\n';color_diagnostic(context,snapshot.items.at(0));}
void cube_shader_diagnostic(const NativeProgram& program){const auto shader=smo_native_kernel(program,NativeAction::reset);
    std::cout<<"BEGIN diagnostic-only cube reset HLSL "<<shader.name<<'\n'<<shader.hlsl<<"\nEND cube reset HLSL\n";
    for(const auto* model:{"cs_5_0","cs_5_1"}){Microsoft::WRL::ComPtr<ID3DBlob> code,errors,assembly;
        const auto result=D3DCompile(shader.hlsl.data(),shader.hlsl.size(),shader.name.c_str(),nullptr,nullptr,"CSMain",model,
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf());
        std::cout<<"BEGIN diagnostic-only cube DXBC "<<model<<" compile_HRESULT=0x"<<std::hex<<static_cast<unsigned long>(result)<<std::dec<<'\n';
        if(errors)std::cout.write(static_cast<const char*>(errors->GetBufferPointer()),static_cast<std::streamsize>(errors->GetBufferSize()));
        if(SUCCEEDED(result)){const auto disassembled=D3DDisassemble(code->GetBufferPointer(),code->GetBufferSize(),0,nullptr,assembly.GetAddressOf());
            if(SUCCEEDED(disassembled)&&assembly)std::cout.write(static_cast<const char*>(assembly->GetBufferPointer()),static_cast<std::streamsize>(assembly->GetBufferSize()));
            else std::cout<<"Disassembly HRESULT=0x"<<std::hex<<static_cast<unsigned long>(disassembled)<<std::dec<<'\n';}
        std::cout<<"\nEND diagnostic-only cube DXBC "<<model<<'\n';}}
std::vector<SampleRecord> samples_and_render(SmoNativeGpuOptions options){options.items=1;std::vector<SampleRecord> result;const auto width=options.device.software?64u:128u,height=options.device.software?36u:72u;
    for(const bool cube:{false,true}){auto program=SmoNativeGpuProgram::compile(sample(cube?"smo_cube.smo":"smo_triangle.smo"),options);auto control=SmoNativeGpuProgram::from_program(manual_sample(cube),options);
        SmoNativeRenderer renderer(program.compute(),width,height),manual_renderer(control.compute(),width,height);
        for(const auto time:{0u,0x3e800000u,0x3f000000u}){if(time){const std::array<std::uint32_t,1> dt{time};success(program.step_binary32(dt,false),1);success(control.step_binary32(dt,false),1);}else{
                success(program.reset(false),1);success(control.reset(false),1);}
            const auto actual=program.inspect_binary32().items.at(0),manual=control.inspect_binary32().items.at(0);check(actual==manual,"The authored triangle/cube differs from its independently handwritten real GPU control.");
            const auto initial=native_reference_real(program.program(),NativeAction::reset);auto expected=initial.frame;
            if(time==0x3e800000u)expected=native_reference_real(program.program(),NativeAction::step,expected,0x3e800000u).frame;
            else if(time==0x3f000000u){expected=native_reference_real(program.program(),NativeAction::step,expected,0x3e800000u).frame;expected=native_reference_real(program.program(),NativeAction::step,expected,0x3f000000u).frame;}
            if(cube&&time==0&&actual.color!=expected.color){color_diagnostic("CPU reset expected",expected);cube_snapshot(program,"source before renderer on failure");cube_snapshot(control,"manual before renderer on failure");
                const auto diagnostic_image=renderer.render(program.frame_view());std::cout<<"Cube diagnostic render code="<<diagnostic_image.diagnostic<<" colored_pixels="<<diagnostic_image.colored_pixels<<'\n';
                cube_snapshot(program,"source after renderer on failing CPU color");cube_shader_diagnostic(program.program());}
            const auto color_bits=cube?std::array<std::uint32_t,3>{0x3e2aaaabu,0x3f2aaaabu,0x3f800000u}:std::array<std::uint32_t,3>{0x3f800000u,0x3f000000u,0x3e2aaaabu};
            literal_color(actual,color_bits,program.info().api+std::string(cube?" source cube":" source triangle"));literal_color(manual,color_bits,program.info().api+std::string(cube?" manual cube":" manual triangle"));
            approximate_frame(actual,expected,program.info().api+std::string(" CPU ")+(cube?"cube":"triangle")+" timebits="+std::to_string(time));const auto image=renderer.render(program.frame_view()),manual_image=manual_renderer.render(control.frame_view());
            check(image.diagnostic==0&&manual_image.diagnostic==0&&image.width==width&&image.height==height&&image.pixels.size()==static_cast<std::size_t>(width)*height&&image.colored_pixels>0,
                "The native real renderer produced a blank/error image or omitted pixels.");
            check(image.pixels==manual_image.pixels&&image.pixel_hash==manual_image.pixel_hash,"Same-API source/manual real rendering differs; pixels cannot use a floating tolerance.");
            const auto geometry=checked_geometry(renderer,actual),manual_geometry=manual_renderer.inspect_geometry();check(geometry.vertices==manual_geometry.vertices&&geometry.diagnostics==manual_geometry.diagnostics,
                "Same-API source/manual transformed geometry differs in raw FP32 words.");
            result.push_back({std::string(cube?"cube":"triangle")+" timebits="+std::to_string(time),actual,image.pixels,geometry.vertices});}
        const auto before=program.inspect_binary32();success(program.frame(false),1);exact_frames(program.inspect_binary32().items,before.items,"Drawing again advanced the original real program.");}
    auto edited=sample("smo_triangle.smo");replace_once(edited,"estado velocidad = 1;","estado velocidad = 2;");replace_once(edited,"vertice(0, 1, 0);","vertice(0, 2, 0);");
    replace_once(edited,"color(1, .6, .2);","color(.2, .8, 1);");auto changed=SmoNativeGpuProgram::compile(edited,options);const std::array<std::uint32_t,1> quarter{0x3e800000u};
    success(changed.step_binary32(quarter,false),1);const auto edited_frame=changed.inspect_binary32().items.at(0);
    check(edited_frame.rotation[2]==0x3f000000u&&edited_frame.vertices[2][1]==0x40000000u&&edited_frame.color==std::array<std::uint32_t,3>{0x3e2aaaabu,0x3f2aaaabu,0x3f800000u},
        "Editing real source state, vertices and fractional color did not affect native GPU results.");SmoNativeRenderer changed_renderer(changed.compute(),width,height);const auto edited_image=changed_renderer.render(changed.frame_view());
    check(edited_image.diagnostic==0&&edited_image.colored_pixels>0&&edited_image.pixels!=result.front().pixels,"Edited real source failed to change rendered pixels.");
    result.push_back({"edited triangle",edited_frame,edited_image.pixels,checked_geometry(changed_renderer,edited_frame).vertices});
    auto hidden_source=sample("smo_triangle.smo");replace_once(hidden_source,"dibujar();","");auto hidden=SmoNativeGpuProgram::compile(hidden_source,options);SmoNativeRenderer hidden_renderer(hidden.compute(),width,height);
    const auto hidden_image=hidden_renderer.render(hidden.frame_view());check(hidden_image.diagnostic==0&&hidden_image.colored_pixels==0&&hidden_image.pixels.size()==static_cast<std::size_t>(width)*height&&
        std::ranges::all_of(hidden_image.pixels,[&](std::uint32_t pixel){return pixel==hidden_image.pixels.front();}),"A source program without dibujar still rendered geometry.");
    auto failed_source=sample("smo_triangle.smo");replace_once(failed_source,"estado velocidad = 1;","estado velocidad = 1; estado canal=.6;");
    replace_once(failed_source,"giro = giro + velocidad * dt;","giro = giro + velocidad * dt; canal=canal+1;");replace_once(failed_source,"color(1, .6, .2);","color(canal,0,0);");
    auto failed=SmoNativeGpuProgram::compile(failed_source,options);SmoNativeRenderer failed_renderer(failed.compute(),width,height);const auto old_frame=failed.inspect_binary32();const auto old_image=failed_renderer.render(failed.frame_view());
    const std::array<std::uint32_t,1> zero{};const auto rejected=failed.step_binary32(zero,false);check(!rejected.committed&&rejected.diagnostics[0].code==NativeError::color&&rejected.revision==old_frame.revision,
        "An invalid authored draw color partially committed its preceding real update.");exact_frames(failed.inspect_binary32().items,old_frame.items,"Draw failure changed the committed original real state.");
    check(failed_renderer.render(failed.frame_view()).pixels==old_image.pixels,"A failed real draw transaction changed the visible previous image.");
    return result;}

void dynamic_rules(SmoNativeGpuOptions options){
    struct Case {std::uint32_t a,b,exponent,product,quotient,power;};
    const std::array<Case,6> cases{{{0x40000000u,0x3f000000u,0xbf800000u,0x3f800000u,0x40800000u,0x3f000000u},
        {0xc0000000u,0xbf000000u,0xc0400000u,0x3f800000u,0x40800000u,0xbe000000u},
        {0xbf000000u,0x3e800000u,0,0xbe000000u,0xc0000000u,0x3f800000u},
        {0x80000000u,0x40000000u,0,0x80000000u,0x80000000u,0x3f800000u},
        {0,0x40000000u,0x40000000u,0,0,0},
        {0x00800000u,0x3f800000u,0xbf800000u,0x00800000u,0x00800000u,0x7e800000u}}};
    options.items=static_cast<std::uint32_t>(cases.size());auto program=SmoNativeGpuProgram::compile(
        "estado a=2;estado b=1;estado e=0;estado producto=0;estado cociente=0;estado potencia=0;"
        "al_actualizar{producto=a*b;cociente=a/b;potencia=pdir(a,e);}al_dibujar{}",options);
    std::vector<std::uint32_t> states(cases.size()*6),dt(cases.size(),0);
    for(std::size_t i=0;i<cases.size();++i){states[i*6]=cases[i].a;states[i*6+1]=cases[i].b;states[i*6+2]=cases[i].exponent;}
    program.seed_binary32_states(states);success(program.step_binary32(dt,false),options.items);const auto snapshot=program.inspect_binary32();
    for(std::size_t i=0;i<cases.size();++i)check(snapshot.items[i].states==std::vector<std::uint32_t>{cases[i].a,cases[i].b,cases[i].exponent,cases[i].product,cases[i].quotient,cases[i].power},
        "Native binary32 runtime changed ordinary signs, signed zero, exponent0/negative or reciprocal-first power semantics.");
}

void repeated_draw(Builder& builder){builder.terminal(NativeOpcode::color,{builder.constant(0x3e2aaaabu),builder.constant(0x3f2aaaabu),builder.constant(0x3f800000u)});
    builder.terminal(NativeOpcode::rotation,{builder.constant(0x3f800000u),builder.constant(0x40000000u),builder.constant(0x40400000u)});
    builder.terminal(NativeOpcode::color,{builder.load(0),builder.constant(0),builder.constant(0)});
    builder.terminal(NativeOpcode::color,{builder.constant(0x3f000000u),builder.constant(0x3e800000u),builder.constant(0x3f400000u)});
    builder.terminal(NativeOpcode::rotation,{builder.constant(0x40800000u),builder.constant(0x40a00000u),builder.constant(0x40c00000u)});builder.terminal(NativeOpcode::draw);}
NativeProgram repeated_manual(){NativeProgram program;program.scalarProfile=real_profile;program.ir.states={"canal"};program.ir.vertices=3;program.ir.faces=1;program.identity="manual-native-repeated-frame-setters-v1";
    Builder reset(1);mesh(reset,false);reset.store(0,reset.constant(0x3f000000u));repeated_draw(reset);program.actions[0]=std::move(reset.graph);
    Builder draw(1);repeated_draw(draw);program.actions[1]=std::move(draw.graph);Builder step(1);step.store(0,step.binary(NativeOpcode::add,step.load(0),step.constant(0x3f800000u)));
    repeated_draw(step);program.actions[2]=std::move(step.graph);return program;}
void repeated_frame_setters(SmoNativeGpuOptions options){options.items=17;const std::string source=
    "vertice(-1,-1,0);vertice(1,-1,0);vertice(0,1,0);cara(0,1,2);\n"
    "estado canal=.6;\nal_actualizar {canal=canal+1;}\nal_dibujar {\n color(.2,.8,1);\n rotacion(1,2,3);\n color(canal,0,0);\n color(.6,.3,.9);\n rotacion(4,5,6);\n dibujar();\n}\n";
    auto program=SmoNativeGpuProgram::compile(source,options);auto control=SmoNativeGpuProgram::from_program(repeated_manual(),options);
    constexpr std::array<std::uint32_t,3> final_color{0x3f000000u,0x3e800000u,0x3f400000u},final_rotation{0x40800000u,0x40a00000u,0x40c00000u};
    for(const bool reset:{true,false}){if(reset){success(program.reset(false),options.items);success(control.reset(false),options.items);}else{success(program.frame(false),options.items);success(control.frame(false),options.items);}
        const auto actual=program.inspect_binary32(),manual=control.inspect_binary32();literal_colors(actual.items,final_color,"source repeated setters");literal_colors(manual.items,final_color,"manual repeated setters");
        exact_frames(actual.items,manual.items,"Source/manual repeated setter output differs.");for(const auto& item:actual.items)check(item.rotation==final_rotation&&item.draw_requested,"Repeated rotations did not preserve the last authored payload.");}
    const auto before=program.inspect_binary32(),manual_before=control.inspect_binary32();const std::vector<std::uint32_t> zero(options.items,0);const auto rejected=program.step_binary32(zero,false),manual_rejected=control.step_binary32(zero,false);
    check(!rejected.committed&&!manual_rejected.committed&&rejected.revision==before.revision&&manual_rejected.revision==manual_before.revision,
        "An overwritten invalid color committed a real source/manual update.");
    for(std::uint32_t item=0;item<options.items;++item){check(rejected.diagnostics[item].code==NativeError::color&&rejected.diagnostics[item].line==7&&rejected.diagnostics[item].column==2&&manual_rejected.diagnostics[item].code==NativeError::color,
        "A later valid color hid the first overwritten-color error or its source coordinate.");}
    exact_frames(program.inspect_binary32().items,before.items,"An overwritten invalid color changed the committed source frame.");exact_frames(control.inspect_binary32().items,manual_before.items,"An overwritten invalid color changed the committed manual frame.");
}

void invalid_inputs(SmoNativeGpuOptions options){options.items=2;auto program=SmoNativeGpuProgram::compile("estado a=1;estado r=0;al_actualizar{r=a+1;}al_dibujar{}",options);
    const std::array<std::uint32_t,2> dt{};
    for(const auto invalid:{0x7fc01234u,0x7f800000u,0xff800000u,0x00000001u,0x807fffffu}){
        const std::array<std::uint32_t,4> states{0x3f800000u,0,invalid,0};program.seed_binary32_states(states);const auto before=program.inspect_binary32();const auto failure=program.step_binary32(dt,false);
        const auto expected=((invalid>>23)&255u)==255u?NativeError::nonfinite:NativeError::underflow;
        check(!failure.committed&&failure.diagnostics[1].code==expected&&failure.diagnostics[1].line>0&&failure.diagnostics[1].column>0,
            "An explicitly injected NaN/Inf/subnormal input bypassed the GPU finite-normal domain check.");
        exact_frames(program.inspect_binary32().items,before.items,"Rejecting an invalid input altered the previous globally committed raw input bank.");}
    program.seed_binary32_states(std::array<std::uint32_t,4>{0x3f800000u,0,0x3f800000u,0});success(program.step_binary32(dt,false),2);
    rejects([&]{(void)program.step(std::array<std::int32_t,2>{0,0},false);},"The real runner silently reinterpreted an integer dt API as float bits.");
    rejects([&]{(void)program.inspect();},"The real runner silently exposed float words as signed integer quantities.");
}

void first_error_order(SmoNativeGpuOptions options){options.items=2;const std::array<std::uint32_t,2> dt{};
    for(const bool divide_first:{false,true}){const std::string expression=divide_first?"(1/b)+(a*2)":"(a*2)+(1/b)";
        const auto source="estado a=1;\nestado b=1;\nestado r=0;\nestado pasos=0;\nal_actualizar {\n pasos=pasos+1;\n r = "+expression+";\n}\nal_dibujar{}";
        auto program=SmoNativeGpuProgram::compile(source,options);const std::array<std::uint32_t,8> states{
            0x3f800000u,0x3f800000u,0,0,0x7f7fffffu,0,0x40800000u,0x40000000u};
        program.seed_binary32_states(states);const auto before=program.inspect_binary32();const auto rejected=program.step_binary32(dt,false);
        check(!rejected.committed&&rejected.revision==before.revision&&rejected.diagnostics.size()==options.items,
            "A binary32 update containing two errors partially committed its tentative state stores.");
        const auto column=6u+static_cast<std::uint32_t>(expression.find(divide_first?'/':'*'));const auto& diagnostic=rejected.diagnostics[1];
        check(diagnostic.code==(divide_first?NativeError::division_zero:NativeError::nonfinite)&&diagnostic.line==7&&diagnostic.column==column,
            "Continuing checked native SSA replaced the first of two different binary32 errors or its exact source operator.");
        check(rejected.diagnostics[0].code==NativeError::none,"Two errors in one binary32 item contaminated a valid item's diagnostic.");
        exact_frames(program.inspect_binary32().items,before.items,"A failed binary32 first-error batch changed any previously committed quantity.");}
}

std::uint32_t rounded_count_reference(std::uint64_t value){if(!value)return 0;unsigned exponent=63u-static_cast<unsigned>(std::countl_zero(value));std::uint64_t significand;
    if(exponent<=23)significand=value<<(23u-exponent);else{const unsigned shift=exponent-23u;significand=value>>shift;
        const auto remainder=value&((std::uint64_t{1}<<shift)-1u),half=std::uint64_t{1}<<(shift-1u);
        if(remainder>half||(remainder==half&&(significand&1u)))++significand;if(significand==0x1000000u){significand>>=1u;++exponent;}}
    return ((exponent+127u)<<23u)|static_cast<std::uint32_t>(significand&0x7fffffu);}
SmoKernelSource count_checkpoint_kernel(SmoKernelSource source){
    // TEST-ONLY copied artifact: expose the production converter's existing
    // numerator through the existing result slot. Production kernels, output
    // records and rendering perform no extra writes or conversions.
    constexpr std::string_view before="nativeDt=ndiv(NF(rawCount),NF(0x4e6e6b28u),diagnostic,0u,0u,0u);",after="nativeDt=NF(rawCount);";
    for(auto* text:{&source.hlsl,&source.glsl}){const auto at=text->find(before);check(at!=text->npos&&text->find(before,at+before.size())==text->npos,"The test checkpoint no longer matches exactly one production clock scaling operation.");text->replace(at,before.size(),after);}
    std::vector<std::uint32_t> denominator_ids;
    for(std::size_t at=5;at<source.spirv.size();){const auto first=source.spirv[at],count=first>>16,opcode=first&0xffffu;check(count&&count<=source.spirv.size()-at,"Malformed production SPIR-V while constructing the clock checkpoint.");
        if(opcode==43u&&count==4u&&source.spirv[at+3]==0x4e6e6b28u)denominator_ids.push_back(source.spirv[at+2]);at+=count;}
    check(!denominator_ids.empty(),"The production SPIR-V clock omitted its exact1e9 binary32 constant.");
    for(std::size_t at=5;at<source.spirv.size();){const auto count=source.spirv[at]>>16,opcode=source.spirv[at]&0xffffu;
        if(opcode==124u&&count==4u&&std::ranges::find(denominator_ids,source.spirv[at+3])!=denominator_ids.end())denominator_ids.push_back(source.spirv[at+2]);at+=count;}
    std::size_t division=0;std::uint32_t result_id=0;unsigned matches=0;
    for(std::size_t at=5;at<source.spirv.size();){const auto count=source.spirv[at]>>16,opcode=source.spirv[at]&0xffffu;
        if(opcode==136u&&count==5u&&std::ranges::find(denominator_ids,source.spirv[at+4])!=denominator_ids.end()){division=at;result_id=source.spirv[at+2];++matches;}at+=count;}
    check(matches==1,"The test checkpoint could not identify exactly one production clock FDiv.");
    std::vector<std::uint32_t> rewritten(source.spirv.begin(),source.spirv.begin()+5);unsigned removed_decoration=0;
    for(std::size_t at=5;at<source.spirv.size();){const auto count=source.spirv[at]>>16,opcode=source.spirv[at]&0xffffu;
        if(at==division)rewritten.insert(rewritten.end(),{(4u<<16u)|83u,source.spirv[at+1],result_id,source.spirv[at+3]});
        else if(opcode==71u&&count==3u&&source.spirv[at+1]==result_id&&source.spirv[at+2]==42u)++removed_decoration;
        else rewritten.insert(rewritten.end(),source.spirv.begin()+static_cast<std::ptrdiff_t>(at),source.spirv.begin()+static_cast<std::ptrdiff_t>(at+count));at+=count;}
    check(removed_decoration==1,"The copied clock result did not have exactly its own NoContraction decoration removed.");source.spirv=std::move(rewritten);
    source.name+="_test_only_raw_count_checkpoint";source.dxbc.clear();source.dxbc51.clear();source.dxbc50_d3d12_compatible=false;return source;
}
std::vector<std::uint32_t> gpu_words(SmoCompute& gpu,SmoBuffer buffer){const auto bytes=gpu.read(buffer);check(bytes.size()%4==0,"A clock checkpoint returned a truncated raw buffer.");std::vector<std::uint32_t> result(bytes.size()/4);
    if(!bytes.empty())std::memcpy(result.data(),bytes.data(),bytes.size());return result;}
void clock_count_checkpoint(const SmoNativeGpuOptions& options,const std::vector<std::uint64_t>& elapsed){
    const auto program=native_compile("estado segundos=0;al_actualizar{segundos=dt;}al_dibujar{}",{},real_profile);
    auto gpu=create_smo_compute(options.api,options.device);const auto kernel=gpu->kernel(count_checkpoint_kernel(smo_native_kernel(program,NativeAction::step)));const auto count=static_cast<std::uint32_t>(elapsed.size());
    std::array<std::vector<std::uint32_t>,7> raw;raw[0].resize(count*2u);raw[1].resize(1);raw[2].resize(1);raw[3].resize(count*20u);raw[4].resize(count*4u);raw[5].resize(count);raw[6].resize(count*2u);
    for(std::uint32_t item=0;item<count;++item){raw[6][item*2u]=static_cast<std::uint32_t>(elapsed[item]);raw[6][item*2u+1u]=static_cast<std::uint32_t>(elapsed[item]>>32u);}
    SmoDispatch call;call.kernel=kernel;call.constants={count,1,0,1};call.groups={(count+63u)/64u,1,1};
    for(unsigned buffer=0;buffer<raw.size();++buffer)call.buffers[buffer]=gpu->buffer(raw[buffer].size()*4u,std::as_bytes(std::span<const std::uint32_t>(raw[buffer])));
    (void)gpu->execute(std::span(&call,1),false);const auto status=gpu_words(*gpu,call.buffers[4]),states=gpu_words(*gpu,call.buffers[0]);
    check(status.size()==count*4u&&states.size()==count*2u,"The clock checkpoint omitted part of its exact ABI output.");
    for(std::uint32_t item=0;item<count;++item){check(status[item*4u]==0,"The raw clock count checkpoint reported a GPU diagnostic.");const auto expected=rounded_count_reference(elapsed[item]);
        std::ostringstream message;message<<"Exact GPU uint64→binary32 RNE checkpoint mismatch: ns="<<elapsed[item]<<" expected=0x"<<std::hex<<expected<<" actual=0x"<<states[count+item];
        check(states[count+item]==expected,message.str());}
    std::cout<<"PASS exact raw-count checkpoint api="<<gpu->info().api<<" values="<<count<<"; test-only artifact copy, production writes unchanged.\n";
}
NativeProgram manual_clock_control(){NativeProgram program;program.scalarProfile=real_profile;program.ir.states={"count","seconds"};program.identity="manual-native-binary32-clock-same-constant-divisor-v1";
    Builder reset(2);reset.store(0,reset.constant(0));reset.store(1,reset.constant(0));program.actions[0]=std::move(reset.graph);
    Builder step(2);step.store(1,step.binary(NativeOpcode::divide,step.load(0),step.constant(0x4e6e6b28u)));program.actions[2]=std::move(step.graph);return program;}

void clock_and_failures(SmoNativeGpuOptions options){
    const std::vector<std::uint64_t> elapsed{0,1,16777215,16777216,16777217,16777219,999999999,1000000000,4294967295,4294967297,9007199254740991ull,9007199254740993ull,
        (1ull<<55)-1ull,(1ull<<55)+0x7fffffffull,(1ull<<55)+0x80000000ull,(1ull<<55)+0x80000001ull,(1ull<<56)+0x100000000ull,0xffffffffff567540ull,std::numeric_limits<std::uint64_t>::max()};
    clock_count_checkpoint(options,elapsed);
    options.items=static_cast<std::uint32_t>(elapsed.size());auto clock=SmoNativeGpuProgram::compile("estado segundos=0;al_actualizar{segundos=dt;}al_dibujar{}",options);
    auto control=SmoNativeGpuProgram::from_program(manual_clock_control(),options);std::vector<std::uint32_t> seeded(elapsed.size()*2),zero_times(elapsed.size(),0);
    for(std::size_t i=0;i<elapsed.size();++i)seeded[i*2]=rounded_count_reference(elapsed[i]);control.seed_binary32_states(seeded);success(control.step_binary32(zero_times,false),options.items);
    success(clock.step_elapsed(elapsed,false),options.items);const auto actual=clock.inspect_binary32(),native=control.inspect_binary32();std::uint32_t max_ulp=0;std::size_t scaled_differences=0;
    for(std::size_t i=0;i<elapsed.size();++i){const auto expected=native_elapsed_binary32(elapsed[i]),value=actual.items[i].states.at(0),same_api=native.items[i].states.at(1);std::ostringstream message;
        message<<"Elapsed scaling differs from the same API/constant-divisor native control: ns="<<elapsed[i]<<" expected_control_bits=0x"<<std::hex<<same_api<<" actual_bits=0x"<<value;
        check(value==same_api,message.str());approximate_word(value,expected,"GPU elapsed scaling exceeds the original CPU reference bound.");
        const auto ulp=value>expected?value-expected:expected-value;max_ulp=std::max(max_ulp,ulp);
        // Vulkan OpFDiv permits2.5ULP; CPU RN adds at most0.5ULP. This
        // separate3ULP ceiling is STRICTER than the unchanged2e-4 bound.
        // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#spirvenv-precision-operation
        check(ulp<=3u,"Native elapsed FDiv exceeds its declared3ULP CPU-RN comparison ceiling.");
        if(value!=expected){++scaled_differences;std::cout<<"Clock native FDiv raw difference ns="<<elapsed[i]<<" GPU_bits=0x"<<std::hex<<value<<" CPU_RN_bits=0x"<<expected<<std::dec<<" ULP="<<ulp<<" count_checkpoint=EXACT same_API_control=EXACT\n";}}
    std::cout<<"Clock scaling api="<<clock.info().api<<" CPU_raw_differences="<<scaled_differences<<" max_ULP="<<max_ulp<<"; exact raw-count checkpoint is separate because scaled outputs may collapse neighboring counts.\n";
    options.items=2;auto program=SmoNativeGpuProgram::compile("estado a=1;\nestado b=1;\nestado r=0;\nal_actualizar {\n r = a / b;\n}\nal_dibujar{}",options);const std::array<std::uint32_t,2> zero_dt{};
    std::array<std::uint32_t,6> values{0x3f800000u,0x40000000u,0,0x3f800000u,0,0};program.seed_binary32_states(values);const auto before=program.inspect_binary32();const auto failed=program.step_binary32(zero_dt,false);
    check(!failed.committed&&failed.revision==before.revision&&failed.diagnostics[1].code==NativeError::division_zero&&failed.diagnostics[1].line==5&&failed.diagnostics[1].column==8,
        "A real runtime divisor error changed its code/source coordinate or committed the batch.");exact_frames(program.inspect_binary32().items,before.items,"A failed real update partially committed another item.");
    values[4]=0x40000000u;program.seed_binary32_states(values);success(program.step_binary32(zero_dt,false),2);check(program.inspect_binary32().items[1].states[2]==0x3f000000u,"Correcting a real divisor did not re-enable execution.");
    for(const auto& [expression,error]:std::array<std::pair<std::string_view,NativeError>,5>{{{"a*b",NativeError::underflow},{"a-b",NativeError::underflow},{"a+b",NativeError::nonfinite},{"pdir(a,b)",NativeError::exponent},{"a/b",NativeError::division_zero}}}){
        auto failing=SmoNativeGpuProgram::compile("estado a=1;estado b=1;estado r=0;al_actualizar{r="+std::string(expression)+";}al_dibujar{}",options);
        std::array<std::uint32_t,6> input{0x3f800000u,0x3f800000u,0,0x00800000u,0x3f000000u,0};
        if(expression=="a-b"){input[3]=0x00800001u;input[4]=0x00800000u;}else if(expression=="a+b"){input[3]=0x7f7fffffu;input[4]=0x7f7fffffu;}
        else if(expression=="pdir(a,b)"){input[3]=0x40000000u;input[4]=0x3f000000u;}else if(expression=="a/b")input[4]=0;
        failing.seed_binary32_states(input);const auto saved=failing.inspect_binary32();const auto rejected=failing.step_binary32(zero_dt,false);
        check(!rejected.committed&&rejected.diagnostics[1].code==error&&rejected.diagnostics[1].line>0&&rejected.diagnostics[1].column>0,"A real native domain failure was flushed, clamped or hidden.");
        exact_frames(failing.inspect_binary32().items,saved.items,"A real overflow/underflow/exponent failure changed committed quantities.");}
}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;const bool hardware=argc>=2&&std::string_view(argv[1])=="hardware";
    check(argc==1||(hardware&&argc<=3),"Usage: smo_native_real_gpu_tests [hardware [d3d11|d3d12|vulkan|opengl|all]]");artifact_contract();
    const std::string_view selector=argc==3?argv[2]:hardware?"all":"d3d11";const bool all=selector=="all";std::string adapter;
    std::optional<std::vector<NativeRealFrame32>> batch_baseline;std::optional<std::vector<SampleRecord>> sample_baseline;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){
        if(!all&&api!=smo_compute_api(selector))continue;SmoNativeGpuOptions options;options.api=api;options.scalar_profile=real_profile;options.device.software=!hardware;options.device.adapter_name=adapter;
        {auto probe=create_smo_compute(api,options.device);if(hardware){check(!probe->info().software,"A real GPU request used a software adapter.");if(adapter.empty())adapter=probe->info().adapter;options.device.adapter_name=adapter;}}
        dynamic_rules(options);invalid_inputs(options);first_error_order(options);repeated_frame_setters(options);stable_raster(options);const auto batch=independent_batch(options);if(batch_baseline)exact_frames(batch,*batch_baseline,"Exactly dyadic source batches changed raw words across native APIs.");else batch_baseline=batch;
        const auto samples=samples_and_render(options);if(sample_baseline){check(samples.size()==sample_baseline->size(),"Cross-API authored sample coverage changed.");for(std::size_t i=0;i<samples.size();++i){
            approximate_frame(samples[i].frame,(*sample_baseline)[i].frame,smo_compute_api_name(api)+" crossAPI sample_index="+std::to_string(i)+" "+samples[i].name);check(samples[i].transformed.size()==(*sample_baseline)[i].transformed.size(),"Cross-API transformed geometry count changed.");
            for(std::size_t vertex=0;vertex<samples[i].transformed.size();++vertex)for(unsigned axis=0;axis<3;++axis)approximate_word(samples[i].transformed[vertex][axis],(*sample_baseline)[i].transformed[vertex][axis],
                "Cross-API transformed FP32 geometry exceeds the unchanged2e-4 bound.");check(samples[i].pixels.size()==(*sample_baseline)[i].pixels.size(),"Cross-API image dimensions changed.");std::size_t pixel_differences=0;
            for(std::size_t pixel=0;pixel<samples[i].pixels.size();++pixel)if(samples[i].pixels[pixel]!=(*sample_baseline)[i].pixels[pixel])++pixel_differences;
            std::cout<<"Cross-API real sample "<<samples[i].name<<" differing_BGRA_pixels="<<pixel_differences<<"; no pixel tolerance or hidden exclusion.\n";
            check(pixel_differences==0,"Cross-API rendered pixels differ; preserve the mismatch instead of claiming identical coverage.");}}
        else sample_baseline=samples;clock_and_failures(options);
    }
    check(batch_baseline.has_value(),"No requested real native GPU backend was selected.");std::cout<<"PASS: "<<checks<<" real native GPU checks; CPU/cross-API raw_quantity_differences="<<raw_reference_differences<<" max_abs_error="<<maximum_reference_error
        <<" reference_bound=0.0002*max(1,abs(reference)); same-API manual control and dyadic batches require exact raw words. No F12 precision or base12 acceleration is claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native real GPU: "<<error.what()<<'\n';return 1;}}

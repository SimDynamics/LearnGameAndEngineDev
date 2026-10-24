#include "presentation/windows/smo_native_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::smo;
using namespace lgaed::presentation;
std::size_t checks=0;
void check(bool condition,std::string_view reason){++checks;if(!condition)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,reason);}
std::string integer12(std::int64_t value){constexpr std::string_view digits="0123456789#~";const bool negative=value<0;
    auto magnitude=negative?static_cast<std::uint64_t>(-(value+1))+1u:static_cast<std::uint64_t>(value);std::string text;
    do{text.push_back(digits[static_cast<std::size_t>(magnitude%12u)]);magnitude/=12u;}while(magnitude);
    if(negative)text.push_back('-');std::ranges::reverse(text);return text;}
std::string source(){return
    "vertice(-1,-1,0);\nvertice(1,-1,0);\nvertice(0,1,0);\ncara(0,1,2);\n"
    "estado posicion=10;\nestado velocidad=2;\nestado desplazamiento=1;\nestado y=0;\nestado doble=0;\nestado cuadrado=0;\nestado pasos=0;\n"
    "al_actualizar {\n posicion=posicion+velocidad*dt;\n y=posicion-desplazamiento;\n doble=y*2;\n cuadrado=pdir(dt,2);\n pasos=pasos+1;\n}\n"
    "al_dibujar {rotacion(posicion,y,cuadrado);color(1,0,1);dibujar();}\n";}
std::vector<std::string> names(){return {"posicion","velocidad","desplazamiento","y","doble","cuadrado","pasos"};}

// Independent handwritten control graph: no native_compile, compile_ir or
// reuse of the source-generated graph. Only the final native GPU emitter is
// shared. It controls source lowering, not a second independent API backend.
struct Builder {
    NativeGraph graph;
    std::array<std::uint32_t,7> current{native_no_value,native_no_value,native_no_value,native_no_value,native_no_value,native_no_value,native_no_value};
    std::uint32_t value(NativeOpcode opcode,std::array<std::uint32_t,3> args={native_no_value,native_no_value,native_no_value},std::uint32_t immediate=0){
        NativeInstruction instruction;instruction.opcode=opcode;instruction.result=graph.value_count++;instruction.arguments=args;instruction.immediate=immediate;
        graph.instructions.push_back(instruction);return instruction.result;}
    std::uint32_t constant(std::int32_t v){return value(NativeOpcode::constant,{native_no_value,native_no_value,native_no_value},std::bit_cast<std::uint32_t>(v));}
    std::uint32_t load(std::uint32_t slot){if(current.at(slot)==native_no_value)
        current[slot]=value(NativeOpcode::load_state,{native_no_value,native_no_value,native_no_value},slot);return current[slot];}
    std::uint32_t binary(NativeOpcode opcode,std::uint32_t a,std::uint32_t b){return value(opcode,{a,b,native_no_value});}
    void terminal(NativeOpcode opcode,std::array<std::uint32_t,3> args={native_no_value,native_no_value,native_no_value},std::uint32_t immediate=0){
        NativeInstruction instruction;instruction.opcode=opcode;instruction.arguments=args;instruction.immediate=immediate;graph.instructions.push_back(instruction);}
    void store(std::uint32_t slot,std::uint32_t v){terminal(NativeOpcode::store_state,{v,native_no_value,native_no_value},slot);current.at(slot)=v;}
    void draw(){terminal(NativeOpcode::rotation,{load(0),load(3),load(5)});terminal(NativeOpcode::color,{constant(1),constant(0),constant(1)});terminal(NativeOpcode::draw);}
};
NativeProgram manual_control(){NativeProgram program;program.ir.states=names();program.ir.vertices=3;program.ir.faces=1;program.identity="independent-handwritten-native-int32-control-v1";
    Builder reset;const std::array<std::int32_t,7> initial{12,2,1,0,0,0,0};
    for(std::uint32_t slot=0;slot<initial.size();++slot)reset.store(slot,reset.constant(initial[slot]));
    for(std::uint32_t vertex=0;vertex<3;++vertex){const std::array<std::array<std::int32_t,3>,3> vertices{{{-1,-1,0},{1,-1,0},{0,1,0}}};
        reset.terminal(NativeOpcode::vertex,{reset.constant(vertices[vertex][0]),reset.constant(vertices[vertex][1]),reset.constant(vertices[vertex][2])},vertex);}
    reset.terminal(NativeOpcode::face,{reset.constant(0),reset.constant(1),reset.constant(2)},0);reset.draw();program.actions[0]=std::move(reset.graph);
    Builder draw;draw.draw();program.actions[1]=std::move(draw.graph);
    Builder step;const auto dt=step.value(NativeOpcode::load_dt);
    const auto move=step.binary(NativeOpcode::multiply,step.load(1),dt);
    step.store(0,step.binary(NativeOpcode::add,step.load(0),move));
    step.store(3,step.binary(NativeOpcode::subtract,step.load(0),step.load(2)));
    step.store(4,step.binary(NativeOpcode::multiply,step.load(3),step.constant(2)));
    step.store(5,step.binary(NativeOpcode::power,dt,step.constant(2)));
    step.store(6,step.binary(NativeOpcode::add,step.load(6),step.constant(1)));
    step.draw();program.actions[2]=std::move(step.graph);return program;}

void kernel_contract(){const auto program=native_compile(source());
    for(const auto action:{NativeAction::reset,NativeAction::frame,NativeAction::step}){const auto shader=smo_native_kernel(program,action);
        check(!shader.name.empty()&&!shader.hlsl.empty()&&!shader.glsl.empty()&&shader.spirv.size()>=5&&shader.spirv[0]==0x07230203u,
            "The source compiler omitted an API-native artifact or direct in-memory SPIR-V.");
        // SPIR-V is parsed as actual instruction records. No floating type can
        // exist in this typed integer route, including hidden conversion types.
        std::size_t offset=5;while(offset<shader.spirv.size()){const auto first=shader.spirv[offset],count=first>>16,opcode=first&0xffffu;
            check(count>0&&count<=shader.spirv.size()-offset,"Direct native SPIR-V contains a malformed instruction.");
            check(opcode!=22u,"The native source route inserted an OpTypeFloat quantity or fallback.");offset+=count;}
        check(offset==shader.spirv.size(),"Direct SPIR-V ends inside an instruction.");}
}
void success(const SmoNativeGpuExecution& execution,std::uint32_t count){
    std::ostringstream failure;failure<<"A successful GPU batch was not committed or omitted item diagnostics: action="<<static_cast<std::uint32_t>(execution.action)
        <<" revision="<<execution.revision<<" expected_items="<<count<<" diagnostic_items="<<execution.diagnostics.size();
    const auto bad=std::ranges::find_if(execution.diagnostics,[](const auto& diagnostic){return diagnostic.code!=NativeError::none;});
    if(bad!=execution.diagnostics.end())failure<<" first_failed_item="<<(bad-execution.diagnostics.begin())<<" code="<<static_cast<std::uint32_t>(bad->code)
        <<" line="<<bad->line<<" column="<<bad->column<<" instruction="<<bad->instruction;
    else failure<<" first_failed_item=none";
    check(execution.committed&&execution.diagnostics.size()==count,failure.str());
    for(const auto& diagnostic:execution.diagnostics)check(diagnostic.code==NativeError::none,"A committed GPU batch hid a numerical error.");
    check(std::isfinite(execution.wall_ms)&&execution.wall_ms>=0&&std::isfinite(execution.status_readback_ms)&&execution.status_readback_ms>=0,
        "The runner omitted host/status timings or represented them as arithmetic output.");
    if(std::isfinite(execution.gpu_ms))check(execution.gpu_ms>=0,"The runner reported a negative hardware timestamp interval.");
}
void frames_equal(const std::vector<NativeFrame>& actual,const std::vector<NativeFrame>& expected,std::string_view reason){
    check(actual.size()==expected.size(),"The GPU batch lost independently addressed instances.");for(std::size_t item=0;item<actual.size();++item)check(actual[item]==expected[item],reason);}
std::vector<NativeFrame> reference_initial(const NativeProgram& program,std::uint32_t count){const auto initial=native_reference(program,NativeAction::reset);
    check(initial.diagnostic.code==NativeError::none,"The independent native CPU reference failed initialization.");return std::vector<NativeFrame>(count,initial.frame);}
std::vector<std::int32_t> random_inputs(std::uint32_t count){std::uint32_t random=0x61d012abu;const auto next=[&]{random=random*1664525u+1013904223u;return random;};
    std::vector<std::int32_t> result;result.reserve(static_cast<std::size_t>(count)*7);
    for(std::uint32_t item=0;item<count;++item){result.push_back(static_cast<std::int32_t>(next()%2001u)-1000);result.push_back(static_cast<std::int32_t>(next()%5u));
        result.push_back(static_cast<std::int32_t>(next()%41u)-20);result.push_back(static_cast<std::int32_t>(next()%201u)-100);
        result.push_back(static_cast<std::int32_t>(next()%201u)-100);result.push_back(static_cast<std::int32_t>(next()%12u));result.push_back(static_cast<std::int32_t>(next()%11u));}
    return result;}
void seed_reference(std::vector<NativeFrame>& frames,const std::vector<std::int32_t>& values){for(std::size_t item=0;item<frames.size();++item)
    frames[item].states.assign(values.begin()+static_cast<std::ptrdiff_t>(item*7),values.begin()+static_cast<std::ptrdiff_t>((item+1)*7));}
void manual_draw(NativeFrame& frame){frame.rotation={frame.states[0],frame.states[3],frame.states[5]};frame.color={1,0,1};frame.draw_requested=true;}
void manual_step(NativeFrame& frame,std::int32_t dt){auto& state=frame.states;state[0]+=state[1]*dt;state[3]=state[0]-state[2];state[4]=state[3]*2;
    state[5]=dt*dt;++state[6];manual_draw(frame);}

std::vector<NativeFrame> batch_and_control(SmoNativeGpuOptions options){options.items=options.device.software?129u:65536u;auto program=SmoNativeGpuProgram::compile(source(),options);
    auto control=SmoNativeGpuProgram::from_program(manual_control(),options);
    check(program.item_count()==options.items&&program.program().ir.states==names(),"The native GPU program changed source state order or batch size.");
    std::cout<<"Native source GPU api="<<program.info().api<<" adapter="<<std::quoted(program.info().adapter)<<" software="<<program.info().software
        <<" instances="<<options.items<<"; exact ordinary int32, source base12, no per-value decimal/packed12/F12 conversion.\n";
    success(program.reset(false),options.items);success(control.reset(false),options.items);
    auto expected=reference_initial(program.program(),options.items);frames_equal(program.inspect().items,expected,"Compiled source initialization differs from the CPU integer oracle.");
    frames_equal(control.inspect().items,expected,"The independent manual SSA control changed initial geometry/state.");
    const auto inputs=random_inputs(options.items);program.seed_states(inputs);control.seed_states(inputs);seed_reference(expected,inputs);
    for(auto& frame:expected)manual_draw(frame);success(program.frame(false),options.items);success(control.frame(false),options.items);
    frames_equal(program.inspect().items,expected,"Draw-only did not consume the real per-instance state inputs.");
    frames_equal(control.inspect().items,expected,"The manual GPU control did not consume the same real state inputs.");
    auto previous=program.inspect();success(program.frame(false),options.items);frames_equal(program.inspect().items,previous.items,"Draw-only changed authored state or animation time.");
    std::uint32_t random=0xc0124ab3u;
    for(unsigned iteration=0;iteration<4;++iteration){std::vector<std::int32_t> dt(options.items);
        for(auto& value:dt){random=random*1664525u+1013904223u;value=static_cast<std::int32_t>(random%12u);}dt[iteration]=0;
        const auto before=expected;for(std::size_t item=0;item<expected.size();++item){manual_step(expected[item],dt[item]);
            const auto independently=native_reference(program.program(),NativeAction::step,before[item],dt[item]);
            check(independently.diagnostic.code==NativeError::none&&independently.frame==expected[item],"The handwritten native oracle and source CPU reference disagree.");}
        success(program.step(dt),options.items);success(control.step(dt),options.items);
        frames_equal(program.inspect().items,expected,"The source-generated GPU update lost an item, an authored dependency or its actual dt.");
        frames_equal(control.inspect().items,expected,"The independent manual SSA GPU control differs in exact integer results.");}
    const auto final=program.inspect().items;success(program.reset(false),options.items);frames_equal(program.inspect().items,reference_initial(program.program(),options.items),"GPU reset retained previous seeded/updated state.");
    auto edited=source();const auto at=edited.find("velocidad=2");check(at!=edited.npos,"The real source edit failed to locate its authored instruction.");edited.replace(at,11,"velocidad=3");
    options.items=1;auto changed=SmoNativeGpuProgram::compile(edited,options);check(changed.program().identity!=program.program().identity,"Changed source reused the old native program identity.");
    success(changed.reset(false),1);const std::array<std::int32_t,1> dt{3};success(changed.step(dt,false),1);
    check(changed.inspect().items[0].rotation[0]==21,"The edited native source did not alter real GPU arithmetic.");
    return final;}

void ordinary_rules_and_notation(SmoNativeGpuOptions options){options.items=1;const auto text=std::string(
    "estado doce=10;estado diez=#;estado once=~;estado alias=@;estado signo=(-2)*(-3);estado vacio=7*0;estado cero=pdir(2,0);"
    "estado negativo=-1;estado minimo=")+integer12(std::numeric_limits<std::int32_t>::min())+";estado entero=10.000;al_actualizar{}al_dibujar{}";
    auto program=SmoNativeGpuProgram::compile(text,options);success(program.reset(false),1);
    check(program.inspect().items[0].states==std::vector<std::int32_t>{12,10,11,12,6,0,1,-1,std::numeric_limits<std::int32_t>::min(),12},
        "The native GPU confused ordinary arithmetic with selected SMO rules or changed exact literal magnitudes.");
    const lgaed::smo::Notation notation{'0','$','&','?'};auto selected=SmoNativeGpuProgram::compile(
        "estado diez=$;estado once=&;estado doce=?;estado combinado=1$;al_actualizar{}al_dibujar{}",options,{},notation);
    success(selected.reset(false),1);check(selected.inspect().items[0].states==std::vector<std::int32_t>{10,11,12,22},"GPU source lowering ignored the selected numeral policy.");
    for(const auto expression:{".6","1.0001","pinv(2,3)","alternativa(2*0)"})rejects([&]{(void)SmoNativeGpuProgram::compile(
        "estado resultado="+std::string(expression)+";al_actualizar{}al_dibujar{}",options);},"A genuinely fractional/special-rule source silently invoked another numerical backend.");
}

void dynamic_operations(SmoNativeGpuOptions options){options.items=129;
    auto program=SmoNativeGpuProgram::compile(
        "estado a=1;estado b=1;estado exponente=0;estado suma=0;estado resta=0;estado producto=0;estado cociente=0;estado potencia=0;"
        "al_actualizar{suma=a+b;resta=a-b;producto=a*b;cociente=a/b;potencia=pdir(a,exponente);}al_dibujar{}",options);
    success(program.reset(false),options.items);std::vector<std::int32_t> inputs(static_cast<std::size_t>(options.items)*8),dt(options.items,0);
    std::vector<std::array<std::int32_t,8>> expected;std::uint32_t random=0x32a12b45u;
    for(std::uint32_t item=0;item<options.items;++item){random=random*1664525u+1013904223u;
        const auto divisor=(item%2u?-1:1)*static_cast<std::int32_t>(random%5u+1u);random=random*1664525u+1013904223u;
        const auto quotient=static_cast<std::int32_t>(random%11u)-5,a=divisor*quotient,exponent=static_cast<std::int32_t>(item%5u);
        std::int32_t power=1;for(std::int32_t e=0;e<exponent;++e)power*=a;
        expected.push_back({a,divisor,exponent,a+divisor,a-divisor,a*divisor,quotient,power});
        std::copy_n(expected.back().begin(),3,inputs.begin()+static_cast<std::ptrdiff_t>(item*8));}
    program.seed_states(inputs);success(program.step(dt,false),options.items);const auto actual=program.inspect();
    for(std::uint32_t item=0;item<options.items;++item)check(actual.items[item].states==std::vector<std::int32_t>(expected[item].begin(),expected[item].end()),
        "A native GPU arithmetic branch failed with independently varying signed operands, exact quotients or dynamic0..4 exponents.");
}

void signed_runtime_division(SmoNativeGpuOptions options){options.items=4;
    auto program=SmoNativeGpuProgram::compile(
        "estado a=1;\nestado b=1;\nestado resultado=0;\nestado pasos=0;\nal_actualizar {\n pasos=pasos+1;\n resultado = a / b;\n}\nal_dibujar{}\n",options);
    success(program.reset(false),options.items);
    // Actual signed int32 input buffers, not constant source expressions:
    // all three sign combinations must consume the same native GPU divide.
    std::array<std::int32_t,16> states{-25,-5,0,0, -25,5,0,0, 25,-5,0,0, 24,4,0,0};
    const std::array<std::int32_t,4> dt{};program.seed_states(states);success(program.step(dt,false),options.items);
    const auto exact=program.inspect();const std::array<std::int32_t,4> quotients{5,-5,-5,6};
    for(std::uint32_t item=0;item<options.items;++item)check(exact.items[item].states==std::vector<std::int32_t>{states[item*4],states[item*4+1],quotients[item],1},
        "Runtime signed division changed -25/-5, -25/5 or25/-5, or falsely diagnosed an exact quotient.");
    states[12]=-25;states[13]=-4;program.seed_states(states);const auto before=program.inspect();
    const auto rejected=program.step(dt,false);check(!rejected.committed&&rejected.revision==before.revision&&rejected.diagnostics.size()==options.items,
        "A signed nonintegral runtime quotient committed a truncated answer.");
    for(std::uint32_t item=0;item<options.items;++item){const auto& diagnostic=rejected.diagnostics[item];
        if(item==3)check(diagnostic.code==NativeError::fraction&&diagnostic.line==7&&diagnostic.column==16,
            "-25/-4 did not preserve its exact fractional-result diagnostic and source operator.");
        else check(diagnostic.code==NativeError::none,"One nonintegral signed quotient contaminated a valid item diagnostic.");}
    frames_equal(program.inspect().items,before.items,"A rejected signed quotient partially committed any instance in the batch.");
    states[13]=-5;program.seed_states(states);success(program.step(dt,false),options.items);
    check(program.inspect().items[3].states==std::vector<std::int32_t>{-25,-5,5,1},"Correcting the signed divisor could not retry the original editable batch.");
}

void rollback(SmoNativeGpuOptions options){options.items=65;const auto source_text=std::string(
    "estado a=6;\nestado b=1;\nestado pasos=0;\nal_actualizar {\n pasos=pasos+1;\n a = a / b;\n}\nal_dibujar{}\n");
    auto program=SmoNativeGpuProgram::compile(source_text,options);success(program.reset(false),options.items);
    std::vector<std::int32_t> states(static_cast<std::size_t>(options.items)*3),dt(options.items,0);
    for(std::uint32_t item=0;item<options.items;++item){states[item*3]=6;states[item*3+1]=2;states[item*3+2]=static_cast<std::int32_t>(item);}
    states[(options.items-1)*3+1]=0;program.seed_states(states);const auto before=program.inspect();const auto revision=program.revision();
    const auto failure=program.step(dt,false);check(!failure.committed&&failure.revision==revision&&program.revision()==revision&&failure.diagnostics.size()==options.items,
        "One bad instance partially committed or advanced the global batch revision.");
    for(std::uint32_t item=0;item<options.items;++item){const auto& diagnostic=failure.diagnostics[item];if(item==options.items-1)
        check(diagnostic.code==NativeError::division_zero&&diagnostic.line==6&&diagnostic.column==8,"The real GPU zero divisor lost its precise source location.");
        else check(diagnostic.code==NativeError::none,"A single failing item contaminated another item's diagnostic.");}
    frames_equal(program.inspect().items,before.items,"A failed GPU action changed earlier successful instances or tentative state stores.");
    states[(options.items-1)*3+1]=2;program.seed_states(states);success(program.step(dt,false),options.items);const auto repaired=program.inspect();
    for(std::uint32_t item=0;item<options.items;++item)check(repaired.items[item].states==std::vector<std::int32_t>{3,2,static_cast<std::int32_t>(item+1)},
        "Correcting a divisor could not resume a failed editable GPU program.");
    auto stable=program.inspect();dt[3]=-1;const auto invalid_time=program.step(dt,false);
    check(!invalid_time.committed&&invalid_time.diagnostics[3].code==NativeError::time,"One negative elapsed input was silently evaluated or skipped.");
    frames_equal(program.inspect().items,stable.items,"Invalid elapsed time changed a globally committed GPU batch.");
    rejects([&]{(void)program.step(std::span<const std::int32_t>(dt).first(dt.size()-1),false);},"A truncated dt vector was read beyond its actual inputs.");
    frames_equal(program.inspect().items,stable.items,"A rejected dt-vector shape changed committed output.");
    rejects([&]{program.seed_states(std::span<const std::int32_t>(states).first(states.size()-1));},"A truncated item-major state upload was accepted.");
    frames_equal(program.inspect().items,stable.items,"A rejected state-upload shape overwrote another instance.");
    struct Failure{std::string expression;NativeError error;};
    for(const auto& test:std::array<Failure,11>{{{"a+1",NativeError::overflow},{"a*2",NativeError::overflow},{"(-a)",NativeError::overflow},
        {"a/(-1)",NativeError::overflow},{"a/b",NativeError::fraction},{"pdir(2,b)",NativeError::exponent},{"a-1",NativeError::overflow},
        {"0*(a+1)",NativeError::overflow},{"(a+1)*0",NativeError::overflow},{"(a+1)+(1/b)",NativeError::overflow},{"(1/b)+(a+1)",NativeError::division_zero}}}){
        options.items=2;const auto program_source="estado a=0;estado b=1;estado pasos=0;al_actualizar{pasos=pasos+1;a="+test.expression+";}al_dibujar{}";
        auto failing=SmoNativeGpuProgram::compile(program_source,options);
        success(failing.reset(false),2);std::array<std::int32_t,6> values{0,1,0,0,1,0};
        if(test.expression=="a+1"||test.expression=="a*2"||test.expression.find("a+1")!=test.expression.npos)values[3]=std::numeric_limits<std::int32_t>::max();
        else if(test.expression=="a/b"){values[3]=5;values[4]=2;}
        else if(test.expression=="pdir(2,b)")values[4]=-1;
        else values[3]=std::numeric_limits<std::int32_t>::min();
        if(test.expression.find("1/b")!=test.expression.npos)values[4]=0;
        failing.seed_states(values);const auto saved=failing.inspect();const std::array<std::int32_t,2> zero_dt{};const auto actual=failing.step(zero_dt,false);
        check(!actual.committed&&actual.diagnostics[1].code==test.error&&actual.diagnostics[1].line>0&&actual.diagnostics[1].column>0,
            "Checked native GPU arithmetic wrapped, truncated, hid an exponent error or committed a bad batch.");
        if(test.expression.find("a+1")!=test.expression.npos){const auto start=program_source.find("a="+test.expression);
            const auto error_at=test.expression.starts_with("(1/b)")?test.expression.find('/'):test.expression.find('+');
            check(start!=program_source.npos&&actual.diagnostics[1].line==1&&actual.diagnostics[1].column==start+2+error_at+1,
                "An eager native GPU failure changed its first source operator after folding or zero multiplication.");}
        frames_equal(failing.inspect().items,saved.items,"A failed checked arithmetic operation changed committed native GPU data.");}
}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;const bool hardware=argc>=2&&std::string_view(argv[1])=="hardware";
    check(argc==1||(hardware&&argc<=3),"Usage: smo_native_gpu_tests [hardware [d3d11|d3d12|vulkan|opengl|all]]");kernel_contract();
    const std::string_view selector=argc==3?argv[2]:hardware?"all":"d3d11";const bool all=selector=="all";
    std::optional<std::vector<NativeFrame>> baseline;std::string adapter;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){
        if(!all&&api!=smo_compute_api(selector))continue;SmoNativeGpuOptions options;options.api=api;options.device.software=!hardware;options.device.adapter_name=adapter;
        const auto probe=create_smo_compute(api,options.device);if(hardware){check(!probe->info().software,"A hardware request silently ran on a software adapter.");if(adapter.empty())adapter=probe->info().adapter;options.device.adapter_name=adapter;}
        ordinary_rules_and_notation(options);dynamic_operations(options);signed_runtime_division(options);const auto result=batch_and_control(options);rollback(options);
        if(baseline)frames_equal(result,*baseline,"Different native GPU APIs disagree on an exact source/state/dt batch result.");else baseline=result;
    }
    check(baseline.has_value(),"No requested native GPU API was actually selected.");
    std::cout<<"PASS: "<<checks<<" exact source/native GPU and independent handwritten-control checks; all results are binary int32, no tolerance, no claim of base12 speed superiority.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native source GPU: "<<error.what()<<'\n';return 1;}}

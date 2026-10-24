#include "presentation/windows/smo_native_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
// Numerical payload/encoding references, not a claim about source notation:
// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpConstant
// https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html#OpTypeFloat
// https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-asfloat
using namespace lgaed::smo;
using namespace lgaed::presentation;
using Word=std::uint32_t;
constexpr auto profile=NativeScalarProfile::ordinary_binary32;
std::size_t checks=0;
void check(bool value,std::string_view reason){++checks;if(!value)throw std::runtime_error(std::string(reason));}

// Independent, deliberately bounded QA decoder, NOT a decimal SMO frontend.
// The decimal rational is accumulated in integers, normalized by powers of2,
// and rounded ONCE to24 significant bits, ties to even. No float/double,
// strtod, radix12 parser or GPU execution is used to obtain the reference.
Word decimal_bits(std::string_view text){bool negative=false;if(!text.empty()&&(text.front()=='-'||text.front()=='+')){negative=text.front()=='-';text.remove_prefix(1);}
    constexpr std::uint64_t bound=1000000000000000ull;
    std::uint64_t numerator=0,denominator=1;bool point=false,seen=false;unsigned fractional_digits=0;
    for(char digit:text){if(digit=='.'){check(!point,"The QA decimal decoder received two fractional points.");point=true;continue;}
        check(digit>='0'&&digit<='9',"The bounded QA decimal decoder received a nondecimal digit.");seen=true;
        const auto value=static_cast<unsigned>(digit-'0');check(numerator<=(bound-value)/10u,"The QA decimal numerator exceeds its documented integer bound.");numerator=numerator*10u+value;
        if(point){check(++fractional_digits<=12,"The QA decimal denominator exceeds twelve fractional digits.");denominator*=10u;}}
    check(seen,"The QA decimal decoder received no digits.");const Word sign=negative?0x80000000u:0u;if(!numerator)return sign;
    int exponent=0;if(numerator>=denominator){while(numerator>=denominator*2u){denominator*=2u;++exponent;}}
    else while(numerator<denominator){numerator*=2u;--exponent;}
    auto remainder=numerator-denominator;Word significand=1u;
    for(unsigned bit=0;bit<23;++bit){remainder*=2u;significand<<=1u;if(remainder>=denominator){++significand;remainder-=denominator;}}
    const auto twice=remainder*2u;if(twice>denominator||(twice==denominator&&(significand&1u)))++significand;
    if(significand==0x1000000u){significand>>=1u;++exponent;}
    check(exponent>=-126&&exponent<=127,"The bounded QA decimal fixture is outside finite-normal binary32.");
    return sign|(static_cast<Word>(exponent+127)<<23u)|(significand&0x7fffffu);}

std::string unsigned12(std::uint64_t value){constexpr std::string_view digits="0123456789#~";std::string text;
    do{text+=digits[static_cast<std::size_t>(value%12u)];value/=12u;}while(value);std::ranges::reverse(text);return text;}
std::string signed12(std::int32_t value){return value<0?"-"+unsigned12(static_cast<std::uint64_t>(-static_cast<std::int64_t>(value))):unsigned12(static_cast<Word>(value));}
struct Literal {std::string_view duodecimal,decimal;Word expected;};
constexpr std::array literals{
    Literal{"0","0",0u},Literal{"-0","-0",0x80000000u},Literal{"1","1",0x3f800000u},Literal{"2","2",0x40000000u},
    Literal{"#","10",0x41200000u},Literal{"~","11",0x41300000u},Literal{"10","12",0x41400000u},Literal{"@","12",0x41400000u},
    Literal{"#.6","10.5",0x41280000u},Literal{"-#.6","-10.5",0xc1280000u},Literal{"10.6","12.5",0x41480000u},
    Literal{".6","0.5",0x3f000000u},Literal{".9","0.75",0x3f400000u},Literal{".3","0.25",0x3e800000u},
    Literal{".16","0.125",0x3e000000u},Literal{"1.3","1.25",0x3fa00000u},Literal{"100","144",0x43100000u},Literal{"1000","1728",0x44d80000u}};
std::string custom_literal(std::string text){for(auto& c:text){if(c=='#')c='$';else if(c=='~')c='&';else if(c=='@')c='?';}return text;}

void literal_contract(){std::size_t bytes12=0,bytes10=0;const Notation custom{'0','$','&','?'};
    for(const auto& literal:literals){check(decimal_bits(literal.decimal)==literal.expected,"The independent QA decoder disagrees with a known IEEE754 encoding.");
        check(native_parse_binary32(literal.duodecimal)==literal.expected,"Source base12 did not lower directly to the known native binary32 bits.");
        check(native_parse_binary32(custom_literal(std::string(literal.duodecimal)),custom)==literal.expected,"A literal glyph changed the numerical transport word.");
        bytes12+=literal.duodecimal.size();bytes10+=literal.decimal.size();}
    // Integer rounding ties/carry are exact mathematical values in BOTH bases.
    for(const auto& [integer,expected]:std::array<std::pair<std::uint64_t,Word>,5>{{{16777215u,0x4b7fffffu},{16777216u,0x4b800000u},
        {16777217u,0x4b800000u},{16777219u,0x4b800002u},{4294967295ull,0x4f800000u}}}){
        check(decimal_bits(std::to_string(integer))==expected,"The decimal QA decoder lost an integer ties-even/carry case.");
        check(native_parse_binary32(unsigned12(integer))==expected,"Base12 integer conversion rounded through another floating representation.");}
    for(const auto& [text,expected]:std::array<std::pair<std::string_view,std::int32_t>,6>{{{"#",10},{"~",11},{"10",12},{"@",12},{"100",144},{"1000",1728}}})
        check(native_parse_integer(text)==expected,"The integer source profile does not deliver the same signed32 value.");
    const auto ordinary=native_compile("estado a=pdir(2,0);estado b=(-1)*(-1);al_actualizar{}al_dibujar{}");
    const auto result=native_reference(ordinary,NativeAction::reset);check(result.diagnostic.code==NativeError::none&&result.frame.states==std::vector<std::int32_t>{1,1},
        "The isolated notation test enabled nonordinary zero-power or sign rules.");
    const auto divide=native_reference(native_compile("estado a=1/0;al_actualizar{}al_dibujar{}"),NativeAction::reset);
    check(divide.diagnostic.code==NativeError::division_zero,"The isolated ordinary profile silently enabled the SMO division-zero alternative.");
    std::cout<<"Literal notation only: fixture_text_bytes_base12="<<bytes12<<" decimal="<<bytes10
        <<"; each compiled native scalar remains4bytes. Source spelling length is not GPU compression.\n";}

std::string authored(){return
    "estado posicion=10.6;estado velocidad=.16;estado balance=-#.6;estado producto=0;estado cociente=0;estado cuadrado=0;estado pasos=0;"
    "al_actualizar{posicion=posicion+velocidad*dt;balance=posicion-velocidad;producto=posicion*#;cociente=posicion/10;cuadrado=pdir(dt,2);pasos=pasos+1;}"
    "al_dibujar{rotacion(0,0,posicion);color(.6,.9,1);}";}

// Handwritten QA graph builder, independent of the source parser/lowerer.
// Reuse identical constants/loads/operations, but never import source nodes.
// Explicit local temporaries below preserve authored left-to-right evaluation
// independently of C++ function-argument evaluation order.
struct ControlBuilder {
    NativeGraph graph;std::array<Word,7> states;
    std::map<std::tuple<NativeOpcode,Word,Word,Word>,Word> values;
    ControlBuilder(){states.fill(native_no_value);}
    Word scalar(NativeOpcode opcode,Word a=native_no_value,Word b=native_no_value,Word immediate=0){
        const auto key=std::tuple{opcode,a,b,immediate};if(const auto found=values.find(key);found!=values.end())return found->second;
        NativeInstruction node;node.opcode=opcode;node.result=graph.value_count++;node.arguments={a,b,native_no_value};node.immediate=immediate;
        graph.instructions.push_back(node);values.emplace(key,node.result);return node.result;}
    Word literal(std::string_view decimal){return scalar(NativeOpcode::constant,native_no_value,native_no_value,decimal_bits(decimal));}
    Word load(Word slot){auto& value=states.at(slot);if(value==native_no_value)value=scalar(NativeOpcode::load_state,native_no_value,native_no_value,slot);return value;}
    void terminal(NativeOpcode opcode,std::array<Word,3> args,Word immediate=0){NativeInstruction node;node.opcode=opcode;node.arguments=args;node.immediate=immediate;graph.instructions.push_back(node);}
    void store(Word slot,Word value){terminal(NativeOpcode::store_state,{value,native_no_value,native_no_value},slot);states.at(slot)=value;}
};
void control_draw(ControlBuilder& builder){const auto zero=builder.literal("0");const auto angle=builder.load(0);builder.terminal(NativeOpcode::rotation,{zero,zero,angle});
    const auto red=builder.literal("0.5"),green=builder.literal("0.75"),blue=builder.literal("1");builder.terminal(NativeOpcode::color,{red,green,blue});}
// RADIX-ONLY ablation: this independently authors every state, arithmetic node
// and terminal for the small known program. It is NOT a decimal language
// frontend or an independent GPU backend. Shared emission must preserve the
// same precision, checked semantics and transaction ABI on both sides.
NativeProgram manual_decimal_control(){NativeProgram control;control.scalarProfile=profile;control.identity="qa-handwritten-decimal-constant-control";
    control.ir.states={"posicion","velocidad","balance","producto","cociente","cuadrado","pasos"};
    ControlBuilder reset;reset.store(0,reset.literal("12.5"));reset.store(1,reset.literal("0.125"));
    // The source lowerer retains the positive literal before its safely folded
    // unary negation. Author that node explicitly; do not copy source output.
    (void)reset.literal("10.5");reset.store(2,reset.literal("-10.5"));
    for(Word slot=3;slot<7;++slot)reset.store(slot,reset.literal("0"));control_draw(reset);control.actions[0]=std::move(reset.graph);
    ControlBuilder frame;control_draw(frame);control.actions[1]=std::move(frame.graph);
    ControlBuilder step;
    const auto old_position=step.load(0),velocity=step.load(1),dt=step.scalar(NativeOpcode::load_dt);
    const auto move=step.scalar(NativeOpcode::multiply,velocity,dt),position=step.scalar(NativeOpcode::add,old_position,move);step.store(0,position);
    const auto balance=step.scalar(NativeOpcode::subtract,position,velocity);step.store(2,balance);
    const auto ten=step.literal("10"),product=step.scalar(NativeOpcode::multiply,position,ten);step.store(3,product);
    const auto twelve=step.literal("12"),quotient=step.scalar(NativeOpcode::divide,position,twelve);step.store(4,quotient);
    const auto two=step.literal("2"),square=step.scalar(NativeOpcode::power,dt,two);step.store(5,square);
    const auto old_steps=step.load(6),one=step.literal("1"),count=step.scalar(NativeOpcode::add,old_steps,one);step.store(6,count);
    control_draw(step);control.actions[2]=std::move(step.graph);return control;}

void prove_graph_and_align_locations(const NativeProgram& source,NativeProgram& control){
    check(source.scalarProfile==control.scalarProfile&&source.ir.states==control.ir.states&&source.ir.vertices==control.ir.vertices&&source.ir.faces==control.ir.faces,
        "The independent numeral control changed profile or state/geometry slots.");
    for(unsigned action=0;action<3;++action){const auto& a=source.actions[action];auto& b=control.actions[action];
        check(a.value_count==b.value_count&&a.instructions.size()==b.instructions.size(),"The source frontend inserted/removed work relative to the independently authored numeral control.");
        for(std::size_t ordinal=0;ordinal<a.instructions.size();++ordinal){const auto& x=a.instructions[ordinal];auto& y=b.instructions[ordinal];
            std::ostringstream reason;reason<<"Independent SSA differs at action="<<action<<" ordinal="<<ordinal<<" source_opcode="<<static_cast<Word>(x.opcode)<<" manual_opcode="<<static_cast<Word>(y.opcode);
            check(x.opcode==y.opcode&&x.result==y.result&&x.arguments==y.arguments&&x.immediate==y.immediate,reason.str());}}
    // Metadata only AFTER proving every numeric node/edge/ordinal in all
    // phases. This avoids an unfair control whose default coordinates let a
    // backend simplify diagnostic selects differently from the source path.
    for(unsigned action=0;action<3;++action)for(std::size_t ordinal=0;ordinal<source.actions[action].instructions.size();++ordinal){
        const auto& x=source.actions[action].instructions[ordinal];auto& y=control.actions[action].instructions[ordinal];y.line=x.line;y.column=x.column;}}

std::array<std::uint64_t,7> logical_buffers(const NativeProgram& program,Word items){const auto banks=static_cast<std::uint64_t>(items)*2u;
    return {banks*program.ir.states.size()*4u,banks*program.ir.vertices*3u*4u,banks*program.ir.faces*3u*4u,banks*native_frame_words*4u,
        static_cast<std::uint64_t>(items)*native_status_words*4u,static_cast<std::uint64_t>(items)*4u,banks*4u};}
void graph_contract(const NativeProgram& a,const NativeProgram& b){check(a.scalarProfile==profile&&b.scalarProfile==profile&&a.ir.states==b.ir.states&&a.ir.vertices==b.ir.vertices&&a.ir.faces==b.ir.faces,
        "The radix-only control changed precision, state slots or geometry capacity.");
    for(unsigned action=0;action<3;++action){const auto& x=a.actions[action];const auto& y=b.actions[action];check(x.value_count==y.value_count&&x.instructions.size()==y.instructions.size(),"Notations lowered to different graph sizes.");
        for(std::size_t at=0;at<x.instructions.size();++at){const auto& i=x.instructions[at];const auto& j=y.instructions[at];
            check(i.opcode==j.opcode&&i.result==j.result&&i.arguments==j.arguments&&i.immediate==j.immediate&&i.line==j.line&&i.column==j.column,
                "Literal notation changed arithmetic, dataflow, scalar bits or diagnostic order/location.");}}}
std::uint64_t hash_words(std::span<const Word> words){std::uint64_t value=14695981039346656037ull;for(const auto word:words)for(unsigned byte=0;byte<4;++byte){value^=(word>>(8u*byte))&255u;value*=1099511628211ull;}return value;}
void artifact_contract(const NativeProgram& source,const NativeProgram& control){graph_contract(source,control);
    const auto glyph_source=custom_literal(authored());const auto glyph_program=native_compile(glyph_source,Notation{'0','$','&','?'},profile);graph_contract(source,glyph_program);
    check(source.identity!=glyph_program.identity,"The authored source identity unexpectedly ignored literal glyph choices.");
    for(const auto action:{NativeAction::reset,NativeAction::frame,NativeAction::step}){const auto a=smo_native_kernel(source,action),b=smo_native_kernel(control,action),c=smo_native_kernel(glyph_program,action);
        check(a.hlsl==b.hlsl&&a.hlsl==c.hlsl&&a.glsl==b.glsl&&a.glsl==c.glsl&&a.spirv==b.spirv&&a.spirv==c.spirv,
            "Equivalent literal notations changed the complete generated GPU artifact, not just its label.");
        std::size_t arithmetic=0;bool native_float=false;
        for(std::size_t at=5;at<a.spirv.size();){const auto size=a.spirv[at]>>16u,opcode=a.spirv[at]&0xffffu;check(size&&size<=a.spirv.size()-at,"The emitted SPIR-V artifact is malformed.");
            if(opcode==22u){check(size==3&&a.spirv[at+2]==32u,"The notation comparison changed scalar precision.");native_float=true;}
            check(opcode!=7u&&opcode!=3u&&opcode!=2u,"The native GPU module unexpectedly carries authored numeral/source strings.");
            if(opcode==129u||opcode==131u||opcode==133u||opcode==136u)++arithmetic;at+=size;}
        check(native_float,"The binary32 test accidentally switched to a packed or software-radix scalar engine.");
        std::cout<<"Artifact action="<<static_cast<Word>(action)<<" identical_HLSL=true identical_GLSL=true identical_SPIRV=true SPIRV_bytes="<<a.spirv.size()*4u
            <<" SPIRV_FNV64=0x"<<std::hex<<hash_words(a.spirv)<<std::dec<<" static_FAdd_FSub_FMul_FDiv="<<arithmetic
            <<"; SPIR-V counts are compiler IR counts, not native hardware ISA.\n";}
    check(logical_buffers(source,257)==logical_buffers(control,257),"Notation changed native raw buffer sizes.");}

void success(const SmoNativeGpuExecution& execution,Word count){std::ostringstream reason;reason<<"GPU radix transaction rejected action="<<static_cast<Word>(execution.action)<<" revision="<<execution.revision;
    const auto bad=std::ranges::find_if(execution.diagnostics,[](const auto& d){return d.code!=NativeError::none;});if(bad!=execution.diagnostics.end())reason<<" item="<<(bad-execution.diagnostics.begin())
        <<" code="<<static_cast<Word>(bad->code)<<" line="<<bad->line<<" column="<<bad->column<<" instruction="<<bad->instruction;
    check(execution.committed&&execution.diagnostics.size()==count&&bad==execution.diagnostics.end(),reason.str());}
void frames_match(const std::vector<NativeRealFrame32>& a,const std::vector<NativeRealFrame32>& b,std::string_view context){check(a.size()==b.size(),"The native radix batch changed shape.");
    for(std::size_t item=0;item<a.size();++item)check(a[item]==b[item],std::string(context)+" item="+std::to_string(item));}
void device_contract(SmoNativeGpuOptions options,const NativeProgram& source,const NativeProgram& control){
    auto a=SmoNativeGpuProgram::from_program(source,options),b=SmoNativeGpuProgram::from_program(control,options);success(a.reset(false),options.items);success(b.reset(false),options.items);
    const auto initial=a.inspect_binary32().items;frames_match(initial,b.inspect_binary32().items,"Equivalent numeral compilation changed reset outputs.");
    const std::array<Word,7> expected_initial{0x41480000u,0x3e000000u,0xc1280000u,0u,0u,0u,0u};
    for(const auto& frame:initial)check(frame.states==std::vector<Word>(expected_initial.begin(),expected_initial.end()),"The real source reset did not produce independently specified literal bits.");
    std::vector<Word> seed12(static_cast<std::size_t>(options.items)*7u),seed10(seed12.size());
    for(Word item=0;item<options.items;++item){const auto offset=static_cast<std::size_t>(item)*7u;const auto position=static_cast<std::int32_t>(item%257u)-128;
        seed12[offset]=native_parse_binary32(signed12(position));seed10[offset]=decimal_bits(std::to_string(position));
        seed12[offset+1]=native_parse_binary32(item%2u?".16":"-.16");seed10[offset+1]=decimal_bits(item%2u?"0.125":"-0.125");}
    check(seed12==seed10,"Equivalent runtime inputs did not produce identical raw upload bytes.");a.seed_binary32_states(seed12);b.seed_binary32_states(seed10);
    const auto seeded=a.inspect_binary32().items;frames_match(seeded,b.inspect_binary32().items,"Runtime input upload changed a quantity based on numeral spelling.");
    constexpr std::array dt12{"0",".16",".3",".6"};constexpr std::array dt10{"0","0.125","0.25","0.5"};
    std::vector<Word> times12(options.items),times10(options.items);std::vector<NativeRealFrame32> final;
    for(unsigned frame=0;frame<4;++frame){for(Word item=0;item<options.items;++item){const auto pick=(item+frame)%dt12.size();times12[item]=native_parse_binary32(dt12[pick]);times10[item]=decimal_bits(dt10[pick]);}
        check(times12==times10,"Equivalent elapsed inputs changed uploaded binary32 bytes.");success(a.step_binary32(times12,false),options.items);success(b.step_binary32(times10,false),options.items);
        final=a.inspect_binary32().items;frames_match(final,b.inspect_binary32().items,"Same-API runtime outputs differ despite identical notation-independent artifacts and inputs.");}
    check(final!=seeded,"The test was constant-folded into an unchanged output and did not exercise dynamic state updates.");
    const auto before=a.inspect_binary32(),other=b.inspect_binary32();seed12[0]=seed10[0]=0x7f800000u;a.seed_binary32_states(seed12);b.seed_binary32_states(seed10);
    const auto failed_before=a.inspect_binary32().items;const auto failed_other=b.inspect_binary32().items;
    const auto rejected=a.step_binary32(times12,false),rejected_other=b.step_binary32(times10,false);
    check(!rejected.committed&&!rejected_other.committed&&rejected.diagnostics==rejected_other.diagnostics&&rejected.diagnostics[0].code==NativeError::nonfinite,
        "Literal-only comparison removed finite-domain guards or changed the first dynamic diagnostic.");
    frames_match(a.inspect_binary32().items,failed_before,"A failed notation batch changed committed data.");frames_match(b.inspect_binary32().items,failed_other,"A failed QA control changed committed data.");
    check(a.revision()==before.revision+1u&&b.revision()==other.revision+1u,"Rejected work advanced revision beyond the explicit seed upload.");
    success(a.reset(false),options.items);success(b.reset(false),options.items);frames_match(a.inspect_binary32().items,initial,"Reset failed to recover the authored source after a dynamic failure.");
    frames_match(b.inspect_binary32().items,initial,"Reset failed to recover the equivalent constant control.");
    const auto sizes=logical_buffers(source,options.items);std::cout<<"PASS actual_GPU api="<<a.info().api<<" adapter="<<std::quoted(a.info().adapter)<<" software="<<a.info().software<<" items="<<options.items
        <<" dynamic_frames=4 exact_raw_outputs=true per_scalar_bytes=4 per_item_state_payload_bytes=28 per_item_dt_payload_bytes=4 logical_buffer_bytes=[";
    for(unsigned i=0;i<sizes.size();++i)std::cout<<(i?",":"")<<sizes[i];std::cout<<"] (logical payload capacity; empty-buffer backing, driver alignment and dummy buffers excluded); no runtime numeral strings or decimal-radix arithmetic are uploaded.\n";}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;const bool hardware=argc>=2&&std::string_view(argv[1])=="hardware";
    check(argc==1||(hardware&&argc<=3),"Usage: smo_native_radix_tests [hardware [d3d11|d3d12|vulkan|opengl|all]]");literal_contract();
    const auto source=native_compile(authored(),{},profile);auto control=manual_decimal_control();prove_graph_and_align_locations(source,control);artifact_contract(source,control);
    const std::string_view selector=argc==3?argv[2]:hardware?"all":"d3d11";const bool all=selector=="all";std::string adapter;unsigned executed=0;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(!all&&api!=smo_compute_api(selector))continue;
        SmoNativeGpuOptions options;options.api=api;options.scalar_profile=profile;options.items=hardware?65536u:257u;options.device.software=!hardware;options.device.adapter_name=adapter;
        {const auto probe=create_smo_compute(api,options.device);if(hardware){check(!probe->info().software,"A requested physical-GPU notation test silently used software.");if(adapter.empty())adapter=probe->info().adapter;options.device.adapter_name=adapter;}}
        device_contract(options,source,control);++executed;}
    check(executed>0,"No requested GPU API was selected.");std::cout<<"PASS: "<<checks<<" radix-only checks. Base12 source is compiled directly to native binary words, without a decimal runtime intermediary. "
        <<"Equivalent scalar bits/algorithm/precision produced identical GPU artifacts and payload sizes; this establishes no GPU operation/transmission saving from notation alone. "
        <<"The decimal decoder is QA-only, not a production language frontend. No timing, FPS, F12-precision or SMO-rule optimization is claimed.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native radix isolation: "<<error.what()<<'\n';return 1;}}

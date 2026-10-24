#include "core/smo_native.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::smo;
std::size_t checks=0;
void check(bool value,std::string_view reason){++checks;if(!value)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,reason);}
// Independent positional construction for test inputs. These are integer
// magnitudes; no decimal text, floating conversion or packed12 intermediates.
std::string integer12(std::int64_t value){constexpr std::string_view digits="0123456789#~";const bool negative=value<0;
    auto magnitude=negative?static_cast<std::uint64_t>(-(value+1))+1u:static_cast<std::uint64_t>(value);std::string result;
    do{result.push_back(digits[static_cast<std::size_t>(magnitude%12u)]);magnitude/=12u;}while(magnitude);
    if(negative)result.push_back('-');std::ranges::reverse(result);return result;}
std::string scalar_source(std::string_view expression){return "estado resultado="+std::string(expression)+";\nal_actualizar{}\nal_dibujar{}\n";}
NativeExecution scalar(std::string_view expression){const auto program=native_compile(scalar_source(expression));return native_reference(program,NativeAction::reset);}
void exact_scalar(std::string_view expression,std::int32_t expected){const auto actual=scalar(expression);
    check(actual.diagnostic.code==NativeError::none&&actual.frame.states==std::vector<std::int32_t>{expected},"An ordinary signed integer expression changed its exact value.");}
void failed_scalar(std::string_view expression,NativeError expected){const auto actual=scalar(expression);
    check(actual.diagnostic.code==expected&&actual.diagnostic.line==1&&actual.diagnostic.column>0,
        "A failed integer expression was rounded, wrapped or lost its source location.");}
std::size_t slot(const NativeProgram& program,std::string_view name){const auto found=std::ranges::find(program.ir.states,name);
    check(found!=program.ir.states.end(),"The source compiler lost an authored state name.");return static_cast<std::size_t>(found-program.ir.states.begin());}

void numeration(){
    for(const auto& [text,expected]:std::array<std::pair<std::string_view,std::int32_t>,15>{{
        {"0",0},{"9",9},{"#",10},{"~",11},{"10",12},{"@",12},{"11",13},{"1#",22},{"1~",23},
        {"100",144},{"-1",-1},{"-#",-10},{"-@",-12},{"+10",12},{"-0",0}}})
        check(native_parse_integer(text)==expected,"Source base12 was confused with decimal notation or a digit-packing format.");
    const auto low=std::numeric_limits<std::int32_t>::min(),high=std::numeric_limits<std::int32_t>::max();
    for(const auto value:{low,low+1,-20736,-144,-12,-1,0,1,12,144,20736,high-1,high}){
        const auto text=integer12(value);check(native_parse_integer(text)==value,"An int32 boundary failed exact base12 parsing.");exact_scalar(text,value);}
    std::uint32_t random=0x12b1a2c3u;
    for(unsigned i=0;i<513;++i){random=random*1664525u+1013904223u;const auto value=std::bit_cast<std::int32_t>(random);
        check(native_parse_integer(integer12(value))==value,"Random signed integer parsing changed the underlying binary magnitude.");}
    const auto source=scalar_source("10");const auto compiled=native_compile(source);const auto result=native_reference(compiled,NativeAction::reset);
    check(result.diagnostic.code==NativeError::none&&std::bit_cast<std::uint32_t>(result.frame.states.at(0))==0x0000000cu,
        "The source token10 did not become the native binary word0x0000000c.");
    bool native_twelve=false;for(const auto& instruction:compiled.actions[0].instructions)
        native_twelve=native_twelve||(instruction.opcode==NativeOpcode::constant&&instruction.immediate==0x0000000cu);
    check(native_twelve,"The native SSA omitted the exact ordinary integer constant for source10.");
    const Notation custom{'0','$','&','?'};
    for(const auto& [text,expected]:std::array<std::pair<std::string_view,std::int32_t>,4>{{{"$",10},{"&",11},{"?",12},{"1$",22}}}){
        check(native_parse_integer(text,custom)==expected,"The native backend ignored selected literal glyphs.");
        const auto program=native_compile(scalar_source(text),custom);const auto value=native_reference(program,NativeAction::reset);
        check(value.diagnostic.code==NativeError::none&&value.frame.states.at(0)==expected&&program.ir.literals.at(0)==text,
            "The compiler changed source text instead of selectively interpreting numeral glyphs.");}
    for(const auto& [text,expected]:std::array<std::pair<std::string_view,std::int32_t>,4>{{{"1.0",1},{"10.000",12},{".0",0},{"-1.0",-1}}}){
        check(native_parse_integer(text)==expected,"A zero fractional suffix changed an exactly integral base12 magnitude.");exact_scalar(text,expected);}
    for(const auto text:{"",".6","-0.1","1.0001","1@","@1","1e2","12_","0x10"})
        rejects([&]{(void)native_parse_integer(text);},"An unsupported or ambiguous integer literal was silently accepted.");
    rejects([&]{(void)native_parse_integer(integer12(static_cast<std::int64_t>(high)+1));},"An out-of-range positive literal wrapped to INT_MIN.");
    rejects([&]{(void)native_parse_integer(integer12(static_cast<std::int64_t>(low)-1));},"A literal below INT_MIN was silently truncated.");
    rejects([&]{(void)native_compile(scalar_source(integer12(static_cast<std::int64_t>(high)+1)));},"A positive source magnitude2^31 was accepted without unary minus.");
    for(const auto text:{"1.0001",".6","pinv(2,3)","alternativa(2*0)"})
        rejects([&]{(void)native_compile(scalar_source(text));},"A fractional or special-rule expression silently fell back to another numerical engine.");
    rejects([&]{(void)native_compile(scalar_source("#"),custom);},"A numeral from a different notation policy was accepted implicitly.");
    auto invalid=custom;invalid.zero='?';rejects([&]{(void)native_compile(scalar_source("0"),invalid);},"The user's fixed0=cero rule changed in the native route.");
}

void ordinary_arithmetic(){
    // The first typed native profile is conventional signed integer arithmetic,
    // deliberately separate from the original selectable SMO rule profiles.
    exact_scalar("(-1)*(-1)",1);exact_scalar("(-2)*(-3)",6);exact_scalar("7*0",0);exact_scalar("0*7",0);
    exact_scalar("pdir(2,0)",1);exact_scalar("pdir(0,0)",1);exact_scalar("pdir(-2,3)",-8);exact_scalar("pdir(-2,4)",16);
    exact_scalar("2+3*4",14);exact_scalar("(2+3)*4",20);exact_scalar("-(-2)",2);exact_scalar("+(-2)",-2);
    for(std::int32_t a=-9;a<=9;++a)for(std::int32_t b=-9;b<=9;++b){const auto left="("+integer12(a)+")",right="("+integer12(b)+")";
        exact_scalar(left+"+"+right,a+b);exact_scalar(left+"-"+right,a-b);exact_scalar(left+"*"+right,a*b);
        if(!b)failed_scalar(left+"/"+right,NativeError::division_zero);
        else if(a%b)failed_scalar(left+"/"+right,NativeError::fraction);else exact_scalar(left+"/"+right,a/b);}
    for(std::int32_t base=-5;base<=5;++base){std::int32_t expected=1;for(unsigned power=0;power<=5;++power){
        exact_scalar("pdir("+integer12(base)+","+integer12(power)+")",expected);expected*=base;}}
    const auto low=integer12(std::numeric_limits<std::int32_t>::min()),high=integer12(std::numeric_limits<std::int32_t>::max());
    failed_scalar(high+"+1",NativeError::overflow);failed_scalar("("+low+")-1",NativeError::overflow);
    failed_scalar("0*("+high+"+1)",NativeError::overflow);failed_scalar("("+high+"+1)*0",NativeError::overflow);
    failed_scalar(high+"*2",NativeError::overflow);failed_scalar("-("+low+")",NativeError::overflow);
    failed_scalar("("+low+")/(-1)",NativeError::overflow);failed_scalar("pdir(2,"+integer12(31)+")",NativeError::overflow);
    failed_scalar("pdir(2,-1)",NativeError::exponent);failed_scalar("pdir(1,"+integer12(65)+")",NativeError::exponent);
    failed_scalar("5/2",NativeError::fraction);failed_scalar("(-5)/2",NativeError::fraction);
}

std::string authored_program(){return
    "vertice(-1,-1,0);\nvertice(1,-1,0);\nvertice(0,1,0);\ncara(0,1,2);\n"
    "estado posicion=10;\nestado velocidad=2;\nestado desplazamiento=1;\nestado y=0;\nestado doble=0;\nestado cuadrado=0;\nestado pasos=0;\n"
    "al_actualizar {\n posicion=posicion+velocidad*dt;\n y=posicion-desplazamiento;\n doble=y*2;\n cuadrado=pdir(dt,2);\n pasos=pasos+1;\n}\n"
    "al_dibujar {rotacion(posicion,y,cuadrado);color(1,0,1);dibujar();}\n";}

void program_and_graphs(){const auto source=authored_program();const auto program=native_compile(source);
    check(program.ir.states.size()==7&&program.ir.vertices==3&&program.ir.faces==1&&!program.identity.empty(),"The typed native program omitted source state, geometry or source identity.");
    check(program.identity==native_compile(source).identity,"Identical source produced an unstable native program identity.");
    for(const auto& graph:program.actions){std::vector<bool> defined(graph.value_count,false);
        for(const auto& instruction:graph.instructions){check(instruction.line>0&&instruction.column>0,"An SSA instruction lost its original source coordinates.");
            for(const auto argument:instruction.arguments)if(argument!=native_no_value)
                check(argument<defined.size()&&defined[argument],"The native graph is not a valid dominating postfix-to-SSA lowering.");
            if(instruction.result!=native_no_value){check(instruction.result<defined.size()&&!defined[instruction.result],"The native graph assigns an SSA result more than once.");defined[instruction.result]=true;}}
        check(std::ranges::all_of(defined,[](bool value){return value;}),"The SSA value count contains unwritten numeric registers.");}
    const auto reset=native_reference(program,NativeAction::reset);check(reset.diagnostic.code==NativeError::none,"The authored integer program failed initialization.");
    auto frame=reset.frame;
    check(frame.vertices==std::vector<std::array<std::int32_t,3>>{{-1,-1,0},{1,-1,0},{0,1,0}}&&
        frame.faces==std::vector<std::array<std::uint32_t,3>>{{0,1,2}}&&frame.color==std::array<std::int32_t,3>{1,0,1}&&frame.draw_requested,
        "Native initialization/drawing changed the authored integer geometry or color.");
    check(frame.states.at(slot(program,"posicion"))==12&&frame.rotation==std::array<std::int32_t,3>{12,0,0},"Initial drawing did not use the actual source states.");
    const auto only_draw=native_reference(program,NativeAction::frame,frame);check(only_draw.diagnostic.code==NativeError::none&&only_draw.frame==frame,"Drawing alone executed an unsolicited update.");
    std::int32_t position=12;unsigned tick=0;
    for(const std::int32_t dt:{3,0,5,1}){const auto next=native_reference(program,NativeAction::step,frame,dt);check(next.diagnostic.code==NativeError::none,"A bounded authored native update failed.");
        frame=next.frame;position+=2*dt;++tick;
        check(frame.states.at(slot(program,"posicion"))==position&&frame.states.at(slot(program,"y"))==position-1&&
            frame.states.at(slot(program,"doble"))==2*(position-1)&&frame.states.at(slot(program,"cuadrado"))==dt*dt&&
            frame.states.at(slot(program,"pasos"))==static_cast<std::int32_t>(tick)&&frame.rotation==std::array<std::int32_t,3>{position,position-1,dt*dt},
            "A real dt/update dependency differs from an independent ordinary integer calculation.");}
    check(native_reference(program,NativeAction::reset,frame).frame==reset.frame,"Reset did not restore the authored three-phase program.");
    const auto negative_time=native_reference(program,NativeAction::step,frame,-1);check(negative_time.diagnostic.code==NativeError::time,"Negative elapsed time was accepted in the integer profile.");
    auto edited=source;const auto at=edited.find("velocidad=2");check(at!=edited.npos,"The source edit did not locate the authored speed.");edited.replace(at,11,"velocidad=3");
    const auto changed=native_compile(edited);check(changed.identity!=program.identity,"A genuine authored source edit retained an obsolete native identity.");
    const auto changed_initial=native_reference(changed,NativeAction::reset);const auto changed_step=native_reference(changed,NativeAction::step,changed_initial.frame,3);
    check(changed_step.diagnostic.code==NativeError::none&&changed_step.frame.rotation[0]==21,"Editing source did not change the compiled native update.");
    for(const auto action:{NativeAction::reset,NativeAction::frame,NativeAction::step}){
        const auto hlsl=native_hlsl(program,action),glsl=native_glsl(program,action);
        check(!hlsl.empty()&&!glsl.empty()&&hlsl.find("smo_f12_")==hlsl.npos&&glsl.find("smo_f12_")==glsl.npos&&
            hlsl.find("smo_d12_")==hlsl.npos&&glsl.find("smo_d12_")==glsl.npos,"The ordinary native backend silently invoked a software radix12 arithmetic engine.");}
}

void runtime_diagnostics(){const auto source=std::string("estado a=1;\nestado b=0;\nal_actualizar {\n  a = a / b;\n}\nal_dibujar{}\n");
    const auto program=native_compile(source);const auto initial=native_reference(program,NativeAction::reset);
    check(initial.diagnostic.code==NativeError::none,"A dynamic division-zero test failed before its update.");
    const auto failed=native_reference(program,NativeAction::step,initial.frame,0);
    check(failed.diagnostic.code==NativeError::division_zero&&failed.diagnostic.line==4&&failed.diagnostic.column==9,
        "An actual runtime zero divisor lost its precise source operator location.");
    const auto high=integer12(std::numeric_limits<std::int32_t>::max());
    for(const bool divide_first:{false,true}){const auto expression=divide_first?"(1/0)+("+high+"+1)":"("+high+"+1)+(1/0)";
        const auto text=scalar_source(expression);const auto at=text.find(divide_first?"/0":"+1");
        check(at!=text.npos,"The first-error test did not locate its authored operator.");const auto actual=scalar(expression);
        check(actual.diagnostic.code==(divide_first?NativeError::division_zero:NativeError::overflow)&&actual.diagnostic.line==1&&actual.diagnostic.column==at+1,
            "Constant folding, CSE or execution changed eager left-to-right error identity/source coordinates.");}
    for(const auto& [source_text,error]:std::array<std::pair<std::string,NativeError>,4>{{
        {"vertice(0,0,0);vertice(1,0,0);vertice(0,1,0);cara(0,1,3);al_actualizar{}al_dibujar{dibujar();}",NativeError::index},
        {"vertice(0,0,0);vertice(1,0,0);vertice(0,1,0);cara(-1,1,2);al_actualizar{}al_dibujar{dibujar();}",NativeError::index},
        {"al_actualizar{}al_dibujar{color(2,0,0);}",NativeError::color},
        {"al_actualizar{}al_dibujar{dibujar();}",NativeError::geometry}}}){
        const auto bad=native_compile(source_text);const auto result=native_reference(bad,NativeAction::reset);
        check(result.diagnostic.code==error&&result.diagnostic.line>0&&result.diagnostic.column>0,"A malformed numeric geometry result was silently repaired.");}
    for(const auto invalid:{"estado x=x;al_actualizar{}al_dibujar{}","estado x=dt;al_actualizar{}al_dibujar{}",
        "estado x=0;al_actualizar{no_existe=1;}al_dibujar{}"})
        rejects([&]{(void)native_compile(invalid);},"Native compilation bypassed the existing initialized-state/name/phase validation.");
    rejects([&]{(void)compile_ir(scalar_source("1"));},"The new compute-only route weakened the existing default geometry contract.");
    check(compile_ir(scalar_source("1"),{},false).states==std::vector<std::string>{"resultado"},
        "Explicit compute-only IR export still requires fabricated geometry.");
}
} //namespace

int main(){try{numeration();ordinary_arithmetic();program_and_graphs();runtime_diagnostics();
    std::cout<<"PASS: "<<checks<<" exact source-base12/native-int32 compiler and CPU-reference checks; ordinary integer semantics, no fractional or SMO-rule fallback.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native SMO source compiler: "<<error.what()<<'\n';return 1;}}

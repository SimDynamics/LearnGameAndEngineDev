#include "core/smo_native.hpp"
#include "native_build_config.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace lgaed::smo;
constexpr auto real_profile=NativeScalarProfile::ordinary_binary32;
std::size_t checks=0;
void check(bool value,std::string_view reason){++checks;if(!value)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool failed=false;try{action();}catch(const std::exception&){failed=true;}check(failed,reason);}
// An independent bounded integer oracle constructs exact radix12 spellings of
// dyadic rationals. It uses neither production parsing nor floating arithmetic.
struct UInt256 {
    std::array<std::uint32_t,8> words{};
    explicit UInt256(std::uint64_t n=0){words[0]=static_cast<std::uint32_t>(n);words[1]=static_cast<std::uint32_t>(n>>32);}
    bool zero()const{return std::ranges::all_of(words,[](auto word){return word==0;});}
    void multiply(std::uint32_t factor){std::uint64_t carry=0;for(auto& word:words){const auto product=static_cast<std::uint64_t>(word)*factor+carry;word=static_cast<std::uint32_t>(product);carry=product>>32;}
        check(carry==0,"The test's exact dyadic constructor exceeded its declared256-bit bound.");}
    std::uint32_t divide(std::uint32_t divisor){std::uint64_t remainder=0;for(std::size_t i=words.size();i>0;--i){const auto value=(remainder<<32)|words[i-1];words[i-1]=static_cast<std::uint32_t>(value/divisor);remainder=value%divisor;}return static_cast<std::uint32_t>(remainder);}
    int compare(const UInt256& other)const{for(std::size_t i=words.size();i>0;--i)if(words[i-1]!=other.words[i-1])return words[i-1]<other.words[i-1]?-1:1;return 0;}
    void subtract(const UInt256& other){check(compare(other)>=0,"The independent rational constructor attempted a negative unsigned remainder.");std::uint64_t borrow=0;
        for(std::size_t i=0;i<words.size();++i){const auto a=static_cast<std::uint64_t>(words[i]),b=static_cast<std::uint64_t>(other.words[i])+borrow;words[i]=static_cast<std::uint32_t>(a-b);borrow=a<b?1u:0u;}
        check(borrow==0,"The independent rational constructor lost a borrow.");}
};
std::string whole12(UInt256 value){constexpr std::string_view digits="0123456789#~";std::string result;
    do{result.push_back(digits[value.divide(12)]);}while(!value.zero());std::ranges::reverse(result);return result;}
std::string dyadic(std::uint64_t numerator,int binary_exponent,bool negative=false){std::string result;
    if(binary_exponent>=0){UInt256 integer(numerator);for(int bit=0;bit<binary_exponent;++bit)integer.multiply(2);result=whole12(integer);}
    else{const auto power=static_cast<unsigned>(-binary_exponent);check(power<240,"The test's dyadic denominator exceeded its declared bound.");
        const auto whole=power<64?numerator>>power:0;const auto remainder=power<64?numerator-(whole<<power):numerator;
        result=whole12(UInt256(whole));UInt256 remaining(remainder),denominator(1);for(unsigned bit=0;bit<power;++bit)denominator.multiply(2);
        if(!remaining.zero()){result.push_back('.');constexpr std::string_view digits="0123456789#~";unsigned places=0;
            while(!remaining.zero()){check(++places<=power,"An exact dyadic fraction did not terminate in radix12.");remaining.multiply(12);unsigned digit=0;
                while(remaining.compare(denominator)>=0){remaining.subtract(denominator);++digit;}check(digit<12,"A rational digit escaped radix12.");result.push_back(digits[digit]);}}}
    if(negative)result.insert(result.begin(),'-');return result;}
std::string spelling(std::uint32_t bits){const auto exponent=(bits>>23)&255u,fraction=bits&0x7fffffu;const bool negative=(bits>>31)!=0;
    check(exponent!=255u,"The test attempted to spell NaN or infinity as an ordinary numeral.");
    if(exponent==0)return dyadic(fraction,-149,negative);return dyadic(0x800000u|fraction,static_cast<int>(exponent)-150,negative);}
NativeProgram compile(std::string_view text){return native_compile(text,{},real_profile);}
NativeRealExecution scalar(std::string_view expression){return native_reference_real(compile("estado resultado="+std::string(expression)+";al_actualizar{}al_dibujar{}"),NativeAction::reset);}
void exact(std::string_view expression,std::uint32_t expected){const auto actual=scalar(expression);
    check(actual.diagnostic.code==NativeError::none&&actual.frame.states==std::vector<std::uint32_t>{expected},"An ordinary binary32 expression changed its expected IEEE754 word.");}
void failure(std::string_view expression,NativeError expected){const auto actual=scalar(expression);
    check(actual.diagnostic.code==expected&&actual.diagnostic.line==1&&actual.diagnostic.column>0,"A failed binary32 expression was silently clamped, flushed or lost its source coordinate.");}
std::string sample(std::string_view filename){std::ifstream input(std::filesystem::path(lgaed::build::source_root)/"native"/filename,std::ios::binary);
    check(input.is_open(),"The authored source sample is missing.");std::ostringstream text;text<<input.rdbuf();return text.str();}

void parser(){
    for(const auto& [text,bits]:std::array<std::pair<std::string_view,std::uint32_t>,13>{{
        {"0",0},{"-0",0x80000000u},{"1",0x3f800000u},{"#",0x41200000u},{"~",0x41300000u},{"10",0x41400000u},{"@",0x41400000u},
        {".6",0x3f000000u},{".3",0x3e800000u},{".1",0x3daaaaabu},{".2",0x3e2aaaabu},{".8",0x3f2aaaabu},{"1.6",0x3fc00000u}}}){
        check(native_parse_binary32(text)==bits,"Exact radix12 source was interpreted as decimal, double-rounded or packed.");exact(text,bits);}
    for(const auto& [numerator,expected]:std::array<std::pair<std::uint64_t,std::uint32_t>,4>{{
        {16777217ull,0x3f800000u},{16777219ull,0x3f800002u},{33554431ull,0x40000000u},{16777218ull,0x3f800001u}}}){
        const auto text=dyadic(numerator,-24);check(native_parse_binary32(text)==expected,"A binary32 midpoint did not use one exact ties-to-even rounding.");
        check(native_parse_binary32("-"+text)==(expected|0x80000000u),"Negative midpoint rounding differs from the selected exact magnitude.");}
    // The minimum-normal boundary is checked AFTER rounding. The midpoint
    // above the largest subnormal rounds upward to the even normal word.
    check(native_parse_binary32(dyadic(16777215ull,-150))==0x00800000u,"A rational rounding to minimum-normal was prematurely rejected.");
    check(native_parse_binary32(dyadic(33554431ull,-151))==0x00800000u,"A rational just above the normal-boundary midpoint was rejected.");
    rejects([&]{(void)native_parse_binary32(dyadic(33554429ull,-151));},"A rational rounding to a subnormal was accepted by the normal-only profile.");
    check(native_parse_binary32(dyadic(67108861ull,102))==0x7f7fffffu,"A value below the overflow midpoint did not round to maximum-finite.");
    rejects([&]{(void)native_parse_binary32(dyadic(33554431ull,103));},"The overflow midpoint silently became infinity.");
    for(const auto bits:{0u,0x80000000u,0x00800000u,0x00800001u,0x3f000000u,0x3f800000u,0x3f800001u,0x40000000u,0x7f7fffffu,0xff7fffffu})
        check(native_parse_binary32(spelling(bits))==bits,"An exactly representable normal/zero failed its integer-oracle source round trip.");
    std::uint32_t random=0x4f3212abu;for(unsigned i=0;i<257;++i){random=random*1664525u+1013904223u;
        const auto bits=(random&0x807fffffu)|((1u+(random>>8)%254u)<<23);
        check(native_parse_binary32(spelling(bits))==bits,"Random binary32 normal bits changed after exact source-base12 parsing.");}
    for(const auto text:{"", ".", "1@", "@1", "@.0", "1e2", "NaN", "Inf", "1..0", "12_"})
        rejects([&]{(void)native_parse_binary32(text);},"An invalid binary32 numeral was accepted or treated as another notation.");
    for(const auto bits:{1u,0x007fffffu,0x80000001u})rejects([&]{(void)native_parse_binary32(spelling(bits));},"A subnormal literal bypassed the explicit finite-normal profile.");
    rejects([&]{(void)native_parse_binary32(dyadic(1,-151));},"A genuinely nonzero literal silently rounded to zero.");
    const Notation selected{'0','$','&','?'};check(native_parse_binary32("$",selected)==0x41200000u&&native_parse_binary32("&",selected)==0x41300000u&&
        native_parse_binary32("?",selected)==0x41400000u,"The real parser ignored the selected numeral glyphs.");
}

void arithmetic(){
    exact(".6+.3",0x3f400000u);exact(".6-.3",0x3e800000u);exact(".6*.6",0x3e800000u);exact(".6/2",0x3e800000u);
    exact(".1/.2",0x3f000000u);exact("(-1)*(-1)",0x3f800000u);exact("pdir(2,0)",0x3f800000u);exact("pdir(0,0)",0x3f800000u);
    exact("pdir(2,-1)",0x3f000000u);exact("pdir(-2,-3)",0xbe000000u);exact("pdir(2,-54)",0x1f800000u);
    exact("-(-0)",0);exact("(-0)*2",0x80000000u);exact("0*2",0);
    const auto minimum=spelling(0x00800000u),maximum=spelling(0x7f7fffffu),next=spelling(0x00800001u);
    exact(minimum+"-"+minimum,0);exact("pdir("+minimum+",-1)",0x7e800000u);
    failure(minimum+"*.6",NativeError::underflow);failure(next+"-"+minimum,NativeError::underflow);failure(minimum+"*"+minimum,NativeError::underflow);
    failure(maximum+"*2",NativeError::nonfinite);failure("0*("+maximum+"*2)",NativeError::nonfinite);
    failure("("+maximum+"*2)*0",NativeError::nonfinite);failure("1/0",NativeError::division_zero);failure("0/0",NativeError::division_zero);
    failure("pdir(2,.6)",NativeError::exponent);failure("pdir(2,55)",NativeError::exponent);failure("pdir(0,-1)",NativeError::division_zero);
    for(const bool divide_first:{false,true}){const auto expression=divide_first?"(1/0)+("+maximum+"*2)":"("+maximum+"*2)+(1/0)";
        const auto text="estado resultado="+expression+";al_actualizar{}al_dibujar{}";const auto at=text.find(divide_first?"/0":"*2");
        check(at!=text.npos,"The binary32 first-error case did not locate its authored operator.");const auto actual=native_reference_real(compile(text),NativeAction::reset);
        check(actual.diagnostic.code==(divide_first?NativeError::division_zero:NativeError::nonfinite)&&actual.diagnostic.line==1&&actual.diagnostic.column==at+1,
            "Binary32 folding or execution replaced the first of two different errors or its exact source operator.");
        check(actual.frame==NativeRealFrame32{},"A binary32 reset containing two errors published a partial frame.");}
    for(const auto text:{"pinv(2,3)","alternativa(2*0)"})rejects([&]{(void)compile("estado x="+std::string(text)+";al_actualizar{}al_dibujar{}");},"The ordinary real profile silently enabled special SMO rules.");
    rejects([&]{(void)native_reference_real(native_compile("estado x=1;al_actualizar{}al_dibujar{}"),NativeAction::reset);},"A binary32 oracle reinterpreted an integer program's raw words.");
}

std::uint32_t integer_to_binary32(std::uint64_t value){if(!value)return 0;unsigned exponent=63u-static_cast<unsigned>(std::countl_zero(value));std::uint64_t significand;
    if(exponent<=23)significand=value<<(23u-exponent);else{const auto shift=exponent-23u;significand=value>>shift;const auto remainder=value&((std::uint64_t{1}<<shift)-1u),half=std::uint64_t{1}<<(shift-1u);
        if(remainder>half||(remainder==half&&(significand&1u)))++significand;if(significand==0x1000000u){significand>>=1;++exponent;}}
    return ((exponent+127u)<<23)|static_cast<std::uint32_t>(significand&0x7fffffu);}
void clock_contract(){
    for(const auto ns:{0ull,1ull,999999999ull,1000000000ull,16777215ull,16777216ull,16777217ull,16777219ull,4294967295ull,4294967297ull,
        9007199254740991ull,9007199254740993ull,0xffffffffff567540ull,std::numeric_limits<std::uint64_t>::max()}){
        const auto count_bits=integer_to_binary32(ns);volatile float count=std::bit_cast<float>(count_bits),denominator=std::bit_cast<float>(0x4e6e6b28u);
        const float seconds=count/denominator;const auto expected=std::bit_cast<std::uint32_t>(seconds);
        check(native_elapsed_binary32(ns)==expected,"Elapsed nanoseconds were narrowed, double-rounded, scaled in another radix or converted more than once.");
        check(native_parse_binary32(whole12(UInt256(ns)))==count_bits,"The literal and clock integer bridges disagree with independent guard/sticky ties-even rounding.");}
}

void programs(){auto triangle=compile(sample("smo_triangle.smo"));check(triangle.scalarProfile==real_profile&&triangle.ir.vertices==3&&triangle.ir.faces==1,"The real compiler lost its profile or actual triangle geometry.");
    const std::string shared="estado x=1;al_actualizar{}al_dibujar{}";
    check(compile(shared).identity!=native_compile(shared).identity,"Different numerical profiles shared an incompatible compiled program identity.");
    const auto initial=native_reference_real(triangle,NativeAction::reset);check(initial.diagnostic.code==NativeError::none&&initial.frame.draw_requested&&
        initial.frame.color==std::array<std::uint32_t,3>{0x3f800000u,0x3f000000u,0x3e2aaaabu},"The original triangle's fractional colors were rounded from the wrong radix.");
    const auto advanced=native_reference_real(triangle,NativeAction::step,initial.frame,0x3e800000u);check(advanced.diagnostic.code==NativeError::none&&advanced.frame.rotation[2]==0x3e800000u,
        "The original authored update did not use real elapsed seconds.");
    check(native_reference_real(triangle,NativeAction::frame,advanced.frame).frame==advanced.frame,"Draw-only unexpectedly advanced a real source program.");
    check(native_reference_real(triangle,NativeAction::reset,advanced.frame).frame==initial.frame,"Real reset did not restore source state.");
    check(native_reference_real(triangle,NativeAction::step,advanced.frame,0xbf800000u).diagnostic.code==NativeError::time,"Negative real elapsed time was accepted.");
    check(native_reference_real(triangle,NativeAction::step,advanced.frame,0x80000000u).diagnostic.code==NativeError::none,"Negative zero elapsed time was incorrectly treated as negative duration.");
    auto cube=compile(sample("smo_cube.smo"));const auto cube_initial=native_reference_real(cube,NativeAction::reset);const auto cube_next=native_reference_real(cube,NativeAction::step,cube_initial.frame,0x3f000000u);
    check(cube_next.diagnostic.code==NativeError::none&&cube_next.frame.vertices.size()==8&&cube_next.frame.faces.size()==12&&cube_next.frame.rotation==std::array<std::uint32_t,3>{0x3e000000u,0x3e800000u,0},
        "The original cube's real update/division/geometry changed in the native binary32 route.");
    const auto source="estado a=1;\nestado b=0;\nal_actualizar {\n a = a / b;\n}\nal_dibujar{}";const auto bad=compile(source);const auto before=native_reference_real(bad,NativeAction::reset);
    const auto failed=native_reference_real(bad,NativeAction::step,before.frame,0);check(failed.diagnostic.code==NativeError::division_zero&&failed.diagnostic.line==4&&failed.diagnostic.column==8&&failed.frame==before.frame,
        "A failing real update lost its source operator or transactional previous state.");
    auto tick=compile("estado pasos=0;al_actualizar{pasos=pasos+1;}al_dibujar{}");const auto zero=native_reference_real(tick,NativeAction::reset);
    check(native_reference_real(tick,NativeAction::step,zero.frame,0).frame.states[0]==0x3f800000u,"step(0) was silently replaced with a host pause.");
}
void repeated_frame_setters(){const auto source=std::string(
    "vertice(-1,-1,0);vertice(1,-1,0);vertice(0,1,0);cara(0,1,2);\n"
    "estado canal=.6;\nal_actualizar {canal=canal+1;}\nal_dibujar {\n color(.2,.8,1);\n rotacion(1,2,3);\n color(canal,0,0);\n color(.6,.3,.9);\n rotacion(4,5,6);\n dibujar();\n}\n");
    const auto program=compile(source);const auto reset=native_reference_real(program,NativeAction::reset);
    check(reset.diagnostic.code==NativeError::none&&reset.frame.color==std::array<std::uint32_t,3>{0x3f000000u,0x3e800000u,0x3f400000u}&&
        reset.frame.rotation==std::array<std::uint32_t,3>{0x40800000u,0x40a00000u,0x40c00000u},"Repeated frame setters did not retain exactly the last authored color/rotation.");
    check(native_reference_real(program,NativeAction::frame,reset.frame).frame==reset.frame,"Draw-only changed the final repeated-setter payload.");
    const auto failed=native_reference_real(program,NativeAction::step,reset.frame,0);
    check(failed.diagnostic.code==NativeError::color&&failed.diagnostic.line==7&&failed.diagnostic.column==2&&failed.frame==reset.frame,
        "A later valid color suppressed an earlier overwritten invalid color, its first source location or rollback.");
}
void constant_geometry_emission(){std::string source;
    // Full existing geometry bounds exercise the source compiler, not a
    // reduced GPU workload or a substitute for the physical render test.
    for(unsigned vertex=0;vertex<1024;++vertex)source+="vertice("+whole12(UInt256(vertex%12))+",0,0);\n";
    for(unsigned face=0;face<1536;++face)source+="cara("+whole12(UInt256(face%1024))+","+whole12(UInt256((face+1)%1024))+","+whole12(UInt256((face+2)%1024))+");\n";
    source+="al_actualizar{}al_dibujar{color(.2,.8,1);dibujar();}";check(source.size()<=64u*1024u,"The compiler-only dense fixture exceeded the existing source bound.");
    const auto program=compile(source);const auto frame=native_reference_real(program,NativeAction::reset);
    check(frame.diagnostic.code==NativeError::none&&frame.frame.vertices.size()==1024&&frame.frame.faces.size()==1536&&frame.frame.draw_requested,
        "Proving constant geometry guards changed the full-size CPU result.");
    for(const auto& shader:{native_hlsl(program,NativeAction::reset),native_glsl(program,NativeAction::reset)}){const auto at=shader.find("void "+std::string(shader.starts_with("#version")?"main":"CSMain")+"(");
        check(at!=shader.npos,"The dense native shader omitted its entry point.");const auto body=shader.substr(at);
        check(body.find("=nload(")==body.npos&&body.find("faceSafe")==body.npos&&body.find("bad(diagnostic,5u,")==body.npos&&body.find("bad(diagnostic,6u,")==body.npos,
            "Statically valid scalar/face/color constants generated repeated runtime diagnostic chains.");}
    const std::string triangle="vertice(0,0,0);vertice(1,0,0);vertice(0,1,0);";
    for(const auto expression:{"-.6",".6","3"}){const auto invalid=compile(triangle+"cara("+expression+",1,2);al_actualizar{}al_dibujar{dibujar();}");
        check(native_reference_real(invalid,NativeAction::reset).diagnostic.code==NativeError::index,"A static negative/fractional/out-of-range index was accepted.");
        for(const auto& shader:{native_hlsl(invalid,NativeAction::reset),native_glsl(invalid,NativeAction::reset)})check(shader.find("faceSafe")!=shader.npos&&shader.find("bad(diagnostic,5u,")!=shader.npos,
            "An unproved constant face lost its safe conversion or index diagnostic.");}
    const auto signed_zero=compile(triangle+"cara(-0,1,2);al_actualizar{}al_dibujar{color(-0,.8,1);dibujar();}");const auto zero_frame=native_reference_real(signed_zero,NativeAction::reset);
    check(zero_frame.diagnostic.code==NativeError::none&&zero_frame.frame.faces[0][0]==0&&zero_frame.frame.color[0]==0x80000000u,
        "Constant guard proofs changed valid negative-zero index/color semantics.");
    auto invalid_literal=compile("estado x=1;al_actualizar{}al_dibujar{}");auto& graph=invalid_literal.actions[0];bool changed=false;
    for(auto& node:graph.instructions)if(node.opcode==NativeOpcode::constant){node.immediate=0x7f800000u;changed=true;break;}
    check(changed&&native_reference_real(invalid_literal,NativeAction::reset).diagnostic.code==NativeError::nonfinite,"A manually supplied nonfinite literal bypassed validation.");
    for(const auto& shader:{native_hlsl(invalid_literal,NativeAction::reset),native_glsl(invalid_literal,NativeAction::reset)})check(shader.find("=nload(0x7f800000u")!=shader.npos,
        "Emission treated an invalid manually supplied constant as a proved finite scalar.");
    const auto dynamic=compile(triangle+"cara(0,1,2);estado canal=.6;al_actualizar{}al_dibujar{color(canal,0,0);dibujar();}");
    for(const auto& shader:{native_hlsl(dynamic,NativeAction::frame),native_glsl(dynamic,NativeAction::frame)})check(shader.find("=nload(r0(")!=shader.npos&&shader.find("bad(diagnostic,6u,")!=shader.npos,
        "Constant geometry specialization removed a genuinely dynamic load/color guard.");
}
} //namespace

int main(){try{parser();arithmetic();clock_contract();programs();repeated_frame_setters();constant_geometry_emission();std::cout<<"PASS: "<<checks<<" exact binary32 native-profile CPU checks; source-base12 rational RNE, normals/zero domain, ordinary arithmetic and original sample programs. This does not assert F12 precision.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native real source compiler: "<<error.what()<<'\n';return 1;}}

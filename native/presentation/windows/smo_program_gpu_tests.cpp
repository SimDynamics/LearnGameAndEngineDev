#include "presentation/windows/smo_program_gpu.hpp"
#include "presentation/windows/smo_program_render.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "native_build_config.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::presentation;
std::size_t checks=0;
void check(bool condition,std::string_view reason){++checks;if(!condition)throw std::runtime_error(std::string(reason));}
template<class F>void rejects(F&& action,std::string_view reason){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}check(rejected,reason);}
std::string sample(std::string_view filename){std::ifstream file(std::filesystem::path(lgaed::build::source_root)/"native"/filename,std::ios::binary);
    check(file.is_open(),"The actual authored SMO sample is missing.");std::ostringstream text;text<<file.rdbuf();return text.str();}
void replace_once(std::string& source,std::string_view before,std::string_view after){const auto at=source.find(before);
    check(at!=source.npos,"A requested source edit did not find its real authored instruction.");source.replace(at,before.size(),after);}
std::string integer12(std::uint64_t value){constexpr std::string_view digits="0123456789#~";std::string text;
    do{text.push_back(digits[static_cast<unsigned>(value%12)]);value/=12;}while(value);std::ranges::reverse(text);return text;}
bool same(SmoF12Result a,SmoF12Result b){return a.primary==b.primary&&a.alternative==b.alternative&&a.flags==b.flags;}
SmoF12Result scalar(SmoF12 value){SmoF12Result result;result.primary=value;result.alternative=smo_f12_zero();result.alternative.diagnostic=smo_d12_domain;
    result.flags=smo_f12_valid(value)?smo_rule_has_primary:0u;return result;}
void exact(SmoF12 actual,SmoF12 expected,std::string_view reason){check(smo_f12_valid(actual)&&actual==expected,reason);}
void variable(SmoGpuProgram& program,std::string_view name,SmoF12Result expected){const auto actual=program.variableExact(name);
    if(!same(actual,expected))throw std::runtime_error("GPU state "+std::string(name)+" differs in exact F12 words, diagnostics or alternatives.");++checks;}
void variable(SmoGpuProgram& program,std::string_view name,std::string_view expected){variable(program,name,scalar(smo_f12_parse(expected)));}
std::vector<std::uint32_t> words(SmoGpuProgram& program,SmoBuffer buffer){const auto bytes=program.compute().read(buffer);
    check(bytes.size()%4==0,"GPU word buffer is truncated.");std::vector<std::uint32_t> result(bytes.size()/4);std::memcpy(result.data(),bytes.data(),bytes.size());return result;}
SmoF12 number(const std::vector<std::uint32_t>& source,std::size_t at){check(at<=source.size()&&source.size()-at>=6,"F12 read crossed its GPU buffer.");
    SmoF12 value;std::copy_n(source.begin()+static_cast<std::ptrdiff_t>(at),4,value.digits.begin());value.negative=source[at+4];value.diagnostic=source[at+5];
    check(smo_f12_valid(value),"A committed program value is not canonical F12.");return value;}
struct Snapshot{
    std::vector<SmoF12> vertices;
    std::vector<std::array<std::uint32_t,3>> faces;
    std::array<SmoF12,3> rotation{},color{};
    std::vector<std::string> names;std::vector<SmoF12Result> states;
    std::uint32_t diagnostic=0;bool draw=false;
    bool same_values(const Snapshot& other)const{
        if(vertices!=other.vertices||faces!=other.faces||rotation!=other.rotation||color!=other.color||names!=other.names||
            draw!=other.draw||states.size()!=other.states.size())return false;
        for(std::size_t i=0;i<states.size();++i)if(!same(states[i],other.states[i]))return false;return true;
    }
    bool operator==(const Snapshot& other)const{return diagnostic==other.diagnostic&&same_values(other);}
};
Snapshot capture(SmoGpuProgram& program,SmoGpuFrame frame){Snapshot result;
    check(!(frame.diagnostic&smo_d12_fatal_mask),"A failed transaction became the visible GPU frame.");
    const auto vertex_words=words(program,frame.vertices),face_words=words(program,frame.faces),frame_words=words(program,frame.frame);
    for(std::size_t i=0;i<static_cast<std::size_t>(frame.vertex_count)*3;++i)result.vertices.push_back(number(vertex_words,frame.vertex_word_offset+i*6));
    check(frame.face_word_offset<=face_words.size()&&static_cast<std::size_t>(frame.face_count)*3<=face_words.size()-frame.face_word_offset,"Face read crossed its GPU bank.");
    for(std::size_t i=0;i<frame.face_count;++i){const auto at=frame.face_word_offset+i*3;
        result.faces.push_back({face_words[at],face_words[at+1],face_words[at+2]});for(const auto index:result.faces.back())check(index<frame.vertex_count,"A committed face references an absent authored vertex.");}
    const auto start=frame.frame_word_offset;check(start<=frame_words.size()&&frame_words.size()-start>=smo_vm_frame_words,"Frame read crossed its GPU bank.");
    for(unsigned axis=0;axis<3;++axis){result.rotation[axis]=number(frame_words,start+axis*6);result.color[axis]=number(frame_words,start+18+axis*6);}
    check(frame_words[start+36]==frame.vertex_count&&frame_words[start+37]==frame.face_count&&frame_words[start+38]==static_cast<unsigned>(frame.draw_requested),"Frame metadata disagrees with its GPU bank.");
    result.diagnostic=frame_words[start+39];result.draw=frame.draw_requested;
    check(result.diagnostic==frame.diagnostic,"The host frame hid a GPU diagnostic.");
    result.names=program.ir().states;for(const auto& name:result.names)result.states.push_back(program.variableExact(name));return result;
}
Snapshot capture(SmoGpuProgram& program){return capture(program,program.current_frame());}
struct NamedSnapshot{std::string name;Snapshot value;std::optional<SmoProgramImage> image;};
SmoProgramImage checked_render(SmoProgramRenderer& renderer,SmoGpuProgram& program,const Snapshot& scene){
    const auto frame=program.current_frame();const auto actual=renderer.render(frame,true);
    check(actual.width==24&&actual.height==16&&actual.pixels.size()==384&&actual.exact_pixels.size()==384,
        "The renderer omitted full exact acceptance records.");
    check(!(actual.diagnostic&smo_d12_fatal_mask),"A fatal render diagnostic was hidden behind an image.");
    std::vector<SmoProgramRenderVector> vertices;for(std::size_t i=0;i<scene.vertices.size();i+=3)
        vertices.push_back({scene.vertices[i],scene.vertices[i+1],scene.vertices[i+2]});
    const auto expected=smo_program_render_reference(vertices,scene.faces,scene.rotation,scene.color,actual.width,actual.height,scene.draw,scene.diagnostic);
    check(actual.pixels==expected.pixels&&actual.exact_pixels==expected.exact_pixels&&actual.diagnostic==expected.diagnostic&&
        actual.colored_pixels==expected.colored_pixels&&actual.pixel_hash==expected.pixel_hash,
        "GPU rendering differs from the independent CPU-F12 oracle in exact depth, face, BGRA or diagnostics.");
    return actual;
}
std::vector<NamedSnapshot> samples_and_edits(SmoComputeApi target,const SmoComputeOptions& device){
    std::vector<NamedSnapshot> result;const auto source=sample("smo_triangle.smo");auto triangle=SmoGpuProgram::compile(source,target,device);
    std::cout<<"Program GPU api="<<triangle.info().api<<" adapter="<<std::quoted(triangle.info().adapter)<<" software="<<triangle.info().software<<"; exact F12 words, no floating tolerance.\n";
    const auto initial=capture(triangle);check(initial.vertices.size()==9&&initial.faces.size()==1&&initial.draw,"Authored triangle geometry or draw ownership was lost.");
    SmoProgramRenderer triangle_renderer(triangle.compute(),24,16);const auto initial_image=checked_render(triangle_renderer,triangle,initial);
    check(initial_image.colored_pixels>0,"The authored triangle rendered only its background.");
    exact(initial.vertices[7],smo_f12_parse("1"),"The original third vertex changed.");
    exact(initial.color[1],smo_f12_parse(".6"),"An authored radix12 color became a decimal fraction.");
    check(capture(triangle,triangle.frame())==initial,"Drawing without updating injected animation or changed authored state.");
    auto advanced=capture(triangle,triangle.step(smo_f12_parse(".3")));exact(advanced.rotation[2],smo_f12_parse(".3"),"The GPU update did not consume F12 elapsed time.");
    const auto rotated_image=checked_render(triangle_renderer,triangle,advanced);
    check(rotated_image.pixels!=initial_image.pixels,"Authored F12 rotation did not affect rendered pixels.");
    variable(triangle,"giro",".3");result.push_back({"triangle step",advanced,rotated_image});
    check(capture(triangle,triangle.step(smo_f12_zero()))==advanced,"A zero elapsed interval created motion in the authored zero-product update.");
    const auto stable_frame=triangle.current_frame();rejects([&]{(void)triangle.step(smo_f12_parse("-.1"));},"Negative elapsed time was accepted.");
    check(triangle.current_frame().revision==stable_frame.revision&&capture(triangle)==advanced,"Invalid elapsed time changed the committed transaction.");
    triangle.reset();check(capture(triangle)==initial,"GPU reset did not restore the authored initial state and geometry.");
    auto edited=source;replace_once(edited,"estado velocidad = 1;","estado velocidad = 2;");replace_once(edited,"vertice(0, 1, 0);","vertice(0, 2, 0);");
    replace_once(edited,"color(1, .6, .2);","color(.2, .8, 1);");
    replace_once(edited,"estado giro = 0;","estado giro = 0; estado diez = %; estado once = &; estado doce = $;");
    const lgaed::smo::Notation custom{'0','%','&','$'};
    auto changed=SmoGpuProgram::compile(edited,target,device,{},custom);
    const auto changed_frame=capture(changed,changed.step(smo_f12_parse(".3")));
    exact(changed_frame.vertices[7],smo_f12_parse("2"),"Editing real source vertices did not change GPU geometry.");
    exact(changed_frame.rotation[2],smo_f12_parse(".6"),"Editing actual authored velocity did not change GPU motion.");
    exact(changed_frame.color[0],smo_f12_parse(".2"),"Editing the authored color did not reach the committed GPU frame.");
    variable(changed,"diez","#");variable(changed,"once","~");variable(changed,"doce","10");
    SmoProgramRenderer changed_renderer(changed.compute(),24,16);const auto edited_image=checked_render(changed_renderer,changed,changed_frame);
    check(edited_image.colored_pixels>0&&edited_image.pixels!=rotated_image.pixels&&
        std::ranges::find(edited_image.pixels,0xff2aaaffu)!=edited_image.pixels.end(),"Source geometry/color edits did not affect real GPU pixels.");
    result.push_back({"edited triangle and notation",changed_frame,edited_image});
    auto cube=SmoGpuProgram::compile(sample("smo_cube.smo"),target,device);
    const auto cube_frame=capture(cube,cube.step(smo_f12_parse(".6")));
    check(cube_frame.vertices.size()==24&&cube_frame.faces.size()==12&&cube_frame.draw,"The actual cube was not compiled as an independent authored program.");
    exact(cube_frame.rotation[1],smo_f12_parse(".3"),"Cube rotation did not use its authored radix12 speed.");
    exact(cube_frame.rotation[0],smo_f12_parse(".16"),"Cube division did not execute in F12.");
    SmoProgramRenderer cube_renderer(cube.compute(),24,16);const auto cube_image=checked_render(cube_renderer,cube,cube_frame);
    check(cube_image.colored_pixels>0,"The authored cube did not produce rendered pixels.");
    result.push_back({"cube step",cube_frame,cube_image});return result;
}
std::string mesh(){return "vertice(-1,-1,0);vertice(1,-1,0);vertice(0,1,0);cara(0,1,2);\n";}

NamedSnapshot perspective_and_depth(SmoComputeApi target,const SmoComputeOptions& device){
    // Large far triangle, smaller near triangle, an equal-depth duplicate,
    // one behind the camera and one exactly at the camera's near origin.
    // The literal #/~ are real base12 face indices10/11, not host constants.
    const std::string source=
        "vertice(-2,-2,2);vertice(2,-2,2);vertice(0,2,2);"
        "vertice(-1,-1,0);vertice(1,-1,0);vertice(0,1,0);"
        "vertice(-2,-2,-5);vertice(2,-2,-5);vertice(0,2,-5);"
        "vertice(-2,-2,-4);vertice(2,-2,-4);vertice(0,2,-4);"
        "cara(0,1,2);cara(3,4,5);cara(3,4,5);cara(6,7,8);cara(9,#,~);"
        "al_actualizar{}al_dibujar{color(1,.6,.2);dibujar();}";
    auto program=SmoGpuProgram::compile(source,target,device);const auto scene=capture(program);
    SmoProgramRenderer renderer(program.compute(),24,16);const auto image=checked_render(renderer,program,scene);
    const auto far=std::ranges::count_if(image.exact_pixels,[](const auto& pixel){return pixel.face==0;});
    const auto near=std::ranges::count_if(image.exact_pixels,[](const auto& pixel){return pixel.face==1;});
    check(far>0&&near>0,"Perspective/depth did not retain both visible authored layers.");
    for(const auto& pixel:image.exact_pixels){
        check(pixel.face==0||pixel.face==1||pixel.face==0xffffffffu,"An equal-depth duplicate, behind-camera or near-clipped triangle won a pixel.");
        if(pixel.face==0)check(smo_f12_compare(pixel.depth,smo_f12_parse("6"))==0,"The far triangle has an incorrect F12 ray depth.");
        if(pixel.face==1)check(smo_f12_compare(pixel.depth,smo_f12_parse("4"))==0,"The nearer triangle failed depth selection.");
    }
    return {"perspective depth near clipping and tie",scene,image};
}

NamedSnapshot all_operations(SmoComputeApi target,const SmoComputeOptions& device){
    std::string source=mesh()+
        "estado suma=2+3;estado resta=2-3;estado positivo=+2;estado negativo=-2;"
        "estado producto=(-2)*(-3);estado cociente=(-6)/(-3);"
        "estado vacio=2*0;estado sin_partir=2/0;estado copia=vacio;"
        "estado otra=alternativa(vacio);estado limpia=+vacio;estado negada=-vacio;"
        "estado alfabeto=# + ~ + @;estado tiempo=0;estado tick=0;";
    for(const auto operation:{4u,5u})for(int exponent=-3;exponent<=3;++exponent){
        source+="estado p"+std::to_string(operation)+"n"+std::to_string(exponent+3)+"="+(operation==4?"pdir":"pinv")+
            std::string("(2,")+(exponent<0?"-":"")+integer12(static_cast<std::uint64_t>(exponent<0?-exponent:exponent))+");";
    }
    const std::string even_tie="1."+std::string(11,'0')+"6",odd_tie="1."+std::string(10,'0')+"16",carry=std::string(12,'~')+".6";
    source+="estado empate_par="+even_tie+";estado empate_impar="+odd_tie+";estado acarreo="+carry+";"
        "estado producto_pequeno=-0.0001971#*0.00002345;"
        "al_actualizar{tiempo=dt;tick=tick+1;}al_dibujar{rotacion(0,0,0);color(1,.6,.2);dibujar();}";
    auto program=SmoGpuProgram::compile(source,target,device);
    variable(program,"suma",scalar(smo_f12_add(smo_f12_parse("2"),smo_f12_parse("3"))));
    variable(program,"resta","-1");variable(program,"positivo","2");variable(program,"negativo","-2");
    variable(program,"producto",smo_f12_evaluate(2,smo_f12_parse("-2"),smo_f12_parse("-3")));
    variable(program,"cociente",smo_f12_evaluate(3,smo_f12_parse("-6"),smo_f12_parse("-3")));
    const auto empty=smo_f12_evaluate(2,smo_f12_parse("2"),smo_f12_zero());
    variable(program,"vacio",empty);variable(program,"copia",empty);
    variable(program,"sin_partir",smo_f12_evaluate(3,smo_f12_parse("2"),smo_f12_zero()));
    variable(program,"otra","2");variable(program,"limpia","0");variable(program,"negada","0");variable(program,"alfabeto","29");
    for(const auto operation:{4u,5u})for(int exponent=-3;exponent<=3;++exponent){const auto name="p"+std::to_string(operation)+"n"+std::to_string(exponent+3);
        const auto n=smo_f12_parse((exponent<0?"-":"")+integer12(static_cast<std::uint64_t>(exponent<0?-exponent:exponent)));
        variable(program,name,smo_f12_evaluate(operation,smo_f12_parse("2"),n));}
    variable(program,"empate_par",scalar(smo_f12_parse(even_tie)));variable(program,"empate_impar",scalar(smo_f12_parse(odd_tie)));
    variable(program,"acarreo",scalar(smo_f12_parse(carry)));
    variable(program,"producto_pequeno",scalar(smo_f12_mul(smo_f12_parse("-0.0001971#"),smo_f12_parse("0.00002345"))));
    // The per-transaction diagnostic summary can omit an initialization-only
    // INEXACT warning during draw-only work. All stored values retain theirs.
    const auto initial=capture(program);check(capture(program,program.frame()).same_values(initial),"A draw-only frame executed the update instructions.");
    (void)program.step(smo_f12_zero());variable(program,"tick","1");variable(program,"tiempo","0");
    (void)program.step_elapsed(250000000u);variable(program,"tick","2");variable(program,"tiempo",".3");
    unsigned ticks=2;
    for(const auto nanoseconds:{(std::uint64_t{1}<<32u)+123u,std::uint64_t{0xffffffffff567540ull},std::numeric_limits<std::uint64_t>::max()}){
        (void)program.step_elapsed(nanoseconds);variable(program,"tick",integer12(++ticks));
        // Exact uint64 count -> ONE F12 RNE -> F12 division. The near-maximum
        // case exposes an erroneous high*2^32 (RNE) +low (RNE) conversion.
        variable(program,"tiempo",scalar(smo_f12_div(smo_f12_parse(integer12(nanoseconds)),smo_f12_parse(integer12(1000000000u)))));
    }
    const auto stepped=capture(program);const auto revision=program.current_frame().revision;
    rejects([&]{(void)program.step(SmoD12{});},"An untagged fixed-format dt was silently interpreted as floating radix12.");
    check(program.current_frame().revision==revision&&capture(program)==stepped,"Invalid elapsed encoding changed the last committed GPU state.");
    rejects([&]{(void)program.variableExact("desconocido");},"The GPU VM fabricated an unknown state.");
    program.reset();check(capture(program)==initial,"Reset lost alternatives, raw-literal rounding or authored initial values.");
    (void)program.step(smo_f12_zero());variable(program,"tick","1");
    return {"all operations and dt",capture(program)};
}

std::vector<NamedSnapshot> transactional_diagnostics(SmoComputeApi target,const SmoComputeOptions& device){
    const std::string source="estado giro=0;\nestado otra=0;\n"+mesh()+
        "al_actualizar{\n    giro = giro + 1;\n    otra = alternativa(1);\n}\n"
        "al_dibujar{rotacion(0,0,giro);color(1,.6,.2);}\n";
    auto program=SmoGpuProgram::compile(source,target,device);const auto before=capture(program);const auto frame=program.current_frame();
    check(!before.draw,"The VM drew a figure without an authored dibujar instruction.");
    SmoProgramRenderer hidden_renderer(program.compute(),24,16);const auto hidden_image=checked_render(hidden_renderer,program,before);
    check(hidden_image.colored_pixels==0&&std::ranges::all_of(hidden_image.pixels,[](auto pixel){return pixel==0xff101820u;}),
        "An omitted authored draw instruction did not keep the renderer's blank background.");
    for(unsigned attempt=0;attempt<2;++attempt){bool rejected=false;
        try{(void)program.step(smo_f12_zero());}catch(const std::exception& error){rejected=true;
            check(std::string_view(error.what()).find("Línea 6, columna 12")!=std::string_view::npos,"A GPU numerical failure lost its original source line/column.");}
        check(rejected,"Missing alternativa silently produced a numerical value.");
        const auto unchanged=program.current_frame();
        check(unchanged.revision==frame.revision&&unchanged.vertex_word_offset==frame.vertex_word_offset&&
            unchanged.face_word_offset==frame.face_word_offset&&unchanged.frame_word_offset==frame.frame_word_offset&&capture(program)==before,
            "A failed update committed partial state, geometry, draw values or a new visible bank.");
        variable(program,"giro","0");variable(program,"otra","0");
    }
    check(capture(program,program.frame())==before,"Draw-only recovery failed after a rejected update.");program.reset();check(capture(program)==before,"Reset failed after a rejected update.");
    // A failure in the DRAW phase must roll back the preceding successful
    // update too, rather than committing state then rejecting only the image.
    const auto color_source=mesh()+"estado canal=.6;al_actualizar{canal=canal+1;}al_dibujar{color(canal,0,0);dibujar();}";
    auto color=SmoGpuProgram::compile(color_source,target,device);const auto color_before=capture(color);const auto color_revision=color.current_frame().revision;
    rejects([&]{(void)color.step(smo_f12_zero());},"Out-of-range authored color was silently clamped.");
    check(color.current_frame().revision==color_revision&&capture(color)==color_before,"A draw-phase failure partially committed its state update.");
    variable(color,"canal",".6");
    // IR exports these syntactically valid expressions. The actual GPU
    // numerical backend must reject their invalid values, not accept them as
    // proof that host parsing alone validates a program.
    for(const auto face:{"cara(-1,1,2);","cara(.6,1,2);","cara(0,1,3);"}){
        const auto invalid=std::string("vertice(0,0,0);vertice(1,0,0);vertice(0,1,0);")+face+"al_actualizar{}al_dibujar{dibujar();}";
        rejects([&]{(void)SmoGpuProgram::compile(invalid,target,device);},"The GPU accepted an invalid authored face index.");
    }
    const auto too_small="0."+std::string(1000,'0')+"1";
    rejects([&]{(void)SmoGpuProgram::compile(mesh()+"estado diminuto="+too_small+";al_actualizar{}al_dibujar{}",target,device);},
        "A grammar-valid literal outside F12 range became an exact zero.");
    return {{"failed update rollback",before,hidden_image},{"failed draw rollback",color_before}};
}

std::vector<NamedSnapshot> experimental_rule_profiles(SmoComputeApi target,const SmoComputeOptions& device){
    struct Witness{std::string_view name,expression,left,right;std::uint32_t operation;};
    const std::array witnesses{
        Witness{"producto_cero","2*0","2","0",2},Witness{"cero_producto","0*2","0","2",2},
        Witness{"signo_producto","(-2)*(-3)","-2","-3",2},Witness{"signo_cociente","(-6)/(-3)","-6","-3",3},
        Witness{"directa_cero","pdir(2,0)","2","0",4},Witness{"inversa_cero","pinv(2,0)","2","0",5},
        Witness{"directa_uno","pdir(2,1)","2","1",4},Witness{"directa_menos_uno","pdir(2,-1)","2","-1",4},
        Witness{"inversa_uno","pinv(2,1)","2","1",5},Witness{"inversa_menos_uno","pinv(2,-1)","2","-1",5}};
    std::string source;
    for(const auto& witness:witnesses)source+="estado "+std::string(witness.name)+"="+std::string(witness.expression)+";\n";
    source+="estado rama=alternativa(producto_cero);estado rama_directa=alternativa(directa_cero);estado rama_inversa=alternativa(inversa_cero);"
        "estado tick=0;estado division=0;estado rama_division=0;\n"
        "vertice(producto_cero,-1,0);vertice(1,-1,0);vertice(0,1,0);cara(0,1,2);\n"
        "al_actualizar{tick=tick+1;division=2/0;rama_division=alternativa(division);}\n"
        "al_dibujar{rotacion(directa_uno,0,tick);color(1,.6,.2);dibujar();}\n";
    const auto divide_at=source.rfind("2/0")+1;const auto line_end=source.rfind('\n',divide_at);
    const auto line=1+std::count(source.begin(),source.begin()+static_cast<std::ptrdiff_t>(divide_at),'\n');
    const auto column=line_end==source.npos?divide_at+1:divide_at-line_end;
    const auto failure_location="Línea "+std::to_string(line)+", columna "+std::to_string(column);
    const auto differs=[](SmoF12Result a,SmoF12Result b){return a.primary!=b.primary||a.alternative!=b.alternative||
        (a.flags&~static_cast<std::uint32_t>(smo_rule_experimental))!=(b.flags&~static_cast<std::uint32_t>(smo_rule_experimental));};
    std::vector<NamedSnapshot> result;unsigned changed_profiles=0;
    for(std::uint32_t profile=0;profile<64;++profile){if((profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)continue;
        auto program=SmoGpuProgram::compile(source,target,device,{}, {},profile);bool changed=false;
        for(const auto& witness:witnesses){const auto left=smo_f12_parse(witness.left),right=smo_f12_parse(witness.right);
            const auto expected=smo_f12_evaluate(witness.operation,left,right,profile);variable(program,witness.name,expected);
            changed=differs(expected,smo_f12_evaluate(witness.operation,left,right,0))||changed;}
        const auto zero_product=smo_f12_evaluate(2,smo_f12_parse("2"),smo_f12_zero(),profile);
        variable(program,"rama",scalar(zero_product.alternative));
        variable(program,"rama_directa",scalar(smo_f12_evaluate(4,smo_f12_parse("2"),smo_f12_zero(),profile).alternative));
        variable(program,"rama_inversa",scalar(smo_f12_evaluate(5,smo_f12_parse("2"),smo_f12_zero(),profile).alternative));
        const auto initial=capture(program);exact(initial.vertices[0],zero_product.primary,"The selected experimental zero policy did not reach authored geometry.");
        exact(initial.rotation[0],smo_f12_evaluate(4,smo_f12_parse("2"),smo_f12_parse("1"),profile).primary,
            "The selected unit-exponent policy did not reach authored draw values.");
        const auto division=smo_f12_evaluate(3,smo_f12_parse("2"),smo_f12_zero(),profile);
        changed=differs(division,smo_f12_evaluate(3,smo_f12_parse("2"),smo_f12_zero(),0))||changed;
        if(division.flags&smo_rule_has_primary){(void)program.step(smo_f12_zero());variable(program,"division",division);variable(program,"rama_division",scalar(division.alternative));
            variable(program,"tick",smo_f12_evaluate(0,smo_f12_zero(),smo_f12_parse("1"),profile));
        }else{
            // A deliberately unresolved division has NO numeric value. It
            // rejects the transaction and preserves the previous GPU bank;
            // it is not a zero result or proof of equivalent faster work.
            check((division.flags&smo_rule_unresolved)!=0&&(division.primary.diagnostic&smo_d12_unresolved)!=0,
                "The CPU rule oracle fabricated an evaluated unresolved division.");
            const auto old_frame=program.current_frame();bool rejected=false;
            try{(void)program.step(smo_f12_zero());}catch(const std::exception& error){rejected=true;
                check(std::string_view(error.what()).find(failure_location)!=std::string_view::npos,"An unresolved experimental division lost its source location.");}
            check(rejected&&program.current_frame().revision==old_frame.revision&&capture(program)==initial,
                "An unresolved profile committed its preceding tentative update or invented an answer.");
            variable(program,"division","0");variable(program,"tick","0");
        }
        check(changed==(profile!=0),"A semantic-profile classification confused an experimental flag with a changed numerical result.");
        changed_profiles+=changed;std::cout<<"Program rule profile="<<profile<<" classification="<<(changed?"DIFFERENT semantic result":"DEFAULT semantic result")
            <<" division="<<((division.flags&smo_rule_has_primary)?smo_f12_format(division.primary):"UNRESOLVED")
            <<"; exact CPU-F12 policy agreement, no speed comparison.\n";
        result.push_back({"experimental policy "+std::to_string(profile),capture(program)});
    }
    check(result.size()==48&&changed_profiles==47,"The program policy matrix omitted an authorized profile or semantic distinction.");
    for(const auto profile:{12u,64u,128u})rejects([&]{(void)SmoGpuProgram::compile(source,target,device,{}, {},profile);},
        "The experimental program accepted an undefined/conflicting rule profile.");
    return result;
}
std::vector<NamedSnapshot> test_api(SmoComputeApi target,const SmoComputeOptions& device){auto result=samples_and_edits(target,device);
    result.push_back(all_operations(target,device));result.push_back(perspective_and_depth(target,device));auto failures=transactional_diagnostics(target,device);
    result.insert(result.end(),std::make_move_iterator(failures.begin()),std::make_move_iterator(failures.end()));auto profiles=experimental_rule_profiles(target,device);
    result.insert(result.end(),std::make_move_iterator(profiles.begin()),std::make_move_iterator(profiles.end()));return result;
}

} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;
    const bool hardware=argc>=2&&std::string_view(argv[1])=="hardware";
    check(argc==1||(hardware&&argc<=3),"Usage: smo_program_gpu_tests [hardware [d3d11|d3d12|vulkan|opengl|all]]");
    const std::string_view selector=argc==3?argv[2]:hardware?"all":"d3d11";
    const bool all=selector=="all";std::optional<std::vector<NamedSnapshot>> baseline;std::string adapter;
    for(const auto target:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){
        if(!all&&target!=smo_compute_api(selector))continue;SmoComputeOptions options;options.software=!hardware;options.adapter_name=adapter;
        std::cout<<"BEGIN authored F12 GPU program "<<smo_compute_api_name(target)<<'\n';
        // Device selection follows the production factory. Hardware runs never
        // claim WARP correctness as physical-GPU performance evidence.
        if(hardware&&adapter.empty()){auto probe=create_smo_compute(target,options);check(!probe->info().software,"The requested hardware test fell back to software.");adapter=probe->info().adapter;options.adapter_name=adapter;}
        auto snapshots=test_api(target,options);
        if(baseline){check(snapshots.size()==baseline->size(),"Cross-API tests produced different program coverage.");
            for(std::size_t i=0;i<snapshots.size();++i){const auto& left=snapshots[i];const auto& right=(*baseline)[i];
                check(left.name==right.name&&left.value==right.value&&left.image.has_value()==right.image.has_value(),
                    "The same authored program produced different exact F12 output words across APIs.");
                if(left.image){const auto& a=*left.image;const auto& b=*right.image;
                    check(a.width==b.width&&a.height==b.height&&a.diagnostic==b.diagnostic&&a.colored_pixels==b.colored_pixels&&
                        a.pixel_hash==b.pixel_hash&&a.pixels==b.pixels&&a.exact_pixels==b.exact_pixels,
                        "The same authored program rendered different exact F12 depths, faces or BGRA pixels across APIs.");}}
            std::cout<<"Cross-API authored programs: exact words match, tolerance=NONE.\n";
        }else baseline=std::move(snapshots);
        std::cout<<"PASS authored F12 GPU program "<<smo_compute_api_name(target)<<'\n';
    }
    check(baseline.has_value(),"No requested GPU API was executed.");
    std::cout<<"PASS: "<<checks<<" authored F12 GPU program checks; actual source/geometry/state, exact numeric results and transactional diagnostics. No performance claim.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL authored F12 GPU program: "<<error.what()<<'\n';return 1;}}

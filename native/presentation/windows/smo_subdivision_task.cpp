#include "presentation/windows/smo_subdivision_task.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

namespace lgaed::presentation { namespace {
using U=std::uint32_t;using I=std::int64_t;using Clock=std::chrono::steady_clock;
double ms(Clock::time_point begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
void require(bool value,std::string_view why){if(!value)throw std::runtime_error(std::string(why));}
void stopped(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Subdivisión SMO cancelada.");}
// Independent exact bounded rational oracle. Never implemented through F12
// or binary floating point. Bounds are checked before every product.
struct Rat {I n=0,d=1;Rat(I a=0,I b=1){require(b>0&&a>=0&&a<=1000000000ll&&b<=1000000000ll,"Racional fuera del dominio exacto de subdivisión.");const auto g=std::gcd(a,b);n=a/g;d=b/g;}};
I mul(I a,I b){require(a>=0&&b>=0&&(a==0||b<=1000000000ll/a),"El programa excede el rango entero demostrado.");return a*b;}
Rat add(Rat a,Rat b){const auto g=std::gcd(a.d,b.d);return {mul(a.n,b.d/g)+mul(b.n,a.d/g),mul(a.d,b.d/g)};}
Rat sub(Rat a,Rat b){const auto g=std::gcd(a.d,b.d);return {mul(a.n,b.d/g)-mul(b.n,a.d/g),mul(a.d,b.d/g)};}
Rat product(Rat a,Rat b){const auto g1=std::gcd(a.n,b.d),g2=std::gcd(b.n,a.d);return {mul(a.n/g1,b.n/g2),mul(a.d/g2,b.d/g1)};}
Rat quotient(Rat a,Rat b){require(b.n!=0,"El control exacto no sustituye la división SMO por cero.");return product(a,{b.d,b.n});}
Rat literal(std::string_view text){if(text=="@")return {12};I n=0,d=1;bool fraction=false;for(const char c:text){if(c=='.'){require(!fraction,"Literal inválido.");fraction=true;continue;}const I digit=c=='#'?10:c=='~'?11:c>='0'&&c<='9'?c-'0':-1;require(digit>=0,"Literal fuera del subconjunto exacto.");n=mul(n,12)+digit;if(fraction)d=mul(d,12);}return {n,d};}
SmoF12 packet(Rat value){std::string text;I whole=value.n/value.d,remainder=value.n%value.d;const auto digit=[](I n){return n<10?static_cast<char>('0'+n):n==10?'#':'~';};do{text.insert(text.begin(),digit(whole%12));whole/=12;}while(whole);if(remainder){text+='.';for(unsigned i=0;i<12u&&remainder;++i){remainder*=12;text+=digit(remainder/value.d);remainder%=value.d;}}require(remainder==0,"La fracción no termina en doce cifras base12.");const auto result=smo_f12_parse(text);require(result.diagnostic==0,"La referencia racional perdió precisión F12.");return result;}
struct Affine {std::array<Rat,3> a{};};
bool constant(const Affine& a){return a.a[0].n==0&&a.a[1].n==0;}
Affine combine(Affine a,Affine b,U opcode){Affine r;
    if(opcode==5u||opcode==6u)for(unsigned i=0;i<3u;++i)r.a[i]=opcode==5u?add(a.a[i],b.a[i]):sub(a.a[i],b.a[i]);
    else if(opcode==7u){require(constant(a)||constant(b),"El control exige un programa afín, no productos de estados.");if(constant(a))std::swap(a,b);for(unsigned i=0;i<3u;++i)r.a[i]=product(a.a[i],b.a[2]);}
    else{require(constant(b),"El control exige divisores constantes positivos.");for(unsigned i=0;i<3u;++i)r.a[i]=quotient(a.a[i],b.a[2]);}return r;
}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& values){return std::as_bytes(std::span(values));}
std::uint64_t hash(std::span<const std::byte> raw){std::uint64_t h=14695981039346656037ull;for(const auto b:raw){h^=std::to_integer<unsigned char>(b);h*=1099511628211ull;}return h;}
std::string source_file(std::string_view file){const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/file,512u*1024u);std::string text;for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;text+=static_cast<char>(raw[i]);}return text;}
template<class T>std::vector<T> read(SmoCompute& gpu,SmoBuffer buffer,std::size_t count){const auto raw=gpu.read(buffer);require(raw.size()==count*sizeof(T),"Salida GPU de subdivisión truncada.");std::vector<T> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;}
double median(std::vector<double> v){if(v.empty()||std::ranges::any_of(v,[](double n){return !std::isfinite(n)||n<=0;}))return std::numeric_limits<double>::quiet_NaN();std::ranges::sort(v);return v[v.size()/2u];}
std::array<Rat,2> rational_step(const SmoSubdivisionProgram& p,std::array<Rat,2> states){std::vector<Rat> stack;for(const auto& op:p.ir.update){const auto code=static_cast<U>(op.opcode);if(code==0u)stack.push_back(literal(p.ir.literals.at(op.operand)));else if(code==1u)stack.push_back(states.at(op.operand));else if(code==15u){states.at(op.operand)=stack.back();stack.pop_back();}else{const auto b=stack.back();stack.pop_back();const auto a=stack.back();stack.pop_back();const auto value=code==5u?add(a,b):code==6u?sub(a,b):code==7u?product(a,b):quotient(a,b);
        // A cancelled final expression cannot conceal a rounded intermediate
        // from the affine control. Prove every evaluated scalar is exactly
        // representable before admitting this task to the timed experiment.
        (void)packet(value);stack.push_back(value);}}return states;}
} //namespace

std::string smo_subdivision_source(){return
    "// Base12: .6=mitad, .4=tercio, .2=sexto; 10=doce.\n"
    "// El laboratorio aporta x/y iniciales en una cuadrícula de objetos.\n"
    "estado x = 0;\nestado y = 0;\n"
    "al_actualizar {\n    x = (x * .6 + .4) * 10;\n    x = x / 10;\n"
    "    y = (y * .4 + .2) * 10;\n    y = y / 10;\n}\n"
    "al_dibujar {}\n";}
SmoSubdivisionProgram compile_smo_subdivision(std::string_view text){SmoSubdivisionProgram p;p.source=text;p.ir=smo::compile_ir(text,{},false);
    require(p.ir.states==std::vector<std::string>{"x","y"}&&p.ir.draw.empty()&&p.ir.max_stack<=8u&&!p.ir.update.empty(),"Subdivisión: declara sólo x/y y una actualización afín sin dibujo ni alternativas.");
    require(p.ir.initial.size()==4u,"Los estados x/y iniciales deben ser cero, reemplazados explícitamente por el lote del laboratorio.");for(unsigned i=0;i<2u;++i){const auto& value=p.ir.initial[i*2u];const auto& store=p.ir.initial[i*2u+1u];require(value.opcode==smo::IrOpcode::literal&&literal(p.ir.literals.at(value.operand)).n==0&&store.opcode==smo::IrOpcode::state&&store.operand==i,"La tarea sólo admite inicializadores cero.");}
    for(const auto& l:p.ir.literals)p.literals.push_back(packet(literal(l)));
    std::array<Affine,2> states{};states[0].a[0]=Rat(1);states[1].a[1]=Rat(1);std::vector<Affine> stack;
    for(const auto& op:p.ir.update){const auto code=static_cast<U>(op.opcode);require(code==0u||code==1u||code==5u||code==6u||code==7u||code==8u||code==15u,"Operación fuera del subconjunto afín exacto; no se sustituye silenciosamente.");
        p.operations.push_back(code);p.operations.push_back(op.operand);
        if(code==0u){Affine c;c.a[2]=literal(p.ir.literals.at(op.operand));stack.push_back(c);}else if(code==1u)stack.push_back(states.at(op.operand));else if(code==15u){require(!stack.empty(),"Pila incompleta.");states.at(op.operand)=stack.back();stack.pop_back();}else{require(stack.size()>=2u,"Pila incompleta.");const auto b=stack.back();stack.pop_back();const auto a=stack.back();stack.pop_back();stack.push_back(combine(a,b,code));}}
    require(stack.empty(),"El programa dejó operandos sin consumir.");
    for(unsigned row=0;row<2u;++row){I denominator=1;for(const auto& v:states[row].a){denominator=mul(denominator/std::gcd(denominator,v.d),v.d);require(denominator<=1000000,"Denominador fuera del control exacto.");}for(unsigned col=0;col<3u;++col){const auto n=mul(states[row].a[col].n,denominator/states[row].a[col].d);p.affine[row][col]=static_cast<U>(n);}p.affine[row][3]=static_cast<U>(denominator);}
    return p;
}
SmoKernelSource smo_subdivision_kernel(unsigned stage){require(stage<smo_subdivision_stages,"Etapa de subdivisión inexistente.");const auto hlsl=source_file("smo_shader_hlsl.inc"),glsl=source_file("smo_shader_glsl.inc"),math=source_file("smo_float12.inc"),body=source_file("smo_subdivision_task.inc");const auto define="#define SMO_SUBDIVISION_STAGE "+std::to_string(stage)+"\n";SmoKernelSource k;k.name="subdivision_stage"+std::to_string(stage);k.hlsl=define+hlsl+math+body;k.glsl="#version 430\n"+define+glsl+math+body;
#ifdef LGAED_SMO_SUBDIVISION_SHADER_ROOT
    const auto root=std::filesystem::path(LGAED_SMO_SUBDIVISION_SHADER_ROOT);const auto raw_manifest=state::read_bytes(root/"source.sha256",256);std::string manifest(raw_manifest.begin(),raw_manifest.end());while(!manifest.empty()&&(manifest.back()=='\r'||manifest.back()=='\n'||manifest.back()==' '))manifest.pop_back();require(manifest==state::sha256(glsl+math+body),"Recompila los shaders actuales de subdivisión.");const auto raw=state::read_bytes(root/"shaders"/(k.name+".spv"),8u*1024u*1024u);require(raw.size()>=20u&&raw.size()%4u==0u,"SPIR-V de subdivisión truncado.");k.spirv.resize(raw.size()/4u);std::memcpy(k.spirv.data(),raw.data(),raw.size());for(std::size_t at=5u;at<k.spirv.size();){const auto words=k.spirv[at]>>16u,opcode=k.spirv[at]&0xffffu;require(words&&words<=k.spirv.size()-at&&opcode!=22u,"El proceso F12 no debe introducir aritmética flotante.");at+=words;}
#endif
    std::filesystem::path dxbc;
#ifdef LGAED_SMO_SUBDIVISION_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_SUBDIVISION_HLSL_SHADER_ROOT;
#endif
    load_smo_dxbc_assets(k,dxbc,hlsl+math+body,true);return k;
}
SmoSubdivisionReport run_smo_subdivision_task(const SmoSubdivisionOptions& o,std::stop_token stop,std::function<void(std::string_view)> progress){
    require(o.items>0u&&o.items<=65536u&&o.steps>0u&&o.steps<=6u&&o.rounds>0u&&o.rounds<=100u&&o.warmups<=10u,"Opciones de subdivisión fuera del dominio verificado.");stopped(stop);SmoSubdivisionReport r;r.source=o.source.empty()?smo_subdivision_source():o.source;r.items=o.items;r.steps=o.steps;r.grid_scale=1;for(U i=0;i<o.steps;++i)r.grid_scale*=12u;
    r.contract="Primarios F12 exactos y diagnósticos cero; x/y iniciales enteros 0..31, 1..6 subdivisiones, cuadrícula 12^pasos. Control entero afín con la misma precisión exacta y sin errores ocultos. No equivale al dominio F12 completo ni a sus alternativas observables. Imagen de puntos calculada por GPU; tiempo completo incluye descarga de píxeles, no composición del escritorio.";
    const auto preparing=Clock::now(),compiling=Clock::now();const auto p=compile_smo_subdivision(r.source);const auto canonical=compile_smo_subdivision(smo_subdivision_source());
    // A fixed shader is a valid lowering only for its entire parsed program,
    // including every literal and its operand index. Same affine coefficients
    // alone cannot justify erasing different intermediate F12 rounding points.
    r.direct_f12=p.operations==canonical.operations&&p.literals==canonical.literals;
    r.contract+=r.direct_f12?" Cuarta variante: bajada F12 directa del flujo completo validado, conservando el orden y cada punto de redondeo; elimina sólo la máquina de instrucciones.":" Cuarta variante: retorno explícito a la máquina F12 especializada porque el programa editado no coincide con la bajada directa verificada.";
    std::array<U,4> direct_literals{};
    if(r.direct_f12){const std::array<std::string_view,4> tokens{".6",".4",".2","10"};for(unsigned i=0;i<tokens.size();++i){const auto value=smo_f12_parse(tokens[i]);const auto found=std::ranges::find(p.literals,value);require(found!=p.literals.end(),"La bajada F12 directa perdió un literal del programa validado.");direct_literals[i]=static_cast<U>(found-p.literals.begin());}}
    r.source_compile_ms=ms(compiling);r.operation_count=static_cast<U>(p.operations.size()/2u);
    const auto validating=Clock::now();std::vector<U> inputs;inputs.reserve(o.items*2u);std::vector<SmoF12> expected;expected.reserve(o.items*2u);std::vector<U> grid;grid.reserve(o.items*3u);std::vector<U> image(r.width*r.height,0xff1e1e1eu);
    std::array<std::array<Rat,2>,1024> reference{};std::array<std::array<SmoF12,2>,1024> packets{};
    for(U i=0;i<1024u&&i<o.items;++i){std::array<Rat,2> state{{Rat(i%32u),Rat(i/32u)}};for(U step=0;step<o.steps;++step){state=rational_step(p,state);for(const auto& value:state)require(value.n<=mul(32,value.d),"El programa salió del dominio de coordenadas 0..32.");}reference[i]=state;packets[i]={packet(state[0]),packet(state[1])};}
    for(U i=0;i<o.items;++i){const U seed=i%1024u;inputs.push_back(seed%32u);inputs.push_back(seed/32u);for(unsigned axis=0;axis<2u;++axis){expected.push_back(packets[seed][axis]);const auto value=reference[seed][axis];const auto n=mul(value.n,r.grid_scale);require(n%value.d==0,"El programa no conserva la cuadrícula exacta de esta tarea.");grid.push_back(static_cast<U>(n/value.d));}grid.push_back(0u);const auto px=(grid[i*3u]*8u/r.grid_scale)%r.width,py=(grid[i*3u+1u]*8u/r.grid_scale)%r.height;image[py*r.width+px]=0xff70d58au;}
    // The integer control's intermediate products must fit before submitting
    // its specialized affine kernel. A domain failure is never timed as a win.
    for(const auto& row:p.affine){const auto worst=mul(mul(32,r.grid_scale),row[0])+mul(mul(32,r.grid_scale),row[1])+mul(r.grid_scale,row[2]);require(worst<=1000000000ll,"El control entero excede su rango demostrado.");}
    r.validation_ms=ms(validating);if(progress)progress("Preparando programa SMO y subdivisión exacta en GPU");auto device=o.device;const auto observer=device.preparation;
    device.preparation=[&r,observer](const SmoComputePreparationEvent& event){r.preparation_events.push_back(event);if(observer)observer(event);};
    auto gpu=create_smo_compute(o.api,device,stop);r.device=gpu->info();const auto shaders_preparing=Clock::now();std::array<SmoKernel,smo_subdivision_stages> kernels{};
    for(unsigned stage=0;stage<kernels.size();++stage){if(stage==6u&&!r.direct_f12)continue;stopped(stop);const auto assets_begin=Clock::now();const auto source=smo_subdivision_kernel(stage);r.shader_asset_ms[stage]=ms(assets_begin);const auto creation_begin=Clock::now();kernels[stage]=gpu->kernel(source);r.shader_creation_ms[stage]=ms(creation_begin);}r.shader_prepare_ms=ms(shaders_preparing);
    const auto uploading=Clock::now();auto operations=p.operations;for(const auto& row:p.affine)operations.insert(operations.end(),row.begin(),row.end());const auto ib=gpu->buffer(inputs.size()*4u,bytes(inputs)),opb=gpu->buffer(operations.size()*4u,bytes(operations)),lb=gpu->buffer(p.literals.size()*sizeof(SmoF12),bytes(p.literals));
    std::array<SmoBuffer,smo_subdivision_variants> out{},points{},images{};std::array<SmoDispatch,smo_subdivision_variants> tasks{},conversions{},clears{};r.gpu_buffer_bytes=inputs.size()*4u+operations.size()*4u+p.literals.size()*sizeof(SmoF12);
    for(U v=0;v<smo_subdivision_variants;++v){const auto state_size=o.items*(v==2u?12u:48u);out[v]=gpu->buffer(state_size);points[v]=gpu->buffer(o.items*12u);images[v]=gpu->buffer(r.width*r.height*4u);r.gpu_buffer_bytes+=state_size+o.items*12u+r.width*r.height*4u;
        std::array<SmoBuffer,8> bindings{ib,v!=2u?out[v]:0u,opb,v==2u?out[v]:0u,lb,points[v],0,images[v]};std::array<U,16> params{o.items,o.steps,r.operation_count,r.grid_scale,r.width,r.height,direct_literals[0],direct_literals[1],direct_literals[2],direct_literals[3]};const U task_stage=v<3u?v:r.direct_f12?6u:1u;tasks[v]={kernels[task_stage],bindings,params,{(o.items+63u)/64u,1,1}};conversions[v]={kernels[v==2u?5u:4u],bindings,params,{(o.items+63u)/64u,1,1}};clears[v]={kernels[3],bindings,params,{(r.width*r.height+63u)/64u,1,1}};}
    r.upload_ms=ms(uploading);r.preparation_ms=ms(preparing);
    const auto verify=[&](U v,const std::vector<U>& actual_image){const auto begin=Clock::now();if(v!=2u){const auto state=read<SmoF12>(*gpu,out[v],o.items*2u);require(state==expected,"Los primarios/diagnósticos F12 difieren de la referencia racional independiente.");r.variants[v].state_hash=hash(bytes(state));r.verified_values+=state.size()*6u;}else{const auto state=read<U>(*gpu,out[v],o.items*3u);require(state==grid,"El control de cuadrícula perdió exactitud o generó un diagnóstico.");r.variants[v].state_hash=hash(bytes(state));r.verified_values+=state.size();}const auto actual_points=read<U>(*gpu,points[v],o.items*3u);
        for(std::size_t word=0;word<grid.size();++word)if(actual_points[word]!=grid[word]){std::ostringstream error;error<<"La conversión al renderizador cambió coordenadas/diagnósticos: variante="<<v<<" objeto="<<word/3u<<" campo="<<word%3u<<" actual="<<actual_points[word]<<" esperado="<<grid[word]<<" diagnóstico="<<actual_points[(word/3u)*3u+2u]<<" escala="<<r.grid_scale;throw std::runtime_error(error.str());}
        require(actual_image==image,"El fotograma GPU difiere de la referencia exacta.");r.variants[v].point_hash=hash(bytes(actual_points));r.variants[v].image_hash=hash(bytes(actual_image));r.verified_values+=actual_points.size()+actual_image.size();r.validation_ms+=ms(begin);};
    for(U warm=0;warm<=o.warmups;++warm){const auto phase=Clock::now();for(U v=0;v<smo_subdivision_variants;++v){stopped(stop);const std::array<SmoDispatch,3> frame{clears[v],tasks[v],conversions[v]};(void)gpu->execute(frame,false);if(warm==0u){const auto pixels=read<U>(*gpu,images[v],r.width*r.height);verify(v,pixels);}}if(warm==0u)r.preflight_ms=ms(phase);else r.warmup_ms+=ms(phase);}
    constexpr std::array<std::array<U,4>,8> orders{{{0,1,2,3},{3,2,1,0},{1,2,3,0},{0,3,2,1},{2,3,0,1},{1,0,3,2},{3,0,1,2},{2,1,0,3}}};
    for(U round=0;round<o.rounds;++round){if(progress)progress("Comparando máquina F12, especialización directa y control exacto");for(U n=0;n<smo_subdivision_variants;++n){stopped(stop);const U v=orders[round%orders.size()][n];auto& timing=r.variants[v];timing.task_gpu_ms.push_back(gpu->execute(std::span(&tasks[v],1),true));(void)gpu->execute(std::span(&clears[v],1),false);timing.conversion_gpu_ms.push_back(gpu->execute(std::span(&conversions[v],1),true));const std::array<SmoDispatch,3> frame{clears[v],tasks[v],conversions[v]};const auto begin=Clock::now();timing.frame_gpu_ms.push_back(gpu->execute(frame,true));const auto read_begin=Clock::now();const auto pixels=read<U>(*gpu,images[v],r.width*r.height);r.readback_ms+=ms(read_begin);timing.frame_wall_ms.push_back(ms(begin));verify(v,pixels);if(v==3u)r.preview_bgra=pixels;}}
    for(auto& timing:r.variants){timing.task_median_ms=median(timing.task_gpu_ms);timing.conversion_median_ms=median(timing.conversion_gpu_ms);timing.frame_median_ms=median(timing.frame_gpu_ms);timing.wall_median_ms=median(timing.frame_wall_ms);timing.timing_valid=std::isfinite(timing.task_median_ms)&&std::isfinite(timing.conversion_median_ms)&&std::isfinite(timing.frame_median_ms)&&std::isfinite(timing.wall_median_ms);}
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters)))r.process_peak_working_set=counters.PeakWorkingSetSize;r.equivalent=true;return r;
}
} //namespace lgaed::presentation

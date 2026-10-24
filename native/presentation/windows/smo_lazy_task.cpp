#include "presentation/windows/smo_lazy_task.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "core/smo_native.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <windows.h>
#include <psapi.h>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using U=std::uint32_t;using Clock=std::chrono::steady_clock;
using Item=std::array<U,6>;using Observation=std::array<U,8>;
void require(bool value,std::string_view text){if(!value)throw std::runtime_error(std::string(text));}
void stopped(std::stop_token stop){require(!stop.stop_requested(),"La tarea de canales SMO fue cancelada.");}
double elapsed(Clock::time_point begin){return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();}
U bits(float v){return std::bit_cast<U>(v);}float real(U v){return std::bit_cast<float>(v);}
bool valid(float v){const auto b=bits(v);return (b&0x7f800000u)!=0x7f800000u&&((b&0x7f800000u)!=0u||(b&0x7fffffu)==0u);}
float add(float a,float b){volatile float result=a+b;return result;}
float mul(float a,float b){volatile float result=a*b;return a<0&&b<0?-result:result;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& v){return std::as_bytes(std::span(v));}
template<class T>std::vector<T> read(SmoCompute& gpu,SmoBuffer b,std::size_t n){const auto raw=gpu.read(b);require(raw.size()==n*sizeof(T),"La tarea de canales devolvió una lectura truncada.");std::vector<T> result(n);std::memcpy(result.data(),raw.data(),raw.size());return result;}
std::uint64_t hash(std::span<const std::byte> data){std::uint64_t h=14695981039346656037ull;for(const auto c:data){h^=std::to_integer<unsigned char>(c);h*=1099511628211ull;}return h;}
std::string source_file(std::string_view name){const auto data=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512u*1024u);std::string text;for(std::size_t i=0;i<data.size();++i){if(data[i]=='\r'&&i+1<data.size()&&data[i+1]=='\n')continue;text+=static_cast<char>(data[i]);}return text;}
struct Compiled {smo::ProgramIr ir;std::vector<U> instructions,literals;Item initial{};U begin=0,end=0;};
Compiled compile(std::string_view source){Compiled c;c.ir=smo::compile_ir(source,{},false);
    require(c.ir.states==std::vector<std::string>{"activo","posicion","velocidad","compartido","requerido","opcional"},"El contrato de canales exige sus seis estados explícitos, en el orden declarado.");
    require(c.ir.draw.empty()&&c.ir.vertices==0&&c.ir.faces==0,"El canal diferido no puede ocultar efectos de dibujo/geometría.");
    require(c.ir.max_stack<=64,"La expresión supera la pila acotada del ensayo de canales.");
    for(const auto& literal:c.ir.literals)c.literals.push_back(smo::native_parse_binary32(literal));
    require(c.ir.initial.size()==12,"Los estados del ensayo requieren inicializadores literales, sin evaluación CPU.");
    for(U i=0;i<6;++i){const auto& value=c.ir.initial[i*2];const auto& store=c.ir.initial[i*2+1];
        require(value.opcode==smo::IrOpcode::literal&&store.opcode==smo::IrOpcode::state&&store.operand==i,"El ensayo sólo acepta un literal por inicializador.");c.initial[i]=c.literals.at(value.operand);}
    U statement_begin=0,depth=0;bool guarded=false;
    for(U pc=0;pc<c.ir.update.size();++pc){const auto& ins=c.ir.update[pc];const U op=static_cast<U>(ins.opcode);
        require(!guarded,"Una salida posterior podría observar el canal omitido; esta versión exige que sea la última asignación.");
        if(op<=2u){++depth;require(depth<=64,"Pila de canal fuera de rango.");}
        else if(op==5u||op==6u||op==7u){require(depth>=2,"Expresión de canal inválida.");--depth;}
        else if(op==15u){require(depth==1&&ins.operand!=0u&&ins.operand!=2u,"La puerta/velocidad no pueden cambiar en este contrato.");depth=0;
            if(ins.operand==5u){c.begin=statement_begin;c.end=pc+1;guarded=true;}statement_begin=pc+1;}
        else throw std::runtime_error("La tarea diferida admite sólo estados, dt, +, -, * y asignaciones. No descarta alternativas ni operaciones no soportadas.");
        c.instructions.insert(c.instructions.end(),{op,ins.operand,ins.line,ins.column});
    }
    require(guarded&&depth==0,"Falta la asignación diferida final a opcional.");return c;
}
bool same_program(const Compiled& a,const Compiled& b){
    if(a.initial!=b.initial||a.literals!=b.literals||a.ir.update.size()!=b.ir.update.size())return false;
    for(std::size_t i=0;i<a.ir.update.size();++i)if(a.ir.update[i].opcode!=b.ir.update[i].opcode||a.ir.update[i].operand!=b.ir.update[i].operand)return false;return true;
}
struct Expected {std::vector<Item> states;std::vector<Observation> output;};
Expected reference(std::span<const Item> previous,std::span<const Observation> prior,const Compiled& c,U frame){Expected e;e.states.assign(previous.begin(),previous.end());e.output.resize(previous.size());
    for(std::size_t i=0;i<previous.size();++i){const auto& old=previous[i];auto& next=e.states[i];auto& o=e.output[i];const bool active=real(old[0])!=0;
        U operations=frame?prior[i][4]:0,writes=frame?prior[i][5]:0,version=frame?prior[i][2]:0;
        const auto delta=mul(real(old[2]),.0625f),shared=add(real(old[1]),delta),required=add(mul(shared,.5f),.25f);
        U diagnostic=(!valid(real(old[0]))||!valid(real(old[1]))||!valid(real(old[2]))||!valid(delta)||!valid(shared)||!valid(required))?1u:0u;
        float value=real(old[5]);
        if(!diagnostic&&active)for(U stage=0;stage<16;++stage){const auto a=mul(value,.5f),b=mul(shared,.5f);value=add(a,b);if(!valid(a)||!valid(b)||!valid(value))diagnostic=1;}
        operations+=c.begin+(active?c.end-c.begin:0);
        if(!diagnostic){next[1]=bits(shared);next[3]=bits(shared);next[4]=bits(required);if(active){next[5]=bits(value);++version;}writes+=active?4u:3u;}
        o={bits(required),bits(value),version,diagnostic,operations,writes,bits(shared),old[0]};
        if(!active)o[1]=old[5];
    }return e;
}
double median(std::vector<double> v){if(v.empty())return 0;std::ranges::sort(v);const auto n=v.size();return n%2?v[n/2]:(v[n/2-1]+v[n/2])*.5;}
std::vector<Item> inputs(const Compiled& c,U count,U percent,bool grouped){std::vector<Item> result(count,c.initial);const U active=static_cast<U>(static_cast<std::uint64_t>(count)*percent/100u);
    for(U i=0;i<count;++i){const bool on=grouped?i<active:(static_cast<std::uint64_t>(i+1)*active/count!=static_cast<std::uint64_t>(i)*active/count);
        result[i][0]=bits(on?1.f:0.f);result[i][1]=bits(.25f+static_cast<float>(i%64u)/256.f);result[i][2]=bits(1.f/64.f);result[i][5]=bits(static_cast<float>(i%32u)/64.f);}
    return result;
}
void verify(std::span<const Item> states,std::span<const Observation> result,const Expected& expected,const Compiled& c,U frame,U mode,std::uint64_t& checked){
    require(states.size()==expected.states.size()&&result.size()==expected.output.size(),"El canal SMO perdió elementos.");
    for(std::size_t i=0;i<states.size();++i){require(states[i]==expected.states[i],"Estado/trajectoria de canales GPU distinto al oráculo exacto.");const auto& o=result[i];const auto& e=expected.output[i];
        for(U part:{0u,1u,2u,3u,5u,6u,7u})require(o[part]==e[part],"La omisión perdió resultados, diagnóstico, destino o contador de escrituras.");
        require(o[4]==(mode==0u?(frame+1u)*c.end:e[4]),"La instrumentación no acredita las instrucciones realmente evitadas.");checked+=14;
    }
}
std::vector<U> project(std::span<const Item> states,U width,U height){std::vector<U> pixels(static_cast<std::size_t>(width)*height,0xff1d1d1du);
    for(const auto& item:states){const U x=static_cast<U>(std::clamp(mul(real(item[1]),static_cast<float>(width-1)),0.f,static_cast<float>(width-1)));
        const U y=height-1-static_cast<U>(std::clamp(mul(real(item[5]),static_cast<float>(height-1)),0.f,static_cast<float>(height-1)));
        const U color=real(item[0])==0.f?0xff707070u:0xff4bd5e5u;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const int px=static_cast<int>(x)+dx,py=static_cast<int>(y)+dy;if(px>=0&&py>=0&&px<static_cast<int>(width)&&py<static_cast<int>(height)){auto& pixel=pixels[static_cast<std::size_t>(py)*width+px];pixel=std::max(pixel,color);}}
    }return pixels;
}
} //namespace

std::string smo_lazy_task_source(){std::string expression="opcional";for(unsigned i=0;i<16;++i)expression="("+expression+" * .6 + compartido * .6)";
    return "// Números en base12. Puerta de actualización diferida: activo.\n"
        "// Sólo la asignación final a opcional es diferida; las demás siempre se ejecutan.\n"
        "estado activo = 1;\nestado posicion = .3;\nestado velocidad = .03;\nestado compartido = 0;\nestado requerido = 0;\nestado opcional = 0;\n"
        "al_actualizar {\n    compartido = posicion + velocidad * dt;\n    requerido = compartido * .6 + .3;\n    posicion = compartido;\n    opcional = "+expression+";\n}\nal_dibujar {}\n";
}

SmoKernelSource smo_lazy_task_kernel(unsigned mode){require(mode<11,"Variante diferida inexistente.");const auto hlsl=source_file("smo_shader_hlsl.inc"),glsl=source_file("smo_shader_glsl.inc"),body=source_file("smo_lazy_task.inc");const auto define="#define SMO_LAZY_MODE "+std::to_string(mode)+"\n";
    SmoKernelSource k;k.name="lazy_mode"+std::to_string(mode);k.hlsl=define+hlsl+body;k.glsl="#version 430\n"+define+glsl+body;
#ifdef LGAED_SMO_LAZY_HLSL_SHADER_ROOT
    load_smo_dxbc_assets(k,std::filesystem::path(LGAED_SMO_LAZY_HLSL_SHADER_ROOT),hlsl+body,true);
#endif
#ifdef LGAED_SMO_LAZY_SHADER_ROOT
    const auto root=std::filesystem::path(LGAED_SMO_LAZY_SHADER_ROOT);const auto raw_hash=state::read_bytes(root/"source.sha256",256);std::string manifest(raw_hash.begin(),raw_hash.end());while(!manifest.empty()&&(manifest.back()=='\n'||manifest.back()=='\r'||manifest.back()==' '))manifest.pop_back();
    require(manifest==state::sha256(glsl+body),"Recompila los shaders del canal SMO; su identidad cambió.");const auto raw=state::read_bytes(root/"shaders"/(k.name+".spv"),4u*1024u*1024u);require(raw.size()>=20&&raw.size()%4==0,"SPIR-V de canal inválido.");k.spirv.resize(raw.size()/4);std::memcpy(k.spirv.data(),raw.data(),raw.size());
#endif
    return k;
}

std::size_t smo_lazy_task_contract_checks(){const auto c=compile(smo_lazy_task_source());std::size_t checks=1;
    require(c.end==c.ir.update.size()&&c.begin>0&&c.end-c.begin>64,"La asignación opcional no contiene trabajo real.");++checks;
    auto reject=[&](std::string text){bool failed=false;try{(void)compile(text);}catch(const std::exception&){failed=true;}require(failed,"El compilador aceptó un efecto/observador fuera del contrato diferido.");++checks;};
    auto observed=smo_lazy_task_source();observed.insert(observed.find("\n}\nal_dibujar"),"\n requerido = opcional;");reject(observed);
    auto gate=smo_lazy_task_source();gate.insert(gate.find("\n    compartido"),"\n activo = 0;");reject(gate);
    auto alternative=smo_lazy_task_source();const auto at=alternative.find("opcional = (");alternative.replace(at,alternative.find(';',at)-at,"opcional = alternativa(2 * 0)");reject(alternative);
    auto draw=smo_lazy_task_source();draw.replace(draw.find("al_dibujar {}"),14,"al_dibujar { dibujar(); }");reject(draw);
    for(const auto percent:{0u,25u,50u,75u,100u})for(const bool grouped:{false,true}){const auto in=inputs(c,129,percent,grouped);const auto e=reference(in,{},c,0);for(std::size_t i=0;i<in.size();++i){const bool active=real(in[i][0])!=0;require(e.output[i][2]==(active?1u:0u)&&e.output[i][3]==0,"El contrato CPU perdió la puerta o el diagnóstico.");if(!active)require(in[i][5]==e.states[i][5],"La omisión CPU sobrescribió el destino.");++checks;}}
    return checks;
}

SmoLazyTaskReport run_smo_lazy_task(const SmoLazyTaskOptions& options,std::stop_token stop,std::function<void(unsigned)> progress){
    require(options.items>0&&options.items<=65536&&options.active_percent<=100&&options.rounds>0&&options.rounds<=100&&options.frames>0&&options.frames<=32&&options.warmups<=10&&options.change_every<=32,"Opciones de canales fuera de su dominio acotado.");
    stopped(stop);const auto start=Clock::now();SmoLazyTaskReport r;r.source=options.source.empty()?smo_lazy_task_source():options.source;
    const auto compiling=Clock::now();const auto c=compile(r.source);require(same_program(c,compile(smo_lazy_task_source())),"El control convencional especializado exige el programa de muestra validado.");r.source_compile_ms=elapsed(compiling);
    r.source_hash=state::sha256(r.source);r.contract="Canal diferido explícito, primaria binary32. Sólo se omiten productores exclusivos y escritura de opcional; los compartidos y errores necesarios se conservan. La agrupación GPU produce una permutación estable de índices, sin mover estados. Las revisiones de la máscara son entradas conocidas del escenario; no se afirma detectar gratis cambios arbitrarios en la GPU. Los relojes incluyen generar máscaras, contar, prefijar, dispersar índices, sincronizar, actualizar y dibujar cuando cada fase es necesaria. La validación se hace fuera de esos relojes; no son FPS de presentación de Windows.";
    r.items=options.items;r.active_items=static_cast<U>(static_cast<std::uint64_t>(options.items)*options.active_percent/100u);r.frames=options.frames;r.grouped=options.grouped;r.change_every=options.change_every;r.optional_instructions=c.end-c.begin;r.required_instructions=c.begin;
    const auto initial=inputs(c,options.items,options.active_percent,options.grouped);
    U mask_stride=(r.items/3u)|1u;while(std::gcd(mask_stride,r.items)!=1u)mask_stride+=2u;
    const auto phase=[&](U frame){if(!options.change_every||r.active_items==0||r.active_items==r.items)return 0u;return static_cast<U>((static_cast<std::uint64_t>(frame/options.change_every)*mask_stride)%r.items);};
    const auto apply_mask=[&](std::vector<Item>& state,U frame){const U shift=phase(frame);for(U i=0;i<r.items;++i){const U rank=(i+shift)%r.items;const bool on=options.grouped?rank<r.active_items:(static_cast<std::uint64_t>(rank+1)*r.active_items/r.items!=static_cast<std::uint64_t>(rank)*r.active_items/r.items);state[i][0]=bits(on?1.f:0.f);}};
    std::vector<Expected> expected;std::vector<std::vector<U>> expected_pixels,maps;auto previous=initial;std::vector<Observation> prior;
    for(U frame=0;frame<options.frames;++frame){apply_mask(previous,frame);expected.push_back(reference(previous,prior,c,frame));previous=expected.back().states;prior=expected.back().output;expected_pixels.push_back(project(previous,r.width,r.height));
        std::vector<U> order;order.reserve(r.items);for(U i=0;i<r.items;++i)if(real(previous[i][0])!=0.f)order.push_back(i);for(U i=0;i<r.items;++i)if(real(previous[i][0])==0.f)order.push_back(i);maps.push_back(std::move(order));}
    const auto preparing=Clock::now();auto gpu=create_smo_compute(options.api,options.device,stop);r.device=gpu->info();std::array<SmoKernel,11> kernels{};for(U i=0;i<kernels.size();++i){stopped(stop);kernels[i]=gpu->kernel(smo_lazy_task_kernel(i));}r.shader_prepare_ms=elapsed(preparing);
    const auto uploading=Clock::now();const auto instructions=gpu->buffer(c.instructions.size()*4,bytes(c.instructions)),input=gpu->buffer(initial.size()*sizeof(Item),bytes(initial)),literals=gpu->buffer(c.literals.size()*4,bytes(c.literals));
    constexpr U variants=5;const U groups=(r.items+63u)/64u;std::array<SmoBuffer,variants> states{},outputs{},indices{};const std::vector<Observation> empty(r.items);
    for(U v=0;v<variants;++v){states[v]=gpu->buffer(initial.size()*sizeof(Item),bytes(initial));outputs[v]=gpu->buffer(empty.size()*sizeof(Observation),bytes(empty));if(v>=3){const auto allocation=Clock::now();indices[v]=gpu->buffer(r.items*4u);r.variants[v].group_allocation_ms=elapsed(allocation);}}
    const auto allocating_scratch=Clock::now();const auto scratch=gpu->buffer((groups*4u+1u)*4u);const double scratch_ms=elapsed(allocating_scratch);for(U v=3;v<variants;++v)r.variants[v].group_allocation_ms+=scratch_ms;r.group_extra_buffer_bytes=r.items*4u+(groups*4u+1u)*4u;const auto image=gpu->buffer(static_cast<std::size_t>(r.width)*r.height*4u);
    r.buffer_bytes=c.instructions.size()*4+c.literals.size()*4+initial.size()*(sizeof(Item)*(variants+1)+sizeof(Observation)*variants)+r.items*4u*2u+(groups*4u+1u)*4u+static_cast<std::uint64_t>(r.width)*r.height*4u;
    r.upload_ms=elapsed(uploading);
    r.variants[0].name="SMO: cálculo completo y omisión final";r.variants[1].name="SMO: omisión anticipada, orden original";r.variants[2].name="Control convencional estático, orden original";r.variants[3].name="SMO: omisión anticipada + agrupación GPU";r.variants[4].name="Control convencional estático + agrupación GPU";
    const std::array<U,variants> update_mode{0,1,2,5,6};std::array<U,variants> mask_revision{},map_revision{};map_revision.fill(0xffffffffu);
    const auto command=[&](U kernel,U v,U frame){SmoDispatch call{kernels[kernel],{instructions,input,states[v],outputs[v],literals,indices[v],scratch,image},
        {r.items,static_cast<U>(c.ir.update.size()),c.begin,c.end,6u,5u,0u,bits(.0625f),frame,c.begin,r.width,r.height,r.active_items,phase(frame),options.grouped?0u:1u,0u},{groups,1,1}};
        if(kernel==8)call.groups={1,1,1};if(kernel==3)call.groups={(r.width*r.height+63u)/64u,1,1};return call;};
    const auto grouping=[&](U v,U frame){return std::array<SmoDispatch,3>{command(7,v,frame),command(8,v,frame),command(9,v,frame)};};
    const auto reset=[&](U v){const auto begin=Clock::now();gpu->write(states[v],bytes(initial));gpu->write(outputs[v],bytes(empty));mask_revision[v]=0u;r.trial_reset_ms+=elapsed(begin);};
    const auto verify_frame=[&](U v,U frame,const std::vector<U>* pixels,bool verify_map){const auto begin=Clock::now();const auto state=read<Item>(*gpu,states[v],r.items);const auto observed=read<Observation>(*gpu,outputs[v],r.items);verify(state,observed,expected[frame],c,frame,v,r.verified_words);
        if(verify_map){const auto order=read<U>(*gpu,indices[v],r.items);require(order==maps[frame],"La agrupación GPU no es una permutación estable de todos los objetos.");r.verified_words+=order.size();}
        if(pixels){require(*pixels==expected_pixels[frame],"La imagen completa de canales GPU no coincide.");r.verified_words+=pixels->size();}
        if(frame+1==options.frames){auto& result=r.variants[v];result.output_hash=hash(bytes(state));if(pixels)result.image_hash=hash(bytes(*pixels));result.executed_instructions=0;result.writes=0;for(const auto& o:observed){result.executed_instructions+=o[4];result.writes+=o[5];}}
        r.validation_ms+=elapsed(begin);};
    // Mandatory error probes retain the original deferred-contract coverage.
    // Mapped variants build their actual GPU list even with an invalid gate:
    // sorting must never filter out an object whose error is observable.
    std::vector<Item> probes(4,c.initial);for(auto& item:probes){item[0]=0;item[1]=bits(.25f);item[2]=bits(.015625f);item[5]=bits(.125f);}probes[0][5]=0x7fc00000u;probes[1][0]=bits(1.f);probes[1][5]=0x7fc00000u;probes[2][1]=0x7fc00000u;probes[3][0]=0x7fc00000u;
    const std::vector<Observation> zero_probes(4);const std::vector<U> probe_order{0,1,2,3};const auto probe_input=gpu->buffer(probes.size()*sizeof(Item),bytes(probes)),probe_state=gpu->buffer(probes.size()*sizeof(Item),bytes(probes)),probe_output=gpu->buffer(zero_probes.size()*sizeof(Observation),bytes(zero_probes)),probe_map=gpu->buffer(16,bytes(probe_order));r.buffer_bytes+=probes.size()*(2*sizeof(Item)+sizeof(Observation))+16;
    for(U v=0;v<variants;++v){stopped(stop);gpu->write(probe_state,bytes(probes));gpu->write(probe_output,bytes(zero_probes));SmoDispatch call{kernels[update_mode[v]],{instructions,probe_input,probe_state,probe_output,literals,probe_map,scratch},{4u,static_cast<U>(c.ir.update.size()),c.begin,c.end,6u,5u,0u,bits(.0625f),0u,c.begin},{1,1,1}};
        if(v>=3){std::array<SmoDispatch,3> prepare{call,call,call};for(U k=0;k<3;++k)prepare[k].kernel=kernels[7u+k];(void)gpu->execute(prepare,false);}
        (void)gpu->execute(std::span(&call,1),false);
        const auto observed=read<Observation>(*gpu,probe_output,4);const auto committed=read<Item>(*gpu,probe_state,4);require(observed[0][3]==0&&observed[0][2]==0&&committed[0][5]==probes[0][5],"La omisión observó un productor privado o escribió el destino inactivo.");for(U i=1;i<4;++i)require(observed[i][3]==1&&observed[i][2]==0&&observed[i][5]==0&&committed[i]==probes[i],"La omisión/agrupación ocultó un error o publicó una transacción fallida.");require(observed[0][4]==(v==0?c.end:c.begin),"La guarda no evitó productores exclusivos.");r.verified_words+=56;}
    for(U v=0;v<variants;++v){reset(v);if(v>=3){const auto group_start=Clock::now();const auto prep=grouping(v,0);r.variants[v].initial_group_gpu_ms=gpu->execute(prep,true);r.variants[v].initial_group_wall_ms=elapsed(group_start);map_revision[v]=0;}
        const auto update=command(update_mode[v],v,0),clear=command(3,v,0),draw=command(4,v,0);const std::array<SmoDispatch,3> pipeline{clear,update,draw};(void)gpu->execute(pipeline,false);const auto pixels=read<U>(*gpu,image,static_cast<std::size_t>(r.width)*r.height);verify_frame(v,0,&pixels,v>=3);}
    r.preparation_ms=elapsed(start);
    const auto warm_start=Clock::now();for(U warm=0;warm<options.warmups;++warm)for(U v=0;v<variants;++v){reset(v);for(U frame=0;frame<r.frames;++frame){std::vector<SmoDispatch> pipeline;const U revision=phase(frame);if(mask_revision[v]!=revision){pipeline.push_back(command(10,v,frame));mask_revision[v]=revision;}if(v>=3&&map_revision[v]!=revision){const auto build=grouping(v,frame);pipeline.insert(pipeline.end(),build.begin(),build.end());map_revision[v]=revision;}pipeline.push_back(command(update_mode[v],v,frame));(void)gpu->execute(pipeline,false);}}
    r.warmup_ms=elapsed(warm_start);
    for(U round=0;round<options.rounds;++round){for(U order=0;order<variants;++order){const U v=(round+order)%variants;auto& result=r.variants[v];double task_total=0,frame_gpu_total=0,wall_total=0,read_total=0;
        for(U pass=0;pass<2;++pass){reset(v);for(U frame=0;frame<r.frames;++frame){stopped(stop);const auto begin=Clock::now();const U revision=phase(frame);std::vector<SmoDispatch> preparation;
            if(mask_revision[v]!=revision){preparation.push_back(command(10,v,frame));mask_revision[v]=revision;}
            const bool rebuilt=v>=3&&map_revision[v]!=revision;if(rebuilt){const auto build=grouping(v,frame);preparation.insert(preparation.end(),build.begin(),build.end());map_revision[v]=revision;}
            std::vector<SmoDispatch> pipeline=preparation;if(pass==1)pipeline.push_back(command(3,v,frame));pipeline.push_back(command(update_mode[v],v,frame));if(pass==1)pipeline.push_back(command(4,v,frame));
            const double gpu_ms=gpu->execute(pipeline,true);const auto reading=Clock::now();std::vector<U> pixels;if(pass==1)pixels=read<U>(*gpu,image,static_cast<std::size_t>(r.width)*r.height);const double read_ms=elapsed(reading),wall_ms=elapsed(begin);
            verify_frame(v,frame,pass==1?&pixels:nullptr,rebuilt);
            if(pass==0)task_total+=gpu_ms;else{frame_gpu_total+=gpu_ms;wall_total+=wall_ms;read_total+=read_ms;result.frame_latencies_gpu.push_back(gpu_ms);if(rebuilt){++result.group_rebuilds;const auto build=grouping(v,frame);result.grouping_samples.push_back(gpu->execute(build,true));}if(v==3&&frame+1==r.frames)r.preview_bgra=std::move(pixels);}
        }}
        result.gpu_samples.push_back(task_total/r.frames);result.frame_gpu_samples.push_back(frame_gpu_total/r.frames);result.frame_samples.push_back(wall_total/r.frames);result.readback_ms+=read_total/(r.frames*options.rounds);
    }if(progress)progress((round+1)*100/options.rounds);}
    for(auto& result:r.variants){result.gpu_ms=median(result.gpu_samples);result.frame_gpu_ms=median(result.frame_gpu_samples);result.frame_ms=median(result.frame_samples);result.group_gpu_ms=median(result.grouping_samples);result.verified=true;result.timing_valid=std::ranges::all_of(result.gpu_samples,[](double n){return std::isfinite(n)&&n>0;})&&std::ranges::all_of(result.frame_gpu_samples,[](double n){return std::isfinite(n)&&n>0;});}
    for(U v=1;v<variants;++v)require(r.variants[v].output_hash==r.variants[0].output_hash&&r.variants[v].image_hash==r.variants[0].image_hash,"Las trayectorias/imágenes de agrupación no coinciden exactamente.");r.equivalent=true;
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))r.process_peak_working_set=memory.PeakWorkingSetSize;return r;
}
} //namespace lgaed::presentation

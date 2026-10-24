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
        const U extra=mode==0u&&real(states[i][0])==0.f?(frame+1u)*(c.end-c.begin):0u;
        require(o[4]==e[4]+extra,"La instrumentación no acredita las instrucciones realmente evitadas.");checked+=14;
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

SmoKernelSource smo_lazy_task_kernel(unsigned mode){require(mode<5,"Variante diferida inexistente.");const auto hlsl=source_file("smo_shader_hlsl.inc"),glsl=source_file("smo_shader_glsl.inc"),body=source_file("smo_lazy_task.inc");const auto define="#define SMO_LAZY_MODE "+std::to_string(mode)+"\n";
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
    require(options.items>0&&options.items<=65536&&options.active_percent<=100&&options.rounds>0&&options.rounds<=100&&options.frames>0&&options.frames<=32&&options.warmups<=10,"Opciones de canales fuera de su dominio acotado.");
    stopped(stop);const auto start=Clock::now();SmoLazyTaskReport r;r.source=options.source.empty()?smo_lazy_task_source():options.source;
    const auto compile_start=Clock::now();const auto c=compile(r.source);const auto canonical=compile(smo_lazy_task_source());require(same_program(c,canonical),"El control convencional especializado corresponde sólo al programa de muestra. Se rechaza una comparación distinta, sin inventar equivalencia.");r.source_compile_ms=elapsed(compile_start);
    r.source_hash=state::sha256(r.source);r.contract="Canal diferido explícito, primaria binary32: activo=0 omite evaluación, valor, alternativa, diagnóstico privado y escritura de opcional. Los productores compartidos y salidas requeridas siguen evaluándose. No cambia las reglas globales de cero. Control estático independiente, mismo contrato y árbol de operaciones.";
    r.items=options.items;r.active_items=static_cast<U>(static_cast<std::uint64_t>(options.items)*options.active_percent/100u);r.frames=options.frames;r.grouped=options.grouped;r.optional_instructions=c.end-c.begin;r.required_instructions=c.begin;
    const auto initial=inputs(c,options.items,options.active_percent,options.grouped);std::vector<Expected> expected;expected.reserve(options.frames);auto prev=initial;std::vector<Observation> last;
    for(U frame=0;frame<options.frames;++frame){expected.push_back(reference(prev,last,c,frame));prev=expected.back().states;last=expected.back().output;}
    std::vector<std::vector<U>> expected_pixels;for(const auto& frame:expected)expected_pixels.push_back(project(frame.states,r.width,r.height));
    const auto shader_start=Clock::now();auto gpu=create_smo_compute(options.api,options.device,stop);r.device=gpu->info();std::array<SmoKernel,5> kernels{};for(U mode=0;mode<5;++mode)kernels[mode]=gpu->kernel(smo_lazy_task_kernel(mode));r.shader_prepare_ms=elapsed(shader_start);
    const auto upload_start=Clock::now();const auto instructions=gpu->buffer(c.instructions.size()*4,bytes(c.instructions)),input=gpu->buffer(initial.size()*sizeof(Item),bytes(initial)),literals=gpu->buffer(c.literals.size()*4,bytes(c.literals));
    const std::vector<Observation> empty_output(initial.size());
    std::array<SmoBuffer,3> states{},outputs{};for(U mode=0;mode<3;++mode){states[mode]=gpu->buffer(initial.size()*sizeof(Item),bytes(initial));outputs[mode]=gpu->buffer(initial.size()*sizeof(Observation),bytes(empty_output));}
    const auto image=gpu->buffer(static_cast<std::size_t>(r.width)*r.height*4);
    r.buffer_bytes=c.instructions.size()*4+c.literals.size()*4+initial.size()*(sizeof(Item)*4+sizeof(Observation)*3)+static_cast<std::uint64_t>(r.width)*r.height*4;r.upload_ms=elapsed(upload_start);r.preparation_ms=elapsed(start);
    r.variants[0].name="SMO: cálculo completo y omisión final";r.variants[1].name="SMO: omisión antes de productores exclusivos";r.variants[2].name="Control convencional estático: omisión anticipada";
    auto calls=[&](U mode,U frame){SmoDispatch update{kernels[mode],{instructions,input,states[mode],outputs[mode],literals,0,0,image},{options.items,static_cast<U>(c.ir.update.size()),c.begin,c.end,6u,5u,0u,bits(.0625f),frame,c.begin,r.width,r.height},{(options.items+63)/64,1,1}};
        auto clear=update;clear.kernel=kernels[3];clear.groups={(r.width*r.height+63)/64,1,1};auto render=update;render.kernel=kernels[4];return std::array<SmoDispatch,3>{clear,update,render};};
    auto dispatch=[&](U mode,U frame,bool timed){const auto pipeline=calls(mode,frame);return gpu->execute(std::span(&pipeline[1],1),timed);};
    const auto validation_start=Clock::now();
    // The sentinels distinguish inactive private invalid operands from invalid
    // required/shared producers. No test silently removes an observable error.
    std::vector<Item> probes(4,c.initial);for(auto& item:probes){item[0]=0;item[1]=bits(.25f);item[2]=bits(.015625f);item[5]=bits(.125f);}
    probes[0][5]=0x7fc00000u; //private NaN is not observed by an inactive channel
    probes[1][0]=bits(1.f);probes[1][5]=0x7fc00000u; //active NaN remains an error
    probes[2][1]=0x7fc00000u; //required shared producer is ALWAYS observed
    probes[3][0]=0x7fc00000u; //the gate itself may never be skipped
    const std::vector<Observation> empty_probes(probes.size());
    const auto probe_input=gpu->buffer(probes.size()*sizeof(Item),bytes(probes)),probe_state=gpu->buffer(probes.size()*sizeof(Item),bytes(probes)),probe_output=gpu->buffer(probes.size()*sizeof(Observation),bytes(empty_probes));
    for(U mode=0;mode<3;++mode){stopped(stop);gpu->write(probe_state,bytes(probes));SmoDispatch call{kernels[mode],{instructions,probe_input,probe_state,probe_output,literals},{4u,static_cast<U>(c.ir.update.size()),c.begin,c.end,6u,5u,0u,bits(.0625f),0u,c.begin},{1,1,1}};
        (void)gpu->execute(std::span(&call,1),false);const auto observed=read<Observation>(*gpu,probe_output,4);const auto committed=read<Item>(*gpu,probe_state,4);
        require(observed[0][3]==0&&observed[0][2]==0&&committed[0][5]==probes[0][5],"Un canal inactivo evaluó un diagnóstico privado observable o sobrescribió su destino.");
        for(U item=1;item<4;++item)require(observed[item][3]==1&&observed[item][2]==0&&observed[item][5]==0&&committed[item]==probes[item],"La omisión ocultó un error requerido o publicó una transacción fallida.");
        const U expected_ops=mode==0?c.end:c.begin;require(observed[0][4]==expected_ops,"La guarda no evitó los productores exclusivos antes de la evaluación.");r.verified_words+=4u*14u;
    }
    r.buffer_bytes+=probes.size()*(2*sizeof(Item)+sizeof(Observation));
    // Exact checks precede performance.
    for(U mode=0;mode<3;++mode){const auto pipeline=calls(mode,0);(void)gpu->execute(pipeline,false);const auto actual=read<Item>(*gpu,states[mode],options.items);const auto out=read<Observation>(*gpu,outputs[mode],options.items);verify(actual,out,expected[0],c,0,mode,r.verified_words);
        const auto pixels=read<U>(*gpu,image,static_cast<std::size_t>(r.width)*r.height);require(pixels==expected_pixels[0],"La proyección GPU de canales difiere de la referencia exacta.");r.verified_words+=pixels.size();}
    r.validation_ms=elapsed(validation_start);r.preparation_ms=elapsed(start);
    const auto warmup_start=Clock::now();
    for(U warm=0;warm<options.warmups;++warm)for(U mode=0;mode<3;++mode){stopped(stop);for(U frame=0;frame<options.frames;++frame){const auto pipeline=calls(mode,frame);(void)gpu->execute(pipeline,false);}}
    r.warmup_ms=elapsed(warmup_start);
    for(U round=0;round<options.rounds;++round){stopped(stop);for(U order=0;order<3;++order){const U mode=(round+order)%3;auto& v=r.variants[mode];double gpu_total=0,frame_gpu_total=0,wall_total=0,read_total=0,conversion_total=0;
            // These are two independent trajectories from exactly the same
            // inputs: task-only timestamps, then a full clear/update/project
            // timestamp. The state is never advanced twice in one trajectory.
            for(U pass=0;pass<2;++pass)for(U frame=0;frame<options.frames;++frame){stopped(stop);const auto frame_start=Clock::now();const auto pipeline=calls(mode,frame);const double gpu_ms=pass==0?dispatch(mode,frame,true):gpu->execute(pipeline,true);
                const auto read_start=Clock::now();std::vector<U> pixels;
                if(pass==1)pixels=read<U>(*gpu,image,static_cast<std::size_t>(r.width)*r.height);const auto read_ms=elapsed(read_start);
                const auto wall_ms=elapsed(frame_start);
                // Presentation consumes the GPU image, not the state dump.
                // State/counter readback and comparison are QA-only costs and
                // must not inflate a frame which does not need those reads.
                const auto validating=Clock::now();const auto actual=read<Item>(*gpu,states[mode],options.items);const auto out=read<Observation>(*gpu,outputs[mode],options.items);
                // Every state, diagnostic, logical operation count, write
                // counter and pixel is checked outside both timing intervals.
                verify(actual,out,expected[frame],c,frame,mode,r.verified_words);
                if(pass==0)gpu_total+=gpu_ms;else{require(pixels==expected_pixels[frame],"La imagen completa de canales GPU no coincide.");r.verified_words+=pixels.size();frame_gpu_total+=gpu_ms;wall_total+=wall_ms;read_total+=read_ms;}
                r.validation_ms+=elapsed(validating);
                if(pass==1&&frame+1==options.frames){v.output_hash=hash(bytes(actual));v.image_hash=hash(bytes(pixels));v.executed_instructions=0;v.writes=0;for(const auto& item:out){v.executed_instructions+=item[4];v.writes+=item[5];}if(mode==1)r.preview_bgra=std::move(pixels);}
            }
            v.gpu_samples.push_back(gpu_total/options.frames);v.frame_gpu_samples.push_back(frame_gpu_total/options.frames);v.frame_samples.push_back(wall_total/options.frames);v.readback_ms+=read_total/(options.frames*options.rounds);v.conversion_ms+=conversion_total/(options.frames*options.rounds);
        }if(progress)progress((round+1)*100/options.rounds);}
    for(auto& v:r.variants){v.gpu_ms=median(v.gpu_samples);v.frame_gpu_ms=median(v.frame_gpu_samples);v.frame_ms=median(v.frame_samples);v.verified=true;v.timing_valid=std::ranges::all_of(v.gpu_samples,[](double value){return std::isfinite(value)&&value>0;})&&std::ranges::all_of(v.frame_gpu_samples,[](double value){return std::isfinite(value)&&value>0;});}
    require(r.variants[0].output_hash==r.variants[1].output_hash&&r.variants[1].output_hash==r.variants[2].output_hash,"Las trayectorias finales de canales no coinciden.");r.equivalent=true;
    require(r.variants[0].image_hash==r.variants[1].image_hash&&r.variants[1].image_hash==r.variants[2].image_hash,"Las imágenes finales de canales no coinciden.");
    PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory)))r.process_peak_working_set=memory.PeakWorkingSetSize;
    return r;
}
} //namespace lgaed::presentation

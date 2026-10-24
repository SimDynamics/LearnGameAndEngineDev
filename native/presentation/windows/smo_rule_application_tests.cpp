#include "presentation/windows/smo_rule_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>

// Opt-in application experiments, NOT a replacement engine. Existing rule
// kernels calculate every numerical application stage. Readback routes their
// results to the next stage outside GPU timestamps; no CPU physics substitutes
// for a GPU result. The small extra kernel isolates genuine skipped writes.
namespace {
using namespace lgaed::presentation;
using Op=SmoRuleOperation;
using U=std::uint32_t;
constexpr unsigned oracle=3;
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
U bits(float value){return std::bit_cast<U>(value);}
float real(U value){return std::bit_cast<float>(value);}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& value){return std::as_bytes(std::span(value));}
template<class T>std::vector<T> read(SmoCompute& gpu,SmoBuffer id,std::size_t count){
    const auto raw=gpu.read(id);require(raw.size()==count*sizeof(T),"Lectura de aplicación GPU incompleta.");
    std::vector<T> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;
}
bool usable(const SmoRuleResult& value){return value.diagnostic==0&&(value.flags&smo_rule_has_primary)!=0;}
bool numeric(U expected,U actual,bool exact,double& maximum){
    if(exact)return expected==actual;
    const float a=real(expected),b=real(actual);if(!std::isfinite(a)||!std::isfinite(b))return false;
    const double error=std::abs(static_cast<double>(a)-b);maximum=std::max(maximum,error);
    return error<=2e-4*std::max(1.,std::abs(static_cast<double>(a)));
}
bool equal_result(const SmoRuleResult& a,const SmoRuleResult& b,bool exact,double& maximum){
    if(a.flags!=b.flags||a.diagnostic!=b.diagnostic||a.packed_primary!=b.packed_primary||
        a.packed_alternative!=b.packed_alternative||a.numerator!=b.numerator||a.denominator!=b.denominator)return false;
    const bool primary=usable(a)?numeric(a.primary_bits,b.primary_bits,exact,maximum):a.primary_bits==b.primary_bits;
    const bool alternate=a.diagnostic==0&&(a.flags&smo_rule_has_alternative)?
        numeric(a.alternative_bits,b.alternative_bits,exact,maximum):a.alternative_bits==b.alternative_bits;
    return primary&&alternate;
}
double median(std::vector<double> samples){
    require(!samples.empty(),"No hay muestras GPU.");
    for(const auto sample:samples)require(std::isfinite(sample)&&sample>0,"El reloj GPU no es válido; no se sustituye por tiempo CPU.");
    std::ranges::sort(samples);const auto middle=samples.size()/2;
    return samples.size()%2?samples[middle]:(samples[middle-1]+samples[middle])*.5;
}
std::string source(std::string_view name){
    const auto raw=lgaed::state::read_bytes(std::filesystem::path(lgaed::build::source_root)/"native/presentation/windows"/name,512*1024);
    std::string result;for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;result+=static_cast<char>(raw[i]);}return result;
}
SmoKernelSource application_kernel(unsigned mode){
    require(mode<3,"Variante de aplicación inexistente.");
    const auto hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc"),
        arithmetic=source("smo_arithmetic.inc"),body=source("smo_rule_application.inc");
    SmoKernelSource result;result.name="application_mode"+std::to_string(mode);
    const auto define="#define APP_MODE "+std::to_string(mode)+"\n";
    result.hlsl=define+hlsl+arithmetic+body;result.glsl="#version 430\n"+define+glsl+arithmetic+body;
#if defined(LGAED_SMO_APPLICATION_HLSL_SHADER_ROOT)
    load_smo_dxbc_assets(result,std::filesystem::path(LGAED_SMO_APPLICATION_HLSL_SHADER_ROOT),hlsl+arithmetic+body,true);
#endif
#if defined(LGAED_HAS_VULKAN) && defined(LGAED_SMO_APPLICATION_SHADER_ROOT)
    const auto root=std::filesystem::path(LGAED_SMO_APPLICATION_SHADER_ROOT);
    const auto raw_hash=lgaed::state::read_bytes(root/"source.sha256",256);std::string hash(raw_hash.begin(),raw_hash.end());
    while(!hash.empty()&&(hash.back()=='\r'||hash.back()=='\n'||hash.back()==' '))hash.pop_back();
    require(hash==lgaed::state::sha256(glsl+arithmetic+body),"Los shaders de aplicación Vulkan requieren recompilar el target QA existente.");
    const auto raw=lgaed::state::read_bytes(root/"shaders"/(result.name+".spv"),2*1024*1024);
    require(raw.size()>=20&&raw.size()%4==0,"SPIR-V de aplicación incompleto.");
    result.spirv.resize(raw.size()/4);std::memcpy(result.spirv.data(),raw.data(),raw.size());
    require(result.spirv.front()==0x07230203u,"SPIR-V de aplicación inválido.");
#endif
    return result;
}

struct Properties {
    std::vector<SmoRuleInput> witnesses;std::size_t assertions=0;
    SmoRuleResult evaluate(Op op,float a,float b,U profile=0,U selection=0){
        const auto item=smo_rule_input(op,a,b,profile,selection);witnesses.push_back(item);return smo_rule_reference(item);
    }
    float value(Op op,float a,float b,U profile=0,U selection=0){const auto result=evaluate(op,a,b,profile,selection);
        require(usable(result),"Una propiedad intentó usar una operación no resuelta.");return smo_rule_primary(result);}
    void check(bool condition,std::string_view message){++assertions;require(condition,message);}
    void run(){
        for(U p=0;p<=smo_rule_profile_mask;++p){if((p&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)continue;
            for(const float a:{-2.f,0.f,2.f}){
                const auto product=evaluate(Op::multiply,a,0,p);
                check(usable(product)&&(product.flags&smo_rule_has_alternative),"El producto cero perdió sus dos ramas explícitas.");
                check(smo_rule_primary(product)==((p&2u)?a:0.f),"La política de producto cero cambió.");
                check(value(Op::multiply,a,0,p,1)==((p&2u)?0.f:a),"La alternativa del producto cero no es la rama restante.");
                const auto division=evaluate(Op::divide,a,0,p);
                if((p&12u)==8u){check(division.diagnostic==smo_rule_division_unresolved&&
                        division.flags==U(smo_rule_experimental|smo_rule_unresolved)&&division.primary_bits==smo_rule_nan,
                        "Una operación no realizada inventó cero o un valor principal.");}
                else{check(usable(division)&&smo_rule_primary(division)==((p&12u)==4u?0.f:a),"La política de divisor cero cambió.");
                    check(value(Op::divide,a,0,p,1)==((p&12u)==4u?a:0.f),"La división perdió su alternativa explícita.");}
            }
            for(const auto op:{Op::direct_power,Op::inverted_power}){
                const auto zero=evaluate(op,2,0,p);check(usable(zero)&&(zero.flags&smo_rule_has_alternative),"La potencia cero perdió alternativas.");
                check(smo_rule_primary(zero)==((p&16u)?2.f:0.f)&&value(op,2,0,p,1)==((p&16u)?0.f:2.f),"La potencia cero no conserva su contrato.");
                for(const float n:{-1.f,1.f})check(value(op,2,n,p)==((p&32u)?n:2.f*n),"Cambió el experimento de exponentes ±1.");
            }
            // Exact dyadic/integer domain: no rounding assumption hides a law.
            for(const float a:{.5f,1.f,2.f})for(const float b:{.5f,1.f,2.f}){
                const auto product=value(Op::multiply,a,b,p);
                check(value(Op::divide,product,b,p)==a,"Multiplicar/dividir no revirtió en el dominio positivo no nulo.");
            }
        }
        for(const float a:{1.f,2.f,3.f})for(const float b:{1.f,2.f,3.f})for(const float c:{1.f,2.f,3.f}){
            check(value(Op::multiply,a,value(Op::add,b,c))==value(Op::add,value(Op::multiply,a,b),value(Op::multiply,a,c)),
                "La bilinealidad positiva exacta perdió su dominio válido.");
        }
        for(const float a:{-2.f,-1.f,0.f,1.f,2.f})for(const float b:{-2.f,-1.f,0.f,1.f,2.f}){
            const auto p=U(smo_rule_conventional_sign);
            check(value(Op::multiply,a,value(Op::add,b,1),p)==value(Op::add,value(Op::multiply,a,b,p),value(Op::multiply,a,1,p),p),
                "El control de signos ordinarios dejó de ser distributivo en enteros exactos.");
        }
        for(const float n:{1.f,2.f,3.f})for(const float m:{1.f,2.f,3.f})
            check(value(Op::direct_power,2,n+m)==value(Op::multiply,value(Op::direct_power,2,n),value(Op::direct_power,2,m)),
                "La ley de exponentes positivos falló en su dominio exacto.");
        check(value(Op::multiply,-2,-3)==-6&&value(Op::multiply,-2,-3,smo_rule_conventional_sign)==6,
            "No se mostró la diferencia real entre signo absorbente y coordenadas ordinarias.");
        check(value(Op::divide,value(Op::multiply,2,-3),-3)==-2,"Se ocultó la falta de reversibilidad con dos negativos.");
        check(value(Op::multiply,2,value(Op::add,0,1),2)!=value(Op::add,value(Op::multiply,2,0,2),value(Op::multiply,2,1,2)),
            "Se atribuyó multilinealidad global a la conservación por cero.");
        check(value(Op::multiply,-2,value(Op::add,-1,1))!=value(Op::add,value(Op::multiply,-2,-1),value(Op::multiply,-2,1)),
            "Se ocultó el contraejemplo de bilinealidad del signo absorbente.");
        check(value(Op::direct_power,2,-1)==-2&&value(Op::direct_power,2,0)==0&&value(Op::direct_power,2,1)==2,
            "Una potencia SMO se sustituyó por su homónima ordinaria.");
        check(value(Op::inverted_power,value(Op::direct_power,2,2),2)==1,
            "Se presentó pinv como inversa funcional de pdir sin demostrarlo.");
        check(evaluate(Op::direct_power,0,2).diagnostic==smo_rule_domain,"Se inventó una generalización de potencia para base cero.");
        check(evaluate(Op::add,1,1,0,1).diagnostic==smo_rule_missing_alternative,"Se fabricó una alternativa ausente.");
        const auto literal=smo_rule_input12(Op::add,"10",".6");
        check(real(literal.left_bits)==12.f&&real(literal.right_bits)==.5f,"La notación duodecimal cambió la cantidad de entrada.");
        witnesses.push_back(literal);
        std::cout<<"CPU properties="<<assertions<<" witnesses="<<witnesses.size()
            <<"; positive nonzero reversibility and limited multilinearity PASS; global counterexamples retained.\n";
        std::cout<<"Counterexamples: absorbing (-2)*(-3)=-6 vs ordinary +6; (2*(-3))/(-3)=-2; "
            <<"conserving zero is not distributive; pdir(2,-1)=-2 is not reciprocal; pinv(pdir(2,2),2)=1.\n";
    }
};

struct Channel {
    std::array<SmoBuffer,3> gpu{};
    std::array<std::vector<U>,4> mirror;
};
struct Samples {std::array<std::vector<double>,3> gpu_ms;std::size_t elements=0;};
class Applications {
public:
    SmoCompute& gpu;std::array<SmoKernel,3> kernels;std::size_t count;
    std::map<std::string,Channel> channels;std::map<std::string,Samples> samples;
    SmoBuffer input;std::array<SmoBuffer,3> output{};std::size_t serial=0,verified=0,omitted=0,valid_zeros=0;double maximum_error=0;
    Applications(SmoCompute& device,const std::array<SmoKernel,3>& shaders,std::size_t size):gpu(device),kernels(shaders),count(size){
        require(size>0&&size<=65536,"Carga de aplicación fuera del límite acotado.");input=gpu.buffer(count*sizeof(SmoRuleInput));
        for(auto& buffer:output)buffer=gpu.buffer(count*sizeof(SmoRuleResult));
    }
    void channel(const std::string& name,const std::function<float(std::size_t)>& initial){
        require(!channels.contains(name),"Canal de aplicación duplicado.");Channel c;
        std::vector<U> values(count);for(std::size_t i=0;i<count;++i)values[i]=bits(initial(i));
        for(unsigned mode=0;mode<4;++mode){c.mirror[mode]=values;if(mode<3)c.gpu[mode]=gpu.buffer(count*4,bytes(values));}
        channels.emplace(name,std::move(c));
    }
    float value(const std::string& name,std::size_t index,unsigned mode)const{return real(channels.at(name).mirror.at(mode).at(index));}
    void stage(const std::string& name,const std::string& destination,
        const std::function<SmoRuleInput(std::size_t,unsigned)>& build,bool exact=true,bool timed=true){
        auto& target=channels.at(destination);std::vector<SmoRuleResult> expected(count);std::vector<U> next=target.mirror[oracle];
        for(std::size_t i=0;i<count;++i){expected[i]=smo_rule_reference(build(i,oracle));
            if(usable(expected[i]))next[i]=expected[i].primary_bits;}
        std::array<std::vector<SmoRuleResult>,3> results;
        for(unsigned position=0;position<3;++position){const unsigned mode=static_cast<unsigned>((serial+position)%3);
            std::vector<SmoRuleInput> data;data.reserve(count);for(std::size_t i=0;i<count;++i)data.push_back(build(i,mode));
            gpu.write(input,bytes(data));SmoDispatch dispatch;dispatch.kernel=kernels[mode];dispatch.buffers={input,output[mode],target.gpu[mode]};
            dispatch.constants[0]=static_cast<U>(count);dispatch.groups={(static_cast<U>(count)+63)/64,1,1};
            const auto& first=data.front();const bool uniform=std::ranges::all_of(data,[&](const auto& item){return item.operation==first.operation&&item.profile==first.profile&&item.selection==first.selection;});
            if(uniform){dispatch.constants[1]=first.operation;dispatch.constants[2]=first.profile;dispatch.constants[3]=first.selection;dispatch.constants[4]=1;}
            const auto elapsed=gpu.execute(std::span(&dispatch,1),timed);
            if(timed){require(std::isfinite(elapsed)&&elapsed>0,"Reloj GPU inválido en una aplicación.");samples[name].gpu_ms[mode].push_back(elapsed);samples[name].elements=count;}
            results[mode]=read<SmoRuleResult>(gpu,output[mode],count);const auto actual=read<U>(gpu,target.gpu[mode],count);
            for(std::size_t i=0;i<count;++i){const auto& got=results[mode][i];
                const U written=usable(got)?got.primary_bits:target.mirror[mode][i];
                if(!equal_result(expected[i],got,exact,maximum_error)||actual[i]!=written||!numeric(next[i],actual[i],exact,maximum_error)){
                    std::cerr<<"stage="<<std::quoted(name)<<" mode="<<mode<<" element="<<i<<" expected_bits="<<expected[i].primary_bits
                        <<" actual_bits="<<got.primary_bits<<" expected_flags="<<expected[i].flags<<" actual_flags="<<got.flags
                        <<" expected_diagnostic="<<expected[i].diagnostic<<" actual_diagnostic="<<got.diagnostic<<'\n';
                    throw std::runtime_error("La composición de aplicación no coincide con su política/oráculo.");}
                if(expected[i].flags&smo_rule_unresolved)++omitted;
                if(usable(got)&&got.primary_bits==0)++valid_zeros;++verified;
            }
            target.mirror[mode]=actual;
        }
        for(unsigned mode=1;mode<3;++mode)for(std::size_t i=0;i<count;++i)
            require(equal_result(results[0][i],results[mode][i],exact,maximum_error),"SMO y control de la MISMA política no coinciden.");
        target.mirror[oracle]=std::move(next);++serial;
    }
    void print()const{
        for(const auto& [name,row]:samples){std::cout<<"application_stage="<<std::quoted(name)<<" elements="<<row.elements
                <<" ordinary_same_policy_ms="<<median(row.gpu_ms[0])<<" SMO_generic_ms="<<median(row.gpu_ms[1])
                <<" SMO_specialized_ms="<<median(row.gpu_ms[2])<<" samples="<<row.gpu_ms[0].size()<<'\n';
            for(unsigned mode=0;mode<3;++mode){std::cout<<"GPU samples["<<mode<<"]=";for(const auto sample:row.gpu_ms[mode])std::cout<<' '<<sample;std::cout<<'\n';}}
        std::cout<<"application_verified="<<verified<<" unresolved_destinations_preserved="<<omitted<<" legitimate_zero_writes="<<valid_zeros<<" maximum_support_view_error="<<maximum_error<<'\n';
    }
};

void applications(Applications& lab,unsigned frames){
    lab.channel("position",[](std::size_t i){return static_cast<float>(i%16)*.25f;});
    lab.channel("velocity",[](std::size_t i){return static_cast<float>(static_cast<int>(i%7)-3)*.125f;});
    lab.channel("force",[](std::size_t i){return static_cast<float>(static_cast<int>(i%5)-2)*.125f;});
    lab.channel("kick",[](std::size_t){return 0.f;});lab.channel("displacement",[](std::size_t){return 0.f;});
    for(unsigned frame=0;frame<frames;++frame){
        lab.stage("particles/gate-zero-is-zero","kick",[&](std::size_t i,unsigned m){return smo_rule_input(Op::multiply,lab.value("force",i,m),(i+frame)%4?1.f:0.f);});
        lab.stage("particles/acceleration-dt","kick",[&](std::size_t i,unsigned m){return smo_rule_input(Op::multiply,lab.value("kick",i,m),.125f);});
        lab.stage("particles/integrate-velocity","velocity",[&](std::size_t i,unsigned m){return smo_rule_input(Op::add,lab.value("velocity",i,m),lab.value("kick",i,m));});
        lab.stage("particles/velocity-dt","displacement",[&](std::size_t i,unsigned m){return smo_rule_input(Op::multiply,lab.value("velocity",i,m),.125f);});
        lab.stage("particles/integrate-position","position",[&](std::size_t i,unsigned m){return smo_rule_input(Op::add,lab.value("position",i,m),lab.value("displacement",i,m));});
    }
    lab.channel("passthrough",[](std::size_t i){return static_cast<float>(static_cast<int>(i%7)-3)*.25f;});
    lab.channel("normalised",[](std::size_t i){return static_cast<float>(i%5)*.5f;});
    for(unsigned frame=0;frame<frames;++frame){
        lab.stage("animation/zero-means-passthrough-NOT-skipped-write","passthrough",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("passthrough",i,m),(i+frame)%3?1.f:0.f,smo_rule_multiply_zero_conserves);});
        lab.stage("velocity/missing-divisor-means-no-update","normalised",[&](std::size_t i,unsigned m){
            const float divisor=(i+frame)%4==0?0.f:(i%2?1.f:2.f);
            return smo_rule_input(Op::divide,lab.value("normalised",i,m),divisor,smo_rule_divide_zero_unresolved);});
        lab.stage("animation/compose-preserved-or-written-value","position",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::add,lab.value("position",i,m),lab.value("normalised",i,m));});
    }
    lab.channel("flagged-magnitude",[](std::size_t i){return i%2?-2.f:2.f;});
    lab.channel("signed-coordinate",[](std::size_t i){return i%2?-2.f:2.f;});
    for(unsigned frame=0;frame<frames;++frame){
        lab.stage("tagged-magnitude/negative-is-absorbing","flagged-magnitude",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("flagged-magnitude",i,m),(i+frame)%2?-.5f:.5f);});
        lab.stage("signed-coordinate/ordinary-sign-contract","signed-coordinate",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("signed-coordinate",i,m),(i+frame)%2?-.5f:.5f,smo_rule_conventional_sign);});
    }
    std::cout<<"Different contracts: tagged magnitudes preserve a negative flag; ordinary coordinates reverse orientation. Their different outputs are NOT a speed comparison.\n";
    lab.channel("direct-envelope",[](std::size_t){return 7.f;});lab.channel("inverse-envelope",[](std::size_t){return 7.f;});
    lab.channel("composed-envelope",[](std::size_t){return 7.f;});
    constexpr std::array<float,7> exponents{-3,-2,-1,0,1,2,3};
    for(const U profile:{0u,16u,32u,48u})for(unsigned frame=0;frame<frames;++frame){
        lab.stage("envelope/pdir-profile-"+std::to_string(profile),"direct-envelope",[&](std::size_t i,unsigned){return smo_rule_input(Op::direct_power,i%2?2.f:.5f,exponents[(i+frame)%exponents.size()],profile);});
        lab.stage("envelope/pinv-profile-"+std::to_string(profile),"inverse-envelope",[&](std::size_t i,unsigned){return smo_rule_input(Op::inverted_power,i%2?2.f:.5f,exponents[(i+frame)%exponents.size()],profile);});
        lab.stage("envelope/compose-profile-"+std::to_string(profile),"composed-envelope",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("direct-envelope",i,m),lab.value("inverse-envelope",i,m),profile);});
    }
    for(const U profile:{0u,16u})for(unsigned frame=0;frame<frames;++frame)
        lab.stage("envelope/explicit-zero-alternative-profile-"+std::to_string(profile),"direct-envelope",[&](std::size_t i,unsigned){
            return smo_rule_input(Op::direct_power,i%2?2.f:.5f,0,profile,1);});
    // A bounded audio-DSP data path, not playback or narration: actual GPU
    // samples retain their signed PCM meaning. A zero gain is a numerical
    // mute, so it must overwrite a previously nonzero destination with zero.
    constexpr std::array<float,8> pcm{-.75f,-.5f,-.25f,0.f,.25f,.5f,.75f,0.f};
    constexpr std::array<float,3> gains{0.f,.5f,1.f};
    lab.channel("dsp-pcm",[&](std::size_t i){return pcm[i%pcm.size()];});
    for(const auto name:{"dsp-gained","dsp-phase","dsp-tagged-phase","dsp-envelope-direct",
        "dsp-envelope-inverse","dsp-envelope-gain","dsp-envelope-signal"})
        lab.channel(name,[](std::size_t){return .75f;});
    std::size_t phase_differences=0,dsp_samples=0,dsp_mutes=0;
    for(unsigned frame=0;frame<frames;++frame){
        lab.stage("DSP/signed-PCM-gain-zero-mutes","dsp-gained",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-pcm",i,m),gains[(i+frame)%gains.size()],smo_rule_conventional_sign);});
        lab.stage("DSP/ordinary-phase-inversion","dsp-phase",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-gained",i,m),-1.f,smo_rule_conventional_sign);});
        lab.stage("DSP/tagged-magnitude-is-NOT-PCM-phase-inversion","dsp-tagged-phase",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-gained",i,m),-1.f);});
        lab.stage("DSP/positive-direct-envelope","dsp-envelope-direct",[&](std::size_t i,unsigned){
            return smo_rule_input(Op::direct_power,.5f,static_cast<float>(1+(i+frame)%3));});
        lab.stage("DSP/positive-inverse-envelope","dsp-envelope-inverse",[&](std::size_t i,unsigned){
            return smo_rule_input(Op::inverted_power,2.f,static_cast<float>(2+(i+frame)%3));});
        lab.stage("DSP/compose-envelope-and-gain","dsp-envelope-gain",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-envelope-direct",i,m),gains[(i+frame)%gains.size()],smo_rule_conventional_sign);});
        lab.stage("DSP/compose-second-envelope","dsp-envelope-gain",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-envelope-gain",i,m),lab.value("dsp-envelope-inverse",i,m),smo_rule_conventional_sign);});
        lab.stage("DSP/apply-composed-envelope-to-signed-PCM","dsp-envelope-signal",[&](std::size_t i,unsigned m){
            return smo_rule_input(Op::multiply,lab.value("dsp-pcm",i,m),lab.value("dsp-envelope-gain",i,m),smo_rule_conventional_sign);});
        for(unsigned mode=0;mode<3;++mode)for(std::size_t i=0;i<lab.count;++i){
            const auto sample=lab.value("dsp-gained",i,mode),phase=lab.value("dsp-phase",i,mode),
                tagged=lab.value("dsp-tagged-phase",i,mode),enveloped=lab.value("dsp-envelope-signal",i,mode);
            require(phase==-sample,"El contrato de PCM firmado no invirtió correctamente la fase.");
            require(std::abs(sample)<=.75f&&std::abs(phase)<=.75f&&std::abs(enveloped)<=.75f,
                "La cadena DSP salió de su dominio PCM acotado.");
            if(gains[(i+frame)%gains.size()]==0){require(sample==0&&enveloped==0,"Mute conservó muestras antiguas en vez de escribir cero.");++dsp_mutes;}
            if(sample<0){require(phase>0&&tagged<0,"Se ocultó el contrato distinto del signo absorbente para muestras negativas.");++phase_differences;}
            ++dsp_samples;
        }
    }
    require(dsp_mutes>0&&phase_differences>0,"La carga DSP no ejerció mute ni inversión de muestras negativas.");
    std::cout<<"DSP_GPU_samples="<<dsp_samples<<" mute_zero_samples="<<dsp_mutes<<" absorbing_vs_PCM_phase_differences="<<phase_differences
        <<"; signed dyadic PCM [-.75,.75], gains 0/.5/1, two composed GPU envelopes and ordinary phase inversion verified. "
        <<"The absorbing-sign alternative is a DIFFERENT contract, not a PCM regression or interchangeable speed result. "
        <<"Data samples only: no WAV, narrator, listening validation, audio-device latency or full-engine FPS measurement.\n";
    lab.channel("rational-view",[](std::size_t){return 9.f;});
    for(unsigned frame=0;frame<frames;++frame)lab.stage("exact-rational/one-third-and-one-sixth","rational-view",[&](std::size_t i,unsigned){
        SmoRuleInput item;item.operation=U(Op::base12_rational);item.packed_left=smo_rule_pack12(1);
        item.packed_right=smo_rule_pack12((i+frame)%2?3:6);return item;},false);
    // Packed/rational fields, not the rounded support view, are authoritative.
    std::cout<<"Fractions: 1/3 = 0.4(base12), 1/6 = 0.2(base12); reduced numerator/denominator verified exactly. "
        <<"This bounded ABI is NOT the separate full F12 engine.\n";
}

struct PolicyResult {U value=smo_rule_nan,flags=0,diagnostic=0,version=0;};
static_assert(sizeof(PolicyResult)==16);
PolicyResult policy_reference(SmoRuleInput item,U zero_policy,U previous_version){
    PolicyResult result;result.version=previous_version;
    if(item.operation!=U(Op::multiply)&&item.operation!=U(Op::divide)){result.diagnostic=smo_rule_invalid_operation;return result;}
    if(zero_policy>2||item.profile>1){result.diagnostic=smo_rule_invalid_profile;return result;}
    if((item.left_bits&0x7f800000u)==0x7f800000u||(item.right_bits&0x7f800000u)==0x7f800000u){result.diagnostic=smo_rule_nonfinite_input;return result;}
    const auto tiny=[](U value){return (value&0x7f800000u)==0&&(value&0x007fffffu)!=0;};
    if(tiny(item.left_bits)||tiny(item.right_bits)){result.diagnostic=smo_rule_subnormal;return result;}
    const float a=real(item.left_bits),b=real(item.right_bits);
    const bool zero=item.operation==U(Op::multiply)?a==0||b==0:b==0;
    if(zero&&zero_policy==2){result.flags=2;return result;}
    if(zero){const float other=item.operation==U(Op::multiply)?(a==0?b:a):a;
        result.value=bits(zero_policy==1?other:0.f);if(real(result.value)==0)result.value=0;}
    else{const auto numerical=smo_rule_reference(item);if(!usable(numerical)){result.diagnostic=numerical.diagnostic;return result;}result.value=numerical.primary_bits;}
    result.flags=1;result.version=previous_version+1;return result;
}
void skipped_writes(SmoCompute& gpu,std::size_t count,unsigned frames){
    std::array<SmoKernel,3> shaders;for(unsigned mode=0;mode<3;++mode)shaders[mode]=gpu.kernel(application_kernel(mode));
    const auto input=gpu.buffer(count*sizeof(SmoRuleInput));std::array<SmoBuffer,3> output{},destination{},version{};
    for(unsigned mode=0;mode<3;++mode){output[mode]=gpu.buffer(count*sizeof(PolicyResult));destination[mode]=gpu.buffer(count*4);version[mode]=gpu.buffer(count*4);}
    std::size_t verified=0,omissions=0,zero_writes=0,diagnostics_preserved=0;
    for(const auto operation:{Op::multiply,Op::divide})for(U policy=0;policy<3;++policy)for(U sign=0;sign<2;++sign){
        std::vector<U> previous(count),versions(count);for(std::size_t i=0;i<count;++i){previous[i]=bits(i%3==0?0.f:7.f);versions[i]=U(i%11)+31;}
        for(unsigned mode=0;mode<3;++mode){gpu.write(destination[mode],bytes(previous));gpu.write(version[mode],bytes(versions));}
        std::array<std::vector<double>,3> samples;
        for(unsigned frame=0;frame<frames;++frame){
            std::vector<SmoRuleInput> items;std::vector<PolicyResult> expected;items.reserve(count);expected.reserve(count);
            for(std::size_t i=0;i<count;++i){const auto pattern=(i+frame)%16;
                constexpr std::array<float,8> a{0,2,0,-2,-2,2,-2,0},b{0,0,2,0,-2,-2,2,-2};
                auto item=smo_rule_input(operation,a[pattern%8],b[pattern%8],sign);
                // Both operands already exist in the input ABI: omission is
                // NOT lazy expression evaluation. It must not hide an invalid
                // precomputed operand merely because the other one is zero.
                if(pattern>=8&&pattern<=10){
                    constexpr std::array<U,3> invalid{0x7fc00000u,0x7f800000u,1u};
                    item.left_bits=invalid[pattern-8];item.right_bits=0;
                }
                items.push_back(item);expected.push_back(policy_reference(items.back(),policy,versions[i]));}
            gpu.write(input,bytes(items));
            for(unsigned position=0;position<3;++position){const unsigned mode=(frame+position)%3;
                SmoDispatch dispatch;dispatch.kernel=shaders[mode];dispatch.buffers={input,output[mode],destination[mode],version[mode]};
                dispatch.constants={U(count),U(operation),policy,sign};dispatch.groups={(U(count)+63)/64,1,1};
                samples[mode].push_back(gpu.execute(std::span(&dispatch,1),true));
                const auto got=read<PolicyResult>(gpu,output[mode],count);
                const auto values=read<U>(gpu,destination[mode],count),generation=read<U>(gpu,version[mode],count);
                for(std::size_t i=0;i<count;++i){const auto& want=expected[i];const U value=want.flags==1?want.value:previous[i];
                    require(got[i].flags==want.flags&&got[i].diagnostic==want.diagnostic&&got[i].version==want.version&&
                        got[i].value==want.value&&values[i]==value&&generation[i]==want.version,"Cero, conservación y omisión GPU no son distinguibles correctamente.");
                    if(want.flags==2){++omissions;require(generation[i]==versions[i]&&values[i]==previous[i],"La operación omitida escribió el destino o su versión.");}
                    if(want.flags==1&&want.value==0){++zero_writes;require(generation[i]==versions[i]+1,"Un cero válido se confundió con operación omitida.");}
                    if(want.diagnostic!=0){++diagnostics_preserved;require(want.flags==0&&generation[i]==versions[i]&&values[i]==previous[i],
                        "La política cero ocultó un operando inválido o modificó el estado tras el diagnóstico.");}
                    ++verified;
                }
            }
            for(std::size_t i=0;i<count;++i){if(expected[i].flags==1)previous[i]=expected[i].value;versions[i]=expected[i].version;}
        }
        std::cout<<"isolated_zero_policy operation="<<smo_rule_operation_name(operation)<<" policy="<<policy<<" sign="<<sign
            <<" ordinary_same_policy_ms="<<median(samples[0])<<" SMO_dynamic_ms="<<median(samples[1])<<" SMO_uniform_ms="<<median(samples[2])<<'\n';
    }
    require(omissions>0&&zero_writes>0&&diagnostics_preserved>0,"No se ejercieron omisión, cero legítimo y validación anterior a omitir.");
    std::cout<<"private_policy_verified="<<verified<<" real_omissions="<<omissions<<" numerical_zero_writes="<<zero_writes
        <<" invalid_operands_preserved="<<diagnostics_preserved
        <<"; destination AND write-version checked after every step; no public SMO ABI/profile changed.\n"
        <<"Omission skips the destination write, NOT evaluation of input expressions: both operands already exist in the ABI. "
        <<"NaN/Inf/subnormal operands are diagnosed before the zero policy; native division by zero is never executed.\n";
}

std::string canonical(std::string_view name){std::string result;for(const unsigned char c:name)if((c>='a'&&c<='z')||(c>='0'&&c<='9'))result+=char(c);else if(c>='A'&&c<='Z')result+=char(c-'A'+'a');return result;}
bool same_gpu(const SmoComputeInfo& a,const SmoComputeInfo& b){const auto x=canonical(a.adapter),y=canonical(b.adapter);
    return a.software==b.software&&(!a.vendor_id||!b.vendor_id||a.vendor_id==b.vendor_id)&&(!a.device_id||!b.device_id||a.device_id==b.device_id)&&
        x.size()>4&&y.size()>4&&(x.find(y)!=x.npos||y.find(x)!=y.npos);}
SmoComputeInfo run_gpu(SmoComputeApi api,SmoComputeOptions options,std::size_t count,unsigned frames,const Properties& properties){
    auto compute=create_smo_compute(api,options);const auto info=compute->info();
    std::cout<<"API="<<std::quoted(info.api)<<" GPU="<<std::quoted(info.adapter)<<" driver="<<std::quoted(info.driver)<<" software="<<info.software<<'\n';
    require(!info.api.empty()&&!info.adapter.empty()&&info.software==options.software,"La prueba no identifica el backend GPU solicitado.");
    std::array<SmoKernel,3> shaders;for(unsigned mode=0;mode<3;++mode)shaders[mode]=compute->kernel(smo_rule_gpu_kernel(mode));
    {Applications conformance(*compute,shaders,properties.witnesses.size());conformance.channel("witness",[](std::size_t){return 7.f;});
        conformance.stage("CPU-law-witnesses-on-GPU","witness",[&](std::size_t i,unsigned){return properties.witnesses[i];},true,false);}
    Applications lab(*compute,shaders,count);applications(lab,frames);lab.print();skipped_writes(*compute,count,frames);
    std::cout<<"rule_source_identity="<<lgaed::state::sha256(source("smo_shader_hlsl.inc")+source("smo_shader_glsl.inc")+source("smo_arithmetic.inc")+source("smo_rules_common.inc"))
        <<" application_policy_identity="<<lgaed::state::sha256(source("smo_shader_hlsl.inc")+source("smo_shader_glsl.inc")+source("smo_arithmetic.inc")+source("smo_rule_application.inc"))<<'\n';
    std::cout<<"PASS: same-policy controls, exact metadata/no-op and composed application states. Timings cover only actual GPU dispatch stages; "
        <<"host routing, transfers, compilation, oracle and readback excluded. No full-engine FPS or radix12 hardware claim.\n";
    if(info.software)std::cout<<"WARP validates behavior only; its timings are not physical GPU performance.\n";
    return info;
}
U argument(std::string_view text,U minimum,U maximum){std::size_t used=0;const auto value=std::stoul(std::string(text),&used);
    require(used==text.size()&&value>=minimum&&value<=maximum,"Argumento fuera de los límites de esta prueba acotada.");return static_cast<U>(value);}
}

int main(int argc,char** argv){try{
    Properties properties;properties.run();if(argc==2&&std::string_view(argv[1])=="cpu")return 0;
    if(argc==1){SmoComputeOptions device;device.software=true;(void)run_gpu(SmoComputeApi::direct3d11,device,64,3,properties);return 0;}
    require(argc>=2&&argc<=5&&std::string_view(argv[1])=="hardware",
        "Uso: lgaed_smo_rule_application_tests [cpu | hardware [d3d11|d3d12|vulkan|opengl|all] [items64..65536] [steps3..33]]");
    const auto selector=argc>2?std::string_view(argv[2]):std::string_view("d3d11");
    const auto count=argc>3?argument(argv[3],64,65536):4096u,frames=argc>4?argument(argv[4],3,33):5u;
    constexpr std::array<SmoComputeApi,4> apis{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl};
    std::optional<SmoComputeInfo> baseline;bool failed=false;
    for(const auto api:apis){if(selector!="all"&&api!=smo_compute_api(selector))continue;
        try{SmoComputeOptions device;if(baseline)device.adapter_name=baseline->adapter;const auto current=run_gpu(api,device,count,frames,properties);
            if(baseline)require(same_gpu(*baseline,current),"La comparación entre APIs cambió de GPU.");else baseline=current;
        }catch(const std::exception& error){failed=true;std::cerr<<"API FAIL "<<smo_compute_api_name(api)<<": "<<error.what()<<'\n';}}
    require(baseline.has_value(),"Ninguna API completó la prueba.");return failed?1:0;
}catch(const std::exception& error){std::cerr<<"SMO application experiment failed: "<<error.what()<<'\n';return 1;}}

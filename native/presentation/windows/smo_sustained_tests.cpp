#include "presentation/windows/smo_scene.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed::presentation;
constexpr std::array<SmoComputeApi,4> apis{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl};
constexpr std::array<std::uint64_t,4> checkpoint_frames{1,32,64,128};
constexpr double tolerance=2e-4;

void check(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
bool near(float a,float b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(static_cast<double>(a)-b)<=tolerance*std::max(1.0,std::abs(static_cast<double>(a)));}
template<class Function>void rejects(Function action,std::string_view message){bool rejected=false;
    try{action();}catch(const std::invalid_argument&){rejected=true;}check(rejected,message);}
std::array<float,8> components(const SmoSceneBody& body){return std::bit_cast<std::array<float,8>>(body);}
const SmoSceneCheckpoint* checkpoint(const SmoSustainedReport& report,std::uint64_t frame){
    const auto found=std::ranges::find_if(report.checkpoints,[&](const auto& value){return value.frame==frame;});
    return found==report.checkpoints.end()?nullptr:&*found;
}
double median(std::vector<double> values){if(values.empty())return std::numeric_limits<double>::quiet_NaN();
    std::ranges::sort(values);const auto middle=values.size()/2;return values.size()%2?values[middle]:(values[middle-1]+values[middle])/2;
}
std::string adapter_key(std::string_view name){std::string result;for(const unsigned char c:name)if(std::isalnum(c))result.push_back(static_cast<char>(std::tolower(c)));return result;}
bool same_adapter(const SmoComputeInfo& a,const SmoComputeInfo& b){if(a.software!=b.software)return false;
    if(a.vendor_id&&b.vendor_id&&a.vendor_id!=b.vendor_id)return false;
    if(a.device_id&&b.device_id&&a.device_id!=b.device_id)return false;
    const auto left=adapter_key(a.adapter),right=adapter_key(b.adapter);
    return left.size()>4&&right.size()>4&&(left.find(right)!=left.npos||right.find(left)!=right.npos);
}
void reference_regressions(){
    const auto initial=smo_scene_initial(16),again=smo_scene_initial(16);
    check(initial.size()==16&&components(initial.front())==components(again.front())&&
        near(initial.front().position[0],-15.5f*.27f)&&near(initial.front().position[1],.1f)&&
        near(initial.front().position[2],2)&&near(initial.front().radius,.18f),"La escena inicial FP32 dejó de ser determinista o cambió su geometría.");
    rejects([]{(void)smo_scene_initial(2049);},"El oráculo admitió más de 2048 cuerpos.");
    const auto moved=smo_scene_step_reference(initial,1);float contacts=0;
    for(std::size_t i=0;i<moved.size();++i){contacts+=moved[i].contacts;for(const auto value:components(moved[i]))check(std::isfinite(value),"El oráculo de contacto produjo valores no finitos.");}
    check(contacts>0,"La escena real inicial no contiene los contactos declarados.");
    SmoSceneBody isolated;isolated.position={0,1,2};isolated.radius=.2f;
    auto falling=smo_scene_step_reference(std::span(&isolated,1),1);
    check(near(falling.front().velocity[1],-1.6f/120.0f)&&falling.front().position[1]<isolated.position[1]&&falling.front().contacts==0,
        "La referencia no integra la gravedad real con dt 1/120.");
    isolated.position[1]=-3;isolated.velocity[1]=-1;const auto bounce=smo_scene_step_reference(std::span(&isolated,1),1);
    check(near(bounce.front().position[1],-2.8f)&&bounce.front().velocity[1]>0&&bounce.front().contacts==1,
        "El suelo no conserva la restitución y el contacto del shader.");
    isolated.position={0,0,2};isolated.velocity={};isolated.radius=.5f;SmoSceneWorkload pixel{1,1,1,1};
    const auto hit=smo_scene_ray_reference(std::span(&isolated,1),0,pixel);
    check(near(hit[3],7.5f)&&hit[0]>.025f&&hit[1]>.04f&&hit[2]>.07f,"El rayo primario real no intersectó la esfera y su iluminación.");
    isolated.position[0]=10;const auto miss=smo_scene_ray_reference(std::span(&isolated,1),0,pixel);
    check(near(miss[0],.025f)&&near(miss[1],.04f)&&near(miss[2],.07f)&&miss[3]==-1,"El rayo que falla inventó geometría.");
    rejects([&]{(void)smo_scene_ray_reference(std::span(&isolated,1),1,pixel);},"El oráculo admitió un píxel fuera de la imagen.");
}
void print_report(const SmoSustainedReport& report){
    std::cout<<std::setprecision(10)<<"api="<<report.device.api<<" adapter="<<std::quoted(report.device.adapter)<<" driver="<<std::quoted(report.device.driver)
        <<" software="<<report.device.software<<" vendor_id="<<report.device.vendor_id<<" device_id="<<report.device.device_id
        <<" gpu_timestamps="<<report.device.timestamps<<'\n'
        <<"source_identity="<<report.identity<<" width="<<report.workload.width<<" height="<<report.workload.height<<" bodies="<<report.workload.bodies
        <<" group_size="<<report.workload.group_size<<" measured_seconds="<<report.elapsed_seconds<<" frames="<<report.frames<<" dynamic_frames="<<report.dynamic_frames
        <<" contacts="<<report.contacts<<" state_mismatches="<<report.state_mismatches<<" pixel_mismatches="<<report.pixel_mismatches<<" invalid_values="<<report.invalid_values
        <<" max_error="<<report.max_error<<" equivalent="<<report.equivalent<<" target_reached="<<report.target_reached<<'\n';
    std::cout<<"within_api_state_AB="<<report.state_pair_mismatches[0]<<" within_api_state_AC="<<report.state_pair_mismatches[1]
        <<" within_api_pixels_AB="<<report.pixel_pair_mismatches[0]<<" within_api_pixels_AC="<<report.pixel_pair_mismatches[1]<<'\n';
    constexpr std::array<const char*,3> modes{"A ordinary/all candidates","B ordinary/group culling","C SMO/group culling"};
    for(std::size_t mode=0;mode<3;++mode){const auto& value=report.metrics[mode];
        std::cout<<modes[mode]<<": GPU_ms median="<<value.median_ms<<" p95="<<value.p95_ms<<" p99="<<value.p99_ms<<" valid="<<value.valid
            <<" samples="<<value.gpu_ms.size()<<" CPU_submit_wait_median_ms="<<median(value.cpu_ms)<<" CPU_samples="<<value.cpu_ms.size()
            <<" actual_ray_sphere_tests="<<report.ray_sphere_tests[mode];
        if(mode)std::cout<<" actual_ray_box_tests="<<report.ray_box_tests[mode-1];std::cout<<'\n';
    }
    for(const auto& value:report.checkpoints){const auto hits=std::ranges::count_if(value.pixels,[](const auto& color){return color[3]>=0;});
        std::cout<<"checkpoint="<<value.frame<<" states="<<value.bodies.size()<<" RGB_depth_pixels="<<value.pixels.size()<<" checkpoint_hits="<<hits<<'\n';}
    std::cout<<report.conclusion<<"\nLos tiempos son consultas GPU del trabajo y espera/envío CPU; no son FPS de la aplicación ni una prueba universal de SMO.\n";
}
void verify_report(const SmoSustainedReport& report,const SmoSustainedOptions& options){
    check(!report.device.api.empty()&&!report.device.adapter.empty()&&!report.identity.empty(),"Falta la identidad real de API, GPU o fuente del cálculo.");
    check(report.device.software==options.device.software,"Se presentó software como rendimiento físico o se ignoró la petición WARP.");
    check(options.calibrate||report.workload==options.workload,"Una API cambió silenciosamente la carga común.");
    check(report.frames>=2&&report.dynamic_frames>0&&report.contacts>0,"La prueba no produjo fotogramas dinámicos con contactos reales.");
    check(std::isfinite(report.elapsed_seconds)&&report.elapsed_seconds>=options.seconds,"La prueba terminó antes de su duración medida solicitada.");
    check(report.equivalent&&report.state_mismatches==0&&report.pixel_mismatches==0&&report.invalid_values==0&&std::isfinite(report.max_error),
        "Las comprobaciones GPU de todos los estados/píxeles A/B/C no fueron equivalentes o contienen valores inválidos.");
    const auto consistent=[](auto combined,const auto& pair){return combined>=std::max(pair[0],pair[1])&&combined<=pair[0]+pair[1];};
    check(consistent(report.state_mismatches,report.state_pair_mismatches)&&consistent(report.pixel_mismatches,report.pixel_pair_mismatches),
        "Los diagnósticos A/B y A/C no corresponden a la comparación GPU completa.");
    check(report.ray_sphere_tests[0]>0&&report.ray_sphere_tests[1]>0&&report.ray_sphere_tests[2]>0&&report.ray_box_tests[0]>0&&report.ray_box_tests[1]>0,
        "No se registraron intersecciones y límites reales de ray tracing.");
    for(const auto& value:report.metrics){
        check(value.gpu_ms.size()==report.frames&&value.cpu_ms.size()==report.frames,"Faltan tiempos de fotogramas realmente ejecutados.");
        const auto valid=!value.gpu_ms.empty()&&std::ranges::all_of(value.gpu_ms,[](double time){return std::isfinite(time)&&time>0;});
        check(value.valid==valid,"Una consulta GPU inválida se presentó como una medida válida.");
        check(!valid||report.device.timestamps,"Se presentaron tiempos GPU sin un reloj GPU disponible.");
        check(std::ranges::all_of(value.cpu_ms,[](double time){return std::isfinite(time)&&time>=0;}),"La espera/envío CPU no conserva sus medidas reales.");
        if(valid)check(std::isfinite(value.median_ms)&&value.median_ms>0&&value.p95_ms>=value.median_ms&&value.p99_ms>=value.p95_ms,
            "Los percentiles GPU no corresponden a muestras válidas ordenadas.");
        else check(std::isnan(value.median_ms)&&std::isnan(value.p95_ms)&&std::isnan(value.p99_ms),"Se sustituyó el reloj GPU inválido por un valor inventado.");
    }
    const auto* first=checkpoint(report,1);check(first!=nullptr,"Falta el primer estado real para contrastar el oráculo independiente.");
    const auto expected=smo_scene_step_reference(smo_scene_initial(report.workload.bodies),1);
    check(first->bodies.size()==expected.size(),"El checkpoint omitió cuerpos del primer fotograma.");
    for(std::size_t body=0;body<expected.size();++body){const auto actual=components(first->bodies[body]),reference=components(expected[body]);
        for(std::size_t k=0;k<actual.size();++k)check(near(reference[k],actual[k]),"El primer estado GPU diverge del oráculo FP32 independiente.");}
    const auto pixels=static_cast<std::uint64_t>(report.workload.width)*report.workload.height;
    check(first->pixels.size()==pixels,"El checkpoint omitió píxeles RGB/profundidad.");
    if(pixels<=4096&&report.workload.bodies<=64){for(std::uint32_t pixel=0;pixel<pixels;++pixel){
        const auto reference=smo_scene_ray_reference(expected,pixel,report.workload);
        for(std::size_t k=0;k<4;++k)check(near(reference[k],first->pixels[pixel][k]),"Un píxel real GPU diverge del oráculo FP32 independiente.");}}
    if(options.seconds>=60)for(const auto frame:checkpoint_frames)check(checkpoint(report,frame)!=nullptr,"La ejecución sostenida omitió un checkpoint común 1/32/64/128.");
}
bool cross_compare(const SmoSustainedReport& baseline,const SmoSustainedReport& other,bool sustained){
    if(!same_adapter(baseline.device,other.device)||baseline.workload!=other.workload||baseline.identity!=other.identity){
        std::cerr<<"Cross-API REJECTED: distinta GPU, carga o fuente entre "<<baseline.device.api<<" y "<<other.device.api<<".\n";return false;}
    std::uint64_t state_errors=0,pixel_errors=0,states=0,pixels=0,compared=0;double max_error=0;
    for(const auto frame:checkpoint_frames){const auto* left=checkpoint(baseline,frame);const auto* right=checkpoint(other,frame);
        if(!left||!right){if(sustained){std::cerr<<"Cross-API missing checkpoint "<<frame<<'\n';return false;}continue;}
        if(left->bodies.size()!=right->bodies.size()||left->pixels.size()!=right->pixels.size()){
            std::cerr<<"Cross-API checkpoint shape mismatch "<<frame<<'\n';return false;}
        ++compared;
        for(std::size_t i=0;i<left->bodies.size();++i){const auto a=components(left->bodies[i]),b=components(right->bodies[i]);
            for(std::size_t k=0;k<8;++k){++states;if(!near(a[k],b[k]))++state_errors;
                if(std::isfinite(a[k])&&std::isfinite(b[k]))max_error=std::max(max_error,std::abs(static_cast<double>(a[k])-b[k]));}}
        for(std::size_t i=0;i<left->pixels.size();++i)for(std::size_t k=0;k<4;++k){++pixels;const auto a=left->pixels[i][k],b=right->pixels[i][k];
            if(!near(a,b))++pixel_errors;if(std::isfinite(a)&&std::isfinite(b))max_error=std::max(max_error,std::abs(static_cast<double>(a)-b));}
    }
    std::cout<<"cross_api="<<baseline.device.api<<" versus "<<other.device.api<<" common_checkpoints="<<compared<<" state_components="<<states
        <<" pixel_components="<<pixels<<" state_mismatches="<<state_errors<<" pixel_mismatches="<<pixel_errors<<" max_absolute_error="<<max_error
        <<" relative_tolerance="<<tolerance<<'\n';
    return compared>0&&state_errors==0&&pixel_errors==0;
}
SmoComputeApi api_argument(std::string_view name){
    if(name=="d3d11"||name=="direct3d11")return SmoComputeApi::direct3d11;
    if(name=="d3d12"||name=="direct3d12")return SmoComputeApi::direct3d12;
    if(name=="vulkan")return SmoComputeApi::vulkan;if(name=="opengl")return SmoComputeApi::opengl;
    throw std::invalid_argument("API esperada: d3d11, d3d12, vulkan, opengl o all.");
}
SmoSustainedOptions quick_options(){SmoSustainedOptions result;result.seconds=.08;result.warmup_seconds=.02;
    result.calibrate=false;result.workload={64,36,16,4};return result;}
int diagnostic(int argc,char** argv){
    check(argc<=7,"Uso: lgaed_smo_sustained_tests diagnose [all|API [pasos [cuerpos [ancho [alto]]]]]");
    const auto number=[](const char* value){std::size_t consumed=0;const auto parsed=std::stoull(value,&consumed);
        check(consumed==std::string_view(value).size()&&parsed<=std::numeric_limits<std::uint32_t>::max(),"Entero de diagnóstico inválido");
        return static_cast<std::uint32_t>(parsed);};
    const auto selector=argc>2?std::string_view(argv[2]):std::string_view("all");
    const auto frames=argc>3?number(argv[3]):1u;
    SmoSceneWorkload workload{1920,1080,2048,16};
    if(argc>4)workload.bodies=number(argv[4]);if(argc>5)workload.width=number(argv[5]);if(argc>6)workload.height=number(argv[6]);
    std::vector<SmoComputeApi> selected;
    if(selector=="all")selected.assign(apis.begin(),apis.end());else selected={SmoComputeApi::direct3d11,api_argument(selector)};
    std::cout<<"BEGIN diagnostic: identical raw inputs per integration/contact/ray phase, first exact and first tolerance differences; no performance claim.\n"
        <<"frames="<<frames<<" width="<<workload.width<<" height="<<workload.height<<" bodies="<<workload.bodies<<" group_size="<<workload.group_size<<std::endl;
    const auto report=diagnose_smo_scene(selected,workload,frames);
    for(std::size_t index=0;index<report.devices.size();++index){const auto& device=report.devices[index];
        std::cout<<"diagnostic_api["<<index<<"]="<<device.api<<" adapter="<<std::quoted(device.adapter)<<" driver="<<std::quoted(device.driver)<<'\n';
        check(same_adapter(report.devices.front(),device),"El diagnóstico requiere la misma GPU física en todas las APIs");}
    const auto print=[&](std::string_view kind,const SmoSceneDifference& value){
        constexpr std::array<const char*,8> bodies{"px","py","pz","radius","vx","vy","vz","contacts"};
        constexpr std::array<const char*,4> pixels{"R","G","B","distance"};
        constexpr std::array<const char*,24> contact{"delta.x","delta.y","delta.z","distance_squared","radii_sum","radii_squared","sqrt_distance","normal.x","normal.y","normal.z","penetration","correction_factor","contribution.x","contribution.y","contribution.z","running.x","running.y","running.z","contacts","active","unused20","unused21","unused22","unused23"};
        constexpr std::array<const char*,24> summary{"raw_correction.x","raw_correction.y","raw_correction.z","correction_length","limited_correction.x","limited_correction.y","limited_correction.z","limit_factor","final.px","final.py","final.pz","radius","final.vx","final.vy","final.vz","contacts","unused16","unused17","unused18","unused19","unused20","unused21","unused22","unused23"};
        const bool ray=value.phase=="rays-common-input",trace=value.phase=="contact-intermediate-body0";
        const auto component=trace?(value.element==workload.bodies?summary[value.component]:contact[value.component]):ray?pixels[value.component]:bodies[value.component];
        std::cout<<std::setprecision(10)<<kind<<" phase="<<value.phase<<" api="<<report.devices[value.api].api<<" frame="<<value.frame
            <<(trace?" neighbor_or_summary=":ray?" pixel=":" body=")<<value.element<<" component="<<component
            <<" baseline="<<value.expected<<" actual="<<value.actual<<" ulps="<<value.ulps<<" outside_2e-4="<<value.outside_tolerance
            <<" expected_bits=0x"<<std::hex<<std::setw(8)<<std::setfill('0')<<value.expected_bits<<" actual_bits=0x"<<std::setw(8)<<value.actual_bits
            <<std::dec<<std::setfill(' ')<<'\n';};
    for(const auto& value:report.first_exact)print("FIRST_EXACT_DIFFERENCE",value);
    for(const auto& value:report.first_outside_tolerance)print("FIRST_TOLERANCE_DIFFERENCE",value);
    std::cout<<"contact_trace_reproduces_production_stage2_bits="<<report.contact_trace_reproduced<<'\n';
    for(const auto& value:report.contact_trace)print("CONTACT_INTERMEDIATE_DIFFERENCE",value);
    for(std::size_t api=1;api<report.devices.size();++api)for(const auto phase:{"integration-common-input","contacts-common-input","rays-common-input","trajectory-accumulated"}){
        const auto matches=[&](const auto& value){return value.api==api&&value.phase==phase;};
        if(!std::ranges::any_of(report.first_exact,matches))std::cout<<"NO_EXACT_DIFFERENCE phase="<<phase<<" api="<<report.devices[api].api<<" through_frame="<<frames<<'\n';
        if(!std::ranges::any_of(report.first_outside_tolerance,matches))std::cout<<"NO_TOLERANCE_DIFFERENCE phase="<<phase<<" api="<<report.devices[api].api<<" through_frame="<<frames<<'\n';}
    std::cout<<"The canonical phase inputs apply only to this diagnostic; sustained trajectories, tolerances and GPU clocks are unchanged.\n";
    return report.first_outside_tolerance.empty()&&report.contact_trace_reproduced?0:1;
}
void warp_test(){auto options=quick_options();options.device.software=true;
    smo_compute_smoke(options.api,options.device);const auto report=run_smo_sustained(options);print_report(report);verify_report(report,options);
    std::cout<<"PASS: D3D11 WARP valida ejecución, gravedad/colisiones, rayos, sombras y equivalencia; no acredita rendimiento físico.\n";}
int hardware(bool sustained,std::string_view selector){
    const bool all=selector=="all";const auto selected=all?SmoComputeApi::direct3d11:api_argument(selector);
    auto common=quick_options();if(sustained){common.seconds=60;common.warmup_seconds=2;common.workload=SmoSceneWorkload{};}
    std::optional<SmoSustainedReport> baseline;bool failed=false;
    for(const auto api:apis){if(!all&&api!=selected)continue;
        try{auto options=common;options.api=api;options.calibrate=sustained&&(!all||api==SmoComputeApi::direct3d11);
            if(baseline){options.device.adapter_name=baseline->device.adapter;options.workload=baseline->workload;options.calibrate=false;}
            std::cout<<"BEGIN "<<smo_compute_api_name(api)<<" requested_seconds="<<options.seconds<<" warmup_seconds="<<options.warmup_seconds<<" calibrate="<<options.calibrate<<std::endl;
            smo_compute_smoke(api,options.device);auto report=run_smo_sustained(options);print_report(report);
            try{verify_report(report,options);}catch(const std::exception& error){failed=true;std::cerr<<"WITHIN-API FAILURE "<<report.device.api<<": "<<error.what()<<'\n';}
            if(!baseline)baseline=std::move(report);
            else if(!cross_compare(*baseline,report,sustained))failed=true;
        }catch(const std::exception& error){failed=true;std::cerr<<"API FAILURE "<<smo_compute_api_name(api)<<": "<<error.what()<<'\n';}
    }
    return failed?1:0;
}
}

int main(int argc,char** argv){try{
    reference_regressions();
    if(argc==1){warp_test();return 0;}
    const std::string_view action=argv[1];if(action=="diagnose")return diagnostic(argc,argv);check(argc<=3&&(action=="probe"||action=="sustained"),
        "Uso: lgaed_smo_sustained_tests [probe|sustained [d3d11|d3d12|vulkan|opengl|all]]");
    return hardware(action=="sustained",argc==3?std::string_view(argv[2]):std::string_view("all"));
}catch(const std::exception& error){std::cerr<<"SMO dynamic scene failure: "<<error.what()<<'\n';return 1;}}

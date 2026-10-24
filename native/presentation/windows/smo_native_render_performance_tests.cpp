#include "presentation/windows/smo_native_render.hpp"
#include "presentation/windows/smo_scene.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
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
using namespace lgaed::presentation;
using U=std::uint32_t;
using Clock=std::chrono::steady_clock;
std::size_t checks=0;
void check(bool condition,std::string_view message){++checks;if(!condition)throw std::runtime_error(std::string(message));}
std::string numeral(U value){constexpr std::string_view digits="0123456789#~";std::string text;do{text.push_back(digits[value%12u]);value/=12u;}while(value);std::ranges::reverse(text);return text;}
std::string eighth(int value){const bool negative=value<0;const auto magnitude=static_cast<unsigned>(negative?-value:value);check(magnitude<=8,"The authored benchmark coordinate left its exact eighth-unit fixture.");
    constexpr std::array<std::string_view,9> numbers{"0",".16",".3",".46",".6",".76",".9",".#6","1"};return (negative?"-":"")+std::string(numbers[magnitude]);}
std::string sixty_fourth(int value){check(value>=0&&value<=64,"The dense fixture coordinate must be nonnegative and at most one.");if(value==0)return "0";if(value==64)return "1";
    // n/64 == (27*n)/12^3 exactly. No unary expression or floating conversion
    // is needed in this compact source, keeping the existing node limit.
    auto fraction=numeral(static_cast<U>(value)*27u);fraction.insert(0,3u-fraction.size(),'0');return "."+fraction;}
std::string authored_scene(bool physical,bool dense){constexpr std::array<std::array<int,3>,8> corner{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}};
    constexpr std::array<std::array<U,3>,12> faces{{{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{3,7,6},{3,6,2},{0,4,7},{0,7,3},{1,2,6},{1,6,5}}};
    std::string source;const U cubes=dense?128u:(physical?16u:1u);for(U cube=0;cube<cubes;++cube){const int cx=dense?(static_cast<int>(cube%16u)*4+2):(physical?(static_cast<int>(cube%4u)*4-6):0),cy=dense?(static_cast<int>(cube/16u)*4+2):(physical?(static_cast<int>(cube/4u)*4-6):0),half=physical?1:4;
        for(const auto& point:corner){const auto x=dense?sixty_fourth(cx+point[0]):eighth(cx+point[0]*half),y=dense?sixty_fourth(cy+point[1]):eighth(cy+point[1]*half),z=dense?sixty_fourth(1+point[2]):eighth(point[2]*half);
            source+="vertice("+x+","+y+","+z+");\n";}
        for(const auto& face:faces)source+="cara("+numeral(cube*8u+face[0])+","+numeral(cube*8u+face[1])+","+numeral(cube*8u+face[2])+");\n";}
    // dt is a prepared, repeated pose-input trace in this rendering experiment,
    // not a measurement of elapsed real time or a simulated application FPS.
    source+="estado giro=0;al_actualizar{giro=dt;}al_dibujar{rotacion(giro/2,giro,0);color(.2,.8,1);dibujar();}";return source;}
std::array<U,256> pose_trace(){std::array<U,256> result{};for(U i=1;i<result.size();++i){const auto top=std::bit_width(i)-1u;result[i]=((top+123u)<<23u)|((i<<(23u-top))&0x7fffffu);}return result;}
void success(const SmoNativeGpuExecution& execution){const auto bad=std::ranges::find_if(execution.diagnostics,[](const auto& value){return value.code!=lgaed::smo::NativeError::none;});
    if(!execution.committed||bad!=execution.diagnostics.end()){std::string text="The common authored GPU program failed before its paired render.";if(bad!=execution.diagnostics.end())text+=" code="+std::to_string(static_cast<U>(bad->code))+" source="+std::to_string(bad->line)+":"+std::to_string(bad->column);throw std::runtime_error(text);}}
void exact(const SmoNativeImage& cached,const SmoNativeImage& uncached,std::uint64_t pair,U pose){check(cached.width==uncached.width&&cached.height==uncached.height&&cached.pixels.size()==uncached.pixels.size(),"A renderer mode changed dimensions or omitted pixels.");
    check(cached.diagnostic==uncached.diagnostic,"Cached and uncached deferred branch diagnostics differ.");check(cached.diagnostic==0,"The controlled finite-normal geometry produced a visible GPU error.");
    std::size_t differences=0,first=0;for(std::size_t i=0;i<cached.pixels.size();++i)if(cached.pixels[i]!=uncached.pixels[i]){if(!differences)first=i;++differences;}
    if(differences){std::ostringstream message;message<<"Cache equivalence FAILED before any speed claim: pair="<<pair<<" pose_bits=0x"<<std::hex<<pose<<std::dec<<" differences="<<differences<<" first_pixel="<<first<<" cached_BGRA=0x"<<std::hex<<cached.pixels[first]<<" uncached_BGRA=0x"<<uncached.pixels[first];throw std::runtime_error(message.str());}
    check(cached.pixel_hash==uncached.pixel_hash&&cached.colored_pixels==uncached.colored_pixels&&cached.colored_pixels>0,"Equal raw images changed their hash/visible geometry metadata.");}
struct Samples {
    std::vector<double> gpu,wall,readback;
    double gpu_sum=0;std::uint64_t discarded=0;bool valid=true;
    void add(const SmoNativeImage& image){const bool correct=std::isfinite(image.gpu_ms)&&image.gpu_ms>0&&std::isfinite(image.wall_ms)&&image.wall_ms>0&&std::isfinite(image.readback_ms)&&image.readback_ms>=0;
        valid=valid&&correct;if(correct)gpu_sum+=image.gpu_ms;
        if(gpu.size()<1048576u){gpu.push_back(image.gpu_ms);wall.push_back(image.wall_ms);readback.push_back(image.readback_ms);}else{++discarded;valid=false;}}
};
double quantile(const std::vector<double>& values,double fraction){if(values.empty()||std::ranges::any_of(values,[](double value){return !std::isfinite(value);}))return std::numeric_limits<double>::quiet_NaN();auto sorted=values;std::ranges::sort(sorted);
    const auto index=static_cast<std::size_t>(fraction*static_cast<double>(sorted.size()-1u));return sorted[index];}
void print_samples(std::string_view name,const Samples& samples){std::cout<<name<<" timing_valid="<<samples.valid<<" samples="<<samples.gpu.size()<<" discarded="<<samples.discarded
    <<" gpu_median_ms="<<quantile(samples.gpu,.5)<<" gpu_p95_ms="<<quantile(samples.gpu,.95)<<" wall_median_ms="<<quantile(samples.wall,.5)<<" readback_median_ms="<<quantile(samples.readback,.5)
    <<" measured_gpu_seconds="<<samples.gpu_sum/1000.0<<'\n';}
struct Result {lgaed::smo::NativeRealFrame32 initial;SmoNativeImage initial_image;std::uint64_t pairs=0;bool timing_valid=false;};
Result run(SmoNativeGpuOptions options,bool sustained,bool dense){const bool physical=!options.device.software;const U width=physical?512u:128u,height=physical?288u:72u,faces=dense?1536u:(physical?192u:12u),vertices=dense?1024u:(physical?128u:8u);
    const auto authored=authored_scene(physical,dense);check(authored.size()<=64u*1024u,"The dense fixture exceeded the existing64KiB source limit; no limits were changed.");
    auto program=SmoNativeGpuProgram::compile(authored,options);SmoNativeRenderer renderer(program.compute(),width,height);const auto trace=pose_trace();
    const auto initial=program.inspect_binary32().items.at(0);check(initial.vertices.size()==vertices&&initial.faces.size()==faces,"The authored source did not supply the declared polygon count.");
    const auto fixed_cached=renderer.render(program.frame_view()),fixed_uncached=renderer.render(program.frame_view(),false,SmoNativeRenderMode::reference_uncached);exact(fixed_cached,fixed_uncached,0,0);
    check(std::isnan(fixed_cached.gpu_ms)&&std::isnan(fixed_cached.wall_ms)&&std::isnan(fixed_cached.readback_ms),"Normal UI rendering unexpectedly enabled benchmark profiling.");
    std::cout<<"Render cache experiment api="<<program.info().api<<" adapter="<<std::quoted(program.info().adapter)<<" software="<<program.info().software<<" resolution="<<width<<'x'<<height
        <<" dense="<<dense<<" cubes="<<faces/12u<<" vertices="<<vertices<<" triangles="<<faces<<" source_bytes="<<authored.size()<<" source_identity="<<program.program().identity<<" fixed_pose_hash="<<fixed_cached.pixel_hash<<'\n';
    std::cout<<"Contract: same hardware binary32/scalar tree/finite-normal guards/scene/pose for each pair. Cached5-dispatch batch includes face prepass; uncached4-dispatch batch has no unused prepass. No radix or polygon-count reduction is being measured.\n";
    for(unsigned warmup=0;warmup<2;++warmup){const std::array<U,1> time{trace[warmup+1u]};success(program.step_binary32(time,false));const auto view=program.frame_view();SmoNativeImage a,b;
        if(warmup%2u){b=renderer.render(view,false,SmoNativeRenderMode::reference_uncached);a=renderer.render(view);}else{a=renderer.render(view);b=renderer.render(view,false,SmoNativeRenderMode::reference_uncached);}exact(a,b,warmup,time[0]);}
    Samples cached,uncached;std::uint64_t pairs=0;double producer_gpu_ms=0;bool producer_timing_valid=true;const auto begin=Clock::now();double elapsed=0;
    const std::uint64_t minimum_pairs=physical?9u:3u;
    do{const std::array<U,1> time{trace[static_cast<std::size_t>((pairs+3u)%trace.size())]};const auto execution=program.step_binary32(time,true);success(execution);
        producer_timing_valid=producer_timing_valid&&std::isfinite(execution.gpu_ms)&&execution.gpu_ms>0;if(std::isfinite(execution.gpu_ms)&&execution.gpu_ms>0)producer_gpu_ms+=execution.gpu_ms;
        const auto view=program.frame_view();SmoNativeImage a,b;
        if(pairs%2u){b=renderer.render(view,true,SmoNativeRenderMode::reference_uncached);a=renderer.render(view,true);}else{a=renderer.render(view,true);b=renderer.render(view,true,SmoNativeRenderMode::reference_uncached);}
        exact(a,b,pairs,time[0]);cached.add(a);uncached.add(b);++pairs;elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
    }while(pairs<minimum_pairs||(sustained&&elapsed<60.0));
    std::cout<<"Paired execution pairs="<<pairs<<" elapsed_wall_seconds="<<elapsed<<" warmup_pairs=2 every_pair_all_BGRA_exact=true common_producer_gpu_seconds="<<producer_gpu_ms/1000.0<<" producer_timing_valid="<<producer_timing_valid<<'\n';
    print_samples("cached",cached);print_samples("uncached",uncached);const bool timing_valid=cached.valid&&uncached.valid&&producer_timing_valid;
    if(timing_valid)std::cout<<"Measured uncached/cached median_GPU ratio="<<quantile(uncached.gpu,.5)/quantile(cached.gpu,.5)<<" median_wall_ratio="<<quantile(uncached.wall,.5)/quantile(cached.wall,.5)<<"; component timings, NOT application FPS or an assumed50% gain.\n";
    else std::cout<<"No performance ratio: GPU timestamps or retained samples are incomplete. Correctness evidence is separate.\n";
    if(sustained)check(elapsed>=60.0,"The requested real minute did not execute.");check(timing_valid||!physical,"Physical performance measurement has no valid GPU-clock evidence.");
    return {initial,fixed_cached,pairs,timing_valid};}
} //namespace

int main(int argc,char** argv){try{std::cout<<std::unitbuf;bool hardware=false,sustained=false,dense=false;std::string selector="d3d11";bool selected=false;
    for(int i=1;i<argc;++i){const std::string_view value=argv[i];if(value=="hardware")hardware=true;else if(value=="sustained")sustained=true;else if(value=="dense")dense=true;else if(value=="all"||value=="d3d11"||value=="d3d12"||value=="vulkan"||value=="opengl"){check(!selected,"Choose one API selector.");selector=value;selected=true;}else throw std::invalid_argument("Usage: smo_native_render_performance_tests [hardware] [d3d11|d3d12|vulkan|opengl|all] [sustained] [dense]");}
    check(!dense||hardware,"The optional1024-vertex/1536-triangle dense fixture requires explicit hardware selection.");
    check(!sustained||hardware,"Sustained512x288/192-triangle measurements require explicit hardware selection; quick WARP is separate.");if(hardware&&!selected)selector="all";
    std::optional<Result> baseline;std::string adapter;unsigned exercised=0;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(selector!="all"&&api!=smo_compute_api(selector))continue;
        SmoNativeGpuOptions options;options.api=api;options.device.software=!hardware;options.device.adapter_name=adapter;options.scalar_profile=lgaed::smo::NativeScalarProfile::ordinary_binary32;
        auto probe=create_smo_compute(api,options.device);if(hardware){check(!probe->info().software,"Hardware rendering silently selected software.");if(adapter.empty())adapter=probe->info().adapter;options.device.adapter_name=adapter;}probe.reset();
        auto result=run(options,sustained,dense);if(baseline){check(result.initial==baseline->initial,"Cross-API source/reset quantities or polygon metadata changed raw words.");check(result.initial_image.pixels.size()==baseline->initial_image.pixels.size(),"Cross-API fixed-pose dimensions differ.");
            std::size_t differences=0;for(std::size_t pixel=0;pixel<result.initial_image.pixels.size();++pixel)if(result.initial_image.pixels[pixel]!=baseline->initial_image.pixels[pixel])++differences;
            std::cout<<"Cross-API fixed-pose hash="<<result.initial_image.pixel_hash<<" baseline_hash="<<baseline->initial_image.pixel_hash<<" actual_BGRA_differences="<<differences
                <<" (reported separately: rotating native FDiv/trig does not promise universal cross-API pixel identity).\n";
        }else baseline=std::move(result);++exercised;}
    check(exercised>0,"No requested API was exercised.");std::cout<<"PASS: "<<checks<<" cache/reference correctness checks. Software quick validation and physical sustained rendering retain their separate scopes.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL native render performance/equivalence: "<<error.what()<<'\n';return 1;}}

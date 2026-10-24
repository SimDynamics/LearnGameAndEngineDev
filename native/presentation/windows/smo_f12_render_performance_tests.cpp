#include "presentation/windows/smo_program_render.hpp"
#include "presentation/windows/smo_scene.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <span>
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
void exact(const SmoProgramImage& a,const SmoProgramImage& b,std::uint64_t pair){check(a.width==b.width&&a.height==b.height,"F12 renderer dimensions differ.");
    check(a.exact_pixels.size()==static_cast<std::size_t>(a.width)*a.height&&a.exact_pixels.size()==b.exact_pixels.size(),"Exact F12 pixel evidence is missing.");
    check(a.diagnostic==b.diagnostic&&!(a.diagnostic&smo_d12_fatal_mask),"Cached/uncached F12 diagnostics differ or geometry failed.");
    std::size_t differences=0,first=0;for(std::size_t i=0;i<a.exact_pixels.size();++i)if(!(a.exact_pixels[i]==b.exact_pixels[i])){if(!differences)first=i;++differences;}
    if(differences){const auto& x=a.exact_pixels[first];const auto& y=b.exact_pixels[first];std::ostringstream error;error<<"F12 cache equivalence failed before any speed claim: pair="<<pair<<" differing_records="<<differences<<" first_pixel="<<first<<" face="<<x.face<<'/'<<y.face<<" BGRA="<<x.bgra<<'/'<<y.bgra<<" diagnostic="<<x.diagnostic<<'/'<<y.diagnostic<<" depth_words=";
        for(unsigned word=0;word<4;++word)error<<x.depth.digits[word]<<'/'<<y.depth.digits[word]<<',';error<<x.depth.negative<<'/'<<y.depth.negative<<','<<x.depth.diagnostic<<'/'<<y.depth.diagnostic;throw std::runtime_error(error.str());}
    check(a.pixels==b.pixels&&a.pixel_hash==b.pixel_hash&&a.colored_pixels==b.colored_pixels&&a.colored_pixels>0,"Raw image equality or visible F12 geometry metadata failed.");}
struct Samples {std::vector<double> gpu,wall,readback;double gpu_sum=0;bool valid=true;std::uint64_t discarded=0;
    void add(const SmoProgramImage& image){const bool good=std::isfinite(image.gpu_ms)&&image.gpu_ms>0&&std::isfinite(image.wall_ms)&&image.wall_ms>0&&std::isfinite(image.readback_ms)&&image.readback_ms>=0;valid=valid&&good;if(good)gpu_sum+=image.gpu_ms;
        if(gpu.size()<1048576u){gpu.push_back(image.gpu_ms);wall.push_back(image.wall_ms);readback.push_back(image.readback_ms);}else{++discarded;valid=false;}}};
double quantile(const std::vector<double>& values,double fraction){if(values.empty()||std::ranges::any_of(values,[](double x){return !std::isfinite(x);}))return std::numeric_limits<double>::quiet_NaN();auto sorted=values;std::ranges::sort(sorted);return sorted[static_cast<std::size_t>(fraction*static_cast<double>(sorted.size()-1u))];}
void print(std::string_view name,const Samples& samples){std::cout<<name<<" valid="<<samples.valid<<" samples="<<samples.gpu.size()<<" discarded="<<samples.discarded<<" GPU_median_ms="<<quantile(samples.gpu,.5)<<" GPU_p95_ms="<<quantile(samples.gpu,.95)<<" wall_median_ms="<<quantile(samples.wall,.5)<<" readback_median_ms="<<quantile(samples.readback,.5)<<" measured_GPU_seconds="<<samples.gpu_sum/1000.0<<'\n';}
// Branch-diagnostic fixtures are deliberately OUTSIDE the benchmark and use
// different acceptance from its valid, visible cube. Inexact remains visible;
// a dead q branch must not leak underflow, while a reached q must report it.
void fixture_equal(const SmoProgramImage& actual,const SmoProgramImage& expected,std::string_view context){
    check(actual.width==expected.width&&actual.height==expected.height&&actual.exact_pixels.size()==expected.exact_pixels.size(),"F12 branch fixture dimensions/evidence differ.");
    if(actual.exact_pixels!=expected.exact_pixels){std::size_t at=0;while(at<actual.exact_pixels.size()&&actual.exact_pixels[at]==expected.exact_pixels[at])++at;
        const auto& a=actual.exact_pixels.at(at);const auto& b=expected.exact_pixels.at(at);std::ostringstream error;error<<context<<" first_pixel="<<at<<" actual/expected_diag="<<a.diagnostic<<'/'<<b.diagnostic<<" BGRA="<<a.bgra<<'/'<<b.bgra<<" face="<<a.face<<'/'<<b.face<<" depth=";
        for(unsigned word=0;word<4u;++word)error<<a.depth.digits[word]<<'/'<<b.depth.digits[word]<<',';error<<a.depth.negative<<'/'<<b.depth.negative<<','<<a.depth.diagnostic<<'/'<<b.depth.diagnostic;throw std::runtime_error(error.str());}
    check(actual.diagnostic==expected.diagnostic&&actual.pixels==expected.pixels&&actual.pixel_hash==expected.pixel_hash&&actual.colored_pixels==expected.colored_pixels,"F12 branch fixture summary differs despite its raw records.");}
void branch_fixture(SmoCompute& gpu,SmoProgramRenderer& renderer,bool reached){const auto zero=smo_f12_zero(),one=smo_f12_uint(1),epsilon=smo_f12_parse("0."+std::string(179u,'0')+"1"),half=smo_f12_parse("0."+std::string(180u,'0')+"6");
    check(smo_f12_valid(epsilon)&&smo_f12_exponent(epsilon)==-191&&smo_f12_valid(half)&&smo_f12_exponent(half)==-192,"The branch fixture epsilon/half is not a valid in-range F12 input.");
    check((smo_f12_mul(epsilon,reached?half:epsilon).diagnostic&smo_d12_underflow)!=0u,"The fixture no longer sensitizes q's strict underflow diagnostic.");
    const auto low=reached?smo_f12_negate(half):epsilon,high=reached?half:smo_f12_add(epsilon,epsilon);
    const std::array<SmoProgramRenderVector,3> vertices{{{zero,low,zero},{epsilon,high,zero},{one,low,zero}}};const std::array<std::array<U,3>,1> faces{{{0u,1u,2u}}};
    const SmoProgramRenderVector rotation{zero,zero,zero},color{one,one,one};std::array<U,40> header{};
    for(unsigned entry=0;entry<6u;++entry){const auto value=entry<3u?zero:one;std::copy(value.digits.begin(),value.digits.end(),header.begin()+entry*6u);header[entry*6u+4u]=value.negative;header[entry*6u+5u]=value.diagnostic;}
    header[36]=3u;header[37]=1u;header[38]=1u;SmoGpuFrame frame;frame.vertices=gpu.buffer(sizeof(vertices),std::as_bytes(std::span(vertices)));frame.faces=gpu.buffer(sizeof(faces),std::as_bytes(std::span(faces)));frame.frame=gpu.buffer(sizeof(header),std::as_bytes(std::span(header)));frame.vertex_count=3u;frame.face_count=1u;frame.draw_requested=true;
    const U height=reached?3u:2u;renderer.resize(4u,height);const auto cpu=smo_program_render_reference(vertices,faces,rotation,color,4u,height),cached=renderer.render(frame,true),uncached=renderer.render(frame,true,false,SmoProgramRenderMode::reference_uncached);
    fixture_equal(cached,cpu,reached?"Reached-q cached/CPU":"Unreached-q cached/CPU");fixture_equal(uncached,cpu,reached?"Reached-q uncached/CPU":"Unreached-q uncached/CPU");fixture_equal(cached,uncached,"Branch fixture cached/uncached");
    for(U pixel=0;pixel<cpu.exact_pixels.size();++pixel){const bool expected_underflow=reached&&pixel/4u==1u;const auto& record=cpu.exact_pixels[pixel];
        check((record.diagnostic&smo_d12_fatal_mask)==(expected_underflow?smo_d12_underflow:0u),"A q diagnostic crossed its original u/v branch.");
        check(record.face==0xffffffffu&&record.depth==zero&&record.bgra==(expected_underflow?0xffff00ffu:0xff101820u),"The branch fixture changed canonical miss depth/face or diagnostic coloring.");}
    check((cpu.diagnostic&smo_d12_fatal_mask)==(reached?smo_d12_underflow:0u),"F12 branch fixture summary failed its exact fatal mask.");
    std::cout<<"F12 untimed_branch_fixture="<<(reached?"q_reached_middle_row":"q_unreached_every_ray")<<" resolution=4x"<<height<<" CPU/cached/uncached_all9words_exact=true summary_diagnostic="<<cached.diagnostic<<'\n';}
SmoProgramImage run(SmoComputeApi api,const SmoComputeOptions& options,bool sustained){const auto bytes=lgaed::state::read_bytes(std::filesystem::path(lgaed::build::source_root)/"native/smo_cube.smo",64u*1024u);const std::string source(bytes.begin(),bytes.end());
    auto program=SmoGpuProgram::compile(source,api,options);const U width=options.software?16u:128u,height=options.software?9u:72u;SmoProgramRenderer renderer(program.compute(),width,height);
    check(program.ir().vertices==8u&&program.ir().faces==12u,"The existing SMO cube source changed its authored polygon count.");
    const std::array<SmoF12,8> dt{smo_f12_parse("0"),smo_f12_parse(".01"),smo_f12_parse(".02"),smo_f12_parse(".04"),smo_f12_parse(".06"),smo_f12_parse(".08"),smo_f12_parse(".0#"),smo_f12_parse(".1")};
    const auto initial=renderer.render(program.current_frame(),true),initial_control=renderer.render(program.current_frame(),true,false,SmoProgramRenderMode::reference_uncached);exact(initial,initial_control,0);
    check(std::isnan(initial.gpu_ms)&&std::isnan(initial.wall_ms)&&std::isnan(initial.readback_ms),"Default F12 rendering unexpectedly enabled profiling.");
    std::cout<<"F12 render cache api="<<program.info().api<<" adapter="<<std::quoted(program.info().adapter)<<" software="<<program.info().software<<" resolution="<<width<<'x'<<height<<" vertices=8 triangles=12 source_SHA256="<<lgaed::state::sha256(source)<<" fixed_pose_hash="<<initial.pixel_hash<<'\n';
    std::cout<<"Contract: identical integer F12 precision/RNE/scalar geometry/scene/frame in each pair; cached5-pass batch includes face prepass, uncached4-pass batch omits it.52-to29 counts a full intersection's high-level operations, not GPU instructions, fewer polygons, an assumed speedup, or application FPS. VM generation is common and outside render GPU intervals.\n";
    for(unsigned warmup=0;warmup<2u;++warmup){const auto frame=program.step(dt[warmup+1u]);SmoProgramImage a,b;if(warmup%2u){b=renderer.render(frame,true,false,SmoProgramRenderMode::reference_uncached);a=renderer.render(frame,true);}else{a=renderer.render(frame,true);b=renderer.render(frame,true,false,SmoProgramRenderMode::reference_uncached);}exact(a,b,warmup);}
    Samples cached,uncached;std::uint64_t pairs=0;double elapsed=0,producer_wall_ms=0;const auto begin=Clock::now();
    do{const auto produce_begin=Clock::now();const auto frame=program.step(dt[static_cast<std::size_t>((pairs+3u)%dt.size())]);producer_wall_ms+=std::chrono::duration<double,std::milli>(Clock::now()-produce_begin).count();SmoProgramImage a,b;
        if(pairs%2u){b=renderer.render(frame,true,true,SmoProgramRenderMode::reference_uncached);a=renderer.render(frame,true,true);}else{a=renderer.render(frame,true,true);b=renderer.render(frame,true,true,SmoProgramRenderMode::reference_uncached);}
        exact(a,b,pairs);cached.add(a);uncached.add(b);++pairs;elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
    }while(pairs<3u||(sustained&&elapsed<60.0));
    std::cout<<"F12 pairs="<<pairs<<" elapsed_wall_seconds="<<elapsed<<" warmup_pairs=2 all_records_every_pair_exact=true common_VM_wall_ms="<<producer_wall_ms<<'\n';print("cached",cached);print("uncached",uncached);
    if(cached.valid&&uncached.valid)std::cout<<"F12 measured_uncached/cached_median_GPU_ratio="<<quantile(uncached.gpu,.5)/quantile(cached.gpu,.5)<<" median_wall_ratio="<<quantile(uncached.wall,.5)/quantile(cached.wall,.5)<<"; component measurements only.\n";
    else std::cout<<"No performance ratio: GPU timing/sample evidence is invalid. Exact arithmetic correctness is reported separately.\n";
    if(sustained)check(elapsed>=60.0,"The requested real minute did not execute.");check((cached.valid&&uncached.valid)||options.software,"Physical F12 render measurement lacks valid GPU timestamps.");
    branch_fixture(program.compute(),renderer,false);branch_fixture(program.compute(),renderer,true);return initial;}
}
int main(int argc,char** argv){try{std::cout<<std::unitbuf;bool hardware=false,sustained=false,selected=false;std::string selector="d3d11";
    for(int i=1;i<argc;++i){const std::string_view value=argv[i];if(value=="hardware")hardware=true;else if(value=="sustained")sustained=true;else if(value=="d3d11"||value=="d3d12"||value=="vulkan"||value=="opengl"||value=="all"){check(!selected,"Choose one API selector.");selector=value;selected=true;}else throw std::invalid_argument("Usage: smo_f12_render_performance_tests [hardware] [d3d11|d3d12|vulkan|opengl|all] [sustained]");}
    check(!sustained||hardware,"The sustained128x72 F12 rendering run requires explicit hardware selection; WARP quick remains16x9.");if(hardware&&!selected)selector="all";
    std::string adapter;std::optional<SmoProgramImage> baseline;unsigned exercised=0;
    for(const auto api:{SmoComputeApi::direct3d11,SmoComputeApi::direct3d12,SmoComputeApi::vulkan,SmoComputeApi::opengl}){if(selector!="all"&&api!=smo_compute_api(selector))continue;SmoComputeOptions options{!hardware,adapter};auto probe=create_smo_compute(api,options);
        if(hardware){check(!probe->info().software,"Hardware F12 rendering silently selected software.");if(adapter.empty())adapter=probe->info().adapter;options.adapter_name=adapter;}probe.reset();const auto initial=run(api,options,sustained);
        if(baseline)exact(initial,*baseline,0);else baseline=initial;++exercised;}
    check(exercised>0,"No requested API was exercised.");std::cout<<"PASS: "<<checks<<" F12 exact-record cache/reference checks. No native-binary32 comparison or radix speedup is inferred.\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL F12 render cache experiment: "<<error.what()<<'\n';return 1;}}

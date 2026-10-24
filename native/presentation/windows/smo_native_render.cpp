#include "presentation/windows/smo_native_render.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace lgaed::presentation { namespace {
using U=std::uint32_t;
void size_valid(U width,U height){if(!width||!height||width>512||height>288)throw std::invalid_argument("La vista binary32 admite1..512 por1..288 píxeles.");}
std::string source(std::string_view name){const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512*1024);std::string result;result.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i){if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;result+=static_cast<char>(raw[i]);}return result;}
std::vector<U> read(SmoCompute& gpu,SmoBuffer buffer,std::size_t expected){const auto raw=gpu.read(buffer);if(raw.size()!=expected*4u)throw std::runtime_error("La lectura gráfica binary32 está incompleta.");std::vector<U> result(expected);std::memcpy(result.data(),raw.data(),raw.size());return result;}
}
SmoKernelSource smo_native_render_kernel(unsigned stage){if(stage>=smo_native_render_stages)throw std::invalid_argument("Etapa gráfica binary32 inexistente.");
    const auto hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc"),body=source("smo_native_render.inc");const auto define="#define SMO_NATIVE_RENDER_STAGE "+std::to_string(stage)+"\n";
    SmoKernelSource result;result.name="native_render"+std::to_string(stage);result.hlsl=define+hlsl+body;result.glsl="#version 430\n"+define+glsl+body;
#ifdef LGAED_SMO_NATIVE_RENDER_SHADER_ROOT
    const auto root=std::filesystem::path(LGAED_SMO_NATIVE_RENDER_SHADER_ROOT);const auto manifest=state::read_bytes(root/"source.sha256",256);std::string expected(manifest.begin(),manifest.end());
    while(!expected.empty()&&(expected.back()=='\r'||expected.back()=='\n'||expected.back()==' '))expected.pop_back();if(expected!=state::sha256(glsl+body))throw std::runtime_error("El shader gráfico binary32 cambió: recompila el preset Windows existente.");
    const auto bytes=state::read_bytes(root/"shaders"/(result.name+".spv"),4*1024*1024);if(bytes.size()<20||bytes.size()%4u)throw std::runtime_error("SPIR-V gráfico binary32 incompleto.");result.spirv.resize(bytes.size()/4u);std::memcpy(result.spirv.data(),bytes.data(),bytes.size());
    if(result.spirv.front()!=0x07230203u)throw std::runtime_error("SPIR-V gráfico binary32 inválido.");
#endif
    std::filesystem::path dxbc;
#ifdef LGAED_SMO_NATIVE_RENDER_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_NATIVE_RENDER_HLSL_SHADER_ROOT;
#endif
    load_smo_dxbc_assets(result,dxbc,hlsl+body,true);return result;}
struct SmoNativeRenderer::Impl {
    SmoCompute& gpu;std::array<SmoKernel,smo_native_render_stages> kernels{};SmoBuffer transformed=0,scratch=0,face_cache=0,pixels=0;
    U width=0,height=0,capacity=0,last_vertices=0;bool uncached_loaded=false;
    Impl(SmoCompute& compute,U w,U h):gpu(compute){for(unsigned stage=0;stage<4u;++stage)kernels[stage]=gpu.kernel(smo_native_render_kernel(stage));
        transformed=gpu.buffer(1024u*4u*4u);scratch=gpu.buffer(16u*4u);face_cache=gpu.buffer(2048u*16u*4u);resize(w,h);}
    void resize(U w,U h){size_valid(w,h);const auto needed=w*h;if(needed>capacity){capacity=1;while(capacity<needed)capacity*=2;pixels=gpu.buffer((static_cast<std::size_t>(capacity)+1u)*4u);}width=w;height=h;}
};
SmoNativeRenderer::SmoNativeRenderer(SmoCompute& gpu,U width,U height):impl_(std::make_unique<Impl>(gpu,width,height)){}
SmoNativeRenderer::~SmoNativeRenderer()=default;SmoNativeRenderer::SmoNativeRenderer(SmoNativeRenderer&&) noexcept=default;SmoNativeRenderer& SmoNativeRenderer::operator=(SmoNativeRenderer&&) noexcept=default;
void SmoNativeRenderer::resize(U width,U height){if(!impl_)throw std::logic_error("Renderer binary32 movido.");impl_->resize(width,height);}
SmoNativeImage SmoNativeRenderer::render(const SmoNativeGpuFrameView& frame,bool timed,SmoNativeRenderMode mode){if(!impl_)throw std::logic_error("Renderer binary32 movido.");auto& r=*impl_;
    if(frame.scalar_profile!=smo::NativeScalarProfile::ordinary_binary32||!frame.vertices||!frame.faces||!frame.frame||frame.vertex_count>1024||frame.face_count>2048||
        frame.vertex_word_offset>std::numeric_limits<U>::max()-frame.vertex_count*3u||frame.face_word_offset>std::numeric_limits<U>::max()-frame.face_count*3u||frame.frame_word_offset>std::numeric_limits<U>::max()-10u)
        throw std::invalid_argument("Vista GPU binary32 inválida o de otro perfil.");
    if(mode!=SmoNativeRenderMode::cached&&mode!=SmoNativeRenderMode::reference_uncached)throw std::invalid_argument("Modalidad gráfica de comparación desconocida.");
    if(mode==SmoNativeRenderMode::reference_uncached&&!r.uncached_loaded){r.kernels[4]=r.gpu.kernel(smo_native_render_kernel(4));r.uncached_loaded=true;}
    const auto begin=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    std::array<U,16> constants{r.width,r.height,frame.vertex_count,frame.face_count,frame.vertex_word_offset,frame.face_word_offset,frame.frame_word_offset,0};
    const std::array<SmoBuffer,8> buffers{frame.vertices,frame.faces,frame.frame,r.transformed,r.scratch,r.pixels,r.face_cache,0};std::array<SmoDispatch,5> calls;
    calls[0]={r.kernels[0],buffers,constants,{1,1,1}};calls[1]={r.kernels[1],buffers,constants,{std::max(1u,(frame.vertex_count+63u)/64u),1,1}};
    constants[7]=1;calls[2]={r.kernels[0],buffers,constants,{1,1,1}};constants[7]=0;std::size_t passes=4;
    if(mode==SmoNativeRenderMode::cached){calls[3]={r.kernels[3],buffers,constants,{std::max(1u,(frame.face_count+63u)/64u),1,1}};calls[4]={r.kernels[2],buffers,constants,{(r.width*r.height+63u)/64u,1,1}};passes=5;}
    else calls[3]={r.kernels[4],buffers,constants,{(r.width*r.height+63u)/64u,1,1}};
    const auto gpu_ms=r.gpu.execute(std::span(calls.data(),passes),timed);r.last_vertices=frame.vertex_count;const auto read_begin=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    auto raw=read(r.gpu,r.pixels,static_cast<std::size_t>(r.capacity)+1u);const auto read_end=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};SmoNativeImage image;image.width=r.width;image.height=r.height;image.diagnostic=raw[r.width*r.height];image.pixel_hash=14695981039346656037ull;
    image.pixels.assign(raw.begin(),raw.begin()+static_cast<std::ptrdiff_t>(r.width*r.height));for(const auto pixel:image.pixels){if(pixel!=0xff101820u&&(image.diagnostic==0||pixel!=0xffff00ffu))++image.colored_pixels;
        for(unsigned byte=0;byte<4;++byte){image.pixel_hash^=(pixel>>(byte*8u))&255u;image.pixel_hash*=1099511628211ull;}}
    if(timed){image.gpu_ms=gpu_ms;image.readback_ms=std::chrono::duration<double,std::milli>(read_end-read_begin).count();image.wall_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();}return image;}
SmoNativeRenderGeometry SmoNativeRenderer::inspect_geometry()const{if(!impl_)throw std::logic_error("Renderer binary32 movido.");const auto& r=*impl_;SmoNativeRenderGeometry result;if(!r.last_vertices)return result;const auto raw=read(r.gpu,r.transformed,1024u*4u);
    for(U vertex=0;vertex<r.last_vertices;++vertex){result.vertices.push_back({raw[vertex*4u],raw[vertex*4u+1u],raw[vertex*4u+2u]});result.diagnostics.push_back(raw[vertex*4u+3u]);}return result;}
} //namespace lgaed::presentation

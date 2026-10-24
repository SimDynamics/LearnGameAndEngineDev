#include "presentation/windows/smo_duodecimal_scene.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace lgaed::presentation {
namespace {
void validate(const SmoD12SceneWorkload& work) {
    if(!work.bodies||work.bodies>16||!work.width||work.width>64||!work.height||work.height>36)
        throw std::invalid_argument("La escena duodecimal está acotada a 16 cuerpos y 64 por 36 píxeles.");
}
std::string source(std::string_view name) {
    const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512*1024);
    std::string result;result.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i) {
        if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;
        result+=static_cast<char>(raw[i]);
    }
    return result;
}
} // namespace

std::array<std::uint32_t,16> smo_duodecimal_scene_constants(const SmoD12SceneWorkload& work,std::uint32_t frame) {
    validate(work);std::array<std::uint32_t,16> result{};
    result[0]=work.bodies;result[1]=work.width;result[2]=work.height;result[3]=frame;return result;
}
SmoKernelSource smo_duodecimal_scene_kernel(unsigned stage,bool fp32_control,bool floating12) {
    if(stage>4)throw std::invalid_argument("Etapa duodecimal inexistente.");
    const auto hlsl=source("smo_shader_hlsl.inc"),glsl=source("smo_shader_glsl.inc"),
        arithmetic=source(fp32_control?"smo_duodecimal_control.inc":floating12?"smo_float12.inc":"smo_duodecimal.inc"),scene=source("smo_duodecimal_scene.inc");
    const auto define="#define D12_STAGE "+std::to_string(stage)+"\n";
    SmoKernelSource result;result.name=std::string(fp32_control?"f12_stage":floating12?"float12_stage":"d12_stage")+std::to_string(stage);
    result.hlsl=define+hlsl+arithmetic+scene;
    const bool modular_gl=floating12&&!fp32_control;
    result.glsl="#version 430\n"+define+glsl+arithmetic+(modular_gl?source("smo_float12_opengl.inc"):std::string{})+scene;
    if(modular_gl){
        result.gl_subroutines={
            {"f12gl_add_dispatch","f12gl_add_impl"},{"f12gl_sub_dispatch","f12gl_sub_impl"},
            {"f12gl_mul_dispatch","f12gl_mul_impl"},{"f12gl_div_dispatch","f12gl_div_impl"},
            {"f12gl_pdir_dispatch","f12gl_pdir_impl"},{"f12gl_pinv_dispatch","f12gl_pinv_impl"},
            {"f12gl_sqrt_dispatch","f12gl_sqrt_impl"},{"f12gl_abs_dispatch","f12gl_abs_impl"},
            {"f12gl_norm3_dispatch","f12gl_norm3_impl"},{"f12gl_magnitude2_dispatch","f12gl_magnitude2_impl"},
            {"f12gl_discriminant_dispatch","f12gl_discriminant_impl"},{"f12gl_clamp_dispatch","f12gl_clamp_impl"}};
    }
    std::filesystem::path root;
    if(fp32_control) {
#ifdef LGAED_SMO_D12_CONTROL_SHADER_ROOT
        root=std::filesystem::path(LGAED_SMO_D12_CONTROL_SHADER_ROOT);
#endif
    }else if(floating12){
#ifdef LGAED_SMO_FLOAT12_SHADER_ROOT
        root=std::filesystem::path(LGAED_SMO_FLOAT12_SHADER_ROOT);
#endif
    }else {
#ifdef LGAED_SMO_D12_SHADER_ROOT
        root=std::filesystem::path(LGAED_SMO_D12_SHADER_ROOT);
#endif
    }
    if(!root.empty()) {
    const auto manifest=state::read_bytes(root/"source.sha256",256);std::string expected(manifest.begin(),manifest.end());
    while(!expected.empty()&&(expected.back()=='\n'||expected.back()=='\r'||expected.back()==' '))expected.pop_back();
    if(expected!=state::sha256(glsl+arithmetic+scene))throw std::runtime_error("La fuente duodecimal cambió: recompila el preset Windows existente.");
    const auto raw=state::read_bytes(root/"shaders"/(result.name+".spv"),4*1024*1024);
    if(raw.empty()||raw.size()%4)throw std::runtime_error("SPIR-V duodecimal precompilado inválido.");
    result.spirv.resize(raw.size()/4);std::memcpy(result.spirv.data(),raw.data(),raw.size());
    if(result.spirv[0]!=0x07230203u)throw std::runtime_error("Cabecera SPIR-V duodecimal inválida.");
    }
    std::filesystem::path hlsl_root;
    if(fp32_control){
#ifdef LGAED_SMO_D12_CONTROL_HLSL_SHADER_ROOT
        hlsl_root=std::filesystem::path(LGAED_SMO_D12_CONTROL_HLSL_SHADER_ROOT);
#endif
    }else if(floating12){
#ifdef LGAED_SMO_FLOAT12_HLSL_SHADER_ROOT
        hlsl_root=std::filesystem::path(LGAED_SMO_FLOAT12_HLSL_SHADER_ROOT);
#endif
    }else{
#ifdef LGAED_SMO_D12_HLSL_SHADER_ROOT
        hlsl_root=std::filesystem::path(LGAED_SMO_D12_HLSL_SHADER_ROOT);
#endif
    }
    load_smo_dxbc_assets(result,hlsl_root,hlsl+arithmetic+scene,floating12&&!fp32_control);
    return result;
}

} // namespace lgaed::presentation

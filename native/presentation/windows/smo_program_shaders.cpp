#include "presentation/windows/smo_program_gpu.hpp"
#include "presentation/windows/smo_program_render.hpp"
#include "presentation/windows/smo_radix_ablation.hpp"
#include "presentation/windows/smo_shader_assets.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace lgaed::presentation { namespace {
std::string part(std::string_view name) {
    const auto raw=state::read_bytes(std::filesystem::path(build::source_root)/"native/presentation/windows"/name,512*1024);
    std::string text; text.reserve(raw.size());
    for(std::size_t i=0;i<raw.size();++i) {
        if(raw[i]=='\r'&&i+1<raw.size()&&raw[i+1]=='\n')continue;
        text+=static_cast<char>(raw[i]);
    }
    return text;
}
void integer_spirv(const SmoKernelSource& shader) {
    if(shader.spirv.empty())return;
    if(shader.spirv.size()<5||shader.spirv.front()!=0x07230203u)throw std::runtime_error("SPIR-V SMO del programa inválido.");
    for(std::size_t at=5;at<shader.spirv.size();) {
        const auto words=shader.spirv[at]>>16;
        if(!words||words>shader.spirv.size()-at)throw std::runtime_error("SPIR-V SMO del programa truncado.");
        if((shader.spirv[at]&0xffffu)==22u)throw std::runtime_error("El programa SMO GPU contiene un tipo flotante.");
        at+=words;
    }
}
SmoKernelSource kernel(std::string name,std::string define,std::string_view implementation,
    const std::filesystem::path& spirv_root,const std::filesystem::path& dxbc_root,bool modular_gl,bool f12=true) {
    const auto hlsl=part("smo_shader_hlsl.inc"),glsl=part("smo_shader_glsl.inc"),
        arithmetic=f12?((implementation=="smo_program_vm.inc"?part("smo_program_profile.inc"):std::string{})+part("smo_float12.inc")):std::string{},body=part(implementation);
    SmoKernelSource result;result.name=std::move(name);result.hlsl=define+hlsl+arithmetic+body;
    result.glsl="#version 430\n"+define+glsl+arithmetic+(modular_gl?part("smo_float12_opengl.inc"):std::string{})+body;
    if(modular_gl)result.gl_subroutines={
        {"f12gl_add_dispatch","f12gl_add_impl"},{"f12gl_sub_dispatch","f12gl_sub_impl"},
        {"f12gl_mul_dispatch","f12gl_mul_impl"},{"f12gl_div_dispatch","f12gl_div_impl"},
        {"f12gl_pdir_dispatch","f12gl_pdir_impl"},{"f12gl_pinv_dispatch","f12gl_pinv_impl"},
        {"f12gl_sqrt_dispatch","f12gl_sqrt_impl"},{"f12gl_abs_dispatch","f12gl_abs_impl"},
        {"f12gl_norm3_dispatch","f12gl_norm3_impl"},{"f12gl_magnitude2_dispatch","f12gl_magnitude2_impl"},
        {"f12gl_discriminant_dispatch","f12gl_discriminant_impl"},{"f12gl_clamp_dispatch","f12gl_clamp_impl"}};
    if(!spirv_root.empty()) {
        const auto bytes=state::read_bytes(spirv_root/"source.sha256",256);
        std::string hash(bytes.begin(),bytes.end());
        while(!hash.empty()&&(hash.back()=='\r'||hash.back()=='\n'||hash.back()==' '))hash.pop_back();
        if(hash!=state::sha256(glsl+arithmetic+body))throw std::runtime_error("El programa SMO GPU cambió: recompila el preset Windows existente.");
        const auto blob=state::read_bytes(spirv_root/"shaders"/(result.name+".spv"),4*1024*1024);
        if(blob.empty()||blob.size()%4)throw std::runtime_error("SPIR-V SMO del programa vacío o truncado.");
        result.spirv.resize(blob.size()/4);std::memcpy(result.spirv.data(),blob.data(),blob.size());
        integer_spirv(result);
    }
    load_smo_dxbc_assets(result,dxbc_root,hlsl+arithmetic+body,true);
    return result;
}
} //namespace
SmoKernelSource smo_program_vm_kernel(unsigned opcode) {
    if(opcode>=smo_vm_kernel_count)throw std::invalid_argument("Opcode SMO inexistente.");
    std::filesystem::path spirv,dxbc;
#ifdef LGAED_SMO_PROGRAM_VM_SHADER_ROOT
    spirv=LGAED_SMO_PROGRAM_VM_SHADER_ROOT;
#endif
#ifdef LGAED_SMO_PROGRAM_VM_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_PROGRAM_VM_HLSL_SHADER_ROOT;
#endif
    return kernel("smo_vm"+std::to_string(opcode),"#define SMO_VM_OPCODE "+std::to_string(opcode)+"\n",
        "smo_program_vm.inc",spirv,dxbc,false);
}
SmoKernelSource smo_program_render_kernel(unsigned stage) {
    if(stage>=5)throw std::invalid_argument("Etapa gráfica SMO inexistente.");
    std::filesystem::path spirv,dxbc;
#ifdef LGAED_SMO_PROGRAM_RENDER_SHADER_ROOT
    spirv=LGAED_SMO_PROGRAM_RENDER_SHADER_ROOT;
#endif
#ifdef LGAED_SMO_PROGRAM_RENDER_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_PROGRAM_RENDER_HLSL_SHADER_ROOT;
#endif
    return kernel("smo_render"+std::to_string(stage),"#define SMO_RENDER_STAGE "+std::to_string(stage)+"\n",
        "smo_program_render.inc",spirv,dxbc,true);
}
SmoKernelSource smo_radix_ablation_kernel(unsigned stage) {
    if(stage>=smo_radix_ablation_stages)throw std::invalid_argument("Etapa de comparación de radix inexistente.");
    std::filesystem::path spirv,dxbc;
#ifdef LGAED_SMO_RADIX_ABLATION_SHADER_ROOT
    spirv=LGAED_SMO_RADIX_ABLATION_SHADER_ROOT;
#endif
#ifdef LGAED_SMO_RADIX_ABLATION_HLSL_SHADER_ROOT
    dxbc=LGAED_SMO_RADIX_ABLATION_HLSL_SHADER_ROOT;
#endif
    return kernel("smo_radix"+std::to_string(stage),"#define SMO_RADIX_STAGE "+std::to_string(stage)+"\n",
        "smo_radix_ablation.inc",spirv,dxbc,false,false);
}
} //namespace lgaed::presentation

#pragma once
#include "presentation/windows/smo_compute.hpp"
#include "platform/windows/state_store.hpp"
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace lgaed::presentation {
// Shared acceptance path for prepared compute assets. A configure alone never
// makes an old CSO valid: each successful compilation records both identities.
inline void load_smo_dxbc_assets(SmoKernelSource& shader,const std::filesystem::path& root,std::string_view body,bool shared50=false) {
    if(root.empty())return;
    const auto manifest=state::read_bytes(root/"fullsource.sha256",128);
    std::string expected(manifest.begin(),manifest.end());
    while(!expected.empty()&&(expected.back()=='\n'||expected.back()=='\r'))expected.pop_back();
    if(expected!=state::sha256(body))throw std::runtime_error("La fuente HLSL cambió: recompila el preset Windows existente.");
    const auto prepared=[&](std::string_view profile,std::vector<std::byte>& output) {
        const auto basename=shader.name+"."+std::string(profile);
        const auto raw_manifest=state::read_bytes(root/"shaders"/(basename+".fullsource.sha256"),512);
        std::istringstream fields(std::string(raw_manifest.begin(),raw_manifest.end()));
        std::string magic,stored_profile,source_hash,code_hash,extra;
        fields>>magic>>stored_profile>>source_hash>>code_hash;
        if(!fields||magic!="LGAED_SMO_DXBC1"||stored_profile!=profile||source_hash!=state::sha256(shader.hlsl)||(fields>>extra))
            throw std::runtime_error("El manifiesto DXBC no corresponde a la fuente/perfil actuales: "+basename);
        const auto raw=state::read_bytes(root/"shaders"/(basename+".cso"),64*1024*1024);
        if(raw.empty()||code_hash!=state::sha256(raw))throw std::runtime_error("El DXBC está incompleto o no supera SHA-256: "+basename);
        output.resize(raw.size());std::memcpy(output.data(),raw.data(),raw.size());
    };
    prepared("cs_5_0",shader.dxbc);
    shader.dxbc50_d3d12_compatible=false;
    if(shared50){
        // Keep the REAL profile/manifest and bytecode in its cs_5_0 slot. The
        // D3D12 adapter reflects and accepts that older model explicitly; it
        // must never be copied to or relabelled as shader model5.1.
        shader.dxbc51.clear();shader.dxbc50_d3d12_compatible=true;
    }else prepared("cs_5_1",shader.dxbc51);
}

}

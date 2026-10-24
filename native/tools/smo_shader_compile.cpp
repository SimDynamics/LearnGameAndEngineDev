#include <windows.h>
#include <bcrypt.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Microsoft::WRL::ComPtr;
std::vector<std::byte> read(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input)throw std::runtime_error("Cannot open HLSL input: "+path.string());
    const auto count=input.tellg();
    if(count<=0||count>2*1024*1024)throw std::runtime_error("HLSL input is empty or exceeds the declared source budget.");
    std::vector<std::byte> result(static_cast<std::size_t>(count));input.seekg(0);
    input.read(reinterpret_cast<char*>(result.data()),static_cast<std::streamsize>(result.size()));
    if(!input)throw std::runtime_error("Incomplete HLSL source read.");return result;
}
std::string sha256(std::span<const std::byte> data){
    if(data.size()>std::numeric_limits<ULONG>::max())throw std::runtime_error("Shader exceeds SHA-256 input capacity.");
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot open Windows SHA-256 provider.");
    struct Close{BCRYPT_ALG_HANDLE value;~Close(){BCryptCloseAlgorithmProvider(value,0);}} close{algorithm};
    unsigned char digest[32]{};
    if(BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(const_cast<std::byte*>(data.data())),static_cast<ULONG>(data.size()),digest,sizeof(digest))<0)
        throw std::runtime_error("Cannot hash compiled shader/source.");
    std::ostringstream result;result<<std::hex<<std::setfill('0');for(const auto byte:digest)result<<std::setw(2)<<static_cast<unsigned>(byte);return result.str();
}
void write(const std::filesystem::path& path,std::span<const std::byte> data){
    std::ofstream output(path,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Cannot create compiled shader: "+path.string());
    output.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));output.close();
    if(!output)throw std::runtime_error("Incomplete compiled shader/manifest write.");
}
}

int wmain(int argc,wchar_t** argv){
    try{
        if(argc!=5)throw std::invalid_argument("Usage: lgaed_smo_shader_compile source.hlsl output.cso cs_5_0|cs_5_1 fullsource.sha256");
        const std::wstring requested=argv[3];
        if(requested!=L"cs_5_0"&&requested!=L"cs_5_1")throw std::invalid_argument("Only the existing compute shader models 5.0 and 5.1 are accepted.");
        const std::string profile(requested.begin(),requested.end());const auto source=read(argv[1]);
        const auto name=std::filesystem::path(argv[1]).filename().string();ComPtr<ID3DBlob> code,errors;
        const auto status=D3DCompile(source.data(),source.size(),name.c_str(),nullptr,nullptr,"CSMain",profile.c_str(),
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_IEEE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.GetAddressOf(),errors.GetAddressOf());
        if(errors)std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),static_cast<std::streamsize>(errors->GetBufferSize()));
        if(FAILED(status)||!code)throw std::runtime_error("D3DCompile failed for "+name+" / "+profile+" ("+std::to_string(static_cast<unsigned long>(status))+").");
        const auto binary=std::span(static_cast<const std::byte*>(code->GetBufferPointer()),code->GetBufferSize());
        const std::string manifest="LGAED_SMO_DXBC1\n"+profile+"\n"+sha256(source)+"\n"+sha256(binary)+"\n";
        // A configure alone never updates this acceptance manifest. It is
        // written only after a successful optimized compilation and bytecode
        // write; the loader verifies both source and artifact hashes.
        write(argv[2],binary);write(argv[4],std::as_bytes(std::span(manifest.data(),manifest.size())));
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

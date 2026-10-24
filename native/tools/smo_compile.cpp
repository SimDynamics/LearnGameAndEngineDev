#include "core/smo_native.hpp"
#include "presentation/windows/smo_native_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"
#include <windows.h>
#include <algorithm>
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace {
using namespace lgaed;
std::string numeral(std::int32_t value) {
    const bool negative=value<0;
    auto magnitude=std::bit_cast<std::uint32_t>(value);
    if(negative)magnitude=0u-magnitude;
    std::string text;
    do{const auto digit=magnitude%12u;text.push_back(digit<10u?static_cast<char>('0'+digit):digit==10u?'#':'~');magnitude/=12u;}while(magnitude);
    if(negative)text.push_back('-');std::ranges::reverse(text);return text;
}
std::string load(const std::filesystem::path& path) {
    std::ifstream file;std::istream* input=&std::cin;
    if(path!="-"){file.open(path,std::ios::binary);input=&file;}
    if(!*input)throw std::runtime_error("No se pudo abrir la fuente SMO.");
    std::string result;result.resize(65537);
    input->read(result.data(),static_cast<std::streamsize>(result.size()));result.resize(static_cast<std::size_t>(input->gcount()));
    if(result.size()>65536)throw std::runtime_error("La fuente excede el límite de 64 KiB.");
    if(input->bad())throw std::runtime_error("No se pudo leer la fuente SMO completa.");
    return result;
}
std::filesystem::path utf8_path(std::string_view text) {
    std::u8string encoded;encoded.reserve(text.size());
    for(const unsigned char c:text)encoded.push_back(static_cast<char8_t>(c));
    return std::filesystem::path(encoded);
}
// A compiler artifact is written only to an explicit destination. CREATE_NEW
// protects existing source, binaries and user data; no temporary copy is made.
void write_spirv(const std::filesystem::path& path,std::span<const std::uint32_t> words) {
    const auto handle=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("No se creó SPIR-V: el destino debe ser nuevo y su directorio debe existir.");
    DWORD count=0;const auto bytes=words.size_bytes();
    const auto written=bytes<=MAXDWORD&&WriteFile(handle,words.data(),static_cast<DWORD>(bytes),&count,nullptr)&&count==bytes;
    const auto closed=CloseHandle(handle);
    if(!written||!closed)throw std::runtime_error("Falló la escritura del artefacto SPIR-V.");
}
void require_success(const presentation::SmoNativeGpuExecution& execution) {
    if(execution.committed)return;
    for(std::size_t item=0;item<execution.diagnostics.size();++item){const auto& d=execution.diagnostics[item];if(d.code!=smo::NativeError::none)
        throw std::runtime_error("La ejecución no se confirmó: instancia "+std::to_string(item)+", diagnóstico "+std::to_string(static_cast<unsigned>(d.code))+", línea "+std::to_string(d.line)+", columna "+std::to_string(d.column)+". Se conserva el estado anterior.");}
    throw std::runtime_error("La GPU no confirmó la ejecución nativa.");
}
}

int main(int argc,char** argv) {
    try {
        if(argc<2||std::string_view(argv[1])=="--help"){std::cout<<"Uso: lgaed_smo_compile fuente.smo|- [--profile i32|graphics] [--spirv destino.spv] [--action reset|frame|step] [--run d3d11|d3d12|vulkan|opengl] [--dt numero_base12] [--steps entero_base12] [--software]\n"
            <<"Perfiles nativos ordinarios: i32 exacto; graphics usa binary32 de la GPU, con precisión distinta de F12. Los valores de --dt y --steps se escriben en base 12. Sin reglas SMO especiales ni interpretación GPU.\n";return 0;}
        const auto source=load(utf8_path(argv[1]));
        std::optional<std::filesystem::path> output;std::optional<presentation::SmoComputeApi> api;
        smo::NativeAction action=smo::NativeAction::step;std::string dt_text="1";std::int32_t steps=1;bool software=false;
        auto scalar_profile=smo::NativeScalarProfile::ordinary_i32;
        for(int i=2;i<argc;++i){const std::string_view option=argv[i];
            const auto argument=[&]()->std::string_view{if(++i>=argc)throw std::invalid_argument("Falta un argumento para "+std::string(option));return argv[i];};
            if(option=="--spirv")output=utf8_path(argument());
            else if(option=="--run")api=presentation::smo_compute_api(argument());
            else if(option=="--dt")dt_text=argument();
            else if(option=="--steps")steps=smo::native_parse_integer(argument());
            else if(option=="--profile"){const auto value=argument();if(value=="i32")scalar_profile=smo::NativeScalarProfile::ordinary_i32;
                else if(value=="graphics")scalar_profile=smo::NativeScalarProfile::ordinary_binary32;else throw std::invalid_argument("Perfil desconocido.");}
            else if(option=="--software")software=true;
            else if(option=="--action"){const auto value=argument();if(value=="reset")action=smo::NativeAction::reset;else if(value=="frame")action=smo::NativeAction::frame;else if(value=="step")action=smo::NativeAction::step;else throw std::invalid_argument("Acción desconocida.");}
            else throw std::invalid_argument("Opción desconocida: "+std::string(option));
        }
        if(steps<0||steps>4096)throw std::invalid_argument("steps debe estar entre 0 y 2454 (4096 en notación decimal).");
        const bool real=scalar_profile==smo::NativeScalarProfile::ordinary_binary32;
        const auto dt=real?0:smo::native_parse_integer(dt_text);const auto dt_bits=real?smo::native_parse_binary32(dt_text):0u;
        if(dt<0||(real&&std::bit_cast<float>(dt_bits)<0))throw std::invalid_argument("dt no puede ser negativo.");
        const auto program=smo::native_compile(source,{},scalar_profile);
        std::cout<<"SMO base 12 -> compilación nativa "<<(real?"binary32":"int32")<<". Perfil de aritmética ordinaria; reglas especiales pendientes de experimentos separados.\n";
        for(unsigned i=0;i<3;++i)std::cout<<"Acción "<<i<<": "<<program.actions[i].instructions.size()<<" instrucciones del compilador; un kernel, sin intérprete por operación.\n";
        if(output){const auto kernel=presentation::smo_native_kernel(program,action);write_spirv(*output,kernel.spirv);
            std::cout<<"SPIR-V emitido directamente en memoria y guardado en el destino solicitado: "<<kernel.spirv.size()*sizeof(std::uint32_t)<<" bytes.\n";}
        if(api){presentation::SmoNativeGpuOptions options;options.api=*api;options.device.software=software;options.scalar_profile=scalar_profile;
            auto gpu=presentation::SmoNativeGpuProgram::from_program(program,options);const auto info=gpu.info();
            std::cout<<info.api<<" · "<<info.adapter<<" · software="<<info.software<<'\n';
            const std::array<std::int32_t,1> ticks{dt};const std::array<std::uint32_t,1> real_ticks{dt_bits};
            for(std::int32_t step=0;step<steps;++step){const auto result=action==smo::NativeAction::reset?gpu.reset():action==smo::NativeAction::frame?gpu.frame():real?gpu.step_binary32(real_ticks):gpu.step(ticks);require_success(result);
                std::cout<<"GPU ms="<<result.gpu_ms<<"; ida y vuelta ms="<<result.wall_ms<<"; lectura de estado ms="<<result.status_readback_ms<<" (no son FPS).\n";}
            if(real){const auto snapshot=gpu.inspect_binary32();if(snapshot.items.size()!=1)throw std::runtime_error("El resultado nativo no contiene su instancia.");const auto& frame=snapshot.items.front();
                // Diagnostic text only, outside all GPU queries. Promotion is
                // exact for binary32 and never feeds values back into execution.
                for(std::size_t i=0;i<program.ir.states.size();++i)std::cout<<program.ir.states[i]<<" = "<<smo::format_number(static_cast<double>(std::bit_cast<float>(frame.states.at(i))))<<" (base 12)\n";
                std::cout<<"Vértices="<<numeral(static_cast<std::int32_t>(frame.vertices.size()))<<"; caras="<<numeral(static_cast<std::int32_t>(frame.faces.size()))<<"; dibujar="<<frame.draw_requested<<'\n';
            }else{const auto snapshot=gpu.inspect();if(snapshot.items.size()!=1)throw std::runtime_error("El resultado nativo no contiene su instancia.");const auto& frame=snapshot.items.front();
                for(std::size_t i=0;i<program.ir.states.size();++i)std::cout<<program.ir.states[i]<<" = "<<numeral(frame.states.at(i))<<" (base 12)\n";
                std::cout<<"Vértices="<<numeral(static_cast<std::int32_t>(frame.vertices.size()))<<"; caras="<<numeral(static_cast<std::int32_t>(frame.faces.size()))<<"; dibujar="<<frame.draw_requested<<'\n';}
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<"SMO: "<<error.what()<<'\n';return 1;}
}

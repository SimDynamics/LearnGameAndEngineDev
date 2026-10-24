#include "presentation/windows/smo_program_gpu.hpp"
#include "presentation/windows/smo_scene.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation {
namespace {
template<class T>std::span<const std::byte> bytes(const std::vector<T>& values){return std::as_bytes(std::span(values));}
template<class T>T extract(std::span<const std::byte> raw,std::size_t offset){
    if(offset>raw.size()||sizeof(T)>raw.size()-offset)throw std::runtime_error("Lectura incompleta del programa SMO GPU.");
    T result;std::memcpy(&result,raw.data()+offset,sizeof(T));return result;
}
std::string canonical_literal(std::string_view literal,smo::Notation notation){std::string result(literal);
    for(auto& c:result){if(c==notation.ten)c='#';else if(c==notation.eleven)c='~';else if(c==notation.twelve_alias)c='@';}return result;
}
std::string failure(std::uint32_t code,std::uint32_t diagnostic){
    switch(code){case 1:return "Una cantidad queda fuera del dominio o rango F12 (diagnóstico "+std::to_string(diagnostic)+").";
    case 2:return "La pila del programa SMO GPU queda fuera de sus límites.";
    case 3:return "El estado SMO todavía no está inicializado.";
    case 4:return "La expresión no tiene una segunda interpretación válida.";
    case 5:return "Los índices de cara deben ser enteros no negativos y referir un vértice existente.";
    case 6:return "Los componentes de color deben estar entre cero y uno.";
    case 7:return "Define al menos tres vértices y una cara válida en SMO.";
    case 9:return "Una coordenada o rotación supera el límite gráfico del laboratorio SMO.";
    default:return "El programa SMO GPU contiene una instrucción o un recurso fuera de sus límites.";}
}
}
struct SmoGpuProgram::Impl {
    smo::ProgramIr syntax;
    std::unique_ptr<SmoCompute> gpu;
    std::array<SmoKernel,smo_vm_kernel_count> kernels{};
    std::array<SmoBuffer,8> buffers{};
    std::uint32_t bank=1;
    std::uint64_t revision=0;
    SmoGpuFrame current;
    std::stop_token stop;
    std::uint32_t rule_profile=0;
    explicit Impl(smo::ProgramIr program,SmoComputeApi api,const SmoComputeOptions& options,std::stop_token token,std::uint32_t profile):syntax(std::move(program)),gpu(create_smo_compute(api,options,token)),stop(token),rule_profile(profile){
        if((profile&~smo_rule_profile_mask)||(profile&smo_rule_divide_zero_mask)==smo_rule_divide_zero_mask)
            throw std::invalid_argument("El perfil experimental SMO no está definido.");
        if(syntax.states.size()>256||syntax.literals.size()>8192||!syntax.max_stack||syntax.max_stack>8192||syntax.vertices<3||syntax.vertices>1024||!syntax.faces||syntax.faces>2048)
            throw std::invalid_argument("El programa SMO excede los límites de estado, pila o geometría del laboratorio.");
        std::vector<SmoF12> literals;for(const auto& text:syntax.literals)literals.push_back(smo_f12_parse(canonical_literal(text,syntax.notation)));
        if(literals.empty())literals.push_back(smo_f12_zero());
        buffers[0]=gpu->buffer(literals.size()*sizeof(SmoF12),bytes(literals));
        buffers[1]=gpu->buffer(std::max<std::size_t>(1,syntax.states.size())*2*sizeof(SmoF12Result));
        buffers[2]=gpu->buffer(static_cast<std::size_t>(syntax.max_stack)*sizeof(SmoF12Result));
        buffers[3]=gpu->buffer(static_cast<std::size_t>(syntax.vertices)*2*smo_vm_vertex_words*4);
        buffers[4]=gpu->buffer(static_cast<std::size_t>(syntax.faces)*2*smo_vm_face_words*4);
        buffers[5]=gpu->buffer(2*smo_vm_frame_words*4);buffers[6]=gpu->buffer(32*4);
        std::set<unsigned> used{smo_vm_prepare,smo_vm_finalize,smo_vm_elapsed};
        for(const auto* sequence:{&syntax.initial,&syntax.update,&syntax.draw})for(const auto& instruction:*sequence)used.insert(static_cast<unsigned>(instruction.opcode));
        for(const auto opcode:used){if(stop.stop_requested())throw std::runtime_error("Preparación SMO GPU cancelada.");
            if(opcode>=smo_vm_kernel_count)throw std::invalid_argument("Opcode SMO GPU no reconocido.");kernels[opcode]=gpu->kernel(smo_program_vm_kernel(opcode));}
        (void)transact(true,false,smo_f12_zero(),{});
    }
    std::array<std::uint32_t,16> constants(std::uint32_t work,bool reset,bool updating)const{
        std::array<std::uint32_t,16> result{};result[4]=work;result[5]=bank;result[6]=static_cast<std::uint32_t>(syntax.states.size());
        result[7]=syntax.max_stack;result[8]=syntax.vertices;result[9]=syntax.faces;result[10]=static_cast<std::uint32_t>(syntax.literals.size());result[11]=reset?1u:0u;result[12]=updating?1u:0u;result[13]=rule_profile;return result;
    }
    SmoGpuFrame transact(bool reset,bool updating,SmoF12 dt,std::optional<std::uint64_t> elapsed){
        if(stop.stop_requested())throw std::runtime_error("Programa SMO GPU cancelado.");
        const auto work=1u-bank;auto parameters=constants(work,reset,updating);
        std::array<std::uint32_t,32> control{};std::memcpy(control.data()+16,&dt,sizeof(dt));
        if(elapsed){control[22]=static_cast<std::uint32_t>(*elapsed);control[23]=static_cast<std::uint32_t>(*elapsed>>32);}
        gpu->write(buffers[6],std::as_bytes(std::span(control)));
        std::vector<SmoDispatch> commands;const auto words=std::max({static_cast<std::uint32_t>(syntax.states.size())*13u,syntax.vertices*18u,syntax.faces*3u,64u});
        commands.push_back({kernels[smo_vm_prepare],buffers,parameters,{(words+63u)/64u,1,1}});
        if(elapsed)commands.push_back({kernels[smo_vm_elapsed],buffers,parameters,{1,1,1}});
        const auto append=[&](const std::vector<smo::IrInstruction>& sequence){for(std::size_t pc=0;pc<sequence.size();++pc){const auto& instruction=sequence[pc];
            auto values=parameters;values[0]=instruction.operand;values[1]=instruction.line;values[2]=instruction.column;values[3]=static_cast<std::uint32_t>(pc);
            commands.push_back({kernels[static_cast<unsigned>(instruction.opcode)],buffers,values,{1,1,1}});}};
        if(reset)append(syntax.initial);if(updating)append(syntax.update);append(syntax.draw);
        commands.push_back({kernels[smo_vm_finalize],buffers,parameters,{1,1,1}});(void)gpu->execute(commands,false);
        const auto observed=gpu->read(buffers[6]);const auto code=extract<std::uint32_t>(observed,0),diagnostic=extract<std::uint32_t>(observed,4);
        if(code){const auto line=extract<std::uint32_t>(observed,8),column=extract<std::uint32_t>(observed,12);
            throw std::runtime_error((line?"Línea "+std::to_string(line)+", columna "+std::to_string(column)+": ":std::string{})+failure(code,diagnostic));}
        const auto frames=gpu->read(buffers[5]);const auto offset=static_cast<std::size_t>(work)*smo_vm_frame_words*4;
        SmoGpuFrame candidate;candidate.vertices=buffers[3];candidate.faces=buffers[4];candidate.frame=buffers[5];
        candidate.vertex_word_offset=work*syntax.vertices*smo_vm_vertex_words;candidate.face_word_offset=work*syntax.faces*smo_vm_face_words;candidate.frame_word_offset=work*smo_vm_frame_words;
        candidate.vertex_count=extract<std::uint32_t>(frames,offset+36*4);candidate.face_count=extract<std::uint32_t>(frames,offset+37*4);
        candidate.draw_requested=extract<std::uint32_t>(frames,offset+38*4)!=0;candidate.diagnostic=extract<std::uint32_t>(frames,offset+39*4);candidate.revision=revision+1;
        if(candidate.vertex_count<3||candidate.vertex_count>syntax.vertices||!candidate.face_count||candidate.face_count>syntax.faces||(candidate.diagnostic&smo_d12_fatal_mask))
            throw std::runtime_error("El programa SMO GPU produjo geometría o diagnósticos fuera de rango.");
        bank=work;revision=candidate.revision;current=candidate;return current;
    }
};
SmoGpuProgram::SmoGpuProgram(std::unique_ptr<Impl> value):impl_(std::move(value)){}
SmoGpuProgram::SmoGpuProgram(SmoGpuProgram&&)noexcept=default;
SmoGpuProgram& SmoGpuProgram::operator=(SmoGpuProgram&&)noexcept=default;
SmoGpuProgram::~SmoGpuProgram()=default;
SmoGpuProgram SmoGpuProgram::compile(std::string_view source,SmoComputeApi api,const SmoComputeOptions& options,std::stop_token stop,smo::Notation notation,std::uint32_t rule_profile){return SmoGpuProgram(std::make_unique<Impl>(smo::compile_ir(source,notation),api,options,stop,rule_profile));}
void SmoGpuProgram::reset(){if(!impl_)throw std::logic_error("Programa SMO GPU movido.");(void)impl_->transact(true,false,smo_f12_zero(),{});}
SmoGpuFrame SmoGpuProgram::frame(){if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->transact(false,false,smo_f12_zero(),{});}
SmoGpuFrame SmoGpuProgram::step(SmoF12 dt){if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->transact(false,true,dt,{});}
SmoGpuFrame SmoGpuProgram::step_elapsed(std::uint64_t nanoseconds){if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->transact(false,true,smo_f12_zero(),nanoseconds);}
SmoGpuFrame SmoGpuProgram::current_frame()const{if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->current;}
SmoF12Result SmoGpuProgram::variableExact(std::string_view name)const{if(!impl_)throw std::logic_error("Programa SMO GPU movido.");const auto found=std::find(impl_->syntax.states.begin(),impl_->syntax.states.end(),name);
    if(found==impl_->syntax.states.end())throw std::invalid_argument("Estado SMO desconocido: "+std::string(name));const auto index=static_cast<std::size_t>(found-impl_->syntax.states.begin());
    const auto raw=impl_->gpu->read(impl_->buffers[1]);return extract<SmoF12Result>(raw,(static_cast<std::size_t>(impl_->bank)*impl_->syntax.states.size()+index)*sizeof(SmoF12Result));}
SmoCompute& SmoGpuProgram::compute(){if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return *impl_->gpu;}
SmoComputeInfo SmoGpuProgram::info()const{if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->gpu->info();}
const smo::ProgramIr& SmoGpuProgram::ir()const{if(!impl_)throw std::logic_error("Programa SMO GPU movido.");return impl_->syntax;}
} //namespace lgaed::presentation

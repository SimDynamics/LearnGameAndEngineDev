#include "presentation/windows/smo_radix_ablation.hpp"
#include "platform/windows/state_store.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace lgaed::presentation { namespace {
void canceled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Ablación radix12 cancelada.");}
void validate_workload(std::uint32_t items,std::uint32_t groups,std::uint32_t terms){
    if(!items||items>1048576u||!groups||groups>1024u||groups>items||!terms||terms>4096u||static_cast<std::uint64_t>(groups)*terms>1048576u)
        throw std::invalid_argument("Carga exacta fuera de límites:1..1048576 elementos,1..1024 grupos,1..4096 términos y tabla de hasta1048576 términos.");}
std::uint32_t random_word(std::uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
std::uint64_t integer_power(std::uint32_t base,std::uint32_t power){const auto squared=static_cast<std::uint64_t>(base)*base;return power==2?squared:squared*base;}
template<class T>std::span<const std::byte> bytes(const std::vector<T>& values){return std::as_bytes(std::span(values));}
std::vector<SmoRadixRecord> read(SmoCompute& gpu,SmoBuffer buffer,std::size_t count){const auto raw=gpu.read(buffer);
    if(raw.size()!=count*sizeof(SmoRadixRecord))throw std::runtime_error("Lectura de ablación GPU incompleta.");std::vector<SmoRadixRecord> result(count);std::memcpy(result.data(),raw.data(),raw.size());return result;}
std::uint64_t hash(std::span<const SmoRadixRecord> data){std::uint64_t result=14695981039346656037ull;
    for(const auto& record:data){const auto words=std::bit_cast<std::array<std::uint32_t,8>>(record);for(const auto word:words)for(unsigned byte=0;byte<4;++byte){result^=(word>>(byte*8))&255u;result*=1099511628211ull;}}return result;}
double median(const std::vector<double>& samples){if(samples.empty()||std::ranges::any_of(samples,[](double value){return !std::isfinite(value)||value<=0;}))return std::numeric_limits<double>::quiet_NaN();
    auto ordered=samples;std::ranges::sort(ordered);const auto half=ordered.size()/2;return ordered.size()%2?ordered[half]:(ordered[half-1]+ordered[half])*.5;}
std::unique_ptr<SmoCompute> create(const SmoRadixAblationOptions& options,std::stop_token stop){switch(options.api){
    case SmoComputeApi::direct3d11:return create_smo_d3d11_compute(options.device,stop);case SmoComputeApi::direct3d12:return create_smo_d3d12_compute(options.device,stop);
    case SmoComputeApi::vulkan:return create_smo_vulkan_compute(options.device,stop);case SmoComputeApi::opengl:return create_smo_opengl_compute(options.device,stop);}
    throw std::invalid_argument("API GPU desconocida.");}
struct Batch {
    SmoCompute& gpu;std::array<SmoKernel,6> kernels;std::array<SmoBuffer,2> input{},terms{},common{},expected{};std::array<SmoBuffer,4> output{};SmoBuffer counters=0;std::array<std::uint32_t,16> constants{};
    Batch(SmoCompute& compute,const std::array<SmoKernel,6>& shaders,const SmoRadixWorkload& data):gpu(compute),kernels(shaders){
        const auto count=data.items[0].size();constants[0]=static_cast<std::uint32_t>(count);constants[1]=data.groups;constants[2]=data.terms;
        for(unsigned representation=0;representation<2;++representation){input[representation]=gpu.buffer(count*sizeof(SmoRadixRecord),bytes(data.items[representation]));
            terms[representation]=gpu.buffer(data.coefficients[representation].size()*sizeof(SmoRadixTerm),bytes(data.coefficients[representation]));common[representation]=gpu.buffer(data.groups*sizeof(SmoRadixRecord));
            auto raw_expected=data.expected;if(representation)for(auto& record:raw_expected)for(auto& value:record.value)value=smo_radix_pack12(value);
            expected[representation]=gpu.buffer(count*sizeof(SmoRadixRecord),bytes(raw_expected));}
        const std::vector<std::uint32_t> zero(8,0);counters=gpu.buffer(zero.size()*4,bytes(zero));for(auto& buffer:output)buffer=gpu.buffer(count*sizeof(SmoRadixRecord));}
    void begin_verification(){const std::vector<std::uint32_t> zero(8,0);gpu.write(counters,bytes(zero));constants[3]=1;}
    double execute(unsigned mode,bool timed){const auto representation=mode/2u;const auto grouped=(mode%2u)!=0;const auto first=representation*3u;
        constants[4]=mode;const std::array<SmoBuffer,8> buffers{input[representation],terms[representation],common[representation],output[mode],expected[representation],0,0,counters};std::array<SmoDispatch,2> calls;
        if(grouped){calls[0]={kernels[first+1u],buffers,constants,{(constants[1]+63u)/64u,1,1}};calls[1]={kernels[first+2u],buffers,constants,{(constants[0]+63u)/64u,1,1}};return gpu.execute(calls,timed);}
        calls[0]={kernels[first],buffers,constants,{(constants[0]+63u)/64u,1,1}};return gpu.execute(std::span(calls.data(),1),timed);}
};
std::array<unsigned,4> order(unsigned round){std::array<unsigned,4> result{};for(unsigned i=0;i<4;++i)result[i]=((round/4u)%2u==0u?(i+round)%4u:(3u-i+round)%4u);return result;}
} //namespace

std::uint32_t smo_radix_pack12(std::uint32_t value){if(value>smo_radix_ablation_limit)throw std::out_of_range("La cantidad excede ocho cifras radix12.");std::uint32_t result=0;
    for(unsigned digit=0;digit<8;++digit){result|=(value%12u)<<(digit*4u);value/=12u;}return result;}
std::uint32_t smo_radix_unpack12(std::uint32_t value){std::uint32_t result=0;for(unsigned digit=8;digit>0;--digit){const auto coefficient=(value>>((digit-1u)*4u))&15u;
        if(coefficient>=12u)throw std::invalid_argument("Cifra packed12 inválida.");result=result*12u+coefficient;}return result;}
SmoRadixWorkload smo_radix_workload(std::uint32_t count,std::uint32_t groups,std::uint32_t terms,std::uint32_t seed){validate_workload(count,groups,terms);SmoRadixWorkload data;data.groups=groups;data.terms=terms;
    for(auto& items:data.items)items.resize(count);for(auto& table:data.coefficients)table.resize(static_cast<std::size_t>(groups)*terms);data.expected.resize(count);
    std::vector<std::array<std::uint64_t,3>> sums(groups);auto state=seed?seed:0x13579bdu;
    for(std::uint32_t group=0;group<groups;++group)for(std::uint32_t term=0;term<terms;++term){const auto at=static_cast<std::size_t>(group)*terms+term;SmoRadixTerm binary,packed;
        binary.power=packed.power=2u+(random_word(state)&1u);for(unsigned axis=0;axis<3;++axis){binary.value[axis]=1u+random_word(state)%7u;packed.value[axis]=smo_radix_pack12(binary.value[axis]);
            sums[group][axis]+=integer_power(binary.value[axis],binary.power);if(sums[group][axis]>smo_radix_ablation_limit)throw std::overflow_error("La suma de términos salió del dominio exacto.");}
        data.coefficients[0][at]=binary;data.coefficients[1][at]=packed;}
    for(std::uint32_t item=0;item<count;++item){const auto group=item%groups;auto& binary=data.items[0][item];auto& packed=data.items[1][item];auto& expected=data.expected[item];binary.group=packed.group=expected.group=group;
        for(unsigned axis=0;axis<3;++axis){const auto available=smo_radix_ablation_limit-static_cast<std::uint32_t>(sums[group][axis]);const auto mode=(item+axis)%9u;std::uint32_t local=0;
            if(mode<6u){constexpr std::array<std::uint32_t,6> boundary{0,11,12,143,144,1728};local=std::min(available,boundary[mode]);}
            else if(mode==6u)local=available;else if(mode==7u)local=available?available-1u:0u;else local=random_word(state)%(available+1u);
            binary.value[axis]=local;packed.value[axis]=smo_radix_pack12(local);expected.value[axis]=local+static_cast<std::uint32_t>(sums[group][axis]);}}
    return data;}
SmoRadixAblationReport run_smo_radix_ablation(const SmoRadixAblationOptions& options,std::stop_token stop){canceled(stop);validate_workload(options.items,options.groups,options.terms);
    if(options.warmups!=2||!options.rounds||options.rounds>32||options.rule!=SmoRadixRuleContract::ordinary_exact||!std::isfinite(options.sustained_seconds)||(options.sustained_seconds!=0&&options.sustained_seconds!=60))
        throw std::invalid_argument("Ablación: dos calentamientos y1..32 rondas; duración0 o60 segundos y sólo contrato entero ordinario exacto.");
    const auto data=smo_radix_workload(options.items,options.groups,options.terms,options.seed);canceled(stop);auto gpu=create(options,stop);std::array<SmoKernel,6> kernels{};std::string identity;
    for(unsigned stage=0;stage<6;++stage){canceled(stop);const auto source=smo_radix_ablation_kernel(stage);identity+=source.hlsl;identity+=source.glsl;kernels[stage]=gpu->kernel(source);}
    SmoRadixAblationReport report;report.device=gpu->info();report.source_identity=state::sha256(identity);report.items=options.items;report.groups=options.groups;report.terms=options.terms;report.warmups=options.warmups;report.rounds=options.rounds;
    constexpr std::array<const char*,4> names{"A native uint32 por elemento","B native uint32 agrupado","C packed radix12 por elemento","D packed radix12 agrupado"};
    for(unsigned mode=0;mode<4;++mode){auto& row=report.variants[mode];row.name=names[mode];row.item_term_evaluations=static_cast<std::uint64_t>(mode%2u?options.groups:options.items)*options.terms;row.scalar_powers=row.item_term_evaluations*3u;}
    Batch batch(*gpu,kernels,data);for(unsigned warmup=0;warmup<options.warmups;++warmup)for(const auto mode:order(warmup)){canceled(stop);(void)batch.execute(mode,false);}
    const bool sustained=options.sustained_seconds!=0;const auto minimum_rounds=sustained?std::max(9u,options.rounds):options.rounds;if(sustained)batch.begin_verification();
    // A minute is WALL time of the executed alternating rounds, including
    // sparse untimed readbacks. Every dispatch is checked on GPU; raw captures
    // merely supply independent host evidence, not the all-frame coverage.
    // At most9 early+16 geometric+1 final snapshots, independent of frame rate.
    struct Snapshot {std::uint64_t round=0;std::array<std::vector<SmoRadixRecord>,4> raw;};std::vector<Snapshot> snapshots;
    constexpr std::uint64_t timing_sample_limit=1048576u;
    const auto begin=std::chrono::steady_clock::now();std::uint64_t actual_rounds=0;
    do{canceled(stop);const auto round=actual_rounds;const bool capture=!sustained||round<9u||((round+1u)<=524288u&&std::has_single_bit(round+1u));Snapshot snapshot;snapshot.round=round;
        for(const auto mode:order(static_cast<unsigned>(round%8u))){canceled(stop);const auto milliseconds=batch.execute(mode,true);auto& result=report.variants[mode];
            if(result.gpu_ms.size()<timing_sample_limit)result.gpu_ms.push_back(milliseconds);else++result.timing_samples_discarded;
            if(capture)snapshot.raw[mode]=read(*gpu,batch.output[mode],options.items);}
        if(capture)snapshots.push_back(std::move(snapshot));++actual_rounds;
        report.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    }while(actual_rounds<minimum_rounds||(sustained&&report.elapsed_seconds<options.sustained_seconds));
    report.rounds_actual=actual_rounds;
    if(snapshots.empty()||snapshots.back().round!=actual_rounds-1u){Snapshot final;final.round=actual_rounds-1u;for(unsigned mode=0;mode<4;++mode){canceled(stop);final.raw[mode]=read(*gpu,batch.output[mode],options.items);}snapshots.push_back(std::move(final));}
    report.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(sustained){const auto raw=gpu->read(batch.counters);if(raw.size()!=8u*sizeof(std::uint32_t))throw std::runtime_error("Contadores de verificación GPU incompletos.");std::array<std::uint32_t,8> counters{};std::memcpy(counters.data(),raw.data(),raw.size());
        for(unsigned mode=0;mode<4;++mode){report.variants[mode].gpu_mismatches=counters[mode*2u]|(static_cast<std::uint64_t>(counters[mode*2u+1u])<<32u);report.variants[mode].gpu_verified_records=actual_rounds*options.items;}}
    report.equivalent=true;report.timing_valid=report.device.timestamps;
    for(unsigned mode=0;mode<4;++mode){auto& row=report.variants[mode];row.median_ms=row.timing_samples_discarded?std::numeric_limits<double>::quiet_NaN():median(row.gpu_ms);if(!std::isfinite(row.median_ms))report.timing_valid=false;
        if(row.gpu_mismatches)report.equivalent=false;
        for(const auto& snapshot:snapshots){canceled(stop);auto canonical=snapshot.raw[mode];for(std::uint32_t item=0;item<options.items;++item){auto& actual=canonical[item];std::string reason;
                try{if(mode>=2)for(auto& value:actual.value)value=smo_radix_unpack12(value);}catch(const std::invalid_argument&){reason="Dígito radix12 de salida inválido.";}
                if(reason.empty()&&actual!=data.expected[item])reason="Cantidad/grupo/padding no coincide exactamente con el oráculo entero.";
                if(!reason.empty()){++row.mismatches;report.equivalent=false;if(report.failures.size()<16)report.failures.push_back({mode,static_cast<unsigned>(snapshot.round),item,data.expected[item],snapshot.raw[mode][item],actual,std::move(reason)});}}
            row.raw_hash=hash(snapshot.raw[mode]);row.canonical_hash=hash(canonical);}}
    if(report.equivalent&&report.timing_valid){const auto a=report.variants[0].median_ms,b=report.variants[1].median_ms,c=report.variants[2].median_ms,d=report.variants[3].median_ms;
        report.binary_grouping_ratio=a/b;report.packed_grouping_ratio=c/d;report.per_item_representation_ratio=a/c;report.grouped_representation_ratio=b/d;
        report.conclusion=sustained?"Minuto sostenido: verificación GPU exacta de todos los registros en todas las rondas, más capturas independientes. Los tiempos INCLUYEN la comparación de ocho palabras, idéntica en las cuatro variantes; no son aritmética pura ni FPS de la aplicación.":
            "Resultados enteros exactos iguales: representación y agrupación se comparan por separado. Los tiempos son sólo GPU, no FPS de la aplicación.";}
    else report.conclusion=report.equivalent?"Resultados exactos iguales, pero algún reloj GPU es inválido: no se acredita rendimiento.":"Las salidas no son exactamente equivalentes: no se acredita ninguna mejora de rendimiento.";
    return report;}
} //namespace lgaed::presentation

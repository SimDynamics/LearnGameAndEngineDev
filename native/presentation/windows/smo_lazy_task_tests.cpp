#include "presentation/windows/smo_lazy_task.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc,char** argv){using namespace lgaed::presentation;try{
    std::cout<<"Compiler/deferred contract checks="<<smo_lazy_task_contract_checks()<<"\n";
    if(argc>1&&std::string_view(argv[1])=="cpu")return 0;
    SmoLazyTaskOptions o;o.items=129;o.rounds=3;o.frames=3;o.warmups=1;
    bool all=false;if(argc>1&&std::string_view(argv[1])=="hardware"){o.device.software=false;o.items=16384;o.rounds=11;o.frames=8;if(argc>2){const std::string_view api=argv[2];if(api=="all")all=true;else if(api=="d3d12")o.api=SmoComputeApi::direct3d12;else if(api=="vulkan")o.api=SmoComputeApi::vulkan;else if(api=="opengl")o.api=SmoComputeApi::opengl;}}
    if(argc>3)o.items=static_cast<std::uint32_t>(std::stoul(argv[3]));
    if(argc>4)o.active_percent=static_cast<std::uint32_t>(std::stoul(argv[4]));
    if(argc>5)o.grouped=std::string_view(argv[5])!="mixed";
    if(argc>6)o.change_every=static_cast<std::uint32_t>(std::stoul(argv[6]));
    std::array<std::uint64_t,5> first_hash{},first_image_hash{};bool first=true;
    for(unsigned index=0;index<(all?4u:1u);++index){if(all)o.api=static_cast<SmoComputeApi>(index);
        const auto r=run_smo_lazy_task(o);if(!r.equivalent)throw std::runtime_error("Channel task outputs differ.");
        std::cout<<r.device.api<<" / "<<r.device.adapter<<" items="<<r.items<<" active="<<r.active_items<<" grouped="<<r.grouped<<" changes_every="<<r.change_every<<" frames="<<r.frames<<" verifiedwords="<<r.verified_words<<" source="<<r.source_hash<<" prep_ms="<<r.preparation_ms<<" parse_ms="<<r.source_compile_ms<<" shader_ms="<<r.shader_prepare_ms<<" upload_ms="<<r.upload_ms<<" trial_resets_ms="<<r.trial_reset_ms<<" buffers="<<r.buffer_bytes<<" group_extra_bytes="<<r.group_extra_buffer_bytes<<" peakWS="<<r.process_peak_working_set<<"\n";
        for(unsigned mode=0;mode<r.variants.size();++mode){const auto& v=r.variants[mode];std::cout<<"mode="<<mode<<" gpu_ms="<<v.gpu_ms<<" frame_gpu_ms="<<v.frame_gpu_ms<<" frame_ms="<<v.frame_ms<<" read_ms="<<v.readback_ms<<" convert_ms="<<v.conversion_ms<<" initial_group_ms="<<v.initial_group_gpu_ms<<" initial_group_wall_ms="<<v.initial_group_wall_ms<<" group_alloc_ms="<<v.group_allocation_ms<<" regroup_probe_ms="<<v.group_gpu_ms<<" rebuilds="<<v.group_rebuilds<<" logical_IR_ops="<<v.executed_instructions<<" writes="<<v.writes<<" hash="<<v.output_hash<<" image_hash="<<v.image_hash<<" verified="<<v.verified<<" timing="<<v.timing_valid<<"\n";
            if(first){first_hash[mode]=v.output_hash;first_image_hash[mode]=v.image_hash;}else if(first_hash[mode]!=v.output_hash||first_image_hash[mode]!=v.image_hash)throw std::runtime_error("Cross-API channel trajectory/image mismatch; no tolerance is used.");}
        first=false;
    }
    if(!all&&o.device.software&&argc==1){
        for(const unsigned percent:{0u,25u,75u,100u})for(const bool grouped:{false,true}){o.active_percent=percent;o.grouped=grouped;o.rounds=1;o.frames=2;o.warmups=0;const auto r=run_smo_lazy_task(o);if(!r.equivalent)throw std::runtime_error("Channel ratio/layout mismatch.");}
        std::cout<<"0/25/50/75/100 percent and grouped/mixed exact GPU checks PASS\n";
        for(const auto count:{1u,65u,129u})for(const auto period:{1u,4u}){o.items=count;o.active_percent=50;o.change_every=period;o.frames=5;const auto r=run_smo_lazy_task(o);if(!r.equivalent)throw std::runtime_error("Changing-mask grouping failed at a partial workgroup boundary.");if(count>1&&r.variants[3].group_rebuilds==0)throw std::runtime_error("Changing masks did not rebuild their GPU index lists.");}
        std::cout<<"Changing masks at periods1/4, counts1/65/129 and five variants PASS\n";
    }
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}

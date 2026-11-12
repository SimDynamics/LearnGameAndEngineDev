#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;

void check(bool allowed,std::string_view description) {
    if(!allowed)throw std::runtime_error(std::string(description));
}

void verify_locale(const std::filesystem::path& path,std::string_view locale) {
    const auto original=windows::load_lesson(path/(std::string(locale)+".before.json"),locale);
    const auto formatted=windows::load_lesson(path/(std::string(locale)+".after.json"),locale);
    check(original.id==formatted.id&&original.locale==formatted.locale&&
        original.pages.size()==formatted.pages.size()&&original.pages.size()==1,
        "The reformatted lesson changed its logical owner or number of pages");
    check(original.state_layout.signatures==formatted.state_layout.signatures&&
        original.state_layout.lineage==formatted.state_layout.lineage&&
        original.state_layout.legacy_lineage==formatted.state_layout.legacy_lineage,
        "A source-format revision changed the immutable state signatures or lineage");
    check(formatted.state_layout.source_format_revisions.size()==5,
        "One of the five authenticated source-format archives was not loaded");

    const auto& initial=original.pages.front().blocks;
    const auto& current=formatted.pages.front().blocks;
    check(initial.size()==current.size()&&initial.size()==6,
        "The formatted lesson lost a presentation, activity or reference");
    for(std::size_t i=0;i<initial.size();++i) {
        const auto& before=initial[i],after=current[i];
        check(before.stable_id==after.stable_id&&before.type==after.type&&
            before.address.page==after.address.page&&before.address.block==after.address.block&&
            curriculum::exercise_key(original,before)==curriculum::exercise_key(formatted,after)&&
            curriculum::draft_key("programming",original,before)==
                curriculum::draft_key("programming",formatted,after),
            "Formatting changed a historical exercise owner or draft storage key");
    }
    check(initial[0].code!=current[0].code&&current[0].code.find("\n\n")!=std::string::npos,
        "Formatted example does not expose new physical source line breaks");
    check(initial[1].code!=current[1].code&&initial[1].answers==current[1].answers,
        "Reformatted prediction changed the expected student answer");
    check(initial[2].fragments!=current[2].fragments&&
        initial[2].correct_order==current[2].correct_order&&
        curriculum::order_representation_frame(initial[2])==
        curriculum::order_representation_frame(current[2]),
        "The graded order puzzle lost its canonical original representation");
    check(initial[3].code_template!=current[3].code_template&&
        initial[3].correct_order==current[3].correct_order,
        "The drag-fill template changed its holes or canonical choices");
    check(initial[4].initial_code!=current[4].initial_code&&
        initial[4].project_files!=current[4].project_files&&
        initial[4].output_check.has_value()&&current[4].output_check.has_value()&&
        curriculum::reference_objective_frame(original,initial[4])==
        curriculum::reference_objective_frame(formatted,current[4]),
        "A compiler formatting revision changed the stored execution objective");
    check(current[4].source_format_original_initial_code==initial[4].initial_code&&
        current[4].source_format_original_project_files==initial[4].project_files,
        "The compiler lost its exact pre-format source or project file snapshot");

    state::StateData saved;
    state::prepare_lesson_state(saved,original);
    state::Response prior;
    prior.correct=true;prior.order={0,1,2,3};
    state::put_response(saved,original,initial[2],prior);
    state::Draft draft;
    draft.source="student's exact unformatted C++ source\nreturn 0;";
    draft.input="7\n";
    draft.selection_start=3;draft.selection_end=9;draft.has_saved=true;
    const auto key=curriculum::draft_key("programming",original,initial[4]);
    state::put_draft(saved,key,draft);
    state::save_current_page(saved,original,0);
    const auto bytes=state::encode(saved);
    const auto reopened=state::decode(bytes);
    check(reopened==saved,"State encoding changed student progress or preferences");
    state::validate_lesson_state(reopened,formatted);
    const auto restored=state::get_response(reopened,formatted,current[2]);
    check(restored.has_value()&&restored->correct&&restored->order==prior.order,
        "A previously graded ordering answer was lost after source formatting");
    const auto loaded=state::get_draft(reopened,
        curriculum::draft_key("programming",formatted,current[4]),current[4].initial_code,
        current[4].standard_input.value_or(""));
    check(loaded.has_saved&&loaded.source==draft.source&&loaded.input==draft.input&&
        loaded.selection_start==draft.selection_start&&loaded.selection_end==draft.selection_end,
        "Reopening a revised lesson replaced the student's saved source or cursor");
    check(state::current_page(reopened,formatted)==0,
        "The historic page position moved after source formatting");

    const auto metadata=windows::load_lesson_progress_metadata(
        path/(std::string(locale)+".after.json"),locale);
    check(metadata.state_layout.lineage==formatted.state_layout.lineage&&
        metadata.pages.size()==formatted.pages.size()&&
        metadata.pages.front().blocks.size()==current.size(),
        "Read-only course progress uses a different historical source mapping");
}

void must_reject(const std::filesystem::path& file) {
    for(const bool catalog:{false,true}) {
        bool rejected=false;
        try {
            if(catalog)(void)windows::load_lesson_progress_metadata(file,"es");
            else (void)windows::load_lesson(file,"es");
        }catch(const std::exception&) {rejected=true;}
        check(rejected,"A forged contract or original archive was admitted by Windows");
    }
}
}

int main(int argc,char** argv) {
    try{
        if(argc!=2)throw std::invalid_argument("Expected isolated fixture directory");
        const std::filesystem::path dir(argv[1]);
        for(const auto locale:{"es","en","fr"})verify_locale(dir,locale);
        must_reject(dir/"es.changed-answer.json");
        must_reject(dir/"es.forged-archive.json");
        std::cout<<"PASS native C++/CMake physical-format loader in es/en/fr: unchanged state/answer/draft keys, exact pre-format receipts, optional source formatting, negative forgeries and progress-catalog reopens\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL native physical-format loader: "<<error.what()<<'\n';
        return 1;
    }
}

#include "core/lesson.hpp"
#include "core/u6_diagnostic_test_fixture.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
void require(bool condition,std::string_view message){if(!condition)throw std::runtime_error(std::string(message));}
struct Fixture {std::string_view lesson,owner,diagnostic,file;std::size_t first,last,changed_files;bool link;};
constexpr std::array fixtures{
    Fixture{"declaraciones_y_firmas","block_0014_0002","u6-difference-unselected-definition","",0,0,0,true},
    Fixture{"declaraciones_y_firmas","block_0015_0002","u6-difference-conflicting-return","diferencia.cpp",3,3,1,false},
    Fixture{"declaraciones_y_firmas","block_0017_0002","u6-difference-missing-header","main.cpp",1,1,1,false},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0016_0002","u6-envelopes-missing-header","main.cpp",6,7,1,false},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0017_0002","u6-envelopes-missing-declaration","main.cpp",7,8,1,false},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0018_0002","u6-envelopes-unselected-definitions","",0,0,0,true},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0019_0002","u6-envelopes-duplicate-definition","",0,0,2,true},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0021_0002","u6-series-unknown-call","main.cpp",6,6,1,false},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0022_0002","u6-series-unselected-definitions","",0,0,0,true},
    Fixture{"interfaces_cabeceras_y_programas_con_varios_archivos","block_0023_0002","u6-series-duplicate-definition","",0,0,2,true},
    Fixture{"construir_un_proyecto_desde_su_interfaz","block_0013_0002","u6-shelf-unselected-definition","",0,0,0,true},
    Fixture{"construir_un_proyecto_desde_su_interfaz","block_0014_0002","u6-shelf-conflicting-return","conteo.cpp",8,8,1,false}
};
const std::string& source(const CppProjectFiles& files,std::string_view path){
    const auto found=std::ranges::find(files,path,[](const auto& file)->std::string_view{return file.first;});
    require(found!=files.end(),"A complete U6 project lost an owned file");return found->second;
}
std::vector<std::string> all_sources(const CppProjectFiles& files){
    std::vector<std::string> result;for(const auto& [path,code]:files)if(path.ends_with(".cpp"))result.push_back(path);return result;
}
}

int main() try {
    std::map<std::string,Lesson> originals;
    std::size_t projects=0,compile=0,link=0,negative=0;
    for(const auto& item:fixtures){
        const auto file="lesson_"+std::string(item.lesson)+".json";
        if(!originals.contains(file))originals.emplace(file,test::load_pre_u6_diagnostic_lesson(file));
        const auto& lesson=originals.at(file);
        const auto position=stable_block_position(lesson,item.owner);require(position.has_value(),"A current U6 editor changed its stable identity");
        auto block=lesson.pages[position->page].blocks[position->block];
        require(!block.compile_check&&!block.output_check&&!block.project_files.empty(),"The source fixture no longer describes the pending U6 diagnostic");
        // Isolate the proposed recipe only. No authored goal, answer, progress
        // or process success is created by this source-plan test.
        block.compile_check=CompileCheck{std::string(item.diagnostic),"QA source plan only."};
        const auto initial=block.project_files;const auto selected=block.source_files;
        const auto plan=prepare_project_compile_diagnostic_source(block,initial,selected);
        require(plan&&plan->project,"The real U6 prepared project has no closed diagnostic source plan");
        const auto& project=*plan->project;
        require(project.link==item.link&&project.expected_file==item.file&&project.first_line==item.first&&project.last_line==item.last,
            "The U6 rejection lost its requested phase, translation unit or actual source interval");
        require(plan->candidate_source==source(initial,"main.cpp")&&plan->control_source==source(project.control_files,"main.cpp"),
            "A project plan rewrote its original main or returned another control source");
        require(plan->first_line==project.first_line&&plan->last_line==project.last_line&&
            !project.error_codes.empty()&&plan->expected_msvc_error==project.error_codes.front(),"The legacy and project plan projections disagree");
        require(project.control_sources==all_sources(initial),"The valid control changed the intended complete translation-unit selection");
        require(project.control_files.size()==initial.size(),"A control repair removed an owned file");
        std::size_t changed=0;for(std::size_t n=0;n<initial.size();++n){
            require(initial[n].first==project.control_files[n].first,"A control repair renamed an owned file");
            changed+=initial[n].second!=project.control_files[n].second;
        }
        require(changed==item.changed_files,"The control repaired more than the requested U6 fault");
        if(item.link){
            ++link;const bool duplicate=item.diagnostic.ends_with("duplicate-definition");
            require(project.error_codes==(duplicate?std::vector<std::string>{"LNK2005","LNK1169"}:
                std::vector<std::string>{"LNK2019","LNK1120"})&&!project.symbols.empty(),
                "A link objective accepts arbitrary failure instead of its exact symbol/code category");
        }else{
            ++compile;const std::vector<std::string> codes=item.diagnostic.ends_with("conflicting-return")?
                std::vector<std::string>{"C2556","C2371"}:item.diagnostic=="u6-difference-missing-header"?
                std::vector<std::string>{"C1083"}:std::vector<std::string>{"C3861","C2593"};
            require(project.error_codes==codes,"A compiler rejection lost its expected primary code family");
        }
        require(!prepare_project_compile_diagnostic_source(block,project.control_files,project.control_sources),
            "The repaired valid control could accredit the intended candidate rejection");++negative;
        require(!prepare_compile_diagnostic_source(block,block.initial_code),"The old single-source contract silently accepted a multifile project");

        auto decorated=initial;
        for(auto& [path,code]:decorated)code="// QA original source preserved\r\n"+code+"\n/* QA harmless tail */\n";
        const auto spaced=prepare_project_compile_diagnostic_source(block,decorated,selected);
        require(spaced&&spaced->project&&spaced->candidate_source==source(decorated,"main.cpp"),
            "Harmless comments/whitespace were rejected or the raw candidate was normalized");
        require(spaced->project->first_line==(item.first?item.first+1:0)&&
            spaced->project->last_line==(item.last?item.last+1:0),"CRLF comments detached diagnostics from the actual candidate lines");
        for(const auto& [path,code]:spaced->project->control_files)
            require(code.find("// QA original source preserved")!=code.npos,"The minimum repair discarded an untouched candidate comment");

        if(!item.link&&project.error_codes==std::vector<std::string>{"C3861","C2593"}){
            auto wrapped=initial;
            auto main=std::ranges::find(wrapped,std::string_view("main.cpp"),[](const auto& file)->std::string_view{return file.first;});
            require(main!=wrapped.end(),"The prepared output expression lost its main file");
            const auto call=main->second.find(item.diagnostic=="u6-series-unknown-call"?"estado_inexistente":"sobres_necesarios");
            require(call!=std::string::npos,"The intended undeclared call is absent");
            main->second.insert(call,"\n    ");
            const auto continued=prepare_project_compile_diagnostic_source(block,wrapped,selected);
            require(continued&&continued->project&&continued->project->first_line==item.first&&continued->project->last_line==item.last+1&&
                continued->project->error_codes==project.error_codes,
                "A harmless wrapped call detached the primary error/cascade from its complete output statement");
        }

        for(std::size_t n=0;n<initial.size();++n){
            auto changed_files=initial;changed_files[n].second+="\nint qa_unrelated = 1;\n";
            require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"An unrelated edit in any owned file accredited a rejection");++negative;
            changed_files=initial;changed_files[n].second+="\n/* unterminated";
            require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"An unterminated comment was ignored");++negative;
            changed_files=initial;changed_files[n].second+="\n// spliced\\\n";
            require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"Translation-phase source splicing was accepted");++negative;
        }
        auto changed_files=initial;std::swap(changed_files[0],changed_files[1]);
        require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"A reordered file manifest borrowed an original project identity");++negative;
        changed_files=initial;changed_files[0].first="renamed.h";
        require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"A renamed header borrowed an original project identity");++negative;
        changed_files=initial;changed_files.pop_back();
        require(!prepare_project_compile_diagnostic_source(block,changed_files,selected),"An incomplete project manifest was accepted");++negative;
        auto changed_sources=selected;changed_sources.push_back("qa_other.cpp");
        require(!prepare_project_compile_diagnostic_source(block,initial,changed_sources),"An altered source selection accredited the original rejection");++negative;
        auto changed_block=block;changed_block.stable_id="another_owner";
        require(!prepare_project_compile_diagnostic_source(changed_block,initial,selected),"Another stable editor owner borrowed a closed recipe");++negative;
        changed_block=block;changed_block.compile_check->diagnostic="unreviewed-error";
        require(!prepare_project_compile_diagnostic_source(changed_block,initial,selected),"An arbitrary diagnostic acquired a project recipe");++negative;
        changed_block=block;changed_block.standard_input="";
        require(!prepare_project_compile_diagnostic_source(changed_block,initial,selected),"A standard-input contract was silently ignored");++negative;
        changed_block=block;changed_block.cmake=CmakeConfig{};
        require(!prepare_project_compile_diagnostic_source(changed_block,initial,selected),"A CMake contract was silently ignored");++negative;
        changed_block=block;changed_block.output_check=OutputCheck{};
        require(!prepare_project_compile_diagnostic_source(changed_block,initial,selected),"A runtime output objective was mixed with build rejection");++negative;
        require(block.project_files==initial&&block.source_files==selected,"Planning mutated the original model");++projects;
    }
    require(projects==12&&compile==6&&link==6,"The closed U6 source-plan inventory changed");
    // Re-read the pinned Git source rather than depending on mutable live
    // curriculum. Work can later adopt these contracts without aging this QA.
    for(const auto& [file,lesson]:originals)require(test::load_pre_u6_diagnostic_lesson(file).raw_json==lesson.raw_json,
        "The source-plan QA changed a pinned U6 source");
    std::cout<<"PASS "<<projects<<" real U6 project source plans ("<<compile<<" compile / "<<link<<" link), "<<negative
        <<" rejected source/owner/selection variants. No compilation, execution, UI or learner proof performed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL U6 source plans: "<<error.what()<<'\n';return 1;}

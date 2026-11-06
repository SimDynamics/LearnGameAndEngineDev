#include "core/compiler_flow.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"

#include <windows.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string read(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Missing owned U12 integration artifact");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
state::StateData reopen_state(const state::StateStore& store) {
    // StateStore takes a profile directory but root() exposes its appended
    // state directory. Reopening root() directly would target state/state.
    state::StateStore reopened(store.root().parent_path());
    require(reopened.root()==store.root(),"The U12 QA reopen targeted another profile/state directory");
    return reopened.load();
}
struct Plan {const char* file;const char* editor;const char* complete;const char* partial=nullptr;};
constexpr std::array<Plan,18> plans{{
    {"representar_enteros_en_distintas_bases","block_0006_0002","block_0007_0005","block_0007_0001"},
    {"estado_del_formato_y_flujos_independientes","block_0006_0002","block_0007_0005"},
    {"precision_y_presentacion_de_double","block_0006_0002","block_0007_0005"},
    {"ancho_relleno_y_alineacion_de_campos","block_0005_0002","block_0007_0004"},
    {"ancho_relleno_y_alineacion_de_campos","block_0008_0003","block_0009_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0001_0002","block_0002_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0004_0002","block_0005_0005","block_0005_0001"},
    {"componer_informes_sin_cambiar_los_datos","block_0007_0003","block_0008_0001"},
    {"texto_con_campos_y_patrones_literales","block_0006_0002","block_0007_0006"},
    {"texto_con_campos_y_patrones_literales","block_0008_0003","block_0009_0001"},
    {"bases_y_anchuras_en_cada_campo","block_0006_0002","block_0007_0006"},
    {"bases_y_anchuras_en_cada_campo","block_0008_0003","block_0009_0001"},
    {"precision_por_campo_y_resumenes_formateados","block_0005_0003","block_0007_0006","block_0006_0001"},
    {"precision_por_campo_y_resumenes_formateados","block_0008_0003","block_0009_0001"},
    {"compartir_flujos_sin_acoplar_el_calculo","block_0003_0004","block_0004_0006"},
    {"compartir_flujos_sin_acoplar_el_calculo","block_0006_0002","block_0007_0001"},
    {"insertar_un_tipo_y_encadenar_la_salida","block_0003_0004","block_0005_0001"},
    {"extraer_un_tipo_sin_cambiarlo_en_fallo","block_0005_0005","block_0007_0001","block_0006_0001"}
}};
struct Asset {Lesson lesson,metadata;std::filesystem::path path;std::string hash;};
const Block& at(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);require(position.has_value(),"Missing stable U12 batch identity");
    return lesson.pages[position->page].blocks[position->block];
}
CompilerCompletionSnapshot snapshot(const Lesson& lesson,const state::StateData& data) {
    return compiler_completion_snapshot(lesson,[&](const Block& block){return compiler_puzzle_completed(data,lesson,block);});
}
void flow(const Asset& asset,const state::StateData& data) {
    const auto proofs=snapshot(asset.lesson,data),projected=snapshot(asset.metadata,data);
    require(proofs==projected,"Runtime and catalog disagree about fresh compiler completion/dependency chains");
    for(const auto& relation:asset.lesson.reference_after)for(const auto& identity:relation.blocks) {
        const auto position=stable_block_position(asset.lesson,identity);
        require(position.has_value(),"A U12 protected reference lost its actual stable block");
        const auto& block=asset.lesson.pages[position->page].blocks[position->block];
        const auto projected_position=stable_block_position(asset.metadata,identity);
        require(projected_position.has_value(),"Catalog omitted a protected reference owner");
        const auto& metadata=asset.metadata.pages[projected_position->page].blocks[projected_position->block];
        const auto available=compiler_reference_available(asset.lesson,position->page,block,proofs,
            [&](const Block& compiler){return compiler_puzzle_completed(data,asset.lesson,compiler);});
        const auto catalog_available=compiler_reference_available(asset.metadata,projected_position->page,metadata,projected,
            [&](const Block& compiler){return compiler_puzzle_completed(data,asset.metadata,compiler);});
        bool expected=proofs.at(relation.activity);
        for(const auto& prior:asset.lesson.pages[position->page].blocks) {
            if(&prior==&block)break;
            if(prior.type==BlockType::cpp_compiler)expected=expected&&compiler_puzzle_completed(data,asset.lesson,prior);
        }
        require(available==expected&&available==catalog_available,
            "A protected U12 reference bypassed its editor or catalog/runtime admission differs");
    }
    for(const auto& relation:asset.lesson.activity_after) {
        bool expected=true;for(const auto& identity:relation.prerequisites)expected=expected&&proofs.at(identity);
        require(compiler_activity_available(asset.lesson,at(asset.lesson,relation.activity),proofs)==expected
            &&compiler_activity_available(asset.metadata,at(asset.metadata,relation.activity),projected)==expected,
            "A dependent U12 activity bypassed its complete compiler chain or lost catalog parity");
    }
}
void preserve_values(const state::StateData& before,const state::StateData& after) {
    for(const auto& [domain,values]:before.preferences)for(const auto& [key,value]:values)
        require(state::find(after,domain,key)&&*state::find(after,domain,key)==value,
            "A compiler result rewrote an unrelated response, draft, marker or previous achievement");
}
struct Capture {CppRunRequest request;CppRunResult result;FreshExecution execution;};

void ordinary_guides(const Asset& asset,state::StateStore& store,state::StateData& data,std::size_t expected,bool guided) {
    std::size_t count=0;
    for(const auto& page:asset.lesson.pages)for(const auto& block:page.blocks) {
        if(block.type!=BlockType::multiple_choice&&block.type!=BlockType::output_prediction&&block.type!=BlockType::order_code)continue;
        AnswerEvaluation correct,wrong;state::Response rejected,accepted;
        if(block.type==BlockType::multiple_choice) {
            require(block.correct_index.has_value(),"A U12 choice lost its correct index");
            correct=evaluate_choice(block,*block.correct_index);accepted.selected=static_cast<std::int64_t>(*block.correct_index);
            for(std::size_t option=0;option<block.options.size();++option)if(option!=*block.correct_index) {
                const auto verdict=evaluate_choice(block,option);
                require(!verdict.correct&&verdict.feedback==incorrect_feedback(block,option)&&(!guided||!verdict.feedback.empty()),
                    "An ordinary U12 wrong option lost its exact new guide or exposed success feedback");
                if(rejected.selected<0){wrong=verdict;rejected.selected=static_cast<std::int64_t>(option);}
            }
        } else if(block.type==BlockType::output_prediction) {
            require(!block.answers.empty(),"A U12 prediction lost its canonical answer");
            accepted.answer=block.answers.front();correct=evaluate_text_answer(block,accepted.answer);
            rejected.answer="__not_the_requested_U12_output__";wrong=evaluate_text_answer(block,rejected.answer);
        } else {
            correct=evaluate_order(block,block.correct_order);
            accepted.order.assign(block.correct_order.begin(),block.correct_order.end());
            auto candidate=block.correct_order;bool found=false;
            for(std::size_t first=0;first<candidate.size()&&!found;++first)for(std::size_t second=first+1;second<candidate.size();++second) {
                std::swap(candidate[first],candidate[second]);const auto verdict=evaluate_order(block,candidate);
                if(!verdict.correct){wrong=verdict;rejected.order.assign(candidate.begin(),candidate.end());found=true;break;}
                std::swap(candidate[first],candidate[second]);
            }
            require(found,"The U12 order fixture lacks a genuinely incorrect full permutation");
        }
        require(correct.correct&&correct.feedback==block.explanation&&!correct.feedback.empty()
            &&!wrong.correct&&wrong.feedback==incorrect_feedback(block,rejected.selected<0?std::nullopt:
                std::optional<std::size_t>{static_cast<std::size_t>(rejected.selected)})&&(!guided||!wrong.feedback.empty()),
            "A U12 ordinary exercise lost its exact answer, rejection guide or complete success explanation");
        rejected.correct=false;state::mark_response_reviewed(rejected,block);
        state::put_response(data,asset.lesson,block,rejected);store.save(data);
        const auto saved=reopen_state(store);
        require(saved==data,"An ordinary rejected U12 answer changed while reopening");
        const auto restored=state::get_response(saved,asset.lesson,block);
        require(restored&&!restored->correct&&state::response_reviewed(*restored,block)
            &&restored->selected==rejected.selected&&restored->answer==rejected.answer&&restored->order==rejected.order,
            "A U12 ordinary rejection was lost or transformed into a success on reopen");
        accepted.correct=true;state::put_response(data,asset.lesson,block,accepted);store.save(data);
        const auto accepted_saved=reopen_state(store);
        const auto response=state::get_response(accepted_saved,asset.lesson,block);
        require(accepted_saved==data&&response&&response->correct,"A correct U12 ordinary answer was not preserved on reopen");
        ++count;
    }
    require(count==expected,"The focused ordinary U12 guide batch omitted or silently added an exercise");
}
}

int main() try {
    const LaunchOptions options{ProfileId::Codex,L"u12-batch-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    const auto source_root=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo";
    std::map<std::string,Asset> assets;
    state::StateStore store(environment.paths.root);auto data=store.load();require(data.preferences.empty(),"The private U12 profile contains old state");
    data.preferences["u12-batch-unrelated"]["opaque"]=std::string(" Preserve\t\r\n保存 ");
    for(const auto& plan:plans)if(!assets.contains(plan.file)) {
        Asset asset;asset.path=source_root/(std::string("lesson_")+plan.file+".json");
        const auto bytes=read(asset.path);asset.hash=state::sha256(bytes);asset.lesson=parse_lesson_json(bytes);
        asset.metadata=parse_lesson_progress_metadata(bytes);state::prepare_lesson_state(data,asset.lesson);
        state::save_current_page(data,asset.lesson,asset.lesson.pages.size()-1);
        assets.emplace(plan.file,std::move(asset));
    }
    require(assets.size()==11,"The U12 compiler batch no longer covers exactly L5 through L15");
    for(const auto& plan:plans) {
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        require(editor.output_check&&editor.output_check->cases.size()==1&&editor.output_check->cases.front().standard_input.empty(),
            "A reviewed U12 editor changed from its single aggregate empty-input objective");
        state::Draft draft;draft.source=editor.initial_code+"\n// isolated personal QA draft\n";draft.input="";
        draft.selection_start=2;draft.selection_end=7;state::put_draft(data,draft_key("programming",asset.lesson,editor),draft);
    }
    store.save(data);
    const auto programs=environment.paths.projects/L"Código & (U12 integrado)";
    std::uint64_t sequence=0;unsigned runs=0,passed=0,failed=0;
    const auto test=[&](std::string_view name,const std::function<void()>& action) {
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    const auto run=[&](std::string source) {
        Capture capture;capture.request.request_id=++sequence;capture.request.revision=100+sequence;
        capture.request.source_utf8=std::move(source);capture.request.projects_directory=programs;
        capture.request.compile_timeout=30s;capture.request.execution_timeout=3s;
        ++runs;capture.result=run_cpp(capture.request);capture.execution=capture_cpp_execution(capture.request,capture.result);
        require(capture.result.error_utf8.empty()&&capture.result.compilation.succeeded()
            &&capture.execution.execution_completed&&capture.execution.execution_succeeded&&capture.execution.exit_code==0,
            "A U12 integration program failed to compile or complete normally: "+capture.result.error_utf8+
            capture.result.compilation.stdout_bytes+capture.result.compilation.stderr_bytes);
        require(read(capture.result.run_directory/L"main.cpp")==capture.request.source_utf8
            &&read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output,
            "The adapter changed the captured U12 source or raw output bytes");
        return capture;
    };
    // A single controlled empty program exercises all aggregate-oracle negatives.
    // Work already checked each separate empty curricular starter; do not repeat it.
    const auto empty=run("int main(){return 0;}\n");require(empty.execution.standard_output.empty(),"The empty U12 probe wrote output");
    for(const auto& plan:plans)test(std::string(plan.file)+"/"+plan.editor,[&] {
        const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        const auto initial=data;flow(asset,data);
        require(!compiler_puzzle_completed(data,asset.lesson,editor),"An unexecuted U12 editor inherited another goal's achievement");
        require(evaluate_cpp_output_check(*editor.output_check,empty.execution,empty.request.revision).verdict==OutputVerdict::incorrect
            &&!record_compiler_puzzle_case(data,asset.lesson,editor,empty.request,empty.execution,"Incorrecto\nControlled empty output.")&&data==initial,
            "An empty successful program completed a nonempty U12 aggregate objective");
        const auto reject=[&](const Capture& capture) {
            require(evaluate_cpp_output_check(*editor.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::incorrect,
                "An incomplete U12 reference satisfied the complete aggregate output");
            require(record_compiler_puzzle_review(data,asset.lesson,editor,capture.request,capture.execution,"Incorrecto\nQA: incomplete aggregate output."),
                "An actual U12 rejection could not retain its review");
            preserve_values(initial,data);store.save(data);const auto restored=reopen_state(store);
            const auto feedback=compiler_puzzle_current_feedback(restored,asset.lesson,editor,capture.request);
            require(restored==data,"Reopening changed the exact saved U12 rejection state");
            require(feedback&&!feedback->correct&&!compiler_puzzle_completed(restored,asset.lesson,editor),
                "Reopening lost an actual U12 rejection or invented completion");flow(asset,restored);
        };
        reject(empty);
        if(plan.partial)reject(run(at(asset.lesson,plan.partial).code));
        const auto& reference=at(asset.lesson,plan.complete);
        require(reference.type==BlockType::code&&reference_activity(asset.lesson,reference).has_value(),
            "The complete U12 program is not an explicitly protected reference");
        const auto capture=run(reference.code);
        const auto verdict=evaluate_cpp_output_check(*editor.output_check,capture.execution,capture.request.revision);
        require(verdict.verdict==OutputVerdict::correct&&verdict.stdout_correct&&verdict.exit_code_correct,
            "The complete real U12 reference did not satisfy its authored objective");
        const auto owned="compiler-puzzle/v1/"+asset.lesson.locale+'/'+asset.lesson.id+'/'+exercise_key(asset.lesson,editor)+'/';
        auto protected_initial=initial; // Only this editor's temporary rejection may be retired.
        if(auto domain=protected_initial.preferences.find("native_windows");domain!=protected_initial.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(owned+state::sha256(reference_objective_frame(asset.lesson,editor))+"/review/");});
        require(record_compiler_puzzle_case(data,asset.lesson,editor,capture.request,capture.execution,"Correcto\n"+verdict.feedback)
            &&compiler_puzzle_case_count(data,asset.lesson,editor)==1&&compiler_puzzle_completed(data,asset.lesson,editor),
            "A fresh complete U12 program did not earn its own aggregate achievement");
        preserve_values(protected_initial,data);require(compiler_puzzle_review(data,asset.lesson,editor,capture.request).empty(),
            "An actual complete U12 program retained obsolete rejected feedback");
        store.save(data);require(reopen_state(store)==data,"A real U12 achievement changed on reopen");
        flow(asset,data);
        if(std::string_view(plan.file)=="compartir_flujos_sin_acoplar_el_calculo"&&std::string_view(plan.editor)=="block_0006_0002")
            require(read(capture.result.run_directory/L"balance_compartido_lgp.txt")=="Balance=5",
                "The real U12L13 round trip did not preserve its nine exact file bytes");
        if(std::string_view(plan.file)=="insertar_un_tipo_y_encadenar_la_salida")
            require(read(capture.result.run_directory/L"recuento_lgp.txt")=="[10;10]",
                "The real U12L14 battery did not replace the file with its final seven bytes");
    });
    test("sixteen ordinary U12L14/L15 answers, exact guides and state reopen",[&] {
        ordinary_guides(assets.at("insertar_un_tipo_y_encadenar_la_salida"),store,data,7,false);
        ordinary_guides(assets.at("extraer_un_tipo_sin_cambiarlo_en_fallo"),store,data,9,true);
    });
    const auto achieved=data;
    for(std::size_t index=0;index<plans.size();++index)test(std::string("reset/")+plans[index].file+"/"+plans[index].editor,[&] {
        const auto& plan=plans[index];const auto& asset=assets.at(plan.file);const auto& editor=at(asset.lesson,plan.editor);
        require(compiler_puzzle_completed(achieved,asset.lesson,editor),"Missing actual U12 achievement before its isolated reset");
        auto reset=achieved,expected=achieved;
        const auto prefix="compiler-puzzle/v1/"+asset.lesson.locale+'/'+asset.lesson.id+'/'+exercise_key(asset.lesson,editor)+'/';
        if(auto domain=expected.preferences.find("native_windows");domain!=expected.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
        reset_compiler_puzzle(reset,asset.lesson,editor);
        require(reset==expected&&!compiler_puzzle_completed(reset,asset.lesson,editor)&&!snapshot(asset.lesson,reset).at(editor.stable_id),
            "Reset failed to restore the U12 prerequisite or changed later answers, drafts or other achievements");
        flow(asset,reset);
        state::StateStore reset_store(environment.paths.root/L"reset"/std::to_wstring(index));(void)reset_store.load();reset_store.save(reset);
        require(reopen_state(reset_store)==expected,"A reset U12 prerequisite or conserved state changed on reopen");
    });
    test("the new authored return-1 objective stays independent of these eighteen checks",[&] {
        const auto& asset=assets.at("compartir_flujos_sin_acoplar_el_calculo");
        const auto& intentional=at(asset.lesson,"block_0005_0002");
        require(has_compiler_objective(intentional)&&intentional.output_check&&intentional.output_check->cases.size()==1
            &&intentional.output_check->cases.front().exit_code==1
            &&intentional.output_check->cases.front().outputs==std::vector<std::string>{"Entrada invalida|9:4"}
            &&intentional.initial_code.find("return 1;")!=std::string::npos,
            "The integrated U12L13 objective lost its exact output or intentional process result");
        require(!compiler_puzzle_completed(achieved,asset.lesson,intentional)&&!snapshot(asset.lesson,achieved).at(intentional.stable_id)
            &&compiler_puzzle_case_count(achieved,asset.lesson,intentional)==0,
            "The eighteen independent batch checks manufactured credit for the new intentional-exit objective");
        for(const auto& [name,asset]:assets)require(state::sha256(read(asset.path))==asset.hash,
            "The headless U12 integration batch changed an active curricular asset");
        require(runs==23,"The focused U12 batch repeated or omitted a planned real compiler program");
    });
    std::cout<<"Headless U12 compiler batch: "<<passed<<" groups passed, "<<failed<<" failed; "<<runs
        <<" actual compiler programs, no UI or audio.\n";
    return failed?1:0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL U12 compiler batch setup: "<<error.what()<<'\n';return 1;
} catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL U12 compiler batch Windows setup: "<<error.code().value<<'\n';return 1;
} catch(...) {
    std::cerr<<"FAIL U12 compiler batch setup: unexpected exception\n";return 1;
}

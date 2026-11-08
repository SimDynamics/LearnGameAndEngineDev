#pragma once
// QA setup only. Ordinary responses are synthetic, evaluated against the
// authored contract, and disclosed separately from actual educational runs.
// This helper never creates compiler receipts, listening or real achievements.
#include "core/lesson_entry_policy.hpp"
#include "core/lesson_page_reach.hpp"
#include "core/compiler_flow.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace lgaed::test {
inline constexpr std::array compiler_reader_entry_owners{
    std::string_view{"cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete"},
    std::string_view{"cpp_fundamentals__texto_interpretacion_y_flujos__declaraciones_aceptadas_solo_al_completar"},
    std::string_view{"cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada"},
    std::string_view{"cpp_fundamentals__texto_interpretacion_y_flujos__constantes_del_lenguaje_y_nombres_predefinidos"}};
struct CompilerReaderEntrySetup { std::map<std::string,std::string> ordinary_responses; };
inline bool compiler_reader_ordinary(curriculum::BlockType type){using B=curriculum::BlockType;
    return type==B::multiple_choice||type==B::order_code||type==B::output_prediction||type==B::fill_code;
}
inline curriculum::LessonEntryDecision compiler_reader_entry_decision(const state::StateData& data,
        const curriculum::Lesson& lesson,std::size_t requested){
    if(std::ranges::find(compiler_reader_entry_owners,lesson.id)==compiler_reader_entry_owners.end()||
            lesson.locale!="es"||lesson.state_layout.version!=2)
        throw std::runtime_error("The compiler-reader QA helper requires one of its audited Spanish v2 owners");
    std::vector<curriculum::EntryRequirement> requirements;
    for(std::size_t page=0;page<lesson.pages.size();++page)for(const auto& block:lesson.pages[page].blocks){
        if(curriculum::compiler_requires_completion(block))requirements.push_back({page,
            windows::compiler_puzzle_completed(data,lesson,block)?curriculum::EntryCompletion::complete:curriculum::EntryCompletion::incomplete});
        else if(compiler_reader_ordinary(block.type)){
            const auto response=state::get_response(data,lesson,block);
            requirements.push_back({page,response&&response->correct?curriculum::EntryCompletion::complete:curriculum::EntryCompletion::incomplete});
        }else if(block.type==curriculum::BlockType::math_puzzle||block.type==curriculum::BlockType::inline_fill_code||
                block.type==curriculum::BlockType::drag_fill_code)
            throw std::runtime_error("The bounded compiler-reader QA helper needs review for this new exercise type");
    }
    return curriculum::lesson_entry_decision(state::clamp_reachable_page(data,lesson,requested),lesson.pages.size(),requirements);
}
inline CompilerReaderEntrySetup prepare_compiler_reader_entry(state::StateData& data,const curriculum::Lesson& lesson,std::size_t destination){
    if(destination>=lesson.pages.size())throw std::runtime_error("Unknown assignment QA destination");
    auto staged=data;state::prepare_lesson_state(staged,lesson);CompilerReaderEntrySetup setup;
    for(std::size_t page=0;page<=destination;++page){
        const auto admission=compiler_reader_entry_decision(staged,lesson,page);
        if(admission.page!=page||admission.pending_math||!state::can_reach_page(staged,lesson,page))
            throw std::runtime_error("The assignment QA arrival is not admitted by its actual preceding requirements");
        state::save_current_page(staged,lesson,page);
        if(page==destination)break;
        for(const auto& block:lesson.pages[page].blocks){
            if(curriculum::compiler_requires_completion(block)){
                if(!windows::compiler_puzzle_completed(staged,lesson,block))
                    throw std::runtime_error("An actual compiler receipt is required; the assignment QA never fabricates one");
                continue;
            }
            if(!compiler_reader_ordinary(block.type))continue;
            if(state::get_response(staged,lesson,block))
                throw std::runtime_error("The assignment setup refuses to replace an existing response");
            state::Response response;curriculum::AnswerEvaluation evaluated;
            if(block.type==curriculum::BlockType::multiple_choice){
                if(!block.correct_index||*block.correct_index>=block.options.size())throw std::runtime_error("Missing authored QA choice");
                response.selected=static_cast<std::int64_t>(*block.correct_index);response.answer=block.options[*block.correct_index];
                evaluated=curriculum::evaluate_choice(block,*block.correct_index);
            }else if(block.type==curriculum::BlockType::order_code){
                evaluated=curriculum::evaluate_order(block,block.correct_order);
                response.order.assign(block.correct_order.begin(),block.correct_order.end());
            }else{
                if(block.answers.empty())throw std::runtime_error("Missing authored QA answer");
                response.answer=block.answers.front();evaluated=curriculum::evaluate_text_answer(block,response.answer);
            }
            if(!evaluated.correct)throw std::runtime_error("The synthetic prerequisite did not satisfy its actual authored evaluator");
            response.correct=true;state::mark_response_reviewed(response,block);state::put_response(staged,lesson,block,response);
            setup.ordinary_responses.emplace(block.stable_id,state::string_value(staged,"learning_progress",state::response_storage_key(lesson,block)));
        }
    }
    state::validate_lesson_state(staged,lesson);
    std::vector<std::string> disclosed;for(const auto& [id,raw]:setup.ordinary_responses){(void)raw;disclosed.push_back(id);}
    staged.preferences["u11_compiler_reader_synthetic_prerequisites_qa"]["ordinary_only"]=std::move(disclosed);
    data=std::move(staged);return setup;
}
inline bool compiler_reader_response_unchanged(const state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CompilerReaderEntrySetup& setup){
    const auto expected=setup.ordinary_responses.find(block.stable_id);
    return expected==setup.ordinary_responses.end()?!state::get_response(data,lesson,block):
        state::string_value(data,"learning_progress",state::response_storage_key(lesson,block))==expected->second;
}
}

#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_history_training.hpp"
#include "platform/windows/state_store.hpp"
#include "core/code_format_compat.hpp"
#include "core/cpp_expression_repair.hpp"
#include "core/named_rule_constant_variation_check.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <set>
#include <stdexcept>

namespace lgaed::windows {
namespace {
std::string token(std::string_view text) { return std::to_string(text.size()) + ':' + std::string(text); }
std::string root(const curriculum::Lesson& lesson, const curriculum::Block& block) {
    return "compiler-puzzle/v1/" + lesson.locale + '/' + lesson.id + '/' + curriculum::exercise_key(lesson,block) + '/';
}
std::string base(const curriculum::Lesson& lesson, const curriculum::Block& block) {
    return root(lesson,block) + state::sha256(curriculum::reference_objective_frame(lesson,block)) + '/';
}
// A precise owner, not a generic option to require extra undocumented work.
// The prior stdout receipt is still valid historical data but is insufficient
// to prove this lesson's separate recorded/compared/restored CMake workflow.
bool selective_history_owner(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(block.compiler_workflow_check==curriculum::CompilerWorkflowCheck::cmake_selective_recovery_v1)
        return block.type==curriculum::BlockType::cpp_compiler&&
            block.output_check&&block.cmake&&block.cmake->target=="comprobar_cajas";
    // Keep the historical owner gated until its exact root declaration is
    // published. Existing stdout receipts cannot replace History evidence.
    return lesson.id=="programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador"
        &&lesson.locale=="es"&&block.stable_id=="block_0010_0003"
        &&block.type==curriculum::BlockType::cpp_compiler
        &&block.output_check&&block.cmake
        &&block.cmake->target=="comprobar_cajas";
}
std::string selective_history_key(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return base(lesson,block)+"history-sequence/v1";
}
std::string selective_history_seal(std::string_view key,std::string_view program) {
    return state::sha256("compiler-selective-history-v1/"+token(key)+token(program));
}
bool selective_history_receipt_valid(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!selective_history_owner(lesson,block))return true;
    const auto key=selective_history_key(lesson,block);
    const auto stored=state::find(data,"native_windows",key);
    if(!stored)return false;
    const auto fields=std::get_if<std::vector<std::string>>(stored);
    return fields&&fields->size()==2&&!(*fields)[0].empty()
        &&(*fields)[1]==selective_history_seal(key,(*fields)[0]);
}

std::string integer_client_key(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    // This prerequisite survives edits to the unfinished test driver. It is
    // still inside the owner's root, so an explicit exercise reset removes it.
    return root(lesson,block)+"integer-client/v1/"+
        state::sha256(curriculum::reference_objective_frame(lesson,block));
}
std::string integer_client_seal(std::string_view key,std::string_view component,
        std::string_view capture) {
    return state::sha256("compiler-integer-client-v1/"+
        token(key)+token(component)+token(capture));
}
const std::vector<std::string>* integer_client_receipt(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto key=integer_client_key(lesson,block);
    const auto value=state::find(data,"native_windows",key);
    if(!value)return nullptr;
    const auto fields=std::get_if<std::vector<std::string>>(value);
    return fields&&fields->size()==3&&!(*fields)[0].empty()&&!(*fields)[1].empty()&&
        (*fields)[2]==integer_client_seal(key,(*fields)[0],(*fields)[1])?fields:nullptr;
}
void validate_integer_client_receipt(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(block.integer_battery_probe&&block.integer_battery_probe->prerequisite&&
       state::find(data,"native_windows",integer_client_key(lesson,block))&&
       !integer_client_receipt(data,lesson,block))
        throw std::runtime_error("Invalid integer battery client provenance; data was preserved.");
}

std::string cmake_initial_prefix(const curriculum::Lesson& lesson,
        const curriculum::Block& block,bool build) {
    // Deliberately outside base(): editing an unfinished repair must retain
    // its independently observed original. Reset still owns the whole root.
    return root(lesson,block)+(build?"cmake-initial-build/v1/":"cmake-initial-run/v1/")+
        state::sha256(curriculum::reference_objective_frame(lesson,block))+'/';
}
std::string cmake_initial_seal(std::string_view key,const std::vector<std::string>& fields) {
    return state::sha256("compiler-cmake-initial-observation-v1/"+token(key)+
        token(fields[0])+token(fields[1])+token(fields[2])+token(fields[3]));
}
const std::vector<std::string>* cmake_initial_receipt(const state::StateData& data,
        const std::string& key) {
    const auto value=state::find(data,"native_windows",key);
    if(!value)return nullptr;
    const auto fields=std::get_if<std::vector<std::string>>(value);
    const auto digest=[](const std::string& text) {
        return text.size()==64&&std::ranges::all_of(text,[](unsigned char c) {
            return (c>='0'&&c<='9')||(c>='a'&&c<='f');
        });
    };
    return fields&&fields->size()==5&&digest((*fields)[0])&&digest((*fields)[1])&&
        digest((*fields)[2])&&((*fields)[3].empty()||digest((*fields)[3]))&&
        (*fields)[4]==cmake_initial_seal(key,*fields)?fields:nullptr;
}
void validate_cmake_initial_receipts(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!block.cmake_graph_check||!block.cmake_graph_check->initial_observation)return;
    const auto domain=data.preferences.find("native_windows");
    if(domain==data.preferences.end())return;
    const auto build_prefix=cmake_initial_prefix(lesson,block,true);
    const auto run_prefix=cmake_initial_prefix(lesson,block,false);
    const bool warning=block.cmake_graph_check->initial_observation->warning.has_value();
    for(const auto& [key,value]:domain->second) {
        const bool build=key.starts_with(build_prefix),run=key.starts_with(run_prefix);
        if(!build&&!run)continue;
        const auto fields=cmake_initial_receipt(data,key);
        if(!fields||key!=(build?build_prefix:run_prefix)+(*fields)[0]+
                (build?'/'+(*fields)[1]:std::string{})||
           (build&&(!warning||!(*fields)[3].empty()))||
           (run&&(warning==(*fields)[3].empty())))
            throw std::runtime_error("Invalid initial CMake observation; learner data was preserved.");
    }
}

bool integer_battery_project_matches(const curriculum::Block& block,const CppRunRequest& request) {
    if(!block.integer_battery_probe||block.integer_battery_probe!=request.integer_battery_probe||
       !block.cmake||!request.cmake||
       !curriculum::integer_battery_spec_valid(*block.integer_battery_probe)||
       request.backend!=CppRunRequest::Backend::native_msvc||request.diagnostic||
       request.boolean_battery_probe||request.reduction_probe||request.open_battery_probe||
       request.named_rule_probe||request.rejection_probe||request.observation_probe||
       request.assertion_mode||request.cmake_graph_check||request.capture_cmake_graph||
       !request.source_files.empty()||request.source_files!=block.source_files||
       request.project_files.size()!=block.project_files.size())return false;
    const auto& spec=*block.integer_battery_probe;
    const bool client=spec.prerequisite&&request.cmake->target==spec.prerequisite->target;
    if(request.cmake->target!=spec.test_target&&!client)return false;
    auto expected=*block.cmake;
    expected.target=request.cmake->target;
    if(expected!=*request.cmake)return false;
    if(request.stdin_utf8!=(client?spec.prerequisite->standard_input:
            block.standard_input.value_or("")))return false;
    std::set<std::string> seen;
    for(const auto& [path,source]:request.project_files) {
        if(!seen.insert(path).second)return false;
        const auto original=std::ranges::find_if(block.project_files,
            [&](const auto& file){return file.first==path;});
        if(original==block.project_files.end()||
           (path!=spec.test_path&&source!=original->second))return false;
    }
    return true;
}

std::vector<std::string_view> tokens(std::string_view text) {
    std::vector<std::string_view> result;
    for (std::size_t start=0;start<text.size();) {
        while(start<text.size()&&std::isspace(static_cast<unsigned char>(text[start])))++start;
        auto end=start;while(end<text.size()&&!std::isspace(static_cast<unsigned char>(text[end])))++end;
        if(end>start)result.push_back(text.substr(start,end-start));start=end;
    }
    return result;
}
std::vector<std::string> case_proofs(const state::StateData& data,const std::string& key) {
    const auto value=state::find(data,"native_windows",key+"cases");
    if(!value)return {};
    const auto proofs=std::get_if<std::vector<std::string>>(value);
    if(!proofs)throw std::runtime_error("Invalid stored compiler case proofs; data was preserved.");
    return *proofs;
}
std::string proof(const std::string& key,std::string_view program,std::size_t index,
                  const curriculum::OutputCase& item) {
    return state::sha256("compiler-case-v1/"+token(key)+token(program)+token(std::to_string(index))
        +token(item.standard_input)+token(item.executable.value_or("")));
}
std::string complete_seal(const std::string& key,std::string_view program,const std::vector<std::string>& proofs) {
    auto frame="compiler-complete-v1/"+token(key)+token(program);
    for(const auto& item:proofs)frame+=token(item);
    return state::sha256(frame);
}
// The first authenticated authored case is sufficient for curricular progress.
// Keep the historical all-case seal unchanged. Additional cases are optional
// observations, and never silently inherit another case's program or input.
std::string first_case_complete_seal(const std::string& key,std::string_view program,
        const std::vector<std::string>& proofs) {
    if(proofs.empty()||proofs.front().empty())return {};
    return state::sha256("compiler-first-case-complete-v1/"+
        token(key)+token(program)+token(proofs.front()));
}
bool valid_completion_seal(const curriculum::Block& block,const std::string& key,
        std::string_view program,const std::vector<std::string>& proofs,std::string_view stored) {
    if(stored.empty()||proofs.empty())return false;
    const bool all=std::ranges::all_of(proofs,[](const auto& value){return !value.empty();});
    if(all&&stored==complete_seal(key,program,proofs))return true;
    return block.output_check&&block.output_check->cases.size()>1&&
        !proofs.front().empty()&&stored==first_case_complete_seal(key,program,proofs);
}
// Presentation-only provenance. Neither this sidecar nor its seal participates
// in grade, execution proof, objective identity or historical completion.
constexpr auto feedback_origin_suffix="feedback-origin/v1";
const std::string* authored_feedback(const curriculum::Block& block,std::string_view path) {
    if(path=="/compileCheck/explanation")return block.compile_check?&block.compile_check->explanation:nullptr;
    constexpr std::string_view prefix="/outputCheck/cases/";
    if(!block.output_check||!path.starts_with(prefix))return nullptr;path.remove_prefix(prefix.size());
    const auto index=[&](std::size_t limit)->std::optional<std::size_t>{const auto slash=path.find('/');if(slash==path.npos)return {};
        const auto number=path.substr(0,slash);if(number.empty()||(number.size()>1&&number.front()=='0'))return {};
        std::size_t result=0;const auto parsed=std::from_chars(number.data(),number.data()+number.size(),result);
        if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||result>=limit)return {};
        path.remove_prefix(slash);return result;};
    const auto selected=index(block.output_check->cases.size());if(!selected)return nullptr;
    const auto& item=block.output_check->cases[*selected];if(path=="/explanation")return &item.explanation;
    constexpr std::string_view review="/reviewFeedback/";if(!path.starts_with(review))return nullptr;path.remove_prefix(review.size());
    const auto guide=index(item.review_feedback.size());if(!guide||path!="/text")return nullptr;
    return &item.review_feedback[*guide].text;
}
bool feedback_has_source(std::string_view feedback,std::string_view source) {
    return !source.empty()&&(feedback==source||(feedback.size()>source.size()&&feedback.ends_with(source)&&
        feedback[feedback.size()-source.size()-1]=='\n'));
}
const curriculum::TextSemantics* feedback_descriptor(const curriculum::Block& block,std::string_view path,std::string_view feedback) {
    const auto source=authored_feedback(block,path);const auto field=block.text_semantics.find(std::string(path));
    if(!source||field==block.text_semantics.end()||field->second.source!=*source||!feedback_has_source(feedback,*source))return nullptr;
    try{curriculum::validate_text_semantics(field->second);}catch(const std::invalid_argument&){return nullptr;}
    return &field->second;
}
std::string feedback_origin_seal(std::string_view key,std::string_view execution_seal,std::string_view feedback,
        std::string_view path,std::string_view source_sha) {
    return state::sha256("compiler-feedback-origin-v1/"+token(key)+token(execution_seal)+token(feedback)+token(path)+token(source_sha));
}
void record_feedback_origin(state::StateData& data,const std::string& key,const curriculum::Block& block,
        std::string_view path,std::string_view feedback,std::string_view execution_seal) {
    auto& values=data.preferences["native_windows"];values.erase(key+feedback_origin_suffix);
    const auto descriptor=feedback_descriptor(block,path,feedback);if(!descriptor||execution_seal.empty())return;
    const auto source_sha=state::sha256(descriptor->source);
    values[key+feedback_origin_suffix]=std::vector<std::string>{std::string(path),source_sha,
        feedback_origin_seal(key,execution_seal,feedback,path,source_sha)};
}
std::string restored_feedback_origin(const state::StateData& data,const std::string& key,const curriculum::Block& block,
        std::string_view feedback,std::string_view execution_seal) {
    const auto stored=state::find(data,"native_windows",key+feedback_origin_suffix);if(!stored)return {};
    const auto fields=std::get_if<std::vector<std::string>>(stored);if(!fields||fields->size()!=3||execution_seal.empty())return {};
    const auto descriptor=feedback_descriptor(block,(*fields)[0],feedback);
    if(!descriptor||state::sha256(descriptor->source)!=(*fields)[1]||(*fields)[2]!=feedback_origin_seal(key,execution_seal,feedback,(*fields)[0],(*fields)[1]))return {};
    return (*fields)[0];
}
std::string review_target(const CppRunRequest& request) {
    return request.cmake?request.cmake->target:std::string{};
}
bool matches_runtime_capture(const CppRunRequest& captured,const curriculum::FreshExecution& execution) {
    return !captured.diagnostic
        &&(!captured.cmake_graph_check||
            (!execution.cmake_graph_request_fingerprint.empty()&&
             execution.cmake_graph_request_fingerprint==
                cpp_cmake_graph_request_fingerprint(captured)))
        &&(!captured.named_rule_probe||!captured.named_rule_probe->battery_check||
            (!execution.named_rule_battery_request_fingerprint.empty()&&
             execution.named_rule_battery_request_fingerprint==
                cpp_named_rule_battery_request_fingerprint(captured)))
        &&(!captured.named_rule_probe||!captured.named_rule_probe->constant_variations||
            (!execution.named_rule_constant_variation_request_fingerprint.empty()&&
             execution.named_rule_constant_variation_request_fingerprint==
                cpp_named_rule_constant_variation_request_fingerprint(captured)))
        &&(!captured.boolean_battery_probe||
            (!execution.boolean_battery_request_fingerprint.empty()&&
             execution.boolean_battery_request_fingerprint==
                cpp_boolean_battery_request_fingerprint(captured)))
        &&(!captured.reduction_probe||
            (!execution.reduction_request_fingerprint.empty()&&
             execution.reduction_request_fingerprint==cpp_reduction_request_fingerprint(captured)))
        &&(!captured.integer_battery_probe||
            (!execution.integer_battery_request_fingerprint.empty()&&
             execution.integer_battery_request_fingerprint==
                cpp_integer_battery_request_fingerprint(captured)))
        &&(!captured.adjustment_project_probe||
            (!execution.adjustment_project_request_fingerprint.empty()&&
             execution.adjustment_project_request_fingerprint==
                cpp_adjustment_project_request_fingerprint(captured)))
        &&(captured.action==CppRunRequest::Action::compile_run||captured.action==CppRunRequest::Action::execute)
        &&captured.stdin_utf8==execution.standard_input
        &&(!captured.cmake||execution.executable==std::optional<std::string>{captured.cmake->target});
}
std::string review_rules(const curriculum::Block& block,bool legacy=false) {
    // Success explanations do not describe a rejected attempt. V2 keeps that
    // attempt when only successful guidance changes, while error selectors and
    // error guides remain exact. V1 reconstruction retains every old byte.
    const auto& check=*block.output_check;std::string frame=legacy?"compiler-review-rules-v1/":"compiler-review-rules-v2/";
    frame+=token(check.input_comparison==curriculum::TextComparison::tokens?"tokens":"exact")
        +token(std::to_string(check.cases.size()));
    if(check.boolean_battery_result) {
        frame+=token(curriculum::boolean_battery_objective_version)+
            token(*check.boolean_battery_result==curriculum::BooleanBatteryPublishedResult::success
                ?"success":"failure");
        if(block.boolean_battery_probe) {
            const auto& spec=*block.boolean_battery_probe;
            frame+=token(spec.function_name)+token(spec.protected_definition)+
                token(std::to_string(spec.cases.size()));
            for(const auto& item:spec.cases)
                frame+=token(std::to_string(item.first))+token(std::to_string(item.second))+
                    token(item.expected?"true":"false");
        }
    }
    if(check.reduction_check) {
        frame+=token(curriculum::reduction_objective_version);
        if(block.reduction_probe)frame+=token(curriculum::reduction_spec_frame(*block.reduction_probe));
    }
    if(check.integer_battery_check&&block.integer_battery_probe)
        frame+=token(curriculum::integer_battery_spec_frame(*block.integer_battery_probe));
    if(check.adjustment_project_check&&block.adjustment_project_probe)
        frame+=token(curriculum::adjustment_project_spec_frame(*block.adjustment_project_probe));
    if(check.rejection_operation)
        frame+=token(*check.rejection_operation==curriculum::RejectionOperation::boxes
            ? "semantic-rejection-boxes-v1" : "semantic-rejection-balances-v1");
    if(check.observation_report)
        frame+=token(*check.observation_report==curriculum::ReportMode::complete
            ? "observation-complete-v1" : "observation-differences-v1");
    if(check.assertion_check)
        frame+=token(*check.assertion_check==curriculum::AssertionMode::enabled_abort
            ? "wasm-assertion-abort-v1"
            : *check.assertion_check==curriculum::AssertionMode::disabled
                ? "wasm-assertion-suppressed-v1" : "wasm-assertion-repair-v1");
    for(const auto& item:check.cases){
        frame+=token(item.standard_input)+token(item.executable.value_or(""));
        if(item.balance_three_pairs)frame+=token("semantic-balance-three-pairs-v1");
        if(legacy)frame+=token(item.explanation);
        frame+=token(item.standard_error==curriculum::StandardErrorExpectation::non_blank?"nonBlank":"ignored")
            +token(std::to_string(item.outputs.size()));
        if(item.exit_code!=0)frame+=token("process-exit-code/v1")+token(std::to_string(item.exit_code));
        if(item.standard_error_exact)frame+=token("standard-error/exact/v1")+token(*item.standard_error_exact);
        for(const auto& output:item.outputs)frame+=token(output);
        frame+=token(std::to_string(item.review_feedback.size()));
        for(const auto& guide:item.review_feedback){
            frame+=token(std::to_string(guide.outputs.size()));
            for(const auto& output:guide.outputs)frame+=token(output);
            frame+=token(guide.text);
        }
    }
    return state::sha256(frame);
}
std::string review_seal(std::string_view key,std::string_view program,std::string_view input,
                        std::string_view target,std::string_view rules,std::string_view feedback) {
    return state::sha256("compiler-review-v1/"+token(key)+token(program)+token(input)+token(target)+token(rules)+token(feedback));
}
std::string diagnostic_review_phase(const CppRunRequest& request) {
    return request.diagnostic&&request.diagnostic->project&&request.diagnostic->project->link?"link":"compile";
}
std::string diagnostic_review_rules(const curriculum::Block& block,const CppRunRequest& request,bool legacy=false) {
    const auto& objective=*request.diagnostic;
    auto frame=std::string(legacy?"compiler-diagnostic-review-rules-v1/":"compiler-diagnostic-review-rules-v2/")+token(block.compile_check->diagnostic);
    if(legacy)frame+=token(block.compile_check->explanation);
    frame+=token(diagnostic_review_phase(request))
        +token(objective.control_source_utf8)+token(objective.expected_error_code)
        +token(std::to_string(objective.first_line))+token(std::to_string(objective.last_line));
    if(objective.project){const auto& project=*objective.project;
        frame+=token(curriculum::cpp_project_frame(project.control_files,project.control_sources,std::nullopt))
            +token(project.expected_file)+token(std::to_string(project.first_line))+token(std::to_string(project.last_line));
        frame+=token(std::to_string(project.error_codes.size()));for(const auto& code:project.error_codes)frame+=token(code);
        frame+=token(std::to_string(project.symbols.size()));for(const auto& symbol:project.symbols)frame+=token(symbol);
    }
    return state::sha256(frame);
}
std::string diagnostic_review_seal(std::string_view key,std::string_view program,std::string_view input,
        std::string_view target,std::string_view rules,std::string_view feedback,const std::vector<std::string>& capture) {
    auto frame="compiler-diagnostic-review-v1/"+token(review_seal(key,program,input,target,rules,feedback));
    for(const auto& field:capture)frame+=token(field);
    return state::sha256(frame);
}
bool diagnostic_review_capture_matches(const std::vector<std::string>& capture,
        const curriculum::Block& block,const CppRunRequest& current) {
    if(capture.size()!=5||capture[0]!="diagnostic-review/v1"||capture[3]!=diagnostic_review_phase(current)||
        capture[4]!=block.compile_check->diagnostic)return false;
    for(const auto n:{1,2}){const auto& text=capture[n];std::uint64_t number=0;
        if(text.empty()||(text.size()>1&&text.front()=='0'))return false;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())return false;
    }
    return true;
}
std::string diagnostic_proof(const std::string& key,std::string_view program,const curriculum::Block& block) {
    return state::sha256("compiler-diagnostic-v1/"+token(key)+token(program)+token(block.compile_check->diagnostic));
}
curriculum::Block historical_block(const curriculum::Block& block) {
    auto original=block;original.output_check=block.compiler_revision->previous_output_check;
    original.initial_code=block.compiler_revision->previous_initial_code;
    original.raw_json=block.compiler_revision->raw_json;original.compiler_revision.reset();
    // Closed archives predate formatting: do not prefer an expanded starter sidecar.
    original.source_format_original_initial_code.reset();
    original.source_format_original_project_files.reset();
    original.compiler_client_epoch.clear();
    original.compiler_case_order_revisions.clear();
    original.compiler_case_revisions.clear();return original;
}
// Each journal entry carries its exact predecessor; case indices are mapped
// only by its authenticated bijection, never by sorting outputs or examples.
curriculum::Block case_order_after_block(const curriculum::Block& current,
        const curriculum::CompilerCaseOrderRevision& revision) {
    auto result=curriculum::compiler_case_order_previous_block(current,revision);
    result.raw_json=revision.after_raw_json;
    if(!result.output_check||revision.order.size()!=result.output_check->cases.size())
        throw std::runtime_error("Invalid compiler case permutation; data was preserved.");
    const auto previous=result.output_check->cases;
    std::vector<bool> seen(previous.size());
    const auto semantics=result.text_semantics;
    std::erase_if(result.text_semantics,[](const auto& item){return item.first.starts_with("/outputCheck/cases/");});
    for(std::size_t index=0;index<revision.order.size();++index) {
        const auto prior=revision.order[index];
        if(prior>=previous.size()||seen[prior])
            throw std::runtime_error("Invalid compiler case permutation; data was preserved.");
        seen[prior]=true;result.output_check->cases[index]=previous[prior];
        const auto prefix="/outputCheck/cases/"+std::to_string(prior)+"/";
        for(const auto& [path,descriptor]:semantics)if(path.starts_with(prefix))
            result.text_semantics.emplace("/outputCheck/cases/"+std::to_string(index)+"/"+path.substr(prefix.size()),descriptor);
    }
    return result;
}
struct CompilerCaseStage {
    std::size_t editorial_index;
    std::string marker;
    curriculum::Block previous,after;
};
std::vector<CompilerCaseStage> compiler_case_stages(const curriculum::Block& block) {
    std::vector<CompilerCaseStage> stages;
    for(const auto& revision:block.compiler_case_order_revisions)
        stages.push_back({revision.editorial_index,"case-order-adopted/"+revision.record_sha256,
            curriculum::compiler_case_order_previous_block(block,revision),case_order_after_block(block,revision)});
    for(const auto& revision:block.compiler_case_revisions)
        stages.push_back({revision.editorial_index,"case-revision-adopted/"+revision.record_sha256,
            curriculum::compiler_case_revision_previous_block(block,revision),
            curriculum::compiler_case_revision_after_block(block,revision)});
    std::ranges::sort(stages,{},&CompilerCaseStage::editorial_index);
    for(std::size_t index=1;index<stages.size();++index)
        if(stages[index-1].editorial_index==stages[index].editorial_index)
            throw std::runtime_error("Ambiguous compiler case revision history; data was preserved.");
    return stages;
}
std::vector<curriculum::Block> historical_blocks(const curriculum::Lesson& lesson,
        const curriculum::Block& block) {
    std::vector<curriculum::Block> result;
    std::set<std::string> seen{base(lesson,block)};
    const auto append=[&](curriculum::Block previous) {
        if(seen.insert(base(lesson,previous)).second)result.push_back(std::move(previous));
    };
    const auto stages=compiler_case_stages(block);
    for(auto it=stages.rbegin();it!=stages.rend();++it) {
        append(it->after);
        append(it->previous);
    }
    if(block.compiler_revision) {
        const auto expanded=stages.empty()?block:stages.front().previous;
        append(historical_block(expanded));
    }
    return result;
}
std::optional<curriculum::Block> historical_receipt_block(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    std::optional<curriculum::Block> partial;
    for(auto previous:historical_blocks(lesson,block)) {
        if(compiler_puzzle_completed(data,lesson,previous))return previous;
        if(!partial&&compiler_puzzle_case_count(data,lesson,previous))partial=std::move(previous);
    }
    return partial;
}
const curriculum::OutputCase* current_crt_feedback_case(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    // This derived flag is granted only by the loader's reviewed CRT owner and
    // editorial proof. Its sole case keeps the same authenticated objective;
    // current guide wording is presentation, not another execution receipt.
    if(!block.crt_output_original||lesson.locale!="es"||lesson.state_layout.version!=2||
        lesson.id!="cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia"||
        block.stable_id!="block_0005_0004"||!block.output_check||block.output_check->cases.size()!=1)return nullptr;
    const auto& item=block.output_check->cases.front();
    if(item.outputs.size()!=2||item.outputs.front()!=*block.crt_output_original)return nullptr;
    std::string expanded;for(const char value:*block.crt_output_original){if(value=='\n')expanded+='\r';expanded+=value;}
    return item.outputs.back()==expanded?&item:nullptr;
}
struct CurrentCompilerGuidance {std::string_view explanation,path;};
std::optional<CurrentCompilerGuidance> current_v2_guidance(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto& layout=lesson.state_layout;
    if(layout.version!=2||layout.locale!=lesson.locale||layout.raw_json.empty()||block.type!=curriculum::BlockType::cpp_compiler||
        !layout.signatures.contains(block.stable_id))return {};
    const auto position=curriculum::stable_block_position(lesson,block.stable_id);
    if(!position||position->page>=layout.current.size()||position->block>=layout.current[position->page].blocks.size()||
        layout.current[position->page].blocks[position->block]!=block.stable_id||
        &lesson.pages[position->page].blocks[position->block]!=&block)return {};
    // A sole authored case has one unambiguous success explanation. Multiple
    // cases keep their exact saved provenance; never choose a guide by wording.
    if(block.compile_check)return CurrentCompilerGuidance{block.compile_check->explanation,"/compileCheck/explanation"};
    if(block.output_check&&block.output_check->cases.size()==1)
        return CurrentCompilerGuidance{block.output_check->cases.front().explanation,"/outputCheck/cases/0/explanation"};
    return {};
}
void validate_receipts(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return;const auto key=base(lesson,block);
    validate_integer_client_receipt(data,lesson,block);
    for(const auto suffix:{"program","completed","feedback"})if(const auto value=state::find(data,"native_windows",key+suffix);value&&!std::holds_alternative<std::string>(*value))
        throw std::runtime_error("Invalid compiler execution provenance; data was preserved.");
    const auto program=state::string_value(data,"native_windows",key+"program");const auto proofs=case_proofs(data,key);
    if(!proofs.empty()&&(program.empty()||proofs.size()!=(block.compile_check?1:block.output_check->cases.size())))
        throw std::runtime_error("Invalid compiler case count or captured program; data was preserved.");
    for(std::size_t index=0;index<proofs.size();++index)if(!proofs[index].empty()&&proofs[index]!=(block.compile_check?diagnostic_proof(key,program,block):proof(key,program,index,block.output_check->cases[index])))
        throw std::runtime_error("A compiler case proof belongs to another program or objective; data was preserved.");
    if(const auto value=state::find(data,"native_windows",key+"completed")) {
        if(!valid_completion_seal(block,key,program,proofs,std::get<std::string>(*value)))
            throw std::runtime_error("Invalid compiler completion proof; data was preserved.");
    }
}
}
bool compiler_selective_history_required(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return selective_history_owner(lesson,block);
}
bool compiler_runtime_request_allowed(const curriculum::Block& block,const CppRunRequest& request) {
    if(request.move_assignment_invariant_probe!=block.move_assignment_invariant_probe)
        return false;
    if(block.move_assignment_invariant_probe) {
        if(request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
           request.cmake||block.cmake||!request.project_files.empty()||
           !request.source_files.empty()||!block.project_files.empty()||
           !block.source_files.empty()||request.named_rule_probe||
           request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
           request.independent_copy_assignment_probe||request.open_battery_probe||
           request.boolean_battery_probe||request.reduction_probe||
           request.integer_battery_probe||request.adjustment_project_probe||
           request.rejection_probe||request.observation_probe||
           request.assertion_mode||request.cmake_graph_check||
           request.capture_cmake_graph||
           !curriculum::cpp_move_invariant_matches(
               *block.move_assignment_invariant_probe,request.source_utf8))
            return false;
    }
    if(request.independent_copy_assignment_probe!=block.independent_copy_assignment_probe)
        return false;
    if(block.independent_copy_assignment_probe) {
        if(request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
           request.cmake||block.cmake||!request.project_files.empty()||
           !request.source_files.empty()||!block.project_files.empty()||
           !block.source_files.empty()||request.named_rule_probe||
           request.indexed_unique_transfer_probe||request.defaulted_move_policy_probe||
           request.open_battery_probe||request.boolean_battery_probe||
           request.reduction_probe||request.integer_battery_probe||
           request.adjustment_project_probe||request.rejection_probe||
           request.observation_probe||request.assertion_mode||
           request.cmake_graph_check||request.capture_cmake_graph||
           !curriculum::cpp_independent_copy_matches(
               *block.independent_copy_assignment_probe,request.source_utf8))
            return false;
    }
    if(request.defaulted_move_policy_probe!=block.defaulted_move_policy_probe)
        return false;
    if(block.defaulted_move_policy_probe) {
        if(request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
           request.cmake||block.cmake||!request.project_files.empty()||
           !request.source_files.empty()||!block.project_files.empty()||
           !block.source_files.empty()||request.named_rule_probe||
           request.indexed_unique_transfer_probe||request.open_battery_probe||
           request.boolean_battery_probe||request.reduction_probe||
           request.integer_battery_probe||request.adjustment_project_probe||
           request.rejection_probe||request.observation_probe||
           request.assertion_mode||request.cmake_graph_check||
           request.capture_cmake_graph||
           !curriculum::cpp_defaulted_move_policy_matches(
               *block.defaulted_move_policy_probe,request.source_utf8))
            return false;
    }
    if(request.indexed_unique_transfer_probe!=block.indexed_unique_transfer_probe)
        return false;
    if(block.indexed_unique_transfer_probe) {
        if(request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
           request.cmake||block.cmake||!request.project_files.empty()||
           !request.source_files.empty()||!block.project_files.empty()||
           !block.source_files.empty()||request.named_rule_probe||
           request.open_battery_probe||request.boolean_battery_probe||
           request.reduction_probe||request.integer_battery_probe||
           request.adjustment_project_probe||request.rejection_probe||
           request.observation_probe||request.assertion_mode||
           request.cmake_graph_check||request.capture_cmake_graph||
           !curriculum::cpp_indexed_unique_transfer_probe_source(
               *block.indexed_unique_transfer_probe,request.source_utf8))
            return false;
    }
    if(request.adjustment_project_probe!=block.adjustment_project_probe)return false;
    if(block.adjustment_project_probe) {
        const auto& spec=*block.adjustment_project_probe;
        if(!block.cmake||!request.cmake||!block.cmake_graph_check||
           !curriculum::adjustment_project_spec_valid(spec)||
           !curriculum::adjustment_project_graph_valid(spec,*block.cmake_graph_check)||
           request.cmake->target!=spec.test_target||
           request.cmake->build_type!=block.cmake->build_type||
           request.cmake->options!=block.cmake->options||
           request.backend!=CppRunRequest::Backend::native_msvc||
           (request.action!=CppRunRequest::Action::compile_run&&
            request.action!=CppRunRequest::Action::execute)||
           !request.stdin_utf8.empty()||request.diagnostic||
           !request.capture_cmake_graph||
           request.source_files!=block.source_files||!request.source_files.empty()||
           request.project_files.size()!=block.project_files.size()||
           request.open_battery_probe||request.boolean_battery_probe||
           request.integer_battery_probe||request.reduction_probe||
           request.named_rule_probe||request.rejection_probe||
           request.observation_probe||request.assertion_mode)return false;
        std::set<std::string> paths;
        for(const auto& [path,source]:request.project_files) {
            if(!paths.insert(path).second)return false;
            const auto original=std::ranges::find_if(block.project_files,
                [&](const auto& item){return item.first==path;});
            if(original==block.project_files.end())return false;
            if((path==spec.library_path||path==spec.client_path||path==spec.test_path)&&
               !curriculum::adjustment_project_source_includes_header(source,spec))return false;
            if(path==spec.header_path) {
                if(!curriculum::code_format::equivalent(original->second,source,
                        curriculum::code_format::Language::cpp))return false;
            } else if(path!="CMakeLists.txt"&&path!=spec.library_path&&
                      path!=spec.client_path&&path!=spec.test_path&&
                      source!=original->second)return false;
        }
    }
    if(request.boolean_battery_probe!=block.boolean_battery_probe)return false;
    if(request.reduction_probe!=block.reduction_probe)return false;
    if(request.integer_battery_probe!=block.integer_battery_probe)return false;
    if(block.integer_battery_probe&&
       (!integer_battery_project_matches(block,request)||
        request.cmake->target!=block.integer_battery_probe->test_target||
        (request.action!=CppRunRequest::Action::compile_run&&
         request.action!=CppRunRequest::Action::execute)))return false;
    if(block.integer_battery_probe) {
        const auto driver=std::ranges::find_if(request.project_files,[&](const auto& item) {
            return item.first==block.integer_battery_probe->test_path;
        });
        if(driver==request.project_files.end()||
           !curriculum::integer_battery_driver_valid(driver->second))return false;
    }
    if(block.boolean_battery_probe) {
        if(block.cmake||request.cmake||!block.project_files.empty()||
           !request.project_files.empty()||!block.source_files.empty()||
           !request.source_files.empty()||request.diagnostic||
           request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||
           request.stdin_utf8!=block.standard_input.value_or("")||
           request.reduction_probe||request.open_battery_probe||request.named_rule_probe||
           request.rejection_probe||request.observation_probe||
           request.assertion_mode||request.cmake_graph_check||request.capture_cmake_graph||
           !curriculum::boolean_battery_preserves_function(
               request.source_utf8,*block.boolean_battery_probe))return false;
    }
    if(block.reduction_probe) {
        if(block.cmake||request.cmake||!block.project_files.empty()||
           !request.project_files.empty()||!block.source_files.empty()||
           !request.source_files.empty()||request.diagnostic||
           request.backend!=CppRunRequest::Backend::native_msvc||
           request.action!=CppRunRequest::Action::compile_run||
           request.stdin_utf8!=block.standard_input.value_or("")||
           request.boolean_battery_probe||request.open_battery_probe||request.named_rule_probe||
           request.rejection_probe||request.observation_probe||request.assertion_mode||
           request.cmake_graph_check||request.capture_cmake_graph||
           !curriculum::reduction_source_check(request.source_utf8,*block.reduction_probe).accepted)
            return false;
    }
    if(request.open_battery_probe!=block.open_battery_probe)return false;
    if(block.open_battery_probe) {
        const auto& authored=*block.open_battery_probe;
        if(!block.cmake||request.cmake!=block.cmake||
           !curriculum::open_battery_probe_spec_valid(authored)||
           request.cmake->target!=authored.test_target||
           (request.action!=CppRunRequest::Action::compile_run&&
            request.action!=CppRunRequest::Action::execute)||
           !request.stdin_utf8.empty()||
           request.backend!=CppRunRequest::Backend::native_msvc||
           request.diagnostic||request.source_files!=block.source_files||
           request.project_files.size()!=block.project_files.size())
            return false;
        // The student's test driver alone may change. The shared function,
        // interface, client and original build description must remain
        // unchanged until Work publishes a separately authenticated revision.
        for(std::size_t i=0;i<request.project_files.size();++i) {
            const auto& original=block.project_files[i];
            const auto& candidate=request.project_files[i];
            if(original.first!=candidate.first||
               (original.first!=authored.test_path&&
                original.second!=candidate.second))return false;
        }
    }
    if(request.cmake_graph_check!=block.cmake_graph_check)return false;
    if(block.cmake_graph_check) {
        if(!block.cmake||!request.cmake||
           request.cmake->build_type!=block.cmake->build_type||
           request.cmake->options!=block.cmake->options||
           request.backend!=CppRunRequest::Backend::native_msvc||
           (request.action!=CppRunRequest::Action::compile_run&&
            request.action!=CppRunRequest::Action::execute&&
            !(block.cmake_graph_check->initial_observation&&
              request.action==CppRunRequest::Action::build))||
           !request.capture_cmake_graph||request.diagnostic)
            return false;
        // A published editor may start on the client while its mandatory
        // first output case runs the checker. Permit only executables named
        // in existing output cases and in this authenticated CMake graph.
        if(request.cmake->target!=block.cmake->target) {
            if(!block.output_check||
               !std::ranges::any_of(block.output_check->cases,
                   [&](const auto& item) {
                       return item.executable==request.cmake->target;
                   })||
               !std::ranges::any_of(block.cmake_graph_check->targets,
                   [&](const auto& item) {
                       return item.name==request.cmake->target&&
                           item.type=="EXECUTABLE";
                   }))
                return false;
        }
        if(block.cmake_graph_check->expression_repair) {
            const auto& repair=*block.cmake_graph_check->expression_repair;
            const auto original=std::ranges::find_if(block.project_files,
                [&](const auto& file){return file.first==repair.path;});
            if(!curriculum::cpp_expression_repair_valid(repair)||
               original==block.project_files.end()||
               original->second!=repair.original_source||
               request.source_files!=block.source_files||
               request.project_files.size()!=block.project_files.size())return false;
            std::set<std::string> seen;
            for(const auto& [path,source]:request.project_files) {
                if(!seen.insert(path).second)return false;
                const auto published=std::ranges::find_if(block.project_files,
                    [&](const auto& file){return file.first==path;});
                if(published==block.project_files.end())return false;
                if(path==repair.path) {
                    if(!curriculum::cpp_expression_repair_matches(repair,source))return false;
                }else {
                    const auto slash=path.find_last_of('/');
                    const auto filename=path.substr(slash==path.npos?0:slash+1);
                    const bool cmake=filename=="CMakeLists.txt"||path.ends_with(".cmake");
                    if(!cmake&&source!=published->second)return false;
                }
            }
        }
    }
    if(block.named_rule_source_check) {
        const auto& named=*block.named_rule_source_check;
        if(request.named_rule_probe!=named.execution_probe)return false;
        if(named.execution_probe&&named.execution_probe->battery_check) {
            const auto& battery=*named.execution_probe->battery_check;
            if(!curriculum::named_rule_battery_spec_valid(battery)||
               battery.source_path!=named.path||!request.stdin_utf8.empty()||
               request.backend!=CppRunRequest::Backend::native_msvc||
               request.open_battery_probe||request.boolean_battery_probe||
               request.reduction_probe||request.integer_battery_probe||
               request.adjustment_project_probe||request.rejection_probe||
               request.observation_probe||request.assertion_mode||
               request.cmake_graph_check||request.capture_cmake_graph)return false;
        }
        if(named.execution_probe&&named.execution_probe->constant_variations) {
            const auto& variations=*named.execution_probe->constant_variations;
            if(variations.source_path!=named.path||variations.requirements!=named.requirements||
               !curriculum::named_rule_constant_variation_spec_valid(
                    variations,named.execution_probe->cases)||
               request.backend!=CppRunRequest::Backend::native_msvc||
               request.open_battery_probe||request.boolean_battery_probe||
               request.reduction_probe||request.integer_battery_probe||
               request.adjustment_project_probe||request.rejection_probe||
               request.observation_probe||request.assertion_mode||
               request.cmake_graph_check||request.capture_cmake_graph)return false;
        }
        if(!named.execution_probe&&std::ranges::any_of(named.editable_paths,
                [](std::string_view path) {
                    return curriculum::is_cpp_source_file(path);
                }))return false;
        if(request.action!=CppRunRequest::Action::compile_run||request.diagnostic||
           request.cmake!=block.cmake||request.source_files!=block.source_files)
            return false;
        std::string_view source;
        if(named.path.empty()) {
            if(!block.project_files.empty()||!request.project_files.empty()||
               request.cmake||!named.editable_paths.empty()||request.source_files.size())
                return false;
            source=request.source_utf8;
        }else{
            if(block.project_files.empty()||request.project_files.size()!=block.project_files.size())
                return false;
            for(std::size_t i=0;i<block.project_files.size();++i) {
                const auto& original=block.project_files[i];
                const auto& current=request.project_files[i];
                if(original.first!=current.first)return false;
                if(original.first!=named.path&&
                   std::ranges::find(named.editable_paths,original.first)==
                       named.editable_paths.end()&&
                   original.second!=current.second)return false;
                if(std::ranges::find(named.comment_only_paths,original.first)!=
                       named.comment_only_paths.end()&&
                   !curriculum::named_rule_comment_only_edit(
                       original.second,current.second))return false;
            }
            const auto found=std::ranges::find_if(request.project_files,
                [&](const auto& file){return file.first==named.path;});
            if(found==request.project_files.end())return false;
            source=found->second;
        }
        if(named.execution_probe&&
           !curriculum::named_rule_detail::probe_source_no_early_exit(source))
            return false;
        if(!curriculum::analyze_named_rule_sources(source,named.requirements).source_evidence)
            return false;
        if(named.execution_probe&&named.execution_probe->constant_variations)
            for(std::size_t index=0;
                    index<named.execution_probe->constant_variations->variations.size();++index)
                if(!curriculum::named_rule_constant_variation_source(
                        source,*named.execution_probe,index))return false;
    }
    const auto goal=block.compiler_workflow_check;
    if(goal==curriculum::CompilerWorkflowCheck::assertion_abort_wasi_v1||
       goal==curriculum::CompilerWorkflowCheck::assertion_suppressed_wasi_v1||
       goal==curriculum::CompilerWorkflowCheck::assertion_repaired_wasi_v1) {
        const auto mode=goal==curriculum::CompilerWorkflowCheck::assertion_abort_wasi_v1
            ?curriculum::AssertionMode::enabled_abort
            :goal==curriculum::CompilerWorkflowCheck::assertion_suppressed_wasi_v1
                ?curriculum::AssertionMode::disabled:curriculum::AssertionMode::repaired;
        return request.backend==CppRunRequest::Backend::wasi&&
            !request.cmake&&!request.diagnostic&&
            !request.rejection_probe&&!request.observation_probe&&
            request.project_files.empty()&&request.source_files.empty()&&
            request.action==CppRunRequest::Action::compile_run&&
            request.assertion_mode==mode&&
            (mode==curriculum::AssertionMode::repaired||
                request.stdin_utf8==block.standard_input.value_or(""))&&
            curriculum::assertion_source_allowed(mode,block.initial_code,request.source_utf8);
    }
    if(goal==curriculum::CompilerWorkflowCheck::observation_complete_v1||
       goal==curriculum::CompilerWorkflowCheck::observation_differences_v1) {
        const auto mode=goal==curriculum::CompilerWorkflowCheck::observation_complete_v1
            ?curriculum::ReportMode::complete:curriculum::ReportMode::differences;
        return !request.cmake&&!request.diagnostic&&!request.rejection_probe&&
            request.project_files.empty()&&request.source_files.empty()&&
            request.action==CppRunRequest::Action::compile_run&&
            request.observation_probe==mode&&
            curriculum::report_main_only_edit(block.initial_code,request.source_utf8);
    }
    if(goal==curriculum::CompilerWorkflowCheck::semantic_rejection_boxes_v1||
       goal==curriculum::CompilerWorkflowCheck::semantic_rejection_balance_v1){
        const auto operation=goal==curriculum::CompilerWorkflowCheck::semantic_rejection_boxes_v1
            ?curriculum::RejectionOperation::boxes:curriculum::RejectionOperation::balance;
        return !request.cmake&&!request.diagnostic&&request.project_files.empty()&&
            request.source_files.empty()&&request.action==CppRunRequest::Action::compile_run&&
            request.rejection_probe==operation&&
            curriculum::rejection_function_only_edit(block.initial_code,request.source_utf8,operation);
    }
    if(goal!=curriculum::CompilerWorkflowCheck::exact_original_project_runtime_v1)return true;
    return request.backend==CppRunRequest::Backend::native_msvc&&
        request.project_files==block.project_files&&request.source_files==block.source_files&&
        request.cmake==block.cmake&&request.stdin_utf8==block.standard_input.value_or("");
}
std::string compiler_program_frame(const CppRunRequest& request) {
    if(request.project_files.empty())return "single-file-v1/"+token(request.source_utf8);
    auto configuration=request.cmake;
    // Cases may select different executables of one unchanged CMake project.
    // Target identity is checked separately in each authored case proof.
    if(configuration)configuration->target.clear();
    return "project-v1/"+curriculum::cpp_project_frame(request.project_files,request.source_files,configuration);
}

// A second, independent objective never changes or inherits the historical
// stdout-only receipt. It adds one separate, source-bound completion seal.
std::string defaulted_move_completion_seal(std::string_view key,
        std::string_view program,const curriculum::CppDefaultedMovePolicy& spec) {
    return state::sha256(std::string("compiler-defaulted-move-policy-v1/")+
        token(key)+token(program)+token(spec.original_source)+
        token(spec.class_name)+token(spec.member_name)+
        token(spec.put_method)+token(spec.change_method)+token(spec.clear_method));
}
std::string independent_copy_assignment_completion_seal(
        std::string_view key,std::string_view program,
        const curriculum::CppIndependentCopyAssignmentPolicy& spec) {
    return state::sha256(std::string("compiler-independent-copy-assignment-v1/")+
        token(key)+token(program)+token(spec.original_source)+
        token(spec.class_name)+token(spec.member_name)+token(spec.parameter_name)+
        token(spec.has_method)+token(spec.value_method)+token(spec.access_method)+
        token(spec.put_method)+token(spec.change_method)+token(spec.clear_method));
}
std::string move_assignment_invariant_completion_seal(
        std::string_view key,std::string_view program,
        const curriculum::CppMoveAssignmentInvariantPolicy& spec) {
    return state::sha256(std::string("compiler-move-invariant-v1/")+
        token(key)+token(program)+token(spec.original_source)+
        token(spec.class_name)+token(spec.member_name)+
        token(spec.label_member)+token(spec.parameter_name)+
        token(spec.has_method)+token(spec.value_method)+token(spec.label_method)+
        token(spec.access_method)+token(spec.invariant_method)+
        token(spec.put_method)+token(spec.change_method)+
        token(spec.retag_method)+token(spec.clear_method));
}
std::string indexed_transfer_completion_seal(std::string_view key,
        std::string_view program,const curriculum::CppIndexedUniqueTransferProbe& spec) {
    return state::sha256(std::string("compiler-indexed-unique-owner-v1/")+
        token(key)+token(program)+token(spec.member.original_source)+
        token(spec.member.signature)+token(spec.class_name)+
        token(spec.first_member)+token(spec.second_member)+
        token(spec.transfer_method)+token(spec.put_method)+
        token(spec.change_method)+token(spec.clear_method));
}
static bool stdout_or_diagnostic_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return false;
    const auto key=base(lesson,block);
    const auto captured=state::string_value(data,"native_windows",key+"program");
    const auto proofs=case_proofs(data,key);
    if(block.indexed_unique_transfer_probe&&
       state::string_value(data,"native_windows",key+"indexed-unique-transfer/v1")!=
           indexed_transfer_completion_seal(key,captured,
               *block.indexed_unique_transfer_probe))return false;
    if(block.defaulted_move_policy_probe&&
       state::string_value(data,"native_windows",key+"defaulted-move-policy/v1")!=
           defaulted_move_completion_seal(key,captured,
               *block.defaulted_move_policy_probe))return false;
    if(block.independent_copy_assignment_probe&&
       state::string_value(data,"native_windows",key+"independent-copy-assignment/v1")!=
           independent_copy_assignment_completion_seal(key,captured,
               *block.independent_copy_assignment_probe))return false;
    if(block.move_assignment_invariant_probe&&
       state::string_value(data,"native_windows",key+"move-invariant/v1")!=
           move_assignment_invariant_completion_seal(key,captured,
               *block.move_assignment_invariant_probe))return false;
    if(captured.empty()||proofs.size()!=(block.compile_check?1:block.output_check->cases.size())||proofs.empty())return false;
    for(std::size_t index=0;index<proofs.size();++index)
        if(!proofs[index].empty()&&
            proofs[index]!=(block.compile_check?diagnostic_proof(key,captured,block):proof(key,captured,index,block.output_check->cases[index])))return false;
    return valid_completion_seal(block,key,captured,proofs,
        state::string_value(data,"native_windows",key+"completed"));
}
bool compiler_puzzle_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return stdout_or_diagnostic_completed(data,lesson,block)
        &&selective_history_receipt_valid(data,lesson,block);
}
std::string compiler_puzzle_feedback(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!compiler_puzzle_completed(data,lesson,block))return {};
    auto stored=state::string_value(data,"native_windows",base(lesson,block)+"feedback");
    if(!stored.empty())if(const auto current=current_crt_feedback_case(lesson,block);current&&!current->explanation.empty())
        return stored.substr(0,stored.find_first_of("\r\n"))+'\n'+current->explanation;
    // The same authenticated, active-owner v2 rule already used by the
    // detailed feedback API also applies to callers of this text-only getter.
    // The reader may show revised guidance without replaying the execution or
    // rewriting its saved receipt. Archived copies and independent multicase
    // goals are deliberately excluded by current_v2_guidance().
    if(!stored.empty())if(const auto guidance=current_v2_guidance(lesson,block);
        guidance&&!guidance->explanation.empty())
        return stored.substr(0,stored.find_first_of("\r\n"))+'\n'+
            std::string(guidance->explanation);
    return stored;
}
std::string compiler_puzzle_review(const state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CppRunRequest& current) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!curriculum::has_compiler_objective(block))return {};
    if(block.boolean_battery_probe!=current.boolean_battery_probe)return {};
    if(block.reduction_probe!=current.reduction_probe)return {};
    if(block.integer_battery_probe!=current.integer_battery_probe)return {};
    const auto key=base(lesson,block)+"review/";
    const auto text=[&](std::string_view suffix)->const std::string*{
        const auto value=state::find(data,"native_windows",key+std::string(suffix));
        return value?std::get_if<std::string>(value):nullptr;
    };
    const auto program=text("program"),input=text("input"),target=text("target"),rules=text("rules"),feedback=text("feedback"),seal=text("seal");
    if(!program||!input||!target||!rules||!feedback||!seal||feedback->empty()
        ||*program!=compiler_program_frame(current)||*input!=current.stdin_utf8||*target!=review_target(current))return {};
    if(block.compile_check){
        if(!current.diagnostic||*current.diagnostic!=prepare_cpp_diagnostic_request(block,current))return {};
        const auto stored=state::find(data,"native_windows",key+"capture");
        const auto capture=stored?std::get_if<std::vector<std::string>>(stored):nullptr;
        if(!capture||!diagnostic_review_capture_matches(*capture,block,current)||
            (*rules!=diagnostic_review_rules(block,current)&&*rules!=diagnostic_review_rules(block,current,true))||
            *seal!=diagnostic_review_seal(key,*program,*input,*target,*rules,*feedback,*capture))return {};
    }else if((*rules!=review_rules(block)&&*rules!=review_rules(block,true))||*seal!=review_seal(key,*program,*input,*target,*rules,*feedback))return {};
    return *feedback;
}
std::optional<CompilerFeedback> compiler_puzzle_success_feedback(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return std::nullopt;
    const auto key=base(lesson,block);
    if(const auto guidance=current_v2_guidance(lesson,block);guidance&&compiler_puzzle_completed(data,lesson,block)){
        const auto original=state::string_value(data,"native_windows",key+"feedback");
        if(!original.empty()){
            auto shown=original.substr(0,original.find_first_of("\r\n"));
            if(!guidance->explanation.empty())shown+='\n'+std::string(guidance->explanation);
            return CompilerFeedback{std::move(shown),true,guidance->explanation.empty()?std::string{}:std::string(guidance->path)};
        }
    }
    if(auto earned=compiler_puzzle_feedback(data,lesson,block);!earned.empty()){
        auto path=restored_feedback_origin(data,key,block,earned,state::string_value(data,"native_windows",key+"completed"));
        if(const auto current_case=current_crt_feedback_case(lesson,block);current_case&&!current_case->explanation.empty())
            path="/outputCheck/cases/0/explanation";
        return CompilerFeedback{std::move(earned),true,std::move(path)};
    }
    return std::nullopt;
}
std::optional<CompilerFeedback> compiler_puzzle_current_feedback(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,const CppRunRequest& current) {
    if(!curriculum::has_compiler_objective(block))return std::nullopt;
    const auto key=base(lesson,block);
    if(auto review=compiler_puzzle_review(data,lesson,block,current);!review.empty()){
        auto path=restored_feedback_origin(data,key+"review/",block,review,state::string_value(data,"native_windows",key+"review/seal"));
        return CompilerFeedback{std::move(review),false,std::move(path)};
    }
    return compiler_puzzle_success_feedback(data,lesson,block);
}
bool record_compiler_puzzle_review(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CppRunRequest& captured,const curriculum::FreshExecution& execution,
        std::string_view feedback) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check||feedback.empty()||!matches_runtime_capture(captured,execution))return false;
    if(block.boolean_battery_probe!=captured.boolean_battery_probe)return false;
    if(block.reduction_probe!=captured.reduction_probe)return false;
    if(block.integer_battery_probe!=captured.integer_battery_probe)return false;
    const auto evaluation=evaluate_cpp_output_check(*block.output_check,execution,captured.revision);
    if(evaluation.verdict!=curriculum::OutputVerdict::incorrect)return false;
    const auto key=base(lesson,block)+"review/",program=compiler_program_frame(captured),target=review_target(captured),rules=review_rules(block);
    auto& values=data.preferences["native_windows"];
    values[key+"program"]=program;values[key+"input"]=captured.stdin_utf8;values[key+"target"]=target;
    values[key+"rules"]=rules;values[key+"feedback"]=std::string(feedback);
    values[key+"seal"]=review_seal(key,program,captured.stdin_utf8,target,rules,feedback);
    record_feedback_origin(data,key,block,evaluation.feedback_path,feedback,std::get<std::string>(values.at(key+"seal")));
    return true;
}
bool record_compiler_diagnostic_review(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CppRunRequest& captured,const CppRunResult& result,
        std::string_view feedback) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.compile_check||block.output_check||feedback.empty()||
        !captured.diagnostic||*captured.diagnostic!=prepare_cpp_diagnostic_request(block,captured)||
        !accepted_compile_diagnostic(captured,result))return false;
    const auto key=base(lesson,block)+"review/",program=compiler_program_frame(captured),target=review_target(captured),
        rules=diagnostic_review_rules(block,captured);
    const std::vector<std::string> capture{"diagnostic-review/v1",std::to_string(result.request_id),std::to_string(result.revision),
        diagnostic_review_phase(captured),block.compile_check->diagnostic};
    auto& values=data.preferences["native_windows"];
    values[key+"program"]=program;values[key+"input"]=captured.stdin_utf8;values[key+"target"]=target;
    values[key+"rules"]=rules;values[key+"feedback"]=std::string(feedback);values[key+"capture"]=capture;
    values[key+"seal"]=diagnostic_review_seal(key,program,captured.stdin_utf8,target,rules,feedback,capture);
    // This is generated failure guidance, not the authored success explanation.
    record_feedback_origin(data,key,block,{},feedback,std::get<std::string>(values.at(key+"seal")));
    return true;
}
void clear_compiler_puzzle_review(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return;
    const auto prefix=base(lesson,block)+"review/";
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
}
std::size_t compiler_puzzle_case_count(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block)||
        !selective_history_receipt_valid(data,lesson,block))return 0;
    if(block.compile_check)return compiler_puzzle_completed(data,lesson,block)?1:0;
    const auto key=base(lesson,block),captured=state::string_value(data,"native_windows",key+"program");
    const auto proofs=case_proofs(data,key);std::size_t count=0;
    for(std::size_t index=0;index<std::min(proofs.size(),block.output_check->cases.size());++index)
        if(proofs[index]==proof(key,captured,index,block.output_check->cases[index]))++count;
    return count;
}
std::size_t compiler_puzzle_historical_case_count(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto previous=historical_receipt_block(data,lesson,block);
    return previous?compiler_puzzle_case_count(data,lesson,*previous):0;
}
bool compiler_puzzle_historical_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto previous=historical_receipt_block(data,lesson,block);
    return previous&&compiler_puzzle_completed(data,lesson,*previous);
}
std::string compiler_puzzle_historical_feedback(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto previous=historical_receipt_block(data,lesson,block);
    return previous?compiler_puzzle_feedback(data,lesson,*previous):std::string{};
}
void validate_compiler_puzzle_state(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    // Client provenance is independent of the optional editorial case history.
    validate_integer_client_receipt(data,lesson,block);
    validate_cmake_initial_receipts(data,lesson,block);
    // Never erase a malformed additional receipt. The original stdout proof
    // stays intact; the new record must be independently authenticated.
    if(selective_history_owner(lesson,block)&&
        state::find(data,"native_windows",selective_history_key(lesson,block))&&
        !selective_history_receipt_valid(data,lesson,block))
        throw std::runtime_error("Invalid selective recovery provenance; historical completion was preserved.");
    // An independent owner identity is never an old stdout answer. Absence
    // is legitimate while the learner has not passed the new private witness;
    // a present but wrong/typed-invalid seal is an import-integrity error.
    if(block.indexed_unique_transfer_probe) {
        const auto key=base(lesson,block);
        if(const auto* stored=state::find(data,"native_windows",
                key+"indexed-unique-transfer/v1")) {
            const auto* receipt=std::get_if<std::string>(stored);
            const auto program=state::string_value(data,"native_windows",key+"program");
            const auto cases=case_proofs(data,key);
            if(!receipt||program.empty()||
               *receipt!=indexed_transfer_completion_seal(
                    key,program,*block.indexed_unique_transfer_probe)||
               !valid_completion_seal(block,key,program,cases,
                   state::string_value(data,"native_windows",key+"completed")))
                throw std::runtime_error(
                    "Invalid independent C++ owner identity receipt; historical state was preserved.");
        }
    }
    if(block.defaulted_move_policy_probe) {
        const auto key=base(lesson,block);
        if(const auto* stored=state::find(data,"native_windows",
                key+"defaulted-move-policy/v1")) {
            const auto* receipt=std::get_if<std::string>(stored);
            const auto program=state::string_value(data,"native_windows",key+"program");
            const auto cases=case_proofs(data,key);
            if(!receipt||program.empty()||
               *receipt!=defaulted_move_completion_seal(
                    key,program,*block.defaulted_move_policy_probe)||
               !valid_completion_seal(block,key,program,cases,
                    state::string_value(data,"native_windows",key+"completed")))
                throw std::runtime_error(
                    "Invalid defaulted move policy receipt; historical answers were preserved.");
        }
    }
    if(block.independent_copy_assignment_probe) {
        const auto key=base(lesson,block);
        if(const auto* stored=state::find(data,"native_windows",
                key+"independent-copy-assignment/v1")) {
            const auto* receipt=std::get_if<std::string>(stored);
            const auto program=state::string_value(data,"native_windows",key+"program");
            const auto cases=case_proofs(data,key);
            if(!receipt||program.empty()||
               *receipt!=independent_copy_assignment_completion_seal(
                    key,program,*block.independent_copy_assignment_probe)||
               !valid_completion_seal(block,key,program,cases,
                    state::string_value(data,"native_windows",key+"completed")))
                throw std::runtime_error(
                    "Invalid independent copy assignment receipt; historical answers were preserved.");
        }
    }
    if(block.move_assignment_invariant_probe) {
        const auto key=base(lesson,block);
        if(const auto* stored=state::find(data,"native_windows",
                key+"move-invariant/v1")) {
            const auto* receipt=std::get_if<std::string>(stored);
            const auto program=state::string_value(data,"native_windows",key+"program");
            const auto cases=case_proofs(data,key);
            if(!receipt||program.empty()||
               *receipt!=move_assignment_invariant_completion_seal(
                    key,program,*block.move_assignment_invariant_probe)||
               !valid_completion_seal(block,key,program,cases,
                    state::string_value(data,"native_windows",key+"completed")))
                throw std::runtime_error(
                    "Invalid move-assignment invariant receipt; previous state was preserved.");
        }
    }
    // Existing objectives retain their historical permissive decoding. The
    // reviewed migration validates every receipt it is about to reuse.
    if(block.compile_check){validate_receipts(data,lesson,block);return;}
    if(!block.compiler_revision&&block.compiler_case_order_revisions.empty()&&block.compiler_case_revisions.empty())return;
    for(const auto& previous:historical_blocks(lesson,block))validate_receipts(data,lesson,previous);
    validate_receipts(data,lesson,block);
}
static void prepare_compiler_expansion(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block) {
    if(block.compiler_revision) {
        const auto key=base(lesson,block),marker=root(lesson,block)+"revision-adopted/"+state::sha256(curriculum::reference_objective_frame(lesson,block));
        if(const auto adopted=state::find(data,"native_windows",marker)) {
            if(!std::holds_alternative<bool>(*adopted)||!std::get<bool>(*adopted))throw std::runtime_error("Invalid compiler expansion adoption marker; data was preserved.");
            return;
        }
        const auto original=historical_block(block);const auto old_key=base(lesson,original);
        const auto program=state::string_value(data,"native_windows",old_key+"program");
        if(!program.empty()&&!state::find(data,"native_windows",key+"program")) {
            const auto old_proofs=case_proofs(data,old_key);std::vector<std::string> proofs(block.output_check->cases.size());
            if(original.output_check->input_comparison==block.output_check->input_comparison)
                for(std::size_t index=0;index<block.output_check->cases.size();++index)
                    for(std::size_t prior=0;prior<old_proofs.size();++prior)
                        if(!old_proofs[prior].empty()&&curriculum::same_output_case_requirements(original.output_check->cases[prior],block.output_check->cases[index])) {
                            proofs[index]=proof(key,program,index,block.output_check->cases[index]);break;
                        }
            // A wholly replaced expected report keeps its old achievement,
            // but does not receive a current receipt or completed seal.
            if(std::ranges::any_of(proofs,[](const auto& value){return !value.empty();})) {
                data.preferences["native_windows"][key+"program"]=program;
                data.preferences["native_windows"][key+"cases"]=std::move(proofs);
            }
        }
        data.preferences["native_windows"][marker]=true;
    }
}
// Reuse only an execution of the identical complete case by one program in
// the same authenticated client-source epoch.
// An aggregate receipt never proves any of the isolated inputs it contains.
static void adopt_compiler_case_stage(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& after,const std::vector<curriculum::Block>& previous_versions) {
    const auto key=base(lesson,after);
    if(state::find(data,"native_windows",key+"completed"))return;
    auto program=state::string_value(data,"native_windows",key+"program");
    auto proofs=case_proofs(data,key);proofs.resize(after.output_check->cases.size());
    bool added=false;
    for(auto version=previous_versions.rbegin();version!=previous_versions.rend();++version) {
        if(!version->output_check||version->output_check->input_comparison!=after.output_check->input_comparison||
           version->compiler_client_epoch!=after.compiler_client_epoch)continue;
        const auto old_key=base(lesson,*version);
        if(old_key==key)continue;
        const auto previous_program=state::string_value(data,"native_windows",old_key+"program");
        if(previous_program.empty()||(!program.empty()&&previous_program!=program))continue;
        const auto previous_proofs=case_proofs(data,old_key);
        for(std::size_t index=0;index<after.output_check->cases.size();++index) {
            if(!proofs[index].empty())continue;
            for(std::size_t prior=0;prior<previous_proofs.size();++prior) {
                if(previous_proofs[prior].empty()||!curriculum::same_output_case_requirements(
                        version->output_check->cases[prior],after.output_check->cases[index]))continue;
                if(program.empty())program=previous_program;
                proofs[index]=proof(key,program,index,after.output_check->cases[index]);added=true;break;
            }
        }
    }
    if(program.empty()||(!added&&(proofs.empty()||proofs.front().empty())))return;
    auto& values=data.preferences["native_windows"];
    values[key+"program"]=program;values[key+"cases"]=proofs;
    // Existing proof positions remain intact, including on an explicit return.
    // Historical completion by itself never grants this version's first case.
    if(!proofs.empty()&&!proofs.front().empty()) {
        const auto seal=std::ranges::all_of(proofs,[](const auto& item){return !item.empty();})
            ?complete_seal(key,program,proofs):first_case_complete_seal(key,program,proofs);
        values[key+"completed"]=seal;
        if(!state::find(data,"native_windows",key+"feedback")) {
            const auto& guide=after.output_check->cases.front().explanation;
            if(!guide.empty()) {
                values[key+"feedback"]=guide;
                record_feedback_origin(data,key,after,"/outputCheck/cases/0/explanation",guide,seal);
            }
        }
    }
}
void prepare_compiler_puzzle_revisions(state::StateData& data,const curriculum::Lesson& lesson) {
    // Validate every reusable version before the staged store changes any receipt.
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if((block.integer_battery_probe&&block.integer_battery_probe->prerequisite)||
           (block.cmake_graph_check&&block.cmake_graph_check->initial_observation)||
           block.compiler_revision||!block.compiler_case_order_revisions.empty()||!block.compiler_case_revisions.empty())
            validate_compiler_puzzle_state(data,lesson,block);
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks) {
        const auto stages=compiler_case_stages(block);
        if(stages.empty()) {
            prepare_compiler_expansion(data,lesson,block);continue;
        }
        // Expansions, permutations and case replacements keep their own epochs.
        // Archived versions remain available even when a case disappears and
        // subsequently returns; proofs are never combined across programs.
        prepare_compiler_expansion(data,lesson,stages.front().previous);
        std::vector<curriculum::Block> previous_versions;
        if(block.compiler_revision)previous_versions.push_back(historical_block(stages.front().previous));
        for(const auto& stage:stages) {
            previous_versions.push_back(stage.previous);
            const auto marker=root(lesson,block)+stage.marker;
            if(const auto adopted=state::find(data,"native_windows",marker)) {
                if(!std::holds_alternative<bool>(*adopted)||!std::get<bool>(*adopted))
                    throw std::runtime_error("Invalid compiler case adoption marker; data was preserved.");
            } else {
                adopt_compiler_case_stage(data,lesson,stage.after,previous_versions);
                data.preferences["native_windows"][marker]=true;
            }
            previous_versions.push_back(stage.after);
        }
    }
}
bool record_compiler_puzzle_case(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const curriculum::FreshExecution& execution,std::string_view feedback) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check||
       !matches_runtime_capture(captured,execution)||!compiler_runtime_request_allowed(block,captured)||
       (block.defaulted_move_policy_probe&&
           (!execution.defaulted_move_policy_probe_correct||
            execution.defaulted_move_policy_request_fingerprint.empty()||
            execution.defaulted_move_policy_request_fingerprint!=
                cpp_defaulted_move_policy_request_fingerprint(captured)))||
       (block.independent_copy_assignment_probe&&
           (!execution.independent_copy_assignment_probe_correct||
            execution.independent_copy_assignment_request_fingerprint.empty()||
            execution.independent_copy_assignment_request_fingerprint!=
                cpp_independent_copy_assignment_request_fingerprint(captured)))||
       (block.move_assignment_invariant_probe&&
           (!execution.move_assignment_invariant_probe_correct||
            execution.move_assignment_invariant_request_fingerprint.empty()||
            execution.move_assignment_invariant_request_fingerprint!=
                cpp_move_assignment_invariant_request_fingerprint(captured)))||
       (block.indexed_unique_transfer_probe&&
           (!execution.indexed_unique_transfer_probe_correct||
            execution.indexed_unique_transfer_request_fingerprint.empty()||
            execution.indexed_unique_transfer_request_fingerprint!=
                cpp_indexed_unique_transfer_request_fingerprint(captured)))||
       (block.named_rule_source_check&&
           block.named_rule_source_check->execution_probe&&
           !execution.named_rule_probe_correct)||
       (block.named_rule_source_check&&
           block.named_rule_source_check->execution_probe&&
           block.named_rule_source_check->execution_probe->battery_check&&
           !execution.named_rule_battery_correct)||
       (block.open_battery_probe&&!execution.open_battery_probe_correct)||
       (block.adjustment_project_probe&&!execution.adjustment_project_probe_correct)||
        (block.boolean_battery_probe&&!execution.boolean_battery_probe_correct)||
       (block.reduction_probe&&!execution.reduction_probe_correct)||
        (block.integer_battery_probe&&(!execution.integer_battery_probe_correct||
            (block.integer_battery_probe->prerequisite&&
             (captured.integer_battery_prerequisite.empty()||
              captured.integer_battery_prerequisite!=
                compiler_integer_battery_prerequisite(data,lesson,block,captured)))))||
       (block.cmake_graph_check&&(!execution.cmake_graph_correct||
            (block.cmake_graph_check->initial_observation&&
             (captured.cmake_initial_observation_prerequisite.empty()||
              captured.cmake_initial_observation_prerequisite!=
                compiler_cmake_initial_observation_prerequisite(data,lesson,block,captured))))))return false;
    const auto evaluation=evaluate_cpp_output_check(*block.output_check,execution,captured.revision);
    if(evaluation.verdict!=curriculum::OutputVerdict::correct)return false;
    // Validate the saved execution receipt before changing a previous
    // rejection. A malformed case vector, program or completion type must
    // not erase that review on the way to throwing an exception.
    const auto key=base(lesson,block),program=compiler_program_frame(captured);
    const bool previously_completed=stdout_or_diagnostic_completed(data,lesson,block);
    auto proofs=case_proofs(data,key);
    const auto stored_program=state::string_value(data,"native_windows",key+"program");
    if((block.indexed_unique_transfer_probe||block.defaulted_move_policy_probe||
        block.independent_copy_assignment_probe||block.move_assignment_invariant_probe)&&
       !previously_completed&&stored_program==program&&
       valid_completion_seal(block,key,program,proofs,
           state::string_value(data,"native_windows",key+"completed"))) {
        // Preserve old stdout-only receipts exactly and append just the
        // independently witnessed objective; no historical auto-promotion.
        if(block.indexed_unique_transfer_probe)
            data.preferences["native_windows"][key+"indexed-unique-transfer/v1"]=
                indexed_transfer_completion_seal(key,program,
                    *block.indexed_unique_transfer_probe);
        if(block.defaulted_move_policy_probe)
            data.preferences["native_windows"][key+"defaulted-move-policy/v1"]=
                defaulted_move_completion_seal(key,program,
                    *block.defaulted_move_policy_probe);
        if(block.independent_copy_assignment_probe)
            data.preferences["native_windows"][key+"independent-copy-assignment/v1"]=
                independent_copy_assignment_completion_seal(key,program,
                    *block.independent_copy_assignment_probe);
        if(block.move_assignment_invariant_probe)
            data.preferences["native_windows"][key+"move-invariant/v1"]=
                move_assignment_invariant_completion_seal(key,program,
                    *block.move_assignment_invariant_probe);
        clear_compiler_puzzle_review(data,lesson,block);
        return true;
    }
    // All-case V1 achievements and achievements for another program are
    // immutable. Only a new first-case achievement of this same program may
    // collect optional extra case receipts afterwards.
    const bool first_only=previously_completed&&stored_program==program&&
        block.output_check->cases.size()>1&&
        state::string_value(data,"native_windows",key+"completed")==
            first_case_complete_seal(key,program,proofs);
    if(previously_completed&&!first_only){
        // A corrupted feedback field must be rejected before the old review
        // is removed. Rechecking an earned objective never rewrites its seal.
        const auto old_feedback=state::string_value(data,"native_windows",key+"feedback");
        const auto old_seal=state::string_value(data,"native_windows",key+"completed");
        clear_compiler_puzzle_review(data,lesson,block);
        record_feedback_origin(data,key,block,evaluation.feedback_path,old_feedback,old_seal);
        return true;
    }
    if(stored_program!=program)proofs.clear();
    proofs.resize(block.output_check->cases.size());
    for(std::size_t index=0;index<block.output_check->cases.size();++index) {
        const auto& item=block.output_check->cases[index];
        const bool input=item.balance_three_pairs
            ?curriculum::balance_three_pair_variation(execution.standard_input).has_value()
            :block.output_check->input_comparison==curriculum::TextComparison::tokens
                ?tokens(item.standard_input)==tokens(execution.standard_input)
                :item.standard_input==execution.standard_input;
        if(input&&(!item.executable||item.executable==execution.executable))proofs[index]=proof(key,program,index,item);
    }
    const bool all=std::ranges::all_of(proofs,[](const auto& item){return !item.empty();});
    const bool first=!proofs.empty()&&!proofs.front().empty();
    // All case comparisons and receipt reads have now succeeded. Only then
    // replace the latest rejected-attempt feedback with the successful run.
    clear_compiler_puzzle_review(data,lesson,block);
    auto& values=data.preferences["native_windows"];values[key+"program"]=program;values[key+"cases"]=proofs;
    if(first_only)return true; // Optional cases do not rewrite an earned seal or its feedback.
    if(all||first){
        values[key+"completed"]=all?complete_seal(key,program,proofs):
            first_case_complete_seal(key,program,proofs);
        if(block.indexed_unique_transfer_probe)
            values[key+"indexed-unique-transfer/v1"]=
                indexed_transfer_completion_seal(key,program,
                    *block.indexed_unique_transfer_probe);
        if(block.defaulted_move_policy_probe)
            values[key+"defaulted-move-policy/v1"]=
                defaulted_move_completion_seal(key,program,
                    *block.defaulted_move_policy_probe);
        if(block.independent_copy_assignment_probe)
            values[key+"independent-copy-assignment/v1"]=
                independent_copy_assignment_completion_seal(key,program,
                    *block.independent_copy_assignment_probe);
        if(block.move_assignment_invariant_probe)
            values[key+"move-invariant/v1"]=
                move_assignment_invariant_completion_seal(key,program,
                    *block.move_assignment_invariant_probe);
        values[key+"feedback"]=std::string(feedback);
        record_feedback_origin(data,key,block,evaluation.feedback_path,feedback,
            std::get<std::string>(values.at(key+"completed")));
    }
    return all||first;
}

std::string compiler_integer_battery_prerequisite(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& current) {
    if(!block.integer_battery_probe||!block.integer_battery_probe->prerequisite||
       !integer_battery_project_matches(block,current)||
       current.cmake->target!=block.integer_battery_probe->test_target)return {};
    const auto receipt=integer_client_receipt(data,lesson,block);
    const auto component=cpp_integer_battery_component_fingerprint(current);
    return receipt&&!component.empty()&&(*receipt)[0]==component?(*receipt)[2]:std::string{};
}
bool record_compiler_integer_battery_client(state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const curriculum::FreshExecution& execution) {
    if(!block.integer_battery_probe||!block.integer_battery_probe->prerequisite||
       !integer_battery_project_matches(block,captured)||
       captured.cmake->target!=block.integer_battery_probe->prerequisite->target||
       !matches_runtime_capture(captured,execution)||
       execution.source_revision!=captured.revision||!execution.compilation_succeeded||
       !execution.execution_completed||!execution.execution_succeeded||
       !execution.integer_battery_client_correct||
       execution.integer_battery_exit_code!=
           std::optional<std::uint32_t>{block.integer_battery_probe->prerequisite->exit_code})
        return false;
    const auto component=cpp_integer_battery_component_fingerprint(captured);
    if(component.empty())return false;
    const auto key=integer_client_key(lesson,block);
    if(const auto prior=integer_client_receipt(data,lesson,block);
       prior&&(*prior)[0]==component)return true; // Preserve an already captured token.
    // A malformed saved receipt is preserved for state-store validation.
    if(state::find(data,"native_windows",key)&&!integer_client_receipt(data,lesson,block))
        return false;
    const auto& capture=execution.integer_battery_request_fingerprint;
    data.preferences["native_windows"][key]=std::vector<std::string>{
        component,capture,integer_client_seal(key,component,capture)};
    return true;
}

std::string compiler_cmake_initial_observation_prerequisite(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& current) {
    if(!block.cmake_graph_check||!block.cmake_graph_check->initial_observation||
       !compiler_runtime_request_allowed(block,current))return {};
    const auto component=cpp_cmake_initial_observation_component_fingerprint(current);
    if(component.empty())return {};
    const auto key=cmake_initial_prefix(lesson,block,false)+component;
    const auto receipt=cmake_initial_receipt(data,key);
    if(!receipt||(*receipt)[0]!=component||
       block.cmake_graph_check->initial_observation->warning.has_value()==(*receipt)[3].empty())
        return {};
    return (*receipt)[4];
}
bool record_compiler_cmake_initial_observation(state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const CppRunResult& result) {
    if(!block.cmake_graph_check||!block.cmake_graph_check->initial_observation||
       !compiler_runtime_request_allowed(block,captured))return false;
    const auto observation=capture_cpp_cmake_initial_observation(captured,result);
    if(!observation||observation->request_fingerprint.empty()||
       observation->request_fingerprint!=cpp_cmake_initial_observation_request_fingerprint(captured)||
       observation->component_fingerprint.empty()||observation->artifact_fingerprint.empty())return false;
    const auto& component=observation->component_fingerprint;
    const auto& artifact=observation->artifact_fingerprint;
    const auto build_key=cmake_initial_prefix(lesson,block,true)+component+'/'+artifact;
    const auto run_key=cmake_initial_prefix(lesson,block,false)+component;
    const auto prior_build=cmake_initial_receipt(data,build_key);
    const auto prior_run=cmake_initial_receipt(data,run_key);
    // Preserve malformed stored data for validation; no partial write can
    // silently replace it with an unrelated successful observation.
    if((state::find(data,"native_windows",build_key)&&!prior_build)||
       (state::find(data,"native_windows",run_key)&&!prior_run))return false;
    const auto make_receipt=[&](const std::string& key,std::string prerequisite) {
        std::vector<std::string> fields{component,artifact,observation->request_fingerprint,
            std::move(prerequisite),{}};
        fields[4]=cmake_initial_seal(key,fields);
        return fields;
    };
    const bool needs_warning=block.cmake_graph_check->initial_observation->warning.has_value();
    const bool new_warning=needs_warning&&observation->warning_observed;
    auto build_receipt=prior_build?*prior_build:make_receipt(build_key,{});
    const bool failure=observation->failure_observed&&
        (!needs_warning||new_warning||prior_build);
    if(!new_warning&&!failure)return false;
    auto& values=data.preferences["native_windows"];
    if(new_warning&&!prior_build)values[build_key]=build_receipt;
    if(failure&&!prior_run)values[run_key]=make_receipt(run_key,
        needs_warning?build_receipt[4]:std::string{});
    return true;
}

bool record_compiler_selective_history_completion(state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& request,const curriculum::FreshExecution& execution,
        const CmakeSelectiveRecoveryTrace& trace) {
    if(!selective_history_owner(lesson,block) || !trace.completed_by(request) ||
        !stdout_or_diagnostic_completed(data,lesson,block) ||
        !matches_runtime_capture(request,execution) ||
        evaluate_cpp_output_check(*block.output_check,execution,request.revision).verdict
            !=curriculum::OutputVerdict::correct)
        return false;
    const auto key=selective_history_key(lesson,block);
    const auto program=compiler_program_frame(request);
    data.preferences["native_windows"][key]=std::vector<std::string>{
        program,selective_history_seal(key,program)};
    return compiler_puzzle_completed(data,lesson,block);
}
bool record_compiler_diagnostic_case(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const CppRunResult& result,std::string_view feedback) {
    if(!block.compile_check||!captured.diagnostic||!expected_compile_diagnostic(captured,result))return false;
    if(*captured.diagnostic!=prepare_cpp_diagnostic_request(block,captured))return false;
    const auto key=base(lesson,block),program=compiler_program_frame(captured);
    // The same ordering applies to intentional compiler diagnostics. Read
    // all saved receipts before clearing a later rejected-attempt message.
    const bool previously_completed=compiler_puzzle_completed(data,lesson,block);
    std::string old_feedback,old_seal;
    if(previously_completed){
        old_feedback=state::string_value(data,"native_windows",key+"feedback");
        old_seal=state::string_value(data,"native_windows",key+"completed");
    }
    const std::vector<std::string> proofs{diagnostic_proof(key,program,block)};
    clear_compiler_puzzle_review(data,lesson,block);
    if(previously_completed){
        record_feedback_origin(data,key,block,"/compileCheck/explanation",old_feedback,old_seal);
        return true;
    }
    auto& values=data.preferences["native_windows"];
    values[key+"program"]=program;values[key+"cases"]=proofs;
    values[key+"completed"]=complete_seal(key,program,proofs);values[key+"feedback"]=std::string(feedback);
    record_feedback_origin(data,key,block,"/compileCheck/explanation",feedback,std::get<std::string>(values.at(key+"completed")));
    return true;
}
void reset_compiler_puzzle(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto prefix=root(lesson,block);
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
}
void invalidate_compiler_puzzle_attempt(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
                                      const CppRunRequest& current) {
    if(!curriculum::has_compiler_objective(block))return;
    // A rejected attempt is specific to stdin and target as well as source.
    // Partial success deliberately remains reusable across authored inputs.
    // An invalid saved receipt cannot cost the student the last feedback.
    // Decide both effects before touching any state, including the review.
    const bool has_current_review=!compiler_puzzle_review(data,lesson,block,current).empty();
    const bool drop_unearned=!stdout_or_diagnostic_completed(data,lesson,block)
        &&state::string_value(data,"native_windows",base(lesson,block)+"program")!=compiler_program_frame(current);
    if(!has_current_review)clear_compiler_puzzle_review(data,lesson,block);
    if(drop_unearned) {
        // Editing revokes only an incomplete attempt of this exact objective.
        // Older objectives remain historical data when an authored requirement
        // grows (for example, stdout plus a runtime stderr diagnostic).
        const auto prefix=base(lesson,block),review_prefix=prefix+"review/";
        if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix)&&!item.first.starts_with(review_prefix);});
    }
}
} // namespace lgaed::windows


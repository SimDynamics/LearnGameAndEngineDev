#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_history_training.hpp"
#include "platform/windows/state_store.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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
    for(const auto& item:check.cases){
        frame+=token(item.standard_input)+token(item.executable.value_or(""));
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
    original.raw_json=block.compiler_revision->raw_json;original.compiler_revision.reset();return original;
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
bool same_case(const curriculum::OutputCase& left,const curriculum::OutputCase& right) {
    return left.standard_input==right.standard_input&&left.outputs==right.outputs&&left.executable==right.executable&&
        left.standard_error==right.standard_error&&left.exit_code==right.exit_code;
}
void validate_receipts(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return;const auto key=base(lesson,block);
    for(const auto suffix:{"program","completed","feedback"})if(const auto value=state::find(data,"native_windows",key+suffix);value&&!std::holds_alternative<std::string>(*value))
        throw std::runtime_error("Invalid compiler execution provenance; data was preserved.");
    const auto program=state::string_value(data,"native_windows",key+"program");const auto proofs=case_proofs(data,key);
    if(!proofs.empty()&&(program.empty()||proofs.size()!=(block.compile_check?1:block.output_check->cases.size())))
        throw std::runtime_error("Invalid compiler case count or captured program; data was preserved.");
    for(std::size_t index=0;index<proofs.size();++index)if(!proofs[index].empty()&&proofs[index]!=(block.compile_check?diagnostic_proof(key,program,block):proof(key,program,index,block.output_check->cases[index])))
        throw std::runtime_error("A compiler case proof belongs to another program or objective; data was preserved.");
    if(const auto value=state::find(data,"native_windows",key+"completed")) {
        if(proofs.empty()||std::ranges::any_of(proofs,[](const auto& value){return value.empty();})||
            std::get<std::string>(*value)!=complete_seal(key,program,proofs))
            throw std::runtime_error("Invalid compiler completion proof; data was preserved.");
    }
}
}
bool compiler_selective_history_required(const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return selective_history_owner(lesson,block);
}
std::string compiler_program_frame(const CppRunRequest& request) {
    if(request.project_files.empty())return "single-file-v1/"+token(request.source_utf8);
    auto configuration=request.cmake;
    // Cases may select different executables of one unchanged CMake project.
    // Target identity is checked separately in each authored case proof.
    if(configuration)configuration->target.clear();
    return "project-v1/"+curriculum::cpp_project_frame(request.project_files,request.source_files,configuration);
}
static bool stdout_or_diagnostic_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(!curriculum::has_compiler_objective(block))return false;
    const auto key=base(lesson,block);
    const auto captured=state::string_value(data,"native_windows",key+"program");
    const auto proofs=case_proofs(data,key);
    if(captured.empty()||proofs.size()!=(block.compile_check?1:block.output_check->cases.size())||proofs.empty())return false;
    for(std::size_t index=0;index<proofs.size();++index)
        if(proofs[index]!=(block.compile_check?diagnostic_proof(key,captured,block):proof(key,captured,index,block.output_check->cases[index])))return false;
    return state::string_value(data,"native_windows",key+"completed")==complete_seal(key,captured,proofs);
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
    // This authenticated text-only review retains the very same successful
    // execution objective. Present its new explanation without rewriting the
    // original receipt, running another program or inventing listening credit.
    if(!stored.empty()&&lesson.locale=="es"
        &&lesson.id=="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla"
        &&block.stable_id=="block_0002_0003"&&block.output_check&&block.output_check->cases.size()==1
        &&std::ranges::any_of(lesson.state_layout.reading_revisions,[](const auto& revision){
            return revision.editorial_prefix==15&&!revision.positional_page_identity&&revision.previous_blocks.size()==2
                &&revision.previous_blocks.contains("block_0002_0003")&&revision.previous_blocks.contains("block_0004_0001");
        })){
        const auto& explanation=block.output_check->cases.front().explanation;
        if(!explanation.empty())return stored.substr(0,stored.find_first_of("\r\n"))+'\n'+explanation;
    }
    return stored;
}
std::string compiler_puzzle_review(const state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CppRunRequest& current) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!curriculum::has_compiler_objective(block))return {};
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
std::optional<CompilerFeedback> compiler_puzzle_current_feedback(const state::StateData& data,
        const curriculum::Lesson& lesson,const curriculum::Block& block,const CppRunRequest& current) {
    if(!curriculum::has_compiler_objective(block))return std::nullopt;
    const auto key=base(lesson,block);
    if(auto review=compiler_puzzle_review(data,lesson,block,current);!review.empty()){
        auto path=restored_feedback_origin(data,key+"review/",block,review,state::string_value(data,"native_windows",key+"review/seal"));
        return CompilerFeedback{std::move(review),false,std::move(path)};
    }
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
bool record_compiler_puzzle_review(state::StateData& data,const curriculum::Lesson& lesson,
        const curriculum::Block& block,const CppRunRequest& captured,const curriculum::FreshExecution& execution,
        std::string_view feedback) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check||feedback.empty()||!matches_runtime_capture(captured,execution))return false;
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
    return block.compiler_revision?compiler_puzzle_case_count(data,lesson,historical_block(block)):0;
}
bool compiler_puzzle_historical_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return block.compiler_revision&&compiler_puzzle_completed(data,lesson,historical_block(block));
}
std::string compiler_puzzle_historical_feedback(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return block.compiler_revision?compiler_puzzle_feedback(data,lesson,historical_block(block)):std::string{};
}
void validate_compiler_puzzle_state(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    // Never erase a malformed additional receipt. The original stdout proof
    // stays intact; the new record must be independently authenticated.
    if(selective_history_owner(lesson,block)&&
        state::find(data,"native_windows",selective_history_key(lesson,block))&&
        !selective_history_receipt_valid(data,lesson,block))
        throw std::runtime_error("Invalid selective recovery provenance; historical completion was preserved.");
    // Existing objectives retain their historical permissive decoding. The
    // reviewed migration validates every receipt it is about to reuse.
    if(block.compile_check){validate_receipts(data,lesson,block);return;}
    if(!block.compiler_revision)return;
    validate_receipts(data,lesson,historical_block(block));validate_receipts(data,lesson,block);
}
void prepare_compiler_puzzle_revisions(state::StateData& data,const curriculum::Lesson& lesson) {
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)if(block.compiler_revision) {
        validate_compiler_puzzle_state(data,lesson,block);
        const auto key=base(lesson,block),marker=root(lesson,block)+"revision-adopted/"+state::sha256(curriculum::reference_objective_frame(lesson,block));
        if(const auto adopted=state::find(data,"native_windows",marker)) {
            if(!std::holds_alternative<bool>(*adopted)||!std::get<bool>(*adopted))throw std::runtime_error("Invalid compiler expansion adoption marker; data was preserved.");
            continue;
        }
        const auto original=historical_block(block);const auto old_key=base(lesson,original);
        const auto program=state::string_value(data,"native_windows",old_key+"program");
        if(!program.empty()&&!state::find(data,"native_windows",key+"program")) {
            const auto old_proofs=case_proofs(data,old_key);std::vector<std::string> proofs(block.output_check->cases.size());
            if(original.output_check->input_comparison==block.output_check->input_comparison)
                for(std::size_t index=0;index<block.output_check->cases.size();++index)
                    for(std::size_t prior=0;prior<old_proofs.size();++prior)
                        if(!old_proofs[prior].empty()&&same_case(original.output_check->cases[prior],block.output_check->cases[index])) {
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
bool record_compiler_puzzle_case(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const curriculum::FreshExecution& execution,std::string_view feedback) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check||!matches_runtime_capture(captured,execution))return false;
    const auto evaluation=evaluate_cpp_output_check(*block.output_check,execution,captured.revision);
    if(evaluation.verdict!=curriculum::OutputVerdict::correct)return false;
    clear_compiler_puzzle_review(data,lesson,block);
    const auto key=base(lesson,block),program=compiler_program_frame(captured);
    if(stdout_or_diagnostic_completed(data,lesson,block)){
        record_feedback_origin(data,key,block,evaluation.feedback_path,state::string_value(data,"native_windows",key+"feedback"),state::string_value(data,"native_windows",key+"completed"));
        return true;
    }
    auto proofs=case_proofs(data,key);
    if(state::string_value(data,"native_windows",key+"program")!=program)proofs.clear();
    proofs.resize(block.output_check->cases.size());
    for(std::size_t index=0;index<block.output_check->cases.size();++index) {
        const auto& item=block.output_check->cases[index];
        const bool input=block.output_check->input_comparison==curriculum::TextComparison::tokens
            ?tokens(item.standard_input)==tokens(execution.standard_input):item.standard_input==execution.standard_input;
        if(input&&(!item.executable||item.executable==execution.executable))proofs[index]=proof(key,program,index,item);
    }
    auto& values=data.preferences["native_windows"];values[key+"program"]=program;values[key+"cases"]=proofs;
    const bool complete=std::ranges::all_of(proofs,[](const auto& item){return !item.empty();});
    if(complete){values[key+"completed"]=complete_seal(key,program,proofs);values[key+"feedback"]=std::string(feedback);
        record_feedback_origin(data,key,block,evaluation.feedback_path,feedback,std::get<std::string>(values.at(key+"completed")));}
    return complete;
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
    clear_compiler_puzzle_review(data,lesson,block);
    const auto key=base(lesson,block),program=compiler_program_frame(captured);
    if(compiler_puzzle_completed(data,lesson,block)){
        record_feedback_origin(data,key,block,"/compileCheck/explanation",state::string_value(data,"native_windows",key+"feedback"),state::string_value(data,"native_windows",key+"completed"));
        return true;
    }
    const std::vector<std::string> proofs{diagnostic_proof(key,program,block)};
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
    if(compiler_puzzle_review(data,lesson,block,current).empty())clear_compiler_puzzle_review(data,lesson,block);
    if(curriculum::has_compiler_objective(block)&&!stdout_or_diagnostic_completed(data,lesson,block)
        &&state::string_value(data,"native_windows",base(lesson,block)+"program")!=compiler_program_frame(current)) {
        // Editing revokes only an incomplete attempt of this exact objective.
        // Older objectives remain historical data when an authored requirement
        // grows (for example, stdout plus a runtime stderr diagnostic).
        const auto prefix=base(lesson,block),review_prefix=prefix+"review/";
        if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix)&&!item.first.starts_with(review_prefix);});
    }
}
} // namespace lgaed::windows

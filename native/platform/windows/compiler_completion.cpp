#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/state_store.hpp"

#include <algorithm>
#include <cctype>
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
}
std::string compiler_program_frame(const CppRunRequest& request) {
    if(request.project_files.empty())return "single-file-v1/"+token(request.source_utf8);
    auto configuration=request.cmake;
    // Cases may select different executables of one unchanged CMake project.
    // Target identity is checked separately in each authored case proof.
    if(configuration)configuration->target.clear();
    return "project-v1/"+curriculum::cpp_project_frame(request.project_files,request.source_files,configuration);
}
bool compiler_puzzle_completed(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check)return false;
    const auto key=base(lesson,block);
    const auto captured=state::string_value(data,"native_windows",key+"program");
    const auto proofs=case_proofs(data,key);
    if(captured.empty()||proofs.size()!=block.output_check->cases.size()||proofs.empty())return false;
    for(std::size_t index=0;index<proofs.size();++index)
        if(proofs[index]!=proof(key,captured,index,block.output_check->cases[index]))return false;
    return state::string_value(data,"native_windows",key+"completed")==complete_seal(key,captured,proofs);
}
std::string compiler_puzzle_feedback(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    return compiler_puzzle_completed(data,lesson,block)?state::string_value(data,"native_windows",base(lesson,block)+"feedback"):std::string{};
}
std::size_t compiler_puzzle_case_count(const state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check)return 0;
    const auto key=base(lesson,block),captured=state::string_value(data,"native_windows",key+"program");
    const auto proofs=case_proofs(data,key);std::size_t count=0;
    for(std::size_t index=0;index<std::min(proofs.size(),block.output_check->cases.size());++index)
        if(proofs[index]==proof(key,captured,index,block.output_check->cases[index]))++count;
    return count;
}
bool record_compiler_puzzle_case(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
        const CppRunRequest& captured,const curriculum::FreshExecution& execution,std::string_view feedback) {
    if(compiler_puzzle_completed(data,lesson,block))return true;
    if(block.type!=curriculum::BlockType::cpp_compiler||!block.output_check||evaluate_cpp_output_check(*block.output_check,execution,captured.revision).verdict!=curriculum::OutputVerdict::correct)return false;
    const auto key=base(lesson,block),program=compiler_program_frame(captured);
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
    if(complete){values[key+"completed"]=complete_seal(key,program,proofs);values[key+"feedback"]=std::string(feedback);}
    return complete;
}
void reset_compiler_puzzle(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block) {
    const auto prefix=root(lesson,block);
    if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
        std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
}
void invalidate_compiler_puzzle_attempt(state::StateData& data,const curriculum::Lesson& lesson,const curriculum::Block& block,
                                      const CppRunRequest& current) {
    if(block.output_check&&!compiler_puzzle_completed(data,lesson,block)
        &&state::string_value(data,"native_windows",base(lesson,block)+"program")!=compiler_program_frame(current)) {
        // Editing revokes only an incomplete attempt of this exact objective.
        // Older objectives remain historical data when an authored requirement
        // grows (for example, stdout plus a runtime stderr diagnostic).
        const auto prefix=base(lesson,block);
        if(const auto domain=data.preferences.find("native_windows");domain!=data.preferences.end())
            std::erase_if(domain->second,[&](const auto& item){return item.first.starts_with(prefix);});
    }
}
} // namespace lgaed::windows

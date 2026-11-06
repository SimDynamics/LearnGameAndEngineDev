#include "core/u6_diagnostic_test_fixture.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace lgaed;using namespace lgaed::windows;using namespace lgaed::curriculum;
using namespace std::chrono_literals;
void require(bool value,std::string_view message){if(!value)throw std::runtime_error(std::string(message));}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);require(bool(input),"Missing captured U6 artifact");return {std::istreambuf_iterator<char>(input),{}};}
const Block& editor(const Lesson& lesson,std::string_view identity){const auto at=stable_block_position(lesson,identity);require(at.has_value(),"Missing diagnostic owner");return lesson.pages[at->page].blocks[at->block];}
void no_execution(const CppRunResult& result){require(result.execution==CppPhaseResult{}&&result.configuration==CppPhaseResult{}&&
    result.artifact.empty()&&!result.executable&&result.target_type.empty(),"A diagnostic-only project was exposed or executed");}
std::string without_code(std::string_view stream,std::string_view code){
    std::string result;
    while(!stream.empty()){
        const auto end=stream.find('\n');const auto length=end==stream.npos?stream.size():end+1;
        const auto line=stream.substr(0,length);
        if(line.find(std::string(" ")+std::string(code)+':')==line.npos)result+=line;
        stream.remove_prefix(length);
    }
    return result;
}
}
int main(int argc,char** argv) try {
    const std::string filter=argc>1?argv[1]:"all";
    const LaunchOptions options{ProfileId::Codex,L"u6-diagnostics-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);initialize_profile_directories(environment.paths);
    state::StateStore store(environment.paths.root);(void)store.load();std::size_t passed=0,failed=0;std::uint64_t sequence=0;
    for(const auto& spec:test::u6_diagnostic_cases){
        if(filter!="all"&&filter!=(spec.link?"link":"compile")&&filter!=spec.diagnostic)continue;
        try {
            const auto before=test::load_pre_u6_diagnostic_lesson(spec.file);const auto lesson=test::with_u6_diagnostic(before,spec);
            const auto metadata=parse_lesson_progress_metadata(lesson.raw_json);const auto& block=editor(lesson,spec.identity);
            const auto& old=editor(before,spec.identity);const auto& projected=editor(metadata,spec.identity);
            require(block.initial_code==old.initial_code&&block.project_files==old.project_files&&block.source_files==old.source_files&&
                exercise_key(lesson,block)==exercise_key(before,old)&&draft_key("programming",lesson,block)==draft_key("programming",before,old),
                "Diagnostic adoption changed sources or saved ownership");
            state::StateData data;state::prepare_lesson_state(data,before);
            state::put_response(data,before,old,state::Response{"opaque pre-adoption flag",true});
            state::Draft draft;draft.source=block.initial_code;draft.selection_start=2;draft.selection_end=5;
            state::put_draft(data,draft_key("programming",lesson,block),draft);
            data.preferences["u6-qa-unrelated"]["retained"]=std::string(" exact\r\n保存 ");
            data.preferences["native_windows"]["u6-qa/unrelated"]=std::string("configuration preserved");
            const auto preserved=data;state::prepare_lesson_state(data,lesson);
            using namespace winrt::Windows::Data::Json;
            JsonArray rows,row;row.Append(JsonValue::CreateStringValue(winrt::to_hstring(spec.identity)));
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(spec.diagnostic)));
            row.Append(JsonValue::CreateStringValue(winrt::to_hstring(state::sha256(block.initial_code))));rows.Append(row);
            auto expected=preserved;
            expected.preferences["learning_progress"]["lesson_"+lesson.id+"_state_compile_checks_v1_es"]=winrt::to_string(rows.Stringify());
            require(data==expected&&!compiler_puzzle_completed(data,lesson,block),"Adoption changed prior data, omitted its anti-removal pin, or granted an unexecuted goal");
            const auto adopted=data;
            CppRunRequest request;request.request_id=++sequence;request.revision=100+sequence;
            request.source_utf8=block.initial_code;request.project_files=block.project_files;request.source_files=block.source_files;
            request.projects_directory=environment.paths.projects/L"Código & (diagnóstico U6)";
            request.compile_timeout=45s;request.execution_timeout=2s;request.diagnostic=prepare_cpp_diagnostic_request(block,request);
            require(request.diagnostic->project&&request.diagnostic->project->link==spec.link&&!request.diagnostic->project->error_codes.empty(),
                "A prepared U6 editor did not produce its exact phase/source objective");
            const auto result=run_cpp(request);no_execution(result);
            std::cout<<"CASE "<<spec.diagnostic<<" control="<<(result.diagnostic&&result.diagnostic->control.succeeded())<<'\n';
            require(result.error_utf8.empty()&&expected_compile_diagnostic(request,result),
                "Expected U6 rejection was not authenticated: "+result.error_utf8+result.compilation.stdout_bytes+result.compilation.stderr_bytes);
            for(const auto& [path,text]:request.project_files)require(read(result.run_directory/L"candidate/project"/path)==text,"The adapter rewrote candidate files");
            for(const auto& [path,text]:request.diagnostic->project->control_files)require(read(result.run_directory/L"control/project"/path)==text,"The adapter rewrote the minimum control");
            require(result.diagnostic->control_units.size()==request.diagnostic->project->control_sources.size(),"A fresh control translation unit was omitted");
            if(spec.link)require(result.diagnostic->candidate_units.size()==request.source_files.size()&&
                result.diagnostic->control_link.succeeded()&&result.diagnostic->candidate_link.status==CppPhaseStatus::completed,
                "A link rejection borrowed an unsuccessful compilation phase");
            else require(result.diagnostic->control_link==CppPhaseResult{}&&result.diagnostic->candidate_link==CppPhaseResult{},
                "A compilation-only objective reached the linker");
            // Reparse raw phase evidence: no old request, summary boolean,
            // altered project, missing object phase or forged error grants credit.
            auto changed=result;changed.diagnostic->expected_rejection=false;
            require(expected_compile_diagnostic(request,changed),"A summary boolean overrode captured evidence");
            changed=result;++changed.revision;require(!expected_compile_diagnostic(request,changed),"A stale capture granted credit");
            changed=result;changed.diagnostic->control_units.front().phase.exit_code=1;
            require(!expected_compile_diagnostic(request,changed),"A failed control translation unit granted credit");
            changed=result;changed.diagnostic->candidate_units.front().source="other.cpp";
            require(!expected_compile_diagnostic(request,changed),"An unrelated translation unit borrowed the rejection");
            changed=result;changed.compilation.status=CppPhaseStatus::output_limit;
            require(!expected_compile_diagnostic(request,changed),"Truncated technical output granted credit");
            if(std::ranges::find(request.diagnostic->project->error_codes,"C2593")!=request.diagnostic->project->error_codes.end()){
                changed=result;
                auto& last=changed.diagnostic->candidate_units.back().phase;
                last.stdout_bytes=without_code(last.stdout_bytes,"C3861");last.stderr_bytes=without_code(last.stderr_bytes,"C3861");
                changed.compilation.stdout_bytes=without_code(changed.compilation.stdout_bytes,"C3861");
                changed.compilation.stderr_bytes=without_code(changed.compilation.stderr_bytes,"C3861");
                std::erase_if(changed.diagnostic->errors,[](const auto& error){return error.code=="C3861";});
                require(!expected_compile_diagnostic(request,changed),"An ambiguous stream insertion alone replaced the required unknown-name diagnostic");
            }
            auto another=request;another.project_files.front().second+="\n// changed captured bytes";
            require(!expected_compile_diagnostic(another,result),"Different project bytes borrowed old diagnostics");
            another=request;another.diagnostic->project->link=!spec.link;
            require(!expected_compile_diagnostic(another,result),"A different phase borrowed the rejection");
            auto unearned=data;
            require(!record_compiler_diagnostic_case(unearned,lesson,block,another,result,"false")&&unearned==data,
                "A changed diagnostic plan manufactured an achievement");
            require(record_compiler_diagnostic_case(data,lesson,block,request,result,"Correcto\nDiagnóstico comprobado.")&&
                compiler_puzzle_completed(data,lesson,block)&&compiler_puzzle_completed(data,metadata,projected),
                "The fresh correct diagnostic could not earn its own lesson/catalog proof");
            for(const auto& [domain,values]:adopted.preferences)for(const auto& [key,value]:values)
                require(state::find(data,domain,key)&&*state::find(data,domain,key)==value,"Grading altered a draft, answer or unrelated preference");
            store.save(data);state::StateStore reopen(store.root().parent_path());
            require(reopen.root()==store.root()&&reopen.load()==data&&compiler_puzzle_completed(reopen.load(),lesson,block),"Diagnostic state did not reopen in its exact profile");
            auto restored=reopen.load();reset_compiler_puzzle(restored,lesson,block);
            require(!compiler_puzzle_completed(restored,lesson,block)&&restored==adopted,"Explicit diagnostic reset changed historical answers, drafts or the anti-removal pin");
            // Adding one other authored file to the model changes objective
            // ownership independently of the active draft and capture.
            auto other_lesson=lesson;auto at=stable_block_position(other_lesson,spec.identity);
            auto& other=other_lesson.pages[at->page].blocks[at->block];other.project_files.front().second+=' ';
            require(reference_objective_frame(other_lesson,other)!=reference_objective_frame(lesson,block),"A changed authored project retained the old objective identity");
            ++passed;std::cout<<"PASS "<<spec.diagnostic<<"; no execution; exact reopen/reset\n";
        }catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<spec.diagnostic<<": "<<error.what()<<'\n';}
    }
    require(passed+failed>0,"Unknown U6 diagnostic selection");
    std::cout<<"U6 closed diagnostic projects: "<<passed<<" passed, "<<failed<<" failed; no GUI/audio.\n";return failed?1:0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace std::chrono_literals;

void require(bool value,std::string_view reason) {
    if(!value)throw std::runtime_error(std::string(reason));
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    require(input.is_open(),"Missing authenticated diagnostic fixture");
    return {std::istreambuf_iterator<char>{input},std::istreambuf_iterator<char>{}};
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const Block* result=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==identity) {
            require(!result,"Ambiguous diagnostic owner in the current lesson");
            result=&block;
        }
    require(result!=nullptr,"Missing stable diagnostic owner");
    return *result;
}
const SourceFormatRevision* source_archive(const Lesson& lesson,std::string_view identity) {
    const auto& entries=lesson.state_layout.source_format_revisions;
    const auto found=std::ranges::find(entries,identity,&SourceFormatRevision::id);
    return found==entries.end()?nullptr:&*found;
}
void preserve_format_history(const Lesson& before,const Lesson& after) {
    using namespace winrt::Windows::Data::Json;
    const auto& original=before.state_layout.source_format_revisions;
    const auto& current=after.state_layout.source_format_revisions;
    require(current.size()>=original.size(),"A diagnostic publication removed a source archive");
    for(std::size_t index=0;index<original.size();++index)
        require(current[index].id==original[index].id&&
                current[index].raw_json==original[index].raw_json&&
                current[index].sha256==original[index].sha256,
                "A diagnostic publication rewrote or reassigned a complete canonical archive");
    const auto prior=JsonObject::Parse(winrt::to_hstring(before.state_layout.raw_json)).GetNamedArray(L"editorial");
    const auto revised=JsonObject::Parse(winrt::to_hstring(after.state_layout.raw_json)).GetNamedArray(L"editorial");
    require(revised.Size()>=prior.Size(),"A diagnostic publication dropped a historical editorial edge");
    for(std::uint32_t index=0;index<prior.Size();++index)
        require(prior.GetAt(index).Stringify()==revised.GetAt(index).Stringify(),
                "A diagnostic publication changed its authenticated editorial prefix");
}
struct Statement { std::string_view diagnostic,source,error; };
constexpr std::array statements{
    Statement{"const-member-call","fija.cambiar_a(4,2);","C2662"},
    Statement{"explicit-copy-initialization","Contador no_permitido = 3;","C2440"},
    Statement{"explicit-argument-conversion","leer(3);","C2664"},
    Statement{"explicit-assignment","destino = 3;","C2679"},
    Statement{"swapped-named-arguments","const Caja mal{Alto{4}, Ancho{3}};","C2440"}
};

// Each receipt below comes from a real control/candidate MSVC invocation.
// Synthetic stale results are used only as negative tests and never saved.
state::StateData exercise(const Lesson& lesson,const Block& block,
        const WindowsEnvironment& environment,std::uint64_t sequence) {
    require(block.compile_check.has_value(),"Diagnostic goal is missing");
    const auto& name=block.compile_check->diagnostic;
    CppRunRequest request;
    request.request_id=sequence;
    request.revision=250+sequence;
    request.source_utf8=block.initial_code;
    request.project_files=block.project_files;
    request.source_files=block.source_files;
    request.projects_directory=environment.paths.projects/L"closed-format";
    request.compile_timeout=75s;
    request.execution_timeout=2s;
    const bool project=!block.project_files.empty();
    const bool link=project&&(name.find("unselected-definition")!=name.npos||
                             name.find("duplicate-definition")!=name.npos);
    if(!project) {
        const auto statement=std::ranges::find(statements,name,&Statement::diagnostic);
        require(statement!=statements.end(),"Unknown source-only diagnostic fixture");
        const auto insertion=request.source_utf8.rfind("    return 0;");
        require(insertion!=request.source_utf8.npos,"Diagnostic insertion point disappeared");
        request.source_utf8.insert(insertion,"    "+std::string(statement->source)+"\n");
        request.diagnostic=prepare_cpp_diagnostic_request(block,request);
        require(request.diagnostic&&!request.diagnostic->project&&
                request.diagnostic->expected_error_code==statement->error&&
                request.diagnostic->first_line>0&&
                request.diagnostic->last_line==request.diagnostic->first_line&&
                request.diagnostic->control_source_utf8.size()==request.source_utf8.size()&&
                request.diagnostic->control_source_utf8!=request.source_utf8,
                "Source-only goal lost its exact candidate, control or diagnostic line");
        auto missing=request;
        missing.source_utf8=block.initial_code;
        const auto denied=prepare_cpp_diagnostic_request(block,missing);
        require(!denied.project&&denied.expected_error_code.empty()&&
                denied.first_line==0&&denied.last_line==0,
                "The original source without the intended error retained accreditation fields");
    }else{
        request.diagnostic=prepare_cpp_diagnostic_request(block,request);
        require(request.diagnostic&&request.diagnostic->project&&
                request.diagnostic->project->link==link&&
                !request.diagnostic->project->error_codes.empty(),
                "Project goal lost its selected sources or compile/link phase");
        auto changed=request;
        changed.project_files.back().second+="\nint unrelated_change{99};\n";
        const auto denied=prepare_cpp_diagnostic_request(block,changed);
        require(denied.project&&denied.project->expected_file.empty()&&
                denied.project->first_line==0&&denied.project->last_line==0&&
                denied.project->error_codes.empty()&&denied.project->symbols.empty(),
                "An unrelated project source change retained accreditation fields");
    }
    const auto submitted_source=request.source_utf8;
    const auto submitted_files=request.project_files;
    const auto result=run_cpp(request);
    require(result.error_utf8.empty()&&result.diagnostic&&
            expected_compile_diagnostic(request,result)&&
            result.execution==CppPhaseResult{}&&result.configuration==CppPhaseResult{}&&
            result.artifact.empty()&&!result.executable&&
            request.source_utf8==submitted_source&&request.project_files==submitted_files,
            "MSVC did not verify the intended rejection without executing or rewriting the draft");
    if(link) {
        require(result.diagnostic->control_link.succeeded()&&
                result.diagnostic->candidate_link.status==CppPhaseStatus::completed&&
                !result.diagnostic->candidate_link.succeeded(),
                "The intended missing/duplicate definition was not a real link rejection");
    }else{
        require(result.diagnostic->control_link==CppPhaseResult{}&&
                result.diagnostic->candidate_link==CppPhaseResult{}&&
                !result.diagnostic->errors.empty(),
                "The expected source rejection was not verified at compilation");
        if(!project)require(result.diagnostic->control.succeeded(),
                "The source-only control did not compile");
    }
    state::StateData progress;
    state::prepare_lesson_state(progress,lesson);
    progress.preferences["unrelated_qa"]["draft"]=std::string("kept exactly\n\treturn 7;");
    const auto untouched=progress;
    auto stale=result;
    ++stale.revision;
    require(!record_compiler_diagnostic_case(progress,lesson,block,request,stale,"Incorrecto")&&
            progress==untouched&&!compiler_puzzle_completed(progress,lesson,block),
            "A result from another captured revision changed progress");
    require(record_compiler_diagnostic_case(progress,lesson,block,request,result,"Correcto")&&
            compiler_puzzle_completed(progress,lesson,block),
            "A real MSVC diagnosis did not earn its historical editor receipt");
    validate_compiler_puzzle_state(progress,lesson,block);
    auto reopened=state::decode(state::encode(progress));
    require(reopened==progress&&compiler_puzzle_completed(reopened,lesson,block),
            "Encoding and reopening changed a real diagnostic achievement");
    require(state::string_value(reopened,"unrelated_qa","draft")=="kept exactly\n\treturn 7;",
            "Diagnostic recording changed unrelated saved text");
    std::cout<<"PASS MSVC "<<name<<" phase="<<(link?"link":"compile")
             <<" source="<<(block.source_format_original_initial_code?"formatted":"original")
             <<" receipt=reopened\n";
    return reopened;
}
}

int main(int argc,char** argv)try {
    require(argc==2,"Supply the isolated diagnostic-format fixture directory");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const std::filesystem::path directory(argv[1]);
    using namespace winrt::Windows::Data::Json;
    const auto manifest=JsonObject::Parse(winrt::to_hstring(
        read(directory/"all-diagnostics.manifest.json")));
    require(manifest.GetNamedNumber(L"version")==1,
            "Unsupported diagnostic fixture manifest version");
    const auto lessons=manifest.GetNamedArray(L"lessons");
    require(lessons.Size()==6,"All six authentic U6/U9 lesson families are required");
    const LaunchOptions options{ProfileId::Codex,
        L"native-diagnostic-format-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    std::set<std::string> diagnostics;
    std::uint64_t sequence=1;
    for(const auto& entry:lessons) {
        const auto item=entry.GetObject();
        const auto file=[&](const wchar_t* field) {
            const std::filesystem::path name(item.GetNamedString(field).c_str());
            require(!name.empty()&&name.filename()==name&&!name.is_absolute(),
                    "A diagnostic fixture path must stay inside its QA directory");
            return directory/name;
        };
        const auto before=load_lesson(file(L"before"),"es");
        const auto after=load_lesson(file(L"after"),"es");
        const auto metadata=parse_lesson_progress_metadata(read(file(L"after")),"es");
        require(before.id==after.id&&after.id==metadata.id&&
                before.state_layout.signatures==after.state_layout.signatures&&
                after.state_layout.signatures==metadata.state_layout.signatures&&
                before.state_layout.lineage==after.state_layout.lineage,
                "Physical spacing changed the published lesson lineage");
        preserve_format_history(before,after);
        preserve_format_history(after,metadata);
        require(after.state_layout.source_format_revisions.size()==
                    metadata.state_layout.source_format_revisions.size(),
                "The progress-only loader changed the complete diagnostic archive census");
        auto expected_archives=before.state_layout.source_format_revisions.size();
        for(const auto& goal:item.GetNamedArray(L"owners")) {
            const auto declaration=goal.GetObject();
            const auto identity=winrt::to_string(declaration.GetNamedString(L"id"));
            const auto diagnostic=winrt::to_string(declaration.GetNamedString(L"diagnostic"));
            require(diagnostics.insert(diagnostic).second,"Duplicate diagnostic scenario");
            const auto& old=owner(before,identity);
            const auto& formatted=owner(after,identity);
            const auto& restored=owner(metadata,identity);
            const auto prior=source_archive(before,identity),current=source_archive(after,identity);
            require(current!=nullptr,"The formatted diagnostic has no complete canonical archive");
            if(!prior)++expected_archives;
            const auto& canonical=prior?prior->raw_json:old.raw_json;
            require(JsonObject::Parse(winrt::to_hstring(canonical)).Stringify()==
                        JsonObject::Parse(winrt::to_hstring(current->raw_json)).Stringify(),
                    "The formatted diagnostic replaced its complete original snapshot with a later presentation");
            require(old.compile_check&&formatted.compile_check&&restored.compile_check&&
                    old.compile_check->diagnostic==diagnostic&&
                    formatted.compile_check->diagnostic==diagnostic&&
                    restored.compile_check->diagnostic==diagnostic&&
                    old.source_files==formatted.source_files&&
                    old.address.page==formatted.address.page&&
                    old.address.block==formatted.address.block&&
                    formatted.address.page==restored.address.page&&
                    formatted.address.block==restored.address.block&&
                    old.initial_code!=formatted.initial_code&&
                    formatted.source_format_original_initial_code&&
                    *formatted.source_format_original_initial_code==
                        old.source_format_original_initial_code.value_or(old.initial_code)&&
                    formatted.source_format_original_initial_code==restored.source_format_original_initial_code&&
                    formatted.source_format_original_project_files==restored.source_format_original_project_files&&
                    reference_objective_frame(before,old)==reference_objective_frame(after,formatted)&&
                    reference_objective_frame(after,formatted)==reference_objective_frame(metadata,restored),
                    "The original diagnostic owner, source selection or archive changed");
            if(!old.project_files.empty())
                require(formatted.source_format_original_project_files&&
                        *formatted.source_format_original_project_files==
                            old.source_format_original_project_files.value_or(old.project_files),
                        "A repeated diagnostic format replaced its canonical multi-file project");
            auto legacy=exercise(before,old,environment,sequence++);
            const auto saved=legacy;
            state::prepare_lesson_state(legacy,after);
            require(compiler_puzzle_completed(legacy,after,formatted)&&
                    compiler_puzzle_completed(legacy,metadata,restored)&&
                    state::string_value(legacy,"unrelated_qa","draft")==
                    state::string_value(saved,"unrelated_qa","draft"),
                    "A real pre-format receipt did not survive the source-only review");
            validate_compiler_puzzle_state(legacy,metadata,restored);
            const auto fresh=exercise(after,formatted,environment,sequence++);
            require(compiler_puzzle_completed(fresh,metadata,restored),
                    "The progress-only loader lost a newly earned formatted receipt");
            validate_compiler_puzzle_state(fresh,metadata,restored);
        }
        require(after.state_layout.source_format_revisions.size()==expected_archives,
                "Diagnostic formatting added an unrelated source archive");
    }
    require(diagnostics.size()==17&&sequence==35,
            "Not all 17 closed goals ran in both before and after source-format publications");
    std::cout<<"PASS 34 real MSVC diagnostics; 17 historical and new receipts preserved\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL reviewed MSVC diagnostic format: "<<error.what()<<'\n';return 1;
}catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL reviewed diagnostic WinRT: "<<winrt::to_string(error.message())<<'\n';return 1;
}

// Complete published U1L4 -> format archive -> both Windows loaders.
// The code remains an illustrative failing example, with no curricular goal.
#include "core/code_format_compat.hpp"
#include "core/lesson.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
constexpr std::string_view owner_id="p1u1l4_codigo_sin_comilla";
constexpr std::string_view publication="e6281a98576dfe78a3072965cb498875c1ceb953269fc4c0c7e5ee396bf2b610";
constexpr std::string_view original_source="48bcc0f8d5b3f5c06cd5bad73030efa2beecba100a653a7da4553b2c0573bf75";
constexpr std::string_view formatted_source="af6544e511a22b6653c555a80dbe4ba0b63a7f1067c594cac11bb5d53d5b5248";

void check(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
std::string text(const JsonObject& value,const wchar_t* name) {
    return winrt::to_string(value.GetNamedString(name));
}
std::string read(const std::filesystem::path& path) {
    const auto bytes=state::read_bytes(path);return {bytes.begin(),bytes.end()};
}
std::filesystem::path inside(const std::filesystem::path& directory,std::string_view name) {
    const std::filesystem::path relative(name);
    check(!relative.empty()&&!relative.is_absolute(),"Fixture path must be relative");
    for(const auto& part:relative)check(part!=L"..","Fixture path leaves its isolated directory");
    return directory/relative;
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);
    check(position.has_value(),"The authenticated active owner is absent");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
void verify_history(const Lesson& before,const Lesson& after,
        const JsonObject& before_document,JsonObject after_document) {
    check(before.id==after.id&&before.state_layout.signatures==after.state_layout.signatures&&
            before.state_layout.lineage==after.state_layout.lineage&&
            before.state_layout.legacy_lineage==after.state_layout.legacy_lineage,
        "A source-format review changed lesson identity, signatures or lineage");
    const auto& old=owner(before,owner_id);const auto& current=owner(after,owner_id);
    check(old.type==BlockType::code&&current.type==BlockType::code&&
            old.language=="cpp"&&current.language=="cpp"&&
            state::sha256(old.code)==original_source&&state::sha256(current.code)==formatted_source&&
            code_format::equivalent(old.code,current.code,code_format::Language::cpp)&&
            !code_format::cpp_tokens(old.code)&&!code_format::cpp_tokens(current.code),
        "The published incomplete literal or the strict semantic lexer changed");
    const auto quote=old.code.find('"'),new_quote=current.code.find('"');
    check(quote!=std::string::npos&&new_quote!=std::string::npos&&
            old.code.substr(quote)==current.code.substr(new_quote),
        "Formatting modified bytes at or after the incomplete literal");
    const auto old_state=before_document.GetNamedObject(L"stateLayout");
    auto new_state=after_document.GetNamedObject(L"stateLayout");
    auto archives=new_state.GetNamedArray(L"sourceFormatRevisions");
    auto edges=new_state.GetNamedArray(L"editorial");
    const auto prior_archives=old_state.GetNamedArray(L"sourceFormatRevisions");
    const auto prior_edges=old_state.GetNamedArray(L"editorial");
    check(archives.Size()==prior_archives.Size()+1&&edges.Size()==prior_edges.Size()+1,
        "The review did not add exactly one archive and one editorial edge");
    const auto added=archives.GetObjectAt(archives.Size()-1);
    check(text(added,L"id")==owner_id&&
            added.GetNamedObject(L"block").Stringify()==
                JsonObject::Parse(winrt::to_hstring(old.raw_json)).Stringify(),
        "The new archive lost the complete authentic original block");
    const auto position=stable_block_position(after,owner_id);
    after_document.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page))
        .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block))
        .Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(old.code)));
    archives.RemoveAt(archives.Size()-1);
    edges.RemoveAt(edges.Size()-1);
    check(after_document.Stringify()==before_document.Stringify(),
        "Formatting changed curriculum, old diagnostics, answers or existing history");
}
void verify_metadata(const Lesson& before,const Lesson& after,const Lesson& metadata) {
    check(metadata.id==after.id&&metadata.state_layout.signatures==after.state_layout.signatures&&
            metadata.state_layout.lineage==after.state_layout.lineage&&
            metadata.state_layout.source_format_revisions.size()==after.state_layout.source_format_revisions.size(),
        "The progress loader disagrees with the full formatted lesson");
    for(const auto& page:before.pages)for(const auto& old:page.blocks) {
        const auto& revised=owner(after,old.stable_id);
        const auto& projected=owner(metadata,old.stable_id);
        for(const auto* current:{&revised,&projected})
            check(current->type==old.type&&current->address.page==old.address.page&&
                    current->address.block==old.address.block&&
                    draft_key("programming",after,*current)==draft_key("programming",before,old)&&
                    exercise_key(after,*current)==exercise_key(before,old),
                "A loader changed a stored address, draft or response identity");
    }
}
void verify_state(const Lesson& before,const Lesson& after,const Lesson& metadata,
        const std::filesystem::path& directory) {
    state::StateData saved;
    state::prepare_lesson_state(saved,before);
    for(const auto& page:before.pages)for(const auto& block:page.blocks) {
        if(block.type==BlockType::cpp_compiler) {
            state::Draft draft;
            draft.source=block.initial_code+"\n// borrador QA independiente\r\n";
            draft.input=" 7\r\n";draft.selection_start=3;draft.selection_end=9;draft.has_saved=true;
            state::put_draft(saved,draft_key("programming",before,block),draft);
        }
        if(block.type==BlockType::output_prediction&&!block.answers.empty()) {
            // Synthetic stored response tests preservation, not a learner
            // interaction or a freshly earned execution result.
            state::Response response;response.answer=block.answers.front();response.correct=true;
            state::put_response(saved,before,block,response);
        }
    }
    saved.preferences["unterminated_format_qa"]["preserve"]=std::string(" exacto\t\r\n");
    state::save_current_page(saved,before,stable_block_position(before,owner_id)->page);
    {
        state::StateStore store(directory);
        check(store.load()==state::StateData{},"The isolated QA state directory was not empty");
        store.save(saved);
    }
    state::StateData reopened;
    {state::StateStore store(directory);reopened=store.load();}
    check(reopened==saved,"The isolated state changed during physical reopen");
    for(const auto* lesson:{&after,&metadata}) {
        state::prepare_lesson_state(reopened,*lesson);
        state::validate_lesson_state(reopened,*lesson);
        check(reopened==saved,"Source formatting rewrote a draft, answer, cursor, bookmark or preference");
    }
    {
        state::StateStore store(directory);
        check(store.load()==saved,"The durable QA state changed before its second save");
        store.save(reopened);
    }
    {state::StateStore store(directory);check(store.load()==saved,"Adopted state changed on its second reopen");}
}
void verify_rejection(const WindowsEnvironment& environment,const std::string& source,std::uint64_t sequence) {
    CppRunRequest request;
    request.request_id=sequence;request.revision=sequence;
    request.project_key="unterminated-source-format-qa";
    request.projects_directory=environment.paths.projects/L"unterminated-source-format";
    request.source_utf8=source;request.compile_timeout=45s;request.execution_timeout=5s;
    const auto result=run_cpp(request);
    const auto diagnostics=result.compilation.stdout_bytes+result.compilation.stderr_bytes;
    check(result.request_id==request.request_id&&result.revision==request.revision&&
            result.compilation.status==CppPhaseStatus::completed&&result.compilation.exit_code&&
            *result.compilation.exit_code!=0&&result.execution.status==CppPhaseStatus::not_started&&
            diagnostics.find("C2001")!=std::string::npos,
        "MSVC did not retain the intentional incomplete-literal rejection: "+diagnostics+result.error_utf8);
    check(read(result.run_directory/L"main.cpp")==source,
        "Native compilation used a different source than the authenticated example");
    std::cout<<"PASS actual MSVC incomplete literal "<<sequence<<": C2001, compilation rejected, no execution.\n";
}
void verify_negative(const JsonObject& manifest,const std::filesystem::path& directory) {
    const std::set<std::string> expected{
        "tail_space","tail_newline","repaired_quote","prefix_token","line_splice",
        "removed_archive","altered_archive","borrowed_owner"};
    std::set<std::string> observed;
    for(const auto& raw:manifest.GetNamedArray(L"negative")) {
        const auto entry=raw.GetObject();const auto name=text(entry,L"name");
        check(observed.insert(name).second,"A negative case was duplicated");
        const auto payload=read(inside(directory,text(entry,L"path")));
        check(state::sha256(payload)==text(entry,L"sha256"),"A negative fixture changed after export");
        bool full=false,progress=false;
        try{(void)parse_lesson_json(payload,"es");}
        catch(const std::exception&){full=true;}catch(const winrt::hresult_error&){full=true;}
        try{(void)parse_lesson_progress_metadata(payload,"es");}
        catch(const std::exception&){progress=true;}catch(const winrt::hresult_error&){progress=true;}
        check(full&&progress,"A Windows loader accepted a resealed invalid source or borrowed archive: "+name);
    }
    check(observed==expected,"The native negative case census changed");
}
} // namespace

int main(int argc,char** argv)try {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;
    check(argc==2,"Supply the authenticated incomplete-literal format manifest");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const std::filesystem::path manifest_path(argv[1]);
    const auto manifest_bytes=read(manifest_path);
    const auto manifest=JsonObject::Parse(winrt::to_hstring(manifest_bytes));
    check(manifest.GetNamedNumber(L"version")==1&&text(manifest,L"kind")=="unterminated-source-format"&&
            text(manifest,L"publishedCommit")=="55e60dcd4c9da2ddf71aad6eddd1c5937e312337"&&
            text(manifest,L"owner")==owner_id&&text(manifest,L"beforeSha256")==publication&&
            text(manifest,L"sourceSha256")==original_source&&text(manifest,L"formattedSourceSha256")==formatted_source,
        "The fixture manifest lost its independent publication and source pins");
    const auto directory=manifest_path.parent_path();
    const auto before_bytes=read(inside(directory,text(manifest,L"before")));
    const auto after_bytes=read(inside(directory,text(manifest,L"after")));
    check(state::sha256(before_bytes)==publication&&state::sha256(after_bytes)==text(manifest,L"afterSha256"),
        "The complete original/formatted fixture bytes differ from their hashes");
    const auto before=parse_lesson_json(before_bytes,"es");
    const auto after=parse_lesson_json(after_bytes,"es");
    const auto metadata=parse_lesson_progress_metadata(after_bytes,"es");
    check(before.id==text(manifest,L"lessonId"),"The complete fixture belongs to another lesson");
    verify_history(before,after,JsonObject::Parse(winrt::to_hstring(before_bytes)),
                   JsonObject::Parse(winrt::to_hstring(after_bytes)));
    verify_metadata(before,after,metadata);
    const LaunchOptions options{ProfileId::Codex,L"unterminated-source-format-"+
        std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()),false};
    const auto environment=resolve_windows_environment(options);
    initialize_profile_directories(environment.paths);
    verify_state(before,after,metadata,environment.paths.projects/L"unterminated-source-format-state");
    verify_rejection(environment,owner(before,owner_id).code,910001);
    verify_rejection(environment,owner(after,owner_id).code,910002);
    verify_negative(manifest,directory);
    check(read(manifest_path)==manifest_bytes&&
            read(inside(directory,text(manifest,L"before")))==before_bytes&&
            read(inside(directory,text(manifest,L"after")))==after_bytes,
        "Native QA changed its original fixtures or manifest");
    std::cout<<"PASS incomplete-literal source format: both Windows loaders, complete archive, exact state reopening, "
        <<"eight resealed negative documents and two real MSVC rejections. No visual/audio acceptance claimed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL incomplete-literal format: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error){std::cerr<<"FAIL incomplete-literal Windows: "<<winrt::to_string(error.message())<<'\n';return 1;}

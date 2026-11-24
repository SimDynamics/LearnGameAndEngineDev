// Exact, authenticated course-edition copies: real Windows loaders/catalog,
// hidden LessonView mounts, MSVC attempts, original math worker and state.
#include "core/catalog.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "platform/windows/android_import.hpp"
#include "platform/windows/catalog_loader.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/narration_worker.hpp"
#include "platform/windows/state_store.hpp"
#include "presentation/windows/lesson_view.hpp"

#include <windows.h>
#include <commctrl.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace lgaed::presentation;
using namespace winrt::Windows::Data::Json;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
std::string phase="setup";
std::uint64_t sequence=1100000;
unsigned compiler_attempts=0,diagnostic_attempts=0,executed_programs=0,math_requests=0,mounted_lessons=0,rejections=0;

void require(bool value,std::string_view message) {
    if(!value)throw std::runtime_error(phase+": "+std::string(message));
}
std::string text(const JsonObject& object,const wchar_t* key) {return winrt::to_string(object.GetNamedString(key));}
JsonObject object(std::string_view value) {return JsonObject::Parse(winrt::to_hstring(value));}
std::string json(const JsonObject& value) {return winrt::to_string(value.Stringify());}
std::string read(const fs::path& path) {const auto bytes=state::read_bytes(path);return {bytes.begin(),bytes.end()};}
fs::path inside(const fs::path& root,std::string_view name) {
    const fs::path relative(winrt::to_hstring(name).c_str());
    require(!relative.empty()&&!relative.is_absolute()&&!relative.has_root_name(),"The QA path is not relative");
    for(const auto& part:relative)require(part!=L".."&&part!=L".","The QA path escapes its directory");
    return root/relative;
}
void write(const fs::path& path,std::string_view bytes) {
    state::require_safe_path(path);fs::create_directories(state::native_io_path(path.parent_path()));
    std::ofstream out(state::native_io_path(path),std::ios::binary|std::ios::trunc);
    require(out.good(),"Cannot open an isolated QA artifact");
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));out.close();
    require(out.good(),"Cannot save a complete isolated QA artifact");
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);
    require(position.has_value(),"An authenticated active owner is missing");
    return lesson.pages.at(position->page).blocks.at(position->block);
}
void preserved(const state::StateData& original,const state::StateData& current) {
    for(const auto& [domain,values]:original.preferences)for(const auto& [key,value]:values) {
        const auto* now=state::find(current,domain,key);
        require(now&&*now==value,"A source receipt, draft, answer or unrelated preference changed");
    }
}
struct Pin {std::string_view id,sha256;};
// Independent, complete original publication pins. The exporter cannot change
// its source or inventory without changing this separate native acceptance.
constexpr std::array<Pin,18> pins{{
    Pin{"u6l2","e9c67087a6c2ba34ee7cd5ea4db0700ab1e52124234011627936cc804de98f18"},
    Pin{"u6l19","d566ea60c14409b92bd9e09231a9d12a545b0b0e5c68bdd94b04b84d99366b80"},
    Pin{"u6l20","c89c2afac52787151092d364a9c8427d647ae7f00853a6c7083aa3ba4a4627eb"},
    Pin{"u9l6","48a2fe71fa4d28d3388964bd7bef12658d83037c282ae2df6accf5205c9c8be4"},
    Pin{"u9l10","9d2e6fad4d0facc99d31c3322b82d8f2e38598df1a56b1d440e9d39cd2e9012e"},
    Pin{"u9l11","adf6588bdb1dd865d894227b041189431ea261a8fe4fab0bf47983c64fe17bcd"},
    Pin{"u9l13","94f62385157f5dc4684ac03bba40eca9cc580023853323e9652c79cfa4dab30e"},
    Pin{"u11l5","32022398a4c57cbfaa90ab20e61af1b357bf4cc3a04f6e8a90e5bdb925b724c9"},
    Pin{"u11l6","0b6f85938324d2bc22f029429eac311847cbabdf882b6f455c19e18e7356fa36"},
    Pin{"u11l7","d4b2fe8c38730312b273e2faa98f1ee6609ca0f028816f7ac59589be0a7ff4ba"},
    Pin{"u11l8","5cf2807129ac1b3fbb42fe0dea9576db74a7d5a47db32e89dea386f24ccbf870"},
    Pin{"u11l14","65b762c8cff56c1fbf5c33094be61dc3d0e989ad1798d110cf80344b21e91393"},
    Pin{"u11l15","7b8704f4d738e56cb873358929b36b49ca9f159f2652fa414470395c1408f1a9"},
    Pin{"u11l16","b7179c7e6a1e5e0a51106878a556a08b7a7606b0552910b97c1d16651db6b205"},
    Pin{"u11l18","1ba0496e8d1afafb5d79556112f8d2d91336925b775593e1aef7db6dd9b7b26a"},
    Pin{"u12l13","793b441f408f7645fc2b22024cfb8d10bfe28c5d1afc07312ea52969532a206d"},
    Pin{"u12l20","c81c2bdc6faf28736d5208183ca8ca224c54813fc74b3e79127251aa93d1e44f"},
    Pin{"u12l26","73eb2577ac3ed1622aeee92ed54204994927309a7c3c8d4cb0b3daaa4e83f18f"},
}};
const Pin& pin_for(std::string_view id) {
    const auto found=std::ranges::find(pins,id,&Pin::id);
    require(found!=pins.end(),"An unexpected lesson entered the edition-copy fixture");return *found;
}
struct Fixture {
    JsonObject entry;
    std::string before_raw,after_raw;
    Lesson before,after,metadata;
};
struct Input {
    fs::path directory,content;
    JsonObject manifest;
    std::string manifest_bytes;
    std::map<std::string,std::string> files;
    std::vector<Fixture> lessons;
};
bool reference_available(const state::StateData& data,const Lesson& lesson,const Block& block) {
    const auto at=stable_block_position(lesson,block.stable_id);
    const auto completed=[&](const Block& item){return compiler_puzzle_completed(data,lesson,item);};
    return compiler_reference_available(lesson,at->page,block,
        compiler_completion_snapshot(lesson,completed),completed);
}
void same_contract(const Fixture& fixture,const Lesson& current) {
    const auto& before=fixture.before;
    require(current.id!=before.id&&current.id==text(fixture.entry,L"editionLessonId")&&
            current.locale==before.locale&&current.course_edition&&current.course_edition_source&&
            current.course_edition_source->lesson_id==before.id&&current.state_layout.version==2&&
            current.state_layout.lineage!=before.state_layout.lineage,
        "A copy lost its authenticated origin or independent edition identity");
    const auto& layout=current.state_layout;
    require(layout.additions.empty()&&layout.retired.empty()&&layout.retirement_history.empty()&&
            layout.source_format_revisions.empty()&&layout.identifier_rename_revisions.empty()&&
            layout.math_text_revisions.empty()&&layout.math_matching_text_revisions.empty()&&
            layout.choice_revisions.empty()&&layout.compiler_revisions.empty()&&
            layout.compiler_case_order_revisions.empty()&&layout.compiler_case_revisions.empty()&&
            layout.editorial_returns.empty()&&layout.reading_revisions.empty()&&current.retired_blocks.empty()&&
            layout.family.size()==1&&layout.family.contains("es"),
        "A fresh edition inherited historical state adapters");
    require(current.pages.size()==before.pages.size()&&layout.current.size()==before.pages.size(),
        "An exact copy changed the page inventory");
    for(std::size_t p=0;p<current.pages.size();++p) {
        const auto& old_page=before.pages[p];const auto& page=current.pages[p];
        require(page.stable_id==old_page.stable_id&&page.blocks.size()==old_page.blocks.size(),
            "An exact copy changed local page/block identities");
        for(std::size_t b=0;b<page.blocks.size();++b) {
            const auto& old=old_page.blocks[b];const auto& block=page.blocks[b];
            require(block.stable_id==old.stable_id&&block.address.page==p&&block.address.block==b&&
                    json(object(block.raw_json))==json(object(old.raw_json))&&
                    block.type==old.type&&block.initial_code==old.initial_code&&block.code==old.code&&
                    block.project_files==old.project_files&&block.source_files==old.source_files&&
                    block.options==old.options&&block.correct_index==old.correct_index&&
                    block.answers==old.answers&&block.correct_order==old.correct_order&&
                    block.crt_output_original==old.crt_output_original&&
                    exercise_key(current,block)!=exercise_key(before,old)&&
                    draft_key("programming",current,block)!=draft_key("programming",before,old),
                "An exact copy changed content, evaluation fields or independent storage");
            require(!block.choice_revision&&!block.compiler_revision&&!block.math_label_revision&&
                    !block.order_refinement&&block.compiler_case_order_revisions.empty()&&
                    block.compiler_case_revisions.empty(),"A copied owner inherited a state migration adapter");
            require(layout.signatures.at(block.stable_id)==test::compile_check_test_fingerprint(object(block.raw_json)),
                "A fresh copy did not bind the complete current block payload");
        }
    }
    for(const auto& raw:fixture.entry.GetNamedArray(L"blocks")) {
        const auto row=raw.GetObject();const auto& old=owner(before,text(row,L"owner"));
        const auto& block=owner(current,text(row,L"owner"));
        if(block.type==BlockType::cpp_compiler)
            require(reference_objective_frame(current,block)!=reference_objective_frame(before,old),
                "A copied compiler objective retained the original receipt namespace");
        if(text(row,L"kind")=="reference")
            require(block.code==text(row,L"source")&&state::sha256(block.code)==text(row,L"sourceSha256"),
                "A complete reviewed reference lost its exact code");
    }
}
Input load_input(const fs::path& manifest) {
    Input result;result.directory=manifest.parent_path();result.content=result.directory/L"content";
    result.manifest_bytes=read(manifest);result.manifest=object(result.manifest_bytes);
    require(result.manifest.GetNamedNumber(L"version")==1&&text(result.manifest,L"kind")=="course-edition-source"&&
            text(result.manifest,L"publishedCommit")=="b92d4d487b7f7deb9a1ff056ddeb6ea9e256d660",
        "The fixture is not the audited complete publication");
    for(const auto& item:result.manifest.GetNamedObject(L"files")) {
        const auto name=winrt::to_string(item.Key());const auto bytes=read(inside(result.directory,name));
        require(state::sha256(bytes)==winrt::to_string(item.Value().GetString()),"A fixture file changed after export");
        result.files.emplace(name,bytes);
    }
    require(result.files.size()==42,"The complete fixture omitted or added a file");
    std::set<std::string> seen;std::map<std::string,unsigned> counts;
    for(const auto& raw:result.manifest.GetNamedArray(L"lessons")) {
        Fixture f;f.entry=raw.GetObject();const auto name=text(f.entry,L"id");phase=name+"/load";
        require(seen.insert(name).second,"A copied lesson was repeated");const auto& pin=pin_for(name);
        f.before_raw=result.files.at(text(f.entry,L"original"));f.after_raw=result.files.at(text(f.entry,L"edition"));
        require(state::sha256(f.before_raw)==pin.sha256&&text(f.entry,L"originalSha256")==pin.sha256&&
                state::sha256(f.after_raw)==text(f.entry,L"editionSha256"),"A complete lesson differs from its publication pin");
        f.before=parse_lesson_json(f.before_raw,"es");f.after=load_lesson(inside(result.directory,text(f.entry,L"edition")),"es");
        f.metadata=parse_lesson_progress_metadata(f.after_raw,"es");
        require(f.before.id==text(f.entry,L"originalLessonId"),"The original lesson identity changed");
        same_contract(f,f.after);same_contract(f,f.metadata);
        std::set<std::string> owners;
        for(const auto& item:f.entry.GetNamedArray(L"blocks")) {
            const auto row=item.GetObject();const auto id=text(row,L"owner");
            require(owners.insert(id).second,"A reviewed owner was repeated");
            require(test::compile_check_test_fingerprint(object(owner(f.before,id).raw_json))==text(row,L"blockFingerprint"),
                "A reviewed block differs from its complete source");
            ++counts[text(row,L"kind")];
        }
        result.lessons.push_back(std::move(f));
    }
    require(result.lessons.size()==18&&counts==std::map<std::string,unsigned>{{"project-diagnostic",12},{"diagnostic",5},
            {"choice",8},{"matching",2},{"output",6},{"intentional-exit",2},{"crt-output",1},{"reference",5}},
        "The fixture lost one of its 18 lessons, 36 evaluable goals or five references");
    return result;
}
void verify_catalog(const Input& input,const state::StateData& data) {
    const CatalogLocation course_location{"es","programming","cpp_fundamentals"};
    const auto course=load_catalog(input.content,course_location);
    require(course.entries.size()==4,"The isolated copied catalog lost a unit");
    const auto scope=catalog_scope(input.content,course_location);
    require(scope.lessons.size()==18,"The catalog scope lost a copied lesson");
    std::set<std::string> found;
    for(const auto& unit:course.entries) {
        auto location=course_location;location.unit=unit.id;
        const auto lessons=load_catalog(input.content,location);
        for(const auto& lesson:lessons.entries) {
            require(lesson.available&&found.insert(lesson.id).second,"A copied lesson is unavailable or duplicated in the catalog");
            require(active_catalog_lesson(input.content,location,lesson.id).has_value(),"The active route cannot open a copied lesson");
            const auto progress=catalog_entry_progress(input.content,location,lesson,data);
            require(progress.completed==0&&progress.total>0,"A new catalog entry inherited completed pages");
        }
    }
    for(const auto& f:input.lessons)require(found.contains(f.after.id)&&!found.contains(f.before.id),
        "The active copied catalog mixed old and new identities");
}
void require_pending(const state::StateData& data,const Fixture& f) {
    require(state::current_page(data,f.after)==0&&!catalog_lesson_completed(data,f.after.id),
        "A copy inherited old navigation or lesson completion");
    for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
        const auto row=raw.GetObject();const auto& block=owner(f.after,text(row,L"owner"));
        require(!state::get_response(data,f.after,block),"A copy inherited an old response");
        if(block.type==BlockType::cpp_compiler) {
            const auto draft=state::get_draft(data,draft_key("programming",f.after,block),block.initial_code);
            require(!draft.has_saved&&draft.source==block.initial_code&&draft.input.empty()&&
                    !compiler_puzzle_completed(data,f.after,block)&&compiler_puzzle_case_count(data,f.after,block)==0,
                "A copy inherited a source draft, input or compiler receipt");
        } else if(text(row,L"kind")=="reference")
            require(!reference_available(data,f.after,block),"A copied reference inherited a previous execution");
    }
}
void rejected_source(std::string_view source,std::string_view reason) {
    bool full=false,metadata=false;
    try{(void)parse_lesson_json(source,"es");}catch(const std::exception&){full=true;}catch(const winrt::hresult_error&){full=true;}
    try{(void)parse_lesson_progress_metadata(source,"es");}catch(const std::exception&){metadata=true;}catch(const winrt::hresult_error&){metadata=true;}
    require(full&&metadata,reason);++rejections;
}
void rejected(const JsonObject& root,std::string_view reason) {
    rejected_source(json(root),reason);
}
void reject_tampering(const Fixture& f) {
    auto duplicate=f.after_raw;const auto brace=duplicate.find('{');
    require(brace!=std::string::npos,"A copied fixture has no JSON object");
    duplicate.insert(brace+1,"\"id\":\""+f.after.id+"\",");
    rejected_source(duplicate,"A copied source accepted a duplicate JSON key");
    auto current=object(f.after_raw);current.Remove(L"courseEditionSource");rejected(current,"A copied special goal lost its origin proof");
    current=object(f.after_raw);current.GetNamedObject(L"courseEditionSource").Insert(L"sourceDocumentSha256",JsonValue::CreateStringValue(L"0000"));
    rejected(current,"A false source-document digest was accepted");
    current=object(f.after_raw);current.GetNamedObject(L"courseEditionSource").Insert(L"sourcePath",JsonValue::CreateStringValue(L"../outside.json"));
    rejected(current,"A foreign or escaping source path was accepted");
    current=object(f.after_raw);current.GetNamedObject(L"stateLayout").Insert(L"editorial",JsonArray::Parse(LR"([{"id":"unproved","beforeSha256":"x","afterSha256":"y"}])"));
    rejected(current,"A v1 exact copy accepted an unsupported own revision");
    for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
        const auto row=raw.GetObject();const auto kind=text(row,L"kind");const auto at=stable_block_position(f.after,text(row,L"owner"));
        current=object(f.after_raw);auto block=current.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
            .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block));
        if(kind=="choice")block.Insert(L"correctIndex",JsonValue::CreateNumberValue(row.GetNamedNumber(L"incorrectIndex")));
        else if(kind=="matching")block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedObject(L"validator")
            .Insert(L"expected",row.GetNamedObject(L"incorrect").GetNamedObject(winrt::to_hstring(text(row,L"responseId"))).GetNamedObject(L"pairs"));
        else if(kind=="reference")block.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(text(block,L"code")+"\n// altered copy")));
        else block.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(text(block,L"initialCode")+"\n// altered copy")));
        rejected(current,"An alleged equivalent copy changed source, answer or reference");
        if(kind=="project-diagnostic"||kind=="diagnostic") {
            current=object(f.after_raw);current.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
                .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block)).Remove(L"compileCheck");
            rejected(current,"A copied compiler diagnostic could be removed");
        } else if(kind=="output"||kind=="intentional-exit"||kind=="crt-output") {
            current=object(f.after_raw);auto check=current.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
                .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block)).GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
            check.Insert(L"outputs",JsonArray::Parse(LR"(["invented-output"])"));
            rejected(current,"A copied goal changed its exact stdout");
            current=object(f.after_raw);check=current.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(at->page))
                .GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(at->block)).GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);
            check.Insert(L"exitCode",JsonValue::CreateNumberValue(kind=="intentional-exit"?0:1));
            rejected(current,"A copied goal changed its exact exit code");
        }
    }
}
CppProjectFiles project_files(const JsonObject& attempt) {
    CppProjectFiles result;
    if(attempt.HasKey(L"projectFiles"))for(const auto& raw:attempt.GetNamedArray(L"projectFiles")) {
        const auto file=raw.GetObject();result.emplace_back(text(file,L"path"),text(file,L"initialCode"));
    }
    return result;
}
void verify_attempt(const Fixture& f,const JsonObject& attempt) {
    const auto provenance=attempt.GetNamedObject(L"provenance");const auto& source_owner=owner(f.before,text(provenance,L"owner"));
    auto expected=text(provenance,L"field")=="code"?source_owner.code:source_owner.initial_code;
    require(state::sha256(expected)==text(provenance,L"sourceSha256"),"An attempt's origin is not an authenticated full source");
    if(provenance.HasKey(L"mutation")) {
        const auto mutation=provenance.GetNamedObject(L"mutation");const auto kind=text(mutation,L"kind");
        if(kind=="insert-diagnostic-statement") {
            const auto at=expected.rfind("    return 0;");require(at!=std::string::npos,"A diagnostic starter lost its insertion point");
            expected.insert(at,"    "+text(mutation,L"statement")+'\n');
        } else {
            require(kind=="normal-exit-wrapper","An unreviewed attempt mutation was declared");
            const auto at=expected.find("int main()");require(at!=std::string::npos,"An exit source lost main");
            expected.replace(at,10,"int edition_source_original_main()");
            expected+="\nint main() { edition_source_original_main(); return 0; }\n";
        }
    }
    require(expected==text(attempt,L"source")&&state::sha256(expected)==text(attempt,L"sourceSha256"),
        "An attempt is not its exact published source or explicit mutation");
    if(attempt.HasKey(L"projectFiles")) {
        require(project_files(attempt)==source_owner.project_files,"A diagnostic project differs from its complete publication");
        for(const auto& [path,source]:project_files(attempt))
            require(state::sha256(source)==winrt::to_string(attempt.GetNamedObject(L"projectFileSha256").GetNamedString(winrt::to_hstring(path))),
                "A project file differs from its authenticated bytes");
    }
}
struct Capture {CppRunRequest request;CppRunResult result;FreshExecution execution;};
Capture run_attempt(const WindowsEnvironment& environment,const Lesson& lesson,const Block& block,const JsonObject& attempt) {
    Capture capture;auto& request=capture.request;request.request_id=++sequence;request.revision=sequence;
    request.project_key=draft_key("programming",lesson,block);request.projects_directory=environment.paths.projects/L"edition-source";
    request.source_utf8=text(attempt,L"source");request.stdin_utf8=text(attempt,L"standardInput");
    request.project_files=project_files(attempt);
    if(attempt.HasKey(L"sourceFiles"))for(const auto& value:attempt.GetNamedArray(L"sourceFiles"))request.source_files.push_back(winrt::to_string(value.GetString()));
    request.compile_timeout=45s;request.execution_timeout=10s;
    if(block.compile_check){request.diagnostic=prepare_cpp_diagnostic_request(block,request);require(request.diagnostic.has_value(),"The copied diagnostic lost its request recipe");}
    else require(block.output_check&&compiler_runtime_request_allowed(block,request),"The copied runtime goal rejected its original request");
    ++compiler_attempts;capture.result=run_cpp(request);
    require(capture.result.error_utf8.empty(),"The real compiler runner failed: "+capture.result.error_utf8);
    if(block.compile_check) {
        ++diagnostic_attempts;require(capture.result.diagnostic&&capture.result.diagnostic->control.succeeded()&&
            capture.result.execution==CppPhaseResult{},"A diagnostic ran code or lost its successful independent control");
        if(request.project_files.empty())require(read(capture.result.run_directory/L"candidate/main.cpp")==request.source_utf8,
            "The diagnostic adapter rewrote its captured source");
        else for(const auto& [path,source]:request.project_files)
            require(read(capture.result.run_directory/L"candidate/project"/path)==source,"A diagnostic candidate file changed");
    } else {
        ++executed_programs;capture.execution=capture_cpp_execution(request,capture.result);
        require(capture.result.compilation.succeeded()&&capture.result.execution.status==CppPhaseStatus::completed&&
                capture.execution.execution_completed,
            "A real program did not compile and finish: "+capture.result.compilation.stdout_bytes+capture.result.compilation.stderr_bytes);
        require(read(capture.result.run_directory/L"main.cpp")==request.source_utf8&&
                read(capture.result.run_directory/L"stdin.bin")==request.stdin_utf8&&
                read(capture.result.run_directory/L"execution-stdout.bin")==capture.execution.standard_output&&
                read(capture.result.run_directory/L"execution-stderr.bin")==capture.execution.standard_error,
            "The executor rewrote program, input or output bytes");
    }
    return capture;
}
bool accepted(const Block& block,const Capture& capture) {
    return block.compile_check?expected_compile_diagnostic(capture.request,capture.result):
        evaluate_cpp_output_check(*block.output_check,capture.execution,capture.request.revision).verdict==OutputVerdict::correct;
}
bool record(state::StateData& data,const Lesson& lesson,const Block& block,const Capture& capture) {
    const auto feedback=std::string("Correcto\n")+(block.compile_check?block.compile_check->explanation:block.output_check->cases.front().explanation);
    return block.compile_check?record_compiler_diagnostic_case(data,lesson,block,capture.request,capture.result,feedback):
        record_compiler_puzzle_case(data,lesson,block,capture.request,capture.execution,feedback);
}
JsonObject math_result(NarrationWorker& worker,const Block& block,const JsonObject& response) {
    ++math_requests;return object(worker.validate_math(math_evaluation_source(block),json(response),"es"));
}
void record_math(state::StateData& data,const Lesson& lesson,const Block& block,const JsonObject& response,const JsonObject& reply) {
    require(reply.GetNamedBoolean(L"correct"),"The authentic matching response failed in the real worker");
    JsonObject payload;payload.Insert(L"responses",response);payload.Insert(L"correct",reply.GetNamedValue(L"correct"));
    payload.Insert(L"contentFingerprint",reply.GetNamedValue(L"contentFingerprint"));
    state::Response saved;saved.correct=true;saved.raw_json=json(payload);state::put_response(data,lesson,block,saved);
}
void seed_originals(const Input& input,const WindowsEnvironment& environment,NarrationWorker& worker,state::StateData& data) {
    data.preferences["edition_source_qa"]["opaque"]=std::string(" Conservar\t\r\n & < > ");
    data.preferences["learning_progress"]["lesson_qa_other__unit__lesson_completed"]=true;
    data.preferences["native_windows"]["ui_language"]=std::string("es");
    data.preferences["native_windows"]["content_language"]=std::string("es");
    for(const auto& f:input.lessons) {
        phase=text(f.entry,L"id")+"/real original achievements";state::prepare_lesson_state(data,f.before);
        for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
            const auto row=raw.GetObject();const auto& block=owner(f.before,text(row,L"owner"));const auto kind=text(row,L"kind");
            if(row.HasKey(L"attempts")) {
                const auto attempts=row.GetNamedArray(L"attempts");require(attempts.Size()==2,"An evaluable source lacks both real attempts");
                const auto good=attempts.GetObjectAt(0),bad=attempts.GetObjectAt(1);
                require(good.GetNamedBoolean(L"accepted")&&!bad.GetNamedBoolean(L"accepted"),"The attempt labels lost their independent contrast");
                verify_attempt(f,good);verify_attempt(f,bad);
                const auto capture=run_attempt(environment,f.before,block,good);
                require(accepted(block,capture)&&record(data,f.before,block,capture)&&compiler_puzzle_completed(data,f.before,block),
                    "The original real execution did not earn its own objective");
                state::Draft draft;draft.source=block.initial_code+"\n// Original QA draft\n";draft.input="original input\t\r\n";
                draft.selection_start=2;draft.selection_end=7;draft.indentation_json=state::make_indentation_metadata(draft.source,{});
                state::put_draft(data,draft_key("programming",f.before,block),draft);
            } else if(kind=="choice") {
                state::Response response;response.selected=static_cast<std::int64_t>(row.GetNamedNumber(L"correctIndex"));
                response.correct=evaluate_choice(block,static_cast<std::size_t>(response.selected)).correct;
                require(response.correct,"The pinned original choice is incorrect");state::put_response(data,f.before,block,response);
            } else if(kind=="matching") {
                const auto answer=row.GetNamedObject(L"correct");record_math(data,f.before,block,answer,math_result(worker,block,answer));
            }
        }
        state::save_current_page(data,f.before,f.before.pages.size()-1);
        data.preferences["learning_progress"]["lesson_"+f.before.id+"_completed"]=true;
    }
}
void pump() {MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
void mount_pending(const WindowsEnvironment& environment,const Fixture& f,state::StateData& data) {
    state::StateStore store(environment.paths.root);(void)store.load();store.save(data);
    const auto host=CreateWindowExW(WS_EX_CONTROLPARENT|WS_EX_NOACTIVATE,L"STATIC",L"Isolated edition-source QA",
        WS_OVERLAPPEDWINDOW,0,0,1100,850,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    require(host!=nullptr,"Cannot create a hidden native lesson host");
    HWND view=nullptr;
    try {
        view=create_lesson_view(host,environment,f.after,{});require(view!=nullptr,"The real LessonView could not open the copied lesson");
        MoveWindow(view,0,0,1100,850,TRUE);pump();
        require(!IsWindowVisible(host)&&lesson_page_state(view).index==0,"Opening a copied lesson inherited a page or exposed the QA window");
        require(flush_lesson_view(view),"The native lesson could not flush its own new state");
        data=read_lesson_state(view);require_pending(data,f);++mounted_lessons;
        DestroyWindow(view);view=nullptr;DestroyWindow(host);
    } catch(...) {if(view)DestroyWindow(view);DestroyWindow(host);throw;}
}
void exercise_copies(const Input& input,const WindowsEnvironment& environment,NarrationWorker& worker,state::StateData& data) {
    for(const auto& f:input.lessons) {
        phase=text(f.entry,L"id")+"/independent copied attempts";
        for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
            const auto row=raw.GetObject();const auto id=text(row,L"owner");const auto kind=text(row,L"kind");
            const auto& old=owner(f.before,id);const auto& block=owner(f.after,id);const auto& compact=owner(f.metadata,id);
            if(row.HasKey(L"attempts")) {
                const auto attempts=row.GetNamedArray(L"attempts");
                const auto bad=run_attempt(environment,f.after,block,attempts.GetObjectAt(1));
                require(!accepted(old,bad)&&!accepted(block,bad),"The copied evaluator accepted an incorrect original counterexample");
                const auto saved=data;require(!record(data,f.after,block,bad)&&data==saved,
                    "An incorrect copied attempt earned credit or changed original state");
                const auto good=run_attempt(environment,f.after,block,attempts.GetObjectAt(0));
                require(accepted(old,good)&&accepted(block,good)&&record(data,f.after,block,good)&&
                        compiler_puzzle_completed(data,f.after,block)&&compiler_puzzle_completed(data,f.metadata,compact)&&
                        compiler_puzzle_case_count(data,f.after,block)==1&&compiler_puzzle_completed(data,f.before,old),
                    "The copied first case did not earn an independent native achievement");
                const auto feedback=compiler_puzzle_success_feedback(data,f.after,block);
                require(feedback&&feedback->correct,"The copied success lost its authored feedback");
                const auto expected=std::string("Correcto\n")+(block.compile_check?block.compile_check->explanation:block.output_check->cases.front().explanation);
                require(feedback->text==expected,"The copied success changed its exact authored explanation");
                state::Draft draft;draft.source=text(attempts.GetObjectAt(0),L"source")+"\n// New edition QA draft\n";
                draft.input="new input\r\n";draft.selection_start=1;draft.selection_end=6;
                draft.indentation_json=state::make_indentation_metadata(draft.source,{});
                state::put_draft(data,draft_key("programming",f.after,block),draft);
            } else if(kind=="choice") {
                const auto correct=static_cast<std::size_t>(row.GetNamedNumber(L"correctIndex"));
                const auto wrong=static_cast<std::size_t>(row.GetNamedNumber(L"incorrectIndex"));
                require(wrong!=correct&&block.options.size()==static_cast<std::size_t>(row.GetNamedNumber(L"optionCount")),
                    "The copied choice lost its published contrast");
                for(std::size_t option=0;option<block.options.size();++option) {
                    const auto a=evaluate_choice(old,option),b=evaluate_choice(block,option),c=evaluate_choice(compact,option);
                    require(a.correct==(option==correct)&&b.correct==a.correct&&c.correct==a.correct&&b.feedback==a.feedback,
                        "The copied choice changed an option/answer/feedback relation");
                }
                state::Response answer;answer.selected=static_cast<std::int64_t>(correct);answer.correct=true;
                state::put_response(data,f.after,block,answer);
            } else if(kind=="matching") {
                const auto wrong=row.GetNamedObject(L"incorrect");const auto a=math_result(worker,old,wrong),b=math_result(worker,block,wrong);
                require(!a.GetNamedBoolean(L"correct")&&!b.GetNamedBoolean(L"correct")&&
                        a.GetNamedString(L"feedback")==b.GetNamedString(L"feedback")&&
                        a.GetNamedString(L"contentFingerprint")==b.GetNamedString(L"contentFingerprint"),
                    "The copied matching changed the original worker's incorrect verdict");
                const auto answer=row.GetNamedObject(L"correct"),reply=math_result(worker,block,answer);
                record_math(data,f.after,block,answer,reply);
            }
        }
        for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
            const auto row=raw.GetObject();if(text(row,L"kind")!="reference")continue;
            require(compiler_puzzle_completed(data,f.after,owner(f.after,text(row,L"relatedOwner")))&&
                    reference_available(data,f.after,owner(f.after,text(row,L"owner"))),
                "A copied reviewed reference stayed blocked after its own first-case success");
        }
        std::cout<<"PASS edition-source copied goals "<<text(f.entry,L"id")<<": exact evaluations, first-case receipts, references and separate drafts.\n";
    }
}
std::string xml(std::string_view source) {
    std::string out;for(const auto c:source)switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;
        case '>':out+="&gt;";break;case '"':out+="&quot;";break;case '\r':out+="&#13;";break;
        case '\n':out+="&#10;";break;case '\t':out+="&#9;";break;default:out+=c;}return out;
}
void verify_import(const Input& input,const WindowsEnvironment& environment,const state::StateData& original) {
    auto exported=original;exported.preferences.erase("native_windows");
    const auto source=environment.paths.root/L"legacy-export";
    for(const auto& [domain,values]:exported.preferences) {
        std::ostringstream out;out<<"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<map>\n";
        for(const auto& [key,value]:values)std::visit([&](const auto& item){using T=std::decay_t<decltype(item)>;
            const auto name=xml(key);
            if constexpr(std::is_same_v<T,std::string>)out<<"<string name=\""<<name<<"\">"<<xml(item)<<"</string>\n";
            else if constexpr(std::is_same_v<T,std::int64_t>)out<<"<long name=\""<<name<<"\" value=\""<<item<<"\"/>\n";
            else if constexpr(std::is_same_v<T,double>)out<<"<float name=\""<<name<<"\" value=\""<<std::setprecision(17)<<item<<"\"/>\n";
            else if constexpr(std::is_same_v<T,bool>)out<<"<boolean name=\""<<name<<"\" value=\""<<(item?"true":"false")<<"\"/>\n";
            else{out<<"<set name=\""<<name<<"\">";for(const auto& member:item)out<<"<string>"<<xml(member)<<"</string>";out<<"</set>\n";}
        },value);
        out<<"</map>\n";write(source/L"shared_prefs"/(domain+".xml"),out.str());
    }
    const auto preview=state::inspect_android_export(source);require(preview.data==exported,"The actual legacy importer changed exported state");
    const auto destination=environment.paths.root/L"import-target";
    {state::StateStore target(destination);(void)target.load();state::commit_android_import(preview,destination);}
    state::StateStore target(destination);auto restored=target.load();preserved(exported,restored);
    for(const auto& f:input.lessons){state::prepare_lesson_state(restored,f.after);require_pending(restored,f);}
    preserved(exported,restored);target.save(restored);
    state::StateStore reopened(destination);require(reopened.load()==restored,"Imported copy state changed on physical reopen");
    require(state::inspect_android_export(source).data==exported,"Import modified its original exported files");
}
void verify_reopen_and_reset(const Input& input,const WindowsEnvironment& environment,const state::StateData& original,const state::StateData& completed) {
    state::StateStore store(environment.paths.root);(void)store.load();store.save(completed);
    state::StateStore reopened(environment.paths.root);const auto restored=reopened.load();
    require(restored==completed,"Physical reopen changed complete edition/source state");preserved(original,restored);
    for(const auto& f:input.lessons) {
        state::validate_lesson_state(restored,f.before);state::validate_lesson_state(restored,f.after);
        for(const auto& raw:f.entry.GetNamedArray(L"blocks")) {
            const auto row=raw.GetObject();const auto& block=owner(f.after,text(row,L"owner"));const auto& old=owner(f.before,block.stable_id);
            if(block.type!=BlockType::cpp_compiler)continue;
            require(compiler_puzzle_completed(restored,f.after,block)&&compiler_puzzle_completed(restored,f.before,old),
                "An independent original/copied achievement was lost on reopen");
            const auto old_draft=state::get_draft(restored,draft_key("programming",f.before,old),old.initial_code);
            const auto new_draft=state::get_draft(restored,draft_key("programming",f.after,block),block.initial_code);
            require(old_draft.has_saved&&new_draft.has_saved&&old_draft.source!=new_draft.source&&
                    old_draft.input=="original input\t\r\n"&&new_draft.input=="new input\r\n"&&
                    old_draft.selection_start==2&&new_draft.selection_start==1,
                "Original and copied source/input/cursor state did not remain separate");
            auto reset=restored;reset_compiler_puzzle(reset,f.after,block);
            require(!compiler_puzzle_completed(reset,f.after,block)&&compiler_puzzle_completed(reset,f.before,old),
                "Resetting a copied objective erased or reused the original achievement");
            auto expected=restored;const auto prefix="compiler-puzzle/v1/es/"+f.after.id+'/'+exercise_key(f.after,block)+'/';
            if(const auto it=expected.preferences.find("native_windows");it!=expected.preferences.end())
                std::erase_if(it->second,[&](const auto& value){return value.first.starts_with(prefix);});
            require(reset==expected,"Resetting a copy removed another goal, original draft or preference");
        }
    }
}
} // namespace

int wmain(int argc,wchar_t** argv)try {
    std::cout<<std::unitbuf;std::cerr<<std::unitbuf;require(argc==3,"Supply edition-source manifest and real math-worker runtime");
    const auto initialized=OleInitialize(nullptr);require(SUCCEEDED(initialized),"OLE initialization failed");
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};
    require(InitCommonControlsEx(&controls)!=FALSE,"Native common controls are unavailable");
    require(LoadLibraryW(L"Msftedit.dll")!=nullptr,"Native RichEdit is unavailable");
    const auto input=load_input(fs::path(argv[1]));
    const auto environment=resolve_windows_environment({ProfileId::Codex,L"edition-source-"+std::to_wstring(GetCurrentProcessId())+
        L"-"+std::to_wstring(GetTickCount64()),false});
    require(!fs::exists(environment.paths.root),"The native edition-source QA profile is not new");
    initialize_profile_directories(environment.paths);NarrationWorker worker{fs::path(argv[2])};
    for(const auto& f:input.lessons){phase=text(f.entry,L"id")+"/tampering";reject_tampering(f);}
    state::StateData original;seed_originals(input,environment,worker,original);
    phase="legacy import without new edition credit";verify_import(input,environment,original);
    auto current=original;
    for(const auto& f:input.lessons){state::prepare_lesson_state(current,f.after);require_pending(current,f);}
    preserved(original,current);verify_catalog(input,current);
    for(const auto& f:input.lessons){phase=text(f.entry,L"id")+"/actual hidden LessonView";mount_pending(environment,f,current);}
    preserved(original,current);exercise_copies(input,environment,worker,current);preserved(original,current);
    phase="durable reopen and scoped reset";verify_reopen_and_reset(input,environment,original,current);
    require(compiler_attempts==78&&diagnostic_attempts==51&&executed_programs==27&&math_requests==8&&mounted_lessons==18,
        "The native QA omitted or repeated a required compiler, worker or real-window scenario");
    for(const auto& [name,bytes]:input.files)require(read(inside(input.directory,name))==bytes,"Native QA changed an authenticated fixture file");
    require(read(fs::path(argv[1]))==input.manifest_bytes,"Native QA changed its publication manifest");
    std::cout<<"PASS edition-source: 18 authenticated lessons, 41 reviewed blocks, 36 evaluated goals; "<<compiler_attempts
        <<" actual MSVC requests ("<<diagnostic_attempts<<" diagnostics, "<<executed_programs<<" executions), "<<math_requests
        <<" real math-worker replies, "<<mounted_lessons<<" hidden LessonView mounts; "<<rejections
        <<" tampering cases rejected by both loaders; fresh catalog/state, real original receipts, import, reopen and scoped reset. No audio acceptance claimed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL edition-source: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error){std::cerr<<"FAIL edition-source Windows "<<phase<<": "<<winrt::to_string(error.message())<<'\n';return 1;}

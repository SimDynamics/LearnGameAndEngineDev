#include "core/code_format_compat.hpp"
#include "core/compiler_flow.hpp"
#include "core/lesson.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/cpp_history_training.hpp"
#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;

void require(bool value,std::string_view reason) {
    if(!value)throw std::runtime_error(std::string(reason));
}
std::string text(const JsonObject& object,const wchar_t* name) {
    return winrt::to_string(object.GetNamedString(name));
}
std::string read(const std::filesystem::path& path) {
    state::require_safe_path(path);
    std::ifstream input(state::native_io_path(path),std::ios::binary);
    require(input.good(),"An authenticated nested-CMake fixture is missing");
    std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(!input.bad(),"Cannot read an authenticated nested-CMake fixture");
    return bytes;
}
std::filesystem::path fixture(const std::filesystem::path& root,std::string_view name) {
    const auto relative=std::filesystem::path(std::string(name));
    require(!relative.empty()&&relative==relative.filename()&&relative.extension()==L".json",
        "A nested-CMake fixture must be one local JSON filename");
    const auto path=root/relative;
    state::require_safe_path(path);
    require(std::filesystem::is_regular_file(path),"A nested-CMake fixture was not generated");
    return path;
}
const Block& owner(const Lesson& lesson,std::string_view identity) {
    const Block* found=nullptr;
    for(const auto& page:lesson.pages)for(const auto& block:page.blocks)
        if(block.stable_id==identity) {
            require(!found,"An authenticated nested-CMake owner is duplicated");
            found=&block;
        }
    require(found!=nullptr,"An authenticated nested-CMake owner is missing");
    return *found;
}
const std::string& source(const CppProjectFiles& files,std::string_view path) {
    const std::string* found=nullptr;
    for(const auto& item:files)if(item.first==path) {
        require(!found,"A nested-CMake project path is duplicated");found=&item.second;
    }
    require(found!=nullptr,"A nested-CMake project path is missing");
    return *found;
}
void replace(CppProjectFiles& files,std::string_view path,std::string replacement) {
    (void)source(files,path);
    for(auto& item:files)if(item.first==path){item.second=std::move(replacement);return;}
}
std::vector<std::string> history_sources(const CppProjectFiles& files) {
    std::vector<std::string> result;
    for(const auto& [path,unused]:files)if(is_cpp_source_file(path))result.push_back(path);
    return result;
}
struct Publication {std::string_view name,sha256;};
constexpr std::array publications{
    Publication{"datos_y_listas_de_objetivos","8d0305f7b3649e1960ea7bb3b1dec875f52560d4f0a3b2fc9ac02c948ffacca4"},
    Publication{"nombrar_reglas_y_separar_acuerdos","13b529b6e65afc634b485792661b6d7d7df1c48bbe8b2099d943e9ae3b5fec18"},
    Publication{"reorganizar_la_descripcion_conservando_el_comprobador","9f3280f719a7967d3e713309fdb29d9b15ac29d84dd7d385fc4e2b3e34be954e"}
};
struct Expected {std::string_view lesson,activity,path;};
constexpr std::array expected{
    Expected{"datos_y_listas_de_objetivos","block_0003_0004","componente/CMakeLists.txt"},
    Expected{"datos_y_listas_de_objetivos","block_0004_0007","componente/CMakeLists.txt"},
    Expected{"datos_y_listas_de_objetivos","block_0006_0004","componente/CMakeLists.txt"},
    Expected{"datos_y_listas_de_objetivos","block_0008_0004","codigo/lotes/CMakeLists.txt"},
    Expected{"nombrar_reglas_y_separar_acuerdos","block_0002_0004","componente/CMakeLists.txt"},
    Expected{"nombrar_reglas_y_separar_acuerdos","block_0005_0006","componente/CMakeLists.txt"},
    Expected{"nombrar_reglas_y_separar_acuerdos","block_0006_0009","componente/CMakeLists.txt"},
    Expected{"reorganizar_la_descripcion_conservando_el_comprobador","block_0004_0012","componente/CMakeLists.txt"},
    Expected{"reorganizar_la_descripcion_conservando_el_comprobador","block_0006_0003","componente/CMakeLists.txt"},
    Expected{"reorganizar_la_descripcion_conservando_el_comprobador","block_0010_0003","componente/CMakeLists.txt"}
};
struct LessonPair {
    Lesson before,after,before_metadata,after_metadata;
    std::filesystem::path before_path,after_path;
    std::string before_bytes,after_bytes;
};
using Lessons=std::map<std::string,LessonPair>;

Lessons load_publications(const std::filesystem::path& root,const JsonObject& manifest) {
    const auto entries=manifest.GetNamedArray(L"lessons");
    require(entries.Size()==publications.size(),"The nested-CMake census requires three complete lessons");
    Lessons lessons;
    for(const auto& entry:entries) {
        const auto item=entry.GetObject();const auto name=text(item,L"name");
        const auto pin=std::ranges::find_if(publications,[&](const auto& value){return value.name==name;});
        require(pin!=publications.end(),"Unknown nested-CMake publication");
        const auto path="app/src/main/assets/content/es/programming/programming_practice_tools/"
            "comprobaciones_repetibles/lesson_"+name+".json";
        require(text(item,L"publicationCommit")=="c0d20f63c846ec7b075b70ee5c403336c78498c6"&&
            text(item,L"publicationPath")==path&&text(item,L"publicationSha256")==pin->sha256,
            "The nested-CMake fixture is not bound to the reviewed publication");
        LessonPair pair;
        pair.before_path=fixture(root,text(item,L"before"));pair.after_path=fixture(root,text(item,L"after"));
        pair.before_bytes=read(pair.before_path);pair.after_bytes=read(pair.after_path);
        require(state::sha256(pair.before_bytes)==pin->sha256,
            "The complete original nested-CMake publication has different bytes");
        pair.before=load_lesson(pair.before_path,"es");pair.after=load_lesson(pair.after_path,"es");
        pair.before_metadata=load_lesson_progress_metadata(pair.before_path,"es");
        pair.after_metadata=load_lesson_progress_metadata(pair.after_path,"es");
        require(pair.before.id=="programming_practice_tools__comprobaciones_repetibles__"+name&&
            pair.after.id==pair.before.id&&pair.before.locale=="es"&&pair.after.locale=="es"&&
            pair.before.pages.size()==pair.after.pages.size()&&
            pair.before.state_layout.lineage==pair.after.state_layout.lineage&&
            pair.before.state_layout.legacy_lineage==pair.after.state_layout.legacy_lineage&&
            pair.before.state_layout.signatures==pair.after.state_layout.signatures&&
            pair.before_metadata.state_layout.signatures==pair.before.state_layout.signatures&&
            pair.after_metadata.state_layout.signatures==pair.after.state_layout.signatures,
            "Nested CMake spacing changed published lineage or progress ownership");
        require(lessons.emplace(name,std::move(pair)).second,"A nested-CMake publication is repeated");
    }
    return lessons;
}

void only_nested_spacing(const CppProjectFiles& before,const CppProjectFiles& after,std::string_view path) {
    require(before.size()==after.size(),"Nested CMake formatting changed the project census");
    std::size_t changed=0;
    for(std::size_t i=0;i<before.size();++i) {
        require(before[i].first==after[i].first,"Nested CMake formatting changed project paths or order");
        if(before[i].first==path) {
            require(after[i].second=="\n"+before[i].second&&
                code_format::equivalent(before[i].second,after[i].second,code_format::Language::cmake),
                "The actual nested CMake program did not retain exactly its reviewed leading LF");
            ++changed;
        } else require(before[i].second==after[i].second,"Nested CMake formatting changed another project file");
    }
    require(changed==1,"The reviewed nested CMake file is missing");
}
void verify_owner(const LessonPair& pair,std::string_view identity,std::string_view path) {
    const auto& before=owner(pair.before,identity);const auto& after=owner(pair.after,identity);
    const auto& old_metadata=owner(pair.before_metadata,identity);
    const auto& new_metadata=owner(pair.after_metadata,identity);
    require(before.type==BlockType::cpp_compiler&&after.type==before.type&&
        before.cmake&&after.cmake==before.cmake&&before.output_check&&after.output_check&&
        !before.output_check->cases.empty()&&
        before.output_check->cases.size()==after.output_check->cases.size()&&
        before.initial_code==after.initial_code&&before.source_files==after.source_files&&
        before.address.page==after.address.page&&before.address.block==after.address.block&&
        before.compiler_workflow_check==after.compiler_workflow_check&&
        exercise_key(pair.before,before)==exercise_key(pair.after,after)&&
        draft_key("programming",pair.before,before)==draft_key("programming",pair.after,after)&&
        reference_objective_frame(pair.before,before)==reference_objective_frame(pair.after,after),
        "Nested CMake formatting changed the first-case goal, stable owner or draft identity");
    // These publications already contain authentic format history. Its
    // canonical original need not equal the current before.project_files.
    require(before.source_format_original_initial_code.value_or(before.initial_code)==
            after.source_format_original_initial_code.value_or(after.initial_code)&&
        before.source_format_original_project_files.value_or(before.project_files)==
            after.source_format_original_project_files.value_or(after.project_files),
        "Nested CMake formatting replaced its earlier canonical source snapshot");
    require(old_metadata.project_files==before.project_files&&new_metadata.project_files==after.project_files&&
        old_metadata.cmake==before.cmake&&new_metadata.cmake==after.cmake&&
        reference_objective_frame(pair.before_metadata,old_metadata)==reference_objective_frame(pair.before,before)&&
        reference_objective_frame(pair.after_metadata,new_metadata)==reference_objective_frame(pair.after,after),
        "The progress loader disagrees with the complete nested-CMake owner");
    only_nested_spacing(before.project_files,after.project_files,path);
}

CppProjectFiles execution_files(const LessonPair& pair,const JsonObject& item,bool formatted) {
    const auto identity=text(item,L"activity"),target=text(item,L"path");
    auto files=owner(formatted?pair.after:pair.before,identity).project_files;
    std::set<std::string> replaced;
    for(const auto& value:item.GetNamedArray(L"executionEdits")) {
        const auto edit=value.GetObject();const auto path=text(edit,L"path");
        require(replaced.insert(path).second,"An execution reference edits the same project path twice");
        const auto& reference=owner(pair.before,text(edit,L"referenceActivity"));
        std::string code;
        if(edit.HasKey(L"referencePath")) {
            require(reference.type==BlockType::cpp_compiler&&text(edit,L"referencePath")==path,
                "An execution file reference changed its original physical role");
            code=source(reference.project_files,path);
        } else {
            require(reference.type==BlockType::code&&!reference.code.empty(),
                "An execution reference is not a complete published code block");
            code=reference.code;
        }
        // The repaired paths in reorganizar/block_0006_0003 are themselves
        // inside the formatted recipe. Keep the same LF after the repair;
        // copying an identical reference to both sides would erase this test.
        if(formatted&&path==target)code.insert(code.begin(),'\n');
        replace(files,path,std::move(code));
    }
    return files;
}

struct SavedDraft {
    state::Draft editor;
    CppProjectFiles files;
    std::string key,project,cmake,selected;
    std::size_t page=0;
};
SavedDraft save_personal_draft(state::StateData& data,const Lesson& lesson,const Block& block,std::string_view path) {
    SavedDraft saved;
    saved.key=draft_key("programming",lesson,block);saved.files=block.project_files;
    saved.selected=path;
    replace(saved.files,path,source(saved.files,path)+"\n# Unpublished personal draft: keep this exact text.\n");
    JsonObject project,cursors,indentation,configuration;JsonArray files,sources;
    std::size_t ordinal=0;
    for(const auto& [name,code]:saved.files) {
        JsonObject file;file.Insert(L"path",JsonValue::CreateStringValue(winrt::to_hstring(name)));
        file.Insert(L"code",JsonValue::CreateStringValue(winrt::to_hstring(code)));files.Append(file);
        cursors.Insert(winrt::to_hstring(name),JsonValue::CreateNumberValue(static_cast<double>(++ordinal)));
        indentation.Insert(winrt::to_hstring(name),JsonValue::CreateStringValue(
            winrt::to_hstring(state::make_indentation_metadata(code,{}))));
    }
    for(const auto& name:history_sources(saved.files))sources.Append(JsonValue::CreateStringValue(winrt::to_hstring(name)));
    project.Insert(L"files",files);project.Insert(L"sources",sources);
    project.Insert(L"selected",JsonValue::CreateStringValue(winrt::to_hstring(saved.selected)));
    project.Insert(L"cursors",cursors);project.Insert(L"indentationV2",indentation);
    configuration.Insert(L"target",JsonValue::CreateStringValue(winrt::to_hstring(block.cmake->target)));
    configuration.Insert(L"buildType",JsonValue::CreateStringValue(winrt::to_hstring(block.cmake->build_type)));
    std::string options;
    for(const auto& [key,value]:block.cmake->options){if(!options.empty())options+='\n';options+=key+'='+value;}
    configuration.Insert(L"optionsText",JsonValue::CreateStringValue(winrt::to_hstring(options)));
    saved.project=winrt::to_string(project.Stringify());saved.cmake=winrt::to_string(configuration.Stringify());
    saved.editor.source=source(saved.files,path);saved.editor.input="  saved personal input\r\n";
    saved.editor.selection_start=3;saved.editor.selection_end=11;saved.editor.has_saved=true;
    saved.editor.indentation_json=state::make_indentation_metadata(saved.editor.source,{});
    state::put_draft(data,saved.key,saved.editor);
    // These are the real CppProjectView sidecars in lesson_view.cpp, including
    // per-file bytes, cursor, indentation, selection and CMake configuration.
    data.preferences["cpp_lesson_drafts"][saved.key+"/project"]=saved.project;
    data.preferences["cpp_lesson_drafts"][saved.key+"/cmake"]=saved.cmake;
    const auto position=stable_block_position(lesson,block.stable_id);
    require(position.has_value(),"The draft owner has no active page");saved.page=position->page;
    state::save_current_page(data,lesson,saved.page);
    return saved;
}
void verify_saved(const state::StateData& data,const LessonPair& pair,const SavedDraft& saved,
        std::string_view identity) {
    state::validate_lesson_state(data,pair.after);
    state::validate_lesson_state(data,pair.after_metadata);
    const auto& current=owner(pair.after,identity);
    const auto draft=state::get_draft(data,draft_key("programming",pair.after,current),current.initial_code,
        current.standard_input.value_or(""));
    require(draft.has_saved&&draft.source==saved.editor.source&&draft.input==saved.editor.input&&
        draft.selection_start==saved.editor.selection_start&&draft.selection_end==saved.editor.selection_end&&
        draft.indentation_json==saved.editor.indentation_json&&
        state::string_value(data,"cpp_lesson_drafts",saved.key+"/project")==saved.project&&
        state::string_value(data,"cpp_lesson_drafts",saved.key+"/cmake")==saved.cmake&&
        state::current_page(data,pair.after)==saved.page,
        "Formatting changed a saved project, input, per-file cursor, selection, indentation or CMake settings");
    const auto stored=JsonObject::Parse(winrt::to_hstring(state::string_value(data,"cpp_lesson_drafts",saved.key+"/project")));
    const auto files=stored.GetNamedArray(L"files");
    require(files.Size()==saved.files.size()&&text(stored,L"selected")==saved.selected,
        "The stored nested-CMake project lost its file census or selected tab");
    for(std::uint32_t i=0;i<files.Size();++i) {
        const auto item=files.GetObjectAt(i);
        require(text(item,L"path")==saved.files[i].first&&text(item,L"code")==saved.files[i].second&&
            stored.GetNamedObject(L"cursors").GetNamedNumber(winrt::to_hstring(saved.files[i].first))==i+1,
            "A saved nested-CMake project file or its cursor was rewritten");
    }
}
void verify_first_receipt(const state::StateData& data,const LessonPair& pair,std::string_view identity) {
    for(const auto* lesson:{&pair.before,&pair.after,&pair.before_metadata,&pair.after_metadata}) {
        const auto& block=owner(*lesson,identity);
        require(compiler_puzzle_completed(data,*lesson,block)&&compiler_puzzle_case_count(data,*lesson,block)==1,
            "Formatting lost the genuine first-case receipt or made an optional target mandatory");
    }
}

CppRunRequest request(const Lesson& lesson,const Block& block,const CppProjectFiles& files,
        const std::filesystem::path& projects) {
    CppRunRequest result;
    result.source_utf8=block.initial_code;result.project_files=files;result.source_files=block.source_files;
    result.cmake=block.cmake;result.stdin_utf8=block.output_check->cases.front().standard_input;
    result.project_key=draft_key("programming",lesson,block);result.projects_directory=projects;
    result.compile_timeout=std::chrono::seconds(90);result.execution_timeout=std::chrono::seconds(8);
    result.action=CppRunRequest::Action::compile_run;
    require(compiler_runtime_request_allowed(block,result),"The authentic CMake execution violates its published objective");
    return result;
}
CppRunResult execute(CppRunRequest& request,std::uint64_t& count,std::string_view label) {
    request.request_id=++count;request.revision=request.request_id;
    std::cout<<"RUN nested CMake "<<count<<": "<<label<<std::endl;
    return run_cpp(request);
}
FreshExecution successful(const Block& block,const CppRunRequest& request,const CppRunResult& result) {
    require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
        (result.configuration.status==CppPhaseStatus::not_started||result.configuration.succeeded())&&
        !result.artifact.empty()&&std::filesystem::is_regular_file(result.artifact),
        "The authentic nested-CMake positive did not compile and execute: "+result.error_utf8+
            result.configuration.stdout_bytes+result.configuration.stderr_bytes+
            result.compilation.stdout_bytes+result.compilation.stderr_bytes);
    auto captured=capture_cpp_execution(request,result);
    require(evaluate_cpp_output_check(*block.output_check,captured,request.revision).verdict==OutputVerdict::correct,
        "The nested-CMake positive disagrees with its published first-case output");
    return captured;
}
void same_execution(const CppRunResult& before,const CppRunResult& after) {
    require(before.execution.stdout_bytes==after.execution.stdout_bytes&&
        before.execution.stderr_bytes==after.execution.stderr_bytes&&before.execution.exit_code==after.execution.exit_code&&
        before.executable==after.executable&&before.target_type==after.target_type&&
        !before.project_fingerprint.empty()&&!after.project_fingerprint.empty()&&
        before.project_fingerprint!=after.project_fingerprint,
        "Nested CMake formatting changed actual results or reused the unformatted project proof");
}
state::StateData reopen(const std::filesystem::path& profile,const state::StateData& prior,
        const LessonPair& pair,const SavedDraft& saved,std::string_view identity) {
    state::StateStore store(profile);auto data=store.load();
    require(data==prior,"The nested-CMake profile changed on physical reopening");
    state::prepare_lesson_state(data,pair.after);
    require(data==prior,"The format-only lesson adoption rewrote historical state");
    verify_saved(data,pair,saved,identity);verify_first_receipt(data,pair,identity);
    return data;
}
void save_and_reopen(const std::filesystem::path& profile,const state::StateData& data,
        const LessonPair& pair,const SavedDraft& saved,std::string_view identity) {
    {state::StateStore store(profile);(void)store.load();store.save(data);}
    (void)reopen(profile,data,pair,saved,identity);
}

void ordinary_pair(const LessonPair& pair,const JsonObject& item,const std::filesystem::path& qa,
        std::size_t index,std::uint64_t& count) {
    const auto identity=text(item,L"activity"),path=text(item,L"path");
    const auto& before=owner(pair.before,identity);const auto& after=owner(pair.after,identity);
    require(!compiler_selective_history_required(pair.before,before),"A History owner entered the ordinary positive loop");
    const auto old_files=execution_files(pair,item,false),new_files=execution_files(pair,item,true);
    only_nested_spacing(old_files,new_files,path);
    const auto profile=qa/("s"+std::to_string(index));
    state::StateData saved;state::prepare_lesson_state(saved,pair.before);
    const auto draft=save_personal_draft(saved,pair.before,before,path);
    {state::StateStore store(profile);require(store.load().preferences.empty(),"The ordinary QA profile is not new");store.save(saved);}
    require(!compiler_puzzle_completed(saved,pair.after,after),"A draft was mistaken for a CMake achievement");
    auto old_request=request(pair.before,before,old_files,qa/"p");
    const auto old_run=execute(old_request,count,identity+" before");
    const auto old_capture=successful(before,old_request,old_run);
    require(record_compiler_puzzle_case(saved,pair.before,before,old_request,old_capture,"genuine original CMake first case"),
        "The genuine unformatted first case did not earn its receipt");
    verify_first_receipt(saved,pair,identity);save_and_reopen(profile,saved,pair,draft,identity);
    auto current=reopen(profile,saved,pair,draft,identity);
    auto new_request=request(pair.after,after,new_files,qa/"p");
    const auto new_run=execute(new_request,count,identity+" formatted");
    const auto new_capture=successful(after,new_request,new_run);same_execution(old_run,new_run);
    state::StateData fresh;state::prepare_lesson_state(fresh,pair.after);
    require(record_compiler_puzzle_case(fresh,pair.after,after,new_request,new_capture,"genuine formatted CMake first case")&&
        compiler_puzzle_completed(fresh,pair.after,after)&&compiler_puzzle_case_count(fresh,pair.after,after)==1,
        "The formatted first case did not suffice in a fresh profile");
    require(record_compiler_puzzle_case(current,pair.after,after,new_request,new_capture,"later formatted CMake run")&&current==saved,
        "Rechecking a formatted project replaced an immutable historical receipt or its draft");
    save_and_reopen(profile,current,pair,draft,identity);
    std::cout<<"PASS nested CMake ordinary pair "<<text(item,L"lesson")<<'/'<<identity
        <<": genuine before/after outputs, first receipt and all six saved files retained\n";
}

void selective_history(const LessonPair& pair,const JsonObject& item,const std::filesystem::path& qa,
        std::size_t index,std::uint64_t& count) {
    const auto identity=text(item,L"activity"),path=text(item,L"path");
    const auto& before=owner(pair.before,identity);const auto& after=owner(pair.after,identity);
    require(identity=="block_0010_0003"&&compiler_selective_history_required(pair.before,before)&&
        compiler_selective_history_required(pair.after,after)&&item.GetNamedArray(L"executionEdits").Size()==0,
        "The genuine selective-History owner or its protected workflow changed");
    const auto profile=qa/("s"+std::to_string(index));
    state::StateData saved;state::prepare_lesson_state(saved,pair.before);
    const auto draft=save_personal_draft(saved,pair.before,before,path);
    {state::StateStore store(profile);require(store.load().preferences.empty(),"The History QA profile is not new");store.save(saved);}
    CppProjectHistory history(profile/"active"/"files"/"cpp_project_history",draft.key);
    const auto recorded=history.record(before.project_files,history_sources(before.project_files),"Original published CMake project");
    CmakeSelectiveRecoveryTrace trace(before);
    require(trace.record(history.recorded_evidence(recorded)),"The real saved revision failed to start selective History");
    auto broken=before.project_files;
    replace(broken,"CMakeLists.txt",source(broken,"CMakeLists.txt")+"# QA: keep this independent draft comment.\n");
    auto defect=source(broken,"componente/src/cajas.cpp");
    constexpr std::string_view right="return (piezas + 3) / 4;",wrong="return piezas / 4;";
    const auto at=defect.find(right);
    require(at!=std::string::npos&&defect.find(right,at+right.size())==std::string::npos,
        "The authentic History formula is missing or ambiguous");
    defect.replace(at,right.size(),wrong);replace(broken,"componente/src/cajas.cpp",std::move(defect));
    auto failed_request=request(pair.before,before,broken,qa/"p");
    const auto failed_run=execute(failed_request,count,identity+" genuine failed History run");
    require(trace.run(failed_request,failed_run)&&trace.stage()==CmakeSelectiveRecoveryTrace::Stage::failed_executed,
        "Selective History did not observe the actual built program and its intended rejection: "+
            failed_run.error_utf8+failed_run.configuration.stderr_bytes+failed_run.compilation.stderr_bytes);
    const auto failed_capture=capture_cpp_execution(failed_request,failed_run);const auto uncredited=saved;
    require(!record_compiler_puzzle_case(saved,pair.before,before,failed_request,failed_capture,"actual intended rejection")&&
        saved==uncredited&&!compiler_puzzle_completed(saved,pair.before,before),
        "The real failed History run granted credit or changed the saved draft");
    require(trace.compare(history.diff_verified("CMakeLists.txt",broken,recorded).evidence)&&
        trace.compare(history.diff_verified("componente/src/cajas.cpp",broken,recorded).evidence),
        "The two authentic on-disk History comparisons did not bind their exact files");
    const auto restored=history.restore_file_verified("componente/src/cajas.cpp",broken,recorded);
    require(trace.restore(restored.evidence)&&source(restored.files,"CMakeLists.txt")==source(broken,"CMakeLists.txt"),
        "Selective restoration changed the independent CMake comment");
    auto old_request=request(pair.before,before,restored.files,qa/"p");
    const auto old_run=execute(old_request,count,identity+" genuine restored History run");
    const auto old_capture=successful(before,old_request,old_run);
    require(trace.run(old_request,old_run)&&trace.completed_by(old_request)&&
        record_compiler_puzzle_case(saved,pair.before,before,old_request,old_capture,"authentic restored first case")&&
        !compiler_puzzle_completed(saved,pair.before,before)&&
        record_compiler_selective_history_completion(saved,pair.before,before,old_request,old_capture,trace),
        "The genuine restore/build/run sequence did not earn its independent History receipt");
    save_and_reopen(profile,saved,pair,draft,identity);
    auto current=reopen(profile,saved,pair,draft,identity);
    auto formatted=restored.files;replace(formatted,path,source(after.project_files,path));
    only_nested_spacing(restored.files,formatted,path);
    auto changed_bytes=old_request;changed_bytes.project_files=formatted;
    require(!trace.completed_by(changed_bytes),
        "The History trace ignored changed project bytes with its original request ID and revision");
    auto new_request=request(pair.after,after,formatted,qa/"p");
    const auto new_run=execute(new_request,count,identity+" formatted History project");
    const auto new_capture=successful(after,new_request,new_run);same_execution(old_run,new_run);
    state::StateData fresh;state::prepare_lesson_state(fresh,pair.after);
    require(record_compiler_puzzle_case(fresh,pair.after,after,new_request,new_capture,"formatted stdout without History")&&
        !compiler_puzzle_completed(fresh,pair.after,after)&&compiler_puzzle_case_count(fresh,pair.after,after)==0,
        "A clean formatted profile gained History credit from stdout alone");
    const auto first_only=fresh;
    require(!record_compiler_selective_history_completion(fresh,pair.after,after,new_request,new_capture,trace)&&fresh==first_only,
        "The formatted project borrowed another execution's completed History trace");
    require(record_compiler_puzzle_case(current,pair.after,after,new_request,new_capture,"later formatted History output")&&
        current==saved,"A later formatted run overwrote genuine historical History evidence");
    require(history.load(recorded.id).files==before.project_files&&history.summaries().size()==1,
        "Formatting replaced the actual recorded revision with the new draft");
    save_and_reopen(profile,current,pair,draft,identity);
    std::cout<<"PASS nested CMake selective History: three genuine runs, both disk comparisons, selected-file restore, "
        "historical receipt retained and clean-profile stdout rejected\n";
}

void negative_loaders(const std::filesystem::path& root,const JsonObject& manifest,const Lessons& lessons) {
    const auto controls=manifest.GetNamedArray(L"controls");
    require(controls.Size()==1,"The filename guard needs its one valid full-lesson control");
    const auto control=controls.GetObjectAt(0);const auto file=fixture(root,text(control,L"file"));
    require(text(control,L"name")=="filename"&&text(control,L"activity")=="qa_nested_cmake_fake_name"&&
        text(control,L"path")=="componente/CMakeLists.txt.bak","The filename control changed its explicit QA scope");
    const auto full=load_lesson(file,"es"),metadata=load_lesson_progress_metadata(file,"es");
    const auto& block=owner(full,text(control,L"activity"));const auto& compact=owner(metadata,block.stable_id);
    const auto& actual=owner(lessons.at(text(control,L"lesson")).after,"block_0003_0004");
    require(block.project_files==compact.project_files&&block.project_files.size()==actual.project_files.size()+1&&
        source(block.project_files,text(control,L"path"))==source(actual.project_files,"componente/CMakeLists.txt"),
        "The valid filename control lost its authentic CMake bytes or metadata parity");
    std::set<std::string> names;
    const auto invalid=manifest.GetNamedArray(L"invalid");
    require(invalid.Size()==4,"The nested-CMake guard census requires four distinct corruptions");
    for(const auto& value:invalid) {
        const auto item=value.GetObject();const auto name=text(item,L"name"),message=text(item,L"message");
        require(names.insert(name).second,"A nested-CMake rejection is duplicated");
        require(message==(name=="path"?"Source formatting renamed a project file":
            "Source formatting modified tokens, literals, comments or preprocessor lines"),
            "A negative fixture stopped checking the intended lexical/path guard");
        for(const bool progress:{false,true}) {
            bool rejected=false;std::string observed;
            try {
                const auto path=fixture(root,text(item,L"file"));
                if(progress)(void)load_lesson_progress_metadata(path,"es");else(void)load_lesson(path,"es");
            }catch(const std::exception& failure){rejected=true;observed=failure.what();}
            require(rejected&&observed.find(message)!=std::string::npos,
                "A nested-CMake corruption missed its specific native guard: "+name+": "+observed);
        }
    }
    require(names==std::set<std::string>{"logic","path","literal","filename"},
        "The negative fixtures omitted a logic, path, literal or filename boundary");
    std::cout<<"PASS nested CMake guards: valid filename control and all four specific rejections in both loaders\n";
}
}

int main(int argc,char** argv) {
    try {
        require(argc==2,"Expected the authentic nested-CMake fixture directory");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        const auto root=std::filesystem::absolute(argv[1]);
        const auto manifest=JsonObject::Parse(winrt::to_hstring(read(fixture(root,"manifest.json"))));
        require(manifest.GetNamedNumber(L"version")==1,"Unsupported nested-CMake fixture contract");
        const auto lessons=load_publications(root,manifest);
        negative_loaders(root,manifest,lessons);
        const auto cases=manifest.GetNamedArray(L"cases");
        require(cases.Size()==expected.size(),"The nested-CMake suite must exercise all ten authentic owners");
        // CMake/MSVC need room for their private TryCompile paths. This new,
        // owned temporary root is never a student profile or a shared cache.
        const auto qa=std::filesystem::temp_directory_path()/
            (L"ncf-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        require(!std::filesystem::exists(qa),"Nested-CMake QA must not reuse an existing profile");
        std::filesystem::create_directories(qa);
        std::set<std::pair<std::string,std::string>> seen;
        std::uint64_t runs=0;std::size_t ordinary=0,histories=0;
        for(std::uint32_t index=0;index<cases.Size();++index) {
            const auto item=cases.GetObjectAt(index);const auto name=text(item,L"lesson"),identity=text(item,L"activity");
            const auto path=text(item,L"path");
            const auto wanted=std::ranges::find_if(expected,[&](const auto& value){
                return value.lesson==name&&value.activity==identity&&value.path==path;});
            require(wanted!=expected.end()&&seen.emplace(name,identity).second,
                "The nested-CMake manifest duplicated or substituted an authenticated owner");
            const auto& pair=lessons.at(name);verify_owner(pair,identity,path);
            if(compiler_selective_history_required(pair.before,owner(pair.before,identity))) {
                ++histories;selective_history(pair,item,qa,index,runs);
            }else {++ordinary;ordinary_pair(pair,item,qa,index,runs);}
        }
        require(seen.size()==10&&ordinary==9&&histories==1&&runs==21,
            "Nested-CMake QA omitted an owner, duplicated a compilation or replaced genuine History evidence");
        for(const auto& [name,pair]:lessons)
            require(read(pair.before_path)==pair.before_bytes&&read(pair.after_path)==pair.after_bytes,
                "The native runner rewrote an authentic curriculum fixture");
        std::cout<<"PASS nested CMake census: ten owners, nine ordinary pairs, one three-run History sequence; "
            "21 real MSVC requests, first-case receipts and ten physical project-draft reopens\n";
        return 0;
    }catch(const winrt::hresult_error& failure){
        std::cerr<<"FAIL nested CMake format: "<<winrt::to_string(failure.message())<<'\n';return 1;
    }catch(const std::exception& failure){
        std::cerr<<"FAIL nested CMake format: "<<failure.what()<<'\n';return 1;
    }
}

#pragma once
#include "core/lesson.hpp"
#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/state_store.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lgaed::windows {

// Evidence from the actual Windows project controls and runner. No persistent
// response is created here, and no UI event, source text or stdout alone can
// certify this curricular task. The caller still owns historical grading.
class CmakeSelectiveRecoveryTrace {
public:
    enum class Stage {
        unrecorded, recorded, failed_built, failed_executed,
        compared_one, compared_both, source_restored, restored_built, verified
    };

    explicit CmakeSelectiveRecoveryTrace(const curriculum::Block& published)
        : initial_(published.project_files), config_(published.cmake.value_or(curriculum::CmakeConfig{})) {
        if (!published.cmake || published.project_files.size()!=6 ||
            config_.target!="comprobar_cajas" || config_.build_type!="Debug")
            throw std::invalid_argument("The U3 CMake recovery trace needs its exact published project");
        for (const auto& [path, content] : initial_) {
            if (!original_.emplace(path,content).second)
                throw std::invalid_argument("The recorded CMake baseline has repeated paths");
        }
        if (!original_.contains(std::string(source_path)) || !original_.contains(std::string(root_path)))
            throw std::invalid_argument("The CMake recovery baseline omits the required files");
        auto original_source=original_.at(std::string(source_path));
        const std::string from(correct_expression), to(wrong_expression);
        auto at=original_source.find(from);
        if (at==std::string::npos || original_source.find(from,at+from.size())!=std::string::npos)
            throw std::invalid_argument("The original library does not have one exact capacity formula");
        original_source.replace(at,from.size(),to);
        failed_source_=std::move(original_source);
    }

    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] bool complete() const noexcept { return stage_==Stage::verified; }
    [[nodiscard]] bool completed_by(const CppRunRequest& request) const {
        return complete() && (request.action==CppRunRequest::Action::execute||
                request.action==CppRunRequest::Action::compile_run) &&
            request.request_id==verified_request_id_ &&
            request.revision==restored_revision_ &&
            request.project_key==project_key_ &&
            request.projects_directory==projects_directory_ &&
            request.cmake==std::optional<curriculum::CmakeConfig>{config_} &&
            curriculum::cpp_project_frame(request.project_files,request.source_files,request.cmake)
                ==verified_project_frame_;
    }
    // A subsequent edit cannot reuse a finished session to grade a new draft.
    void invalidate() noexcept {
        stage_=Stage::unrecorded;revision_id_.clear();failed_root_.clear();
        project_key_.clear();projects_directory_.clear();
        failed_fingerprint_.clear();restored_fingerprint_.clear();
        failed_revision_=restored_revision_=failed_request_id_=verified_request_id_=0;
        verified_project_frame_.clear();compared_=0;
    }

    // The source of each event is the real record/compare/restore action.
    // Readiness also authenticates the entire first-source census.
    bool record(const CppHistoryRecordEvidence& event) {
        if (!digest_id(event.revision_id) || event.recorded_file_count!=original_.size() ||
            event.file_sha256.size()!=original_.size()) return false;
        for (const auto& [path,source] : original_) {
            const auto found=event.file_sha256.find(path);
            if (found==event.file_sha256.end()||found->second!=state::sha256(source))return false;
        }
        invalidate();
        revision_id_=event.revision_id;
        stage_=Stage::recorded;
        return true;
    }

    bool compare(const CppHistoryComparisonEvidence& event) {
        if ((stage_!=Stage::failed_executed && stage_!=Stage::compared_one) ||
            event.revision_id!=revision_id_ || !event.changed ||
            event.added_lines+event.removed_lines==0) return false;
        unsigned bit=0;
        if (event.path==root_path)bit=1;
        else if (event.path==source_path)bit=2;
        else return false;
        if ((compared_&bit)!=0) return false;
        const auto expected_baseline=state::sha256(original_.at(event.path));
        const auto expected_draft=state::sha256(event.path==root_path?failed_root_:failed_source_);
        if (event.revision_sha256!=expected_baseline || event.draft_sha256!=expected_draft)return false;
        compared_|=bit;
        stage_=compared_==3?Stage::compared_both:Stage::compared_one;
        return true;
    }

    bool restore(const CppHistoryRecoveryEvidence& event) {
        if (stage_!=Stage::compared_both || event.revision_id!=revision_id_ ||
            event.path!=source_path || !event.changed ||
            event.preserved_other_files!=original_.size()-1 ||
            event.before_sha256!=state::sha256(failed_source_) ||
            event.after_sha256!=state::sha256(original_.at(std::string(source_path))))
            return false;
        stage_=Stage::source_restored;
        return true;
    }

    // The app passes only current-revision results from its own run_cpp worker.
    // A wrong/stale action cannot advance, including a recycled stdout.
    bool run(const CppRunRequest& request,const CppRunResult& result) {
        if (!common(request,result)) return false;
        if (stage_!=Stage::recorded&&stage_!=Stage::failed_built &&
            stage_!=Stage::source_restored&&stage_!=Stage::restored_built)return false;
        const auto wanted=stage_==Stage::recorded||stage_==Stage::failed_built
            ? Mode::broken : Mode::restored;
        std::string current_root;
        if (!project_snapshot(request.project_files,wanted,current_root))return false;
        if ((stage_==Stage::failed_built||stage_==Stage::source_restored||
             stage_==Stage::restored_built)&&current_root!=failed_root_)return false;
        if (stage_==Stage::source_restored||stage_==Stage::restored_built)
            if (request.project_key!=project_key_ ||
                request.projects_directory!=projects_directory_ ||
                request.revision<=failed_revision_)return false;

        const auto built_now=[&]{
            return result.compilation.succeeded() &&
                result.target_type=="EXECUTABLE" && !result.artifact.empty();
        };
        const auto completed_now=[&]{
            return built_now() &&
                result.executable==config_.target &&
                result.execution.status==CppPhaseStatus::completed &&
                result.execution.exit_code==std::optional<std::uint32_t>{
                    wanted==Mode::broken?1u:0u} &&
                result.execution.stderr_bytes.empty() &&
                crlf_to_lf(result.execution.stdout_bytes)==oracle(wanted);
        };
        // The actual checked CMake editor has a COMBINED "Compile, run and
        // check" button. It is a real build followed by a real execution,
        // so both transitions can belong to the SAME captured request.
        const bool combined=request.action==CppRunRequest::Action::compile_run;
        if (stage_==Stage::recorded || stage_==Stage::source_restored) {
            if (request.action!=CppRunRequest::Action::build&&!combined)return false;
            if (!built_now()||(combined&&!completed_now()))return false;
            if (stage_==Stage::recorded) {
                project_key_=request.project_key;projects_directory_=request.projects_directory;
                failed_root_=std::move(current_root);
                failed_revision_=request.revision;
                failed_fingerprint_=result.project_fingerprint;
                if(combined)failed_request_id_=request.request_id;
                stage_=combined?Stage::failed_executed:Stage::failed_built;
            } else {
                if (result.project_fingerprint==failed_fingerprint_ ||
                    request.request_id==failed_request_id_)return false;
                restored_revision_=request.revision;
                restored_fingerprint_=result.project_fingerprint;
                if(combined) {
                    verified_request_id_=request.request_id;
                    verified_project_frame_=curriculum::cpp_project_frame(
                        request.project_files,request.source_files,request.cmake);
                }
                stage_=combined?Stage::verified:Stage::restored_built;
            }
            return true;
        }
        // Explicit Build followed by either Execute or the app's combined
        // checked action remains valid. Neither a build-only result nor a
        // replay of the old run can complete the learner's restoration.
        if (request.action!=CppRunRequest::Action::execute&&!combined ||
            !completed_now() ||
            result.project_fingerprint!=(wanted==Mode::broken?failed_fingerprint_:restored_fingerprint_))
            return false;
        if (stage_==Stage::failed_built) {
            if (request.revision!=failed_revision_)return false;
            failed_request_id_=request.request_id;
            stage_=Stage::failed_executed;
        } else {
            if (request.revision!=restored_revision_ || request.request_id==failed_request_id_)
                return false;
            verified_request_id_=request.request_id;
            verified_project_frame_=curriculum::cpp_project_frame(
                request.project_files,request.source_files,request.cmake);
            stage_=Stage::verified;
        }
        return true;
    }

private:
    enum class Mode{broken,restored};
    static constexpr std::string_view root_path="CMakeLists.txt";
    static constexpr std::string_view source_path="componente/src/cajas.cpp";
    static constexpr std::string_view correct_expression="return (piezas + 3) / 4;";
    static constexpr std::string_view wrong_expression="return piezas / 4;";

    static bool digest_id(std::string_view id) {
        if(id.size()!=64)return false;
        for(char c:id)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
        return true;
    }
    static std::string crlf_to_lf(std::string_view text) {
        std::string result;result.reserve(text.size());
        for(std::size_t i=0;i<text.size();++i) {
            if(text[i]=='\r') {
                if(i+1>=text.size()||text[i+1]!='\n')return {};
                continue;
            }
            result.push_back(text[i]);
        }
        return result;
    }
    static std::string oracle(Mode mode) {
        constexpr std::array<std::pair<int,int>,7> cases{{
            {0,0},{4,1},{8,2},{20,5},{1,1},{5,2},{17,5}}};
        std::string result;
        bool all=true;
        for(const auto [pieces,expected]:cases) {
            const int actual=mode==Mode::broken?pieces/4:(pieces+3)/4;
            const bool correct=actual==expected;all&=correct;
            result+="piezas="+std::to_string(pieces)+" esperado="+std::to_string(expected)
                +" obtenido="+std::to_string(actual)+(correct?" OK\n":" FALLO\n");
        }
        result+=all?"Bateria: OK\n":"Bateria: FALLO\n";
        return result;
    }
    static bool only_added_cmake_comments(std::string_view baseline,std::string_view revised) {
        // Accept free-standing or trailing inline CMake comments. Every
        // original command remains byte-identical and in the same order;
        // adding executable commands or omitting a baseline line is invalid.
        std::size_t old=0,current=0;
        bool has_comment=false;
        while(current<revised.size()){
            const auto newline=revised.find('\n',current);
            const auto end=newline==std::string_view::npos?revised.size():newline+1;
            const auto row=revised.substr(current,end-current);
            const auto oldline=baseline.find('\n',old);
            const auto oldend=oldline==std::string_view::npos?baseline.size():oldline+1;
            const auto original=old<baseline.size()?baseline.substr(old,oldend-old):std::string_view{};
            if(old<baseline.size()&&row==original){
                old=oldend;
            }else{
                bool inline_comment=false;
                if(old<baseline.size()){
                    auto command=original;
                    if(command.ends_with('\n'))command.remove_suffix(1);
                    if(command.ends_with('\r'))command.remove_suffix(1);
                    if(row.starts_with(command)){
                        auto tail=row.substr(command.size());
                        while(!tail.empty()&&(tail.front()==' '||tail.front()=='\t'))
                            tail.remove_prefix(1);
                        if(tail.starts_with('#')){
                            old=oldend;has_comment=true;inline_comment=true;
                        }
                    }
                }
                if(!inline_comment){
                    auto extra=row;
                    while(!extra.empty()&&(extra.front()==' '||extra.front()=='\t'))
                        extra.remove_prefix(1);
                    if(!extra.empty()&&extra.front()=='#')has_comment=true;
                    else if(extra.find_first_not_of(" \t\r\n")!=std::string_view::npos)
                        return false;
                }
            }
            current=end;
        }
        return old==baseline.size()&&has_comment;
    }
    bool project_snapshot(const curriculum::CppProjectFiles& files,Mode mode,
                          std::string& current_root) const {
        if (files.size()!=original_.size())return false;
        std::map<std::string,std::string> actual;
        for(const auto& [path,content]:files)if(!actual.emplace(path,content).second)return false;
        if (actual.size()!=original_.size())return false;
        for(const auto& [path,source]:original_) {
            const auto found=actual.find(path);if(found==actual.end())return false;
            if(path==source_path) {
                if (found->second!=(mode==Mode::broken?failed_source_:source))return false;
            } else if(path==root_path) {
                if (!only_added_cmake_comments(source,found->second))return false;
                current_root=found->second;
            } else if(found->second!=source)return false;
        }
        return true;
    }
    bool common(const CppRunRequest& request,const CppRunResult& result) const {
        return request.cmake && *request.cmake==config_ &&
            request.backend==CppRunRequest::Backend::native_msvc &&
            request.source_files.empty() && !request.diagnostic &&
            request.stdin_utf8.empty() && !request.project_key.empty() &&
            request.request_id!=0 &&
            request.request_id==result.request_id &&
            request.revision==result.revision &&
            result.error_utf8.empty() && result.project_fingerprint.size()==64 &&
            result.configuration_fingerprint.size()==64 &&
            !result.run_directory.empty() &&
            (result.configuration.status==CppPhaseStatus::not_started||
                result.configuration.succeeded());
    }
    curriculum::CppProjectFiles initial_;
    curriculum::CmakeConfig config_;
    std::map<std::string,std::string> original_;
    Stage stage_=Stage::unrecorded;
    std::string revision_id_,failed_source_,failed_root_;
    std::string project_key_,failed_fingerprint_,restored_fingerprint_;
    std::filesystem::path projects_directory_;
    std::uint64_t failed_revision_=0,restored_revision_=0,failed_request_id_=0,verified_request_id_=0;
    std::string verified_project_frame_;
    unsigned compared_=0;
};

} // namespace lgaed::windows

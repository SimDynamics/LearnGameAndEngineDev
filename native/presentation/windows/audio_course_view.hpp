#pragma once
#include "core/lesson_state.hpp"
#include "platform/windows/narration_worker.hpp"
#include <windows.h>
#include <filesystem>
#include <functional>
#include <map>
#include <compare>
#include <optional>
#include <memory>
#include <set>
#include <stop_token>
#include <string>
#include <vector>

namespace lgaed::presentation {
struct CourseAudioProfile {
    std::string locale="es",language="es",ui_language="es",engine="KOKORO";
    std::map<std::string,std::string> voices;
    bool enabled=true;
    bool operator==(const CourseAudioProfile&)const=default;
};
struct CourseAudioRef {
    std::string route,course;
    auto operator<=>(const CourseAudioRef&)const=default;
};
struct CourseAudioFile {
    std::filesystem::path path;
    std::string key,sha256;
    std::uint64_t bytes=0,file_id=0;
    DWORD volume=0;
};
struct CourseAudioSource {
    std::filesystem::path path;
    std::string sha256;
    std::uintmax_t bytes=0;
    std::filesystem::file_time_type modified{};
    bool exists=false;
};
struct CourseAudioCoverage {
    std::size_t expected=0,generated=0,files=0;
    std::uint64_t bytes=0;
    [[nodiscard]] std::optional<unsigned> percentage()const;
};
struct CourseAudioCourse {
    CourseAudioRef owner;
    std::size_t number=0;
    std::string title;
    std::set<std::string> expected;
    CourseAudioCoverage coverage;
};
struct CourseAudioReport {
    CourseAudioProfile profile;
    std::vector<CourseAudioCourse> courses;
    std::map<std::string,std::vector<CourseAudioFile>> inventory;
    std::vector<CourseAudioSource> sources;
    bool complete=false;
};
using CourseAudioPlanner=std::function<std::string(std::string_view,std::size_t,std::string_view,std::string_view,std::string_view,std::stop_token,std::string_view)>;
// Pure planning and read-only, verified file inventory. Never synthesizes audio,
// touches access timestamps, creates cache files, or changes any preference.
[[nodiscard]] CourseAudioReport scan_course_audio(const std::filesystem::path& content,
    const std::filesystem::path& cache,CourseAudioProfile profile,std::string_view route,
    bool complete,const CourseAudioPlanner&,std::stop_token={});
struct CourseAudioRemovalPreview {
    CourseAudioReport report;
    CourseAudioRef target;
    std::vector<CourseAudioFile> exclusive;
    std::size_t shared_files=0;
    std::uint64_t removable_bytes=0;
};
[[nodiscard]] CourseAudioRemovalPreview preview_course_audio_removal(CourseAudioReport,CourseAudioRef);
enum class CourseAudioRemovalStatus {completed,busy,changed,cancelled};
struct CourseAudioRemovalResult {
    CourseAudioRemovalStatus status=CourseAudioRemovalStatus::cancelled;
    std::size_t deleted=0,failed=0;
    std::uint64_t bytes=0;
};
// The caller must already have shown the exact preview and obtained explicit
// confirmation. permit() represents the existing exclusive idle-maintenance
// lease, retained throughout this call; no synthesis or playback is cancelled.
[[nodiscard]] CourseAudioRemovalResult remove_confirmed_course_audio(const CourseAudioRemovalPreview&,
    const CourseAudioProfile& current,const std::function<bool()>& permit,std::stop_token={});
struct CourseAudioAccess {
    std::filesystem::path content,cache;
    std::shared_ptr<windows::NarrationWorker> worker;
    std::function<CourseAudioProfile()> profile;
    std::function<bool(const CourseAudioRef&)> paused;
    std::function<void(const CourseAudioRef&,bool)> pause;
    // Called on the UI thread. Empty means busy. The owning app restores its
    // temporarily disabled lesson/settings views when this token is released.
    std::function<std::shared_ptr<void>()> idle_maintenance;
};
[[nodiscard]] HWND open_course_audio_window(HWND owner,std::string route,CourseAudioAccess);
}

#pragma once
#include "core/profile.hpp"
#include "core/catalog.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/audio_generation.hpp"
#include "presentation/windows/lesson_reader.hpp"
#include "presentation/windows/lesson_view.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace lgaed::presentation {
// An explicit local acceptance surface on the actual graphical executable.
// This adapter is unreachable without Codex + qa-instance and an existing
// C:\LGAED-QA directory. It never accepts a personal state/cache/content path.
struct AppAudioRevisionQaConfig { std::filesystem::path root,content; };
std::optional<AppAudioRevisionQaConfig> parse_app_audio_revision_qa_arguments(
    std::vector<std::wstring_view>& arguments);
void bind_app_audio_revision_qa_environment(const AppAudioRevisionQaConfig&,WindowsEnvironment&);
struct AppAudioRevisionQaAccess {
    HWND application=nullptr;
    std::function<HWND()> lesson;
    std::function<LessonReader*()> reader;
    std::function<windows::AudioGeneration*()> generation;
    std::function<unsigned()> available_processors;
    std::function<void(const std::filesystem::path&,const curriculum::CatalogLocation&)> open;
    std::function<void(const std::function<void(state::StateData&)>&)> preferences;
};
class AppAudioRevisionQa {
public:
    AppAudioRevisionQa(AppAudioRevisionQaConfig,WindowsEnvironment,AppAudioRevisionQaAccess);
    ~AppAudioRevisionQa();
    void tick() noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}

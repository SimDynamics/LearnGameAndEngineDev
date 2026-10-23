#pragma once
#include "core/lesson_state.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::windows {
struct KokoroVoicePreference {
    std::string id, language;
};
// The generated runtime catalogue is the source of supported voice identities.
// These read-only helpers never reset preferences or require model files merely
// to retain the identity of an already compatible recording.
[[nodiscard]] std::vector<KokoroVoicePreference> kokoro_voice_catalog(const std::filesystem::path& runtime);
[[nodiscard]] std::string selected_narration_language(const state::StateData&, std::string_view fallback = "es");
[[nodiscard]] std::string selected_kokoro_voice(const state::StateData&, std::string_view language,
                                              const std::filesystem::path& runtime);
}

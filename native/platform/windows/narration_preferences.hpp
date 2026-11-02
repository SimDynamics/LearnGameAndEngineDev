#pragma once
#include "core/lesson_state.hpp"
#include <filesystem>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace lgaed::windows {
struct SystemVoice;
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
// Same policy as the foreground reader: retain an explicit token unchanged;
// otherwise use the first installed voice for this explicit language. An empty
// result means no compatible installed voice, never a different-language fallback.
[[nodiscard]] std::string selected_system_voice(const state::StateData&,std::string_view language,
                                               std::span<const SystemVoice> installed);
}

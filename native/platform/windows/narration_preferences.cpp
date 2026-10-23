#include "platform/windows/narration_preferences.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>

namespace lgaed::windows {
namespace {
struct CachedCatalog {
    std::filesystem::file_time_type stamp;
    std::uintmax_t size;
    std::shared_ptr<const std::vector<KokoroVoicePreference>> voices;
};
std::shared_ptr<const std::vector<KokoroVoicePreference>> catalog(const std::filesystem::path& runtime) {
    ensure_winrt_apartment();
    try {
        const auto path = state::native_io_path(runtime);
        const auto stamp = std::filesystem::last_write_time(path);
        const auto size = std::filesystem::file_size(path);
        static std::mutex mutex;
        static std::map<std::filesystem::path,CachedCatalog> cached;
        {
            const std::lock_guard lock(mutex);
            const auto found = cached.find(path);
            if (found != cached.end() && found->second.stamp == stamp && found->second.size == size)
                return found->second.voices;
        }
        const auto bytes = state::read_bytes(path,1024 * 1024);
        const auto json = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(
            std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
        auto voices = std::make_shared<std::vector<KokoroVoicePreference>>();
        std::set<std::string> identities;
        for (const auto& entry : json.GetNamedArray(L"voices")) {
            const auto voice = entry.GetObject();
            const auto id = winrt::to_string(voice.GetNamedString(L"id"));
            const auto lang = narration_language_tag(winrt::to_string(voice.GetNamedString(L"language")));
            const auto hash = winrt::to_string(voice.GetNamedString(L"sha256"));
            if (id.empty() || !std::ranges::all_of(id,[](char value) {
                    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
                }) || !lang || !identities.insert(id).second || hash.size() != 64 ||
                !std::ranges::all_of(hash,[](char value) {
                    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
                })) throw std::runtime_error("The original Kokoro voice catalogue is invalid");
            voices->push_back({id,*lang});
        }
        if (voices->empty()) throw std::runtime_error("The original Kokoro voice catalogue is empty");
        if (std::filesystem::last_write_time(path) != stamp || std::filesystem::file_size(path) != size)
            throw std::runtime_error("The Kokoro voice catalogue changed while being read");
        {
            const std::lock_guard lock(mutex);
            // Runtime metadata only. No voices, model weights or student state are copied.
            if (cached.size() >= 8) cached.erase(cached.begin());
            cached.insert_or_assign(path,CachedCatalog{stamp,size,voices});
        }
        return voices;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Cannot read the original Kokoro voice catalogue: " + winrt::to_string(error.message()));
    } catch (const std::exception& error) {
        throw std::runtime_error("Cannot read the original Kokoro voice catalogue: " + std::string(error.what()));
    }
}
}
std::vector<KokoroVoicePreference> kokoro_voice_catalog(const std::filesystem::path& runtime) {
    return *catalog(runtime);
}
std::string selected_narration_language(const state::StateData& data,std::string_view fallback) {
    const auto value = state::string_value(data,"lesson_narration_settings","language");
    return narration_language_tag(value).value_or(narration_language_tag(fallback).value_or("es"));
}
std::string selected_kokoro_voice(const state::StateData& data,std::string_view language,
                                const std::filesystem::path& runtime) {
    const auto requested = narration_language_tag(language);
    if (!requested) throw std::invalid_argument("Unsupported Kokoro narration language");
    const auto voices = catalog(runtime);
    const auto supports = [&](std::string_view candidate) {
        return std::ranges::any_of(*voices,[&](const auto& item) { return item.id == candidate && item.language == *requested; });
    };
    const auto saved = state::string_value(data,"lesson_narration_settings","kokoro_voice:" + *requested);
    if (supports(saved)) return saved;
    const auto legacy = state::string_value(data,"lesson_narration_settings","kokoro_voice");
    if (supports(legacy)) return legacy;
    const auto preferred = *requested == "es" ? "ef_dora" : *requested == "fr" ? "ff_siwis" :
        *requested == "en-GB" ? "bf_emma" : "af_heart";
    if (!supports(preferred)) throw std::runtime_error("The original default voice is missing from the Kokoro catalogue");
    return preferred;
}
}

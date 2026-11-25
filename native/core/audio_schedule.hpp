#pragma once
#include "core/catalog.hpp"
#include <optional>

namespace lgaed::narration {
struct CurriculumAudioPage {
    curriculum::CatalogLocation location;
    std::filesystem::path asset;
    std::size_t page = 0;
    std::string language;
    bool operator==(const CurriculumAudioPage&) const = default;
};
// A concrete studied page reserves its audio for foreground work. Following
// pages in its course are checked first in curricular order, then the remaining
// inventory follows the persisted route turn. Cached pages consume no turn.
[[nodiscard]] std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage>,
    const curriculum::CatalogLocation&,std::optional<std::size_t> current_page,
    std::string_view next_route="programming");
}

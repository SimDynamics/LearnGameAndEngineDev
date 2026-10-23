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
// Manifest order is retained within every priority. The turn advances only
// after a page actually generated audio; cache hits never starve the other route.
[[nodiscard]] std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage>,
    const curriculum::CatalogLocation&,std::optional<std::size_t> current_page,
    std::string_view next_route="programming");
}

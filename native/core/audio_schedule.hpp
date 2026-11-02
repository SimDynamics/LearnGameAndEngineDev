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
// Background work always starts at the beginning of each route's manifest,
// independent of the open page, course or Options screen. The producer advances
// the turn only after actual generation; cached pages do not consume that turn.
// The location/page arguments remain source compatible. Interactive preparation
// has its own worker reservation and never reorders this background inventory.
[[nodiscard]] std::vector<std::size_t> audio_schedule(std::span<const CurriculumAudioPage>,
    const curriculum::CatalogLocation&,std::optional<std::size_t> current_page,
    std::string_view next_route="programming");
}

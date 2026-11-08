#include "core/positional_reading_archive.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using lgaed::curriculum::PositionalReadingBlock;
using lgaed::curriculum::positional_reading_regions;
constexpr auto title_index = std::numeric_limits<std::size_t>::max();

// Plain input records exercise the production template without loading a
// window, substituting an importer, or creating progress/listening receipts.
struct Region {
    std::size_t block_index;
    bool required_compiler = false, solved = false, correct = false;
    std::string source, anchor, opaque;
    bool operator==(const Region&) const = default;
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<Region> visible_regions() {
    // Current U11L21 page order is original [0,1,3,4,2]. The compiler
    // therefore has visible index 4; its old following paragraphs have 2/3.
    return {
        {title_index, true, true, false, "lesson title", "lesson/title", "title metadata"},
        {0, false, true, false, "original heading", "block_0005_0000/0", "heading metadata"},
        {1, false, true, false, "original instructions, first segment", "block_0005_0001/0", "source A"},
        {1, false, true, false, "original instructions, second segment", "block_0005_0001/1", "source B"},
        {2, true, false, false, "original following paragraph", "block_0005_0003/0", "after editor A"},
        {3, true, false, false, "original criteria", "block_0005_0004/0", "after editor B"},
        {4, false, false, false, "original compiler region", "block_0005_0002/0", "unchanged compiler metadata"},
    };
}

std::vector<PositionalReadingBlock> original_order(bool completed) {
    return {{0, false, false}, {1, false, false}, {4, true, completed},
            {2, false, false}, {3, false, false}};
}

void verify_script(bool completed) {
    const auto source = visible_regions();
    const auto unchanged = source;
    const auto owners = original_order(completed);
    const auto result = positional_reading_regions(source, owners);
    require(source == unchanged, "Archiving changed the input script or its opaque metadata");
    const std::vector<std::size_t> expected_source_positions = completed
        ? std::vector<std::size_t>{0, 1, 2, 3, 6, 4, 5}
        : std::vector<std::size_t>{0, 1, 2, 3, 6};
    require(result.size() == expected_source_positions.size(), "Historical visibility crossed the compiler gate");
    for (std::size_t index = 0; index < result.size(); ++index) {
        auto expected = source[expected_source_positions[index]];
        if (expected.block_index != title_index) {
            expected.required_compiler = expected.block_index == 4;
            if (expected.block_index == 4) expected.solved = completed;
        }
        require(result[index] == expected,
            "Historical ordering changed source, anchor, verdict, metadata, title or segment order");
    }
    const auto compiler = std::ranges::find(result, std::size_t{4}, &Region::block_index);
    require(compiler != result.end() && compiler->required_compiler && compiler->solved == completed,
        "The archive did not use the supplied real compiler-completion verdict");
    require(!compiler->correct, "Archiving manufactured a correct response from a completion flag");
    if (!completed) require(result.back().block_index == 4,
        "An unsuccessful original compiler exposed its following paragraphs");
}

void rejects_without_partial_result(const std::vector<Region>& source,
        const std::vector<PositionalReadingBlock>& owners) {
    const auto unchanged = source;
    const std::vector<Region> sentinel{{title_index, false, false, false,
        "previous complete archive", "retained", "retained"}};
    auto output = sentinel;
    bool rejected = false;
    try { output = positional_reading_regions(source, owners); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "An incomplete, duplicated or foreign archive was accepted");
    require(output == sentinel && source == unchanged,
        "A failed archive mixed a partial result into existing data");
}

void invalid_archives_fail_closed() {
    const auto source = visible_regions();
    const auto owners = original_order(true);
    rejects_without_partial_result(source, {});
    rejects_without_partial_result({}, owners);
    auto duplicated = owners;
    duplicated.push_back(owners.front());
    rejects_without_partial_result(source, duplicated);
    auto absent_regions = source;
    std::erase_if(absent_regions, [](const auto& region) { return region.block_index == 2; });
    rejects_without_partial_result(absent_regions, owners);
    auto absent_owner = owners;
    absent_owner.pop_back();
    rejects_without_partial_result(source, absent_owner);
    auto foreign_region = source;
    foreign_region.push_back({9, false, false, false, "foreign source", "foreign", "foreign"});
    rejects_without_partial_result(foreign_region, owners);
    auto foreign_owner = owners;
    foreign_owner.back().current_index = 9;
    rejects_without_partial_result(source, foreign_owner);
    // Validate the entire declared archive before cutting at a failed editor;
    // missing data after that gate cannot be mistaken for an authentic prefix.
    rejects_without_partial_result(absent_regions, original_order(false));
}
}

int main() try {
    verify_script(false);
    verify_script(true);
    invalid_archives_fail_closed();
    std::cout << "PASS positional reading archive: original order, unsuccessful/successful compiler visibility, "
        "unchanged titles/source/anchors/verdicts, multi-segment owners and eight invalid archives. "
        "Production helper only; no import, UI, audio or progress receipts.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL positional reading archive: " << error.what() << '\n';
    return 1;
}

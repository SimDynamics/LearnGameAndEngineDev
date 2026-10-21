#pragma once

#include "core/lesson_state.hpp"
#include <filesystem>
#include <optional>

namespace lgaed::curriculum { struct Lesson; struct Block; }

namespace lgaed::state {

// Each instance belongs to one private profile. Concurrent stale writes fail closed.
class StateStore {
public:
    explicit StateStore(std::filesystem::path profile_root);
    [[nodiscard]] StateData load();
    void save(const StateData&);
    [[nodiscard]] bool empty() const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }
private:
    std::filesystem::path root_;
    std::optional<std::string> expected_hash_;
    bool loaded_ = false;
};

[[nodiscard]] std::optional<Response> get_response(const StateData&, std::string_view exercise_key);
void put_response(StateData&, std::string_view exercise_key, const Response&);
[[nodiscard]] bool response_reviewed(const Response&, const curriculum::Block&);
void mark_response_reviewed(Response&, const curriculum::Block&);
void clear_response_review(Response&) noexcept;
[[nodiscard]] Draft get_draft(const StateData&, std::string_view draft_key,
                              std::string_view initial_code, std::string_view initial_input = {});
void put_draft(StateData&, std::string_view draft_key, const Draft&);
// Validate immutable Android family ownership before reading positional state.
// Metadata is seeded once and old keys remain untouched, exactly as on Android.
void prepare_lesson_state(StateData&, const curriculum::Lesson&);
// Read-only compatibility check. Never turns a partial/incorrect answer into a submission.
void validate_lesson_state(const StateData&, const curriculum::Lesson&,
                           std::string_view route_id = "programming");
void save_current_page(StateData&, const curriculum::Lesson&, std::size_t display_page);
[[nodiscard]] std::size_t current_page(const StateData&, const curriculum::Lesson&);
// UTF-16 offsets, matching both Android Spannable and Win32 edit selections.
[[nodiscard]] std::vector<std::int64_t> indentation_units(std::string_view source,
                                                       std::string_view metadata);
[[nodiscard]] std::string make_indentation_metadata(std::string_view source,
                                                  const std::vector<std::int64_t>& starts);
[[nodiscard]] std::string sha256(std::span<const std::uint8_t>);
[[nodiscard]] std::string sha256(std::string_view);
[[nodiscard]] std::vector<std::uint8_t> read_bytes(const std::filesystem::path&,
                                                std::size_t limit = 128 * 1024 * 1024);
void require_safe_path(const std::filesystem::path&);
// Extended Win32 spelling is I/O only; stored IDs and relative paths are unchanged.
[[nodiscard]] std::filesystem::path native_io_path(const std::filesystem::path&);

} // namespace lgaed::state

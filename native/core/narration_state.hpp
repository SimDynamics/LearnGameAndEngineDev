#pragma once
#include "core/lesson_state.hpp"
#include <optional>

namespace lgaed::narration {
inline constexpr const char* resume_domain="lesson_narration_resume_bounded_v1";
struct Position {
    std::int64_t segment=0, milliseconds=0, heard_through=0;
    bool finished=false;
    bool operator==(const Position&)const=default;
};
struct Bookmark { std::string key,stable;Position position;bool operator==(const Bookmark&)const=default; };
struct History {
    std::vector<Bookmark> entries;
    std::string active_key,active_owner;
    bool operator==(const History&)const=default;
};
// Same bounded metadata and policy as Android's NarrationResumePolicy. Players
// are deliberately absent. Finishing retains a slot and releases ownership.
[[nodiscard]] History read_history(const state::StateData&);
void write_history(state::StateData&,const History&);
[[nodiscard]] std::optional<Position> restore(const History&,std::string_view key,std::size_t segments);
[[nodiscard]] History begin(History,std::string key,std::string owner,Position,std::string stable={});
[[nodiscard]] History checkpoint(History,std::string_view key,std::string_view owner,Position);
// A seek changes visual position, never evidence that feedback was listened to.
[[nodiscard]] Position seek(Position,std::int64_t segment,std::int64_t milliseconds);
}

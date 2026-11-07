#pragma once
#include <string_view>

namespace lgaed::windows::bibliography {
// Recognition only: neither string is rewritten. Mirrors the preservation
// predicate in scripts/curriculum/bibliography_revision.py.
[[nodiscard]] bool only_parenthetical_locator_removal(std::string_view before,std::string_view after);
}

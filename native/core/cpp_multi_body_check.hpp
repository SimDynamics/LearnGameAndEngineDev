#pragma once

#include "core/code_format_compat.hpp"
#include "core/cpp_member_body_check.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Shared source-preservation gate for a small, author-declared set of C++
// function bodies. Works with qualified member definitions AND free functions.
// It is not an evaluator: semantic contracts must still be compiled and run.
struct CppEditableBody {
    std::string signature; // Full signature, excluding the opening brace.
    bool qualified = false;
    bool operator==(const CppEditableBody&) const = default;
};

struct CppMultiBodyPolicy {
    std::string original_source;
    std::vector<CppEditableBody> bodies;
    bool operator==(const CppMultiBodyPolicy&) const = default;
};

namespace cpp_multi_body_detail {

inline constexpr std::size_t maximum_source = 4 * 1024 * 1024;
inline constexpr std::size_t maximum_bodies = 8;

inline bool valid_source(std::string_view code) {
    return !code.empty() && code.size() <= maximum_source &&
           code.find('\0') == code.npos &&
           !code_format::physical_splice(code);
}

inline std::optional<CppMemberBodySpan> find_body(
        std::string_view code, const CppEditableBody& declared) {
    const auto signature = std::string_view(declared.signature);
    if (!valid_source(code) || signature.empty() ||
        signature.size() > 512 || signature.find('\n') != signature.npos ||
        signature.find('\r') != signature.npos ||
        signature.find('\0') != signature.npos ||
        signature.find('(') == signature.npos ||
        signature.find(')') == signature.npos ||
        (declared.qualified && signature.find("::") == signature.npos))
        return {};
    const auto at = code.find(signature);
    if (at == code.npos || code.find(signature, at + 1) != code.npos)
        return {};
    const auto line_break = code.rfind('\n', at);
    const auto line_start = line_break == code.npos ? 0 : line_break + 1;
    for (auto i = line_start; i < at; ++i)
        if (!cpp_member_body_detail::horizontal_space(code[i])) return {};
    auto opening = at + signature.size();
    while (opening < code.size() &&
           (cpp_member_body_detail::horizontal_space(code[opening]) ||
            code[opening] == '\n' || code[opening] == '\r'))
        ++opening;
    if (opening >= code.size() || code[opening] != '{') return {};
    const auto closing = cpp_member_body_detail::closing_brace(code, opening);
    if (!closing || *closing <= opening) return {};
    return CppMemberBodySpan{opening + 1, *closing};
}

inline bool safe_body(std::string_view source, CppMemberBodySpan body) {
    if (body.begin > body.end || body.end > source.size()) return false;
    const auto text = source.substr(body.begin, body.end - body.begin);
    if (code_format::physical_splice(text)) return false;
    const auto tokens = code_format::cpp_tokens(text);
    if (!tokens || code_format::cpp_position_sensitive_tokens(*tokens))
        return false;
    for (const auto& token : *tokens)
        if (token.starts_with("directive:") || token == "op:#" ||
            token == "op:##" || token == "op:%:" ||
            token == "op:%:%:")
            return false;
    return true;
}

inline std::optional<std::vector<CppMemberBodySpan>> spans(
        std::string_view source, const std::vector<CppEditableBody>& bodies,
        bool inspect_body) {
    if (!valid_source(source) || bodies.empty() ||
        bodies.size() > maximum_bodies) return {};
    std::vector<CppMemberBodySpan> positions;
    positions.reserve(bodies.size());
    std::size_t previous_end = 0;
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        for (std::size_t preceding = 0; preceding < index; ++preceding)
            if (bodies[preceding].signature == bodies[index].signature)
                return {};
        const auto body = find_body(source, bodies[index]);
        if (!body || body->begin < previous_end ||
            (inspect_body && !safe_body(source, *body)))
            return {};
        positions.push_back(*body);
        previous_end = body->end;
    }
    return positions;
}

} // namespace cpp_multi_body_detail

inline bool cpp_multi_body_matches(const CppMultiBodyPolicy& policy,
                                   std::string_view candidate) {
    namespace d = cpp_multi_body_detail;
    const auto originals = d::spans(policy.original_source, policy.bodies, false);
    const auto edited = d::spans(candidate, policy.bodies, true);
    if (!originals || !edited || originals->size() != edited->size())
        return false;

    // Compare all of the non-editable intervals byte-for-byte, not just
    // tokens. This protects declarations, namespace boundaries, other
    // methods, original main, literals, test cases and published formatting.
    const std::string_view before = policy.original_source;
    std::size_t previous_before = 0, previous_after = 0;
    for (std::size_t index = 0; index < originals->size(); ++index) {
        const auto& a = originals->at(index);
        const auto& b = edited->at(index);
        if (before.substr(previous_before, a.begin - previous_before) !=
            candidate.substr(previous_after, b.begin - previous_after))
            return false;
        previous_before = a.end;
        previous_after = b.end;
    }
    return before.substr(previous_before) == candidate.substr(previous_after);
}

} // namespace lgaed::curriculum

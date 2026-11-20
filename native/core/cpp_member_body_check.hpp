#pragma once

#include "core/code_format_compat.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace lgaed::curriculum {

// A source-preservation prerequisite, not proof that the edited method behaves
// correctly. An independent, compiled probe must verify the requested behavior.
struct CppMemberBodyCheck {
    std::string original_source;
    std::string signature;
    bool operator==(const CppMemberBodyCheck&) const = default;
};

struct CppMemberBodySpan {
    std::size_t begin = 0, end = 0; // Content between the unchanged { and }.
    bool operator==(const CppMemberBodySpan&) const = default;
};

namespace cpp_member_body_detail {
constexpr std::size_t source_limit = 4 * 1024 * 1024;

inline bool horizontal_space(char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

inline bool physical_splice(std::string_view source) {
    return source.find("\\\n") != source.npos ||
        source.find("\\\r\n") != source.npos;
}

// Skip an ordinary quoted literal or a C++ raw string, so a brace in text is
// never mistaken for a code boundary. An invalid/unterminated literal fails
// closed, rather than letting a comment or source suffix masquerade as code.
inline std::optional<std::size_t> quoted_end(std::string_view text, std::size_t at) {
    if(at >= text.size()) return {};
    if(text[at] == 'R' && at + 1 < text.size() && text[at+1] == '"') {
        const auto begin = at + 2;
        const auto opening = text.find('(', begin);
        if(opening == text.npos || opening - begin > 16) return {};
        const auto delimiter = text.substr(begin, opening - begin);
        for(char c : delimiter)
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\\' ||
               c == '(' || c == ')') return {};
        const auto close = std::string(")") + std::string(delimiter) + '"';
        const auto ending = text.find(close, opening + 1);
        if(ending == text.npos) return {};
        return ending + close.size();
    }
    if(text[at] != '"' && text[at] != '\'') return {};
    const char quote = text[at];
    for(auto i = at + 1; i < text.size(); ++i) {
        if(text[i] == '\n' || text[i] == '\r') return {};
        if(text[i] == '\\') {
            if(++i >= text.size()) return {};
            continue;
        }
        if(text[i] == quote) return i + 1;
    }
    return {};
}

inline std::optional<std::size_t> closing_brace(std::string_view text,
                                                std::size_t opening) {
    if(opening >= text.size() || text[opening] != '{' ||
       physical_splice(text)) return {};
    std::size_t depth = 0;
    bool line_start = false;
    for(auto i = opening; i < text.size();) {
        const auto c = text[i];
        if(c == '\n' || c == '\r') { line_start = true; ++i; continue; }
        if(horizontal_space(c)) { ++i; continue; }
        if(c == '/' && i + 1 < text.size() && text[i+1] == '/') {
            const auto end = text.find_first_of("\r\n", i + 2);
            i = end == text.npos ? text.size() : end;
            continue;
        }
        if(c == '/' && i + 1 < text.size() && text[i+1] == '*') {
            const auto end = text.find("*/", i + 2);
            if(end == text.npos) return {};
            // A multi-line comment is preprocessor whitespace. A directive
            // following the closing comment can still be line-leading.
            if(text.substr(i, end + 2 - i).find_first_of("\r\n") != text.npos)
                line_start = true;
            i = end + 2;
            continue;
        }
        if(c == '#' && line_start) return {}; // No directives in the editable body.
        line_start = false;
        if(c == 'R' && i + 1 < text.size() && text[i+1] == '"') {
            const auto end = quoted_end(text, i);
            if(!end) return {};
            i = *end;
            continue;
        }
        if(c == '"' || c == '\'') {
            const auto end = quoted_end(text, i);
            if(!end) return {};
            i = *end;
            continue;
        }
        // C++ digraphs <% and %> are braces, including when mixed with
        // literal { and }. An early %> must not escape the editable member.
        if(c == '<' && i + 1 < text.size() && text[i+1] == '%') {
            ++depth;
            i += 2;
            continue;
        }
        if(c == '%' && i + 1 < text.size() && text[i+1] == '>') {
            if(depth == 0) return {};
            if(--depth == 0) return i;
            i += 2;
            continue;
        }
        if(c == '{') ++depth;
        if(c == '}') {
            if(depth == 0) return {};
            if(--depth == 0) return i;
        }
        ++i;
    }
    return {};
}
} // namespace cpp_member_body_detail

inline std::optional<CppMemberBodySpan> cpp_member_body_span(
        const CppMemberBodyCheck& check) {
    const auto& source = check.original_source;
    const auto& signature = check.signature;
    if(source.empty() || source.size() > cpp_member_body_detail::source_limit ||
       source.find('\0') != source.npos ||
       signature.empty() || signature.size() > 512 ||
       signature.find('(') == signature.npos || signature.find(')') == signature.npos ||
       signature.find("::") == signature.npos ||
       cpp_member_body_detail::physical_splice(source)) return {};
    const auto at = source.find(signature);
    if(at == source.npos || source.find(signature, at + 1) != source.npos) return {};
    const auto prior_newline = source.rfind('\n', at);
    const auto line_begin = prior_newline == source.npos ? 0 : prior_newline + 1;
    for(auto i = line_begin; i < at; ++i)
        if(!cpp_member_body_detail::horizontal_space(source[i])) return {};
    if(at > 0) {
        const auto before = static_cast<unsigned char>(source[at - 1]);
        if((before >= '0' && before <= '9') ||
           (before >= 'A' && before <= 'Z') ||
           (before >= 'a' && before <= 'z') || before == '_') return {};
    }
    auto opening = at + signature.size();
    while(opening < source.size() &&
          (cpp_member_body_detail::horizontal_space(source[opening]) ||
           source[opening] == '\r' || source[opening] == '\n')) ++opening;
    if(opening >= source.size() || source[opening] != '{') return {};
    const auto end = cpp_member_body_detail::closing_brace(source, opening);
    if(!end || *end <= opening) return {};
    return CppMemberBodySpan{opening + 1, *end};
}

inline bool cpp_member_body_preserves_source(const CppMemberBodyCheck& check,
                                             std::string_view candidate) {
    const auto span = cpp_member_body_span(check);
    if(!span || candidate.size() > cpp_member_body_detail::source_limit ||
       candidate.find('\0') != candidate.npos) return false;
    const auto prefix = std::string_view(check.original_source).substr(0, span->begin);
    const auto suffix = std::string_view(check.original_source).substr(span->end);
    if(candidate.size() < prefix.size() + suffix.size() ||
       !candidate.starts_with(prefix) || !candidate.ends_with(suffix)) return false;
    // The original brace pair stays intact; no injected method closing brace,
    // preprocessor directive or physical line splice can escape the boundary.
    const auto close = candidate.size() - suffix.size();
    const auto closing = cpp_member_body_detail::closing_brace(candidate, prefix.size()-1);
    if (!closing || *closing != close) return false;
    const auto body = candidate.substr(prefix.size(), close - prefix.size());
    const auto tokens = code_format::cpp_tokens(body);
    // The source-level scanner also has a separate lexical guard. Both
    // must agree that no original or alternative brace closes this function.
    return tokens && code_format::confined_function_body_edit(body) &&
        !code_format::cpp_process_termination_tokens(*tokens);
}

} // namespace lgaed::curriculum

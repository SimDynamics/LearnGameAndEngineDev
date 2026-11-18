#pragma once

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::presentation {
namespace compiler_diagnostic_detail {

inline bool separator(wchar_t value) { return value == L'\\' || value == L'/'; }
inline bool digit(wchar_t value) { return value >= L'0' && value <= L'9'; }
inline bool space(wchar_t value) { return value == L' ' || value == L'\t'; }

inline std::size_t skip_spaces(std::wstring_view text, std::size_t at) {
    while (at < text.size() && space(text[at])) ++at;
    return at;
}
inline std::size_t skip_digits(std::wstring_view text, std::size_t at) {
    while (at < text.size() && digit(text[at])) ++at;
    return at;
}
inline bool command_location(std::wstring_view line, std::size_t at) {
    // CMake's "file:line (command):" and call-stack "file:line (command)".
    if (at >= line.size() || !space(line[at])) return false;
    at = skip_spaces(line, at);
    if (at >= line.size() || line[at++] != L'(') return false;
    const auto begin = at;
    while (at < line.size() && ((line[at] >= L'a' && line[at] <= L'z') ||
        (line[at] >= L'A' && line[at] <= L'Z') || digit(line[at]) || line[at] == L'_')) ++at;
    if (at == begin || at >= line.size() || line[at++] != L')') return false;
    at = skip_spaces(line, at);
    return at == line.size() || line[at] == L'\r' || line[at] == L':';
}
inline bool linker_diagnostic(std::wstring_view tail) {
    // MSVC link locations have no line/column. The LNK code is independent
    // of the UI language; do not identify diagnostics by translated severity.
    tail = tail.substr(0, 104);
    const auto code = tail.find(L"LNK");
    if (code == tail.npos || code > 96 || (code && !space(tail[code - 1]))) return false;
    const auto end = skip_digits(tail, code + 3);
    return end == code + 7 && end < tail.size() && tail[end] == L':';
}
inline std::optional<std::size_t> path_end(std::wstring_view line, std::size_t begin) {
    for (auto at = begin; at < line.size(); ++at) {
        if (line[at] == L'(') {
            // MSVC file(line[,column]) : severity CODE: message.
            auto next = skip_digits(line, at + 1);
            if (next == at + 1) continue;
            if (next < line.size() && line[next] == L',') {
                const auto column = next + 1;
                next = skip_digits(line, column);
                if (next == column) continue;
            }
            if (next < line.size() && line[next] == L')') {
                next = skip_spaces(line, next + 1);
                if (next < line.size() && line[next] == L':') return at;
            }
        } else if (line[at] == L':') {
            // A drive-letter colon does not have decimal coordinates.
            auto next = skip_digits(line, at + 1);
            if (next != at + 1) {
                if (next < line.size() && (line[next] == L':' || line[next] == L','))
                    return at; // Clang/GCC diagnostics and include-stack entries.
                if (command_location(line, next)) return at;
            }
            if (at + 1 < line.size() && space(line[at + 1]) && linker_diagnostic(line.substr(at + 1))) {
                auto end = at;
                while (end > begin && space(line[end - 1])) --end;
                if (end > begin) return end;
            }
        }
    }
    return std::nullopt;
}
inline std::optional<std::size_t> msvc_include_begin(std::wstring_view line) {
    // MSVC /showIncludes has no line:column suffix, unlike source
    // diagnostics. Recognize only the documented compiler prefix and only
    // when it begins the line. The owned-root check still applies.
    // Keep leading spaces and the nested-include indentation untouched.
    auto begin = skip_spaces(line, 0);
    constexpr std::wstring_view prefix = L"Note: including file:";
    if (!line.substr(begin).starts_with(prefix)) return std::nullopt;
    return skip_spaces(line, begin + prefix.size());
}
inline std::size_t path_begin(std::wstring_view line) {
    auto begin = skip_spaces(line, 0);
    const auto body = line.substr(begin);
    constexpr std::wstring_view include = L"In file included from ";
    if (body.starts_with(include)) return begin + include.size();
    if (body.starts_with(L"from ")) return begin + 5;
    if (body.starts_with(L"CMake Error") || body.starts_with(L"CMake Warning") ||
        body.starts_with(L"CMake Deprecation Warning")) {
        const auto at = body.find(L" at ");
        if (at != body.npos) return begin + at + 4;
    }
    return begin;
}
inline std::optional<std::size_t> root_length(std::wstring_view path, const std::vector<std::wstring>& roots) {
    for (const auto& root : roots) {
        if (path.size() <= root.size() + 1 || !separator(path[root.size()])) continue;
        std::wstring prefix(path.substr(0, root.size()));
        std::replace(prefix.begin(), prefix.end(), L'/', L'\\');
        if (CompareStringOrdinal(prefix.data(), static_cast<int>(prefix.size()),
            root.data(), static_cast<int>(root.size()), TRUE) == CSTR_EQUAL) return root.size();
    }
    return std::nullopt;
}
} // namespace compiler_diagnostic_detail

// Display-only: recognize compiler/CMake source locations and leave all other
// bytes/characters, source excerpts, carets and message text in their places.
// Call only for tool diagnostics, never for a program's stdout or stderr.
[[nodiscard]] inline std::wstring compact_compiler_diagnostic_paths(
    std::wstring_view text, std::span<const std::filesystem::path> known_roots) {
    using namespace compiler_diagnostic_detail;
    std::vector<std::wstring> roots;
    for (const auto& path : known_roots) {
        if (path.empty() || !path.is_absolute()) continue;
        auto root = path.wstring();
        while (!root.empty() && separator(root.back())) root.pop_back();
        if (root.empty() || root.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) continue;
        std::replace(root.begin(), root.end(), L'/', L'\\');
        // Tools and Windows file APIs can report the same owned directory
        // with or without the extended-length namespace. Keep both spellings
        // so long paths are compacted without changing the retained raw text.
        // Other device namespaces do not identify an ordinary drive/UNC root.
        const bool extended_drive = root.starts_with(L"\\\\?\\") && root.size() >= 6 &&
            ((root[4] >= L'A' && root[4] <= L'Z') || (root[4] >= L'a' && root[4] <= L'z')) &&
            root[5] == L':' && (root.size() == 6 || separator(root[6]));
        if (extended_drive) root.erase(0, 4);
        else if (root.size() > 8 && CompareStringOrdinal(root.data(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL)
            root = L"\\\\" + root.substr(8);
        roots.push_back(root);
        const bool drive = root.size() >= 2 &&
            ((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z')) &&
            root[1] == L':' && (root.size() == 2 || separator(root[2]));
        if (drive) roots.push_back(L"\\\\?\\" + root);
        else if (root.starts_with(L"\\\\") && root.size() > 2 && root[2] != L'?' && root[2] != L'.')
            roots.push_back(L"\\\\?\\UNC\\" + root.substr(2));
    }
    // A project source root must win over its enclosing generated/run folder.
    std::sort(roots.begin(), roots.end(), [](const auto& left, const auto& right) {
        return left.size() > right.size();
    });
    std::wstring result;
    result.reserve(text.size());
    for (std::size_t begin = 0; begin < text.size();) {
        const auto newline = text.find(L'\n', begin);
        const auto end = newline == text.npos ? text.size() : newline;
        const auto line = text.substr(begin, end - begin); // Keep an original CR.
        const auto include_begin = msvc_include_begin(line);
        const auto first = include_begin.value_or(path_begin(line));
        const auto root = root_length(line.substr(first), roots);
        // /showIncludes reports only an absolute include path, without
        // coordinates. Other compiler/CMake diagnostics still need a real
        // location suffix. Both paths require the same trusted owned root;
        // literal source lines and external headers remain unchanged.
        const auto last = root ? (include_begin ? std::optional<std::size_t>{line.size()}
                                               : path_end(line, first)) : std::nullopt;
        if (root && last && *last > first + *root + 1) {
            result.append(line.substr(0, first));
            result.append(line.substr(first + *root + 1, *last - first - *root - 1));
            result.append(line.substr(*last));
        } else {
            result.append(line);
        }
        if (newline == text.npos) break;
        result.push_back(L'\n');
        begin = newline + 1;
    }
    return result;
}
} // namespace lgaed::presentation

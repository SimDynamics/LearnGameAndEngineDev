#pragma once

#include "core/code_format_compat.hpp"
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

struct CppExpressionRepair {
    std::string path, original_source, expression;
    bool operator==(const CppExpressionRepair&) const = default;
};

inline constexpr std::string_view cpp_expression_repair_objective_version =
    "cpp-authenticated-scalar-expression-repair-v1";

namespace cpp_expression_repair_detail {
inline constexpr std::size_t source_limit = 4 * 1024 * 1024;

inline bool path_valid(std::string_view path) {
    if(path.empty() || path.size() > 256) return false;
    for(const unsigned char c : path)
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' ||
             c == '.' || c == '/')) return false;
    for(std::size_t at = 0; at < path.size();) {
        const auto slash = path.find('/', at);
        const auto end = slash == path.npos ? path.size() : slash;
        if(end == at || path[at] == '.' || path[end - 1] == '.') return false;
        auto component = path.substr(at, end - at);
        component = component.substr(0, component.find('.'));
        std::string device(component);
        for(char& c : device) if(c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if(device == "CON" || device == "PRN" || device == "AUX" || device == "NUL" ||
           (device.size() == 4 && (device.starts_with("COM") || device.starts_with("LPT")) &&
            device.back() >= '1' && device.back() <= '9')) return false;
        if(end == path.size()) break;
        at = end + 1;
        if(at == path.size()) return false;
    }
    const auto dot = path.rfind('.');
    if(dot == path.npos) return false;
    std::string extension(path.substr(dot));
    for(char& c : extension) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".c++";
}

struct Token {
    std::string value;
    std::size_t begin = 0, end = 0;
};

inline std::optional<std::vector<Token>> tokens(std::string_view source) {
    if(source.empty() || source.size() > source_limit || source.find('\0') != source.npos ||
       code_format::physical_splice(source)) return {};
    const auto scanned = code_format::cpp_tokens(source);
    if(!scanned || code_format::cpp_position_sensitive_tokens(*scanned)) return {};
    std::vector<Token> result;
    result.reserve(scanned->size());
    std::size_t offset = 0;
    for(const auto& value : *scanned) {
        const auto colon = value.find(':');
        if(colon == value.npos) return {};
        const auto literal = std::string_view(value).substr(colon + 1);
        const auto begin = source.find(literal, offset);
        if(begin == source.npos || !std::ranges::all_of(source.substr(offset, begin - offset), code_format::space))
            return {};
        offset = begin + literal.size();
        result.push_back({value, begin, offset});
    }
    if(!std::ranges::all_of(source.substr(offset), code_format::space)) return {};
    return result;
}

inline bool include_directive(std::string_view value) {
    if(!value.starts_with("directive:")) return false;
    value.remove_prefix(10);
    value.remove_prefix(value.starts_with("%:") ? 2 : 1);
    while(!value.empty() && code_format::space(value.front())) value.remove_prefix(1);
    if(!value.starts_with("include")) return false;
    value.remove_prefix(7);
    while(!value.empty() && code_format::space(value.front())) value.remove_prefix(1);
    if(value.empty() || (value.front() != '<' && value.front() != '"')) return false;
    const char close = value.front() == '<' ? '>' : '"';
    const auto end = value.find(close, 1);
    if(end == value.npos || end == 1) return false;
    const auto trailing = code_format::cpp_tokens(value.substr(end + 1));
    return trailing && std::ranges::all_of(*trailing, [](const auto& token) {
        return token.starts_with("comment:");
    });
}

inline bool source_tokens_valid(const std::vector<Token>& scanned) {
    // The original file is authenticated, but a local macro could expand a
    // seemingly pure identifier into statements. This opt-in contract admits
    // ordinary literal includes, not local preprocessing or inactive branches.
    return std::ranges::all_of(scanned, [](const auto& token) {
        return !token.value.starts_with("directive:") || include_directive(token.value);
    });
}

inline bool number(std::string_view value) {
    std::string clean;
    clean.reserve(value.size());
    const auto digit = [](char c, int base) {
        if(c >= '0' && c <= '9') return c - '0' < base;
        return base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
    };
    for(std::size_t i = 0; i < value.size(); ++i) {
        if(value[i] == '\'') {
            if(i == 0 || i + 1 == value.size() || !digit(value[i - 1], 16) || !digit(value[i + 1], 16))
                return false;
        } else clean.push_back(value[i]);
    }
    value = clean;
    int base = 10;
    std::size_t at = 0;
    if(value.starts_with("0x") || value.starts_with("0X")) { base = 16; at = 2; }
    else if(value.starts_with("0b") || value.starts_with("0B")) { base = 2; at = 2; }
    const auto first = at;
    while(at < value.size() && digit(value[at], base)) ++at;
    bool has_digits = at != first;
    bool floating = false;
    if(base != 2 && at < value.size() && value[at] == '.') {
        floating = true; ++at;
        const auto after_dot = at;
        while(at < value.size() && digit(value[at], base)) ++at;
        has_digits = has_digits || at != after_dot;
    }
    if(!has_digits) return false;
    const bool exponent = at < value.size() &&
        (base == 16 ? value[at] == 'p' || value[at] == 'P' :
         base == 10 && (value[at] == 'e' || value[at] == 'E'));
    if(exponent) {
        floating = true; ++at;
        if(at < value.size() && (value[at] == '+' || value[at] == '-')) ++at;
        const auto start = at;
        while(at < value.size() && digit(value[at], 10)) ++at;
        if(start == at) return false;
    }
    if(base == 16 && floating && !exponent) return false;
    std::string suffix(value.substr(at));
    for(char& c : suffix) if(c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if(floating) return suffix.empty() || suffix == "f" || suffix == "l";
    return suffix.empty() || suffix == "u" || suffix == "l" || suffix == "ul" ||
        suffix == "lu" || suffix == "ll" || suffix == "ull" || suffix == "llu" ||
        suffix == "z" || suffix == "uz" || suffix == "zu";
}

inline bool character(std::string_view value) {
    if(value.starts_with("u8")) value.remove_prefix(2);
    else if(value.starts_with('u') || value.starts_with('U') || value.starts_with('L')) value.remove_prefix(1);
    if(value.size() < 3 || value.front() != '\'' || value.find('\n') != value.npos || value.find('\r') != value.npos)
        return false;
    const auto end = code_format::quoted_end(value, 0);
    return end && *end == value.size(); // A user-defined suffix would invoke code.
}

inline bool identifier(std::string_view value) {
    if(value.empty() || value.starts_with("__")) return false;
    if(value == "true" || value == "false" || value == "nullptr") return true;
    constexpr std::string_view keywords[]{
        "alignas", "alignof", "and", "and_eq", "asm", "atomic_cancel", "atomic_commit", "atomic_noexcept",
        "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t",
        "class", "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast",
        "else", "enum", "explicit", "export", "extern", "float", "for", "friend", "goto", "if", "inline",
        "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq", "operator", "or", "or_eq",
        "private", "protected", "public", "reflexpr", "register", "reinterpret_cast", "requires", "return",
        "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct", "switch", "synchronized",
        "template", "this", "thread_local", "throw", "try", "typedef", "typeid", "typename", "union", "unsigned",
        "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
    };
    return std::ranges::find(keywords, value) == std::end(keywords);
}

inline int precedence(std::string_view token) {
    if(token == "op:||" || token == "id:or") return 1;
    if(token == "op:&&" || token == "id:and") return 2;
    if(token == "op:|" || token == "id:bitor") return 3;
    if(token == "op:^" || token == "id:xor") return 4;
    if(token == "op:&" || token == "id:bitand") return 5;
    if(token == "op:==" || token == "op:!=" || token == "id:not_eq") return 6;
    if(token == "op:<" || token == "op:<=" || token == "op:>" || token == "op:>=") return 7;
    if(token == "op:<<" || token == "op:>>") return 8;
    if(token == "op:+" || token == "op:-") return 9;
    if(token == "op:*" || token == "op:/" || token == "op:%") return 10;
    return 0;
}

class ScalarExpression {
    const std::vector<std::string_view>& values_;
    std::size_t at_ = 0;

    bool take(std::string_view token) {
        if(at_ == values_.size() || values_[at_] != token) return false;
        ++at_; return true;
    }
    bool unary(std::size_t depth) {
        if(depth > 128 || at_ == values_.size()) return false;
        const auto token = values_[at_];
        if(token == "op:+" || token == "op:-" || token == "op:!" || token == "op:~" ||
           token == "id:not" || token == "id:compl") {
            ++at_; return unary(depth + 1);
        }
        if(take("op:(")) return conditional(depth + 1) && take("op:)");
        ++at_;
        return token.starts_with("id:") ? identifier(token.substr(3)) :
            token.starts_with("number:") ? number(token.substr(7)) :
            token.starts_with("literal:") && character(token.substr(8));
    }
    bool binary(int minimum, std::size_t depth) {
        if(!unary(depth)) return false;
        while(at_ < values_.size()) {
            const auto level = precedence(values_[at_]);
            if(level < minimum) break;
            ++at_;
            if(!binary(level + 1, depth + 1)) return false;
        }
        return true;
    }
    bool conditional(std::size_t depth) {
        if(depth > 128 || !binary(1, depth)) return false;
        if(!take("op:?")) return true;
        return conditional(depth + 1) && take("op::") && conditional(depth + 1);
    }
public:
    explicit ScalarExpression(const std::vector<std::string_view>& values) : values_(values) {}
    bool valid() { return conditional(0) && at_ == values_.size(); }
};

inline std::vector<std::string_view> code_tokens(const std::vector<Token>& scanned) {
    std::vector<std::string_view> result;
    for(const auto& token : scanned)
        if(!token.value.starts_with("comment:")) result.push_back(token.value);
    return result;
}

inline bool expression_valid(const std::vector<Token>& scanned) {
    const auto values = code_tokens(scanned);
    return !values.empty() && ScalarExpression(values).valid();
}

struct Selection {
    std::vector<Token> source;
    std::size_t first = 0, last = 0; // Inclusive token positions, including internal comments.
};

inline std::optional<Selection> select(const CppExpressionRepair& repair) {
    if(!path_valid(repair.path) || repair.expression.empty() || repair.expression.size() > 512) return {};
    auto original = tokens(repair.original_source);
    const auto wanted = tokens(repair.expression);
    if(!original || !source_tokens_valid(*original) || !wanted || !expression_valid(*wanted)) return {};
    const auto pattern = code_tokens(*wanted);
    std::vector<std::size_t> code;
    for(std::size_t i = 0; i < original->size(); ++i)
        if(!(*original)[i].value.starts_with("comment:")) code.push_back(i);
    if(code.size() < pattern.size()) return {};
    std::optional<std::pair<std::size_t, std::size_t>> found;
    for(std::size_t i = 0; i <= code.size() - pattern.size(); ++i) {
        bool same = true;
        for(std::size_t j = 0; j < pattern.size(); ++j)
            if((*original)[code[i + j]].value != pattern[j]) { same = false; break; }
        if(!same) continue;
        if(found) return {}; // An authored selector must identify exactly one real expression.
        found = {code[i], code[i + pattern.size() - 1]};
    }
    if(!found) return {};
    return Selection{std::move(*original), found->first, found->second};
}
} // namespace cpp_expression_repair_detail

inline bool cpp_expression_repair_valid(const CppExpressionRepair& repair) {
    return cpp_expression_repair_detail::select(repair).has_value();
}

inline bool cpp_expression_repair_matches(const CppExpressionRepair& repair, std::string_view candidate) {
    using namespace cpp_expression_repair_detail;
    const auto selected = select(repair);
    if(!selected || candidate.empty() || candidate.size() > source_limit) return false;
    const auto begin = selected->source[selected->first].begin;
    const auto end = selected->source[selected->last].end;
    const auto prefix = std::string_view(repair.original_source).substr(0, begin);
    const auto suffix = std::string_view(repair.original_source).substr(end);
    if(candidate.size() <= prefix.size() + suffix.size() || !candidate.starts_with(prefix) || !candidate.ends_with(suffix))
        return false;
    const auto replacement_end = candidate.size() - suffix.size();
    const auto replacement = candidate.substr(begin, replacement_end - begin);
    if(replacement.size() > 4096) return false;
    const auto expression = tokens(replacement);
    const auto whole = tokens(candidate);
    if(!expression || !expression_valid(*expression) || !whole || !source_tokens_valid(*whole)) return false;

    // Identical bytes alone are insufficient: a trailing //, a raw literal,
    // or a newly joined token could consume the fixed suffix. Authenticate
    // both boundaries against the complete source's tokenization, retaining
    // every comment and directive outside the selected interval as well.
    const auto before = selected->first;
    const auto after = selected->source.size() - selected->last - 1;
    if(whole->size() <= before + after) return false;
    for(std::size_t i = 0; i < before; ++i) {
        const auto& expected = selected->source[i]; const auto& actual = (*whole)[i];
        if(actual.value != expected.value || actual.begin != expected.begin || actual.end != expected.end) return false;
    }
    for(std::size_t i = 0; i < after; ++i) {
        const auto& expected = selected->source[selected->last + 1 + i];
        const auto& actual = (*whole)[whole->size() - after + i];
        if(actual.value != expected.value || actual.begin != replacement_end + expected.begin - end ||
           actual.end != replacement_end + expected.end - end) return false;
    }
    for(std::size_t i = before; i < whole->size() - after; ++i)
        if((*whole)[i].begin < begin || (*whole)[i].end > replacement_end) return false;
    return true;
}

inline std::string cpp_expression_repair_frame(const CppExpressionRepair& repair) {
    if(!cpp_expression_repair_valid(repair)) throw std::invalid_argument("Invalid C++ expression repair specification");
    std::string result;
    const auto field = [&](std::string_view value) {
        result += std::to_string(value.size()); result += ':'; result += value;
    };
    field(cpp_expression_repair_objective_version);
    field(repair.path); field(repair.original_source); field(repair.expression);
    return result;
}
} // namespace lgaed::curriculum

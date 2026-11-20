#pragma once

#include "core/named_rule_check.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>

namespace lgaed::curriculum {

inline constexpr std::string_view named_rule_battery_objective_version =
    "named-rule-real-battery-independent-mutations-v1";

enum class NamedRuleBatteryMutation { baseline, mutate };
struct NamedRuleBatteryPhase {
    NamedRuleBatteryMutation mutation = NamedRuleBatteryMutation::baseline;
    std::optional<std::size_t> target;
    bool operator==(const NamedRuleBatteryPhase&) const = default;
};
struct NamedRuleBatteryObservation {
    std::string function;
    int input = 0, result = 0;
    bool operator==(const NamedRuleBatteryObservation&) const = default;
};
struct NamedRuleBatteryRun {
    std::string program_frame;
    bool compiled = false, executed = false, completed_normally = false;
    std::uint32_t exit_code = 0;
    NamedRuleBatteryPhase phase;
    std::vector<NamedRuleBatteryObservation> calls;
    std::string stdout_bytes, stderr_bytes;
};
struct NamedRuleBatteryTrace {
    std::vector<NamedRuleBatteryObservation> calls;
    bool completed_normally = false;
    std::string visible;
};
struct NamedRuleBatteryVerdict {
    bool accepted = false;
    std::string reason;
};

namespace named_rule_battery_detail {
inline constexpr std::string_view private_name = "lgaed_named_rule_battery_";
inline constexpr std::string_view private_marker = "LGAED_NAMED_RULE_BATTERY_";
inline bool identifier(std::string_view value) {
    if(value.empty() || value.size() > 128 || value == "main") return false;
    const auto letter = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    if(!letter(static_cast<unsigned char>(value.front()))) return false;
    for(const unsigned char c : value)
        if(!letter(c) && !(c >= '0' && c <= '9')) return false;
    return !value.starts_with(private_name) &&
        !value.starts_with("lgaed_boolean_battery_") &&
        !value.starts_with("lgaed_integer_battery_");
}
inline bool source_path(std::string_view value) {
    if(value.empty()) return true;
    if(value.size() > 256 || value.front() == '/' || value.front() == '.' ||
       value.find("..") != value.npos || !value.ends_with(".cpp")) return false;
    return std::ranges::all_of(value, [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
    });
}
inline bool stream(std::string_view value) {
    return value.size() <= 4 * 1024 * 1024 && value.find('\0') == value.npos;
}
// Both int and bool can represent 0 and 1. This mutation always differs from
// the authored result, including INT_MIN/INT_MAX, without arithmetic overflow.
inline int changed(int expected) { return expected == 0 ? 1 : 0; }
struct Token {
    std::string value;
    std::size_t begin = 0, end = 0;
};
using Tokens = std::vector<Token>;
inline bool opening(std::string_view token) { return token == "op:{" || token == "op:<%"; }
inline bool closing(std::string_view token) { return token == "op:}" || token == "op:%>"; }

// generated=true is used only after instrument_source has validated the raw
// single-file candidate. Authored sources cannot opt into this lexical mode.
inline std::optional<Tokens> tokens(std::string_view source, bool generated = false) {
    if(source.empty() || !stream(source) || code_format::physical_splice(source)) return {};
    const auto scanned = code_format::cpp_tokens(source);
    if(!scanned) return {};
    if(!generated && (!named_rule_detail::probe_environment_neutral(*scanned) ||
        !named_rule_detail::probe_source_no_early_exit(source))) return {};
    Tokens result;
    std::size_t offset = 0;
    for(const auto& token : *scanned) {
        const auto colon = token.find(':');
        if(colon == token.npos) return {};
        const auto literal = std::string_view(token).substr(colon + 1);
        const auto begin = source.find(literal, offset);
        if(begin == source.npos || !std::ranges::all_of(
            source.substr(offset, begin - offset), code_format::space)) return {};
        offset = begin + literal.size();
        if(token.starts_with("comment:")) continue;
        if(!generated) {
            if(token == "op:#" || token == "op:##" || token == "op:%:" ||
               token == "op:%:%:") return {};
            if(token.starts_with("id:")) {
                const auto name = std::string_view(token).substr(3);
                if(name.starts_with(private_name) ||
                   name.starts_with("lgaed_boolean_battery_") ||
                   name.starts_with("lgaed_integer_battery_") ||
                   name == "__func__" || name == "__FUNCTION__") return {};
            }
            if(token.starts_with("literal:") && literal.find(private_marker) != literal.npos)
                return {};
            if(token.starts_with("directive:")) {
                auto directive = literal;
                directive.remove_prefix(directive.starts_with("%:") ? 2 : 1);
                while(!directive.empty() && code_format::space(directive.front()))
                    directive.remove_prefix(1);
                if(!directive.starts_with("include")) return {};
                directive.remove_prefix(7);
                while(!directive.empty() && code_format::space(directive.front()))
                    directive.remove_prefix(1);
                if(directive.empty() || (directive.front() != '<' && directive.front() != '"'))
                    return {};
            }
        }
        // Normalize digraph braces for all source analysis and private
        // counterfactual phases, but retain the exact original byte spans.
        result.push_back({named_rule_detail::canonical_brace_token(token),
                          begin, offset});
    }
    if(!generated) {
        std::size_t depth = 0;
        for(const auto& token : result) {
            if(token.value.starts_with("directive:") && depth) return {};
            if(opening(token.value)) { if(++depth > 4096) return {}; }
            else if(closing(token.value)) { if(!depth) return {}; --depth; }
        }
        if(depth) return {};
    }
    return result;
}

inline std::optional<std::size_t> main_count(const Tokens& t) {
    std::size_t depth = 0, count = 0;
    for(std::size_t i = 0; i < t.size(); ++i) {
        if(!depth && t[i].value == "id:main" && i + 1 < t.size() &&
           t[i + 1].value == "op:(") {
            if(i == 0 || t[i - 1].value != "id:int") return {};
            std::size_t end = i + 2, parentheses = 1;
            for(; end < t.size() && parentheses; ++end) {
                if(t[end].value == "op:(") ++parentheses;
                else if(t[end].value == "op:)") --parentheses;
                if(parentheses > 4096) return {};
            }
            if(parentheses) return {};
            if(end < t.size() && opening(t[end].value)) ++count;
            else if(end >= t.size() || t[end].value != "op:;") return {};
        }
        if(opening(t[i].value)) { if(++depth > 4096) return {}; }
        else if(closing(t[i].value)) { if(!depth) return {}; --depth; }
    }
    if(depth) return {};
    return count;
}
struct Definition {
    std::size_t begin = 0, opening_end = 0, closing_begin = 0, end = 0;
    std::string name, parameter;
    bool boolean = false;
};
// Deliberately bounded: global int/bool functions with one named int value
// parameter. No overloads, qualifiers, references or namespace rewriting.
inline std::optional<Definition> definition(const Tokens& t, std::string_view name) {
    if(!identifier(name)) return {};
    std::optional<Definition> found;
    std::vector<bool> declarations;
    std::size_t depth = 0;
    for(std::size_t i = 0; i < t.size(); ++i) {
        if(t[i].value.starts_with("directive:") && depth) return {};
        if(t[i].value == "id:" + std::string(name) && i + 1 < t.size() &&
           t[i + 1].value == "op:(") {
            std::size_t end = i + 2, nesting = 1;
            for(; end < t.size() && nesting; ++end) {
                if(t[end].value == "op:(") ++nesting;
                else if(t[end].value == "op:)") --nesting;
                if(nesting > 4096) return {};
            }
            if(nesting) return {};
            const bool body = end < t.size() && opening(t[end].value);
            const bool declared = i && t[i - 1].value.starts_with("id:") &&
                t[i - 1].value != "id:return" && t[i - 1].value != "id:co_return" &&
                t[i - 1].value != "id:throw";
            const bool reference = !depth && i > 1 &&
                (t[i - 2].value == "id:int" || t[i - 2].value == "id:bool") &&
                (t[i - 1].value == "op:&" || t[i - 1].value == "op:&&" ||
                 t[i - 1].value == "op:*");
            if(body || declared || reference) {
                if(!i || depth || (t[i - 1].value != "id:int" && t[i - 1].value != "id:bool") ||
                   i + 5 >= t.size()) return {};
                if(i > 1 && t[i - 2].value != "op:;" && !closing(t[i - 2].value) &&
                   !t[i - 2].value.starts_with("directive:")) return {};
                if(t[i + 2].value != "id:int" || !t[i + 3].value.starts_with("id:") ||
                   t[i + 4].value != "op:)") return {};
                const auto parameter = t[i + 3].value.substr(3);
                if(!identifier(parameter)) return {};
                const bool boolean = t[i - 1].value == "id:bool";
                if(t[i + 5].value == "op:;") declarations.push_back(boolean);
                else {
                    if(t[i + 5].value != "op:{" || found) return {};
                    end = i + 6; nesting = 1;
                    for(; end < t.size() && nesting; ++end) {
                        if(t[end].value.starts_with("directive:")) return {};
                        if(opening(t[end].value)) ++nesting;
                        else if(closing(t[end].value)) --nesting;
                        if(nesting > 4096) return {};
                    }
                    if(nesting) return {};
                    --end;
                    found = Definition{t[i - 1].begin, t[i + 5].end,
                        t[end].begin, t[end].end, std::string(name), parameter, boolean};
                }
            }
        }
        if(opening(t[i].value)) { if(++depth > 4096) return {}; }
        else if(closing(t[i].value)) { if(!depth) return {}; --depth; }
    }
    if(depth || !found) return {};
    for(const bool boolean : declarations) if(boolean != found->boolean) return {};
    return found;
}
inline std::size_t line(std::string_view source, std::size_t end) {
    return 1 + static_cast<std::size_t>(std::ranges::count(source.substr(0, end), '\n'));
}
inline std::optional<int> decimal(std::string_view text) {
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if(error != std::errc{} || end != text.data() + text.size()) return {};
    return value;
}
inline std::string_view runtime_prefix() {
    return R"cpp(#include <charconv>
#include <cstdio>
#include <iostream>
namespace lgaed_named_rule_battery_private {
unsigned count=0;
volatile unsigned anchor_value=0;
void literal(const char* text,std::size_t size) { std::fwrite(text,1,size,stderr); }
void number(int value) {
    char bytes[32];
    const auto converted=std::to_chars(bytes,bytes+sizeof(bytes),value);
    if(converted.ec==std::errc{})std::fwrite(bytes,1,static_cast<std::size_t>(converted.ptr-bytes),stderr);
}
void call(const char* function,std::size_t size,int input,int result) {
    literal("LGAED_NAMED_RULE_BATTERY_CALL:",sizeof("LGAED_NAMED_RULE_BATTERY_CALL:")-1);
    number(static_cast<int>(count++));literal(",",1);literal(function,size);
    literal(",",1);number(input);literal(",",1);number(result);literal("\n",1);
    std::fflush(stderr);
}
}
extern "C" void lgaed_named_rule_battery_anchor() { lgaed_named_rule_battery_private::anchor_value=1; }
#line 1
)cpp";
}
} // namespace named_rule_battery_detail

inline bool named_rule_battery_spec_valid(const NamedRuleBatterySpec& spec) {
    using namespace named_rule_battery_detail;
    if(!source_path(spec.source_path) || spec.cases.empty() || spec.cases.size() > 32 ||
       !spec.failure_exit_code) return false;
    for(std::size_t i = 0; i < spec.cases.size(); ++i) {
        if(!identifier(spec.cases[i].function)) return false;
        for(std::size_t j = 0; j < i; ++j)
            if(spec.cases[i].function == spec.cases[j].function &&
               spec.cases[i].input == spec.cases[j].input) return false;
    }
    return true;
}

inline bool named_rule_battery_source_valid(std::string_view source,
        const NamedRuleBatterySpec& spec) {
    if(!named_rule_battery_spec_valid(spec)) return false;
    const auto parsed = named_rule_battery_detail::tokens(source);
    if(!parsed) return false;
    const auto mains = named_rule_battery_detail::main_count(*parsed);
    if(!mains || *mains != (spec.source_path.empty() ? 1u : 0u)) return false;
    for(const auto& item : spec.cases) {
        const auto function = named_rule_battery_detail::definition(*parsed, item.function);
        if(!function || (function->boolean && item.expected != 0 && item.expected != 1)) return false;
    }
    return true;
}
inline bool named_rule_battery_driver_valid(std::string_view source) {
    const auto parsed = named_rule_battery_detail::tokens(source);
    if(!parsed) return false;
    const auto mains = named_rule_battery_detail::main_count(*parsed);
    return mains && *mains == 1;
}

inline std::string named_rule_battery_spec_frame(const NamedRuleBatterySpec& spec) {
    if(!named_rule_battery_spec_valid(spec))
        throw std::invalid_argument("Invalid named-rule battery specification");
    std::string frame;
    const auto field = [&](std::string_view value) {
        frame += std::to_string(value.size()); frame += ':'; frame += value;
    };
    field(named_rule_battery_objective_version); field(spec.source_path);
    field(std::to_string(spec.failure_exit_code)); field(std::to_string(spec.cases.size()));
    for(const auto& item : spec.cases) {
        field(item.function); field(std::to_string(item.input)); field(std::to_string(item.expected));
    }
    return frame;
}
inline std::optional<std::vector<NamedRuleBatteryPhase>> named_rule_battery_plan(
        const NamedRuleBatterySpec& spec) {
    if(!named_rule_battery_spec_valid(spec)) return {};
    std::vector<NamedRuleBatteryPhase> plan{{NamedRuleBatteryMutation::baseline, {}}};
    for(std::size_t i = 0; i < spec.cases.size(); ++i)
        plan.push_back({NamedRuleBatteryMutation::mutate, i});
    return plan;
}

// Instrument only the reviewed functions. The original body executes inside
// a typed lambda; the student's helpers, comparisons and main stay unchanged.
inline std::optional<std::string> named_rule_battery_instrument_source(
        std::string_view source, const NamedRuleBatterySpec& spec,
        const NamedRuleBatteryPhase& phase) {
    using namespace named_rule_battery_detail;
    if(!named_rule_battery_source_valid(source, spec)) return {};
    const auto plan = named_rule_battery_plan(spec);
    if(!plan || std::ranges::find(*plan, phase) == plan->end()) return {};
    const auto parsed = tokens(source);
    if(!parsed) return {};
    std::vector<Definition> functions;
    for(const auto& item : spec.cases) {
        if(std::ranges::find(functions, item.function, &Definition::name) != functions.end()) continue;
        const auto function = definition(*parsed, item.function);
        if(!function) return {};
        functions.push_back(*function);
    }
    std::ranges::sort(functions, {}, &Definition::begin);
    std::string result(runtime_prefix());
    std::size_t position = 0;
    for(const auto& function : functions) {
        result += source.substr(position, function.opening_end - position);
        const std::string type = function.boolean ? "bool" : "int";
        result += "\n    const int lgaed_named_rule_battery_input=" + function.parameter + ";\n";
        result += "    const " + type + " lgaed_named_rule_battery_original=[&]()->" + type + " {\n#line " +
            std::to_string(line(source, function.opening_end)) + "\n";
        result += source.substr(function.opening_end, function.closing_begin - function.opening_end);
        result += "\n    }();\n    " + type +
            " lgaed_named_rule_battery_result=lgaed_named_rule_battery_original;\n";
        if(phase.target && spec.cases[*phase.target].function == function.name) {
            const auto& target = spec.cases[*phase.target];
            result += "    if(lgaed_named_rule_battery_input==" + std::to_string(target.input) +
                ") lgaed_named_rule_battery_result=" + std::to_string(changed(target.expected)) + ";\n";
        }
        result += "    lgaed_named_rule_battery_private::call(\"" + function.name + "\"," +
            std::to_string(function.name.size()) +
            ",lgaed_named_rule_battery_input,static_cast<int>(lgaed_named_rule_battery_result));\n";
        result += "    return lgaed_named_rule_battery_result;\n#line " +
            std::to_string(line(source, function.closing_begin)) + "\n";
        result += source.substr(function.closing_begin, function.end - function.closing_begin);
        position = function.end;
    }
    result += source.substr(position);
    return result;
}

// CMake: pass the raw authored driver. Single-file: call instrument_source
// first and pass its result here. The private symbol forces linkage of the
// instrumented library even when a faulty main never calls a reviewed function.
inline std::optional<std::string> named_rule_battery_instrument_driver(
        std::string_view source, const NamedRuleBatterySpec& spec) {
    if(!named_rule_battery_spec_valid(spec)) return {};
    if(spec.source_path.empty()) {
        if(!source.starts_with(named_rule_battery_detail::runtime_prefix())) return {};
        const auto parsed = named_rule_battery_detail::tokens(source, true);
        if(!parsed) return {};
        const auto mains = named_rule_battery_detail::main_count(*parsed);
        if(!mains || *mains != 1) return {};
    } else if(!named_rule_battery_driver_valid(source)) return {};
    return std::string(R"cpp(#include <cstdio>
#include <iostream>
extern "C" void lgaed_named_rule_battery_anchor();
namespace lgaed_named_rule_battery_driver {
struct Completion {
    Completion() { lgaed_named_rule_battery_anchor(); }
    ~Completion() {
        std::fwrite("LGAED_NAMED_RULE_BATTERY_END\n",1,sizeof("LGAED_NAMED_RULE_BATTERY_END\n")-1,stderr);
        std::fflush(stderr);
    }
} completion;
}
#line 1
)cpp") + std::string(source);
}

inline std::optional<NamedRuleBatteryTrace> named_rule_battery_parse_trace(std::string_view all) {
    using namespace named_rule_battery_detail;
    if(!stream(all)) return {};
    constexpr std::string_view call = "LGAED_NAMED_RULE_BATTERY_CALL:";
    constexpr std::string_view finish = "LGAED_NAMED_RULE_BATTERY_END";
    NamedRuleBatteryTrace result;
    for(std::size_t position = 0; position < all.size();) {
        const auto begin = all.find(private_marker, position);
        if(begin == all.npos) { result.visible += all.substr(position); break; }
        result.visible += all.substr(position, begin - position);
        const auto newline = all.find('\n', begin);
        if(newline == all.npos) return {};
        auto record = all.substr(begin, newline - begin);
        if(record.ends_with('\r')) record.remove_suffix(1);
        if(record == finish) {
            if(result.completed_normally) return {};
            result.completed_normally = true;
        } else {
            if(result.completed_normally || !record.starts_with(call) || result.calls.size() >= 32) return {};
            record.remove_prefix(call.size());
            std::array<std::string_view, 4> fields{};
            for(std::size_t i = 0; i < fields.size(); ++i) {
                const auto comma = record.find(',');
                if((i + 1 == fields.size()) != (comma == record.npos)) return {};
                fields[i] = record.substr(0, comma);
                if(comma != record.npos) record.remove_prefix(comma + 1);
            }
            const auto index = decimal(fields[0]), input = decimal(fields[2]), value = decimal(fields[3]);
            if(!index || *index != static_cast<int>(result.calls.size()) ||
               !identifier(fields[1]) || !input || !value) return {};
            result.calls.push_back({std::string(fields[1]), *input, *value});
        }
        position = newline + 1;
    }
    return result;
}

inline NamedRuleBatteryVerdict named_rule_battery_verify(const NamedRuleBatterySpec& spec,
        std::span<const NamedRuleBatteryRun> runs, std::string_view program_frame) {
    const auto plan = named_rule_battery_plan(spec);
    if(!plan || program_frame.empty() || runs.size() != plan->size())
        return {false, "Faltan la batería original o sus alteraciones independientes."};
    for(std::size_t r = 0; r < runs.size(); ++r) {
        const auto& run = runs[r];
        if(run.program_frame != program_frame || run.phase != (*plan)[r] ||
           !run.compiled || !run.executed || !run.completed_normally)
            return {false, "La evidencia pertenece a otra revisión, fase o ejecución incompleta."};
        if(!named_rule_battery_detail::stream(run.stdout_bytes) ||
           !named_rule_battery_detail::stream(run.stderr_bytes))
            return {false, "El informe contiene bytes no admitidos o supera su límite."};
        if(run.calls.size() != spec.cases.size())
            return {false, "Deben ejecutarse todos los casos exactamente una vez, también tras un fallo."};
        std::vector<bool> seen(spec.cases.size());
        for(const auto& call : run.calls) {
            const auto found = std::ranges::find_if(spec.cases, [&](const auto& item) {
                return item.function == call.function && item.input == call.input;
            });
            if(found == spec.cases.end())
                return {false, "Se observó una llamada ajena a la batería declarada."};
            const auto index = static_cast<std::size_t>(found - spec.cases.begin());
            if(seen[index]) return {false, "Una llamada repetida no acredita un esperado independiente."};
            seen[index] = true;
            const int expected = run.phase.target == index
                ? named_rule_battery_detail::changed(found->expected) : found->expected;
            if(call.result != expected)
                return {false, "La traza no contiene los resultados de su fase independiente."};
        }
        const std::uint32_t expected_exit = r == 0 ? 0 : spec.failure_exit_code;
        if(run.exit_code != expected_exit)
            return {false, "Las comparaciones de la batería no determinan su código final."};
        if(r) {
            if((run.stdout_bytes.empty() && run.stderr_bytes.empty()) ||
               (run.stdout_bytes == runs.front().stdout_bytes &&
                run.stderr_bytes == runs.front().stderr_bytes))
                return {false, "La batería no comunica la discrepancia observada."};
            for(std::size_t prior = 1; prior < r; ++prior)
                if(run.stdout_bytes == runs[prior].stdout_bytes && run.stderr_bytes == runs[prior].stderr_bytes)
                    return {false, "El informe no permite distinguir el caso que falló."};
        }
    }
    return {true, "La batería real ejecuta sus casos y detecta sus alteraciones independientes."};
}

} // namespace lgaed::curriculum

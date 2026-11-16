#pragma once

#include "core/named_rule_battery_check.hpp"

namespace lgaed::curriculum {

inline constexpr std::string_view named_rule_constant_variation_objective_version =
    "named-rule-authenticated-local-constant-variations-v1";

namespace named_rule_constant_variation_detail {
inline bool purpose_word(std::string_view word) {
    return word.size() >= 3 && word.size() <= 32 &&
        std::ranges::all_of(word, [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        });
}
inline bool cases_valid(std::span<const NamedRuleProbeCase> cases,
        std::span<const NamedRuleRequirement> requirements) {
    if(cases.empty() || cases.size() > 32) return false;
    for(std::size_t i = 0; i < cases.size(); ++i) {
        if(std::ranges::find(requirements, cases[i].function,
                &NamedRuleRequirement::function) == requirements.end()) return false;
        for(std::size_t prior = 0; prior < i; ++prior)
            if(cases[prior].function == cases[i].function &&
               cases[prior].input == cases[i].input) return false;
    }
    for(const auto& requirement : requirements)
        if(std::ranges::find(cases, requirement.function,
                &NamedRuleProbeCase::function) == cases.end()) return false;
    return true;
}
inline bool structure_valid(const NamedRuleConstantVariationSpec& spec) {
    using named_rule_battery_detail::identifier;
    if(!named_rule_battery_detail::source_path(spec.source_path) ||
       spec.requirements.empty() || spec.requirements.size() > 8 ||
       spec.variations.empty() || spec.variations.size() > 8) return false;
    for(std::size_t i = 0; i < spec.requirements.size(); ++i) {
        const auto& requirement = spec.requirements[i];
        if(!identifier(requirement.function) || requirement.min_uses == 0 ||
           requirement.min_uses > 8 || requirement.purpose_words.empty() ||
           requirement.purpose_words.size() > 12) return false;
        for(std::size_t prior = 0; prior < i; ++prior)
            if(spec.requirements[prior].function == requirement.function) return false;
        for(std::size_t word = 0; word < requirement.purpose_words.size(); ++word) {
            if(!purpose_word(requirement.purpose_words[word])) return false;
            for(std::size_t prior = 0; prior < word; ++prior)
                if(requirement.purpose_words[prior] == requirement.purpose_words[word]) return false;
        }
    }
    for(std::size_t i = 0; i < spec.variations.size(); ++i) {
        const auto& variation = spec.variations[i];
        const auto requirement = std::ranges::find(spec.requirements, variation.function,
            &NamedRuleRequirement::function);
        if(requirement == spec.requirements.end() || variation.value == requirement->value ||
           !cases_valid(variation.cases, spec.requirements)) return false;
        for(std::size_t prior = 0; prior < i; ++prior)
            if(spec.variations[prior].function == variation.function &&
               spec.variations[prior].value == variation.value) return false;
    }
    return true;
}

struct Initializer { std::size_t begin = 0, end = 0; };
inline std::optional<Initializer> initializer(std::string_view source,
        const NamedRuleRequirement& requirement) {
    // Reuse the positioned lexer, not a search for a name or a number. It
    // accounts for comments, raw/ordinary literals, UTF-8 bytes and CRLF while
    // denying physical line splicing and unauthenticated preprocessing.
    const auto scanned = named_rule_battery_detail::tokens(source);
    if(!scanned) return {};
    named_rule_detail::Tokens tokens;
    std::vector<Initializer> positions;
    for(const auto& token : *scanned) {
        if(token.value.starts_with("directive:")) continue;
        // Keep literal tokens here: a literal inside an initializer must not
        // disappear and turn an unsupported expression into integer evidence.
        tokens.push_back(token.value);
        positions.push_back({token.begin, token.end});
    }
    using namespace named_rule_detail;
    const auto bounds = body(tokens, requirement.function);
    if(!bounds) return {};
    std::optional<LocalRule> selected;
    std::size_t depth = 0;
    for(auto at = bounds->open + 1; at < bounds->close; ++at) {
        if(op(tokens[at], "{")) { ++depth; continue; }
        if(op(tokens[at], "}")) { if(depth) --depth; continue; }
        if(depth) continue;
        const auto candidate = constant(tokens, at, bounds->close);
        if(!candidate || !role(candidate->name, requirement.purpose_words)) continue;
        if(selected) return {};
        selected = candidate;
    }
    if(!selected || selected->value != requirement.value ||
       selected->initializer_first >= selected->initializer_last ||
       selected->initializer_last > positions.size()) return {};
    const auto first = positions[selected->initializer_first].begin;
    const auto last = positions[selected->initializer_last - 1].end;
    if(first >= last || last > source.size()) return {};
    return Initializer{first, last};
}
} // namespace named_rule_constant_variation_detail

inline bool named_rule_constant_variation_spec_valid(
        const NamedRuleConstantVariationSpec& spec,
        std::span<const NamedRuleProbeCase> base_cases) {
    using namespace named_rule_constant_variation_detail;
    if(!structure_valid(spec) || !cases_valid(base_cases, spec.requirements)) return false;
    for(const auto& variation : spec.variations) {
        for(const auto& requirement : spec.requirements) {
            const bool target = requirement.function == variation.function;
            bool shared = false, changed = false;
            for(const auto& item : variation.cases) {
                if(item.function != requirement.function) continue;
                const auto original = std::ranges::find_if(base_cases, [&](const auto& candidate) {
                    return candidate.function == item.function && candidate.input == item.input;
                });
                if(original == base_cases.end()) continue;
                shared = true;
                if(item.expected != original->expected) {
                    if(!target) return false;
                    changed = true;
                }
            }
            // A shared changed answer witnesses the target's dependence on
            // its agreement. Every other function retains shared observations
            // so changing one agreement cannot silently substitute another.
            if(target ? !changed : !shared) return false;
        }
    }
    return true;
}

// Framing binds the source selection as well as every authored counterfactual.
// Agreement with the base cases is checked separately by spec_valid; callers
// append this frame only when the optional extension is present.
inline std::string named_rule_constant_variation_spec_frame(
        const NamedRuleConstantVariationSpec& spec) {
    if(!named_rule_constant_variation_detail::structure_valid(spec))
        throw std::invalid_argument("Invalid named-rule constant variation specification");
    std::string frame;
    const auto field = [&](std::string_view value) {
        frame += std::to_string(value.size()); frame += ':'; frame += value;
    };
    field(named_rule_constant_variation_objective_version); field(spec.source_path);
    field(std::to_string(spec.requirements.size()));
    for(const auto& requirement : spec.requirements) {
        field(requirement.function); field(std::to_string(requirement.value));
        field(std::to_string(requirement.min_uses));
        field(std::to_string(requirement.purpose_words.size()));
        for(const auto& word : requirement.purpose_words) field(word);
    }
    field(std::to_string(spec.variations.size()));
    for(const auto& variation : spec.variations) {
        field(variation.function); field(std::to_string(variation.value));
        field(std::to_string(variation.cases.size()));
        for(const auto& item : variation.cases) {
            field(item.function); field(std::to_string(item.input)); field(std::to_string(item.expected));
        }
    }
    return frame;
}

inline std::optional<NamedRuleExecutionProbe> named_rule_constant_variation_probe(
        const NamedRuleExecutionProbe& base, std::size_t index) {
    if(!base.constant_variations ||
       !named_rule_constant_variation_spec_valid(*base.constant_variations, base.cases) ||
       index >= base.constant_variations->variations.size()) return {};
    auto result = base;
    result.cases = base.constant_variations->variations[index].cases;
    // These are independent functional checks, not recursive variations or
    // mutations of the student's own comparisons/reporting battery.
    result.constant_variations.reset();
    result.battery_check.reset();
    return result;
}

// The caller supplies the submitted source at spec.source_path (or the single
// file), and must still prove the original and private compilations/executions.
// Every phase starts from that same original source, never a prior variation.
inline std::optional<std::string> named_rule_constant_variation_source(
        std::string_view original, const NamedRuleExecutionProbe& base,
        std::size_t index) {
    if(!named_rule_constant_variation_probe(base, index)) return {};
    const auto& spec = *base.constant_variations;
    if(!analyze_named_rule_sources(original, spec.requirements).source_evidence) return {};
    const auto& variation = spec.variations[index];
    const auto requirement = std::ranges::find(spec.requirements, variation.function,
        &NamedRuleRequirement::function);
    if(requirement == spec.requirements.end()) return {};
    const auto position = named_rule_constant_variation_detail::initializer(original, *requirement);
    if(!position) return {};
    std::string result(original.substr(0, position->begin));
    result += '('; result += std::to_string(variation.value); result += ')';
    result += original.substr(position->end);
    auto varied_requirements = spec.requirements;
    varied_requirements[static_cast<std::size_t>(requirement - spec.requirements.begin())].value =
        variation.value;
    if(!analyze_named_rule_sources(result, varied_requirements).source_evidence) return {};
    return result;
}

} // namespace lgaed::curriculum

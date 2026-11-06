#include "core/lesson.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace lgaed::curriculum {
namespace {
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
bool puzzle_type(BlockType type) {
    return type == BlockType::multiple_choice || type == BlockType::output_prediction ||
        type == BlockType::fill_code || type == BlockType::order_code ||
        type == BlockType::inline_fill_code || type == BlockType::drag_fill_code || type == BlockType::math_puzzle;
}
bool identifier(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || static_cast<unsigned char>(c) >= 128;
}
std::string_view trimmed(std::string_view value) {
    while (!value.empty() && whitespace(value.front())) value.remove_prefix(1);
    while (!value.empty() && whitespace(value.back())) value.remove_suffix(1);
    return value;
}
std::size_t quoted_end(std::string_view text, std::size_t start) {
    const char quote = text[start];
    for (auto index = start + 1; index < text.size(); ++index) {
        if (text[index] == '\\') ++index;
        else if (text[index] == quote) return index + 1;
    }
    return text.size();
}
std::optional<std::size_t> raw_end(std::string_view text, std::size_t start) {
    if (start && identifier(text[start - 1])) return {};
    for (const std::string_view prefix : {"u8R\"", "uR\"", "UR\"", "LR\"", "R\""}) {
        if (!text.substr(start).starts_with(prefix)) continue;
        const auto delimiter_start = start + prefix.size();
        auto body = delimiter_start;
        while (body < text.size() && text[body] != '(') {
            const auto c = static_cast<unsigned char>(text[body]);
            if (body - delimiter_start >= 16 || c < 0x21 || c > 0x7e || c == ')' || c == '\\') return {};
            ++body;
        }
        if (body == text.size()) return {};
        const auto closing = ")" + std::string(text.substr(delimiter_start, body - delimiter_start)) + "\"";
        const auto end = text.find(closing, body + 1);
        return end == std::string_view::npos ? text.size() : end + closing.size();
    }
    return {};
}
std::size_t number_end(std::string_view text, std::size_t start) {
    auto index = start + 1;
    while (index < text.size()) {
        const auto c = text[index];
        if (identifier(c) || c == '.') ++index;
        else if (c == '\'' && index + 1 < text.size() && identifier(text[index + 1])) index += 2;
        else if ((c == '+' || c == '-') && std::string_view("eEpP").find(text[index - 1]) != std::string_view::npos) ++index;
        else break;
    }
    return index;
}
std::size_t token_end(std::string_view text, std::size_t index) {
    if (const auto raw = raw_end(text, index)) return *raw;
    if (text[index] == '"' || text[index] == '\'') return quoted_end(text, index);
    if ((text[index] >= '0' && text[index] <= '9') ||
        (text[index] == '.' && index + 1 < text.size() && text[index + 1] >= '0' && text[index + 1] <= '9')) return number_end(text, index);
    return index + 1;
}
std::vector<bool> literal_holes(std::string_view text,std::string_view marker) {
    std::vector<std::pair<std::size_t,std::size_t>> ranges;
    auto i = std::size_t{0};
    while (i < text.size()) {
        if (text.substr(i).starts_with("//")) {
            const auto end = text.find('\n', i + 2); i = end == text.npos ? text.size() : end;
        } else if (text.substr(i).starts_with("/*")) {
            const auto end = text.find("*/", i + 2); i = end == text.npos ? text.size() : end + 2;
        } else {
            const bool literal = raw_end(text, i).has_value() || text[i] == '"' || text[i] == '\'';
            const auto end = token_end(text, i);
            if (literal) ranges.emplace_back(i,end);
            i = end;
        }
    }
    std::vector<bool> result;
    auto hole = text.find(marker);
    while (hole != text.npos) {
        result.push_back(std::ranges::any_of(ranges,[&](const auto& range) { return hole >= range.first && hole < range.second; }));
        hole = text.find(marker,hole + marker.size());
    }
    return result;
}
std::string decimal_magnitude(std::string_view value) {
    const auto first = value.find_first_not_of('0');
    return first == value.npos ? "0" : std::string(value.substr(first));
}
std::string add_magnitudes(std::string_view left,std::string_view right) {
    std::string result; int carry = 0;
    auto a = left.size(), b = right.size();
    while (a || b || carry) {
        auto sum = carry;
        if (a) sum += left[--a] - '0';
        if (b) sum += right[--b] - '0';
        result += static_cast<char>('0' + sum % 10); carry = sum / 10;
    }
    std::ranges::reverse(result); return result;
}
std::string subtract_magnitudes(std::string_view left,std::string_view right) {
    std::string result; int borrow = 0; auto a = left.size(), b = right.size();
    while (a) {
        int digit = left[--a] - '0' - borrow; if (b) digit -= right[--b] - '0';
        borrow = digit < 0 ? 1 : 0; if (borrow) digit += 10; result += static_cast<char>('0' + digit);
    }
    std::ranges::reverse(result); return decimal_magnitude(result);
}
std::string shifted_exponent(std::string exponent,std::int64_t shift) {
    bool negative = exponent.starts_with('-');
    auto magnitude = negative ? exponent.substr(1) : exponent;
    const bool shift_negative = shift < 0;
    const auto addition = std::to_string(shift_negative ? -shift : shift);
    if (negative == shift_negative) magnitude = add_magnitudes(magnitude,addition);
    else if (magnitude.size() > addition.size() || (magnitude.size() == addition.size() && magnitude >= addition))
        magnitude = subtract_magnitudes(magnitude,addition);
    else { magnitude = subtract_magnitudes(addition,magnitude); negative = shift_negative; }
    return magnitude == "0" ? "0" : (negative ? "-" : "") + magnitude;
}
struct DecimalValue {
    std::string significand,exponent;
    bool operator==(const DecimalValue&) const = default;
};
std::optional<DecimalValue> decimal_literal(std::string_view answer) {
    const auto text = trimmed(answer);
    // Same resource bound and exact decimal profile as Android CppDecimalLiteral.
    // No IEEE conversion or allocation proportional to an exponent's value.
    if (text.size() > 16384) throw std::length_error("C++ decimal literal exceeds the parser limit of 16384 characters");
    if (text.empty()) return {};
    std::size_t cursor = 0;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    const auto digits = [&]() {
        std::string result;
        while (cursor < text.size()) {
            const auto c = text[cursor];
            if (digit(c)) { result += c; ++cursor; }
            else if (!result.empty() && c == '\'' && cursor + 1 < text.size() && digit(text[cursor + 1])) ++cursor;
            else break;
        }
        return result;
    };
    const auto integral = digits();
    const bool point = cursor < text.size() && text[cursor] == '.';
    if (point) ++cursor;
    const auto fraction = point ? digits() : std::string{};
    if (integral.empty() && fraction.empty()) return {};
    const bool has_exponent = cursor < text.size() && (text[cursor] == 'e' || text[cursor] == 'E');
    std::string exponent = "0";
    if (has_exponent) {
        ++cursor; bool negative = false;
        if (cursor < text.size() && (text[cursor] == '+' || text[cursor] == '-')) negative = text[cursor++] == '-';
        const auto value = digits(); if (value.empty()) return {};
        exponent = decimal_magnitude(value); if (negative && exponent != "0") exponent = '-' + exponent;
    }
    if (cursor != text.size()) return {};
    if (!point && !has_exponent && ((integral.front() == '0' && integral.size() != 1) || integral.size() > 10 ||
        (integral.size() == 10 && integral > "2147483647"))) return {};
    const auto value = integral + fraction; const auto first = value.find_first_not_of('0');
    if (first == value.npos) return DecimalValue{"0","0"};
    const auto last = value.find_last_not_of('0');
    const auto shift = static_cast<std::int64_t>(value.size() - last - 1) - static_cast<std::int64_t>(fraction.size());
    return DecimalValue{value.substr(first,last - first + 1),shifted_exponent(std::move(exponent),shift)};
}
AnswerEvaluation verdict(const Block& block, bool correct) {
    return {correct, correct ? block.explanation : block.review_feedback};
}
std::vector<std::string_view> tokens(std::string_view value) {
    std::vector<std::string_view> result;
    while (!(value = trimmed(value)).empty()) {
        const auto end = value.find_first_of(" \t\r\n\f\v");
        result.push_back(value.substr(0, end));
        if (end == value.npos) break;
        value.remove_prefix(end);
    }
    return result;
}
}

std::optional<StatePosition> stable_block_position(const Lesson& lesson, std::string_view identity) {
    for (std::size_t page = 0; page < lesson.pages.size(); ++page)
        for (std::size_t block = 0; block < lesson.pages[page].blocks.size(); ++block)
            if (lesson.pages[page].blocks[block].stable_id == identity) return StatePosition{page,block};
    return {};
}
bool retired_block(const Lesson& lesson, std::string_view identity) {
    return std::ranges::any_of(lesson.state_layout.retired,
        [&](const auto& item) { return item.id == identity; });
}
std::optional<StatePosition> active_block_successor(const Lesson& lesson, std::string_view identity) {
    if (auto active = stable_block_position(lesson,identity)) return active;
    const StateRetirement* retired = nullptr;
    auto candidate = std::string(identity);
    for (std::size_t count = 0; count < lesson.state_layout.retired.size(); ++count) {
        const auto found = std::ranges::find_if(lesson.state_layout.retired,
            [&](const auto& item) { return item.id == candidate; });
        if (found == lesson.state_layout.retired.end()) break;
        if (!retired) retired = &*found;
        candidate = found->next;
        if (auto active = stable_block_position(lesson,candidate)) return active;
        if (candidate.empty()) break;
    }
    if (!retired) return {};
    for (std::size_t page = 0; page < lesson.pages.size(); ++page) {
        if (lesson.pages[page].stable_id != retired->page) continue;
        // A final archived block falls back to the preceding active content.
        if (!lesson.pages[page].blocks.empty()) return StatePosition{page,lesson.pages[page].blocks.size()-1};
        for (auto next = page + 1; next < lesson.pages.size(); ++next)
            if (!lesson.pages[next].blocks.empty()) return StatePosition{next,0};
        for (auto prior = page; prior > 0; --prior)
            if (!lesson.pages[prior-1].blocks.empty()) return StatePosition{prior-1,lesson.pages[prior-1].blocks.size()-1};
        break;
    }
    return {};
}
void validate_reference_relationships(const Lesson& lesson) {
    if (lesson.reference_after.empty()) return;
    if (!lesson.state_layout.version) throw std::invalid_argument("Reference relationships require stable state-layout identities");
    std::vector<std::string> activities, references;
    for (const auto& relation : lesson.reference_after) {
        if (std::ranges::find(activities,relation.activity) != activities.end()) throw std::invalid_argument("Duplicate reference activity");
        activities.push_back(relation.activity);
        const auto target = stable_block_position(lesson,relation.activity);
        if (!target) throw std::invalid_argument("Reference activity does not exist");
        const auto& activity = lesson.pages[target->page].blocks[target->block];
        if (!has_compiler_objective(activity))
            throw std::invalid_argument("Reference activity requires a compiler with an authored objective");
        if (relation.blocks.empty()) throw std::invalid_argument("Reference relationship has no presentation blocks");
        for (const auto& identity : relation.blocks) {
            if (identity == relation.activity || std::ranges::find(references,identity) != references.end())
                throw std::invalid_argument("Duplicate or self-referencing presentation block");
            references.push_back(identity);
            const auto position = stable_block_position(lesson,identity);
            if (!position) throw std::invalid_argument("Reference presentation block does not exist");
            const auto type = lesson.pages[position->page].blocks[position->block].type;
            if (type != BlockType::heading && type != BlockType::paragraph && type != BlockType::note && type != BlockType::code)
                throw std::invalid_argument("An exercise or editor cannot be hidden as a reference");
            if (position->page < target->page || (position->page == target->page && position->block <= target->block))
                throw std::invalid_argument("A reference must follow its activity in visual order");
        }
    }
}
std::optional<StatePosition> reference_activity(const Lesson& lesson, const Block& reference) {
    for (const auto& relation : lesson.reference_after)
        if (std::ranges::find(relation.blocks,reference.stable_id) != relation.blocks.end())
            return stable_block_position(lesson,relation.activity);
    return {};
}
void validate_activity_relationships(const Lesson& lesson) {
    if (lesson.activity_after.empty()) return;
    if (!lesson.state_layout.version) throw std::invalid_argument("Activity relationships require stable state-layout identities");
    std::vector<std::string> activities;
    for (const auto& relation : lesson.activity_after) {
        if (std::ranges::find(activities,relation.activity) != activities.end())
            throw std::invalid_argument("Duplicate dependent activity");
        activities.push_back(relation.activity);
        const auto target = stable_block_position(lesson,relation.activity);
        if (!target) throw std::invalid_argument("Dependent activity does not exist");
        const auto type = lesson.pages[target->page].blocks[target->block].type;
        if (type != BlockType::cpp_compiler && !puzzle_type(type))
            throw std::invalid_argument("Dependent activity requires a compiler or puzzle");
        if (relation.prerequisites.empty()) throw std::invalid_argument("Activity relationship has no prerequisites");
        std::vector<std::string> prerequisites;
        for (const auto& identity : relation.prerequisites) {
            if (identity == relation.activity || std::ranges::find(prerequisites,identity) != prerequisites.end())
                throw std::invalid_argument("Duplicate or self-referencing activity prerequisite");
            prerequisites.push_back(identity);
            const auto position = stable_block_position(lesson,identity);
            if (!position) throw std::invalid_argument("Activity prerequisite does not exist");
            const auto& activity = lesson.pages[position->page].blocks[position->block];
            if (!has_compiler_objective(activity))
                throw std::invalid_argument("Activity prerequisite requires a compiler with an authored objective");
            if (position->page > target->page || (position->page == target->page && position->block >= target->block))
                throw std::invalid_argument("An activity prerequisite must precede its dependent activity in visual order");
        }
    }
}
std::vector<StatePosition> activity_prerequisites(const Lesson& lesson, const Block& activity) {
    std::vector<StatePosition> result;
    for (const auto& relation : lesson.activity_after) {
        if (relation.activity != activity.stable_id) continue;
        for (const auto& identity : relation.prerequisites) {
            const auto position = stable_block_position(lesson,identity);
            if (!position) throw std::invalid_argument("Activity prerequisite does not exist");
            result.push_back(*position);
        }
        break;
    }
    return result;
}
bool compiler_is_puzzle_prerequisite(const Lesson& lesson, const Block& compiler) {
    if (!has_compiler_objective(compiler) || lesson.activity_after.empty()) return false;
    std::map<std::string,BlockType> types;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) types.emplace(block.stable_id,block.type);
    std::map<std::string,std::vector<std::string_view>> successors;
    for (const auto& relation : lesson.activity_after) for (const auto& prerequisite : relation.prerequisites)
        successors[prerequisite].push_back(relation.activity);
    std::vector<std::string_view> pending{compiler.stable_id};
    std::set<std::string_view> visited;
    while (!pending.empty()) {
        const auto identity = pending.back(); pending.pop_back();
        if (!visited.insert(identity).second) continue;
        const auto descendants = successors.find(std::string(identity));
        if (descendants == successors.end()) continue;
        for (const auto target : descendants->second) {
            const auto found = types.find(std::string(target));
            if (found == types.end()) continue;
            if (puzzle_type(found->second)) return true;
            if (found->second == BlockType::cpp_compiler) pending.push_back(target);
        }
    }
    return false;
}
std::string reference_objective_frame(const Lesson& lesson, const Block& activity) {
    if (!has_compiler_objective(activity))
        throw std::invalid_argument("Only an authored compiler objective can unlock a reference");
    std::string result = "reference-objective-v1/";
    const auto token = [&](std::string_view text) { result += std::to_string(text.size()) + ':'; result += text; };
    token(lesson.id); token(lesson.locale); token(activity.stable_id);
    token(activity.address.addition_namespace);
    token(std::to_string(activity.address.page)); token(std::to_string(activity.address.block));
    token(activity.initial_code); token(activity.language);
    if (activity.compile_check) {
        token("compile-check-v1"); token(activity.compile_check->diagnostic);
        return result;
    }
    // Keep historical single-file identities byte-for-byte compatible. A
    // project objective additionally owns all authored files and build inputs.
    if(!activity.project_files.empty()){token("cpp-project-v1");token(cpp_project_frame(activity.project_files,activity.source_files,activity.cmake));}
    token(activity.output_check->input_comparison == TextComparison::tokens ? "tokens" : "exact");
    result += std::to_string(activity.output_check->cases.size()) + '[';
    for (const auto& item : activity.output_check->cases) {
        token(item.standard_input); token(item.executable.value_or(""));
        bool equivalent_crt=false;
        if(activity.crt_output_original&&lesson.locale=="es"
            &&lesson.id=="cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia"
            &&activity.stable_id=="block_0005_0004"&&activity.output_check->cases.size()==1
            &&item.outputs.size()==2&&item.outputs.front()==*activity.crt_output_original){
            std::string expanded;
            for(const char value:*activity.crt_output_original){if(value=='\n')expanded+='\r';expanded+=value;}
            equivalent_crt=item.outputs.back()==expanded;
        }
        result += std::to_string(equivalent_crt?1:item.outputs.size()) + '[';
        for(std::size_t index=0;index<(equivalent_crt?1:item.outputs.size());++index)token(item.outputs[index]);
        result += ']';
        // Keep stdout-only proof identities byte-for-byte compatible, while a
        // newly authored stderr objective always requires fresh native proof.
        if (item.standard_error == StandardErrorExpectation::non_blank) token("standard-error/nonBlank/v1");
        // A normal nonzero termination is a new objective, never an inherited
        // stdout-only proof. Omitted/explicit zero keeps every old byte intact.
        if (item.exit_code != 0) { token("process-exit-code/v1"); token(std::to_string(item.exit_code)); }
    }
    result += ']';
    return result;
}

std::string exercise_key(const Lesson& lesson, const Block& block) {
    const auto& address = block.address;
    return "exercise_" + lesson.id + (address.addition_namespace.empty() ? "" : "_state_v2_" + address.addition_namespace) +
        "_page_" + std::to_string(address.page) + "_block_" + std::to_string(address.block);
}
std::vector<EmphasisRange> emphasis_ranges(std::string_view text, std::span<const Emphasis> entries) {
    std::vector<EmphasisRange> result;
    for (const auto& entry : entries) {
        if (entry.text.empty() || (entry.style != "bold" && entry.style != "italic" && entry.style != "bold_italic")) continue;
        std::vector<std::size_t> matches;
        auto offset = text.find(entry.text);
        while (offset != text.npos) { matches.push_back(offset); offset = text.find(entry.text, offset + 1); }
        std::optional<std::size_t> start;
        if (entry.occurrence && entry.occurrence <= matches.size()) start = matches[entry.occurrence - 1];
        else if (!entry.occurrence && matches.size() == 1) start = matches.front();
        if (start) result.push_back({*start, *start + entry.text.size(), entry.style});
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) { return left.start < right.start || (left.start == right.start && left.end < right.end); });
    return result;
}
std::string draft_key(std::string_view route_id, const Lesson& lesson, const Block& block) {
    const auto& address = block.address;
    return std::string(route_id) + "/" + lesson.id + "/" +
        (address.addition_namespace.empty() ? "" : "state-layout-v2/" + address.addition_namespace + "/") +
        std::to_string(address.page) + "/" + std::to_string(address.block);
}
std::size_t storage_page(const Lesson& lesson, std::size_t page) {
    const auto& layout = lesson.state_layout;
    if (page >= lesson.pages.size()) throw std::out_of_range("Unknown display page");
    if (!layout.version) return page;
    const auto id = layout.current.at(page).id;
    for (std::size_t i = 0; i < layout.origin.size(); ++i) if (layout.origin[i].id == id) return i;
    throw std::logic_error("Missing state page");
}
std::size_t display_page(const Lesson& lesson, std::size_t page) {
    const auto& layout = lesson.state_layout;
    if (!layout.version) return std::min(page, lesson.pages.size() - 1);
    const auto id = layout.origin.at(std::min(page, layout.origin.size() - 1)).id;
    for (std::size_t i = 0; i < layout.current.size(); ++i) if (layout.current[i].id == id) return i;
    throw std::logic_error("Missing state page");
}
std::string normalize_cpp_answer(std::string_view text) {
    std::string result;
    bool pending_space = false;
    std::size_t index = 0, comment_end = 0;
    while (index < text.size()) {
        if (whitespace(text[index])) { pending_space = true; ++index; continue; }
        if (pending_space && !result.empty()) result += ' ';
        pending_space = false;
        if (index >= comment_end) {
            if (text.substr(index).starts_with("//")) {
                const auto end = text.find('\n', index + 2); comment_end = end == text.npos ? text.size() : end;
            } else if (text.substr(index).starts_with("/*")) {
                const auto end = text.find("*/", index + 2); comment_end = end == text.npos ? text.size() : end + 2;
            } else {
                const auto end = token_end(text, index);
                result.append(text.substr(index, end - index)); index = end; continue;
            }
        }
        result += text[index++];
    }
    return result;
}
AnswerEvaluation evaluate_text_answer(const Block& block, std::string_view answer) {
    if (block.type != BlockType::output_prediction && block.type != BlockType::fill_code)
        throw std::invalid_argument("Block has no text answer contract");
    if (block.type == BlockType::fill_code && block.answer_comparison == AnswerComparison::cpp_decimal_literal_value) {
        if (block.answers.empty()) throw std::invalid_argument("Missing canonical decimal literals");
        const auto expected = decimal_literal(block.answers.front());
        if (!expected) throw std::invalid_argument("Accepted answer must be a decimal int/double literal");
        for (const auto& candidate : block.answers)
            if (decimal_literal(candidate) != expected) throw std::invalid_argument("Accepted decimal literals must have the same exact value");
        return verdict(block,decimal_literal(answer) == expected);
    }
    const auto holes = block.type == BlockType::fill_code ? literal_holes(block.code,"___") : std::vector<bool>{};
    const bool literal_hole = std::ranges::any_of(holes,[](bool literal) { return literal; });
    const auto normalize = [&](std::string_view value) -> std::string {
        if (block.type == BlockType::output_prediction)
            return std::string(block.output_comparison == TextComparison::exact ? value : trimmed(value));
        return literal_hole ? std::string(value) : normalize_cpp_answer(value);
    };
    const auto actual = normalize(answer);
    const auto correct = std::ranges::any_of(block.answers, [&](const auto& candidate) { return normalize(candidate) == actual; });
    return verdict(block, correct);
}
std::string incorrect_feedback(const Block& block,std::optional<std::size_t> selected) {
    if(block.type==BlockType::multiple_choice&&selected&&*selected<block.review_feedback_options.size())
        return block.review_feedback_options[*selected];
    return block.review_feedback;
}
AnswerEvaluation evaluate_choice(const Block& block, std::size_t option) {
    if (block.type != BlockType::multiple_choice || !block.correct_index || option >= block.options.size())
        throw std::invalid_argument("Invalid choice answer");
    const bool correct=option==*block.correct_index;
    return {correct,correct?block.explanation:incorrect_feedback(block,option)};
}
AnswerEvaluation evaluate_order(const Block& block, std::span<const std::size_t> order) {
    if (block.type != BlockType::order_code) throw std::invalid_argument("Invalid order answer");
    if (order.size() != block.fragments.size() || block.correct_order.size() != block.fragments.size()) return verdict(block, false);
    auto seen = std::vector<bool>(order.size());
    for (const auto index : order) {
        if (index >= order.size() || seen[index]) return verdict(block, false);
        seen[index] = true;
    }
    // Every physical card must occur once. A reviewed group exchanges its
    // canonical text slots only; text-identical cards remain interchangeable
    // even when one physical index lies outside that group. Compare text bags
    // in those slots rather than inferring C++ semantics or normalizing source.
    const auto ungrouped = order.size();
    std::vector<std::size_t> groups(order.size(),ungrouped);
    std::vector<std::multiset<std::string_view>> expected_groups(block.order_equivalence_groups.size()), actual_groups(expected_groups.size());
    for (std::size_t group=0;group<block.order_equivalence_groups.size();++group) {
        const auto& members=block.order_equivalence_groups[group];
        if(members.size()<2)return verdict(block,false);
        for(const auto index:members) {
            if(index>=order.size()||groups[index]!=ungrouped)return verdict(block,false);
            groups[index]=group;expected_groups[group].insert(block.fragments[index]);
        }
    }
    std::fill(seen.begin(), seen.end(), false);
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto expected = block.correct_order[i];
        if (expected >= order.size() || seen[expected]) return verdict(block, false);
        seen[expected] = true;
        if(groups[expected]!=ungrouped)actual_groups[groups[expected]].insert(block.fragments[order[i]]);
        else if (block.fragments[order[i]] != block.fragments[expected]) return verdict(block, false);
    }
    if(actual_groups!=expected_groups)return verdict(block,false);
    return verdict(block, true);
}
std::string order_representation_frame(const Block& block) {
    if (block.type != BlockType::order_code) throw std::invalid_argument("Invalid order representation");
    std::string result = "order-representation-v1/" + std::to_string(block.fragments.size()) + '[';
    for (const auto& fragment : block.fragments) result += std::to_string(fragment.size()) + ':' + fragment;
    result += "]/" + std::to_string(block.correct_order.size()) + '[';
    for (const auto index : block.correct_order) result += std::to_string(index) + ';';
    result += ']';
    return result;
}
std::string choice_representation_frame(const Block& block) {
    if(block.type!=BlockType::multiple_choice||!block.correct_index||*block.correct_index>=block.options.size())
        throw std::invalid_argument("Invalid choice representation");
    std::string result="choice-options-v1/"+std::to_string(block.options.size())+'[';
    for(const auto& option:block.options)result+=std::to_string(option.size())+':'+option;
    return result+"]/"+std::to_string(*block.correct_index);
}
AnswerEvaluation evaluate_hole_answers(const Block& block,std::span<const std::string> answers) {
    if (block.type != BlockType::inline_fill_code) throw std::invalid_argument("Invalid inline fill answer");
    const auto holes = literal_holes(block.code_template,"[]");
    if (holes.size() != block.answers.size() || (!block.answer_alternatives.empty() && block.answer_alternatives.size() != holes.size()))
        throw std::invalid_argument("Invalid inline fill configuration");
    if (answers.size() != holes.size()) return verdict(block,false);
    for (std::size_t index = 0; index < holes.size(); ++index) {
        const auto normalize = [&](std::string_view value) { return holes[index] ? std::string(value) : normalize_cpp_answer(value); };
        const auto actual = normalize(answers[index]);
        if (block.answer_alternatives.empty()) {
            if (actual != normalize(block.answers[index])) return verdict(block,false);
        } else {
            const auto& accepted = block.answer_alternatives[index];
            if (accepted.empty() || accepted.front() != block.answers[index]) throw std::invalid_argument("Canonical inline answer must remain first");
            if (!std::ranges::any_of(accepted,[&](const auto& value) { return normalize(value) == actual; })) return verdict(block,false);
        }
    }
    return verdict(block,true);
}
AnswerEvaluation evaluate_drag_answers(const Block& block,std::span<const std::size_t> choices) {
    if (block.type != BlockType::drag_fill_code) throw std::invalid_argument("Invalid drag fill answer");
    if (choices.size() != block.correct_order.size()) return verdict(block,false);
    std::vector<std::size_t> used;
    for (std::size_t index = 0; index < choices.size(); ++index) {
        if (block.correct_order[index] >= block.choices.size()) throw std::invalid_argument("Invalid canonical drag choice");
        if (choices[index] >= block.choices.size() || std::ranges::find(used,choices[index]) != used.end() ||
            block.choices[choices[index]] != block.choices[block.correct_order[index]]) return verdict(block,false);
        used.push_back(choices[index]);
    }
    return verdict(block,true);
}
bool uses_single_line_answer(const Block& block) {
    return !block.answers.empty() && std::ranges::all_of(block.answers, [](const auto& text) { return text.find_first_of("\r\n") == text.npos; });
}
OutputEvaluation evaluate_output_check(const OutputCheck& check, const FreshExecution& execution, std::uint64_t current_revision) {
    if (execution.source_revision != current_revision || !execution.compilation_succeeded ||
        (!execution.execution_succeeded && !execution.execution_completed) ||
        execution.exit_code < 0 || execution.exit_code > 255 ||
        (execution.exit_code != 0 && !execution.execution_completed)) return {};
    const OutputCase* selected = nullptr;
    for (const auto& item : check.cases) {
        const bool input_matches = check.input_comparison == TextComparison::tokens ?
            tokens(item.standard_input) == tokens(execution.standard_input) : item.standard_input == execution.standard_input;
        if (input_matches && (!item.executable || item.executable == execution.executable)) {
            if (selected) throw std::logic_error("Ambiguous output check cases");
            selected = &item;
        }
    }
    if (!selected) return {};
    if (selected->exit_code < 0 || selected->exit_code > 255 ||
        (selected->exit_code == 0 && execution.exit_code != 0)) return {};
    const bool stdout_correct = std::ranges::find(selected->outputs, execution.standard_output) != selected->outputs.end();
    const bool stderr_correct = selected->standard_error == StandardErrorExpectation::ignored ||
        !trimmed(execution.standard_error).empty();
    const bool exit_code_correct = execution.exit_code == selected->exit_code;
    if (!exit_code_correct) return {OutputVerdict::incorrect, {}, stdout_correct, stderr_correct, false};
    if (stdout_correct && stderr_correct)
        return {OutputVerdict::correct, selected->explanation, true, true};
    if (stdout_correct)
        return {OutputVerdict::incorrect, "La salida estándar es correcta; falta el diagnóstico solicitado en stderr.", true, false};
    for (const auto& rule : selected->review_feedback)
        if (std::ranges::find(rule.outputs, execution.standard_output) != rule.outputs.end()) return {OutputVerdict::incorrect, rule.text, false, stderr_correct};
    return {OutputVerdict::incorrect, {}, false, stderr_correct};
}

} // namespace lgaed::curriculum

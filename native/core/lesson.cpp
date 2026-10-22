#include "core/lesson.hpp"

#include <algorithm>
#include <stdexcept>

namespace lgaed::curriculum {
namespace {
bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
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
bool hole_inside_literal(std::string_view text) {
    auto i = std::size_t{0};
    while (i < text.size()) {
        if (text.substr(i).starts_with("//")) {
            const auto end = text.find('\n', i + 2); i = end == text.npos ? text.size() : end;
        } else if (text.substr(i).starts_with("/*")) {
            const auto end = text.find("*/", i + 2); i = end == text.npos ? text.size() : end + 2;
        } else {
            const bool literal = raw_end(text, i).has_value() || text[i] == '"' || text[i] == '\'';
            const auto end = token_end(text, i);
            if (literal && text.substr(i, end - i).find("___") != text.npos) return true;
            i = end;
        }
    }
    return false;
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
        if (activity.type != BlockType::cpp_compiler || !activity.output_check)
            throw std::invalid_argument("Reference activity requires a compiler with an authored output check");
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
std::string reference_objective_frame(const Lesson& lesson, const Block& activity) {
    if (activity.type != BlockType::cpp_compiler || !activity.output_check)
        throw std::invalid_argument("Only an authored compiler output check can unlock a reference");
    std::string result = "reference-objective-v1/";
    const auto token = [&](std::string_view text) { result += std::to_string(text.size()) + ':'; result += text; };
    token(lesson.id); token(lesson.locale); token(activity.stable_id);
    token(activity.address.addition_namespace);
    token(std::to_string(activity.address.page)); token(std::to_string(activity.address.block));
    token(activity.initial_code); token(activity.language);
    token(activity.output_check->input_comparison == TextComparison::tokens ? "tokens" : "exact");
    result += std::to_string(activity.output_check->cases.size()) + '[';
    for (const auto& item : activity.output_check->cases) {
        token(item.standard_input); token(item.executable.value_or(""));
        result += std::to_string(item.outputs.size()) + '[';
        for (const auto& output : item.outputs) token(output);
        result += ']';
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
    const bool literal_hole = block.type == BlockType::fill_code && hole_inside_literal(block.code);
    const auto normalize = [&](std::string_view value) -> std::string {
        if (block.type == BlockType::output_prediction)
            return std::string(block.output_comparison == TextComparison::exact ? value : trimmed(value));
        return literal_hole ? std::string(value) : normalize_cpp_answer(value);
    };
    const auto actual = normalize(answer);
    const auto correct = std::ranges::any_of(block.answers, [&](const auto& candidate) { return normalize(candidate) == actual; });
    return verdict(block, correct);
}
AnswerEvaluation evaluate_choice(const Block& block, std::size_t option) {
    if (block.type != BlockType::multiple_choice || !block.correct_index || option >= block.options.size())
        throw std::invalid_argument("Invalid choice answer");
    return verdict(block, option == *block.correct_index);
}
AnswerEvaluation evaluate_order(const Block& block, std::span<const std::size_t> order) {
    if (block.type != BlockType::order_code) throw std::invalid_argument("Invalid order answer");
    if (order.size() != block.fragments.size()) return verdict(block, false);
    auto seen = std::vector<bool>(order.size());
    for (const auto index : order) {
        if (index >= order.size() || seen[index]) return verdict(block, false);
        seen[index] = true;
    }
    return verdict(block, std::ranges::equal(order, block.correct_order));
}
bool uses_single_line_answer(const Block& block) {
    return !block.answers.empty() && std::ranges::all_of(block.answers, [](const auto& text) { return text.find_first_of("\r\n") == text.npos; });
}
OutputEvaluation evaluate_output_check(const OutputCheck& check, const FreshExecution& execution, std::uint64_t current_revision) {
    if (execution.source_revision != current_revision || !execution.compilation_succeeded ||
        !execution.execution_succeeded || execution.exit_code != 0) return {};
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
    if (std::ranges::find(selected->outputs, execution.standard_output) != selected->outputs.end())
        return {OutputVerdict::correct, selected->explanation};
    for (const auto& rule : selected->review_feedback)
        if (std::ranges::find(rule.outputs, execution.standard_output) != rule.outputs.end()) return {OutputVerdict::incorrect, rule.text};
    return {OutputVerdict::incorrect, {}};
}

} // namespace lgaed::curriculum

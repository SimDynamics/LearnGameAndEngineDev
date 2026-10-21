#include "platform/windows/content_loader.hpp"
#include "platform/windows/winrt_apartment.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <roapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace lgaed::windows {
namespace {
using namespace winrt::Windows::Data::Json;
using namespace curriculum;
[[noreturn]] void invalid(std::string_view message) { throw std::runtime_error("Curriculum: " + std::string(message)); }
void require(bool condition, std::string_view message) { if (!condition) invalid(message); }
std::string utf8(const winrt::hstring& value) { return winrt::to_string(value); }
winrt::hstring key(std::string_view value) { return winrt::to_hstring(value); }
std::string text(const JsonObject& value, std::string_view name) {
    const auto property = value.GetNamedValue(key(name));
    require(property.ValueType() == JsonValueType::String, "Expected a string");
    return utf8(property.GetString());
}
std::string optional_text(const JsonObject& value, std::string_view name) {
    return value.HasKey(key(name)) ? text(value, name) : std::string{};
}
JsonArray array(const JsonObject& value, std::string_view name) { return value.GetNamedArray(key(name)); }
std::size_t integer(const IJsonValue& value, std::size_t maximum = 1000000) {
    require(value.ValueType() == JsonValueType::Number, "Expected an integer");
    const double number = value.GetNumber();
    require(std::isfinite(number) && number >= 0 && number <= static_cast<double>(maximum) && std::trunc(number) == number,
        "Integer outside supported bounds");
    return static_cast<std::size_t>(number);
}
void allowed_keys(const JsonObject& object, std::initializer_list<std::string_view> names, bool exact = false) {
    std::set<std::string> expected;
    for (const auto name : names) expected.insert(std::string(name));
    for (const auto& entry : object) require(expected.contains(utf8(entry.Key())), "Unknown contract property");
    if (exact) require(object.Size() == expected.size(), "Incomplete contract");
}
std::vector<std::string> strings(const JsonArray& value, bool require_nonempty = false, bool allow_empty = true) {
    if (require_nonempty) require(value.Size() > 0, "An answer list cannot be empty");
    std::vector<std::string> result;
    for (const auto& item : value) {
        require(item.ValueType() == JsonValueType::String, "Expected string list");
        result.push_back(utf8(item.GetString()));
        if (!allow_empty) require(result.back().find_first_not_of(" \t\r\n\f\v") != std::string::npos, "Blank answer");
    }
    return result;
}
bool valid_id(std::string_view id) {
    return !id.empty() && id.size() <= 128 && std::ranges::all_of(id, [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
bool valid_hash(std::string_view hash) {
    return hash.size() == 64 && std::ranges::all_of(hash, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string sha256(std::string_view data) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "Cannot initialize SHA-256");
    struct Close { BCRYPT_ALG_HANDLE value; ~Close() { BCryptCloseAlgorithmProvider(value, 0); } } close{algorithm};
    require(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "Cannot create SHA-256");
    struct Destroy { BCRYPT_HASH_HANDLE value; ~Destroy() { BCryptDestroyHash(value); } } destroy{hash};
    require(data.size() <= std::numeric_limits<ULONG>::max(), "Hash input too large");
    require(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), 0) >= 0, "Cannot hash content");
    unsigned char digest[32]{};
    require(BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, "Cannot finish SHA-256");
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) { result += digits[byte >> 4]; result += digits[byte & 15]; }
    return result;
}
void canonical(const IJsonValue& value, std::string& output, unsigned depth = 0) {
    require(depth <= 64, "JSON nesting too deep");
    const auto string = [&](const winrt::hstring& source) {
        const auto bytes = utf8(source); output += 's' + std::to_string(bytes.size()) + ':'; output += bytes;
    };
    switch (value.ValueType()) {
    case JsonValueType::Null: output += 'z'; break;
    case JsonValueType::Boolean: output += value.GetBoolean() ? 't' : 'f'; break;
    case JsonValueType::String: string(value.GetString()); break;
    case JsonValueType::Array:
        output += '['; for (const auto& item : value.GetArray()) canonical(item, output, depth + 1); output += ']'; break;
    case JsonValueType::Object: {
        output += '{';
        std::map<std::wstring, IJsonValue> sorted;
        for (const auto& property : value.GetObject()) sorted.emplace(std::wstring(property.Key()), property.Value());
        for (const auto& [name, item] : sorted) { string(winrt::hstring(name)); canonical(item, output, depth + 1); }
        output += '}'; break;
    }
    case JsonValueType::Number: {
        const auto number = value.GetNumber();
        // Do not round a state-bearing decimal through binary floating point.
        // This initial curriculum contract contains integer indices only.
        require(std::isfinite(number) && std::trunc(number) == number && std::abs(number) <= 9007199254740991.0,
            "Non-integral state-bearing JSON numbers need an exact decimal adapter");
        output += 'n' + std::to_string(static_cast<std::int64_t>(number)) + ';'; break;
    }
    default: invalid("Unsupported JSON value");
    }
}
std::string fingerprint(const IJsonValue& value) { std::string framing; canonical(value, framing); return sha256(framing); }
std::string state_fingerprint(const JsonObject& original) {
    const auto value = JsonObject::Parse(original.Stringify());
    const auto type = text(value, "type");
    if (type == "heading" || type == "paragraph" || type == "note") {
        value.Remove(L"text"); if (value.HasKey(L"emphasis")) value.Remove(L"emphasis");
    } else if (type == "multiple_choice" || type == "fill_code" || type == "output_prediction" || type == "order_code" ||
               type == "drag_fill_code" || type == "inline_fill_code") {
        for (const auto name : {L"question", L"explanation", L"reviewFeedback"}) if (value.HasKey(name)) value.Remove(name);
    } else if (type == "cpp_compiler" && value.HasKey(L"outputCheck")) {
        for (const auto& item : value.GetNamedObject(L"outputCheck").GetNamedArray(L"cases")) {
            const auto object = item.GetObject();
            if (object.HasKey(L"explanation")) object.Remove(L"explanation");
            if (object.HasKey(L"reviewFeedback")) object.Remove(L"reviewFeedback");
        }
    }
    return fingerprint(value);
}
std::vector<StatePage> read_pages(const JsonArray& values, std::map<std::string, std::string>* signatures) {
    require(values.Size() > 0 && values.Size() <= 2048, "Invalid state page count");
    std::vector<StatePage> result;
    std::set<std::string> pages, blocks;
    for (const auto& value : values) {
        const auto item = value.GetObject(); allowed_keys(item, {"id", "blocks"}, true);
        StatePage page; page.id = text(item, "id");
        require(valid_id(page.id) && pages.insert(page.id).second, "Invalid or duplicate page identity");
        for (const auto& block_value : array(item, "blocks")) {
            std::string id;
            if (signatures) {
                const auto block = block_value.GetObject(); allowed_keys(block, {"id", "sha256"}, true);
                id = text(block, "id"); const auto digest = text(block, "sha256");
                require(valid_hash(digest) && signatures->emplace(id, digest).second, "Invalid or duplicate signature");
            } else {
                require(block_value.ValueType() == JsonValueType::String, "Invalid block identity"); id = utf8(block_value.GetString());
            }
            require(valid_id(id) && blocks.insert(id).second && blocks.size() <= 100000, "Invalid or duplicate block identity");
            page.blocks.push_back(std::move(id));
        }
        result.push_back(std::move(page));
    }
    return result;
}
std::string framing(const std::vector<StatePage>& pages) {
    std::string result;
    for (const auto& page : pages) {
        if (!result.empty()) result += '|'; result += page.id + ':';
        for (std::size_t b = 0; b < page.blocks.size(); ++b) { if (b) result += ','; result += page.blocks[b]; }
    }
    return result;
}
bool known_locale(std::string_view value) { return value == "es" || value == "en" || value == "fr"; }
void require_integer_json_numbers(std::string_view source) {
    // Windows.Data.Json owns full JSON syntax parsing. This additional lexical
    // guard prevents its double API from silently rounding an exact decimal
    // before the Android-compatible state fingerprint is calculated.
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const auto c = source[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') { quoted = true; continue; }
        if ((c >= '0' && c <= '9') || c == '-') {
            const auto end = source.find_first_of(" \t\r\n,]}:", i);
            const auto token = source.substr(i, end == source.npos ? source.size() - i : end - i);
            require(token.find_first_of(".eE") == token.npos, "Decimal JSON values require an exact decimal content adapter");
            if (end == source.npos) break;
            i = end - 1;
        }
    }
}
StateLayout read_layout(const JsonObject& metadata, const JsonArray& pages, std::string_view locale) {
    StateLayout result;
    result.raw_json = utf8(metadata.Stringify());
    result.version = static_cast<int>(integer(metadata.GetNamedValue(L"version"), 2));
    require(result.version == 1 || result.version == 2, "Unsupported state layout version");
    if (result.version == 1) allowed_keys(metadata, {"version", "origin", "current"}, true);
    else allowed_keys(metadata, {"version", "locale", "origin", "current", "family", "additions", "editorial"}, true);
    result.origin = read_pages(array(metadata, "origin"), &result.signatures);
    result.current = read_pages(array(metadata, "current"), nullptr);
    require(result.current.size() == pages.Size(), "State map does not match visible pages");
    result.legacy_lineage = sha256(framing(result.origin));
    if (result.version == 2) {
        result.locale = text(metadata, "locale"); require(result.locale == locale && known_locale(locale), "State map belongs to another language");
        const auto variants = metadata.GetNamedObject(L"family");
        require(variants.Size() > 0 && variants.Size() <= 3, "Invalid family count");
        for (const auto& entry : variants) {
            const auto language = utf8(entry.Key()); require(known_locale(language), "Unknown family language");
            const auto record = entry.Value().GetObject(); allowed_keys(record, {"sha256", "origin"}, true);
            StateFamily family; family.sha256 = text(record, "sha256"); require(valid_hash(family.sha256), "Invalid family hash");
            family.origin = read_pages(array(record, "origin"), &family.signatures);
            result.family.emplace(language, std::move(family));
        }
        require(result.family.contains(result.locale) && framing(result.family.at(result.locale).origin) == framing(result.origin) &&
                result.family.at(result.locale).signatures == result.signatures, "Selected historical family changed");
        auto reserved_page = result.origin.size();
        for (const auto& [language, family] : result.family) { (void)language; reserved_page = std::max(reserved_page, family.origin.size()); }
        std::set<std::pair<std::size_t, std::size_t>> slots;
        const auto additions = array(metadata, "additions"); require(additions.Size() <= 100000, "Too many additions");
        for (const auto& value : additions) {
            const auto item = value.GetObject(); allowed_keys(item, {"id", "sha256", "storage", "progressPage"}, true);
            StateAddition addition; addition.id = text(item, "id"); addition.sha256 = text(item, "sha256"); addition.progress_page = text(item, "progressPage");
            require(valid_id(addition.id) && valid_hash(addition.sha256) && result.signatures.emplace(addition.id, addition.sha256).second,
                "Invalid or colliding new identity");
            const auto storage = item.GetNamedObject(L"storage"); allowed_keys(storage, {"page", "block"}, true);
            addition.storage = {integer(storage.GetNamedValue(L"page")), integer(storage.GetNamedValue(L"block"), 100000)};
            require(addition.storage.page >= reserved_page && slots.emplace(addition.storage.page, addition.storage.block).second,
                "Colliding new storage slot");
            require(std::ranges::any_of(result.origin, [&](const auto& page) { return page.id == addition.progress_page; }), "Unknown addition progress page");
            result.additions.push_back(std::move(addition));
        }
        std::string lineage = "v2/" + result.locale + '/';
        bool first = true;
        for (const auto& [language, family] : result.family) {
            if (!first) lineage += ';'; first = false;
            lineage += language + ':' + family.sha256 + ':' + framing(family.origin) + ':';
            bool first_signature = true;
            for (const auto& [id, digest] : family.signatures) { if (!first_signature) lineage += ','; first_signature = false; lineage += id + '=' + digest; }
        }
        result.lineage = sha256(lineage);
    } else result.lineage = result.legacy_lineage;

    std::set<std::string> origin_pages, current_pages, current_blocks;
    for (const auto& page : result.origin) origin_pages.insert(page.id);
    for (const auto& page : result.current) { current_pages.insert(page.id); current_blocks.insert(page.blocks.begin(), page.blocks.end()); }
    require(current_pages == origin_pages && current_blocks.size() == result.signatures.size(), "State map loses historical identities");
    for (const auto& id : current_blocks) require(result.signatures.contains(id), "Unknown state block");
    std::map<std::string, std::string> editorial;
    if (result.version == 2) {
        std::map<std::string, std::set<std::string>> seen;
        for (const auto& value : array(metadata, "editorial")) {
            const auto change = value.GetObject(); allowed_keys(change, {"id", "beforeSha256", "afterSha256"}, true);
            const auto id = text(change, "id"), before = text(change, "beforeSha256"), after = text(change, "afterSha256");
            require(result.signatures.contains(id) && valid_hash(before) && valid_hash(after) && before != after, "Invalid editorial record");
            if (!seen.contains(id)) seen[id].insert(before);
            require((!editorial.contains(id) || editorial.at(id) == before) && !seen[id].contains(after), "Broken editorial chain");
            seen[id].insert(after); editorial[id] = after;
        }
    }
    for (std::size_t p = 0; p < result.current.size(); ++p) {
        const auto blocks = array(pages.GetAt(static_cast<unsigned>(p)).GetObject(), "blocks");
        require(blocks.Size() == result.current[p].blocks.size(), "State map block count differs");
        for (std::size_t b = 0; b < result.current[p].blocks.size(); ++b) {
            const auto id = result.current[p].blocks[b]; const auto block = blocks.GetAt(static_cast<unsigned>(b)).GetObject();
            require(state_fingerprint(block) == result.signatures.at(id), "State-bearing block changed without a compatible map");
            if (editorial.contains(id)) require(fingerprint(block) == editorial.at(id), "Stale editorial fingerprint");
        }
    }
    return result;
}
TextComparison comparison(std::string_view mode, bool input = false) {
    if (mode == "exact") return TextComparison::exact;
    if (input && mode == "tokens") return TextComparison::tokens;
    if (!input && mode == "trimmed") return TextComparison::trimmed;
    invalid("Unknown text comparison");
}
OutputCheck read_output_check(const JsonObject& value) {
    allowed_keys(value, {"inputComparison", "cases"});
    OutputCheck result;
    result.input_comparison = comparison(value.HasKey(L"inputComparison") ? text(value, "inputComparison") : "exact", true);
    const auto cases = array(value, "cases"); require(cases.Size() > 0, "Missing output check cases");
    for (const auto& case_value : cases) {
        const auto item = case_value.GetObject(); allowed_keys(item, {"standardInput", "outputs", "explanation", "reviewFeedback", "target"});
        OutputCase record; record.standard_input = text(item, "standardInput"); record.outputs = strings(array(item, "outputs"), true);
        record.explanation = optional_text(item, "explanation");
        if (item.HasKey(L"target")) invalid("CMake output-check targets are not supported by the initial single-source Windows adapter");
        if (item.HasKey(L"reviewFeedback")) {
            std::set<std::string> reviewed(record.outputs.begin(), record.outputs.end());
            for (const auto& review : array(item, "reviewFeedback")) {
                const auto object = review.GetObject(); allowed_keys(object, {"outputs", "text"}, true);
                OutputReviewFeedback rule{strings(array(object, "outputs"), true), text(object, "text")};
                require(!rule.text.empty(), "Empty output review");
                for (const auto& output : rule.outputs) require(reviewed.insert(output).second, "Overlapping output review");
                record.review_feedback.push_back(std::move(rule));
            }
        }
        require(std::set<std::string>(record.outputs.begin(), record.outputs.end()).size() == record.outputs.size(), "Duplicate accepted outputs");
        result.cases.push_back(std::move(record));
    }
    // The pure evaluator also checks matching ambiguity; reject it at load time.
    for (const auto& item : result.cases) {
        FreshExecution execution{1, true, true, 0, item.standard_input, {}, {}};
        (void)evaluate_output_check(result, execution, 1);
    }
    return result;
}
Block read_block(const JsonObject& value) {
    Block result; result.raw_json = utf8(value.Stringify());
    const auto type = text(value, "type");
    const std::map<std::string, BlockType> types{{"heading", BlockType::heading}, {"paragraph", BlockType::paragraph}, {"note", BlockType::note}, {"code", BlockType::code},
        {"multiple_choice", BlockType::multiple_choice}, {"output_prediction", BlockType::output_prediction}, {"fill_code", BlockType::fill_code},
        {"order_code", BlockType::order_code}, {"cpp_compiler", BlockType::cpp_compiler}};
    require(types.contains(type), "Unsupported block type: " + type); result.type = types.at(type);
    result.id = optional_text(value, "id"); result.text = optional_text(value, "text"); result.question = optional_text(value, "question");
    result.code = optional_text(value, "code"); result.initial_code = optional_text(value, "initialCode"); result.language = optional_text(value, "language");
    result.explanation = optional_text(value, "explanation"); result.review_feedback = optional_text(value, "reviewFeedback");
    if (value.HasKey(L"emphasis")) for (const auto& emphasis : array(value, "emphasis")) {
        const auto item = emphasis.GetObject();
        Emphasis entry; entry.text = text(item, "text");
        if (item.HasKey(L"occurrence")) entry.occurrence = integer(item.GetNamedValue(L"occurrence"));
        if (item.HasKey(L"style")) entry.style = text(item, "style");
        result.emphasis.push_back(std::move(entry));
    }
    if (result.type == BlockType::multiple_choice) {
        result.options = strings(array(value, "options"), true); result.correct_index = integer(value.GetNamedValue(L"correctIndex"));
        require(*result.correct_index < result.options.size(), "Choice answer is outside options");
    } else if (result.type == BlockType::output_prediction || result.type == BlockType::fill_code) {
        result.answers = strings(array(value, "answers"), true, false);
        if (value.HasKey(L"outputComparison")) result.output_comparison = comparison(text(value, "outputComparison"));
        if (value.HasKey(L"answerComparison")) invalid("Numeric fill comparison is outside the initial Windows lesson contract");
    } else if (result.type == BlockType::order_code) {
        result.fragments = strings(array(value, "fragments"), true);
        std::set<std::size_t> seen;
        for (const auto& index : array(value, "correctOrder")) {
            const auto number = integer(index); require(number < result.fragments.size() && seen.insert(number).second, "Invalid order permutation");
            result.correct_order.push_back(number);
        }
        require(seen.size() == result.fragments.size(), "Order answer is not a full permutation");
    } else if (result.type == BlockType::cpp_compiler) {
        require(!value.HasKey(L"cmake") && !value.HasKey(L"projectFiles"), "Multi-file/CMake projects need their Windows adapter");
        require(!result.initial_code.empty(), "Compiler block requires its source");
        if (value.HasKey(L"initialStandardInput")) result.standard_input = text(value, "initialStandardInput");
        if (value.HasKey(L"outputCheck")) result.output_check = read_output_check(value.GetNamedObject(L"outputCheck"));
        if (!result.standard_input && result.output_check && std::ranges::any_of(result.output_check->cases,
            [](const auto& item) { return !item.standard_input.empty(); })) result.standard_input = "";
    }
    return result;
}
void resolve_addresses(Lesson& lesson) {
    const auto& layout = lesson.state_layout;
    std::map<std::string, StateAddress> addresses;
    for (std::size_t p = 0; p < layout.origin.size(); ++p)
        for (std::size_t b = 0; b < layout.origin[p].blocks.size(); ++b)
            addresses.emplace(layout.origin[p].blocks[b], StateAddress{p, b, {}, layout.origin[p].id});
    for (const auto& addition : layout.additions)
        addresses.emplace(addition.id, StateAddress{addition.storage.page, addition.storage.block,
            sha256(layout.lineage + '/' + addition.id), addition.progress_page});
    for (std::size_t p = 0; p < lesson.pages.size(); ++p) {
        auto& page = lesson.pages[p]; page.stable_id = layout.version ? layout.current[p].id : "page_" + std::to_string(p);
        for (std::size_t b = 0; b < page.blocks.size(); ++b) {
            auto& block = page.blocks[b];
            block.stable_id = layout.version ? layout.current[p].blocks[b] : "block_" + std::to_string(p) + '_' + std::to_string(b);
            block.address = layout.version ? addresses.at(block.stable_id) : StateAddress{p, b, {}, page.stable_id};
        }
    }
}
}

curriculum::Lesson parse_lesson_json(std::string_view source, std::string_view expected_locale) {
    require(known_locale(expected_locale), "Unsupported asset language");
    ensure_winrt_apartment();
    try {
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        require(source.size() <= 32 * 1024 * 1024, "Lesson file too large");
        require_integer_json_numbers(source);
        const auto root = JsonObject::Parse(winrt::to_hstring(source));
        Lesson lesson; lesson.id = text(root, "id"); lesson.title = text(root, "title"); lesson.locale = expected_locale;
        require(!lesson.id.empty() && !lesson.title.empty(), "Lesson identity/title missing"); lesson.raw_json = source;
        const auto pages = array(root, "pages"); require(pages.Size() > 0 && pages.Size() <= 2048, "Invalid page count");
        if (root.HasKey(L"stateLayout")) lesson.state_layout = read_layout(root.GetNamedObject(L"stateLayout"), pages, expected_locale);
        for (const auto& value : pages) {
            Page page; for (const auto& block : array(value.GetObject(), "blocks")) page.blocks.push_back(read_block(block.GetObject()));
            lesson.pages.push_back(std::move(page));
        }
        resolve_addresses(lesson); return lesson;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Curriculum JSON/contract error: " + utf8(error.message()));
    }
}
curriculum::Lesson load_lesson(const std::filesystem::path& path, std::string_view expected_locale) {
    std::ifstream input(path, std::ios::binary); require(input.good(), "Cannot open lesson file");
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "Cannot read lesson file");
    auto lesson = parse_lesson_json(bytes, expected_locale); lesson.source_path = path; return lesson;
}
} // namespace lgaed::windows

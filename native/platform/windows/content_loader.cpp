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
bool narration_space(wchar_t value) {
    // Kotlin Char.isWhitespace also includes Unicode space characters. This is
    // metadata normalization, independent of the Windows/device locale.
    return (value >= L'\t' && value <= L'\r') || (value >= 0x1c && value <= 0x20) ||
        value == 0xa0 || value == 0x1680 || (value >= 0x2000 && value <= 0x200a) ||
        value == 0x2028 || value == 0x2029 || value == 0x202f || value == 0x205f || value == 0x3000;
}
std::optional<std::string> narration_tag(const winrt::hstring& authored) {
    std::wstring_view tag(authored.c_str(), authored.size());
    while (!tag.empty() && narration_space(tag.front())) tag.remove_prefix(1);
    while (!tag.empty() && narration_space(tag.back())) tag.remove_suffix(1);
    if (tag.empty()) return std::nullopt;
    std::wstring normalized(tag);
    for (auto& character : normalized) {
        if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t>(character + L'a' - L'A');
        else if (character == L'\u00d1') character = L'\u00f1';
        else if (character == L'\u00c7') character = L'\u00e7';
        else if (character == L'_') character = L'-';
    }
    // Same aliases and order as Android NarrationLanguage.fromTag. In particular
    // only en-GB-* and the exact en-UK alias select the British pronunciation.
    if (normalized == L"es" || normalized == L"espa\u00f1ol" || normalized == L"spanish" || normalized.starts_with(L"es-")) return "es";
    if (normalized == L"en-gb" || normalized == L"en-uk" || normalized.starts_with(L"en-gb-")) return "en-GB";
    if (normalized == L"en" || normalized == L"english" || normalized.starts_with(L"en-")) return "en-US";
    if (normalized == L"fr" || normalized == L"fran\u00e7ais" || normalized == L"french" || normalized.starts_with(L"fr-")) return "fr";
    return std::nullopt;
}
std::optional<std::string> direct_narration_language(const JsonObject& source) {
    for (const auto name : {L"language", L"locale", L"lang", L"idioma"}) {
        if (!source.HasKey(name)) continue;
        const auto value = source.GetNamedValue(name);
        // Android optString treats other JSON values as unrecognized aliases;
        // they must not hide a valid subsequent key or lower-priority source.
        if (value.ValueType() == JsonValueType::String)
            if (auto language = narration_tag(value.GetString())) return language;
    }
    return std::nullopt;
}
std::optional<std::string> explicit_narration_language(const JsonObject& source) {
    if (auto language = direct_narration_language(source)) return language;
    if (source.HasKey(L"metadata")) {
        const auto metadata = source.GetNamedValue(L"metadata");
        if (metadata.ValueType() == JsonValueType::Object) return direct_narration_language(metadata.GetObject());
    }
    return std::nullopt;
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
            // Adding this runtime objective cannot change historical block
            // addresses/drafts. Native completion hashes include it separately.
            if (object.HasKey(L"standardError")) {
                require(text(object,"standardError") == "nonBlank", "Unknown runtime stderr expectation");
                object.Remove(L"standardError");
            }
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
// Windows.Data.Json validates the document, but its Stringify round-trips
// decimals through double. Slice the already validated UTF-8 source instead so
// the shared mathematical backend receives the exact authored number tokens.
class OriginalJson {
public:
    struct Slice { std::size_t begin = 0,end = 0; };
    explicit OriginalJson(std::string_view source) : source_(source) {}
    [[nodiscard]] Slice root() const { auto cursor = std::size_t{0}; space(cursor); const auto begin = cursor; return {begin,value(cursor)}; }
    [[nodiscard]] std::string_view text(Slice item) const { return source_.substr(item.begin,item.end - item.begin); }
    [[nodiscard]] Slice member(Slice object,std::string_view key_name) const {
        auto cursor = object.begin; space(cursor); require(source_.at(cursor++) == '{',"Expected original JSON object");
        std::optional<Slice> found;
        for (;;) {
            space(cursor); if (source_.at(cursor) == '}') break;
            const auto key_start = cursor; const auto key_end = string_end(cursor);
            const auto name = utf8(JsonValue::Parse(winrt::to_hstring(source_.substr(key_start,key_end - key_start))).GetString());
            cursor = key_end; space(cursor); require(source_.at(cursor++) == ':',"Expected original JSON property"); space(cursor);
            const auto begin = cursor; const auto end = value(cursor);
            if (name == key_name) { require(!found,"Duplicate structural JSON property"); found = Slice{begin,end}; }
            cursor = end; space(cursor); if (source_.at(cursor) == '}') break;
            require(source_.at(cursor++) == ',',"Expected original JSON object separator");
        }
        require(found.has_value(),"Missing original JSON property"); return *found;
    }
    [[nodiscard]] std::vector<Slice> elements(Slice array_value) const {
        auto cursor = array_value.begin; space(cursor); require(source_.at(cursor++) == '[',"Expected original JSON array");
        std::vector<Slice> result;
        for (;;) {
            space(cursor); if (source_.at(cursor) == ']') break;
            const auto begin = cursor; cursor = value(cursor); result.push_back({begin,cursor});
            space(cursor); if (source_.at(cursor) == ']') break;
            require(source_.at(cursor++) == ',',"Expected original JSON array separator");
        }
        return result;
    }
private:
    std::string_view source_;
    void space(std::size_t& cursor) const { while (cursor < source_.size() && std::string_view(" \t\r\n").find(source_[cursor]) != std::string_view::npos) ++cursor; }
    std::size_t string_end(std::size_t cursor) const {
        require(source_.at(cursor++) == '"',"Expected original JSON string");
        while (cursor < source_.size()) {
            const auto character = source_[cursor++];
            if (character == '\\') { require(cursor < source_.size(),"Incomplete original JSON escape"); ++cursor; }
            else if (character == '"') return cursor;
        }
        invalid("Incomplete original JSON string");
    }
    std::size_t value(std::size_t cursor) const {
        if (source_.at(cursor) == '"') return string_end(cursor);
        if (source_[cursor] == '{' || source_[cursor] == '[') {
            unsigned depth = 0;
            do {
                const auto character = source_.at(cursor);
                if (character == '"') { cursor = string_end(cursor); continue; }
                if (character == '{' || character == '[') { ++depth; require(depth <= 64,"JSON nesting too deep"); }
                else if (character == '}' || character == ']') --depth;
                ++cursor;
            } while (depth);
            return cursor;
        }
        while (cursor < source_.size() && std::string_view(" \t\r\n,]}").find(source_[cursor]) == std::string_view::npos) ++cursor;
        return cursor;
    }
};
StateLayout read_layout(const JsonObject& metadata, const JsonArray& pages, std::string_view locale) {
    StateLayout result;
    result.raw_json = utf8(metadata.Stringify());
    result.version = static_cast<int>(integer(metadata.GetNamedValue(L"version"), 2));
    require(result.version == 1 || result.version == 2, "Unsupported state layout version");
    if (result.version == 1) allowed_keys(metadata, {"version", "origin", "current"}, true);
    else {
        allowed_keys(metadata, {"version", "locale", "origin", "current", "family", "additions", "editorial", "retired", "retirementHistory"});
        for (const auto field : {L"version",L"locale",L"origin",L"current",L"family",L"additions",L"editorial"})
            require(metadata.HasKey(field),"Incomplete state-layout contract");
    }
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
    for (const auto& id : current_blocks) require(result.signatures.contains(id), "Unknown state block");
    std::map<std::string, std::string> successors;
    if (metadata.HasKey(L"retired")) {
        require(result.version == 2, "Retirement requires state layout v2");
        const auto archive = array(metadata,"retired");
        require(archive.Size() <= 100000 && (!archive.Size() || result.locale == "es"),
                "Retirement is currently authorized only for Spanish");
        for (const auto& value : archive) {
            const auto item = value.GetObject(); allowed_keys(item,{"id","page","next","block"},true);
            StateRetirement retired; retired.id = text(item,"id"); retired.page = text(item,"page"); retired.next = text(item,"next");
            require(result.signatures.contains(retired.id) && !current_blocks.contains(retired.id) &&
                    successors.emplace(retired.id,retired.next).second,"Unknown, duplicated or active retired identity");
            require(origin_pages.contains(retired.page),"Unknown retired activity page");
            require((retired.next.empty() || (valid_id(retired.next) && result.signatures.contains(retired.next))) &&
                    retired.next != retired.id,"Invalid retired activity successor");
            const auto block = item.GetNamedObject(L"block"); const auto type = text(block,"type");
            require(type == "heading" || type == "paragraph" || type == "note" || type == "code" || type == "cpp_compiler",
                    "Only compiler activities and their presentation blocks can be retired");
            require(state_fingerprint(block) == result.signatures.at(retired.id),"A retired response/code contract changed");
            retired.raw_json = utf8(block.Stringify()); retired.sha256 = fingerprint(block);
            result.retired.push_back(std::move(retired));
        }
        for (const auto& [identity, next] : successors) {
            (void)next; std::set<std::string> seen; auto candidate = identity;
            while (successors.contains(candidate)) {
                require(seen.insert(candidate).second,"Cyclic retired activity successors");
                candidate = successors.at(candidate);
            }
        }
    }
    require(current_pages == origin_pages && current_blocks.size() + result.retired.size() == result.signatures.size(),
            "State map loses historical identities");
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
    for (const auto& retired : result.retired)
        if (editorial.contains(retired.id)) require(retired.sha256 == editorial.at(retired.id),"Stale retired editorial fingerprint");
    if (metadata.HasKey(L"retirementHistory")) {
        const auto history = array(metadata,"retirementHistory"); require(history.Size() <= 100000,"Too many retirement reading snapshots");
        std::vector<std::string> covered;
        std::map<std::string,JsonObject> shells;
        for (std::size_t p=0;p<result.current.size();++p) {
            auto shell=JsonObject::Parse(pages.GetAt(static_cast<unsigned>(p)).Stringify());shell.Remove(L"blocks");
            shells.emplace(result.current[p].id,shell);
        }
        for (const auto& value : history) {
            const auto item=value.GetObject();
            allowed_keys(item,{"retired","current","pages","compilerPuzzlesRequired","referenceAfter","activityAfter"},true);
            StateRetirementHistory snapshot;
            snapshot.retired=strings(array(item,"retired"),true,false);
            std::set<std::string> unique;
            for (const auto& id:snapshot.retired) {
                require(successors.contains(id)&&unique.insert(id).second,"Unknown or duplicated reading-history retirement");
                covered.push_back(id);
            }
            const auto required=item.GetNamedValue(L"compilerPuzzlesRequired");
            require(required.ValueType()==JsonValueType::Boolean,"Historical compiler flow must be boolean");
            snapshot.compiler_puzzles_required=required.GetBoolean();
            snapshot.current=read_pages(array(item,"current"),nullptr);
            const auto historical_pages=array(item,"pages");
            require(snapshot.current.size()==result.current.size()&&historical_pages.Size()==snapshot.current.size(),"Historical reading page count changed");
            std::set<std::string> snapshot_pages;
            for(std::size_t p=0;p<snapshot.current.size();++p) {
                const auto& record=snapshot.current[p];snapshot_pages.insert(record.id);
                require(shells.contains(record.id),"Unknown historical reading page");
                const auto page=historical_pages.GetAt(static_cast<unsigned>(p)).GetObject();
                const auto blocks=array(page,"blocks");require(blocks.Size()==record.blocks.size(),"Historical reading block ownership changed");
                auto shell=JsonObject::Parse(page.Stringify());shell.Remove(L"blocks");
                require(fingerprint(shell)==fingerprint(shells.at(record.id)),"Historical reading page metadata changed");
                for(std::size_t b=0;b<record.blocks.size();++b) {
                    const auto& id=record.blocks[b];
                    require(result.signatures.contains(id)&&state_fingerprint(blocks.GetAt(static_cast<unsigned>(b)).GetObject())==result.signatures.at(id),
                            "Historical reading snapshot changed an activity contract");
                }
            }
            require(snapshot_pages==origin_pages,"Historical reading page identities changed");
            for(const auto& link:array(item,"referenceAfter")) {
                const auto object=link.GetObject();allowed_keys(object,{"activity","blocks"},true);
                snapshot.reference_after.push_back({text(object,"activity"),strings(array(object,"blocks"),true,false)});
            }
            for(const auto& link:array(item,"activityAfter")) {
                const auto object=link.GetObject();allowed_keys(object,{"activity","requires"},true);
                snapshot.activity_after.push_back({text(object,"activity"),strings(array(object,"requires"),true,false)});
            }
            snapshot.raw_json=utf8(item.Stringify());snapshot.sha256=fingerprint(item);
            result.retirement_history.push_back(std::move(snapshot));
        }
        require(covered.size()<=result.retired.size(),"Reading history exceeds the retired identity archive");
        std::set<std::string> covered_once;
        for(std::size_t index=0;index<covered.size();++index)
            require(covered_once.insert(covered[index]).second&&covered[index]==result.retired[result.retired.size()-covered.size()+index].id,
                    "Reading retirement batches must own an exact suffix of archived identities");
        auto active=current_blocks;
        std::set<std::string> addition_ids;for(const auto& addition:result.additions)addition_ids.insert(addition.id);
        for(auto item=result.retirement_history.rbegin();item!=result.retirement_history.rend();++item) {
            auto expected=active;for(const auto& id:item->retired)require(expected.insert(id).second,"An archived reading batch is still active");
            std::set<std::string> before;for(const auto& page:item->current)before.insert(page.blocks.begin(),page.blocks.end());
            // A later v2 addition has no payload in an earlier presentation.
            // Original identities and the batch being retired cannot disappear.
            for(const auto& id:item->retired)require(before.contains(id),"Reading history lost its own retirement batch");
            for(const auto& id:before)require(expected.contains(id),"Reading history revived an already retired identity");
            for(const auto& id:expected)require(before.contains(id)||addition_ids.contains(id),"Reading history lost an original identity");
            active=std::move(before);
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
OutputCheck read_output_check(const JsonObject& value,bool cmake=false) {
    allowed_keys(value, {"inputComparison", "cases"});
    OutputCheck result;
    result.input_comparison = comparison(value.HasKey(L"inputComparison") ? text(value, "inputComparison") : "exact", true);
    const auto cases = array(value, "cases"); require(cases.Size() > 0, "Missing output check cases");
    for (const auto& case_value : cases) {
        const auto item = case_value.GetObject(); allowed_keys(item, {"standardInput", "outputs", "explanation", "reviewFeedback", "target", "standardError"});
        OutputCase record; record.standard_input = text(item, "standardInput"); record.outputs = strings(array(item, "outputs"), true);
        record.explanation = optional_text(item, "explanation");
        if (item.HasKey(L"standardError")) {
            require(text(item, "standardError") == "nonBlank", "Unknown runtime stderr expectation");
            record.standard_error = StandardErrorExpectation::non_blank;
        }
        if(item.HasKey(L"target")){require(cmake,"An outputCheck target requires a CMake project");record.executable=text(item,"target");validate_cmake_config(CmakeConfig{*record.executable,{},"Debug"});}
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
        FreshExecution execution{1, true, true, 0, item.standard_input, {}, item.executable};
        (void)evaluate_output_check(result, execution, 1);
    }
    return result;
}
Block read_block(const JsonObject& value,std::string_view original) {
    Block result; result.raw_json = original;
    const auto type = text(value, "type");
    const std::map<std::string, BlockType> types{{"heading", BlockType::heading}, {"paragraph", BlockType::paragraph}, {"note", BlockType::note}, {"code", BlockType::code},
        {"multiple_choice", BlockType::multiple_choice}, {"output_prediction", BlockType::output_prediction}, {"fill_code", BlockType::fill_code},
        {"order_code", BlockType::order_code}, {"cpp_compiler", BlockType::cpp_compiler},
        {"inline_fill_code",BlockType::inline_fill_code}, {"drag_fill_code",BlockType::drag_fill_code}};
    const std::set<std::string> mathematics{"math_puzzle","math_multiple_choice_single","math_multiple_choice_multi","math_numeric_input",
        "math_multi_input","math_expression_input","math_structured_answer","math_equation_fill_blanks","math_ordering_drag","math_number_line",
        "math_slider_interaction","math_matching_drag","math_coordinate_graph_builder","math_geometry_builder","math_rigid_angle_copy"};
    require(types.contains(type) || mathematics.contains(type), "Unsupported block type: " + type);
    result.type = mathematics.contains(type) ? BlockType::math_puzzle : types.at(type);
    result.id = optional_text(value, "id"); result.text = optional_text(value, "text"); result.question = optional_text(value, "question");
    result.code = optional_text(value, "code"); result.initial_code = optional_text(value, "initialCode"); result.language = optional_text(value, "language");
    result.explanation = optional_text(value, "explanation");
    if(value.HasKey(L"reviewFeedback")) {
        const auto feedback=value.GetNamedValue(L"reviewFeedback");
        if(feedback.ValueType()==JsonValueType::String)result.review_feedback=utf8(feedback.GetString());
        else {
            require(result.type==BlockType::multiple_choice&&feedback.ValueType()==JsonValueType::Array,
                "Per-option review feedback requires a multiple-choice string array");
            result.review_feedback_options=strings(feedback.GetArray(),true,false);
        }
    }
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
        require(result.review_feedback_options.empty()||result.review_feedback_options.size()==result.options.size(),
            "Per-option review feedback must match the number of choices");
    } else if (result.type == BlockType::output_prediction || result.type == BlockType::fill_code) {
        result.answers = strings(array(value, "answers"), true, false);
        if (value.HasKey(L"outputComparison")) result.output_comparison = comparison(text(value, "outputComparison"));
        if (value.HasKey(L"answerComparison")) {
            require(result.type == BlockType::fill_code && text(value,"answerComparison") == "cpp_decimal_literal_value","Invalid fill comparison profile");
            result.answer_comparison = AnswerComparison::cpp_decimal_literal_value;
            (void)evaluate_text_answer(result,result.answers.front());
        }
    } else if (result.type == BlockType::inline_fill_code || result.type == BlockType::drag_fill_code) {
        result.code_template = text(value,"template");
        std::size_t holes = 0, position = 0;
        while ((position = result.code_template.find("[]",position)) != std::string::npos) { ++holes; position += 2; }
        require(holes > 0,"A fill template needs at least one hole");
        if (result.type == BlockType::inline_fill_code) {
            result.answers = strings(array(value,"answers"),true,false);
            require(result.answers.size() == holes,"Inline fill holes and canonical answers differ");
            if (value.HasKey(L"answerAlternatives")) {
                const auto groups = array(value,"answerAlternatives"); require(groups.Size() == holes,"Inline alternatives must belong to individual holes");
                for (unsigned index = 0; index < groups.Size(); ++index) {
                    auto accepted = strings(groups.GetAt(index).GetArray(),true,false);
                    require(accepted.front() == result.answers[index],"Canonical inline answer must remain first");
                    result.answer_alternatives.push_back(std::move(accepted));
                }
            }
        } else {
            result.choices = strings(array(value,"choices"),true,false);
            std::set<std::size_t> used;
            for (const auto& item : array(value,"correctOrder")) {
                const auto index = integer(item);
                require(index < result.choices.size() && used.insert(index).second,"A drag fill choice is invalid or reused"); result.correct_order.push_back(index);
            }
            require(result.correct_order.size() == holes,"Drag fill holes and answers differ");
        }
    } else if (result.type == BlockType::math_puzzle) {
        result.math_type = type; result.prompt = value.HasKey(L"prompt") ? text(value,"prompt") : text(value,"question");
        require(!result.prompt.empty(),"Math puzzle requires its visible prompt");
    } else if (result.type == BlockType::order_code) {
        result.fragments = strings(array(value, "fragments"), true);
        std::set<std::size_t> seen;
        for (const auto& index : array(value, "correctOrder")) {
            const auto number = integer(index); require(number < result.fragments.size() && seen.insert(number).second, "Invalid order permutation");
            result.correct_order.push_back(number);
        }
        require(seen.size() == result.fragments.size(), "Order answer is not a full permutation");
    } else if (result.type == BlockType::cpp_compiler) {
        if(value.HasKey(L"projectFiles")){
            for(const auto& entry:array(value,"projectFiles")){const auto file=entry.GetObject();result.project_files.emplace_back(text(file,"path"),text(file,"initialCode"));}
            validate_cpp_project_files(result.project_files);
            if(value.HasKey(L"cmake")){
                std::size_t bytes=0;for(const auto& [path,code]:result.project_files)bytes+=code.size();require(result.project_files.size()<=128&&bytes<=8*1024*1024,"CMake project exceeds its original128-file/8MiB limits");
                require(!value.HasKey(L"sourceFiles"),"CMake owns its source selection");const auto config=value.GetNamedObject(L"cmake");CmakeConfig cmake;
                for(const auto& item:config)require(item.Key()==L"target"||item.Key()==L"options"||item.Key()==L"buildType","Unknown CMake configuration field");
                cmake.target=text(config,"target");if(config.HasKey(L"buildType"))cmake.build_type=text(config,"buildType");
                if(config.HasKey(L"options"))for(const auto& item:config.GetNamedObject(L"options"))cmake.options.emplace(utf8(item.Key()),utf8(item.Value().GetString()));
                validate_cmake_config(cmake);result.cmake=std::move(cmake);
            }else{
                if(value.HasKey(L"sourceFiles"))result.source_files=strings(array(value,"sourceFiles"),true);
                else for(const auto& [path,code]:result.project_files)if(is_cpp_source_file(path))result.source_files.push_back(path);
                validate_cpp_project(result.project_files,result.source_files);
            }
        }else{require(!value.HasKey(L"cmake")&&!value.HasKey(L"sourceFiles"),"Project metadata requires projectFiles");require(!result.initial_code.empty(), "Compiler block requires its source");}
        if (value.HasKey(L"initialStandardInput")) result.standard_input = text(value, "initialStandardInput");
        if (value.HasKey(L"outputCheck")) result.output_check = read_output_check(value.GetNamedObject(L"outputCheck"),result.cmake.has_value());
        if (!result.standard_input && result.output_check && std::ranges::any_of(result.output_check->cases,
            [](const auto& item) { return !item.standard_input.empty(); })) result.standard_input = "";
    }
    return result;
}
void read_relationships(const JsonObject& root, Lesson& lesson) {
    if (root.HasKey(L"referenceAfter")) {
        const auto relationships = array(root,"referenceAfter");
        require(relationships.Size() <= 100000,"Too many reference relationships");
        for (const auto& item : relationships) {
            const auto relation = item.GetObject(); allowed_keys(relation,{"activity","blocks"},true);
            ReferenceAfter record{text(relation,"activity"),strings(array(relation,"blocks"),true,false)};
            require(valid_id(record.activity),"Invalid reference activity identity");
            for (const auto& identity : record.blocks) require(valid_id(identity),"Invalid reference block identity");
            lesson.reference_after.push_back(std::move(record));
        }
    }
    if (root.HasKey(L"activityAfter")) {
        const auto relationships = array(root,"activityAfter");
        require(relationships.Size() <= 100000,"Too many activity relationships");
        for (const auto& item : relationships) {
            const auto relation = item.GetObject(); allowed_keys(relation,{"activity","requires"},true);
            ActivityAfter record{text(relation,"activity"),strings(array(relation,"requires"),true,false)};
            require(valid_id(record.activity),"Invalid dependent activity identity");
            for (const auto& identity : record.prerequisites) require(valid_id(identity),"Invalid activity prerequisite identity");
            lesson.activity_after.push_back(std::move(record));
        }
    }
}
void resolve_addresses(Lesson& lesson) {
    auto& layout = lesson.state_layout;
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
    for (const auto& retired : layout.retired) {
        auto block = read_block(JsonObject::Parse(winrt::to_hstring(retired.raw_json)),retired.raw_json);
        block.stable_id = retired.id; block.address = addresses.at(retired.id);
        lesson.retired_blocks.push_back(std::move(block));
    }
    for (auto& snapshot : layout.retirement_history) {
        const auto archived=JsonObject::Parse(winrt::to_hstring(snapshot.raw_json));
        const auto pages=array(archived,"pages");
        for(std::size_t p=0;p<snapshot.current.size();++p) {
            const auto page_source=pages.GetAt(static_cast<unsigned>(p)).GetObject();
            Page page;page.stable_id=snapshot.current[p].id;page.narration_language=explicit_narration_language(page_source);
            if(!page.narration_language)page.narration_language=lesson.narration_language;
            const auto blocks=array(page_source,"blocks");
            for(std::size_t b=0;b<snapshot.current[p].blocks.size();++b) {
                const auto source=blocks.GetAt(static_cast<unsigned>(b)).GetObject();
                auto block=read_block(source,utf8(source.Stringify()));block.stable_id=snapshot.current[p].blocks[b];
                block.address=addresses.at(block.stable_id);page.blocks.push_back(std::move(block));
            }
            snapshot.pages.push_back(std::move(page));
        }
        Lesson previous;previous.id=lesson.id;previous.title=lesson.title;previous.locale=lesson.locale;
        previous.state_layout.version=2;previous.pages=snapshot.pages;previous.compiler_puzzles_required=snapshot.compiler_puzzles_required;
        previous.reference_after=snapshot.reference_after;previous.activity_after=snapshot.activity_after;
        validate_reference_relationships(previous);validate_activity_relationships(previous);
    }
}
}

std::optional<std::string> narration_language_tag(std::string_view authored) {
    return narration_tag(winrt::to_hstring(authored));
}

curriculum::Lesson parse_lesson_json(std::string_view source, std::string_view expected_locale) {
    require(known_locale(expected_locale), "Unsupported asset language");
    ensure_winrt_apartment();
    try {
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        require(source.size() <= 32 * 1024 * 1024, "Lesson file too large");
        const auto root = JsonObject::Parse(winrt::to_hstring(source));
        Lesson lesson; lesson.id = text(root, "id"); lesson.title = text(root, "title"); lesson.locale = expected_locale;
        if (root.HasKey(L"compilerPuzzlesRequired")) {
            const auto required = root.GetNamedValue(L"compilerPuzzlesRequired");
            require(required.ValueType() == JsonValueType::Boolean,"compilerPuzzlesRequired must be boolean");
            lesson.compiler_puzzles_required = required.GetBoolean();
        }
        lesson.narration_language = explicit_narration_language(root);
        require(!lesson.id.empty() && !lesson.title.empty(), "Lesson identity/title missing"); lesson.raw_json = source;
        const auto pages = array(root, "pages"); require(pages.Size() > 0 && pages.Size() <= 2048, "Invalid page count");
        if (root.HasKey(L"stateLayout")) {
            require_integer_json_numbers(source);
            lesson.state_layout = read_layout(root.GetNamedObject(L"stateLayout"), pages, expected_locale);
        }
        read_relationships(root,lesson);
        const OriginalJson original(source);
        const auto original_pages = original.elements(original.member(original.root(),"pages"));
        require(original_pages.size() == pages.Size(),"Original JSON page ownership mismatch");
        for (unsigned p = 0; p < pages.Size(); ++p) {
            const auto page_source = pages.GetAt(p).GetObject();
            const auto blocks = array(page_source,"blocks");
            const auto original_blocks = original.elements(original.member(original_pages[p],"blocks"));
            require(original_blocks.size() == blocks.Size(),"Original JSON block ownership mismatch");
            Page page; page.narration_language = explicit_narration_language(page_source);
            if (!page.narration_language) page.narration_language = lesson.narration_language;
            for (unsigned b = 0; b < blocks.Size(); ++b) page.blocks.push_back(read_block(blocks.GetAt(b).GetObject(),original.text(original_blocks[b])));
            lesson.pages.push_back(std::move(page));
        }
        resolve_addresses(lesson);
        validate_reference_relationships(lesson);
        validate_activity_relationships(lesson);
        return lesson;
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
curriculum::Lesson parse_lesson_progress_metadata(std::string_view source, std::string_view expected_locale) {
    require(known_locale(expected_locale),"Unsupported asset language");
    ensure_winrt_apartment();
    try {
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        require(source.size() <= 32 * 1024 * 1024,"Lesson file too large");
        const auto root = JsonObject::Parse(winrt::to_hstring(source));
        Lesson lesson; lesson.id = text(root,"id"); lesson.title = text(root,"title");
        lesson.locale = expected_locale; lesson.raw_json = source;
        if (root.HasKey(L"compilerPuzzlesRequired")) {
            const auto required = root.GetNamedValue(L"compilerPuzzlesRequired");
            require(required.ValueType() == JsonValueType::Boolean,"compilerPuzzlesRequired must be boolean");
            lesson.compiler_puzzles_required = required.GetBoolean();
        }
        require(!lesson.id.empty() && !lesson.title.empty(),"Lesson identity/title missing");
        const auto pages = array(root,"pages");
        require(pages.Size() > 0 && pages.Size() <= 2048,"Invalid page count");
        if (root.HasKey(L"stateLayout")) {
            require_integer_json_numbers(source);
            lesson.state_layout = read_layout(root.GetNamedObject(L"stateLayout"),pages,expected_locale);
        }
        read_relationships(root,lesson);
        // The catalog keeps its historical read-only progress projection, but
        // dependency sources need their real objectives even in lessons that
        // have not enabled mandatory compiler puzzles globally.
        std::set<std::string> goal_owners;
        for (const auto& relation : lesson.reference_after) goal_owners.insert(relation.activity);
        for (const auto& relation : lesson.activity_after)
            goal_owners.insert(relation.prerequisites.begin(),relation.prerequisites.end());
        const OriginalJson original(source);
        const auto original_pages = original.elements(original.member(original.root(),"pages"));
        require(original_pages.size() == pages.Size(),"Original JSON page ownership mismatch");
        for (unsigned p = 0; p < pages.Size(); ++p) {
            const auto blocks = array(pages.GetAt(p).GetObject(),"blocks");
            const auto original_blocks = original.elements(original.member(original_pages[p],"blocks"));
            require(original_blocks.size() == blocks.Size(),"Original JSON block ownership mismatch");
            Page page;
            for (unsigned b = 0; b < blocks.Size(); ++b) {
                const auto block = blocks.GetAt(b).GetObject();
                const auto raw = original.text(original_blocks[b]);
                if (text(block,"type") == "cpp_compiler") {
                    const auto identity = lesson.state_layout.version ? lesson.state_layout.current[p].blocks[b] :
                        "block_" + std::to_string(p) + '_' + std::to_string(b);
                    if((lesson.compiler_puzzles_required||!lesson.state_layout.retirement_history.empty()||goal_owners.contains(identity))&&block.HasKey(L"outputCheck")) {
                        // Required compiler progress needs the whole authored
                        // objective and build inputs to verify captured proofs.
                        page.blocks.push_back(read_block(block,raw));
                    } else {
                        // Keep the legacy read-only projection compatible with
                        // future/free compiler payloads; it never executes or
                        // credits them as an output puzzle.
                        Block metadata;metadata.type=BlockType::cpp_compiler;metadata.raw_json=raw;
                        page.blocks.push_back(std::move(metadata));
                    }
                } else {
                    // Progress needs answer contracts and historical addresses,
                    // not the runtime renderer's editorial presentation fields.
                    // Valid per-answer reviewFeedback arrays must remain raw,
                    // without being misinterpreted as a string by read_block.
                    const auto projection=JsonObject::Parse(block.Stringify());
                    if(projection.HasKey(L"explanation"))projection.Remove(L"explanation");
                    if(projection.HasKey(L"reviewFeedback"))projection.Remove(L"reviewFeedback");
                    try { page.blocks.push_back(read_block(projection,raw)); }
                    catch(const std::exception& failure){throw std::runtime_error("Progress page "+std::to_string(p+1)+" block "+std::to_string(b+1)+": "+failure.what());}
                }
            }
            lesson.pages.push_back(std::move(page));
        }
        resolve_addresses(lesson);
        validate_reference_relationships(lesson);
        validate_activity_relationships(lesson);
        return lesson;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Curriculum progress JSON/contract error: " + utf8(error.message()));
    }
}
curriculum::Lesson load_lesson_progress_metadata(const std::filesystem::path& path, std::string_view expected_locale) {
    std::ifstream input(path,std::ios::binary); require(input.good(),"Cannot open lesson progress metadata");
    const std::string source{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(!input.bad(),"Cannot read lesson progress metadata");
    try { auto lesson = parse_lesson_progress_metadata(source,expected_locale); lesson.source_path = path; return lesson; }
    catch(const std::exception& failure){throw std::runtime_error(path.string()+": "+failure.what());}
}
} // namespace lgaed::windows

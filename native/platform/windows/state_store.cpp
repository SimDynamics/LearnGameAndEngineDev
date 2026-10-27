#include "platform/windows/state_store.hpp"
#include "core/lesson.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <roapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <cmath>

namespace lgaed::state {
namespace {
using namespace winrt::Windows::Data::Json;
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::wstring wide(std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(INT_MAX)) throw std::runtime_error("Texto demasiado largo.");
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!size) throw std::runtime_error("Texto UTF-8 no valido.");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::string utf8(const winrt::hstring& text) { return winrt::to_string(text); }
JsonObject object(std::string_view value) { return JsonObject::Parse(wide(value)); }
std::string json_string(const JsonObject& value, std::wstring_view key, std::string fallback = {}) {
    if (!value.HasKey(key)) return fallback;
    return utf8(value.GetNamedString(key));
}
std::int64_t json_integer(double value) {
    // UI indices/UTF-16 offsets are bounded far below the exact-integer range of JSON doubles.
    if (!std::isfinite(value) || std::trunc(value) != value || value < -2147483648.0 || value > 2147483647.0)
        throw std::runtime_error("Indice JSON no valido.");
    return static_cast<std::int64_t>(value);
}
void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    Handle file{CreateFileW(native_io_path(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("No se pudo crear el estado pendiente.");
    std::size_t cursor = 0;
    while (cursor < bytes.size()) {
        DWORD written = 0;
        const auto count = static_cast<DWORD>((std::min)(bytes.size() - cursor, std::size_t{1024 * 1024}));
        if (!WriteFile(file.value, bytes.data() + cursor, count, &written, nullptr) || written != count)
            throw std::runtime_error("No se pudo escribir el estado completo.");
        cursor += written;
    }
    if (!FlushFileBuffers(file.value)) throw std::runtime_error("No se pudo confirmar el estado en disco.");
}
std::optional<std::string> file_hash(const std::filesystem::path& path) {
    if (!std::filesystem::exists(native_io_path(path))) return std::nullopt;
    return sha256(read_bytes(path));
}
}

std::filesystem::path native_io_path(const std::filesystem::path& path) {
    const auto native = path.native();
    if (native.starts_with(L"\\\\?\\")) return path;
    const auto absolute = std::filesystem::absolute(path).lexically_normal().native();
    if (absolute.starts_with(L"\\\\")) return std::filesystem::path(L"\\\\?\\UNC\\" + absolute.substr(2));
    return std::filesystem::path(L"\\\\?\\" + absolute);
}
void require_safe_path(const std::filesystem::path& path) {
    auto cursor = std::filesystem::absolute(path).lexically_normal();
    while (!cursor.empty()) {
        const auto attributes = GetFileAttributesW(native_io_path(cursor).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("El estado no admite enlaces ni puntos de redireccion.");
        const auto parent = cursor.parent_path();
        if (parent == cursor) break;
        cursor = parent;
    }
}
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path, std::size_t limit) {
    require_safe_path(path);
    const auto size = std::filesystem::file_size(native_io_path(path));
    if (size > limit) throw std::runtime_error("El archivo supera el limite de seguridad.");
    std::ifstream input(native_io_path(path), std::ios::binary);
    if (!input) throw std::runtime_error("No se pudo leer el estado.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() && !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("El archivo de estado esta incompleto.");
    return bytes;
}
std::string sha256(std::span<const std::uint8_t> bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA-256 no disponible.");
    std::array<std::uint8_t, 32> digest{};
    const auto result = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                                   static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (result < 0) throw std::runtime_error("No se pudo calcular SHA-256.");
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded; encoded.reserve(64);
    for (auto byte : digest) { encoded += hex[byte >> 4]; encoded += hex[byte & 15]; }
    return encoded;
}
std::string sha256(std::string_view value) {
    return sha256(std::span(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}
StateStore::StateStore(std::filesystem::path profile_root) : root_(std::move(profile_root) / "state") {
    if (!root_.is_absolute()) throw std::runtime_error("El perfil debe tener una ruta absoluta independiente del directorio actual.");
    root_ = root_.lexically_normal();
    require_safe_path(root_);
}
bool StateStore::empty() const { return !std::filesystem::exists(native_io_path(root_ / "active")); }
StateData StateStore::load() {
    const auto file = root_ / "active" / "state.bin";
    loaded_ = false;
    require_safe_path(file); const auto hash = file_hash(file);
    if (!hash) {
        if (std::filesystem::exists(native_io_path(root_ / "active"))) throw std::runtime_error("El estado activo esta incompleto; no se ha reemplazado.");
        expected_hash_ = hash; loaded_ = true;
        return {};
    }
    auto bytes = read_bytes(file);
    if (bytes.size() < 64) throw std::runtime_error("Estado Windows incompleto.");
    const std::string checksum(bytes.end() - 64, bytes.end()); bytes.resize(bytes.size() - 64);
    if (sha256(bytes) != checksum) throw std::runtime_error("La integridad del estado no coincide; se conserva el archivo.");
    auto result = decode(bytes); expected_hash_ = hash; loaded_ = true;
    return result;
}
void StateStore::save(const StateData& state) {
    if (!loaded_) throw std::runtime_error("Lee el estado antes de guardarlo.");
    require_safe_path(root_); std::filesystem::create_directories(native_io_path(root_));
    Handle lock{CreateFileW(native_io_path(root_ / "writer.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (lock.value == INVALID_HANDLE_VALUE) throw std::runtime_error("Otra instancia esta guardando este perfil.");
    const auto active = root_ / "active";
    const auto final_file = active / "state.bin";
    if (file_hash(final_file) != expected_hash_) throw std::runtime_error("El perfil cambio en otra instancia; vuelve a abrirlo antes de guardar.");
    auto bytes = encode(state); const auto checksum = sha256(bytes);
    bytes.insert(bytes.end(), checksum.begin(), checksum.end());
    const auto token = L"write-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const bool initial = !std::filesystem::exists(native_io_path(active));
    const auto pending_directory = root_ / (token + L".pending");
    const auto pending_file = initial ? pending_directory / "state.bin" : active / (token + L".pending");
    try {
        if (initial) std::filesystem::create_directory(native_io_path(pending_directory));
        write_file(pending_file, bytes);
        if (initial) {
            if (!MoveFileExW(native_io_path(pending_directory).c_str(), native_io_path(active).c_str(), MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("No se pudo publicar el estado inicial.");
        } else if (!ReplaceFileW(native_io_path(final_file).c_str(), native_io_path(pending_file).c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr))
            throw std::runtime_error("No se pudo actualizar el estado; la version anterior se conserva.");
        expected_hash_ = sha256(bytes);
    } catch (...) {
        std::error_code error; std::filesystem::remove(native_io_path(pending_file), error);
        if (initial) std::filesystem::remove(native_io_path(pending_directory), error);
        throw;
    }
}

std::optional<Response> get_response(const StateData& state, std::string_view key) try {
    const auto stored = find(state, "learning_progress", key);
    if (!stored) return std::nullopt;
    windows::ensure_winrt_apartment();
    const auto raw = string_value(state, "learning_progress", key);
    const auto json = object(raw); Response response;
    response.raw_json = raw;
    response.answer = json_string(json, L"answer");
    response.correct = json.GetNamedBoolean(L"correct", false);
    response.reviewed_answer_identity = json_string(json, L"reviewedAnswerIdentity");
    if (json.HasKey(L"selectedIndex")) response.selected = json_integer(json.GetNamedNumber(L"selectedIndex"));
    if (json.HasKey(L"order")) for (const auto item : json.GetNamedArray(L"order"))
        response.order.push_back(json_integer(item.GetNumber()));
    return response;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("La respuesta guardada contiene JSON o tipos no compatibles; no se ha modificado.");
}
void put_response(StateData& state, std::string_view key, const Response& response) try {
    windows::ensure_winrt_apartment();
    // Preserve opaque metadata/partial answers on an unchanged imported response.
    auto json = response.raw_json.empty() ? JsonObject() : object(response.raw_json);
    json.Insert(L"answer", JsonValue::CreateStringValue(wide(response.answer)));
    json.Insert(L"correct", JsonValue::CreateBooleanValue(response.correct));
    json.Insert(L"selectedIndex", JsonValue::CreateNumberValue(static_cast<double>(response.selected)));
    JsonArray order;
    for (auto index : response.order) order.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
    json.Insert(L"order", order);
    if (response.reviewed_answer_identity.empty()) { if (json.HasKey(L"reviewedAnswerIdentity")) json.Remove(L"reviewedAnswerIdentity"); }
    else json.Insert(L"reviewedAnswerIdentity", JsonValue::CreateStringValue(wide(response.reviewed_answer_identity)));
    state.preferences["learning_progress"][std::string(key)] = utf8(json.Stringify());
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("No se pudo codificar la respuesta; el estado anterior se conserva.");
}
namespace {
std::string quote_android_json(std::string_view text) {
    // Match JSONObject.quote/JSONStringer on Android, including escaped forward slashes.
    std::string result = "\""; constexpr char digits[] = "0123456789abcdef";
    for (unsigned char character : text) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '/': result += "\\/"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character < 32) {
                result += "\\u00"; result += digits[character >> 4]; result += digits[character & 15];
            } else result += static_cast<char>(character);
        }
    }
    result += '"'; return result;
}
std::string response_identity(const Response& response, const curriculum::Block& block) {
    std::string canonical;
    switch (block.type) {
    case curriculum::BlockType::math_puzzle: {
        const auto source=response.raw_json.empty()?JsonObject():object(response.raw_json);
        canonical=source.HasKey(L"responses")?utf8(source.GetNamedObject(L"responses").Stringify()):"{}";break;
    }
    case curriculum::BlockType::multiple_choice: canonical = std::to_string(response.selected); break;
    case curriculum::BlockType::order_code:
        canonical = "[";
        for (auto item : response.order) { if (canonical.size() > 1) canonical += ','; canonical += std::to_string(item); }
        canonical += ']'; break;
    case curriculum::BlockType::inline_fill_code:
    case curriculum::BlockType::drag_fill_code: {
        const auto source = response.raw_json.empty() ? JsonObject() : object(response.raw_json);
        const auto name = block.type == curriculum::BlockType::inline_fill_code ? L"answers" : L"slots";
        canonical = "[";
        if (source.HasKey(name)) for (const auto& item : source.GetNamedArray(name)) {
            if (canonical.size() > 1) canonical += ',';
            canonical += block.type == curriculum::BlockType::inline_fill_code ? quote_android_json(utf8(item.GetString())) : std::to_string(json_integer(item.GetNumber()));
        }
        canonical += ']'; break;
    }
    default: canonical = quote_android_json(response.answer); break;
    }
    return sha256(canonical);
}
}
bool response_reviewed(const Response& response, const curriculum::Block& block) {
    return !response.correct && !response.reviewed_answer_identity.empty() &&
        response_identity(response, block) == response.reviewed_answer_identity;
}
void mark_response_reviewed(Response& response, const curriculum::Block& block) {
    response.reviewed_answer_identity = response.correct ? std::string{} : response_identity(response, block);
}
void clear_response_review(Response& response) noexcept { response.reviewed_answer_identity.clear(); }
std::string selected_choice_text(const Response& response,const curriculum::Block& block) {
    if(block.type!=curriculum::BlockType::multiple_choice)return {};
    if(response.historical_choice&&!block.choice_revision)throw std::runtime_error("Falta el origen de la selección histórica.");
    const auto& options=response.historical_choice?block.choice_revision->original_options:block.options;
    if(response.selected<0)return {};
    if(static_cast<std::uint64_t>(response.selected)>=options.size())throw std::runtime_error("La selección no pertenece a esta edición.");
    return options[static_cast<std::size_t>(response.selected)];
}
std::string choice_incorrect_feedback(const Response& response,const curriculum::Block& block) {
    if(response.historical_choice) {
        if(!block.choice_revision)throw std::runtime_error("Falta la guía de la respuesta histórica.");
        const auto& revision=*block.choice_revision;
        if(response.selected>=0&&static_cast<std::uint64_t>(response.selected)<revision.original_review_feedback_options.size())
            return revision.original_review_feedback_options[static_cast<std::size_t>(response.selected)];
        return revision.original_review_feedback;
    }
    return curriculum::incorrect_feedback(block,response.selected<0?std::nullopt:std::optional<std::size_t>(static_cast<std::size_t>(response.selected)));
}
namespace {
curriculum::Block original_choice_block(const curriculum::Block& block) {
    const auto& revision=*block.choice_revision;auto original=block;
    original.options=revision.original_options;original.correct_index=revision.original_correct_index;
    original.question=revision.original_question;original.explanation=revision.original_explanation;
    original.review_feedback=revision.original_review_feedback;original.review_feedback_options=revision.original_review_feedback_options;
    original.raw_json=revision.raw_json;original.choice_revision.reset();return original;
}
void validate_choice_response(const Response& response,const curriculum::Block& block) {
    if(response.selected < -1 || (response.selected>=0&&static_cast<std::uint64_t>(response.selected)>=block.options.size()))
        throw std::runtime_error("La opción guardada no pertenece a esta edición; se conserva su origen.");
    if(response.correct&&(response.selected<0||!curriculum::evaluate_choice(block,static_cast<std::size_t>(response.selected)).correct))
        throw std::runtime_error("El acierto guardado no corresponde a esta edición; se conserva su origen.");
}
void validate_order_response(const Response& response, const curriculum::Block& block) {
    std::set<std::int64_t> seen;
    for (const auto index : response.order)
        if (index < 0 || static_cast<std::uint64_t>(index) >= block.fragments.size() || !seen.insert(index).second)
            throw std::runtime_error("El orden guardado contiene bloques ajenos o repetidos.");
    if (!response.order.empty() && response.order.size() != block.fragments.size())
        throw std::runtime_error("La cantidad de bloques guardados no coincide con esta actividad; se conserva el origen.");
    if (response.correct) {
        std::vector<std::size_t> order;
        for (const auto index : response.order) order.push_back(static_cast<std::size_t>(index));
        if (!curriculum::evaluate_order(block, order).correct)
            throw std::runtime_error("El acierto guardado no corresponde a este programa; sus datos se conservan.");
    }
}
curriculum::Block original_order_block(const curriculum::Block& block) {
    auto original = block;
    original.fragments = block.order_refinement->original_fragments;
    original.correct_order = block.order_refinement->original_correct_order;
    original.order_equivalence_groups.clear();
    original.order_refinement.reset();
    return original;
}
}
std::string response_storage_key(const curriculum::Lesson& lesson, const curriculum::Block& block) {
    const auto historical = curriculum::exercise_key(lesson,block);
    if(block.type==curriculum::BlockType::multiple_choice&&block.choice_revision) {
        const auto signature=sha256(curriculum::choice_representation_frame(block));
        if(signature!=block.choice_revision->representation_signature)throw std::runtime_error("La representación de opciones cambió sin conservar su origen.");
        return "choice-options/v1/"+lesson.locale+'/'+signature+'/'+historical;
    }
    if (block.type != curriculum::BlockType::order_code || !block.order_refinement) return historical;
    const auto signature = sha256(curriculum::order_representation_frame(block));
    if (!block.order_refinement->representation_signature.empty() &&
        signature != block.order_refinement->representation_signature)
        throw std::runtime_error("La representación de las líneas no corresponde a su contrato.");
    return "order-lines/v1/" + lesson.locale + '/' + signature + '/' + historical;
}
std::optional<Response> get_response(const StateData& data, const curriculum::Lesson& lesson, const curriculum::Block& block) {
    if(block.type==curriculum::BlockType::multiple_choice&&block.choice_revision) {
        auto historical=get_response(data,curriculum::exercise_key(lesson,block));
        if(historical)validate_choice_response(*historical,original_choice_block(block));
        if(auto current=get_response(data,response_storage_key(lesson,block))) {validate_choice_response(*current,block);return current;}
        if(historical)historical->historical_choice=true;
        return historical;
    }
    if (block.type != curriculum::BlockType::order_code || !block.order_refinement)
        return get_response(data,curriculum::exercise_key(lesson,block));
    const auto& refinement = *block.order_refinement;
    const auto original = original_order_block(block);
    const auto historical = get_response(data,curriculum::exercise_key(lesson,block));
    if (historical) validate_order_response(*historical,original);
    if (const auto saved = get_response(data,response_storage_key(lesson,block))) {
        validate_order_response(*saved,block);
        return saved;
    }
    if (!historical) return std::nullopt;
    auto projected = *historical;
    const bool reviewed = response_reviewed(projected,original);
    projected.order.clear();
    for (const auto group : historical->order) {
        if (static_cast<std::size_t>(group) >= refinement.groups.size())
            throw std::runtime_error("El grupo histórico no pertenece al refinamiento de líneas.");
        for (const auto line : refinement.groups.at(static_cast<std::size_t>(group)))
            projected.order.push_back(static_cast<std::int64_t>(line));
    }
    validate_order_response(projected,block);
    if (reviewed) mark_response_reviewed(projected,block);
    else clear_response_review(projected);
    // raw_json intentionally remains the original snapshot here. put_response
    // updates only the new sidecar's typed fields and keeps opaque metadata.
    return projected;
}
void put_response(StateData& data, const curriculum::Lesson& lesson, const curriculum::Block& block, const Response& response) {
    if(response.historical_choice)throw std::runtime_error("Una selección histórica necesita una elección o reinicio explícito antes de guardarse como actual.");
    if(block.type==curriculum::BlockType::multiple_choice&&block.choice_revision)validate_choice_response(response,block);
    if (block.type == curriculum::BlockType::order_code && block.order_refinement)
        validate_order_response(response,block);
    put_response(data,response_storage_key(lesson,block),response);
}
std::vector<std::int64_t> indentation_units(std::string_view source, std::string_view metadata) try {
    if (metadata.empty()) return {};
    windows::ensure_winrt_apartment();
    const auto json = object(metadata);
    if (json.GetNamedNumber(L"version") != 1 || json_string(json, L"sha256") != sha256(source))
        throw std::runtime_error("La identidad de sangria no coincide con el codigo guardado.");
    const auto text = wide(source); std::vector<std::int64_t> starts;
    std::int64_t previous = -4;
    for (const auto value : json.GetNamedArray(L"starts")) {
        const auto number = value.GetNumber(); const auto offset = json_integer(number);
        if (number != static_cast<double>(offset) || offset < previous + 4 || offset < 0 ||
            static_cast<std::uint64_t>(offset) + 4 > text.size() || text.substr(static_cast<std::size_t>(offset), 4) != L"    ")
            throw std::runtime_error("La identidad de sangria contiene rangos invalidos.");
        const auto first = text.rfind(L'\n', static_cast<std::size_t>(offset));
        const auto line_start = first == text.npos ? 0 : first + 1;
        for (auto cursor = line_start; cursor < static_cast<std::size_t>(offset); ++cursor)
            if (text[cursor] != L' ' && text[cursor] != L'\t')
                throw std::runtime_error("La identidad de sangria esta fuera del prefijo de linea.");
        starts.push_back(offset); previous = offset;
    }
    return starts;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("Los datos de identidad de sangria no son compatibles; se conserva el borrador.");
}
std::string make_indentation_metadata(std::string_view source, const std::vector<std::int64_t>& starts) try {
    windows::ensure_winrt_apartment(); JsonObject json; JsonArray entries;
    json.Insert(L"version", JsonValue::CreateNumberValue(1));
    json.Insert(L"sha256", JsonValue::CreateStringValue(wide(sha256(source))));
    for (auto offset : starts) entries.Append(JsonValue::CreateNumberValue(static_cast<double>(offset)));
    json.Insert(L"starts", entries); const auto text = utf8(json.Stringify());
    (void)indentation_units(source, text); return text;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("No se pudo codificar la identidad de sangria.");
}
Draft get_draft(const StateData& data, std::string_view key, std::string_view code, std::string_view input) try {
    windows::ensure_winrt_apartment(); Draft result;
    const auto base = std::string(key);
    result.has_saved = find(data, "cpp_lesson_drafts", base + "/source") != nullptr;
    result.source = string_value(data, "cpp_lesson_drafts", base + "/source", code);
    result.input = string_value(data, "cpp_lesson_drafts", base + "/input", input);
    const auto decode_text = [&](std::string& text, std::string_view suffix, std::wstring_view field) {
        if (!find(data, "cpp_lesson_drafts", base + std::string(suffix))) return;
        const auto encoding = string_value(data, "cpp_lesson_drafts", base + std::string(suffix));
        if (encoding != "json-v1") throw std::runtime_error("Version del borrador Android no compatible.");
        const auto json = object(text);
        if (json.GetNamedNumber(L"version") != 1) throw std::runtime_error("Version del texto no compatible.");
        text = utf8(json.GetNamedString(field));
    };
    decode_text(result.source, "/sourceEncoding", L"code");
    decode_text(result.input, "/inputEncoding", L"input");
    result.indentation_json = string_value(data, "cpp_lesson_drafts", base + "/sourceIndentation_v2");
    // Only the independently stored pre-damage digest can prove old XML indentation damage.
    if (!result.indentation_json.empty()) {
        const auto meta = object(result.indentation_json);
        if (meta.GetNamedNumber(L"version") != 1) throw std::runtime_error("Version de identidad de sangria no compatible.");
        const auto expected = json_string(meta, L"sha256");
        // json-v1 already escapes LF and cannot acquire closing-tag indentation.
        // The Android decoder only applies this old repair to a source without encoding.
        if (!find(data, "cpp_lesson_drafts", base + "/sourceEncoding") &&
            sha256(result.source) != expected && result.source.ends_with("\n    ") &&
            sha256(std::string_view(result.source).substr(0, result.source.size() - 4)) == expected)
            result.source.resize(result.source.size() - 4);
        // CppIndentationUnits.loadedText keeps the exact source when an old sidecar
        // no longer belongs to it. Unproved spaces must not become tab units.
        if (sha256(result.source) != expected) result.indentation_json.clear();
        else (void)indentation_units(result.source, result.indentation_json);
    }
    const auto input_hash = string_value(data, "cpp_lesson_drafts", base + "/inputSha256");
    if (!input_hash.empty() && sha256(result.input) != input_hash && result.input.ends_with("\n    ") &&
        sha256(std::string_view(result.input).substr(0, result.input.size() - 4)) == input_hash)
        result.input.resize(result.input.size() - 4);
    result.selection_start = integer_value(data, "cpp_lesson_drafts", base + "/windowsSelectionStart");
    result.selection_end = integer_value(data, "cpp_lesson_drafts", base + "/windowsSelectionEnd");
    return result;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("El borrador contiene JSON o tipos no compatibles; sus datos se conservan.");
}
void put_draft(StateData& data, std::string_view key, const Draft& draft) try {
    windows::ensure_winrt_apartment();
    if (!draft.indentation_json.empty()) (void)indentation_units(draft.source, draft.indentation_json);
    const auto length = wide(draft.source).size();
    if (draft.selection_start < 0 || draft.selection_end < 0 ||
        static_cast<std::uint64_t>(draft.selection_start) > length || static_cast<std::uint64_t>(draft.selection_end) > length)
        throw std::runtime_error("Seleccion fuera del codigo.");
    auto& prefs = data.preferences["cpp_lesson_drafts"]; const auto base = std::string(key);
    const auto encoded = [&](std::wstring_view field, const std::string& text) {
        JsonObject object; object.Insert(L"version", JsonValue::CreateNumberValue(1));
        object.Insert(field, JsonValue::CreateStringValue(wide(text))); return utf8(object.Stringify());
    };
    prefs[base + "/source"] = encoded(L"code", draft.source);
    prefs[base + "/sourceEncoding"] = std::string("json-v1");
    prefs[base + "/input"] = encoded(L"input", draft.input);
    prefs[base + "/inputEncoding"] = std::string("json-v1");
    prefs[base + "/inputSha256"] = sha256(draft.input);
    prefs[base + "/sourceIndentation_v2"] = draft.indentation_json;
    prefs[base + "/windowsSelectionStart"] = draft.selection_start;
    prefs[base + "/windowsSelectionEnd"] = draft.selection_end;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("No se pudo codificar el borrador; el estado anterior se conserva.");
}

namespace {
bool bool_value(const StateData& data, std::string_view key) {
    if (const auto value = find(data, "learning_progress", key)) {
        if (const auto boolean = std::get_if<bool>(value)) return *boolean;
        throw std::runtime_error("Tipo de estado de lectura no valido.");
    }
    return false;
}
std::vector<std::string> set_value(const StateData& data, const std::string& key) {
    if (const auto value = find(data, "learning_progress", key)) {
        if (const auto set = std::get_if<std::vector<std::string>>(value)) return *set;
        throw std::runtime_error("Cobertura de lectura no valida.");
    }
    throw std::runtime_error("Cobertura de lectura incompleta; se conserva el estado.");
}
std::string lineage_of(const std::vector<curriculum::StatePage>& pages) {
    std::string frame;
    for (const auto& page : pages) {
        if (!frame.empty()) frame += '|';
        frame += page.id + ':';
        bool first = true;
        for (const auto& id : page.blocks) { if (!first) frame += ','; first = false; frame += id; }
    }
    return sha256(frame);
}
void check_coverage(const curriculum::StateLayout& layout, const std::vector<std::string>& pages,
                    const std::vector<std::string>& blocks) {
    std::set<std::string> page_ids, block_ids;
    for (const auto& page : layout.origin) { page_ids.insert(page.id); for (const auto& id : page.blocks) block_ids.insert(id); }
    for (const auto& addition : layout.additions) block_ids.insert(addition.id);
    for (const auto& id : pages) if (!page_ids.contains(id)) throw std::runtime_error("Pagina guardada ajena a esta familia.");
    for (const auto& id : blocks) if (!block_ids.contains(id)) throw std::runtime_error("Bloque guardado ajeno a esta familia.");
}
JsonArray addition_records(const curriculum::StateLayout& layout) {
    JsonArray result;
    for (const auto& addition : layout.additions) {
        JsonArray row; row.Append(JsonValue::CreateStringValue(wide(addition.id)));
        row.Append(JsonValue::CreateStringValue(wide(addition.sha256)));
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(addition.storage.page)));
        row.Append(JsonValue::CreateNumberValue(static_cast<double>(addition.storage.block)));
        row.Append(JsonValue::CreateStringValue(wide(addition.progress_page))); result.Append(row);
    }
    return result;
}
JsonArray retirement_records(const curriculum::StateLayout& layout) {
    JsonArray result;
    for (const auto& retired : layout.retired) {
        JsonArray row;
        for (const auto* field : {&retired.id,&retired.page,&retired.next,&retired.sha256})
            row.Append(JsonValue::CreateStringValue(wide(*field)));
        result.Append(row);
    }
    return result;
}
std::string retirement_key(const curriculum::Lesson& lesson) {
    return "lesson_" + lesson.id + "_state_retired_v2_" + lesson.locale;
}
std::optional<std::string> retirement_state_update(const StateData& data, const curriculum::Lesson& lesson) {
    const auto& layout = lesson.state_layout;
    const auto key = retirement_key(lesson);
    const auto current = retirement_records(layout);
    if (find(data,"learning_progress",key)) {
        if (layout.version != 2) throw std::runtime_error("Falta el archivo historico de actividades retiradas.");
        const auto raw = string_value(data,"learning_progress",key);
        if (raw.empty()) throw std::runtime_error("El archivo de actividades retiradas no es valido.");
        const auto previous = JsonArray::Parse(wide(raw));
        if (previous.Size() > current.Size()) throw std::runtime_error("Se retiraron identidades del archivo historico.");
        for (std::uint32_t index = 0; index < previous.Size(); ++index)
            if (previous.GetAt(index).Stringify() != current.GetAt(index).Stringify())
                throw std::runtime_error("Cambio el contrato de una actividad retirada.");
        if (previous.Size() == current.Size()) return {};
    } else if (!current.Size()) return {};
    return utf8(current.Stringify());
}
std::string retirement_history_key(const curriculum::Lesson& lesson) {
    return "lesson_"+lesson.id+"_state_retirement_history_v2_"+lesson.locale;
}
std::optional<std::string> retirement_history_state_update(const StateData& data,const curriculum::Lesson& lesson) {
    const auto key=retirement_history_key(lesson);JsonArray current;
    for(const auto& snapshot:lesson.state_layout.retirement_history)
        current.Append(JsonValue::CreateStringValue(wide(snapshot.sha256)));
    if(find(data,"learning_progress",key)) {
        if(lesson.state_layout.version!=2)throw std::runtime_error("Falta el historial de lectura de las actividades retiradas.");
        const auto raw=string_value(data,"learning_progress",key);
        if(raw.empty())throw std::runtime_error("El historial de lectura retirado no es valido.");
        const auto previous=JsonArray::Parse(wide(raw));
        if(previous.Size()>current.Size())throw std::runtime_error("Se elimino una presentacion historica retirada.");
        for(std::uint32_t index=0;index<previous.Size();++index)
            if(previous.GetAt(index).Stringify()!=current.GetAt(index).Stringify())
                throw std::runtime_error("Cambio una presentacion historica retirada.");
        if(previous.Size()==current.Size())return {};
    } else if(!current.Size())return {};
    return utf8(current.Stringify());
}
std::string order_refinement_key(const curriculum::Lesson& lesson) {
    return "lesson_"+lesson.id+"_state_order_refinements_v1_"+lesson.locale;
}
std::optional<std::string> order_refinement_state_update(const StateData& data,const curriculum::Lesson& lesson) {
    const auto key=order_refinement_key(lesson);JsonArray current;
    for(const auto& refinement:lesson.state_layout.order_refinements) {
        JsonArray record;
        for(const auto* field:{&refinement.id,&refinement.sha256,&refinement.representation_signature})
            record.Append(JsonValue::CreateStringValue(wide(*field)));
        current.Append(record);
    }
    if(find(data,"learning_progress",key)) {
        if(lesson.state_layout.version!=2)throw std::runtime_error("Falta el mapa del refinamiento histórico de líneas.");
        const auto raw=string_value(data,"learning_progress",key);
        if(raw.empty())throw std::runtime_error("La procedencia de las líneas guardadas no es válida.");
        const auto previous=JsonArray::Parse(wide(raw));
        if(previous.Size()>current.Size())throw std::runtime_error("Se eliminó un refinamiento de líneas publicado.");
        for(std::uint32_t index=0;index<previous.Size();++index)
            if(previous.GetAt(index).Stringify()!=current.GetAt(index).Stringify())
                throw std::runtime_error("Cambió el origen o la representación de las líneas guardadas.");
        if(previous.Size()==current.Size())return {};
    } else if(!current.Size())return {};
    return utf8(current.Stringify());
}
std::string choice_revision_key(const curriculum::Lesson& lesson) {
    return "lesson_"+lesson.id+"_state_choice_revisions_v1_"+lesson.locale;
}
std::optional<std::string> choice_revision_state_update(const StateData& data,const curriculum::Lesson& lesson) {
    const auto key=choice_revision_key(lesson);JsonArray current;
    for(const auto& revision:lesson.state_layout.choice_revisions) {
        JsonArray row;for(const auto* field:{&revision.id,&revision.sha256,&revision.representation_signature})
            row.Append(JsonValue::CreateStringValue(wide(*field)));
        current.Append(row);
    }
    if(find(data,"learning_progress",key)) {
        if(lesson.state_layout.version!=2)throw std::runtime_error("Falta el mapa de las opciones históricas.");
        const auto raw=string_value(data,"learning_progress",key);
        if(raw.empty())throw std::runtime_error("La procedencia de las opciones guardadas no es válida.");
        const auto previous=JsonArray::Parse(wide(raw));
        if(previous.Size()>current.Size())throw std::runtime_error("Se eliminó una revisión publicada de opciones.");
        for(std::uint32_t index=0;index<previous.Size();++index)
            if(previous.GetAt(index).Stringify()!=current.GetAt(index).Stringify())throw std::runtime_error("Cambió la edición histórica de una opción guardada.");
        if(previous.Size()==current.Size())return {};
    }else if(!current.Size())return {};
    return utf8(current.Stringify());
}
}
void prepare_lesson_state(StateData& data, const curriculum::Lesson& lesson) try {
    windows::ensure_winrt_apartment();
    const auto& layout = lesson.state_layout; const auto base = "lesson_" + lesson.id;
    // Validate the archive before any lineage/adoption writes can take place.
    const auto retirement_update = retirement_state_update(data,lesson);
    const auto retirement_history_update = retirement_history_state_update(data,lesson);
    const auto order_refinement_update = order_refinement_state_update(data,lesson);
    const auto choice_revision_update = choice_revision_state_update(data,lesson);
    const auto old_pin_key = base + "_state_lineage_v1";
    const bool has_old_pin = find(data, "learning_progress", old_pin_key) != nullptr;
    const auto old_pin = string_value(data, "learning_progress", old_pin_key);
    const auto owner = string_value(data, "learning_progress", base + "_state_v1_owner_locale");
    if (layout.version != 2 && find(data, "learning_progress", base + "_state_lineage_v2_" + lesson.locale))
        throw std::runtime_error("Falta el mapa de estado de esta familia; no se usan posiciones visibles.");
    if (!layout.version) {
        if (has_old_pin && (!find(data, "learning_progress", base + "_state_v1_owner_locale") || owner == lesson.locale))
            throw std::runtime_error("Falta el mapa historico del progreso guardado.");
        return;
    }
    if (layout.version == 2 && has_old_pin && old_pin != layout.legacy_lineage) {
        const auto other = layout.family.find(owner);
        const bool proven_other = !owner.empty() && owner != lesson.locale && other != layout.family.end() &&
            lineage_of(other->second.origin) == old_pin;
        if (!proven_other) throw std::runtime_error("La familia historica del progreso no coincide.");
    }
    const auto suffix = layout.version == 2 ? "v2_" + layout.locale : "v1";
    const auto pin_key = base + "_state_lineage_" + suffix;
    const auto pages_key = base + "_read_pages_" + suffix;
    const auto blocks_key = base + "_read_blocks_" + suffix;
    const auto additions_key = base + "_state_additions_v2_" + layout.locale;
    const auto pin = string_value(data, "learning_progress", pin_key);
    if (find(data, "learning_progress", pin_key)) {
        if (pin != layout.lineage) throw std::runtime_error("Las identidades originales del progreso cambiaron.");
        check_coverage(layout, set_value(data, pages_key), set_value(data, blocks_key));
        if (layout.version == 2) {
            const auto raw = string_value(data, "learning_progress", additions_key);
            if (raw.empty()) throw std::runtime_error("Faltan las identidades de actividades nuevas.");
            const auto previous = JsonArray::Parse(wide(raw)); const auto current = addition_records(layout);
            if (previous.Size() > current.Size()) throw std::runtime_error("Cambieron las actividades nuevas.");
            for (std::uint32_t index = 0; index < previous.Size(); ++index)
                if (previous.GetAt(index).Stringify() != current.GetAt(index).Stringify())
                    throw std::runtime_error("Cambio la propiedad de una actividad nueva.");
            if (previous.Size() != current.Size()) data.preferences["learning_progress"][additions_key] = utf8(current.Stringify());
        }
        if (retirement_update) data.preferences["learning_progress"][retirement_key(lesson)] = *retirement_update;
        if (retirement_history_update) data.preferences["learning_progress"][retirement_history_key(lesson)] = *retirement_history_update;
        if (order_refinement_update) data.preferences["learning_progress"][order_refinement_key(lesson)] = *order_refinement_update;
        if (choice_revision_update) data.preferences["learning_progress"][choice_revision_key(lesson)] = *choice_revision_update;
        return;
    }
    if (find(data, "learning_progress", pages_key) || find(data, "learning_progress", blocks_key) ||
        (layout.version == 2 && (find(data, "learning_progress", additions_key) ||
                               find(data,"learning_progress",retirement_key(lesson)) ||
                               find(data,"learning_progress",retirement_history_key(lesson)) ||
                               find(data,"learning_progress",order_refinement_key(lesson)) ||
                               find(data,"learning_progress",choice_revision_key(lesson)))))
        throw std::runtime_error("Migracion de estado incompleta; no se reemplaza el progreso.");
    std::vector<std::string> pages, blocks;
    const bool started = bool_value(data, base + "_started");
    if (started) {
        const auto saved = saved_page(data, lesson.id);
        for (std::size_t index = 0; index <= (std::min)(saved, layout.origin.size() - 1); ++index) {
            pages.push_back(layout.origin[index].id);
            blocks.insert(blocks.end(), layout.origin[index].blocks.begin(), layout.origin[index].blocks.end());
        }
    }
    const bool adopt = layout.version == 2 && has_old_pin && old_pin == layout.legacy_lineage;
    if (adopt) {
        pages = set_value(data, base + "_read_pages_v1"); blocks = set_value(data, base + "_read_blocks_v1");
        check_coverage(layout, pages, blocks);
    }
    auto& prefs = data.preferences["learning_progress"];
    prefs[pin_key] = layout.lineage; prefs[pages_key] = pages; prefs[blocks_key] = blocks;
    if (layout.version == 2) {
        prefs[additions_key] = utf8(addition_records(layout).Stringify());
        if (adopt) prefs[base + "_state_v1_owner_locale"] = lesson.locale;
    }
    if (retirement_update) prefs[retirement_key(lesson)] = *retirement_update;
    if (retirement_history_update) prefs[retirement_history_key(lesson)] = *retirement_history_update;
    if (order_refinement_update) prefs[order_refinement_key(lesson)] = *order_refinement_update;
    if (choice_revision_update) prefs[choice_revision_key(lesson)] = *choice_revision_update;
    if (started && !find(data, "learning_progress", base + "_page")) prefs[base + "_page"] = std::int64_t{0};
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("Los metadatos del progreso no son compatibles; no se usan direcciones visibles.");
}
void validate_lesson_state(const StateData& data, const curriculum::Lesson& lesson, std::string_view route_id) try {
    auto metadata = data;
    prepare_lesson_state(metadata, lesson);
    (void)current_page(metadata, lesson);
    std::vector<const curriculum::Block*> preserved_blocks;
    for (const auto& page : lesson.pages) for (const auto& block : page.blocks) preserved_blocks.push_back(&block);
    for (const auto& block : lesson.retired_blocks) preserved_blocks.push_back(&block);
    for (const auto* preserved : preserved_blocks) {
        const auto& block = *preserved;
        if (block.type == curriculum::BlockType::cpp_compiler) {
            (void)get_draft(data, curriculum::draft_key(route_id, lesson, block),
                            block.initial_code, block.standard_input.value_or(""));
            continue;
        }
        if (block.type != curriculum::BlockType::multiple_choice && block.type != curriculum::BlockType::order_code &&
            block.type != curriculum::BlockType::output_prediction && block.type != curriculum::BlockType::fill_code &&
            block.type != curriculum::BlockType::inline_fill_code && block.type != curriculum::BlockType::drag_fill_code) continue;
        const auto response = get_response(data, lesson, block);
        if (!response) continue;
        bool correct = false;
        if (block.type == curriculum::BlockType::multiple_choice) {
            if (response->selected < -1 || (response->selected >= 0 &&
                static_cast<std::uint64_t>(response->selected) >= block.options.size()))
                throw std::runtime_error("Una opcion guardada no pertenece a esta actividad; la importacion no se ha publicado.");
            if (response->correct && response->selected >= 0)
                correct = curriculum::evaluate_choice(block, static_cast<std::size_t>(response->selected)).correct;
        } else if (block.type == curriculum::BlockType::order_code) {
            std::set<std::int64_t> seen;
            for (const auto index : response->order)
                if (index < 0 || static_cast<std::uint64_t>(index) >= block.fragments.size() || !seen.insert(index).second)
                    throw std::runtime_error("El orden guardado contiene bloques ajenos o repetidos.");
            if (!response->order.empty() && response->order.size() != block.fragments.size())
                throw std::runtime_error("La cantidad de bloques guardados no coincide con esta actividad; se conserva el origen.");
            if (response->correct) {
                std::vector<std::size_t> order;
                for (const auto index : response->order) order.push_back(static_cast<std::size_t>(index));
                correct = curriculum::evaluate_order(block, order).correct;
            }
        } else if (block.type == curriculum::BlockType::inline_fill_code || block.type == curriculum::BlockType::drag_fill_code) {
            const auto raw = response->raw_json.empty() ? JsonObject() : object(response->raw_json);
            const auto inline_fill = block.type == curriculum::BlockType::inline_fill_code;
            const auto field = inline_fill ? L"answers" : L"slots";
            const auto values = raw.HasKey(field) ? raw.GetNamedArray(field) : JsonArray{};
            const auto expected_count = inline_fill ? block.answers.size() : block.correct_order.size();
            if (values.Size() && values.Size() != expected_count)
                throw std::runtime_error("La cantidad de huecos guardados no coincide con esta actividad; se conserva el origen.");
            if (inline_fill) {
                std::vector<std::string> answers;
                for (const auto& item : values) answers.push_back(utf8(item.GetString()));
                if (response->correct) correct = curriculum::evaluate_hole_answers(block,answers).correct;
            } else {
                std::vector<std::size_t> selected; std::set<std::int64_t> used;
                for (const auto& item : values) {
                    const auto slot = json_integer(item.GetNumber());
                    if (slot < -1 || (slot >= 0 && (static_cast<std::uint64_t>(slot) >= block.choices.size() || !used.insert(slot).second)))
                        throw std::runtime_error("Los huecos guardados contienen fichas ajenas o repetidas; se conserva el origen.");
                    selected.push_back(slot < 0 ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(slot));
                }
                if (response->correct) correct = curriculum::evaluate_drag_answers(block,selected).correct;
            }
        } else if (response->correct) correct = curriculum::evaluate_text_answer(block, response->answer).correct;
        if (response->correct && !correct)
            throw std::runtime_error("Una respuesta marcada correcta no es compatible con el contrato de esta actividad; sus datos no se han reemplazado.");
    }
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("La respuesta guardada contiene huecos o tipos incompatibles; sus datos se conservan.");
}
void save_current_page(StateData& data, const curriculum::Lesson& lesson, std::size_t page) {
    prepare_lesson_state(data, lesson);
    set_saved_page(data, lesson.id, curriculum::storage_page(lesson, page));
    const auto& layout = lesson.state_layout;
    if (!layout.version) return;
    const auto suffix = layout.version == 2 ? "v2_" + layout.locale : "v1";
    const auto base = "lesson_" + lesson.id;
    auto pages = set_value(data, base + "_read_pages_" + suffix);
    auto blocks = set_value(data, base + "_read_blocks_" + suffix);
    const auto& current = layout.current.at(page);
    if (std::find(pages.begin(), pages.end(), current.id) == pages.end()) pages.push_back(current.id);
    for (const auto& block : current.blocks)
        if (std::find(blocks.begin(), blocks.end(), block) == blocks.end()) blocks.push_back(block);
    auto& prefs = data.preferences["learning_progress"];
    prefs[base + "_read_pages_" + suffix] = pages; prefs[base + "_read_blocks_" + suffix] = blocks;
}
std::size_t current_page(const StateData& data, const curriculum::Lesson& lesson) {
    if (!find(data, "learning_progress", "lesson_" + lesson.id + "_page")) return 0;
    return curriculum::display_page(lesson, saved_page(data, lesson.id));
}
} // namespace lgaed::state

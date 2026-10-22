#include "platform/windows/android_import.hpp"
#include "platform/windows/state_store.hpp"
#include "platform/windows/winrt_apartment.hpp"

#include <windows.h>
#include <xmllite.h>
#include <ole2.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <set>
#include <cstring>

namespace lgaed::state {
namespace {
using namespace winrt::Windows::Data::Json;
template<class T> class ComPtr {
public:
    T* value = nullptr;
    ~ComPtr() { if (value) value->Release(); }
    T** put() { return &value; }
    T* operator->() const { return value; }
};
void check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("El XML de preferencias no es valido."); }
std::string utf8(const std::wstring& value) { return winrt::to_string(value); }
struct XmlElement {
    std::wstring name, text;
    std::map<std::wstring, std::wstring> attributes;
    std::vector<XmlElement> children;
};
std::wstring xml_value(IXmlReader* reader) {
    const wchar_t* value = nullptr; UINT length = 0;
    check(reader->GetValue(&value, &length));
    return value ? std::wstring(value, length) : std::wstring{};
}
std::wstring xml_name(IXmlReader* reader) {
    const wchar_t* value = nullptr; UINT length = 0;
    check(reader->GetNamespaceUri(&value, &length));
    if (length) throw std::runtime_error("Las preferencias Android no admiten espacios de nombres XML.");
    check(reader->GetPrefix(&value, &length));
    if (length) throw std::runtime_error("Las preferencias Android no admiten prefijos XML.");
    check(reader->GetLocalName(&value, &length));
    return value ? std::wstring(value, length) : std::wstring{};
}
bool only_xml_whitespace(std::wstring_view text) {
    for (const auto character : text)
        if (character != L' ' && character != L'\t' && character != L'\r' && character != L'\n') return false;
    return true;
}
const std::wstring& attribute(const XmlElement& element, const wchar_t* name) {
    const auto value = element.attributes.find(name);
    if (value == element.attributes.end()) throw std::runtime_error("Falta un atributo de preferencias Android.");
    return value->second;
}
std::int64_t integer(const std::wstring& text) {
    const auto str = utf8(text); std::int64_t value = 0;
    const auto result = std::from_chars(str.data(), str.data() + str.size(), value);
    if (result.ec != std::errc{} || result.ptr != str.data() + str.size()) throw std::runtime_error("Entero Android no valido.");
    return value;
}
XmlElement read_xml_document(const std::filesystem::path& file) {
    const auto bytes = read_bytes(file);
    const auto memory = GlobalAlloc(GMEM_MOVEABLE, bytes.empty() ? 1 : bytes.size());
    if (!memory) throw std::runtime_error("No se pudo reservar memoria para el XML de preferencias.");
    const auto buffer = GlobalLock(memory);
    if (!buffer) { GlobalFree(memory); throw std::runtime_error("No se pudo leer el XML de preferencias."); }
    if (!bytes.empty()) std::memcpy(buffer, bytes.data(), bytes.size());
    GlobalUnlock(memory);
    ComPtr<IStream> input;
    const auto created = CreateStreamOnHGlobal(memory, TRUE, input.put());
    if (FAILED(created)) { GlobalFree(memory); check(created); }
    ULARGE_INTEGER size{}; size.QuadPart = bytes.size(); check(input->SetSize(size));
    ComPtr<IXmlReader> reader;
    check(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(reader.put()), nullptr));
    check(reader->SetProperty(XmlReaderProperty_ConformanceLevel, XmlConformanceLevel_Document));
    check(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
    check(reader->SetProperty(XmlReaderProperty_XmlResolver, 0));
    check(reader->SetProperty(XmlReaderProperty_MaxElementDepth, 8));
    check(reader->SetProperty(XmlReaderProperty_MaxEntityExpansion, 100000));
    check(reader->SetInput(input.value));
    std::vector<XmlElement> stack; std::optional<XmlElement> root;
    std::size_t elements = 0;
    const auto finish = [&] {
        auto node = std::move(stack.back()); stack.pop_back();
        if (stack.empty()) {
            if (root) throw std::runtime_error("Las preferencias contienen mas de una raiz XML.");
            root = std::move(node);
        } else stack.back().children.push_back(std::move(node));
    };
    XmlNodeType type{}; HRESULT result = S_OK;
    while ((result = reader->Read(&type)) == S_OK) {
        if (type == XmlNodeType_Element) {
            if (++elements > 1000000 || stack.size() >= 8) throw std::runtime_error("El XML supera el limite de seguridad.");
            XmlElement node; node.name = xml_name(reader.value);
            const bool empty = reader->IsEmptyElement() != FALSE;
            auto moved = reader->MoveToFirstAttribute(); check(moved);
            while (moved == S_OK) {
                auto name = xml_name(reader.value); auto value = xml_value(reader.value);
                if (!node.attributes.emplace(std::move(name), std::move(value)).second)
                    throw std::runtime_error("Atributo XML duplicado.");
                moved = reader->MoveToNextAttribute(); check(moved);
            }
            check(reader->MoveToElement()); stack.push_back(std::move(node));
            if (empty) finish();
        } else if (type == XmlNodeType_EndElement) {
            if (stack.empty() || xml_name(reader.value) != stack.back().name)
                throw std::runtime_error("Estructura XML de preferencias no valida.");
            finish();
        } else if (type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace) {
            auto text = xml_value(reader.value);
            if (!stack.empty()) stack.back().text += text;
            else if (!only_xml_whitespace(text)) throw std::runtime_error("Texto fuera del mapa SharedPreferences.");
        } else if (type != XmlNodeType_XmlDeclaration && type != XmlNodeType_Comment && type != XmlNodeType_ProcessingInstruction)
            throw std::runtime_error("Tipo XML no permitido en preferencias Android.");
    }
    check(result);
    if (result != S_FALSE || !root || !stack.empty()) throw std::runtime_error("XML de preferencias incompleto.");
    return std::move(*root);
}
Preferences parse_preferences(const std::filesystem::path& file) {
    const auto root = read_xml_document(file);
    if (root.name != L"map" || !root.attributes.empty() || !only_xml_whitespace(root.text))
        throw std::runtime_error("No es un mapa SharedPreferences Android.");
    Preferences preferences;
    for (const auto& node : root.children) {
        const auto& tag = node.name; const auto key = utf8(attribute(node, L"name")); Value value;
        if (key.empty()) throw std::runtime_error("Clave Android vacia.");
        const bool text_value = tag == L"string" || tag == L"set";
        if (node.attributes.size() != (text_value ? 1U : 2U))
            throw std::runtime_error("Atributos Android no compatibles.");
        if (tag != L"set" && !node.children.empty()) throw std::runtime_error("Elemento inesperado dentro de una preferencia.");
        if (tag != L"string" && !only_xml_whitespace(node.text)) throw std::runtime_error("Texto inesperado dentro de una preferencia.");
        if (tag == L"string") {
            value = utf8(node.text);
        }
        else if (tag == L"int" || tag == L"long") {
            const auto number = integer(attribute(node, L"value"));
            if (tag == L"int" && (number < INT32_MIN || number > INT32_MAX))
                throw std::runtime_error("Entero Android fuera de rango.");
            value = number;
        }
        else if (tag == L"boolean") {
            const auto text = attribute(node, L"value");
            if (text != L"true" && text != L"false") throw std::runtime_error("Booleano Android no valido.");
            value = text == L"true";
        } else if (tag == L"float") {
            const auto text = utf8(attribute(node, L"value")); float number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(number))
                throw std::runtime_error("Numero Android no compatible.");
            value = static_cast<double>(number);
        } else if (tag == L"set") {
            std::vector<std::string> strings; std::set<std::string> unique;
            for (const auto& entry : node.children) {
                if (entry.name != L"string" || !entry.children.empty() || !entry.attributes.empty())
                    throw std::runtime_error("Conjunto Android con elementos inesperados.");
                const auto string = utf8(entry.text);
                if (!unique.insert(string).second) throw std::runtime_error("Conjunto Android con entradas duplicadas.");
                strings.push_back(string);
            }
            value = std::move(strings);
        } else throw std::runtime_error("Tipo SharedPreferences desconocido; no se ha importado nada.");
        if (!preferences.emplace(key, std::move(value)).second) throw std::runtime_error("Clave Android duplicada.");
    }
    return preferences;
}
bool safe_relative(const std::filesystem::path& value) {
    if (value.empty() || value.is_absolute()) return false;
    for (const auto& part : value) if (part == ".." || part == ".") return false;
    return true;
}
std::filesystem::path utf8_path(std::string_view value) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
bool project_path(std::string_view value) {
    if (value.empty() || value.front() == ' ' || value.back() == ' ' || !safe_relative(utf8_path(value))) return false;
    for (unsigned char character : value)
        if (character < 32 || std::string_view("\\:<>\"|?*").find(static_cast<char>(character)) != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto slash = value.find('/', start); const auto end = slash == value.npos ? value.size() : slash;
        const auto part = value.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == value.npos) break;
        start = slash + 1;
    }
    return true;
}
bool source_path(std::string_view path) {
    const auto dot = path.rfind('.'); if (dot == path.npos) return false;
    auto extension = std::string(path.substr(dot + 1));
    for (auto& letter : extension) if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter + ('a' - 'A'));
    return extension == "cpp" || extension == "cc" || extension == "cxx" || extension == "c++";
}
void check_file_hierarchy(const std::set<std::string>& paths) {
    for (const auto& path : paths) {
        auto slash = path.find('/');
        while (slash != path.npos) {
            if (paths.contains(path.substr(0, slash))) throw std::runtime_error("Un archivo de proyecto tambien figura como carpeta.");
            slash = path.find('/', slash + 1);
        }
    }
}
void validate_json_versions(const StateData& data) {
    const auto drafts = data.preferences.find("cpp_lesson_drafts");
    if (drafts == data.preferences.end()) return;
    for (const auto& [key, value] : drafts->second) {
        const auto text = std::get_if<std::string>(&value);
        if (!text) continue;
        if (key.ends_with("/sourceEncoding") || key.ends_with("/inputEncoding")) {
            if (*text != "json-v1") throw std::runtime_error("Codificacion de borrador Android no compatible.");
            const bool source = key.ends_with("/sourceEncoding");
            const auto base = key.substr(0, key.size() - (source ? 15 : 14));
            const auto raw = string_value(data, "cpp_lesson_drafts", base + (source ? "/source" : "/input"));
            const auto json = JsonObject::Parse(winrt::to_hstring(raw));
            if (json.GetNamedNumber(L"version") != 1 ||
                json.GetNamedValue(source ? L"code" : L"input").ValueType() != JsonValueType::String)
                throw std::runtime_error("Version JSON de borrador no compatible.");
        } else if (key.ends_with("/sourceIndentation_v2") && !text->empty()) {
            const auto json = JsonObject::Parse(winrt::to_hstring(*text));
            if (json.GetNamedNumber(L"version") != 1 || json.GetNamedString(L"sha256").size() != 64 ||
                json.GetNamedValue(L"starts").ValueType() != JsonValueType::Array)
                throw std::runtime_error("Version de sangria Android no compatible.");
        } else if (key.ends_with("/project")) {
            const auto json = JsonObject::Parse(winrt::to_hstring(*text));
            if (!json.HasKey(L"files") || !json.HasKey(L"sources")) throw std::runtime_error("Borrador de proyecto Android no compatible.");
            std::set<std::string> paths;
            for (const auto entry : json.GetNamedArray(L"files")) {
                const auto item = entry.GetObject(); const auto path = winrt::to_string(item.GetNamedString(L"path"));
                if (!project_path(path) || !paths.insert(path).second)
                    throw std::runtime_error("Ruta de proyecto Android no compatible.");
                (void)item.GetNamedString(L"code");
            }
            if (paths.empty()) throw std::runtime_error("Proyecto sin archivos.");
            check_file_hierarchy(paths); std::set<std::string> selected;
            for (const auto source : json.GetNamedArray(L"sources")) {
                const auto path = winrt::to_string(source.GetString());
                if (!paths.contains(path) || !source_path(path) || !selected.insert(path).second)
                    throw std::runtime_error("Fuente de proyecto ausente o repetida.");
            }
        }
    }
}
class RevisionReader {
    std::span<const std::uint8_t> bytes_; std::size_t offset_ = 0;
public:
    explicit RevisionReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint64_t number(unsigned count) {
        if (bytes_.size() - offset_ < count) throw std::runtime_error("Revision Android incompleta.");
        std::uint64_t value = 0;
        while (count--) value = (value << 8) | bytes_[offset_++];
        return value;
    }
    std::string text(std::size_t limit = 16 * 1024 * 1024) {
        const auto size = number(4);
        if (size > limit || size > bytes_.size() - offset_) throw std::runtime_error("Texto de revision no valido.");
        const auto first = bytes_.data() + offset_;
        std::string text(reinterpret_cast<const char*>(first), static_cast<std::size_t>(size));
        offset_ += static_cast<std::size_t>(size);
        // Json conversion is not used for source; this call only validates Unicode round-trip.
        if (winrt::to_string(winrt::to_hstring(text)) != text) throw std::runtime_error("UTF-8 de revision no valido.");
        return text;
    }
    std::size_t count() { const auto value = number(4); if (value > 256) throw std::runtime_error("Demasiados archivos en revision."); return static_cast<std::size_t>(value); }
    bool done() const { return offset_ == bytes_.size(); }
};
void validate_revision(const ImportFile& file, std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 32 * 1024 * 1024 || file.relative_path.stem().string() != sha256(bytes))
        throw std::runtime_error("SHA-256 de revision Android no coincide.");
    RevisionReader reader(bytes);
    if (reader.text(128) != "lgp-cpp-local-history-v1" ||
        reader.text(128) != file.relative_path.parent_path().filename().string())
        throw std::runtime_error("Version o identidad de historial Android no compatible.");
    const auto uuid = reader.text(128);
    if (uuid.size() != 36) throw std::runtime_error("UUID de revision no valido.");
    for (std::size_t index = 0; index < uuid.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (uuid[index] != '-') throw std::runtime_error("UUID de revision no valido.");
        } else if (!((uuid[index] >= '0' && uuid[index] <= '9') || (uuid[index] >= 'a' && uuid[index] <= 'f') ||
                     (uuid[index] >= 'A' && uuid[index] <= 'F'))) throw std::runtime_error("UUID de revision no valido.");
    }
    (void)reader.number(8);
    if (reader.text(8192).empty()) throw std::runtime_error("Revision sin nombre.");
    std::set<std::string> paths;
    const auto files = reader.count();
    for (std::size_t index = 0; index < files; ++index) {
        const auto path = reader.text(4096);
        if (!project_path(path) || !paths.insert(path).second)
            throw std::runtime_error("Ruta de revision Android no valida.");
        (void)reader.text();
    }
    check_file_hierarchy(paths);
    const auto sources = reader.count(); std::set<std::string> selected;
    for (std::size_t index = 0; index < sources; ++index) {
        const auto path = reader.text(4096);
        if (!paths.contains(path) || !source_path(path) || !selected.insert(path).second) throw std::runtime_error("Fuente de revision no valida.");
    }
    const auto metadata = reader.count(); std::set<std::string> indentation;
    for (std::size_t index = 0; index < metadata; ++index) {
        const auto path = reader.text(4096);
        if (!paths.contains(path) || !indentation.insert(path).second) throw std::runtime_error("Sangria de revision no valida.");
        (void)reader.text();
    }
    if (!reader.done()) throw std::runtime_error("Revision con datos desconocidos.");
}
void write_synced(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    const auto handle = CreateFileW(native_io_path(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("No se pudo crear el archivo de importacion.");
    DWORD written = 0;
    const bool okay = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!okay) throw std::runtime_error("No se pudo confirmar la importacion en disco.");
}
void verify_import_archive(const std::filesystem::path& directory,const std::map<std::filesystem::path,std::string>& expected,
                           std::string_view manifest) {
    require_safe_path(directory);
    const auto marker = read_bytes(directory / ".import-manifest.txt");
    if (std::string_view(reinterpret_cast<const char*>(marker.data()),marker.size()) != manifest)
        throw std::runtime_error("La copia Android existente no pertenece a esta importacion; se conserva intacta.");
    std::set<std::filesystem::path> directories;
    for (const auto& [file,hash] : expected) {
        (void)hash;
        for (auto parent = file.parent_path(); !parent.empty(); parent = parent.parent_path()) directories.insert(parent);
    }
    std::size_t files = 0; const auto native_directory = native_io_path(directory);
    for (const auto& item : std::filesystem::recursive_directory_iterator(native_directory)) {
        require_safe_path(item.path()); const auto relative = item.path().lexically_relative(native_directory);
        if (relative == ".import-manifest.txt") continue;
        if (item.is_directory()) {
            if (!directories.contains(relative)) throw std::runtime_error("La copia Android contiene una carpeta ajena; no se reemplaza.");
            continue;
        }
        if (!item.is_regular_file() || !expected.contains(relative) || sha256(read_bytes(item.path())) != expected.at(relative))
            throw std::runtime_error("La copia Android existente cambio o contiene archivos ajenos; se conserva intacta.");
        ++files;
    }
    if (files != expected.size()) throw std::runtime_error("La copia Android existente esta incompleta; se conserva intacta.");
}
}

bool can_import_android_profile(const StateData& data) {
    for (const auto& [domain,values] : data.preferences) {
        if (domain != "native_windows") return false;
        for (const auto& [key,value] : values) {
            (void)value;
            if (!key.starts_with("catalog/") && key != "ui_language" && key != "content_language") return false;
        }
    }
    return true;
}

AndroidImportPreview inspect_android_export(const std::filesystem::path& folder) try {
    windows::ensure_winrt_apartment(); AndroidImportPreview preview;
    preview.source = std::filesystem::absolute(folder).lexically_normal(); require_safe_path(preview.source);
    if (!std::filesystem::is_directory(native_io_path(preview.source / "shared_prefs")))
        throw std::runtime_error("Selecciona una exportacion que contenga shared_prefs; no se extraen datos del telefono.");
    for (const auto& item : std::filesystem::directory_iterator(native_io_path(preview.source))) {
        const auto name = item.path().filename();
        if (name != "shared_prefs" && name != "files")
            throw std::runtime_error("La exportacion contiene una entrada no admitida: " + utf8(name.native()) +
                ". Falta un adaptador para esos datos; no se ha omitido ni importado nada.");
    }
    std::uintmax_t total = 0;
    for (const auto& subtree : {"shared_prefs", "files"}) {
        const auto root = preview.source / subtree;
        if (!std::filesystem::exists(native_io_path(root))) continue;
        for (const auto& item : std::filesystem::recursive_directory_iterator(native_io_path(root))) {
            require_safe_path(item.path());
            if (item.is_directory()) continue;
            if (!item.is_regular_file()) throw std::runtime_error("La exportacion contiene un archivo especial.");
            const auto relative = item.path().lexically_relative(native_io_path(preview.source));
            if (!safe_relative(relative)) throw std::runtime_error("Ruta fuera de la exportacion.");
            const auto bytes = read_bytes(item.path()); total += bytes.size();
            if (total > 512ULL * 1024 * 1024 || preview.files.size() >= 100000)
                throw std::runtime_error("La exportacion supera el limite de seguridad.");
            ImportFile file{relative, bytes.size(), sha256(bytes)};
            if (std::string(subtree) == "shared_prefs") {
                if (relative.parent_path() != "shared_prefs" || relative.extension() != ".xml")
                    throw std::runtime_error("Archivo SharedPreferences no compatible; la importacion no omite archivos silenciosamente.");
                if (relative.stem().native() == L"native_windows")
                    throw std::runtime_error("La exportacion incluye el dominio reservado de Windows native_windows; no es una exportacion Android compatible y no se ha importado nada.");
                auto preferences = parse_preferences(item.path());
                preview.preference_count += preferences.size();
                if (!preview.data.preferences.emplace(relative.stem().string(), std::move(preferences)).second)
                    throw std::runtime_error("Mapa de preferencias duplicado.");
            } else if (relative.extension() == ".revision" && relative.generic_string().starts_with("files/cpp_project_history/")) {
                validate_revision(file, bytes); ++preview.project_revisions;
            }
            preview.files.push_back(std::move(file));
        }
    }
    if (!preview.data.preferences.contains("learning_progress"))
        throw std::runtime_error("La exportacion no contiene learning_progress.xml; no se ha inventado progreso.");
    validate_json_versions(preview.data);
    preview.notes.push_back("Las preferencias y archivos se conservan con sus identidades originales; no se reutilizan veredictos de ejecucion.");
    preview.notes.push_back("Los proyectos e historiales se conservan y verifican; esta etapa no incorpora todavia un editor Windows de proyectos multiples.");
    return preview;
} catch (const winrt::hresult_error&) {
    throw std::runtime_error("La exportacion contiene JSON o tipos de borrador no compatibles; el origen permanece intacto.");
}

void commit_android_import(const AndroidImportPreview& preview, const std::filesystem::path& profile_root) {
    const auto root = std::filesystem::absolute(profile_root / "state").lexically_normal();
    require_safe_path(root);
    const auto source_text = preview.source.generic_wstring() + L"/";
    const auto target_text = root.generic_wstring() + L"/";
    const auto starts_with = [](const std::wstring& path, const std::wstring& prefix) {
        return path.size() >= prefix.size() && prefix.size() <= INT_MAX &&
            CompareStringOrdinal(path.data(), static_cast<int>(prefix.size()), prefix.data(),
                                 static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
    };
    if (starts_with(target_text, source_text) || starts_with(source_text, target_text))
        throw std::runtime_error("El origen de importacion debe estar separado del perfil Windows.");
    std::filesystem::create_directories(native_io_path(root));
    const auto lock = CreateFileW(native_io_path(root / "writer.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                                 nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE) throw std::runtime_error("Otra instancia esta guardando este perfil.");
    struct Lock { HANDLE handle; ~Lock() { CloseHandle(handle); } } held{lock};
    const auto active = root / "active";
    const bool existing_active = std::filesystem::exists(native_io_path(active));
    StateStore prior_store(profile_root); const auto prior = prior_store.load();
    if (!can_import_android_profile(prior)) throw std::runtime_error("El perfil ya contiene estado: no se reemplaza ni fusiona progreso real.");
    const auto prior_hash = existing_active ? sha256(read_bytes(active / "state.bin")) : std::string{};
    if (existing_active) for (const auto& entry : std::filesystem::directory_iterator(native_io_path(active))) {
        require_safe_path(entry.path()); const auto name = entry.path().filename();
        if (name != "state.bin" && name != "android")
            throw std::runtime_error("El perfil contiene archivos ajenos a la navegacion; no se reemplaza nada.");
    }
    const auto pending = root / (L"import-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) + L".pending");
    if (!std::filesystem::create_directory(native_io_path(pending))) throw std::runtime_error("No se pudo preparar la transaccion de importacion.");
    bool published_archive = false;
    std::map<std::filesystem::path,std::string> expected;
    std::string manifest = "LearnGameAndEngineDev.android-import.v1\n";
    try {
        // Fresh inspection catches source changes, including added or removed files, after confirmation.
        const auto checked = inspect_android_export(preview.source);
        if (checked.files.size() != preview.files.size() || checked.data != preview.data)
            throw std::runtime_error("El origen cambio desde la revision; vuelve a inspeccionarlo.");
        for (const auto& file : preview.files) expected[file.relative_path] = file.sha256;
        for (const auto& [file,hash] : expected) manifest += hash + " " + file.generic_string() + "\n";
        for (const auto& file : checked.files) {
            if (!expected.contains(file.relative_path) || expected.at(file.relative_path) != file.sha256)
                throw std::runtime_error("El origen cambio desde la revision.");
            const auto bytes = read_bytes(preview.source / file.relative_path);
            if (sha256(bytes) != file.sha256) throw std::runtime_error("El origen cambio durante la copia.");
            const auto destination = pending / "android" / file.relative_path;
            std::filesystem::create_directories(native_io_path(destination.parent_path())); write_synced(destination, bytes);
            if (sha256(read_bytes(destination)) != file.sha256) throw std::runtime_error("La copia no coincide con el origen.");
        }
        const auto last_check = inspect_android_export(preview.source);
        if (last_check.files.size() != expected.size() || last_check.data != checked.data)
            throw std::runtime_error("El origen cambio durante la transaccion.");
        for (const auto& file : last_check.files)
            if (!expected.contains(file.relative_path) || expected.at(file.relative_path) != file.sha256)
                throw std::runtime_error("El origen cambio durante la transaccion.");
        auto imported = checked.data;
        if (const auto native = prior.preferences.find("native_windows"); native != prior.preferences.end())
            imported.preferences.emplace("native_windows",native->second);
        auto bytes = encode(imported); const auto checksum = sha256(bytes);
        bytes.insert(bytes.end(), checksum.begin(), checksum.end()); write_synced(pending / "state.bin", bytes);
        write_synced(pending / "import-manifest.txt", std::span(reinterpret_cast<const std::uint8_t*>(manifest.data()), manifest.size()));
        if (!existing_active) {
            if (!MoveFileExW(native_io_path(pending).c_str(),native_io_path(active).c_str(),MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("No se pudo publicar la transaccion; el estado anterior permanece intacto.");
        } else {
            write_synced(pending / "android" / ".import-manifest.txt",std::span(reinterpret_cast<const std::uint8_t*>(manifest.data()),manifest.size()));
            verify_import_archive(pending / "android",expected,manifest);
            if (sha256(read_bytes(active / "state.bin")) != prior_hash)
                throw std::runtime_error("El perfil cambio durante la importacion; se conserva el estado anterior.");
            if (std::filesystem::exists(native_io_path(active / "android"))) {
                // A crash after archive publication, before the atomic state
                // replacement, leaves the original UI-only state intact. The
                // same verified export can resume without replacing files.
                verify_import_archive(active / "android",expected,manifest);
            } else {
                if (!MoveFileExW(native_io_path(pending / "android").c_str(),native_io_path(active / "android").c_str(),MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("No se pudo publicar el archivo Android; el estado anterior permanece intacto.");
                published_archive = true;
            }
            // Files are already complete and durable. Replacing this one file
            // is the commit point; no old directory or backup is created.
            if (!ReplaceFileW(native_io_path(active / "state.bin").c_str(),native_io_path(pending / "state.bin").c_str(),nullptr,
                REPLACEFILE_IGNORE_MERGE_ERRORS,nullptr,nullptr))
                throw std::runtime_error("No se pudo publicar el estado importado; la navegacion anterior permanece intacta.");
            published_archive = false;
            std::error_code error; std::filesystem::remove_all(native_io_path(pending),error);
        }
    } catch (...) {
        if (published_archive) {
            // Only discard a directory created by this invocation, and only
            // if every original copied byte is still demonstrably ours.
            try {
                if (sha256(read_bytes(active / "state.bin")) == prior_hash) {
                    verify_import_archive(active / "android",expected,manifest);
                    std::filesystem::remove_all(native_io_path(active / "android"));
                }
            }
            catch (...) { /* Changed or unverified archive is preserved for explicit recovery. */ }
        }
        // Only this invocation's unpublished transaction is disposable; source and active data are untouched.
        std::error_code error; std::filesystem::remove_all(native_io_path(pending), error); throw;
    }
}
} // namespace lgaed::state

#include "platform/windows/content_loader.hpp"
#include "platform/windows/bibliography_revision.hpp"
#include "core/code_format_compat.hpp"
#include "core/identifier_rename_compat.hpp"
#include "core/cmake_selective_recovery.hpp"
#include "core/named_rule_battery_check.hpp"
#include "core/named_rule_constant_variation_check.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "platform/windows/state_store.hpp"

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
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

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
bool authored_nonblank(std::string_view source) {
    // Python's curricular str.strip() additionally treats NEL U+0085 as
    // whitespace. Keep all authenticated public-prose paths consistent with
    // the authoring validator, without changing legacy narration trimming.
    const auto text=key(source);
    return std::ranges::any_of(std::wstring_view(text.c_str(),text.size()),
        [](wchar_t c){return c!=0x85&&!narration_space(c);});
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
// Full block digests, never a blanket exemption for mathematical labels.
// Keep these closed reviews in sync with stable_state_layout.py.
struct ReviewedMathLabel {
    std::string_view lesson,identity,puzzle,before,after,label_before;
    unsigned source_index;
};
const std::array<ReviewedMathLabel,2>& reviewed_math_labels(){
    static constexpr std::array<ReviewedMathLabel,2> values{{
        {"cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos","block_0007_0001",
            "cpp_fundamentals__datos_propios_y_estado_valido__construir_y_comprobar_un_modelo_de_pedidos__reglas_de_los_caminos_publicos",
            "ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d","20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b",
            "Construcción que termina normalmente",0},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas","block_0008_0002",
            "cpp_fundamentals__texto_interpretacion_y_flujos__interprete_historial__casos_y_contratos",
            "fed5b6d2dcfb3321715725fe61a366c8c1f337d1b24a7e3e0b1fd43ef1be0549","d2ec5c530dba31ba0c4d2b3575d609bfbd688fca0dbb4d94a0fd1524a9803187",
            "100+1;",5}
    }};return values;
}
const ReviewedMathLabel* reviewed_math_label(const JsonObject& block){
    if(text(block,"type")!="math_puzzle")return nullptr;
    const auto puzzle=optional_text(block,"id");
    for(const auto& spec:reviewed_math_labels())if(puzzle==spec.puzzle&&fingerprint(block)==spec.after)return &spec;
    return nullptr;
}
void reject_unmapped_reviewed_math(const JsonArray& pages) {
    for (const auto& page : pages) for (const auto& item : array(page.GetObject(),"blocks")) {
        const auto block = item.GetObject();
        // Unmapped math may contain exact fractional/large source numbers;
        // avoid applying the integer-only state-layout digest to those puzzles.
        require(reviewed_math_label(block)==nullptr,
            "The reviewed mathematical label requires its published v2 identity and proof");
    }
}
struct ReviewedCompileCheck {
    std::string_view lesson, identity, diagnostic, source_sha256, payload_sha256;
    bool project=false;
};
const std::array<ReviewedCompileCheck,17>& reviewed_compile_checks() {
    static constexpr std::array<ReviewedCompileCheck,17> values{{
        {"cpp_fundamentals__datos_propios_y_estado_valido__consultas_const_y_objetos_constantes","block_0006_0006","const-member-call",
            "1274ec3ea9af32c3e1bd1476e05fccff6ce78ca9eea984bc646a976d0d01d448","05cdb3e534649bd7e3efd1e080e5199443deace1e074d56d45490a8ff32c4c27"},
        {"cpp_fundamentals__datos_propios_y_estado_valido__valores_por_defecto_con_significado","block_0012_0008","explicit-copy-initialization",
            "d00845dfe9b5e26b22ea330e60ffa4c688f62600e270ba8025f193f385d1ff8e","ac0365d7452a3a6ff52a0f6c306d01815a09196f9f49e2bc90cdd4ec5dbc9fe9"},
        {"cpp_fundamentals__datos_propios_y_estado_valido__valores_por_defecto_con_significado","block_0012_0011","explicit-argument-conversion",
            "22e81e9eecd45003f9d5c7d533f6a20417259ffbc0f053e01fae970c651673ef","9f7db3f3a1004a235926bc12e06e9d0bf1a93b707f3ca92f6de2876677fd9c58"},
        {"cpp_fundamentals__datos_propios_y_estado_valido__valores_por_defecto_con_significado","block_0014_0011","explicit-assignment",
            "3fa4838a63863d92e57912b0f3101d423f523040155672447af08608c126bed5","3e8eb728eac0d493c864596ec2364fe6c69ff85ce01408dd0020cec36ad6b087"},
        {"cpp_fundamentals__datos_propios_y_estado_valido__argumentos_con_tipos_que_expresan_su_funcion","block_0006_0006","swapped-named-arguments",
            "f84caa30d3a6dd9cdcc741adbadba33f4e139416608247ab650cbd6eff02cd53","b35436bcbac5b5e98731d953fb50499d5e7d3ad66ed1ad88771d56e481a8c9ed"},
        {"cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas","block_0014_0002","u6-difference-unselected-definition",
            "fe06539284d12f8621a06e997ef68e3696a3c79ea66b9bd62a60ea442bf12f34","d17f1d73d7217ea45e1468f6ed0d1a5ef3565ecce33410a8022660baff950eba",true},
        {"cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas","block_0015_0002","u6-difference-conflicting-return",
            "fe06539284d12f8621a06e997ef68e3696a3c79ea66b9bd62a60ea442bf12f34","e5b83315920efce2c1a6092721096ef596e2c58d7c96089ba01075bceb3986df",true},
        {"cpp_fundamentals__funciones_y_contratos__declaraciones_y_firmas","block_0017_0002","u6-difference-missing-header",
            "46aef238e19c467244140d20a5169315e0266b9e8412c3953ce2399e331c2e35","3d0f8c6e8fc84ec145f1c9a6c79ec7d5fbe50d8a70294c9f6effc0b4d8eb755c",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0016_0002","u6-envelopes-missing-header",
            "0ed979b41f9fe2022c1202639331d46bb4a67fdef289d8bb7be131ed4c261857","ae23ef7cc266649cbad08efc8736916121dc404e132520a0c386ea73b23d7782",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0017_0002","u6-envelopes-missing-declaration",
            "6a8d64c9ffaf614e1f13f6a723d88fa4737c6b45809838f0d93d15f261d91b0a","1c69caf83414219854952076e951e9adb3f9b6168503f60113a2d858103ef650",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0018_0002","u6-envelopes-unselected-definitions",
            "6a8d64c9ffaf614e1f13f6a723d88fa4737c6b45809838f0d93d15f261d91b0a","b263e83bb0b912281964eb60261d33713e9cb6d5df046b375f5f008544c4a405",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0019_0002","u6-envelopes-duplicate-definition",
            "6a8d64c9ffaf614e1f13f6a723d88fa4737c6b45809838f0d93d15f261d91b0a","72ac435d8a7399bd39c91be5a72ae2034aeb9653a8a78ec45fa3f82e6ac9470e",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0021_0002","u6-series-unknown-call",
            "11b327722be0f31efe23e497b396443ce4134a689d28e676b9bfc160c310eefa","b84a7b928030171e03178f3300a1fd3dbbd86388ff998fcd8ddea9d247f9effe",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0022_0002","u6-series-unselected-definitions",
            "983e8b50cbc94c3838497f74e4e9cca7740f49c1c9b0f396c9afbc0632ffbe05","157dc0f18f12db33cc3a7787009c408bb067ea833544df42ebdc76e2e2c00e34",true},
        {"cpp_fundamentals__funciones_y_contratos__interfaces_cabeceras_y_programas_con_varios_archivos","block_0023_0002","u6-series-duplicate-definition",
            "983e8b50cbc94c3838497f74e4e9cca7740f49c1c9b0f396c9afbc0632ffbe05","26416e82df9c1bd708b840b6d940c79db644f69bcd2acecc56f595f23cf48d20",true},
        {"cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz","block_0013_0002","u6-shelf-unselected-definition",
            "b922498caa08e5092c39a8594628eca5e02b8931110113069fe53f140d072511","2c568ff3ab456b8111cb9a338fc9c0d6f9ccff45a1e1cd3f840cee872900f7e3",true},
        {"cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz","block_0014_0002","u6-shelf-conflicting-return",
            "b922498caa08e5092c39a8594628eca5e02b8931110113069fe53f140d072511","5b1315f399da68b066291bf96a7f1ed8cfd1aadf3964b2d2ca55cd1f6bae50ec",true}
    }};return values;
}
const ReviewedCompileCheck* reviewed_compile_check(std::string_view diagnostic) {
    const auto& values=reviewed_compile_checks();
    const auto found=std::ranges::find(values,diagnostic,&ReviewedCompileCheck::diagnostic);
    return found==values.end()?nullptr:&*found;
}
CompileCheck read_compile_check(const JsonObject& source) {
    allowed_keys(source,{"diagnostic","explanation"});
    CompileCheck result{text(source,"diagnostic"),optional_text(source,"explanation")};
    require(reviewed_compile_check(result.diagnostic)!=nullptr,"Unknown reviewed compilation diagnostic");
    if(source.HasKey(L"explanation")){const auto wording=key(result.explanation);require(std::ranges::any_of(wording,[](wchar_t c){return !narration_space(c);}),"Blank compilation diagnostic explanation");}
    return result;
}
void validate_compile_check_payload(const JsonObject& source) {
    require(text(source,"type")=="cpp_compiler","A compilation diagnostic belongs to a compiler");
    for(const auto field:{L"outputCheck",L"cmake",L"initialStandardInput"})
        require(!source.HasKey(field),"A reviewed compilation diagnostic has no runtime objective, CMake or input");
    const auto check=read_compile_check(source.GetNamedObject(L"compileCheck"));
    const auto spec=reviewed_compile_check(check.diagnostic);
    if(spec->project)require(source.HasKey(L"projectFiles")&&source.HasKey(L"sourceFiles"),
        "A reviewed project diagnostic retains its complete project and source selection");
    else require(!source.HasKey(L"projectFiles")&&!source.HasKey(L"sourceFiles"),
        "A reviewed single-source diagnostic cannot gain a project or source selection");
    const auto original=JsonObject::Parse(source.Stringify());original.Remove(L"compileCheck");
    require(sha256(text(original,"initialCode"))==spec->source_sha256&&fingerprint(original)==spec->payload_sha256,
        "A reviewed compilation diagnostic cannot alter its initial source or original block");
}
struct ReviewedIntentionalExit {std::string_view lesson,identity,before,output;};
constexpr std::array reviewed_intentional_exits{
    ReviewedIntentionalExit{"cpp_fundamentals__archivos_y_programa_completo__compartir_flujos_sin_acoplar_el_calculo",
        "block_0005_0002","d263ac4c841168cb7723bdc2daaeaf6ddc839c4830f17a26f6166ff919fb252c","Entrada invalida|9:4"},
    ReviewedIntentionalExit{"cpp_fundamentals__archivos_y_programa_completo__construir_un_programa_de_carga_y_guardado",
        "block_0004_0017","6f796c5c55683ac266ae3f83c54f935273c341c34da087161276c7e361524ae8","Error al abrir salida"}
};
const ReviewedIntentionalExit* reviewed_intentional_exit_spec(const JsonObject& block) {
    if(text(block,"type")!="cpp_compiler"||!block.HasKey(L"outputCheck"))return nullptr;
    const auto original=JsonObject::Parse(block.Stringify());original.Remove(L"outputCheck");
    const auto before=fingerprint(original);
    const auto spec=std::ranges::find(reviewed_intentional_exits,before,&ReviewedIntentionalExit::before);
    if(spec==reviewed_intentional_exits.end())return nullptr;
    // These are closed adoptions of exact exploratory editors, never a
    // general exemption for adding/changing output contracts in old state maps.
    const auto config=block.GetNamedObject(L"outputCheck");allowed_keys(config,{"cases"},true);
    const auto cases=array(config,"cases");require(cases.Size()==1,"The reviewed intentional-exit editor requires exactly one output case");
    const auto item=cases.GetObjectAt(0);allowed_keys(item,{"standardInput","outputs","exitCode","explanation"},true);
    const auto outputs=strings(array(item,"outputs"),true);
    require(text(item,"standardInput").empty()&&outputs==std::vector<std::string>{std::string(spec->output)}
        &&integer(item.GetNamedValue(L"exitCode"),255)==1,
        "The reviewed editor requires exact stdout without a final newline, empty input and return 1");
    const auto wording=key(text(item,"explanation"));
    require(std::ranges::any_of(wording,[](wchar_t c){return !narration_space(c);}),"The reviewed intentional-exit goal requires an explanation");
    return &*spec;
}
bool reviewed_intentional_exit_goal(const JsonObject& block) {
    return reviewed_intentional_exit_spec(block)!=nullptr;
}
constexpr std::string_view crt_output_lesson="cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia";
constexpr std::string_view crt_output_identity="block_0005_0004";
constexpr std::string_view crt_output_before="b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35";
constexpr std::string_view crt_output_after="53648380e15d1148a779de47c0f94dc08f63e2319f79bdc08e72d25a6992ed31";
JsonObject without_compiler_guidance(const JsonObject& block) {
    const auto result=JsonObject::Parse(block.Stringify());
    if(result.HasKey(L"compileCheck")&&result.GetNamedValue(L"compileCheck").ValueType()==JsonValueType::Object){
        const auto config=result.GetNamedObject(L"compileCheck");if(config.HasKey(L"explanation"))config.Remove(L"explanation");
    }
    if(result.HasKey(L"outputCheck")&&result.GetNamedValue(L"outputCheck").ValueType()==JsonValueType::Object){
        const auto config=result.GetNamedObject(L"outputCheck");
        if(config.HasKey(L"cases")&&config.GetNamedValue(L"cases").ValueType()==JsonValueType::Array)
            for(const auto& value:config.GetNamedArray(L"cases"))if(value.ValueType()==JsonValueType::Object){const auto item=value.GetObject();
                if(item.HasKey(L"explanation"))item.Remove(L"explanation");if(item.HasKey(L"reviewFeedback"))item.Remove(L"reviewFeedback");}
    }
    return result;
}
std::optional<std::string> reviewed_crt_output_original(const JsonObject& block) {
    if(text(block,"type")!="cpp_compiler"||fingerprint(without_compiler_guidance(block))!=crt_output_after)return {};
    const auto cases=array(block.GetNamedObject(L"outputCheck"),"cases");
    require(cases.Size()==1,"The reviewed CRT spelling needs its original single case");
    const auto item=cases.GetObjectAt(0);const auto outputs=strings(array(item,"outputs"),true);
    require(text(item,"standardInput").empty()&&outputs.size()==2,"The reviewed CRT spelling needs its original empty input and two outputs");
    const auto& original=outputs.front();std::string expanded;
    for(const char character:original){if(character=='\n')expanded+='\r';expanded+=character;}
    require(original.size()==802&&std::ranges::count(original,'\n')==42&&std::ranges::count(original,'\r')==3
        &&outputs.back()==expanded&&expanded.size()==844,
        "The reviewed CRT spelling must expand LF only and retain every original control");
    return original;
}
// A format archive is a complete published block. Never normalize every
// stored hash: project only the explicitly verified source fields back onto
// the original bytes before checking their old signatures.
JsonObject source_format_projection(const JsonObject& active,const JsonObject& snapshot) {
    require(text(active,"type")==text(snapshot,"type"),"A format archive changed its activity type");
    auto projected=JsonObject::Parse(active.Stringify());
    const auto preserve=[&](const JsonObject& old,const JsonObject& current,
            const JsonObject& target,const wchar_t* name,curriculum::code_format::Language language) {
        require(old.HasKey(name)==current.HasKey(name),"Source formatting changed an authored field");
        if(!old.HasKey(name))return;
        const auto before=text(old,utf8(winrt::hstring(name)));
        const auto after=text(current,utf8(winrt::hstring(name)));
        require(curriculum::code_format::equivalent(before,after,language),
            "Source formatting modified tokens, literals, comments or preprocessor lines");
        target.Insert(name,old.GetNamedValue(name));
    };
    // Examples and ordering puzzles can be authored explicitly as CMake.
    // Do not reinterpret their exact CMake syntax as C++ for source revisions.
    const auto authored_language=optional_text(active,"language");
    const auto language=authored_language=="cmake"?curriculum::code_format::Language::cmake:
        authored_language.empty()||authored_language=="cpp"||authored_language=="c++"?
            curriculum::code_format::Language::cpp:curriculum::code_format::Language::immutable;
    preserve(snapshot,active,projected,L"code",language);
    if(text(active,"type")=="cpp_compiler") {
        preserve(snapshot,active,projected,L"initialCode",curriculum::code_format::Language::cpp);
        require(snapshot.HasKey(L"projectFiles")==active.HasKey(L"projectFiles"),
            "Source formatting changed project file ownership");
        if(snapshot.HasKey(L"projectFiles")) {
            const auto originals=array(snapshot,"projectFiles"),current=array(active,"projectFiles");
            auto replacing=projected.GetNamedArray(L"projectFiles");
            require(originals.Size()==current.Size(),"Source formatting changed the file list");
            for(unsigned i=0;i<originals.Size();++i) {
                const auto before=originals.GetObjectAt(i),after=current.GetObjectAt(i);
                const auto path=text(before,"path");
                require(path==text(after,"path"),"Source formatting renamed a project file");
                const auto cmake=path=="CMakeLists.txt"||std::string_view(path).ends_with(".cmake");
                const auto dot=path.rfind('.');
                constexpr std::array<std::string_view,10> source_extensions{
                    ".cpp",".cc",".cxx",".c",".h",".hpp",".hh",".hxx",".inl",".ixx"};
                const auto cpp=dot!=path.npos&&std::ranges::find(
                    source_extensions,std::string_view(path).substr(dot))!=source_extensions.end();
                // Do not silently edit arbitrary project data files.
                const auto language=cmake?curriculum::code_format::Language::cmake:
                    cpp?curriculum::code_format::Language::cpp:curriculum::code_format::Language::immutable;
                preserve(before,after,replacing.GetObjectAt(i),L"initialCode",language);
            }
        }
    }
    if(text(active,"type")=="inline_fill_code"||text(active,"type")=="drag_fill_code")
        preserve(snapshot,active,projected,L"template",language);
    for(const auto field:{L"fragments",L"choices"})if(
            (std::wstring_view(field)==L"fragments"&&text(active,"type")=="order_code")||
            (std::wstring_view(field)==L"choices"&&text(active,"type")=="drag_fill_code")) {
        require(snapshot.HasKey(field)==active.HasKey(field),"Source formatting changed a puzzle source list");
        if(!snapshot.HasKey(field))continue;
        const auto old=array(snapshot,utf8(winrt::hstring(field)));
        const auto now=array(active,utf8(winrt::hstring(field)));
        require(old.Size()==now.Size(),"Source formatting changed the number of graded fragments");
        for(unsigned i=0;i<old.Size();++i) {
            require(old.GetAt(i).ValueType()==JsonValueType::String&&now.GetAt(i).ValueType()==JsonValueType::String&&
                curriculum::code_format::equivalent(utf8(old.GetStringAt(i)),utf8(now.GetStringAt(i)),
                    language),
                "A formatted fragment changed its original code tokens or answer slot");
        }
        projected.Insert(field,snapshot.GetNamedValue(field));
    }
    return projected;
}

// Identifier-only revisions own a *different* permission from source spacing.
// Reuse the native production lexer to extract exactly the authored C++ slots;
// preserve all other JSON fields, file paths, answers and objective metadata.
curriculum::code_format::Language identifier_slot_language(std::string language) {
    while(!language.empty()&&std::isspace(static_cast<unsigned char>(language.front())))
        language.erase(language.begin());
    while(!language.empty()&&std::isspace(static_cast<unsigned char>(language.back())))
        language.pop_back();
    for(auto& c:language)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(language.empty()||language=="cpp"||language=="c++"||language=="cxx"||
       language=="c"||language=="h"||language=="hpp")
        return curriculum::code_format::Language::cpp;
    if(language=="cmake")return curriculum::code_format::Language::cmake;
    return curriculum::code_format::Language::immutable;
}
curriculum::code_format::Language identifier_file_language(std::string path) {
    const auto slash=path.find_last_of("/\\");
    const auto basename=slash==path.npos?path:path.substr(slash+1);
    if(basename=="CMakeLists.txt"||std::string_view(path).ends_with(".cmake"))
        return curriculum::code_format::Language::cmake;
    const auto dot=path.find_last_of('.');
    if(dot==path.npos)return curriculum::code_format::Language::immutable;
    auto ext=path.substr(dot);
    for(auto& c:ext)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    constexpr std::array<std::string_view,10> cpp{
        ".cpp",".cc",".cxx",".c",".h",".hpp",".hh",".hxx",".inl",".ixx"};
    return std::ranges::find(cpp,ext)!=cpp.end()?
        curriculum::code_format::Language::cpp:
        curriculum::code_format::Language::immutable;
}
std::vector<curriculum::identifier_rename::SourceSlot>
    identifier_code_slots(const JsonObject& block) {
    using namespace curriculum::identifier_rename;
    std::vector<SourceSlot> out;
    const auto type=text(block,"type");
    const auto language=identifier_slot_language(optional_text(block,"language"));
    if(block.HasKey(L"code"))
        out.push_back({"code",language,text(block,"code")});
    if((type=="inline_fill_code"||type=="drag_fill_code")&&
       block.HasKey(L"template"))
        out.push_back({"template",language,text(block,"template")});
    if(type=="cpp_compiler") {
        if(block.HasKey(L"initialCode"))
            out.push_back({"initialCode",curriculum::code_format::Language::cpp,
                           text(block,"initialCode")});
        if(block.HasKey(L"projectFiles")) {
            const auto files=array(block,"projectFiles");
            for(unsigned i=0;i<files.Size();++i) {
                const auto file=files.GetObjectAt(i);
                out.push_back({"projectFiles/"+std::to_string(i)+"/initialCode",
                               identifier_file_language(text(file,"path")),
                               text(file,"initialCode")});
            }
        }
    }
    if(type=="order_code"&&block.HasKey(L"fragments")) {
        const auto fragments=array(block,"fragments");
        for(unsigned i=0;i<fragments.Size();++i)
            out.push_back({"fragments/"+std::to_string(i),language,
                           utf8(fragments.GetStringAt(i))});
    }
    if(type=="drag_fill_code"&&block.HasKey(L"choices")) {
        const auto choices=array(block,"choices");
        for(unsigned i=0;i<choices.Size();++i)
            out.push_back({"choices/"+std::to_string(i),language,
                           utf8(choices.GetStringAt(i))});
    }
    return out;
}
JsonObject identifier_restore_source(const JsonObject& active,const JsonObject& prior) {
    auto result=JsonObject::Parse(active.Stringify());
    for(const auto& entry:identifier_code_slots(prior)) {
        const auto slash=entry.id.find('/');
        const auto value=JsonValue::CreateStringValue(key(entry.source));
        if(slash==std::string::npos) {
            result.Insert(key(entry.id),value);
            continue;
        }
        const auto ending=entry.id.find('/',slash+1);
        const auto index=static_cast<unsigned>(std::stoul(
            entry.id.substr(slash+1,ending==entry.id.npos?
                std::string::npos:ending-slash-1)));
        if(ending==entry.id.npos)result.GetNamedArray(key(entry.id.substr(0,slash))).SetAt(index,value);
        else result.GetNamedArray(L"projectFiles").GetObjectAt(index).Insert(
            L"initialCode",value);
    }
    return result;
}

// Source formatting can accompany a question correction or an optional
// ordinary explanation introduced for the first time. A changed passage
// always requires its own authored semantic descriptor. Unchanged prose is
// not granted new reading credit through this source-format allowance.
void require_formatted_question_semantics(const JsonObject& root,
        std::string_view identity,const JsonObject& original,const JsonObject& current) {
    require(original.HasKey(L"question")==current.HasKey(L"question"),
        "Source formatting cannot add or remove a published question");
    const auto changed_question=original.HasKey(L"question")&&
        text(original,"question")!=text(current,"question");
    const auto type=text(current,"type");
    const bool ordinary=type=="multiple_choice"||type=="fill_code"||
        type=="output_prediction"||type=="order_code"||
        type=="drag_fill_code"||type=="inline_fill_code";
    const bool old_explanation=original.HasKey(L"explanation");
    const bool new_explanation=current.HasKey(L"explanation");
    const bool changed_explanation=ordinary&&(
        old_explanation!=new_explanation||
        (old_explanation&&text(original,"explanation")!=text(current,"explanation")));
    if(!changed_question&&!changed_explanation)return;

    // The same Unicode-aware authored-text rule applies to questions and
    // newly added explanations, including NEL and other space characters.
    if(changed_question)
        require(authored_nonblank(text(original,"question"))&&
                authored_nonblank(text(current,"question")),
            "Combined source/question revision needs nonblank public wording");
    if(changed_explanation)
        require(new_explanation&&(!old_explanation||
                    authored_nonblank(text(original,"explanation")))&&
                authored_nonblank(text(current,"explanation")),
            "Combined source/explanation revision needs nonblank authored prose");

    require(root.HasKey(L"textSemantics"),
        "Formatted prose needs an explicit semantic descriptor");
    const auto semantics=root.GetNamedObject(L"textSemantics");
    const auto assert_mark=[&](std::string_view path,std::string_view source){
        std::size_t count=0;
        for(const auto& value:array(semantics,"fields")) {
            const auto field=value.GetObject();
            if(text(field,"block")!=identity||text(field,"path")!=path)continue;
            ++count;
            require(text(field,"sourceSha256")==sha256(source),
                "Formatted prose has a stale authored semantic source");
        }
        require(count==1,"Formatted prose needs exactly one authored source marker");
    };
    if(changed_question)assert_mark("/question",text(current,"question"));
    if(changed_explanation)assert_mark("/explanation",text(current,"explanation"));
}
std::string state_fingerprint(const JsonObject& original) {
    const auto value = JsonObject::Parse(original.Stringify());
    const auto type = text(value, "type");
    if(value.HasKey(L"compileCheck")) {
        validate_compile_check_payload(value);
        value.Remove(L"compileCheck");
    }
    if(reviewed_intentional_exit_goal(value))value.Remove(L"outputCheck");
    if(reviewed_crt_output_original(value)) {
        value.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0).GetNamedArray(L"outputs").RemoveAtEnd();
        require(fingerprint(without_compiler_guidance(value))==crt_output_before,"The reviewed CRT spelling lost its exact complete original block");
    }
    if(type=="image"){
        value.Remove(L"alt");if(value.HasKey(L"caption"))value.Remove(L"caption");
    } else if (type == "heading" || type == "paragraph" || type == "note") {
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
            require(!(object.HasKey(L"standardError") && object.HasKey(L"standardErrorExact")),
                "Conflicting runtime stderr objectives");
            if (object.HasKey(L"standardError")) {
                require(text(object,"standardError") == "nonBlank", "Unknown runtime stderr expectation");
                object.Remove(L"standardError");
            }
            if (object.HasKey(L"standardErrorExact")) {
                (void)text(object,"standardErrorExact");
                object.Remove(L"standardErrorExact");
            }
        }
    } else if (const auto reviewed=reviewed_math_label(value);type == "math_puzzle"&&reviewed) {
        value.GetNamedArray(L"responses").GetAt(0).GetObject().GetNamedArray(L"sources").GetAt(reviewed->source_index).GetObject()
            .Insert(L"label", JsonValue::CreateStringValue(winrt::to_hstring(reviewed->label_before)));
        require(fingerprint(value) == reviewed->before, "Invalid reviewed mathematical label");
    }
    return fingerprint(value);
}
// A reviewed option is an editorial text revision, not a new answer. Project
// only that option back to the full archived predecessor before comparing the
// immutable historical response signature. Other option texts, the correct
// index, question, feedback and every unlisted property remain unchanged.
JsonObject general_choice_projection(const JsonObject& current,
        const JsonObject& revision,bool allow_original=false) {
    const auto original=revision.GetNamedObject(L"block");
    require(text(original,"type")=="multiple_choice"&&
            text(current,"type")=="multiple_choice",
        "A general choice archive must contain its original choice owner");
    const auto earlier=array(original,"options");
    const auto now=array(current,"options");
    require(earlier.Size()>0&&earlier.Size()==now.Size(),
        "A general choice review changed the number of answers");
    const auto position=integer(revision.GetNamedValue(L"optionIndex"),
        static_cast<std::size_t>(earlier.Size()-1));
    const auto replacement=text(revision,"replacement");
    // A choice correction is another authored public-text surface.
    require(authored_nonblank(replacement)&&
            replacement!=utf8(earlier.GetStringAt(static_cast<unsigned>(position))),
        "A general choice review must revise one nonblank option");
    if(fingerprint(current)==fingerprint(original)) {
        require(allow_original,
            "A general choice revision has no active wording change");
        return JsonObject::Parse(original.Stringify());
    }
    require(utf8(now.GetStringAt(static_cast<unsigned>(position)))==replacement,
        "A general choice review does not match its authored replacement");
    const auto projected=JsonObject::Parse(current.Stringify());
    projected.GetNamedArray(L"options").SetAt(static_cast<unsigned>(position),
        earlier.GetAt(static_cast<unsigned>(position)));
    require(fingerprint(projected)==fingerprint(original),
        "A general choice revision changed another answer, explanation or field");
    return projected;
}
// Authenticate the previous public-text marker, not only the option bytes.
// The root overlay is outside statePayload; without this archive, a choice
// could silently alter old narration offsets while preserving its answer.
std::optional<TextSemantics> validate_general_choice_semantic_archive(
        const JsonObject& root,const JsonObject& revision) {
    require(revision.HasKey(L"semanticBefore"),
        "A general choice revision must archive its previous semantic marker");
    const auto original=revision.GetNamedObject(L"block");
    const auto options=array(original,"options");
    require(options.Size()>0,"A semantic choice archive has no original options");
    const auto at=integer(revision.GetNamedValue(L"optionIndex"),
        static_cast<std::size_t>(options.Size()-1));
    const auto owner=text(revision,"id");
    const auto path="/options/"+std::to_string(at);

    // The source and UTF-16 selection belong to the ORIGINAL or the NEW
    // wording explicitly, never to a different option or its adjacent prose.
    const auto check_marker=[&](const JsonObject& field,std::string_view source) {
        allowed_keys(field,{"block","path","sourceSha256","spans"},true);
        require(text(field,"block")==owner&&text(field,"path")==path&&
                text(field,"sourceSha256")==sha256(source),
            "A choice semantic marker has the wrong option owner or source");
        TextSemantics semantics;
        semantics.source=std::string(source);
        for(const auto& value:array(field,"spans")) {
            const auto span=value.GetObject();
            allowed_keys(span,{"start","end","text","role","reading"});
            SemanticSpan part;
            part.start=integer(span.GetNamedValue(L"start"),16*1024*1024);
            part.end=integer(span.GetNamedValue(L"end"),16*1024*1024);
            part.text=text(span,"text");
            part.role=parse_semantic_role(text(span,"role"));
            if(span.HasKey(L"reading"))
                part.reading=parse_semantic_reading(text(span,"reading"));
            semantics.spans.push_back(std::move(part));
        }
        validate_text_semantics(semantics);
        return semantics;
    };
    const auto archived_value=revision.GetNamedValue(L"semanticBefore");
    std::optional<JsonObject> archived;
    std::optional<TextSemantics> previous_semantic;
    if(archived_value.ValueType()!=JsonValueType::Null) {
        require(archived_value.ValueType()==JsonValueType::Object,
            "The previous semantic marker must be an object or null");
        archived=archived_value.GetObject();
        previous_semantic=check_marker(*archived,
            utf8(options.GetStringAt(static_cast<unsigned>(at))));
    }

    std::optional<JsonObject> active;
    if(root.HasKey(L"textSemantics")) {
        const auto metadata=root.GetNamedObject(L"textSemantics");
        for(const auto& item:array(metadata,"fields")) {
            const auto field=item.GetObject();
            if(text(field,"block")!=owner||text(field,"path")!=path)continue;
            require(!active,"Duplicate semantic marker for a revised option");
            active=field;
        }
    }
    if(revision.HasKey(L"semanticAfter")) {
        require(revision.HasKey(L"semanticRootBefore")&&
                revision.GetNamedValue(L"semanticRootBefore").ValueType()==JsonValueType::Boolean,
            "An explicit choice revision must declare its previous semantic root");
        const bool old_root=revision.GetNamedValue(L"semanticRootBefore").GetBoolean();
        require(old_root||!archived,
            "A choice without a previous semantic root cannot archive an old marker");
        require(revision.GetNamedValue(L"semanticAfter").ValueType()==JsonValueType::Object,
            "An explicit choice descriptor must be an object");
        const auto authored=revision.GetNamedObject(L"semanticAfter");
        const auto authored_semantic=check_marker(authored,text(revision,"replacement"));
        require(!previous_semantic||previous_semantic->spans.empty()||!authored_semantic.spans.empty(),
            "A marked choice cannot silently lose all its highlights");
        require(active&&fingerprint(*active)==fingerprint(authored),
            "The active choice semantic marker differs from its authenticated revision");
    }else{
        require(!revision.HasKey(L"semanticRootBefore"),
            "The legacy choice revision cannot invent a semantic-root flag");
        require(static_cast<bool>(archived)==static_cast<bool>(active),
            "A revised option invented or discarded its original semantic marker");
        if(active) {
            auto expected=JsonObject::Parse(archived->Stringify());
            expected.Insert(L"sourceSha256",JsonValue::CreateStringValue(
                key(sha256(text(revision,"replacement")))));
            require(fingerprint(*active)==fingerprint(expected),
                "A general choice revision altered a historic semantic span");
        }
    }
    return previous_semantic;
}

struct ReviewedChoice {
    std::string_view lesson,identity,before_payload,after_payload,before_full,first_after_full;
    std::string_view paired_explanation_before,paired_explanation_after;
};
const std::vector<ReviewedChoice>& reviewed_choices() {
    static const std::vector<ReviewedChoice> values{
        {"cpp_fundamentals__texto_interpretacion_y_flujos__estados_del_flujo_y_datos_validos","block_0006_0004",
            "5393e161f7486d6803e37a6ce4b3cb9ca5fb832d458b7e3303e3477d462d1b25","e817c3fea65911ada1efdb18b7784a76b63d4b5a915530b56ff79a041a1960d6",
            "ccba0a5461601602d2c74520a20b3b800156e23c4156c6f9edb613908d4c3a5f","0853764d61cd61df3f1ab10f310ff237a8ee4b057151d5c37e99490963589d3d"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__validar_una_operacion_y_recuperar_la_siguiente","block_0005_0004",
            "ddc59680893eb3049921f89156aabbf9cbda4dd5ff60d45e51d1dc03e4068272","8db059b96aad937bf75def2d3d18181620bd2fd2215f7d01d923a7a02ec15b74",
            "c694da695d8cb9b889ece1b22570c0e25cfcb37c004a3b22d71f3862a61d373f","5dfe0ec554c75abe71df341c5fda4a0c68fa41747bb2d9bbefb753ef0f636713"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_lector_de_entrada_preparada","block_0000_0004",
            "6cf40a01fa8a87c4bfcf577dd41fe2e071a0219a26a7571c2d081677d911a260","2bcddeb382c97368786dc91558d6b53de5a2bbf5320ba1672a919fc95e973f9f",
            "1588a86d210ef1c95b08fee6120684afc51caa303e932519d401b13981eb65dc","35964088aa38c6231c8febb7b1a00da3a8389e4f1605112af426d03cde09d360"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__tokens_con_categoria_y_valor","block_0003_0002",
            "1a1a494aa8c49c0255c0c8634e52835d84f6d5c29a2eb533ed257c13ef18b816","b4e00e593e89935b313392a17d7983b2998e64905c37f38c2a18e3e837c699f8",
            "9b28a5432859a50e1d336098022614188ad75f81eb7cb2db87284b98838efca1","4d598fee502105e520f800f975a57486947851153339a448ea92cdff768ac400"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__tokens_con_categoria_y_valor","block_0006_0001",
            "2c62ea997577dd3e617968b33746ed53576100e3ae0a16b220cdb9f15b59a917","b3a836a39bae7b92db9b357b530681316c69af866e35c3b5f8b7190eeb026bf3",
            "8f36e0a7b384b20953b45f76011c01c1fab1766eb641ff11ce7f4c94c62e5a91","123f9105afc6bd9cab76c21cde2af2d83b4004a89ec91e8593573ae8fe4dd4e1"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__diagnosticar_y_recuperar_hasta_fin_de_operacion","block_0007_0001",
            "8b90e1ce9f7e75fbc2dab47db07e49977857ae41e648abf8d0d03957e03ac021","9f4dcd56c9d45f79d3aa799b7a29ac7b68ee97797d89e32f68d4152ef963223c",
            "1a4a51d3a8dcc2c9ace27e6d9422d8fff1d2fe5acb7e7da49eed6b4efe6a52f6","571e7f17ffd3bf84f8a15c500503af9cfeceed04f8e6a23738c9ed2856a4f1ba"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__diagnosticar_y_recuperar_hasta_fin_de_operacion","block_0003_0004",
            "2c203b9684c188f398b35cbcdea5e1ef47001f7c76a84b94df1ff3e5be410863","996400efadbb06a1a18764ee4306065a0d7f2697af3cd863493970744a0b53ce",
            "a2b0f46e6254179eaf9d67047e4c5c6c1dd7acd1807e260a923a24bc44accc9a","cb07aff65f16ac14875e50631d73cd447262afbc2a033cafa9bf5958fefcc8a2",
            "El ciclo miró una unidad sólo para distinguir final de una operación disponible. La devuelve porque pertenece a la regla que va a interpretar la expresión.",
            "El ciclo leyó un token para distinguir el final del texto de la presencia de datos por analizar. Lo devuelve para que la gramática pueda consumirlo al interpretar la operación."},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__una_tabla_persistente_de_nombres","block_0000_0003",
            "defd38f10fdaf2b5a1da80501b346a0383ec537ed1e884fd5be5e07bf37339f0","faef1ddda48892fff0bba2e8eccb4de2dc0df0204db7ca5b82137c7d56d78bb7",
            "673e20034fb06a10a562cfc9c6517d0f980803d7101c53e0a7d9b0efa603817e","701e1b22c8409d748ac500845fced4328b27b02c16fd90e6e7b164a7400012d4"}
    };
    return values;
}
const ReviewedChoice* reviewed_choice_owner(std::string_view lesson,std::string_view identity) {
    const auto& values=reviewed_choices();
    const auto found=std::ranges::find_if(values,[&](const auto& item){return item.lesson==lesson&&item.identity==identity;});
    return found==values.end()?nullptr:&*found;
}
void validate_choice_pair(const JsonObject& block,const ReviewedChoice* reviewed) {
    if(!reviewed||reviewed->paired_explanation_before.empty())return;
    const auto digest=state_fingerprint(block);
    if(digest==reviewed->before_payload||digest==reviewed->after_payload)
        require(text(block,"explanation")==
            (digest==reviewed->after_payload?reviewed->paired_explanation_after:reviewed->paired_explanation_before),
            "The reviewed token option and explanation must belong to the same exact version");
}
bool reviewed_choice_payload(std::string_view digest) {
    return std::ranges::any_of(reviewed_choices(),[&](const auto& item){return item.after_payload==digest;});
}
void reject_unmapped_reviewed_choices(const JsonArray& pages) {
    for(const auto& page:pages)for(const auto& item:array(page.GetObject(),"blocks")) {
        const auto block=item.GetObject();
        require(text(block,"type")!="multiple_choice"||!reviewed_choice_payload(state_fingerprint(block)),
            "A reviewed option revision needs its published v2 snapshot and proof");
    }
}
struct ReviewedCompiler {
    std::string_view lesson,identity,before,after;
    std::string_view reference_identity,reference_before,reference_after;
    std::string_view starter_append;
};
const std::vector<ReviewedCompiler>& reviewed_compilers() {
    static const std::vector<ReviewedCompiler> values{
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_lector_de_entrada_preparada","block_0003_0005",
            "0ff28cd1a7cb92334d0a2486d5fcd773c7727813c84a082f8908950a6a3399c6","e3d81fc9c5d397dfdd1be6df7271b34cba21d56a696cce43ca5ad1c8e5ae088a"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__tokens_con_categoria_y_valor","block_0004_0004",
            "a0633caa9e65905eb3216a603cc7bfcb23b766fcad3715756d47c08796f2940e","0800175e415321a20eb95403a67778d11549225a67ba9732cc9358a031138629",
            "block_0007_0001","ddd301c6c0a3c53e70ac18b616b2a3a1aae584590cf7b9e632d550088f6bde5a","4d2480ce9aa48f34eb3b1777ab5d0ad88524436faa22cf666a0abb3c010edb46"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__numeros_de_varios_digitos_y_limites","block_0004_0003",
            "c28f8e645fd47269c44c418648e69b765ff614441a5cb4c2e3723e6ac29c7e10","27698af9c281a19311b4c1c070cc49a86bf68223d85175aeff0bae4aaffc3d7a",
            "block_0008_0001","1b9b8dc7b0973c441205dcff6182317983b4dfa6865f15738fa20d61bc165055","10f38c37b8a211edd53a6a9bb059296360bd2c2fe5c11d865a8b48d29d8b5c07"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__una_gramatica_para_sumar_y_restar","block_0004_0003",
            "3e793a0dcd1ed13fd57e97c68f4a22f960585f9acf8611c7aa09927a5c8404f2","55b38dc4d94d9cea6ed446040c7e043dde447be7a18ead7735df182de91cee01",
            "block_0008_0001","05a7b84c5fe6eecd11e75fb9548d9051cc7575dbe421894fdb9ef3cab207256d","eee52db854d07ecc2e3319731c18c07940ed543f9836b722ba85ea5e790cc5e4"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__terminos_prioridad_y_division_segura","block_0004_0003",
            "9bddda66a2a7024ef1280fc82a82e7ad59cd189a2b3089138b5f06c06de78f1d","dcd52cc803827c671d2d783d6af8e423e79cb8ef7377a365ff5afbcec05a926e",
            "block_0008_0001","44f26849307e799bc3263578b3353b4979218eae494c39a906c2fe09bafb2cb5","a396321ffa32d8e9c4ca4cc1df1f01cf8e8acef762295d2ec6a8e8077460074a"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__signos_unarios_parentesis_y_progreso_recursivo","block_0004_0004",
            "ffef99d8c0504d333a81d68da83eb473974d43ba573626a4808eab6015bddd30","efcd239dc6e8f02d04a418f2b81e0c18e54e8125c7dc0c6dcac57e45abc57300",
            "block_0009_0001","70662e3574fbc6db855250d8a9579466ef5827e0ea768477076290e6191b577b","44dc46d8aace0591bb180e11022c9a3b171c20e4b792fdc91da672a6618c0a44"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__diagnosticar_y_recuperar_hasta_fin_de_operacion","block_0004_0004",
            "dc68860e953dd941d4495f68d295b6a7cbca70fe286ee356675f405bb8bd2502","cf3c9ab1f070195689b4f2bce4bd635ae8368f760384663a2ebecbb5a1f7bfdd",
            "block_0008_0001","61c61981819c377ca9bc8194d889e9f8abfff6d1b572da2ac96a74b8658aa7e6","6a9952bd00bb00a74a3b51fba27757940cadcb72708a0ea3af5e8447660f18d6"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas","block_0002_0002",
            "ebf1067d19fbf87bce3ddef2bae69442972df180055d5572faef4473eb76c34e","7b30da24db7834b2b8cc9ca2014b05de9dfeea69637764e8a9834dbe90b69f5b",
            "block_0009_0001","778473599281a1b2165b4dabb9acedde00904b55d9b907c8dedeb9831de20618","96bcd1d3821c7e80908dd178e855705206d366ead6fbe1831fc891a85969087b"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete","block_0003_0004",
            "c03fef34aa19969bac10860c7229545551733638be93dcc84104a56e61227b22","247c41d8494fe203bcc83908dacb2c4e563ae6959e81438452991a30c31c561b",
            "block_0006_0001","14fa456ea18c3478fb9fb2476d70f337bdab5ee3cfad5d6b3224b07bc36db0b2","1e16cb31b5885fa0ae2f3faa6ffd2687391d088f304a035a7cd124278316d2e2"},
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_un_interprete_con_historial_y_pruebas","block_0006_0003",
            "b7e77211fde9c243eee599d2711c8cf0cb2f11fbb47de347de298147188a2e47","7b9c4229709dc9caf07a9c0bd56bf496e62b0148195f69797290cd8ac63be83f",
            "block_0010_0001","9f88cd2b376b8b869b2fa3dd6fb569fce823ce3deae64c138a0f9a2c643fcccc","80ee77eccf110d4438d2643a1dd2c2cb9be696d5f50e57ef285b162e37a0a8a1",
            "    std::cout << \"Caso 5\\n\";\n    probar(\"2;2;\");\n"}
    };return values;
}
const ReviewedCompiler* reviewed_compiler_owner(std::string_view lesson,std::string_view identity) {
    const auto& values=reviewed_compilers();const auto found=std::ranges::find_if(values,[&](const auto& item){return item.lesson==lesson&&item.identity==identity;});
    return found==values.end()?nullptr:&*found;
}
bool reviewed_compiler_payload(std::string_view digest) {
    return std::ranges::any_of(reviewed_compilers(),[&](const auto& item){return item.after==digest;});
}
bool reviewed_compiler_reference_payload(std::string_view digest) {
    return std::ranges::any_of(reviewed_compilers(),[&](const auto& item){return !item.reference_after.empty()&&item.reference_after==digest;});
}
void reject_unmapped_compiler_revisions(const JsonArray& pages) {
    for(const auto& page:pages)for(const auto& item:array(page.GetObject(),"blocks")) {
        const auto block=item.GetObject();require(!reviewed_intentional_exit_goal(block),
            "The reviewed intentional-exit goal needs its original Spanish v2 identity and proof");
        require(!reviewed_crt_output_original(block),"The reviewed CRT spelling needs its exact Spanish v2 owner and editorial proof");
        require(text(block,"type")!="cpp_compiler"||!reviewed_compiler_payload(state_fingerprint(block)),
            "A reviewed compiler expansion needs its published v2 snapshot and proof");
        require(text(block,"type")!="code"||!reviewed_compiler_reference_payload(state_fingerprint(block)),
            "An expanded reference needs its paired reviewed compiler and original code archive");
    }
}
struct ExpandedOrder {
    OrderRefinement record;
    Block block;
};
void validate_order_line_semantics(const OrderRefinement& record) {
    std::string program;
    bool first = true;
    for (const auto index : record.original_correct_order) {
        if (!first) program += '\n';
        first = false; program += record.original_fragments[index];
    }
    // Translation-phase splicing and multiline raw-literal bodies give physical
    // blank lines semantic meaning. Keep their grouped source until a dedicated
    // representation can preserve those separators in every arrangement.
    require(program.find("\\\n") == program.npos && program.find("\\\r\n") == program.npos,
        "Ordering line continuations need a semantic-preserving refinement contract");
    std::size_t cursor = 0;
    while ((cursor = program.find("R\"",cursor)) != program.npos) {
        const auto opening = std::string_view(program).substr(cursor + 2,17).find('(');
        const auto open = opening == std::string_view::npos ? program.npos : cursor + 2 + opening;
        const auto delimiter = std::string_view(program).substr(cursor + 2,
            (open == program.npos ? program.size() : open) - cursor - 2);
        const bool valid = open != program.npos && delimiter.size() <= 16 &&
            std::ranges::all_of(delimiter,[](unsigned char character) {
                return character >= 33 && character <= 126 && character != '(' && character != ')' && character != '\\';
            });
        if (!valid) { cursor += 2; continue; }
        const auto terminator = ")" + std::string(delimiter) + '"';
        const auto closing = program.find(terminator,open + 1);
        require(closing != program.npos, "An unterminated raw literal cannot be refined into source lines");
        require(std::string_view(program).substr(cursor,closing-cursor).find('\n') == std::string_view::npos,
            "Multiline raw literals need a semantic-preserving refinement contract");
        cursor = closing + terminator.size();
    }
}
ExpandedOrder expand_order(const JsonObject& snapshot, std::string_view identity) {
    require(text(snapshot,"type") == "order_code", "An order refinement needs its original ordering block");
    ExpandedOrder result;
    auto& record = result.record;
    record.id = identity; record.raw_json = utf8(snapshot.Stringify()); record.sha256 = fingerprint(snapshot);
    record.original_fragments = strings(array(snapshot,"fragments"),true);
    require(record.original_fragments.size() <= 100000, "Too many original order fragments");
    record.groups.resize(record.original_fragments.size());
    std::set<std::size_t> seen;
    for (const auto& item : array(snapshot,"correctOrder")) {
        const auto index = integer(item);
        require(index < record.original_fragments.size() && seen.insert(index).second, "Invalid original order permutation");
        record.original_correct_order.push_back(index);
    }
    require(seen.size() == record.original_fragments.size(), "The original ordering needs every fragment");
    validate_order_line_semantics(record);
    result.block.type = BlockType::order_code;
    const auto append = [&](std::size_t group, std::string_view line) {
        require(line.find('\r') == line.npos, "Unsupported bare carriage return in order fragments");
        if (line.find_first_not_of(" \t") == line.npos) return;
        require(result.block.fragments.size() < 100000, "Too many refined order lines");
        record.groups[group].push_back(result.block.fragments.size());
        result.block.fragments.emplace_back(line);
    };
    for (std::size_t group = 0; group < record.original_fragments.size(); ++group) {
        const auto& fragment = record.original_fragments[group];
        // This reviewed compact statement has no original LF. Keep every byte,
        // including the spaces starting the assignment and final closing line.
        if (fragment == "if (validar(candidata)) { tabla = candidata; }") {
            append(group,"if (validar(candidata)) {");
            append(group," tabla = candidata;");
            append(group," }");
            continue;
        }
        std::size_t start = 0;
        while (start < fragment.size()) {
            const auto end = fragment.find('\n',start);
            auto line = std::string_view(fragment).substr(start,(end == fragment.npos ? fragment.size() : end) - start);
            if (end != fragment.npos && !line.empty() && line.back() == '\r') line.remove_suffix(1);
            append(group,line);
            if (end == fragment.npos) break;
            start = end + 1;
        }
        require(!record.groups[group].empty(), "An original fragment must contain a nonempty code line");
    }
    require(!result.block.fragments.empty(), "An ordering refinement cannot contain only blank lines");
    for (const auto group : record.original_correct_order)
        result.block.correct_order.insert(result.block.correct_order.end(),record.groups[group].begin(),record.groups[group].end());
    require(result.block.fragments != record.original_fragments || result.block.correct_order != record.original_correct_order,
        "An ordering refinement must actually split its original representation");
    record.representation_signature = sha256(order_representation_frame(result.block));
    return result;
}
JsonObject expanded_order_json(const JsonObject& snapshot, const Block& expanded) {
    auto value = JsonObject::Parse(snapshot.Stringify());
    JsonArray fragments, order;
    for (const auto& fragment : expanded.fragments) fragments.Append(JsonValue::CreateStringValue(key(fragment)));
    for (const auto index : expanded.correct_order) order.Append(JsonValue::CreateNumberValue(static_cast<double>(index)));
    value.Insert(L"fragments",fragments); value.Insert(L"correctOrder",order);
    return value;
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
    // Preserve evaluation object order and exact numeric tokens. Decoded
    // strings and insignificant whitespace may differ; only the explicit
    // root prompt slice is excluded from this protected contract.
    [[nodiscard]] std::string ordered_frame(Slice item,const std::vector<Slice>& ignored={}) const {
        auto cursor=item.begin;std::string result;ordered_value(cursor,result,ignored,0);
        require(cursor==item.end,"Incomplete ordered JSON contract");return result;
    }
private:
    std::string_view source_;
    std::string ordered_string(std::size_t& cursor,std::string& output) const {
        const auto end=string_end(cursor);
        const auto parsed=JsonValue::Parse(key(source_.substr(cursor,end-cursor))).GetString();
        for(decltype(parsed.size()) at=0;at<parsed.size();++at){const auto c=parsed[at];
            if(c>=0xd800&&c<=0xdbff){require(at+1<parsed.size()&&parsed[at+1]>=0xdc00&&parsed[at+1]<=0xdfff,
                "Invalid Unicode scalar in mathematical contract");++at;}
            else require(c<0xdc00||c>0xdfff,"Invalid Unicode scalar in mathematical contract");}
        canonical(JsonValue::CreateStringValue(parsed),output);cursor=end;return utf8(parsed);
    }
    void ordered_value(std::size_t& cursor,std::string& output,const std::vector<Slice>& ignored,unsigned depth) const {
        require(depth<=64,"JSON nesting too deep");space(cursor);
        const auto omitted=std::ranges::find(ignored,cursor,&Slice::begin);
        if(omitted!=ignored.end()){require(value(cursor)==omitted->end,"Invalid archived text slice");
            output+='p';cursor=omitted->end;return;}
        const auto first=source_.at(cursor);
        if(first=='"'){(void)ordered_string(cursor,output);return;}
        if(first=='{'){
            ++cursor;output+='{';std::set<std::string> names;space(cursor);
            if(source_.at(cursor)!='}')for(;;){
                space(cursor);const auto name=ordered_string(cursor,output);
                require(names.insert(name).second,"Duplicate mathematical JSON property");space(cursor);
                require(source_.at(cursor++)==':',"Expected mathematical JSON property");
                ordered_value(cursor,output,ignored,depth+1);space(cursor);
                if(source_.at(cursor)=='}')break;
                require(source_.at(cursor++)==',',"Expected mathematical object separator");
            }
            ++cursor;output+='}';return;
        }
        if(first=='['){
            ++cursor;output+='[';space(cursor);
            if(source_.at(cursor)!=']')for(;;){
                ordered_value(cursor,output,ignored,depth+1);space(cursor);
                if(source_.at(cursor)==']')break;
                require(source_.at(cursor++)==',',"Expected mathematical array separator");
            }
            ++cursor;output+=']';return;
        }
        const auto end=value(cursor);const auto token=source_.substr(cursor,end-cursor);
        const auto parsed=JsonValue::Parse(key(token));
        if(parsed.ValueType()==JsonValueType::Number){require(std::isfinite(parsed.GetNumber()),"Nonfinite mathematical number");
            output+='n'+std::to_string(token.size())+':';output+=token;}
        else canonical(parsed,output);
        cursor=end;
    }
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
std::optional<CourseEdition> read_course_edition(
        const JsonObject& root, std::string_view locale, std::string_view source) {
    if (!root.HasKey(L"courseEdition")) return {};
    const OriginalJson original(source);
    const auto descriptor = original.member(original.root(), "courseEdition");
    require_integer_json_numbers(original.text(descriptor));
    const auto object = root.GetNamedObject(L"courseEdition");
    allowed_keys(object, {"version", "courseId", "locale", "id", "generation"}, true);
    for (const auto field : {"version", "courseId", "locale", "id", "generation"})
        (void)original.member(descriptor, field); // Reject duplicated known properties.
    require(integer(object.GetNamedValue(L"version"), 1) == 1, "Unsupported course edition version");
    CourseEdition edition{text(object, "courseId"), text(object, "locale"), text(object, "id"),
        integer(object.GetNamedValue(L"generation"), 1000000)};
    validate_course_edition(edition);
    require(edition.locale == locale, "Course edition belongs to another asset language");
    return edition;
}
void bind_course_edition(Lesson& lesson) {
    if (!lesson.course_edition) {
        require(!uses_course_edition_namespace(lesson.id),
            "A reserved course-edition identity requires its descriptor");
        return;
    }
    const auto& edition = *lesson.course_edition;
    require(course_edition_lesson_id(edition, lesson.id),
        "Lesson identity does not belong to its course edition");
    auto& layout = lesson.state_layout;
    require(layout.version == 2 && layout.locale == edition.locale &&
            layout.family.size() == 1 && layout.family.contains(edition.locale),
        "A course edition requires its own single-language state layout v2");
    // A descriptor cannot be changed under already stored state, even when the
    // authored display label changes without changing its generation namespace.
    layout.lineage = sha256(course_edition_frame(edition) + layout.lineage);
}
struct EditionIndex {
    JsonObject object;
    std::string source;
};
EditionIndex read_edition_index(const std::filesystem::path& path) {
    std::ifstream input(state::native_io_path(path), std::ios::binary);
    require(input.good(), "Cannot open the active course-edition index");
    std::string source{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad() && source.size() <= 8 * 1024 * 1024,
        "Cannot read the active course-edition index");
    if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3);
    return {JsonObject::Parse(key(source)), std::move(source)};
}
void require_physical_course_edition_asset(
        const std::filesystem::path& content_root,
        const std::filesystem::path& resource) {
    // The publication gate checks physical files from the repository root.
    // At runtime the content root is the trustworthy boundary we can derive
    // from an authored lesson. Never follow a link/junction below that root.
    // Normalize the full requested paths without resolving filesystem
    // aliases. An alias above 'content' must not disappear via canonical().
    const auto root = std::filesystem::absolute(content_root).lexically_normal();
    const auto target = std::filesystem::absolute(resource).lexically_normal();
    const auto relative = target.lexically_relative(root);
    require(!relative.empty() && !relative.is_absolute() &&
            relative != std::filesystem::path(L"."),
        "A course-edition resource must be inside its content directory");
    for (const auto& component : relative)
        require(component != L"." && component != L"..",
            "A course-edition resource cannot escape its content directory");

    const auto physical = [](const std::filesystem::path& current, bool directory) {
        // Other native content/state I/O supports extended Windows paths.
        // Keep the same spelling for this metadata check: otherwise an
        // ordinary installed course can be rejected past MAX_PATH.
        const auto flags = GetFileAttributesW(state::native_io_path(current).c_str());
        if (flags == INVALID_FILE_ATTRIBUTES) {
            const auto reason = GetLastError();
            // Report only the failing path component, not a full user/QA
            // directory. This distinguishes a missing file from a malformed
            // root spelling without exposing a personal profile path.
            throw std::runtime_error(
                "A course-edition resource or directory is unavailable (Win32 " +
                std::to_string(reason) + ", component " +
                utf8(winrt::hstring(current.filename().wstring())) + ")");
        }
        require((flags & FILE_ATTRIBUTE_REPARSE_POINT) == 0,
            "A course-edition resource cannot use a symbolic link or junction");
        require(((flags & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory,
            "A course-edition resource has an unexpected file type");
    };
    // The catalog sometimes passes a native_io_path() spelling, such as
    // \\?\C:\... . std::filesystem::parent_path() of that spelling can
    // yield a root-name-only "C:" component, which GetFileAttributesW
    // rejects with Win32 ERROR_INVALID_PARAMETER (87). Use the already
    // established state path guard for the ancestors: it checks reparse
    // points without mistaking a root-name spelling for a directory.
    // Our stricter physical() check below still requires the content root
    // and each indexed resource to exist with the correct file type.
    state::require_safe_path(root);
    physical(root, true);
    auto current = root;
    auto iterator = relative.begin();
    for (; iterator != relative.end(); ++iterator) {
        current /= *iterator;
        const auto next = std::next(iterator);
        physical(current, next != relative.end());
    }
}
void validate_course_edition_file(const std::filesystem::path& path, const Lesson& lesson) {
    // Legacy translated assets retain their historical per-file ES fallback,
    // including existing files outside the new Spanish edition's active index.
    if (!lesson.course_edition) return;
    try {
        const auto& edition = *lesson.course_edition;
        const auto unit_folder = path.parent_path();
        const auto course_folder = unit_folder.parent_path();
        const auto unit_slug = unit_folder.filename().string();
        const auto unit_id = edition.course_id + "__" + unit_slug;
        require(course_folder.filename().string() == edition.course_id &&
                course_folder.parent_path().parent_path().filename().string() == edition.locale &&
                (course_folder.parent_path().filename() == L"programming" ||
                 course_folder.parent_path().filename() == L"math_physics") &&
                course_edition_unit_id(edition, unit_id),
            "Course-edition lesson is outside its declared course, language or unit");
        require(lesson.id.starts_with(unit_id + "__") &&
                path.filename().string() == "lesson_" + lesson.id.substr(unit_id.size() + 2) + ".json",
            "Course-edition lesson file does not match its published identity");
        const auto root = course_folder.parent_path().parent_path().parent_path();
        const auto course_path = course_folder / ("course_" + edition.course_id + ".json");
        const auto unit_path = unit_folder / ("unit_" + unit_slug + ".json");
        require_physical_course_edition_asset(root, path);
        require_physical_course_edition_asset(root, course_path);
        require_physical_course_edition_asset(root, unit_path);
        const auto course = read_edition_index(course_path);
        require(text(course.object, "id") == edition.course_id &&
                read_course_edition(course.object, edition.locale, course.source) == lesson.course_edition,
            "Lesson edition differs from its active course index");
        std::set<std::string> units;
        for (const auto& item : array(course.object, "units")) {
            const auto id = text(item.GetObject(), "id");
            require(course_edition_unit_id(edition, id) && units.insert(id).second,
                "Course edition contains a mixed or duplicated unit identity");
        }
        require(units.contains(unit_id), "Course-edition unit is not active in its course index");
        const auto unit = read_edition_index(unit_path);
        require(text(unit.object, "courseId") == edition.course_id && text(unit.object, "unitId") == unit_id &&
                read_course_edition(unit.object, edition.locale, unit.source) == lesson.course_edition,
            "Lesson edition differs from its active unit index");
        std::set<std::string> lessons;
        for (const auto& item : array(unit.object, "lessons")) {
            const auto id = text(item.GetObject(), "id");
            require(course_edition_lesson_id(edition, id) && id.starts_with(unit_id + "__") &&
                    lessons.insert(id).second,
                "Course edition contains a mixed or duplicated lesson identity");
        }
        require(lessons.contains(lesson.id), "Course-edition lesson is not active in its unit index");
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Course edition index JSON/contract error: " + utf8(error.message()));
    }
}

void require_math_prompt_contract(std::string_view current,std::string_view previous,bool historical=false) {
    const OriginalJson live(current),original(previous);
    const auto live_root=live.root(),old_root=original.root();
    std::vector<OriginalJson::Slice> live_ignored{live.member(live_root,"prompt")},old_ignored{original.member(old_root,"prompt")};
    // A snapshot can precede one of the two already published label reviews.
    // Only that exact full before/after pair may omit its reviewed label too;
    // arbitrary historical labels remain part of the ordered contract.
    if(historical)if(const auto reviewed=reviewed_math_label(JsonObject::Parse(key(previous)));
            reviewed&&fingerprint(JsonObject::Parse(key(current)))==reviewed->before){
        const auto label=[&](const OriginalJson& source,OriginalJson::Slice root){
            const auto response=source.elements(source.member(root,"responses")).at(0);
            const auto item=source.elements(source.member(response,"sources")).at(reviewed->source_index);
            return source.member(item,"label");
        };
        live_ignored.push_back(label(live,live_root));old_ignored.push_back(label(original,old_root));
    }
    require(live.ordered_frame(live_root,live_ignored)==original.ordered_frame(old_root,old_ignored),
        "A mathematical prompt revision changed evaluation data, property order or numeric tokens");
}
SimulationSpec read_simulation(const JsonObject& block) {
    allowed_keys(block,{"type","id","title","description","simulation"});
    // Validate the original UTF-16 units before WinRT can replace an unpaired
    // surrogate during UTF-8 conversion. The core must see the authored data.
    const auto strict_utf8=[](const winrt::hstring& authored) {
        for(decltype(authored.size()) at=0;at<authored.size();++at) {
            const auto unit=authored[at];
            if(unit>=0xd800&&unit<=0xdbff) {
                require(at+1<authored.size()&&authored[at+1]>=0xdc00&&authored[at+1]<=0xdfff,
                    "A simulation string contains an invalid Unicode scalar");
                ++at;
            }else require(unit<0xdc00||unit>0xdfff,
                "A simulation string contains an invalid Unicode scalar");
        }
        return utf8(authored);
    };
    const auto strict_text=[&](const JsonObject& object,std::string_view name) {
        const auto property=object.GetNamedValue(key(name));
        require(property.ValueType()==JsonValueType::String,"Expected a simulation string");
        return strict_utf8(property.GetString());
    };
    SimulationSpec spec;
    spec.title=strict_text(block,"title");spec.description=strict_text(block,"description");
    if(block.HasKey(L"id")) {
        const auto id=strict_text(block,"id");const auto visible=key(id);
        require(id.size()<=128&&id.find('\0')==id.npos&&
                std::ranges::any_of(visible,[](wchar_t c){return c!=0x85&&!narration_space(c);}),
            "A simulation block id must be nonblank bounded text");
    }
    const auto value=block.GetNamedObject(L"simulation");
    allowed_keys(value,{"version","model","view","valueType","values","sort",
        "editable","labels","explanations","outputTemplate","scenarios"});
    require(integer(value.GetNamedValue(L"version"),1)==1,
        "Unsupported simulation version");
    spec.model=strict_text(value,"model");
    const std::map<std::string,SimulationViewMode> modes{
        {"array",SimulationViewMode::array},{"groups",SimulationViewMode::groups},
        {"walkthrough",SimulationViewMode::walkthrough},
        {"candidates",SimulationViewMode::candidates},{"summary",SimulationViewMode::summary}};
    const auto mode=strict_text(value,"view");
    require(modes.contains(mode),"Unknown simulation presentation");
    spec.view=modes.at(mode);
    const auto kind=strict_text(value,"valueType");
    require(kind=="integer"||kind=="text","Unknown simulation value type");
    spec.text_values=kind=="text";
    spec.sort=value.GetNamedBoolean(L"sort");
    spec.editable=value.GetNamedBoolean(L"editable");
    const auto read_values=[&](const JsonArray& entries) {
        require(entries.Size()<=64,"A simulation accepts at most 64 values");
        std::vector<SimulationValue> result;
        for(const auto& entry:entries) {
            if(spec.text_values) {
                require(entry.ValueType()==JsonValueType::String,
                    "A text simulation requires string values");
                result.emplace_back(strict_utf8(entry.GetString()));
            }else{
                require(entry.ValueType()==JsonValueType::Number,
                    "An integer simulation requires numeric values");
                const auto number=entry.GetNumber();
                require(std::isfinite(number)&&std::trunc(number)==number&&
                        number>=-2147483648.0&&number<=2147483647.0,
                    "A simulation integer must fit in signed 32 bits");
                result.emplace_back(static_cast<std::int64_t>(number));
            }
        }
        return result;
    };
    spec.values=read_values(array(value,"values"));
    const auto labels=value.GetNamedObject(L"labels");
    allowed_keys(labels,{"array","start","end","frequency","maximum","leaders","output"},true);
    spec.labels={strict_text(labels,"array"),strict_text(labels,"start"),strict_text(labels,"end"),
        strict_text(labels,"frequency"),strict_text(labels,"maximum"),strict_text(labels,"leaders"),strict_text(labels,"output")};
    const auto explanations=value.GetNamedObject(L"explanations");
    allowed_keys(explanations,{"initial","start","advance","newMaximum","tie","belowMaximum","done","empty"},true);
    spec.explanations={strict_text(explanations,"initial"),strict_text(explanations,"start"),
        strict_text(explanations,"advance"),strict_text(explanations,"newMaximum"),
        strict_text(explanations,"tie"),strict_text(explanations,"belowMaximum"),
        strict_text(explanations,"done"),strict_text(explanations,"empty")};
    spec.output_template=strict_text(value,"outputTemplate");
    if(value.HasKey(L"scenarios")) {
        const auto scenarios=array(value,"scenarios");
        require(scenarios.Size()<=8,"A simulation accepts at most eight scenarios");
        for(const auto& entry:scenarios) {
            const auto scenario=entry.GetObject();
            allowed_keys(scenario,{"id","label","values"},true);
            spec.scenarios.push_back({strict_text(scenario,"id"),strict_text(scenario,"label"),
                read_values(array(scenario,"values"))});
        }
    }
    validate_simulation_spec(spec);
    return spec;
}
Block read_block(const JsonObject& value,std::string_view original,
    const JsonObject* authenticated_source_before=nullptr);
std::string bibliography_string(const winrt::hstring& value){
    for(decltype(value.size()) at=0;at<value.size();++at){const auto c=value[at];
        if(c>=0xd800&&c<=0xdbff){require(at+1<value.size()&&value[at+1]>=0xdc00&&value[at+1]<=0xdfff,
            "Bibliographic source contains an invalid Unicode scalar");++at;}
        else require(c<0xdc00||c>0xdfff,"Bibliographic source contains an invalid Unicode scalar");}
    return utf8(value);
}
std::optional<TextSemantics> bibliography_semantics(const IJsonValue& value,std::string_view owner,const JsonObject& block){
    if(value.ValueType()==JsonValueType::Null)return {};
    const auto field=value.GetObject();allowed_keys(field,{"block","path","sourceSha256","spans"},true);
    require(text(field,"block")==owner&&text(field,"path")=="/explanation","Bibliographic semantics has a different field owner");
    TextSemantics result;result.source=bibliography_string(block.GetNamedString(L"explanation"));
    const auto digest=text(field,"sourceSha256");require(valid_hash(digest)&&sha256(result.source)==digest,"Stale bibliographic source semantics");
    const auto spans=array(field,"spans");require(spans.Size()<=100000,"Too many bibliographic semantic spans");
    for(const auto& item:spans){const auto span=item.GetObject();allowed_keys(span,{"start","end","text","role","reading"});
        SemanticSpan selected;selected.start=integer(span.GetNamedValue(L"start"),16*1024*1024);selected.end=integer(span.GetNamedValue(L"end"),16*1024*1024);
        selected.text=bibliography_string(span.GetNamedString(L"text"));selected.role=parse_semantic_role(text(span,"role"));
        if(span.HasKey(L"reading"))selected.reading=parse_semantic_reading(text(span,"reading"));result.spans.push_back(std::move(selected));}
    validate_text_semantics(result);return result;
}
JsonObject math_prompt_projection(const JsonObject& current,const JsonObject& previous,bool historical=false) {
    const auto prompt=[](const JsonObject& block){
        require(text(block,"type")=="math_puzzle"&&block.HasKey(L"prompt")&&!block.HasKey(L"question"),
            "A mathematical text archive requires the original explicit prompt field");
        const auto authored=block.GetNamedValue(L"prompt");require(authored.ValueType()==JsonValueType::String,"A mathematical prompt must be text");
        const auto value=authored.GetString();require(std::ranges::any_of(value,[](wchar_t c){return !narration_space(c);}),
            "A mathematical prompt cannot be blank");return value;
    };
    (void)prompt(current);const auto before=prompt(previous);
    if(historical)if(const auto reviewed=reviewed_math_label(previous);
            reviewed&&fingerprint(current)==reviewed->before)return previous;
    const auto projected=JsonObject::Parse(current.Stringify());
    projected.Insert(L"prompt",JsonValue::CreateStringValue(before));
    require(fingerprint(projected)==fingerprint(previous),"A mathematical prompt revision changed another field");
    return projected;
}
std::optional<TextSemantics> math_prompt_semantics(const IJsonValue& value,std::string_view owner,const JsonObject& block) {
    if(value.ValueType()==JsonValueType::Null)return {};
    const auto field=value.GetObject();allowed_keys(field,{"block","path","sourceSha256","spans"},true);
    require(text(field,"block")==owner&&text(field,"path")=="/prompt","Archived mathematical semantics has a different owner or field");
    TextSemantics result;result.source=bibliography_string(block.GetNamedString(L"prompt"));
    const auto digest=text(field,"sourceSha256");require(valid_hash(digest)&&sha256(result.source)==digest,"Stale archived mathematical prompt semantics");
    const auto spans=array(field,"spans");require(spans.Size()<=100000,"Too many archived mathematical semantic spans");
    for(const auto& value:spans){const auto span=value.GetObject();allowed_keys(span,{"start","end","text","role","reading"});
        SemanticSpan item;item.start=integer(span.GetNamedValue(L"start"),16*1024*1024);item.end=integer(span.GetNamedValue(L"end"),16*1024*1024);
        item.text=bibliography_string(span.GetNamedString(L"text"));item.role=parse_semantic_role(text(span,"role"));
        if(span.HasKey(L"reading"))item.reading=parse_semantic_reading(text(span,"reading"));result.spans.push_back(std::move(item));}
    validate_text_semantics(result);return result;
}
void require_math_prompt_semantics(const JsonObject& root,std::string_view owner,const JsonObject& current,bool retain_spans) {
    require(root.HasKey(L"textSemantics"),"A mathematical prompt revision needs explicit current semantics");
    const auto metadata=root.GetNamedObject(L"textSemantics");const auto fields=array(metadata,"fields");
    std::size_t matches=0;
    for(const auto& value:fields){const auto field=value.GetObject();
        if(text(field,"block")==owner&&text(field,"path")=="/prompt"){
            const auto descriptor=math_prompt_semantics(value,owner,current);
            require(descriptor.has_value(),"A mathematical prompt descriptor cannot be null");
            require(!retain_spans||!descriptor->spans.empty(),"A mathematical prompt revision cannot discard all existing semantic spans");++matches;}}
    require(matches==1,"A mathematical prompt revision needs exactly one current prompt descriptor");
}
bool bibliography_pair(const JsonObject& before,const JsonObject& after){
    static const std::set<std::string> types{"multiple_choice","output_prediction","fill_code","order_code","drag_fill_code","inline_fill_code"};
    if(!types.contains(text(before,"type"))||!before.HasKey(L"explanation")||!after.HasKey(L"explanation"))return false;
    auto old=JsonObject::Parse(before.Stringify()),now=JsonObject::Parse(after.Stringify());
    const auto old_text=bibliography_string(old.GetNamedString(L"explanation")),new_text=bibliography_string(now.GetNamedString(L"explanation"));
    old.Remove(L"explanation");now.Remove(L"explanation");
    return fingerprint(old)==fingerprint(now)&&bibliography::only_parenthetical_locator_removal(old_text,new_text);
}
std::vector<StatePositionalReadingPage> compiler_positional_reading_pages(const JsonObject& root,
        const JsonObject& metadata,const JsonArray& pages,const StateLayout& layout,std::string_view lesson_id) {
    if(layout.version!=2||layout.locale!="es")return {};
    // Complete publications: 617f03472d20a97f2e1e110a3233ad11fd966d01 ->
    // 5d115a0c99c8d2a17d01eee99dfe887247b82dbb. Only this closed source
    // data restores positional pages; it grants no active editorial permission.
    struct OriginalBlock {
        const char* id;unsigned current;const char* sha256;const char* text;
        bool command_emphasis=false;unsigned original_output_count=0;
    };
    struct Publication {
        const char* owner;unsigned page_count,page,editorial_count;
        const char* root_sha;const char* family_sha;const char* origin_sha;const char* current_sha;
        const char* editorial_sha;const char* before_page_sha;const char* after_page_sha;
        std::vector<OriginalBlock> original;
    };
    static const std::array<Publication,7> publications{{
        // U11L19 p5 raw source SHA-256, before and after:
        // 887dbfb3f98c0f04c92567d2d1a46d82ddb1bd3ebe761ecf60c17a905e1815a0
        // bfaa88c3f4294c30bf50b58f96aff91fbf71ffdbdfd20c7471d1c71e358c1cb6
        {"cpp_fundamentals__texto_interpretacion_y_flujos__declaraciones_aceptadas_solo_al_completar",8,4,27,
            "3fd93a03d527f964c4fc161b961e4f5ed5709aa3f352604c3f970a8b84526971",
            "99f329d08b80347be1a68344d1118176db6248e2fcbefa2018afaf5cd49c4827",
            "4145af91d344e6ed355078e4bcf990884010c6f7d8137b9615cf810382f5b946",
            "b462421d7889e1a807cf2bdd273d02ffc15f3a47f96e7210dc8ca2931ac4ecf6",
            "b9e2415d6071dc940cb642d25b5f833c0f8a3c12b365bfae1a7e0933ce22b934",
            "a391a98e6f4f958095821d9e0779a5bbe29cc9b0797c0af261d1e3ad93da8ee1",
            "379916e3512869dbbc9dd826ad3560a8d9ac6311e8ea10a72b68a1fdf9a3a73c",
            {
                {"block_0004_0000",0,"135fd28d6ba91926a222df3438d9b006388766e525008ca0fa206d25dcc428c8",nullptr},
                {"block_0004_0001",1,"12c57e25c4552fc50fd218cad3bbbdf5293b286eaed8f809eeadac23868ebf8e",
                    "Integra nombre como unidad y crea como sentencia en tu intérprete.\n\n"
                    "El texto inicial es \"crea base=4;base*3;crea base=8;base;\".\n\n"
                    "Debe aceptar 4,12 y4, rechazar la redefinición y conservar base=4.\n\n"
                    "Recupera tus módulos previos, pero aplica la copia de tabla y la publicación sólo después de ;.\n\n"
                    "Construye una sola batería mediante probar(texto), con lector, tabla y contadores nuevos por llamada. main usa \"crea base=4;base*3;crea base=8;base;\", \"crea a=0;a;\", \"crea a=-99-1;a;\", \"crea a=99+1;a;\", \"crea a=100;a;\", \"crea a=a+1;crea a=2;a;\", \"crea a=5\", \"crea dato=3;crea dato2=-dato;dato;dato2;\", en ese orden.\n\n"
                    "Cada llamada comienza con Caso N, numerado desde 1; conserva las etiquetas Valor: N, Aceptadas:, Rechazadas: y Tabla:, con un espacio antes de cada vínculo en orden de creación.\n\n"
                    "Todas las líneas terminan con salto, sin líneas vacías añadidas.\n\n"
                    "Los rechazos de esta batería presentan Error: seguido de una de estas causas exactas: Nombre no definido, Nombre ya definido, Numero fuera de dominio, Se esperaba ;.\n\n"
                    "La comprobación exige ese informe, sin imponer nombres de funciones ni una única organización del programa.\n\n"
                    "Usa Compilar, ejecutar y comprobar con todos los casos de la misma implementación.",true},
                {"block_0004_0002",4,"f39ac13f9e47680db14cb9ebf76ec35f3c2af8faf2a67303d1ba6bea3c6a6b7e",nullptr},
                {"block_0004_0003",2,"e7aabc9464c589e774b7c33dab163ddba3e99d264531a145f058f821015fbd37",
                    "La batería contrasta cero, extremos de resultado, literal 100 rechazado, autorreferencia sin vínculo previo, terminador ausente y dos nombres cercanos. Cada rechazo conserva la tabla aceptada y permite recuperar su propia frontera; consultar no añade un vínculo provisional."},
                {"block_0004_0004",3,"958ced4d076334214d50b83ae9037cad847c63adf505c8cf6536ba09f7d2ec8f",
                    "Criterios: inicialización obligatoria, unicidad, consulta pura, límites heredados, conservación de la tabla tras duplicados/ausencia/mala gramática y continuación por la frontera correcta. Una lectura de EOF es final normal sólo entre operaciones; dentro de una creación incompleta es un rechazo."},
            }},
        // U11L20 p6 raw source SHA-256, before and after:
        // cde8f8a248fcc8a7eab16033f2e469932eb24929e5fff3cae3d77c688f1338f8
        // 69eef7a0633bafaea663bc5b3bfa23da2a8940cf25ecbbd8af60090587204cf9
        {"cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada",9,5,25,
            "ee52a84f2abb5a9c5bded4da4bdff2f443dea7d96fe143f43cc1c3f3c84069bf",
            "5b1700977fc6a022db478e6f8e203e0cf3f6b7c9f10ff591f5e5bff7da493ba8",
            "fb98f229033bafa190fec6bc841a9abf90685944424c2232a46bd9f674887448",
            "2c8935ee701c092d3713380bb41ac669ca9559e80e3f4491afb0af92ccdeaf5e",
            "fcb91d108bf5712ca0237cc74c31003ef316383ffc17e8abef8321375ff3c8ed",
            "e642593c08552a6a96314bab52207e5a3980648c10c14f7ddd0ea2378b1bbeed",
            "1a780df3c81825c243f8299f1606af80f0bde2d614d07ef5aa4cde92d7026eaf",
            {
                {"block_0005_0000",0,"49f176a0f0dc9d83d233d3d696d60bc5f2d3cbf2b2ddbc417398f455d1ab2dc7",nullptr},
                {"block_0005_0001",1,"a1688e271a704709a8d0d44c718727d88d5eea377c7d7fef469ef3de5e3d8db6",
                    "Integra el despacho de sentencias y la continuación desde un valor en el programa de declaraciones.\n\n"
                    "Parte de \"crea base=7;base=base+2;base*2+1;base=4 8;base;\".\n\n"
                    "Debe aceptar 7,9,19 y9, rechazar la asignación incompleta y dejar base=9.\n\n"
                    "El editor no entrega los módulos; recupera su estructura y comprueba cada frontera.\n\n"
                    "Construye una sola batería mediante probar(texto), con lector, tabla y contadores nuevos por llamada. main usa \"crea base=7;base=base+2;base*2+1;base=4 8;base;\", \"crea a=4;a=8/0;a;\", \"crea a=4;a=99+2;a;\", \"a=3;crea a=2;a;\", \"crea a=5;crea b=2;a=b=3;a;b;\", \"crea a=5;a=a+1\", \"crea a=6;a%4*3;\", \"crea paso=5;ausente;paso+1;\", en ese orden.\n\n"
                    "Cada llamada comienza con Caso N, numerado desde 1; conserva las etiquetas Valor: N, Aceptadas:, Rechazadas: y Tabla:, con un espacio antes de cada vínculo en orden de creación.\n\n"
                    "Todas las líneas terminan con salto, sin líneas vacías añadidas.\n\n"
                    "Los rechazos de esta batería presentan Error: seguido de una de estas causas exactas: Divisor cero, Nombre no definido, Resultado fuera de dominio, Se esperaba ;.\n\n"
                    "La comprobación exige ese informe, sin imponer nombres de funciones ni una única organización del programa.\n\n"
                    "Usa Compilar, ejecutar y comprobar con todos los casos de la misma implementación.",true},
                {"block_0005_0002",4,"2afd4d309a2571e5ac47d1ad64d809edf3293edce7b2cdd6ead5fd06a0036f08",nullptr},
                {"block_0005_0003",2,"f0adf115232510b37d472d576dc88da5365c116dcd0442ce33804b674d0ea93f",
                    "Las variantes de la batería comprueban divisor cero, dominio, destino ausente, asignación encadenada, terminador ausente, continuación multiplicativa y consulta ausente antes de una operación válida. El rechazo no cambia la tabla aceptada; el ; reservado de ausente no debe provocar que se descarte paso+1;."},
                {"block_0005_0004",3,"933dade08349b80446ed7cad157f0265c082edac8f8cbba9c886788f5eeccdfd",
                    "Criterios: distinguir creación, modificación y consulta; mantener reserva única; completar el término antes de la expresión; consultar el valor anterior en el lado derecho; publicar sólo después del terminador y conservar la tabla tras cada rechazo. No se promete devolver los caracteres consumidos."},
            }},
        // U11L21 p6 raw source SHA-256, before and after:
        // 65b1eb97508bfb7f6dbbc695f25c8cfa27994333892878c44c297b7ccea463cc
        // d444222f6ce6f69436279bee30e734f6fb9823ce7c6df592f6085a07734b04db
        {"cpp_fundamentals__texto_interpretacion_y_flujos__constantes_del_lenguaje_y_nombres_predefinidos",8,5,22,
            "dfdb12121e27374a4cd54db4bd9ec610c08984f6e097c993929710e6dc95a1ca",
            "3ab92cc5bbda79981a0b6760151cb9c1f420dcd515e0b4e3f802c2182807ba6c",
            "7f2edabeb9c3e5755411100c43237665324e6ad3b1ac843ccd71634f98b053c3",
            "e724a0d1a1b61d7df69d8fd2123657712a66c07f7893eb35b471d33f84dfb7d4",
            "324cd0b445d434d114fd96fa63ae343226e2ade800bdcd90b2bfe8478f0c1abc",
            "3018c71926e1427c7d31df2f0cb098fd6dc43c0f92e54a22ca11a4552d927ebe",
            "86441557dd179650e7bb552b2fab9dbc7e3ba85b8aaaf9ae23fde54f13e8e967",
            {
                {"block_0005_0000",0,"150dde3e216aea9ea11f13f43eda55dcdda7792d59f75a80bfa85639f0d6b594",nullptr},
                {"block_0005_0001",1,"ec1da1e4ab8fc6afbe539e41a6933385d21cfbcf0c5141035484c3c8eb810741",
                    "Amplía la sesión con la categoría fija y la protección por vínculo.\n\n"
                    "El caso inicial es \"crea base=4;fija copia=base+2;base=9;copia;limite=3;limite;\".\n\n"
                    "Debe aceptar 4,6,9,6 y100, rechazar el cambio de limite y conservar copia=6.\n\n"
                    "No entregues una protección sólo visual: asignar debe rechazar realmente la modificación.\n\n"
                    "Construye una sola batería mediante probar(texto), con lector, tabla y contadores nuevos por llamada. main usa \"crea base=4;fija copia=base+2;base=9;copia;limite=3;limite;\", \"fija x=5 2;x;\", \"fija x=unidad;x=x+1;x;\", \"crea unidad=3;unidad;\", \"crea a=limite+1;a;\", \"crea a=1;crea b=2;crea c=3;crea d=4;crea e=5;crea f=6;crea g=7;a;\", en ese orden.\n\n"
                    "Cada llamada comienza con Caso N, numerado desde 1; conserva las etiquetas Valor: N, Aceptadas:, Rechazadas: y Tabla:, con un espacio antes de cada vínculo en orden de creación.\n\n"
                    "Todas las líneas terminan con salto, sin líneas vacías añadidas.\n\n"
                    "Los rechazos de esta batería presentan Error: seguido de una de estas causas exactas: Nombre constante, Nombre no definido, Nombre ya definido, Resultado fuera de dominio, Se esperaba ;, Tabla llena.\n\n"
                    "La comprobación exige ese informe, sin imponer nombres de funciones ni una única organización del programa.\n\n"
                    "Usa Compilar, ejecutar y comprobar con todos los casos de la misma implementación.\n\n"
                    "Cada vínculo añade su marca (V) si es modificable o (C) si es constante.\n\n"
                    "Cada tabla comienza con unidad=1(C) y limite=100(C), que cuentan dentro de las ocho plazas.",true},
                {"block_0005_0002",4,"b31af141cccd6da262f40c46a6d6f3aaf311d8ac6e8fba83a9bf51b65b4bcfdd",nullptr},
                {"block_0005_0003",2,"6b4b5fda01b16f90576589a4078b992ac0decd104cc57ac58dbc2be228101773",
                    "Las seis pruebas de la batería comprueban inicializador incompleto, constante protegida, duplicación de predefinido, dominio del resultado y capacidad compartida. La consulta final de a después de rechazar la séptima creación comprueba que el estado aceptado sigue disponible."},
                {"block_0005_0004",3,"79d1aa206257b8dfdbab019177acba1d2ce2982451349f3d18f89d920210f4f0",
                    "Criterios: palabras completas, nombres reservados, copia de la política completa, inicialización única de predefinidos, protección antes de escribir, capacidad compartida y aceptación sólo tras ;. La comprobación contrasta sus informes; revisar la implementación sigue siendo necesario para acreditar esos criterios."},
            }},
        // U11L22 p4 raw source SHA-256, before and after:
        // 02556412590ab4bd53c86eff1074ba2b52145fcf9a4d7d2bee771cd4af806bf7
        // 1932b3c86cabf23bf02154eb2d0371c3b8e7fe48fc01e42e1d559b3220cb1696
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado",10,3,26,
            "26d86ac1c857a82759738a34741a72baa293100b7efcb1fd435ef568e9543193",
            "21ba75fa9504523ce0c8fc5a32d9aea75e3822c1a7b900b3ba3cf02c85bb1780",
            "8f63bbdd3f8d3dad5be813b865e141df59614dca6bc7dc7adb2df576cad77d10",
            "6cae82ceaaf28ff37a047ff3c2cf4137ace7c4b611733702a25f0c95876ca418",
            "12b8619a9c48805b0c7b128d9041dee111b7a8ef36b4f917fba3dcf432a8afb4",
            "5d1a006b8d0cec809dfd4f6ff36fe310819a20ebc3beff37f9b97256664fa37c",
            "37f3ef1d2e0ebeaf4937767add00fc78583d5e80aa7bcdbb7b5638a7595e7b83",
            {
                {"block_0003_0000",0,"085ba993072a08c1b753b595822eb23daca5caad06034f4a11b32ff73ab1496d",nullptr},
                {"block_0003_0001",4,"f9e64b47ad9adb230ff94f8fff062faf7e2fc5b4a35e618499dedcca99cb3ad0",nullptr},
                {"block_0003_0002",1,"2738a6f644854f35d7e8ce0c8ffaf0e7e3ab63f2494cc4a34bf047ec75dfb1ff",
                    "El editor aporta sólo includes, el texto inicial y main. Recupera ideas o módulos anteriores, decide tus tipos e interfaces y conecta reconocimiento, anticipación, gramática, candidato, recuperación, historial y resumen. No se entregan implementaciones nuevas ni condiciones de decisiones para rellenar."},
                {"block_0003_0003",2,"e71ffd1ee0e2cbc5b58e365aa18c7769daebd9761305b6c4fe43fc258460e7d3",
                    "Puedes integrar por etapas y reintentar este mismo objetivo: reconocimiento, consulta sin inserción, creación, asignación, protección y continuación tras error. La batería fija los informes que se comparan; el historial sólo se amplía después de aceptar una operación completa."},
                {"block_0003_0004",3,"921f98038a4dde1185c32ba75b9a11d761b378feade1789c4a36270b47fc49d2",
                    "Criterios de corrección: todos los contratos y casos anteriores, reserva coherente con la interfaz de una unidad, ninguna variable global modificable, ausencia de salidas codificadas por caso, publicación sólo tras ; y resumen por consultas que no cambian los vínculos. Una solución distinta de la referencia es válida si demuestra esos resultados y estados."},
            }},
        // U11L22 p7 raw source SHA-256, before and after:
        // 02556412590ab4bd53c86eff1074ba2b52145fcf9a4d7d2bee771cd4af806bf7
        // 1932b3c86cabf23bf02154eb2d0371c3b8e7fe48fc01e42e1d559b3220cb1696
        {"cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado",10,6,26,
            "26d86ac1c857a82759738a34741a72baa293100b7efcb1fd435ef568e9543193",
            "21ba75fa9504523ce0c8fc5a32d9aea75e3822c1a7b900b3ba3cf02c85bb1780",
            "8f63bbdd3f8d3dad5be813b865e141df59614dca6bc7dc7adb2df576cad77d10",
            "6cae82ceaaf28ff37a047ff3c2cf4137ace7c4b611733702a25f0c95876ca418",
            "12b8619a9c48805b0c7b128d9041dee111b7a8ef36b4f917fba3dcf432a8afb4",
            "e5901ea62ebdd5b33887e4b9bc0915788ed0af84da9b391a19f9d98f2152470e",
            "f0790b41d3a1f0cd047a1b0d2c9b5376b96e023eeacc88f4bf00b1c55f0d35f1",
            {
                {"block_0006_0000",0,"bb00489c00aa22228b1fbc5591c61e001105c51020dea058f7f683a76004defc",nullptr},
                {"block_0006_0001",1,"e3366ea6deadcff0d772dddc8f13050fe3d01ebb65fc12eaadfbcaf42be55854",nullptr},
                {"block_0006_0002",2,"533196badb3b9bfff518c9edd35f695767a9bef13156eadad0a6410c4a1c8a65",
                    "En el editor nuevo de esta página, amplía sólo el resumen con Ceros: N después de Constantes:. Cuenta vínculos cuyo valor aceptado actual sea cero, sin modificar tabla ni historial. El editor incluye la base del intérprete y cuatro sesiones: \"crea a=0;\", \"crea a=0;a=1;\", \"fija k=unidad;k=0;\" y \"crea a=0;fija b=0;a=2;\". Conserva todas las salidas anteriores y sus cabeceras Caso N. La consulta recorre la tabla mediante const referencia; no cuenta ceros históricos. Cada línea termina con salto. Usa Compilar, ejecutar y comprobar con el programa completo.",true},
                {"block_0006_0003",3,"a7dba20876cdafcaf129b5a9dac179944d6342763928a4846aecff064093f9f7",nullptr},
            }},
        // U12L20 p6 raw source SHA-256, before and after:
        // f2e04cdddc482d708f29ef5954d388cc68a9d2d69c69345430b0585afed04789
        // 744739618d59f751567445eedbfa97fca20ce300ce61f3061d33eea610870532
        {"cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia",11,5,50,
            "ac0c200bd3be189e18b228c836bb4630df907582d77a1d9eba159710faca5a92",
            "34da95805cf873ff2eec9d898c778a9f29a7f317fd256ed2c821d75cc29c3497",
            "df53434c6f265abf1fed8b10f538a60eed9244474d0d2488abbeb0565193d216",
            "1e108408c50d2dcc98cb1f96b6a02d90303942a1e4c469f60b600aaa24ac26cb",
            "88189db6d0be92e504dcb415298dcf0132e524b183482162bee4475cf0d3e25c",
            "13b9e617b3e346eb0f3b838c48cdf62cec21b88ae0a465da165bc3ff37d721e6",
            "064b0b63804d034068352f93aad407f724fe3a664cf92b060f659355bbe1a7b9",
            {
                {"block_0005_0000",0,"ad072b0270086da75fd63d8842bfd411b4723a161bc84e2215a6b00112f1ad0c",nullptr},
                {"block_0005_0001",1,"8d176d6113a3b161ef96531216cf87850d53fbd545fbc184b28b98a6bb36dcf3",
                    "Construye texto_admitido(const string&): acepta hasta 40 unidades ASCII, cuyos valores sin signo están entre 0 y 127; rechaza lo demás antes de clasificar o convertir. El string vacío es válido. Este control corresponde a nuestro dominio de trabajo, no a un validador de Unicode."},
                {"block_0005_0002",2,"0a25844559975c5f4ccafeeecff36811bc152815d4adbb1f9bc552c8deb57601",
                    "Define Resumen_caracteres con cuatro contadores int: letras, digitos, blancos y otros. resumir recibe texto admitido por referencia const y devuelve los recuentos según isalpha, isdigit e isspace del locale C, con categorías exclusivas. Define copiar_en_minusculas y copiar_en_mayusculas: reciben texto admitido por referencia const, conservan todas sus unidades y devuelven una copia convertida. Las tres funciones trabajan sin imprimir."},
                {"block_0005_0003",3,"91ab273b9a7506cb041e1a34c292db8400db2b30968c896f6eb96bb7691b8d27",
                    "main conserva una copia previa de la entrada, valida todo y sólo después llama a las funciones.\n\n"
                    "Usa \"NoDo 7!\" inicialmente.\n\n"
                    "Muestra Original:<texto>, Minusculas:<copia>, Mayusculas:<copia>, Letras:L;Digitos:D;Blancos:B;Otros:O y Entrada conservada:V, cada informe en su línea.\n\n"
                    "V es el bool de comparar entrada con la copia previa.\n\n"
                    "Para un rechazo muestra únicamente Texto rechazado.\n\n"
                    "Construye una sola batería mediante probar(const std::string& entrada), con copia previa, validación y resultados locales en cada llamada. main usa \"NoDo 7!\", \"\", \"aZ09_\", cuarenta letras a, cuarenta y una letras a, la cadena de seis caracteres \" \\t\\n\\r\\f\\v\" y una cadena de una unidad construida como std::string(1, static_cast<char>(-1)), en ese orden.\n\n"
                    "Las escrituras con barra representan caracteres de control reales.\n\n"
                    "Ante cada prueba presenta Caso N, numerado desde 1.\n\n"
                    "Las válidas conservan los cinco informes originales y añaden Unidades:N con la longitud; cada informe termina con salto, sin líneas vacías añadidas.\n\n"
                    "Los rechazos sólo muestran Texto rechazado después de su cabecera.\n\n"
                    "El último argumento, convertido a unsigned char, supera 127 y debe rechazarse antes de llamar a <cctype>.\n\n"
                    "Usa Compilar, ejecutar y comprobar para toda la batería.",true},
                {"block_0005_0004",5,"b475e21c42a6b93cb635f5bfd5197b66b7c1107145a413f2fa9582ab142cda35",nullptr,false,1},
                {"block_0005_0005",6,"18da8007edca08b9237ea2d9c47891bd48df9f888d7dce9399411eabc5d85171",
                    "La batería comprueba vacío, letras ya convertidas, dígitos, signos, límites de cuarenta y cuarenta y una unidades, seis clases de blanco y una unidad fuera de ASCII. Los controles pueden mover el cursor al presentar sus copias: la comprobación conserva literalmente esas unidades y observa también Unidades y los recuentos. Cada contador y su suma no exceden 40 en los textos admitidos."},
                {"block_0005_0006",4,"8ea12040685765a3c2922d75231bb70d22b98bdfd6c262d6d298543120fc2b3c",
                    "La referencia está después del intento. No se entregan las funciones ni la validación dentro del editor; no escribas un informe fijo que sólo imite la primera muestra. Todas las entradas son strings preparados dentro de esta ejecución."},
            }},
        // U12L20 p10 raw source SHA-256, before and after:
        // f2e04cdddc482d708f29ef5954d388cc68a9d2d69c69345430b0585afed04789
        // 744739618d59f751567445eedbfa97fca20ce300ce61f3061d33eea610870532
        {"cpp_fundamentals__archivos_y_programa_completo__clasificar_caracteres_y_normalizar_una_copia",11,9,50,
            "ac0c200bd3be189e18b228c836bb4630df907582d77a1d9eba159710faca5a92",
            "34da95805cf873ff2eec9d898c778a9f29a7f317fd256ed2c821d75cc29c3497",
            "df53434c6f265abf1fed8b10f538a60eed9244474d0d2488abbeb0565193d216",
            "1e108408c50d2dcc98cb1f96b6a02d90303942a1e4c469f60b600aaa24ac26cb",
            "88189db6d0be92e504dcb415298dcf0132e524b183482162bee4475cf0d3e25c",
            "74371ef3e67e56b8bfffad9aea1ca2f06c5220e6bb49833a3f38e5ca56dd6d36",
            "08753dd000150023ab2099e34914d1b3907e7091d95448999701507a21a3896a",
            {
                {"block_0009_0000",0,"a11fed3ed0553081988b2741aa6a81e20672515fabf5b4833ed98a2006bd46bc",nullptr},
                {"block_0009_0001",1,"28a8aa1e53e756d80bbecdb56fc1f830c8a33bf7e5f62088948c56a4f94285ff",
                    "Reconstruye copiar_en_minusculas(const std::string&) desde este editor. El llamador garantiza hasta cuarenta caracteres ASCII y el locale C inicial; la función devuelve otra string, conserva longitud, orden, dígitos y signos y no imprime. Prepara una sola batería con \"ABC9_\", \"abc9_\" y \"--7\". Presenta sus copias en tres líneas y después el primer argumento original en una cuarta, todas con salto final. Usa Compilar, ejecutar y comprobar; no vuelvas al editor de clasificación.",true},
                {"block_0009_0002",2,"d33532cf27b2415ea796da3ec14ca23b9f24d111196e6db1a60baa116d182b44",nullptr},
            }},
    }};
    std::vector<StatePositionalReadingPage> result;
    for(const auto& published:publications){
        if(lesson_id!=published.owner)continue;
        const auto page=published.page;const auto& original=published.original;
        // Canonical full-document and page proofs ignore JSON formatting while
        // preserving every value. Reject all pages of a later or mixed source.
        if(pages.Size()!=published.page_count||layout.origin.size()!=published.page_count||layout.current.size()!=published.page_count
            ||page>=pages.Size()||fingerprint(root)!=published.root_sha
            ||fingerprint(metadata.GetNamedObject(L"family"))!=published.family_sha
            ||fingerprint(array(metadata,"origin"))!=published.origin_sha
            ||fingerprint(array(metadata,"current"))!=published.current_sha
            ||array(metadata,"editorial").Size()!=published.editorial_count
            ||fingerprint(array(metadata,"editorial"))!=published.editorial_sha
            ||fingerprint(pages.GetAt(page))!=published.after_page_sha)return {};
        if(layout.origin[page].blocks.size()!=original.size()||layout.current[page].blocks.size()!=original.size())return {};
        StatePositionalReadingPage archived;archived.page=page;archived.original.stable_id="page_"+std::to_string(page);
        JsonArray reconstructed;
        const auto current=array(pages.GetObjectAt(page),"blocks");
        for(unsigned index=0;index<original.size();++index){
            const auto& item=original[index];
            if(item.current>=current.Size()||layout.origin[page].blocks[index]!=item.id
                ||layout.current[page].blocks[item.current]!=item.id)return {};
            const auto previous=JsonObject::Parse(current.GetObjectAt(item.current).Stringify());
            if(item.text){
                previous.Insert(L"text",JsonValue::CreateStringValue(key(item.text)));
                if(previous.HasKey(L"emphasis"))previous.Remove(L"emphasis");
            }
            if(item.command_emphasis){
                JsonObject highlight;highlight.Insert(L"text",JsonValue::CreateStringValue(L"Compilar, ejecutar y comprobar"));
                highlight.Insert(L"style",JsonValue::CreateStringValue(L"bold"));JsonArray emphasis;emphasis.Append(highlight);
                previous.Insert(L"emphasis",emphasis);
            }
            // These complete originals predate the authored wrong-answer and
            // compiler guides. Their full hashes below authenticate the removal.
            if(previous.HasKey(L"reviewFeedback"))previous.Remove(L"reviewFeedback");
            if(previous.HasKey(L"outputCheck"))for(const auto& value:array(previous.GetNamedObject(L"outputCheck"),"cases")){
                const auto test=value.GetObject();
                if(test.HasKey(L"explanation"))test.Remove(L"explanation");
                if(test.HasKey(L"reviewFeedback"))test.Remove(L"reviewFeedback");
                if(item.original_output_count){
                    const auto outputs=array(test,"outputs");if(outputs.Size()<item.original_output_count)return {};
                    JsonArray accepted;for(unsigned output=0;output<item.original_output_count;++output)accepted.Append(outputs.GetAt(output));
                    test.Insert(L"outputs",accepted);
                }
            }
            if(fingerprint(previous)!=item.sha256)return {};
            reconstructed.Append(previous);
            const auto raw=utf8(previous.Stringify());auto block=read_block(previous,raw);
            block.stable_id="block_"+std::to_string(page)+'_'+std::to_string(index);block.address={page,index,{},archived.original.stable_id};
            archived.current_owners.emplace_back(item.id);archived.original.blocks.push_back(std::move(block));
        }
        const auto original_page=JsonObject::Parse(pages.GetObjectAt(page).Stringify());original_page.Insert(L"blocks",reconstructed);
        if(fingerprint(original_page)!=published.before_page_sha)return {};
        result.push_back(std::move(archived));
    }
    return result;
}
std::vector<StateReadingRevision> assignment_reading_revisions(const JsonObject& metadata,const JsonArray& pages,
        const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__texto_interpretacion_y_flujos__asignaciones_con_anticipacion_limitada";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es")return {};
    // This is an annotation of an already validated map, not an additional
    // authored format or a general permission to change exercise text.
    if(fingerprint(metadata.GetNamedObject(L"family"))!="5b1700977fc6a022db478e6f8e203e0cf3f6b7c9f10ff591f5e5bff7da493ba8"
        ||fingerprint(array(metadata,"origin"))!="fb98f229033bafa190fec6bc841a9abf90685944424c2232a46bd9f674887448")return {};
    const auto editorial=array(metadata,"editorial");
    if(editorial.Size()<=20)return {};
    JsonArray published_prefix;for(unsigned i=0;i<20;++i)published_prefix.Append(editorial.GetAt(i));
    JsonObject provenance;provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(owner)));
    provenance.Insert(L"locale",JsonValue::CreateStringValue(L"es"));provenance.Insert(L"editorial",published_prefix);
    if(fingerprint(provenance)!="38bf01987a09a1dcb850488033801b6f74a839b2c24c9eb41327807e80e72b5b")return {};
    struct Field {std::string_view name,before,after;};
    struct Review {std::string_view identity,before;std::size_t page,block;std::vector<Field> fields;};
    static const std::vector<Review> reviews{
        {"block_0002_0003","75bd3371395fe205e1aa8cc4598f99dae109a053f43d7e9c3e095917ba050fe4",2,3,{
            {"question","Ordena la rama que NO encontró =. Supón primero es el nombre y siguiente es el token que vino después.",
                "Ordena la rama que no encontró `=`. Supón que `primero` es el token de nombre y `siguiente` es el token que vino después."},
            {"explanation","Sólo siguiente se reserva. La consulta da la primera unidad; después se completa el término antes de la expresión.",
                "Sólo se reserva `siguiente`, antes de una consulta que puede fallar. La consulta proporciona la primera unidad; después se completa el término y, con ese resultado, la expresión. Si falla la consulta, la recuperación todavía puede encontrar el terminador reservado."}}},
        {"block_0004_0001","1795562bd3f78ab353f8368134ca1a8f6aa122a8148199569402b6c3c88e2595",4,1,{
            {"explanation","La consulta empieza por t y completa el término: 8/3 es 2 y 2%2 es 0. La asignación usa el ocho anterior, guarda nueve y la última expresión consulta nueve.",
                "La creación acepta 8 para `t`. La expresión `t/3%2+4;` completa el término de izquierda a derecha: la división entera produce 2 y el resto siguiente es 0; la suma obtiene 4. La asignación consulta el 8 anterior, acepta 9 y la última expresión obtiene 18. La tabla conserva `t=9`."}}},
        {"block_0004_0002","39cf702fee359eb05408354353e0281d5bb8693d9135cab1d5e501144b0d66ce",4,2,{
            {"question","¿Por qué a=b=3; se rechaza aunque a y b existan?",
                "En nuestro lenguaje, ¿por qué se rechaza `a=b=3;` aunque `a` y `b` existan?"},
            {"explanation","La gramática sólo permite asignación como sentencia superior. El = después de b impide aceptar el terminador y no publica el candidato.",
                "El lado derecho sólo admite las expresiones aritméticas ya construidas. Consulta `b`, pero el `=` siguiente impide reconocer `;`. Por eso no se publica la candidata y ambas asociaciones conservan su valor anterior."}}},
        {"block_0007_0001","9dcecb759eec49a264cb3e937c9338f46948e4cd498be62788c36931d7075490",7,1,{
            {"explanation","El lado derecho obtiene el valor de b y después encuentra = en lugar de ;. Ningún cambio del candidato llega a la tabla aceptada. La recuperación llega al primer ;.",
                "El lado derecho consulta el valor 2 de `b`, pero después encuentra `=` donde se exige `;`. La propuesta de asignar ese valor a `a` no llega a la tabla aceptada. La recuperación consume lo que falta hasta ese primer `;`; las consultas posteriores siguen obteniendo 6 y 2."}}}
    };
    std::map<std::string,JsonObject> current,snapshot;
    std::map<std::string,std::map<std::string,JsonObject>> variants;
    for(const auto& review:reviews){
        if(review.page>=layout.origin.size()||review.block>=layout.origin[review.page].blocks.size()
            ||layout.origin[review.page].blocks[review.block]!=review.identity)return {};
        JsonObject active=nullptr;
        for(std::size_t p=0;p<layout.current.size();++p)for(std::size_t b=0;b<layout.current[p].blocks.size();++b)
            if(layout.current[p].blocks[b]==review.identity)active=array(pages.GetObjectAt(static_cast<unsigned>(p)),"blocks").GetObjectAt(static_cast<unsigned>(b));
        if(!active)return {};
        const auto base=JsonObject::Parse(active.Stringify());
        for(const auto& field:review.fields){
            const auto value=text(active,field.name);if(value!=field.before&&value!=field.after)return {};
            base.Insert(key(field.name),JsonValue::CreateStringValue(key(field.before)));
        }
        // Complete original payloads pin IDs, type, programs, answers, guides,
        // and every field not explicitly reviewed, rather than only a contract.
        if(fingerprint(base)!=review.before)return {};
        const auto id=std::string(review.identity);auto& allowed=variants[id];
        for(std::size_t mask=0;mask<(std::size_t{1}<<review.fields.size());++mask){
            const auto version=JsonObject::Parse(base.Stringify());
            for(std::size_t field=0;field<review.fields.size();++field)if(mask&(std::size_t{1}<<field))
                version.Insert(key(review.fields[field].name),JsonValue::CreateStringValue(key(review.fields[field].after)));
            allowed.emplace(fingerprint(version),version);
        }
        if(!allowed.contains(fingerprint(active)))return {};
        current.emplace(id,active);snapshot.emplace(id,active);
    }
    std::vector<StateReadingRevision> result;
    // Only the actual suffix can be reversed. A newer unrelated edit has no
    // authenticated old payload here, so do not fabricate a cross-version
    // page by walking through it. Extra edits before this suffix are harmless.
    for(std::size_t count=editorial.Size();count>20;--count){
        const auto edge=editorial.GetObjectAt(static_cast<unsigned>(count-1));const auto id=text(edge,"id");
        const auto allowed=variants.find(id);if(allowed==variants.end())break;
        const auto after=text(edge,"afterSha256"),before=text(edge,"beforeSha256");
        if(fingerprint(snapshot.at(id))!=after)break;
        const auto previous=allowed->second.find(before);if(previous==allowed->second.end())break;
        const auto review=std::ranges::find(reviews,id,&Review::identity);if(review==reviews.end())break;
        bool changed=false,forward=true;
        for(const auto& field:review->fields){
            const auto old=text(previous->second,field.name),now=text(snapshot.at(id),field.name);
            if(old==now)continue;
            changed=true;if(old!=field.before||now!=field.after)forward=false;
        }
        if(!changed||!forward)break;
        snapshot.at(id)=previous->second;
        StateReadingRevision archived;archived.editorial_prefix=count-1;
        for(const auto& [identity,block]:snapshot)if(fingerprint(block)!=fingerprint(current.at(identity)))
            archived.previous_blocks.emplace(identity,utf8(block.Stringify()));
        result.push_back(std::move(archived));
    }
    return result;
}
std::optional<StateReadingRevision> remainder_reading_revision(const JsonObject& root,const JsonObject& metadata,
        const JsonArray& pages,const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__texto_interpretacion_y_flujos__resto_y_asociacion_en_el_interprete";
    constexpr std::string_view identity="block_0002_0002",prose_identity="block_0002_0001";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es")return {};
    // These two complete publications can restore only their authentic p3 at
    // prefix17. A coherent later edit or different authoring is not that source.
    const auto editorial=array(metadata,"editorial");const auto root_hash=fingerprint(root);
    const bool published26=editorial.Size()==26
        &&root_hash=="f48a23a3ba894ce7256211371deee1167675d491bf41992df32f6d4ec19b81d9";
    const bool published30=editorial.Size()==30
        &&root_hash=="f1604f89367e7fdc6c946c331390c8d2492d400ecf612c8ed0430f457023a0be";
    if(!published26&&!published30)return {};
    if(pages.Size()<=2||layout.origin.size()<=2||layout.current.size()<=2
        ||layout.origin[2].blocks!=std::vector<std::string>{"block_0002_0000",std::string(prose_identity),std::string(identity),"block_0002_0003"}
        ||layout.current[2].blocks!=layout.origin[2].blocks
        ||fingerprint(metadata.GetNamedObject(L"family"))!="fd3affe1046654eecd9e3ad66884bcc925f161e2fef0bdfd10700b659242fd0f"
        ||fingerprint(array(metadata,"origin"))!="648e2b6ee7937c22a378ceb45a674fc2e0e39c26d9f2a1da9185076ef4b16342"
        ||fingerprint(array(metadata,"current"))!="43fb175bb9f00a9c000b709fd8af187e75e86d170ed1533df2b34bfa1df68956")return {};
    JsonArray prefix;for(unsigned index=0;index<17;++index)prefix.Append(editorial.GetAt(index));
    if(fingerprint(prefix)!="fcd3c0bf5c790fe050ec4b8f5005797ff90ff420cba958ca1dc48fb1359b6694")return {};
    const auto reconstructed=JsonObject::Parse(pages.GetObjectAt(2).Stringify());
    const auto blocks=array(reconstructed,"blocks");const auto previous=blocks.GetObjectAt(2);
    const auto edge=editorial.GetObjectAt(19);
    constexpr std::string_view before="524e9d5823d04f180a87ea73f34ac891b0d102450036493991026f93ce65ae2e";
    constexpr std::string_view after="f1d3e2efbfd02a258da6416c5de3ad4590e2130eb4db28283f97c4a05b1270c0";
    if(edge.Size()!=3||text(edge,"id")!=identity||text(edge,"beforeSha256")!=before
        ||text(edge,"afterSha256")!=after||fingerprint(previous)!=after)return {};
    previous.Insert(L"explanation",JsonValue::CreateStringValue(
        L"La guardia completa se ejecuta antes del operador %. El diagnóstico impide llegar al cálculo cuando el divisor es cero."));
    if(fingerprint(previous)!=before)return {};
    StateReadingRevision result;result.editorial_prefix=17;
    result.previous_blocks.emplace(identity,utf8(previous.Stringify()));
    if(published30){
        const auto prose=blocks.GetObjectAt(1),prose_edge=editorial.GetObjectAt(29);
        constexpr std::string_view prose_before="d8e19c559028ddd9747b6cd42908b2425c6951b4f964fb18904329a6925ad25d";
        constexpr std::string_view prose_after="90e89b257a2c692159ff77a1f7d53aa627db167753bf41fb817de3272f43b527";
        if(prose_edge.Size()!=3||text(prose_edge,"id")!=prose_identity||text(prose_edge,"beforeSha256")!=prose_before
            ||text(prose_edge,"afterSha256")!=prose_after||fingerprint(prose)!=prose_after)return {};
        prose.Insert(L"text",JsonValue::CreateStringValue(
            L"C++ no define un resultado para `/` o `%` con divisor cero. Nuestro intérprete comprueba el divisor y, si vale 0, lanza una excepción con el mensaje \"Divisor cero\" antes de ejecutar el operador (Köppe et al., 2023, sección [expr.mul]; Stroustrup, 2024, sección 6.5).\n\n"
            L"Ejecutar `throw` abandona ese cálculo y transfiere el control al `catch` que trata el rechazo. El mensaje comunica la causa; es el lanzamiento de la excepción el que impide llegar a la operación.\n\n"
            L"Los límites elegidos también permiten calcular los candidatos antes de comprobar la banda. Con operandos entre -100 y 100, una multiplicación tiene magnitud máxima 10000 y una suma o resta, 200. Dividir por -1 produce como máximo una magnitud de 100. Esos candidatos caben en el rango mínimo garantizado para `int`, ya estudiado; después se comprueba si el resultado pertenece al dominio del intérprete (Köppe et al., 2023, sección [basic.fundamental])."));
        if(fingerprint(prose)!=prose_before)return {};
        result.previous_blocks.emplace(prose_identity,utf8(prose.Stringify()));
    }
    // The unchanged heading, tail and emphasis must also belong to the old
    // complete page; the view removes current semantics from changed sources.
    if(fingerprint(reconstructed)!="f7f392f010b4d8cf734be852937710e2016099d286a6e7b7db75101724f58c72")return {};
    return result;
}
std::optional<StateReadingRevision> session_literal_reading_revision(const JsonObject& metadata,const JsonArray& pages,
        const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__texto_interpretacion_y_flujos__construir_una_sesion_con_nombres_y_estado";
    constexpr std::string_view identity="block_0001_0004";
    constexpr std::string_view before="7246b74a0c6b46f2c14b26ea813cfc85a0d691348aea8ada18c279d46ce19156";
    constexpr std::string_view after="09a016cfe0637a8be64ccd67e0ec8f3bd57e8d63ca5e03c39ecbe760d7bacad1";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es")return {};
    // Ordinary puzzle prose remains editable. Only this complete published
    // Spanish payload and authentic initial history can supply old feedback.
    if(layout.origin.size()<=1||layout.origin[1].blocks.size()<=4||layout.origin[1].blocks[4]!=identity
        ||layout.current.size()<=1||layout.current[1].blocks.size()<=4||layout.current[1].blocks[4]!=identity
        ||fingerprint(metadata.GetNamedObject(L"family"))!="21ba75fa9504523ce0c8fc5a32d9aea75e3822c1a7b900b3ba3cf02c85bb1780"
        ||fingerprint(array(metadata,"origin"))!="8f63bbdd3f8d3dad5be813b865e141df59614dca6bc7dc7adb2df576cad77d10")return {};
    const auto editorial=array(metadata,"editorial");
    // Keep any valid intervening prose history intact. Reversing this exact
    // final edge reconstructs its real immediate predecessor; reversing an
    // earlier edge after a later change could invent a page script instead.
    if(editorial.Size()<15)return {};
    JsonArray prefix;for(unsigned index=0;index<14;++index)prefix.Append(editorial.GetAt(index));
    JsonObject provenance;provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(owner)));
    provenance.Insert(L"locale",JsonValue::CreateStringValue(L"es"));provenance.Insert(L"editorial",prefix);
    if(fingerprint(provenance)!="c28edae1339c91de5a3fbac7c727c939f530b67bc9902a72fd065d94e62a93ee")return {};
    const auto edge=editorial.GetObjectAt(editorial.Size()-1);
    if(edge.Size()!=3||text(edge,"id")!=identity||text(edge,"beforeSha256")!=before||text(edge,"afterSha256")!=after)return {};
    const auto active=array(pages.GetObjectAt(1),"blocks").GetObjectAt(4);
    if(fingerprint(active)!=after)return {};
    const auto previous=JsonObject::Parse(active.Stringify());
    previous.Insert(L"explanation",JsonValue::CreateStringValue(
        L"La creación completa guardó cero. El literal100 se rechaza y su candidato no sustituye ese estado."));
    if(fingerprint(previous)!=before)return {};
    StateReadingRevision result;result.editorial_prefix=editorial.Size()-1;
    result.previous_blocks.emplace(identity,utf8(previous.Stringify()));
    return result;
}
std::optional<StateReadingRevision> append_hypothesis_reading_revision(const JsonObject& metadata,const JsonArray& pages,
        const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__archivos_y_programa_completo__cerrar_reemplazar_y_anadir_datos";
    constexpr std::string_view identity="block_0002_0001";
    constexpr std::string_view before="3f3f15e209f0d54ce342167a2e007062ebf83bb1784531d40873a75dafd765ef";
    constexpr std::string_view after="b80a16cd7af0d46d6628a11784747299e7607317f58f87b48a41019f923157b2";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es")return {};
    // This closed annotation restores only the reviewed question's page.
    // Later edits on other pages cannot change that historical spoken script.
    // It grants no general exercise-editing or cross-page archive format.
    if(layout.origin.size()<=2||layout.origin[2].blocks.size()<=1||layout.origin[2].blocks[1]!=identity
        ||layout.current.size()<=2||layout.current[2].blocks!=layout.origin[2].blocks
        ||fingerprint(metadata.GetNamedObject(L"family"))!="a50cf6bf3544c70d3df1b5f9e152f35d51214cf4d6999d02fb4d8b568a0e044a"
        ||fingerprint(array(metadata,"origin"))!="70274d28ca19d9c4e9c5f2d1632b135aeb5b86e4c4431dfcbd023d68c87a0ef7")return {};
    const auto editorial=array(metadata,"editorial");
    if(editorial.Size()<7)return {};
    JsonArray prefix;for(unsigned index=0;index<6;++index)prefix.Append(editorial.GetAt(index));
    if(fingerprint(prefix)!="2bbd1afca905bbafa53a49d7197e1f328bdd2f2107e305583156a3182702387c")return {};
    std::optional<unsigned> revision_index;
    for(unsigned index=6;index<editorial.Size();++index){
        const auto edge=editorial.GetObjectAt(index);
        if(text(edge,"id")!=identity)continue;
        if(revision_index||edge.Size()!=3||text(edge,"beforeSha256")!=before||text(edge,"afterSha256")!=after)return {};
        revision_index=index;
    }
    if(!revision_index||(*revision_index!=6&&*revision_index!=11))return {};
    JsonArray reviewed_prefix;
    for(unsigned index=0;index<*revision_index;++index)reviewed_prefix.Append(editorial.GetAt(index));
    const auto prefix_hash=fingerprint(reviewed_prefix);
    if(!((*revision_index==6&&prefix_hash=="2bbd1afca905bbafa53a49d7197e1f328bdd2f2107e305583156a3182702387c")
        ||(*revision_index==11&&prefix_hash=="6ea8b50922d76302cd12241f8a57cc189a0e90532a6f71490e12857b361b0713")))return {};
    std::set<std::string> other_page_blocks;
    for(std::size_t page=0;page<layout.current.size();++page)if(page!=2)
        other_page_blocks.insert(layout.current[page].blocks.begin(),layout.current[page].blocks.end());
    for(unsigned index=*revision_index+1;index<editorial.Size();++index){
        const auto changed=text(editorial.GetObjectAt(index),"id");
        // Without that block's complete old payload, a later edit on p3
        // cannot be mixed into the old question's actual page narration.
        if(!other_page_blocks.contains(changed))return {};
    }
    const auto active=array(pages.GetObjectAt(2),"blocks").GetObjectAt(1);
    if(fingerprint(active)!=after)return {};
    const auto previous=JsonObject::Parse(active.Stringify());
    previous.Insert(L"question",JsonValue::CreateStringValue(
        L"Supón que las operaciones de archivo se completan. Esta versión omite app en la segunda apertura. Predice el diagnóstico visible."));
    if(fingerprint(previous)!=before)return {};
    StateReadingRevision result;result.editorial_prefix=*revision_index;
    result.previous_blocks.emplace(identity,utf8(previous.Stringify()));
    return result;
}
std::optional<StateReadingRevision> composed_output_reading_revision(const JsonObject& metadata,const JsonArray& pages,
        const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es")return {};
    // The first adoption, later publication and exact proposed review retain
    // the same first two pages. They may restore only those pages' authentic
    // positional narration, never later pages or an editable history.
    if(pages.Size()!=5||layout.signatures.size()!=25
        ||fingerprint(metadata.GetNamedObject(L"family"))!="f9865100c59e76353575893fd342d00b0ce5eee3ef2c9605a4b9189db4fbd887"
        ||fingerprint(array(metadata,"origin"))!="6c5a4300e032dfde02180cfb468cbb2b418931fbc65fb8f771561aa30b2dbee3"
        ||fingerprint(array(metadata,"current"))!="430aa6b167fac29d684c9c45734b6c611ce06be9bbd16a303c598d1c562a6e92")return {};
    const auto editorial=array(metadata,"editorial");
    const auto history_hash=fingerprint(editorial),pages_hash=fingerprint(pages);
    const bool first=editorial.Size()==8
        &&history_hash=="1ad25edf3618c1f51a1e21504b52bbc65f27d70b7deba6ef5344d126912c345b"
        &&pages_hash=="b3ee4c125cf8fbc0242a4a0cbb24f34dc03677d84ad6485c7c319c8fbd878852";
    const bool published=editorial.Size()==15
        &&history_hash=="317364cd08c3847423c5a3d3a6b8b017e1882f91be895b738361aef13a68a956"
        &&pages_hash=="5c2c07a09c4679edb36ce6c246d0dcb265f00ab856f583ee52a4ad5e1c2ef14b";
    const bool feedback=editorial.Size()==17
        &&history_hash=="3ff7da20dbe7d2e0709b927606c59890b4dd3d975bfd6e67ad0f44ecb29a0c29"
        &&pages_hash=="e9960d58dacdbb79b32b45e8c4b8d5db23fb0090eee6ab758152078754c65950";
    if(!first&&!published&&!feedback)return {};
    struct ReviewedBlock {unsigned page,block;const char* id;const char* before;const char* after;const wchar_t* prose;};
    constexpr std::array<ReviewedBlock,8> reviewed{{
        {0,1,"block_0000_0001","c02adc2d24a66e7b0dce4f5aa5692cb2f204850bb1552792ca1bd5e2087c6b19",
            "b3d34bd31f3e0109c48641f6c3bd16dd75eddaeda0a9788611bd595e79d73f21",
            L"std::ostringstream, declarado en <sstream>, recibe inserciones y conserva los caracteres producidos en memoria. Su operaci\u00f3n str() devuelve una cadena con ese contenido. A diferencia de cout, insertar en este flujo no env\u00eda el mensaje a la salida est\u00e1ndar. Puedes construir un texto, devolverlo y decidir despu\u00e9s si mostrarlo, guardarlo o usarlo como nombre de una muestra (Stroustrup, 2024, secci\u00f3n 9.11, pp. 283\u2013284)."},
        {0,3,"block_0000_0003","53da4b1c7e1185a16256c4981ac6e5efc6ae5367e6786724929ebe9ef483b2fc",
            "d90d01bec3b7c8fedd2054b4ce07e9b3694b28f3617a894ce5ab6e88935eac82",
            L"El resultado visible es Cajas=4;Estado=listo, pero s\u00f3lo la \u00faltima instrucci\u00f3n lo muestra. Las inserciones anteriores forman los caracteres en texto. El nombre mensaje conserva una cadena, no un flujo abierto ni una instrucci\u00f3n que deba ejecutarse otra vez."},
        {0,4,"block_0000_0004","7a041d7e9cfc4c7f5e8cc7000144fa9123dac81f9af54e4505a19640e2081e58",
            "2834c29f0d3680bdf1fda9e6b573ba3e624ebd507e67d7083e6654d1ef4140fd",nullptr},
        {1,1,"block_0001_0001","042cc93bd22860e2760dabc3b06baab868eae9c22a1e89c0d738f16b9f9b264b",
            "79de873c83c4090d903b2ac91b67fe186b053b45ba2566b33ed51bb5e909f114",nullptr},
        {1,2,"block_0001_0002","fa2bd564d660cb74e06163a7cbb9986c6fdfb9bb83a6743027e363f38b32b566",
            "a3c73edce6e41ffa8f86f478a5667a53e2ed1801fb43430e89721228e35d5e9b",
            L"str() consulta el contenido; no lo vac\u00eda ni imprime. Las siguientes inserciones contin\u00faan en el mismo flujo. No se debe confundir copiar el resultado string con copiar el objeto ostringstream: este \u00faltimo no admite la copia ordinaria que usamos con cadenas y registros."},
        {1,3,"block_0001_0003","faf6393e200b49d4b9a31be600ebabd33fe4f7b224ff6bfbc6479a7027382480",
            "4d21f3591bfa76c81c6fa7fdc48ffb455745414abdad0fc9cf1cb768b9d7bf0f",nullptr},
        {1,4,"block_0001_0004","5259a7687faffd5b6daf28dd2db9a4f73c387bc13c9f78b97affee8c1987b622",
            "7fed11b6ada96a990c364b94264c5152690187a2b59106156cfe225daa2c31b3",nullptr},
        {1,5,"block_0001_0005","2b80ca083b2d0eb1b2a5b9492a15ae5ae2095f880cbcb65f36276a6069705109",
            "8216c1ca7b4af782b539a6e72edf5b36ccd0d3fcf98103a21da4453398dbaf22",nullptr},
    }};
    const auto reconstructed=JsonArray::Parse(pages.Stringify());
    StateReadingRevision result;result.editorial_prefix=8;result.positional_page_identity=true;
    for(unsigned index=0;index<reviewed.size();++index){
        const auto& item=reviewed[index];const auto edge=editorial.GetObjectAt(index);
        const auto active=array(pages.GetObjectAt(item.page),"blocks").GetObjectAt(item.block);
        if(layout.origin[item.page].blocks[item.block]!=item.id||layout.current[item.page].blocks[item.block]!=item.id
            ||edge.Size()!=3||text(edge,"id")!=item.id||text(edge,"beforeSha256")!=item.before
            ||text(edge,"afterSha256")!=item.after||fingerprint(active)!=item.after)return {};
        const auto previous=JsonObject::Parse(active.Stringify());
        if(item.prose){
            previous.Insert(L"text",JsonValue::CreateStringValue(item.prose));
            previous.Remove(L"emphasis");
        }else previous.Remove(L"reviewFeedback");
        if(fingerprint(previous)!=item.before)return {};
        array(reconstructed.GetObjectAt(item.page),"blocks").SetAt(item.block,previous);
        result.previous_blocks.emplace(item.id,utf8(previous.Stringify()));
    }
    JsonArray positional_pages;positional_pages.Append(reconstructed.GetAt(0));positional_pages.Append(reconstructed.GetAt(1));
    if(fingerprint(positional_pages)!="fcc6108499fd8f21209a035a8676be437251dc5ab2c7c849b18f57f20f2bd966"
        ||(first&&fingerprint(reconstructed)!="ffeb1349bb0fcd7d1bc9e7002c344ce68e3fcf4a8162464e9b908606521010a8"))return {};
    return result;
}
std::optional<StateReadingRevision> composed_feedback_reading_revision(const JsonObject& metadata,const JsonArray& pages,
        const StateLayout& layout,std::string_view lesson_id) {
    constexpr std::string_view owner="cpp_fundamentals__archivos_y_programa_completo__componer_salida_sin_imprimirla";
    constexpr std::string_view identity="block_0004_0001";
    constexpr std::string_view before="cf06a21a5f5a9b85cc94cf0d4e41cebc672813a7a82c30728832291dc25bcdb8";
    constexpr std::string_view after="4bddac581f21a70aa212ec0039f9ab2ab64badbcd315419e2e149a7ae7240166";
    if(lesson_id!=owner||layout.version!=2||layout.locale!="es"||pages.Size()!=5||layout.signatures.size()!=25
        ||fingerprint(metadata.GetNamedObject(L"family"))!="f9865100c59e76353575893fd342d00b0ce5eee3ef2c9605a4b9189db4fbd887"
        ||fingerprint(array(metadata,"origin"))!="6c5a4300e032dfde02180cfb468cbb2b418931fbc65fb8f771561aa30b2dbee3"
        ||fingerprint(array(metadata,"current"))!="430aa6b167fac29d684c9c45734b6c611ce06be9bbd16a303c598d1c562a6e92"
        ||fingerprint(array(metadata,"editorial"))!="3ff7da20dbe7d2e0709b927606c59890b4dd3d975bfd6e67ad0f44ecb29a0c29"
        ||fingerprint(pages)!="e9960d58dacdbb79b32b45e8c4b8d5db23fb0090eee6ab758152078754c65950")return {};
    const auto editorial=array(metadata,"editorial");
    if(editorial.Size()!=17||layout.origin.size()!=5||layout.current.size()!=5
        ||layout.origin[4].blocks.size()!=3||layout.origin[4].blocks[1]!=identity
        ||layout.current[4].blocks!=layout.origin[4].blocks)return {};
    const auto edge=editorial.GetObjectAt(16);
    if(edge.Size()!=3||text(edge,"id")!=identity||text(edge,"beforeSha256")!=before||text(edge,"afterSha256")!=after)return {};
    const auto active=array(pages.GetObjectAt(4),"blocks").GetObjectAt(1);
    if(fingerprint(active)!=after)return {};
    const auto previous=JsonObject::Parse(active.Stringify());
    previous.Insert(L"question",JsonValue::CreateStringValue(
        L"Supón que se completan apertura y escritura. ¿Qué texto aparece en la salida estándar de este programa?"));
    if(fingerprint(previous)!=before)return {};
    const auto compiler_edge=editorial.GetObjectAt(15);
    if(compiler_edge.Size()!=3||text(compiler_edge,"id")!="block_0002_0003"
        ||text(compiler_edge,"beforeSha256")!="628f37fb8e1fb1f82180b90a6ff241b9e868482914e4ce9dd84149e453b8e00d"
        ||text(compiler_edge,"afterSha256")!="2a22d63af34fb093c3feeef538fdb794fa6b57074fddcc601b473162937043b7")return {};
    const auto compiler=array(pages.GetObjectAt(2),"blocks").GetObjectAt(3);
    if(fingerprint(compiler)!="2a22d63af34fb093c3feeef538fdb794fa6b57074fddcc601b473162937043b7")return {};
    const auto old_compiler=JsonObject::Parse(compiler.Stringify());
    const auto old_case=array(old_compiler.GetNamedObject(L"outputCheck"),"cases").GetObjectAt(0);
    old_case.Remove(L"explanation");old_case.Remove(L"reviewFeedback");
    if(fingerprint(old_compiler)!="628f37fb8e1fb1f82180b90a6ff241b9e868482914e4ce9dd84149e453b8e00d")return {};
    const auto reconstructed=JsonArray::Parse(pages.Stringify());
    array(reconstructed.GetObjectAt(2),"blocks").SetAt(3,old_compiler);
    array(reconstructed.GetObjectAt(4),"blocks").SetAt(1,previous);
    if(fingerprint(reconstructed)!="5c2c07a09c4679edb36ce6c246d0dcb265f00ab856f583ee52a4ad5e1c2ef14b")return {};
    // Both complete old payloads belong to the same authentic prefix15.
    // Each view extracts only its own page; the independent positional p1/p2
    // archive must not acquire this stable v2 feedback/question history.
    StateReadingRevision result;result.editorial_prefix=15;
    result.previous_blocks.emplace("block_0002_0003",utf8(old_compiler.Stringify()));
    result.previous_blocks.emplace(identity,utf8(previous.Stringify()));
    return result;
}
// Case-order provenance is independent of the closed compiler expansions.
// Canonical full cases, including runtime channels and comparison modes, are
// compared separately: state_fingerprint intentionally omits some channels.
std::string compiler_case_contract(const JsonObject& block,bool allow_root_output=false) {
    require(text(block,"type")=="cpp_compiler"&&(block.HasKey(L"outputCheck")||allow_root_output)&&
        !block.HasKey(L"compileCheck"),"Case ordering requires an output-check compiler");
    if(!block.HasKey(L"outputCheck"))return "root-output-contract";
    return fingerprint(without_compiler_guidance(block).GetNamedObject(L"outputCheck"));
}
JsonObject permuted_compiler_cases(const JsonObject& before,
        const std::vector<std::size_t>& order) {
    const auto cases=array(before.GetNamedObject(L"outputCheck"),"cases");
    require(cases.Size()>=2&&order.size()==cases.Size(),
        "Case ordering needs every existing case exactly once");
    std::set<std::size_t> used;bool moved=false;JsonArray reordered;
    for(std::size_t i=0;i<order.size();++i) {
        require(order[i]<order.size()&&used.insert(order[i]).second,
            "Case ordering needs every existing case exactly once");
        moved=moved||order[i]!=i;reordered.Append(cases.GetAt(static_cast<unsigned>(order[i])));
    }
    require(moved,"Case ordering must change the declared order");
    auto result=JsonObject::Parse(before.Stringify());
    result.GetNamedObject(L"outputCheck").Insert(L"cases",reordered);
    require(fingerprint(result)!=fingerprint(before),"Case ordering must change the declared order");
    return result;
}
std::optional<unsigned> compiler_client_file(const JsonObject& block,std::string_view path) {
    if(!block.HasKey(L"projectFiles"))return {};
    const auto files=array(block,"projectFiles");
    for(unsigned index=0;index<files.Size();++index)
        if(text(files.GetObjectAt(index),"path")==path)return index;
    return {};
}
void copy_compiler_client(const JsonObject& target,const JsonObject& source,std::string_view path) {
    require(target.HasKey(L"projectFiles")==source.HasKey(L"projectFiles")&&
        target.HasKey(L"cmake")==source.HasKey(L"cmake"),
        "A compiler client must retain its source mode");
    if(!source.HasKey(L"projectFiles")) {
        require(path=="main.cpp"&&!source.HasKey(L"cmake")&&!source.HasKey(L"sourceFiles")&&
            !target.HasKey(L"sourceFiles")&&source.HasKey(L"initialCode")&&target.HasKey(L"initialCode"),
            "A single-source compiler client uses only its virtual main.cpp source");
        target.Insert(L"initialCode",source.GetNamedValue(L"initialCode"));
        return;
    }
    const auto target_file=compiler_client_file(target,path),source_file=compiler_client_file(source,path);
    require(target_file&&source_file,"A compiler client must retain its exact existing file");
    require(source.HasKey(L"cmake")||target.HasKey(L"initialCode")==source.HasKey(L"initialCode"),
        "A compiler client must retain its authored initialCode alias");
    array(target,"projectFiles").GetObjectAt(*target_file).Insert(L"initialCode",
        array(source,"projectFiles").GetObjectAt(*source_file).GetNamedValue(L"initialCode"));
    // CMake's top-level initialCode is independent metadata, not a client
    // alias. Its own authenticated formatting must survive this projection.
    if(!source.HasKey(L"cmake")&&source.HasKey(L"initialCode"))
        target.Insert(L"initialCode",source.GetNamedValue(L"initialCode"));
}
bool is_cmake_recipe_path(std::string_view path) {
    const auto leaf=path.substr(path.find_last_of('/')+1);
    return leaf=="CMakeLists.txt"||path.ends_with(".cmake");
}
bool compiler_client_matches(const JsonObject& source,const JsonObject& expected,
        std::string_view path,bool allow_format) {
    if(source.HasKey(L"projectFiles")!=expected.HasKey(L"projectFiles")||
            source.HasKey(L"cmake")!=expected.HasKey(L"cmake"))return false;
    const auto same=[&](const std::string& before,const std::string& after) {
        return before==after||(allow_format&&curriculum::code_format::equivalent(
            before,after,is_cmake_recipe_path(path)?curriculum::code_format::Language::cmake:
                curriculum::code_format::Language::cpp));
    };
    if(!source.HasKey(L"projectFiles"))
        return path=="main.cpp"&&!source.HasKey(L"cmake")&&!source.HasKey(L"sourceFiles")&&
            !expected.HasKey(L"sourceFiles")&&source.HasKey(L"initialCode")&&expected.HasKey(L"initialCode")&&
            same(text(expected,"initialCode"),text(source,"initialCode"));
    const auto actual_file=compiler_client_file(source,path),expected_file=compiler_client_file(expected,path);
    if(!actual_file||!expected_file)return false;
    if(!source.HasKey(L"cmake")&&source.HasKey(L"initialCode")!=expected.HasKey(L"initialCode"))return false;
    return same(text(array(expected,"projectFiles").GetObjectAt(*expected_file),"initialCode"),
            text(array(source,"projectFiles").GetObjectAt(*actual_file),"initialCode"))&&
        (source.HasKey(L"cmake")||!expected.HasKey(L"initialCode")||
            same(text(expected,"initialCode"),text(source,"initialCode")));
}
JsonObject revised_compiler_client(const JsonObject& before,const JsonObject& client) {
    allowed_keys(client,{"path","source"},true);
    require(!before.HasKey(L"compileCheck"),"A compilation diagnostic cannot revise its runtime client");
    if(before.HasKey(L"projectFiles"))for(const auto& value:array(before,"projectFiles"))
        allowed_keys(value.GetObject(),{"path","initialCode"},true);
    const auto parsed=read_block(before,utf8(before.Stringify()));
    const auto path=text(client,"path"),source=text(client,"source");
    std::optional<unsigned> position;std::string previous;
    if(!before.HasKey(L"projectFiles")) {
        require(path=="main.cpp","A single-source compiler client uses the virtual main.cpp path");
        previous=text(before,"initialCode");
        require(previous.size()<=4*1024*1024&&source.size()<=4*1024*1024,
            "A single-source compiler client exceeds the existing 4 MiB limit");
    } else {
        // CMake selects sources through its real description. The loader can
        // authenticate an existing C++ file, not infer target membership.
        if(parsed.cmake)require(is_cpp_source_file(path),"A CMake client must be an existing C++ source");
        else require(std::ranges::find(parsed.source_files,path)!=parsed.source_files.end(),
            "A compiler client must be an existing selected C++ source");
        position=compiler_client_file(before,path);
        require(position.has_value(),"A compiler client must retain its exact existing file");
        previous=text(array(before,"projectFiles").GetObjectAt(*position),"initialCode");
        if(!parsed.cmake)require(!before.HasKey(L"initialCode")||text(before,"initialCode")==previous,
            "The existing initialCode alias must exactly name the selected client");
    }
    require(!source.empty()&&source.find('\0')==source.npos&&source!=previous,
        "A compiler client revision needs a changed nonempty source without NUL");
    auto result=JsonObject::Parse(before.Stringify());
    if(position)array(result,"projectFiles").GetObjectAt(*position).Insert(L"initialCode",JsonValue::CreateStringValue(key(source)));
    if(!parsed.cmake&&result.HasKey(L"initialCode"))result.Insert(L"initialCode",JsonValue::CreateStringValue(key(source)));
    // Reuse the existing project paths, source selection, CMake recipe and
    // byte limits without altering any auxiliary file or build configuration.
    (void)read_block(result,utf8(result.Stringify()));
    return result;
}
JsonObject revised_compiler_cases(const JsonObject& before,const JsonArray& cases,
        const std::optional<JsonObject>& client={}) {
    (void)compiler_case_contract(before);
    require(cases.Size()>0,"A compiler case revision needs explicit nonempty cases");
    auto result=client?revised_compiler_client(before,*client):JsonObject::Parse(before.Stringify());
    result.GetNamedObject(L"outputCheck").Insert(L"cases",cases);
    require(fingerprint(result)!=fingerprint(before),"A compiler case revision must change its declared cases");
    return result;
}
JsonObject revised_compiler_recipe(const JsonObject& before,const JsonObject& item) {
    const auto recipe=item.GetNamedObject(L"recipe");
    allowed_keys(recipe,{"path","source"},true);
    require(!before.HasKey(L"compileCheck"),"A CMake recipe cannot change a compilation diagnostic");
    for(const auto& value:array(before,"projectFiles"))
        allowed_keys(value.GetObject(),{"path","initialCode"},true);
    const auto parsed=read_block(before,utf8(before.Stringify()));
    require(parsed.type==BlockType::cpp_compiler&&parsed.cmake&&before.HasKey(L"projectFiles"),
        "A recipe revision requires an existing CMake project");
    require(std::ranges::any_of(parsed.project_files,[](const auto& file) {
        return is_cpp_source_file(file.first);
    }),"A recipe revision must retain an existing C++ source");
    const auto path=text(recipe,"path"),source=text(recipe,"source");
    const auto position=compiler_client_file(before,path);
    require(is_cmake_recipe_path(path)&&position.has_value(),
        "A recipe revision must select an existing CMakeLists.txt or .cmake file");
    require(!source.empty()&&source.find('\0')==source.npos&&
        source!=text(array(before,"projectFiles").GetObjectAt(*position),"initialCode"),
        "A recipe revision requires changed nonempty text without NUL");
    if(before.HasKey(L"outputCheck")) {
        (void)compiler_case_contract(before);
        require(fingerprint(array(item,"cases"))==
            fingerprint(array(before.GetNamedObject(L"outputCheck"),"cases")),
            "A recipe revision must retain every original output case");
    } else {
        require(item.GetNamedValue(L"cases").ValueType()==JsonValueType::Null&&
            array(item,"semanticBefore").Size()==0&&array(item,"semanticAfter").Size()==0,
            "A root-evaluated recipe must preserve absent cases and case semantics");
    }
    require(fingerprint(array(item,"semanticBefore"))==fingerprint(array(item,"semanticAfter")),
        "A recipe revision cannot change case semantics");
    auto result=JsonObject::Parse(before.Stringify());
    array(result,"projectFiles").GetObjectAt(*position).Insert(L"initialCode",
        JsonValue::CreateStringValue(key(source)));
    (void)read_block(result,utf8(result.Stringify()));
    return result;
}
struct CompilerCaseVersion {
    JsonObject before,after;
    std::vector<std::size_t> order;
    std::size_t journal_index=0;
    bool case_revision=false;
    std::size_t editorial_index=0;
    std::string client_path,client_epoch;
};
void validate_compiler_recipe_objectives(const JsonObject& root,const Lesson& lesson) {
    for(const auto& revision:lesson.state_layout.compiler_case_revisions) {
        if(!revision.preserves_root_output_check)continue;
        require(revision.client_revision&&revision.client_revision->cmake_recipe&&
            root.HasKey(L"compilerOpenBatteryChecks"),
            "A recipe without outputCheck needs its authored root battery objective");
        const auto entries=array(root,"compilerOpenBatteryChecks");
        bool authored=false;
        for(const auto& entry:entries)
            if(text(entry.GetObject(),"activity")==revision.id)authored=true;
        require(authored,"A recipe cannot borrow another editor's root battery objective");
        const auto position=stable_block_position(lesson,revision.id);
        require(position.has_value(),"A recipe must retain its active objective owner");
        const auto& owner=lesson.pages.at(position->page).blocks.at(position->block);
        require(owner.cmake&&owner.output_check&&!owner.output_check->cases.empty()&&!owner.compile_check,
            "A root-evaluated recipe must retain its complete runtime output contract");
    }
}
struct CompilerCaseHistory {
    std::vector<CompilerCaseVersion> versions;
    std::map<std::string,std::set<std::size_t>> published_epochs;
    std::set<std::string> client_paths;
    bool source_formatted=false;
};
using CompilerCaseHistories=std::map<std::string,CompilerCaseHistory>;
CompilerCaseHistories compiler_case_histories(
        const StateLayout& layout,const JsonArray& editorial,std::string_view lesson_id) {
    CompilerCaseHistories result;
    if(layout.compiler_case_order_revisions.empty()&&layout.compiler_case_revisions.empty())return result;
    struct Pending {
        std::string_view id,raw_json,after_raw_json,sha256,history_sha256;
        const std::vector<std::size_t>* order=nullptr;
        std::size_t journal_index=0;
        std::string_view client_path,record_sha256;
    };
    std::map<std::size_t,Pending> indexed;
    std::optional<std::size_t> last_index;
    for(std::size_t i=0;i<layout.compiler_case_order_revisions.size();++i) {
        const auto& record=layout.compiler_case_order_revisions[i];
        require(record.editorial_index<editorial.Size()&&
            (!last_index||*last_index<record.editorial_index)&&
            indexed.emplace(record.editorial_index,Pending{record.id,record.raw_json,record.after_raw_json,
                record.sha256,record.history_sha256,&record.order,i,{},record.record_sha256}).second,
            "Case-order archives must follow their exact editorial order");
        last_index=record.editorial_index;
    }
    last_index.reset();
    for(std::size_t i=0;i<layout.compiler_case_revisions.size();++i) {
        const auto& record=layout.compiler_case_revisions[i];
        require(record.editorial_index<editorial.Size()&&
            (!last_index||*last_index<record.editorial_index)&&
            indexed.emplace(record.editorial_index,Pending{record.id,record.raw_json,record.after_raw_json,
                record.sha256,record.history_sha256,nullptr,i,
                record.client_revision?std::string_view(record.client_revision->path):std::string_view{},record.record_sha256}).second,
            "Compiler case archives must use distinct ordered editorial edges");
        last_index=record.editorial_index;
    }
    for(const auto& [index,record]:indexed) {
        auto& history=result[std::string(record.id)];
        const auto before=JsonObject::Parse(key(record.raw_json));
        if(!history.versions.empty())
            require(compiler_case_contract(before,true)==compiler_case_contract(history.versions.back().after,true),
                "Case ordering changed an existing case, channel or comparison mode");
        const auto after=record.order?permuted_compiler_cases(before,*record.order):
            JsonObject::Parse(key(record.after_raw_json));
        const auto client_epoch=!record.client_path.empty()?std::string(record.record_sha256):
            history.versions.empty()?std::string{}:history.versions.back().client_epoch;
        history.versions.push_back({before,after,record.order?*record.order:std::vector<std::size_t>{},
            record.journal_index,!record.order,index,std::string(record.client_path),client_epoch});
        if(!record.client_path.empty())history.client_paths.emplace(record.client_path);
    }
    // The full format archive is authenticated below. Its presence permits
    // only lexical formatting at this step; source mutations still need their
    // own exact client event before projection may restore earlier bytes.
    const auto metadata=JsonObject::Parse(key(layout.raw_json));
    if(metadata.HasKey(L"sourceFormatRevisions"))for(const auto& value:array(metadata,"sourceFormatRevisions")) {
        const auto owner=text(value.GetObject(),"id");
        if(const auto found=result.find(owner);found!=result.end())found->second.source_formatted=true;
    }
    for(const auto& [owner,history]:result) {
        (void)owner;
        for(std::size_t index=1;index<history.versions.size();++index)
            for(const auto& path:history.client_paths)
                require(compiler_client_matches(history.versions[index].before,history.versions[index-1].after,
                    path,history.source_formatted),
                    "Compiler client history changed a source outside its authenticated transition");
    }
    JsonArray prefix;std::map<std::string,std::size_t> epochs;
    for(unsigned index=0;index<editorial.Size();++index) {
        const auto proof=editorial.GetObjectAt(index);const auto id=text(proof,"id");
        if(const auto found=result.find(id);found!=result.end()) {
            auto& history=found->second;auto& epoch=epochs[id];
            history.published_epochs[text(proof,"beforeSha256")].insert(epoch);
            if(const auto entry=indexed.find(index);entry!=indexed.end()) {
                const auto& record=entry->second;
                require(record.id==id&&epoch<history.versions.size(),
                    "Case-order editorial edge belongs to a different owner");
                const auto& version=history.versions[epoch];
                require(text(proof,"beforeSha256")==record.sha256&&
                    text(proof,"afterSha256")==fingerprint(version.after),
                    "Case ordering needs its exact complete editorial pair");
                JsonObject provenance;provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(lesson_id)));
                provenance.Insert(L"locale",JsonValue::CreateStringValue(key(layout.locale)));
                provenance.Insert(L"editorial",prefix);
                require(record.history_sha256==fingerprint(provenance),
                    "Case-order history differs from its complete ordered prefix");
                ++epoch;
            }
            history.published_epochs[text(proof,"afterSha256")].insert(epoch);
        } else require(!indexed.contains(index),
            "Case-order editorial edge belongs to a different owner");
        prefix.Append(proof);
    }
    return result;
}
std::size_t compiler_case_epoch(const JsonObject& source,
        const CompilerCaseHistory& history,bool historical,
        std::optional<std::size_t> editorial_prefix={}) {
    const auto contract=compiler_case_contract(source,true);
    const auto matches=[&](std::size_t epoch) {
        const auto expected=epoch?history.versions.at(epoch-1).after:history.versions.front().before;
        if(contract!=compiler_case_contract(expected,true))return false;
        return std::ranges::all_of(history.client_paths,[&](const auto& path) {
            return compiler_client_matches(source,expected,path,history.source_formatted);
        });
    };
    if(!historical) {
        require(matches(history.versions.size()),
            "Case ordering changed an existing case, channel or comparison mode");
        return history.versions.size();
    }
    const auto node=history.published_epochs.find(fingerprint(source));
    require(node!=history.published_epochs.end(),
        "A historical case order needs an exact occurrence in its owner's editorial chain");
    if(editorial_prefix) {
        const auto epoch=static_cast<std::size_t>(std::ranges::count_if(history.versions,
            [&](const auto& version){return version.editorial_index<*editorial_prefix;}));
        require(node->second.contains(epoch)&&matches(epoch),
            "Historical compiler cases differ from their authenticated editorial prefix");
        return epoch;
    }
    // Legacy snapshots do not identify which occurrence of a returned payload
    // they captured. Keep their existing interpretation instead of borrowing
    // a later journal merely because its block bytes happen to match.
    for(const auto epoch:node->second)if(matches(epoch))return epoch;
    invalid("Historical case order changed a case, channel or comparison mode");
}
JsonObject compiler_case_projection(const JsonObject& source,
        const CompilerCaseHistory& history,bool historical=false,
        std::optional<std::size_t> editorial_prefix={}) {
    const auto epoch=compiler_case_epoch(source,history,historical,editorial_prefix);
    if(!epoch)return source;
    auto result=JsonObject::Parse(source.Stringify());
    // Each epoch is authenticated before projection. Undo in reverse time:
    // a four-case permutation never addresses a later sixteen-case array.
    for(std::size_t step=epoch;step>0;--step) {
        const auto& version=history.versions[step-1];
        if(!version.client_path.empty()) {
            require(compiler_client_matches(result,version.after,version.client_path,history.source_formatted),
                "A compiler client differs from its authenticated source epoch");
            copy_compiler_client(result,version.before,version.client_path);
        }
        if(version.case_revision) {
            if(version.before.HasKey(L"outputCheck"))
                result.GetNamedObject(L"outputCheck").Insert(L"cases",
                    array(version.before.GetNamedObject(L"outputCheck"),"cases"));
            else require(!result.HasKey(L"outputCheck"),
                "A recipe history cannot introduce an unrecorded output contract");
        } else {
            const auto cases=array(result.GetNamedObject(L"outputCheck"),"cases");
            require(cases.Size()==version.order.size(),"Case-order projection changed its authenticated case count");
            std::vector<std::size_t> inverse(version.order.size());
            for(std::size_t i=0;i<version.order.size();++i)inverse.at(version.order[i])=i;
            JsonArray previous;
            for(const auto index:inverse)previous.Append(cases.GetAt(static_cast<unsigned>(index)));
            result.GetNamedObject(L"outputCheck").Insert(L"cases",previous);
        }
    }
    return result;
}
std::map<std::string,TextSemantics> read_case_order_semantics(
    const JsonObject&,const JsonArray&,const std::string&);

StateLayout read_layout(const JsonObject& metadata, const JsonArray& pages, std::string_view locale, std::string_view lesson_id,const JsonObject& root,std::string_view document_source) {
    StateLayout result;
    result.raw_json = utf8(metadata.Stringify());
    result.version = static_cast<int>(integer(metadata.GetNamedValue(L"version"), 2));
    require(result.version == 1 || result.version == 2, "Unsupported state layout version");
    if (result.version == 1) allowed_keys(metadata, {"version", "origin", "current"}, true);
    else {
        allowed_keys(metadata, {"version", "locale", "origin", "current", "family", "additions", "editorial", "retired", "retirementHistory", "orderRefinements", "choiceRevisions", "compilerRevisions", "compilerCaseOrderRevisions", "compilerCaseRevisions", "sourceFormatRevisions", "mathTextRevisions"});
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
    std::map<std::string,std::string> refined_order_signatures;
    std::map<std::string,const ReviewedChoice*> revised_choices;
    std::map<std::string,JsonObject> general_choice_revisions;
    std::map<std::string,const ReviewedCompiler*> revised_compilers;
    std::map<std::string,const ReviewedCompiler*> revised_compiler_references;
    if(metadata.HasKey(L"compilerRevisions")) {
        const auto records=array(metadata,"compilerRevisions");
        require(result.version==2&&records.Size()<=reviewed_compilers().size()&&(!records.Size()||locale=="es"),"Reviewed compiler expansions require Spanish v2 ownership");
        for(const auto& value:records) {
            const auto item=value.GetObject();allowed_keys(item,{"id","block","references"});const auto id=text(item,"id");
            const auto* reviewed=reviewed_compiler_owner(lesson_id,id);
            require(reviewed&&current_blocks.contains(id)&&revised_compilers.emplace(id,reviewed).second,
                "Unknown, inactive or repeated reviewed compiler identity");
            const auto snapshot=item.GetNamedObject(L"block");
            require(text(snapshot,"type")=="cpp_compiler"&&fingerprint(snapshot)==reviewed->before&&
                state_fingerprint(snapshot)==reviewed->before&&result.signatures.at(id)==reviewed->before,
                "A compiler expansion changed its complete published original");
            CompilerRevision record;record.id=id;record.raw_json=utf8(snapshot.Stringify());record.sha256=fingerprint(snapshot);
            record.previous_payload=reviewed->before;record.current_payload=reviewed->after;
            if(!reviewed->starter_append.empty())record.starter_append=std::string(reviewed->starter_append);
            require(item.HasKey(L"references")==!reviewed->reference_identity.empty(),"The expanded report and its reference must be revised together");
            if(item.HasKey(L"references")) {
                const auto references=array(item,"references");require(references.Size()==1,"A compiler expansion has one exact reviewed reference");
                const auto reference=references.GetAt(0).GetObject();allowed_keys(reference,{"id","block"},true);const auto reference_id=text(reference,"id");
                const auto original=reference.GetNamedObject(L"block");
                require(reference_id==reviewed->reference_identity&&current_blocks.contains(reference_id)&&
                    revised_compiler_references.emplace(reference_id,reviewed).second&&text(original,"type")=="code"&&
                    fingerprint(original)==reviewed->reference_before&&result.signatures.at(reference_id)==reviewed->reference_before,
                    "The reviewed reference changed its complete published code or owner");
                record.references.push_back({reference_id,utf8(original.Stringify()),std::string(reviewed->reference_before),std::string(reviewed->reference_after)});
            }
            result.compiler_revisions.push_back(std::move(record));
        }
    }
    if(metadata.HasKey(L"choiceRevisions")) {
        const auto records=array(metadata,"choiceRevisions");
        require(result.version==2&&
                records.Size()<=reviewed_choices().size()+64&&
                (!records.Size()||locale=="es"),
            "Choice revisions require their Spanish v2 ownership");
        for(const auto& value:records) {
            const auto item=value.GetObject();
            const auto id=text(item,"id");
            require(valid_id(id)&&current_blocks.contains(id)&&
                    !revised_choices.contains(id)&&
                    !general_choice_revisions.contains(id),
                "Unknown, inactive or repeated choice revision identity");
            const auto* reviewed=reviewed_choice_owner(lesson_id,id);
            const auto snapshot=item.GetNamedObject(L"block");
            std::optional<TextSemantics> previous_option_semantic;
            if(item.HasKey(L"optionIndex")) {
                if(item.HasKey(L"semanticAfter"))
                    allowed_keys(item,{"id","block","optionIndex","replacement","semanticBefore",
                        "semanticAfter","semanticRootBefore"},true);
                else allowed_keys(item,{"id","block","optionIndex","replacement","semanticBefore"},true);
                previous_option_semantic=validate_general_choice_semantic_archive(root,item);
                require(!reviewed&&text(snapshot,"type")=="multiple_choice"&&
                        state_fingerprint(snapshot)==result.signatures.at(id),
                    "General choice archive must belong to an original unchanged answer");
                // Validate index, type and nonempty replacement now; its
                // authentic active bytes are checked below against this copy.
                (void)general_choice_projection(snapshot,item,true);
                general_choice_revisions.emplace(id,item);
            }else{
                allowed_keys(item,{"id","block"},true);
                require(reviewed&&text(snapshot,"type")=="multiple_choice"&&
                        fingerprint(snapshot)==reviewed->before_full&&
                        state_fingerprint(snapshot)==reviewed->before_payload&&
                        result.signatures.at(id)==reviewed->before_payload,
                    "The reviewed option snapshot changed its complete published original");
                revised_choices.emplace(id,reviewed);
            }
            ChoiceRevision record;record.id=id;
            record.raw_json=utf8(snapshot.Stringify());
            record.sha256=fingerprint(snapshot);
            if(item.HasKey(L"optionIndex")) {
                record.general_option_index=integer(item.GetNamedValue(L"optionIndex"),
                    static_cast<std::size_t>(array(snapshot,"options").Size()-1));
                record.original_option_text_semantics=std::move(previous_option_semantic);
            }
            result.choice_revisions.push_back(std::move(record));
        }
    }
    if (metadata.HasKey(L"orderRefinements")) {
        require(result.version == 2, "Ordering refinements require state layout v2");
        const auto records = array(metadata,"orderRefinements");
        require(records.Size() <= 100000 && (!records.Size() || locale == "es"),
            "Ordering refinements are currently authorized only for Spanish");
        for (const auto& value : records) {
            const auto item = value.GetObject(); allowed_keys(item,{"id","block"},true);
            const auto id = text(item,"id");
            require(valid_id(id) && current_blocks.contains(id) && !refined_order_signatures.contains(id),
                "Unknown, inactive or duplicated ordering refinement identity");
            const auto snapshot = item.GetNamedObject(L"block");
            require(state_fingerprint(snapshot) == result.signatures.at(id),
                "An ordering refinement changed its original response contract");
            auto expanded = expand_order(snapshot,id);
            refined_order_signatures.emplace(id,state_fingerprint(expanded_order_json(snapshot,expanded.block)));
            result.order_refinements.push_back(std::move(expanded.record));
        }
    }
    if(metadata.HasKey(L"compilerCaseOrderRevisions")) {
        const auto records=array(metadata,"compilerCaseOrderRevisions");
        require(result.version==2&&records.Size()<=100000&&(!records.Size()||locale=="es"),
            "Compiler case ordering requires Spanish stateLayout v2");
        for(const auto& value:records) {
            const auto item=value.GetObject();allowed_keys(item,
                {"id","block","order","editorialIndex","historySha256","semanticBefore"},true);
            CompilerCaseOrderRevision record;record.id=text(item,"id");
            require(valid_id(record.id)&&current_blocks.contains(record.id),
                "Unknown or inactive compiler case-order owner");
            const auto previous=item.GetNamedObject(L"block");
            (void)compiler_case_contract(previous);
            record.raw_json=utf8(previous.Stringify());record.sha256=fingerprint(previous);
            record.record_sha256=fingerprint(item);record.history_sha256=text(item,"historySha256");
            require(valid_hash(record.history_sha256),"Invalid case-order history fingerprint");
            record.editorial_index=integer(item.GetNamedValue(L"editorialIndex"),100000);
            for(const auto& index:array(item,"order"))record.order.push_back(integer(index,100000));
            record.after_raw_json=utf8(permuted_compiler_cases(previous,record.order).Stringify());
            const auto parsed=read_block(previous,record.raw_json);
            require(parsed.output_check.has_value(),"Case ordering needs its original output contract");
            record.previous_initial_code=parsed.initial_code;record.previous_project_files=parsed.project_files;
            record.previous_output_check=*parsed.output_check;
            record.previous_case_text_semantics=read_case_order_semantics(previous,array(item,"semanticBefore"),record.id);
            result.compiler_case_order_revisions.push_back(std::move(record));
        }
    }
    if(metadata.HasKey(L"compilerCaseRevisions")) {
        const auto records=array(metadata,"compilerCaseRevisions");
        require(result.version==2&&records.Size()<=100000&&(!records.Size()||locale=="es"),
            "Compiler case revisions require Spanish stateLayout v2");
        for(const auto& value:records) {
            const auto item=value.GetObject();
            if(item.HasKey(L"recipe"))allowed_keys(item,
                {"id","block","cases","editorialIndex","historySha256","semanticBefore","semanticAfter","recipe"},true);
            else if(item.HasKey(L"client"))allowed_keys(item,
                {"id","block","cases","editorialIndex","historySha256","semanticBefore","semanticAfter","client"},true);
            else allowed_keys(item,
                {"id","block","cases","editorialIndex","historySha256","semanticBefore","semanticAfter"},true);
            CompilerCaseRevision record;record.id=text(item,"id");
            require(valid_id(record.id)&&current_blocks.contains(record.id),
                "Unknown or inactive compiler case-revision owner");
            const auto previous=item.GetNamedObject(L"block");
            const auto client=item.HasKey(L"client")?std::optional<JsonObject>{item.GetNamedObject(L"client")}:std::nullopt;
            const auto recipe=item.HasKey(L"recipe")?std::optional<JsonObject>{item.GetNamedObject(L"recipe")}:std::nullopt;
            const auto revised=recipe?revised_compiler_recipe(previous,item):
                revised_compiler_cases(previous,array(item,"cases"),client);
            record.preserves_root_output_check=recipe&&!previous.HasKey(L"outputCheck");
            record.raw_json=utf8(previous.Stringify());record.after_raw_json=utf8(revised.Stringify());
            record.sha256=fingerprint(previous);record.record_sha256=fingerprint(item);
            record.history_sha256=text(item,"historySha256");
            require(valid_hash(record.history_sha256),"Invalid compiler case-revision history fingerprint");
            record.editorial_index=integer(item.GetNamedValue(L"editorialIndex"),100000);
            const auto before_block=read_block(previous,record.raw_json);
            const auto after_block=read_block(revised,record.after_raw_json);
            require((before_block.output_check&&after_block.output_check)||
                (record.preserves_root_output_check&&!before_block.output_check&&!after_block.output_check),
                "A compiler case revision requires both complete output contracts");
            record.previous_initial_code=before_block.initial_code;
            record.previous_project_files=before_block.project_files;
            if(!record.preserves_root_output_check) {
                record.previous_output_check=*before_block.output_check;
                record.revised_output_check=*after_block.output_check;
            }
            if(client||recipe) {
                CompilerClientRevision change;
                change.path=text(recipe?*recipe:*client,"path");
                change.cmake_recipe=recipe.has_value();change.revised_initial_code=after_block.initial_code;
                change.revised_project_files=after_block.project_files;
                record.client_revision=std::move(change);
            }
            record.previous_case_text_semantics=read_case_order_semantics(previous,array(item,"semanticBefore"),record.id);
            record.revised_case_text_semantics=read_case_order_semantics(revised,array(item,"semanticAfter"),record.id);
            result.compiler_case_revisions.push_back(std::move(record));
        }
    }
    const auto case_order_histories=compiler_case_histories(result,
        result.version==2?array(metadata,"editorial"):JsonArray{},lesson_id);
    const auto case_order_source=[&](const std::string& id,const JsonObject& source,bool historical=false,
            std::optional<std::size_t> editorial_prefix={}) {
        const auto found=case_order_histories.find(id);if(found==case_order_histories.end())return source;
        // A reading snapshot may precede the separately authenticated closed
        // expansion. Its shorter original objective is not a permutation.
        if(historical&&revised_compilers.contains(id)&&
                state_fingerprint(source)==result.signatures.at(id)&&
                (!editorial_prefix||std::ranges::none_of(found->second.versions,
                    [&](const auto& version){return version.editorial_index<*editorial_prefix;}))) {
            const auto original=std::ranges::find(result.compiler_revisions,id,&CompilerRevision::id);
            require(original!=result.compiler_revisions.end()&&
                compiler_case_contract(source)==compiler_case_contract(JsonObject::Parse(key(original->raw_json))),
                "An original historical compiler changed a case, channel or comparison mode");
            return source;
        }
        return compiler_case_projection(source,found->second,historical,editorial_prefix);
    };
    // Full pre-format snapshots retain their existing identity, including
    // after an authenticated retirement. They are not extra answers, storage
    // slots or drafts; current/retired ownership is checked below.
    std::map<std::string,JsonObject> source_format_originals;
    if(metadata.HasKey(L"sourceFormatRevisions")) {
        require(result.version==2,"Source format archives require stateLayout v2");
        const auto revisions=array(metadata,"sourceFormatRevisions");
        require(revisions.Size()<=100000,"Too many source-format archives");
        for(const auto& item_value:revisions){
            const auto item=item_value.GetObject();allowed_keys(item,{"id","block"},true);
            const auto id=text(item,"id");
            require(valid_id(id)&&result.signatures.contains(id)&&!source_format_originals.contains(id),
                "Unknown or duplicated source-format owner");
            const auto previous=item.GetNamedObject(L"block");
            const auto projected=case_order_source(id,previous,true);
            const auto before=state_fingerprint(projected);
            // A formatting archive can begin after an authenticated compiler,
            // reference, choice or order revision. Compare with that revised
            // contract, not the original pre-revision signature. The active
            // block is independently checked against the same target below.
            const auto expected=revised_compiler_references.contains(id)?
                std::string(revised_compiler_references.at(id)->reference_after):
                revised_compilers.contains(id)?
                    std::string(revised_compilers.at(id)->after):
                revised_choices.contains(id)?
                    std::string(revised_choices.at(id)->after_payload):
                refined_order_signatures.contains(id)?
                    refined_order_signatures.at(id):result.signatures.at(id);
            require(before==expected,"Source format archive does not match its authenticated pre-format contract");
            source_format_originals.emplace(id,projected);
            result.source_format_revisions.push_back({id,utf8(previous.Stringify()),fingerprint(previous)});
        }
    }
    // The full original string preserves the mathematical worker's insertion
    // order and exact numbers. It never substitutes the visible authored block.
    std::map<std::string,JsonObject> math_text_originals;
    if(metadata.HasKey(L"mathTextRevisions")) {
        const auto revisions=array(metadata,"mathTextRevisions");
        require(result.version==2&&locale=="es"&&revisions.Size()<=100000,"Mathematical prompt archives require Spanish stateLayout v2");
        for(const auto& value:revisions){
            const auto item=value.GetObject();allowed_keys(item,{"id","field","rawJson","semanticBefore"},true);
            MathTextRevision record;record.id=text(item,"id");record.field=text(item,"field");record.raw_json=text(item,"rawJson");
            require(valid_id(record.id)&&current_blocks.contains(record.id)&&!math_text_originals.contains(record.id)
                &&record.field=="/prompt","Unknown, inactive or duplicated mathematical prompt owner");
            require(!record.raw_json.empty()&&record.raw_json.size()<=32*1024*1024,"Invalid mathematical prompt archive size");
            require_integer_json_numbers(record.raw_json);
            const auto previous=JsonObject::Parse(key(record.raw_json));
            (void)math_prompt_projection(previous,previous);
            require_math_prompt_contract(record.raw_json,record.raw_json);
            require(state_fingerprint(previous)==result.signatures.at(record.id),"Mathematical prompt archive changed its original state contract");
            (void)read_block(previous,record.raw_json);
            record.sha256=fingerprint(previous);
            record.semantic_before=math_prompt_semantics(item.GetNamedValue(L"semanticBefore"),record.id,previous);
            math_text_originals.emplace(record.id,previous);result.math_text_revisions.push_back(std::move(record));
        }
    }
    std::map<std::string,std::string_view> math_text_raw,math_current_raw;
    std::set<std::string> math_marked_prompts;
    for(const auto& revision:result.math_text_revisions){
        math_text_raw.emplace(revision.id,revision.raw_json);
        if(revision.semantic_before&&!revision.semantic_before->spans.empty())math_marked_prompts.insert(revision.id);
    }
    const OriginalJson original_document(document_source);
    std::vector<OriginalJson::Slice> original_editorial;
    std::optional<OriginalJson::Slice> original_metadata;
    if(!math_text_originals.empty()){
        const auto document=original_document.root();original_metadata=original_document.member(document,"stateLayout");
        original_editorial=original_document.elements(original_document.member(*original_metadata,"editorial"));
        const auto original_pages=original_document.elements(original_document.member(document,"pages"));
        for(std::size_t p=0;p<result.current.size();++p){
            const auto blocks=original_document.elements(original_document.member(original_pages.at(p),"blocks"));
            for(std::size_t b=0;b<result.current[p].blocks.size();++b){const auto& id=result.current[p].blocks[b];
                if(math_text_originals.contains(id))math_current_raw.emplace(id,original_document.text(blocks.at(b)));}
        }
    }
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
            require(type == "heading" || type == "paragraph" || type == "note" || type == "code" || type == "cpp_compiler" || type == "image" || type == "simulation",
                    "Only compiler activities and their presentation blocks can be retired");
            auto projected=block;
            if(const auto format=source_format_originals.find(retired.id);
                    format!=source_format_originals.end()) {
                projected=source_format_projection(block,format->second);
                const auto& original=format->second;
                if(original.HasKey(L"question")!=block.HasKey(L"question")||
                        (original.HasKey(L"question")&&text(original,"question")!=text(block,"question"))) {
                    // A retired owner no longer has active semantic fields.
                    // Its wording must be authenticated by its own exact
                    // reading snapshot, never by another owner's markers.
                    std::size_t snapshots=0;
                    if(metadata.HasKey(L"retirementHistory"))
                        for(const auto& value:array(metadata,"retirementHistory")) {
                            const auto snapshot=value.GetObject();
                            const auto owners=strings(array(snapshot,"retired"),true,false);
                            if(std::ranges::find(owners,retired.id)==owners.end())continue;
                            require_formatted_question_semantics(snapshot,retired.id,original,block);
                            ++snapshots;
                        }
                    require(snapshots==1,"A retired formatted question needs its own exact reading snapshot");
                }
            }
            require(state_fingerprint(projected) == result.signatures.at(retired.id),"A retired response/code contract changed");
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
    for(const auto& [id,original]:source_format_originals) {
        (void)original;
        require(current_blocks.contains(id)||successors.contains(id),
            "A source-format archive needs its active or retired owner");
    }
    // Historical reading / bibliography checks compare against the immutable
    // pre-format source, never an unreviewed hash of freshly spaced code.
    const auto protected_source=[&](const std::string& owner,const JsonObject& current,bool historical=false) {
        auto projected=case_order_source(owner,current,historical);
        if(const auto it=source_format_originals.find(owner);it!=source_format_originals.end())
            projected=source_format_projection(projected,it->second);
        if(const auto it=math_text_originals.find(owner);it!=math_text_originals.end())
            projected=math_prompt_projection(projected,it->second,historical);
        return projected;
    };
    for(const auto& record:result.compiler_case_order_revisions) {
        const auto previous=JsonObject::Parse(key(record.raw_json));
        const auto expected=revised_compilers.contains(record.id)?
            std::string(revised_compilers.at(record.id)->after):result.signatures.at(record.id);
        require(state_fingerprint(protected_source(record.id,previous,true))==expected,
            "Case-order predecessor changed its original protected owner contract");
    }
    for(const auto& record:result.compiler_case_revisions) {
        const auto previous=JsonObject::Parse(key(record.raw_json));
        const auto expected=revised_compilers.contains(record.id)?
            std::string(revised_compilers.at(record.id)->after):result.signatures.at(record.id);
        require(state_fingerprint(protected_source(record.id,previous,true))==expected,
            "Case-revision predecessor changed its original protected owner contract");
    }
    std::map<std::string, std::string> editorial;
    std::map<std::string,std::set<std::pair<std::string,std::string>>> editorial_edges;
    std::map<std::string, std::set<std::string>> seen;
    std::map<std::string,std::vector<std::string>> editorial_nodes;
    std::map<std::string,JsonObject> return_actual_blocks;
    struct BibliographyEdge {
        std::size_t ordinal=0;std::string id,before,after;JsonObject previous,current;
        std::optional<TextSemantics> previous_semantics,current_semantics;
    };
    std::vector<BibliographyEdge> bibliography_edges;
    const auto collect_actual=[&]{
        if(!return_actual_blocks.empty())return;
        for(std::size_t p=0;p<result.current.size();++p){const auto blocks=array(pages.GetObjectAt(static_cast<unsigned>(p)),"blocks");
            for(std::size_t b=0;b<result.current[p].blocks.size();++b)
                return_actual_blocks.emplace(result.current[p].blocks[b],blocks.GetObjectAt(static_cast<unsigned>(b)));}
        for(const auto& retired:result.retired)return_actual_blocks.emplace(retired.id,JsonObject::Parse(key(retired.raw_json)));
    };
    if (result.version == 2) {
        JsonArray prefix;const auto revisions=array(metadata,"editorial");
        require(revisions.Size()<=100000,"Too many editorial proofs");
        for (const auto& value : revisions) {
            const auto change = value.GetObject();
            allowed_keys(change, {"id", "beforeSha256", "afterSha256", "returnTo", "bibliography"});
            for(const auto field:{L"id",L"beforeSha256",L"afterSha256"})require(change.HasKey(field),"Incomplete editorial record");
            const auto id = text(change, "id"), before = text(change, "beforeSha256"), after = text(change, "afterSha256");
            require(result.signatures.contains(id) && valid_hash(before) && valid_hash(after) && before != after, "Invalid editorial record");
            if (!seen.contains(id)){seen[id].insert(before);editorial_nodes[id].push_back(before);}
            require(!editorial.contains(id) || editorial.at(id) == before,"Broken editorial chain");
            if(change.HasKey(L"bibliography")){
                require(!change.HasKey(L"returnTo"),"A bibliographic revision cannot claim an editorial return");
                const auto archive=change.GetNamedObject(L"bibliography");
                allowed_keys(archive,{"historySha256","previousBlock","currentBlock","previousTextSemantics","currentTextSemantics"},true);
                JsonObject provenance;provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(lesson_id)));
                provenance.Insert(L"locale",JsonValue::CreateStringValue(key(result.locale)));provenance.Insert(L"editorial",prefix);
                const auto history=text(archive,"historySha256");require(valid_hash(history)&&history==fingerprint(provenance),
                    "Bibliographic history differs from its complete ordered prefix");
                const auto previous=archive.GetNamedObject(L"previousBlock"),current=archive.GetNamedObject(L"currentBlock");
                require(fingerprint(previous)==before&&fingerprint(current)==after,"Bibliographic archive hashes do not match its exact block pair");
                require(bibliography_pair(previous,current),"Bibliographic archive changed more than optional citation locators");
                collect_actual();const auto actual=return_actual_blocks.find(id);
                require(actual!=return_actual_blocks.end()&&state_fingerprint(previous)==state_fingerprint(protected_source(id,actual->second)),
                    "Bibliographic archive changed the protected owner contract");
                (void)read_block(previous,utf8(previous.Stringify()));(void)read_block(current,utf8(current.Stringify()));
                bibliography_edges.push_back({prefix.Size(),id,before,after,previous,current,
                    bibliography_semantics(archive.GetNamedValue(L"previousTextSemantics"),id,previous),
                    bibliography_semantics(archive.GetNamedValue(L"currentTextSemantics"),id,current)});
            }
            if(change.HasKey(L"returnTo")){
                const auto target=change.GetNamedObject(L"returnTo");allowed_keys(target,{"revision","historySha256","previousBlock"},true);
                const auto revision=integer(target.GetNamedValue(L"revision"),100000);
                const auto history=text(target,"historySha256");
                const auto& nodes=editorial_nodes.at(id);
                require(seen[id].contains(after)&&revision+1<nodes.size()&&nodes[revision]==after,
                    "An editorial return must identify an earlier occurrence of the same identity");
                JsonObject provenance;
                provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(lesson_id)));
                provenance.Insert(L"locale",JsonValue::CreateStringValue(key(result.locale)));
                provenance.Insert(L"editorial",prefix);
                require(valid_hash(history)&&history==fingerprint(provenance),"Editorial return history differs from its complete ordered prefix");
                const auto previous=target.GetNamedObject(L"previousBlock");
                require(fingerprint(previous)==before,"Editorial return must archive its complete preceding block");
                collect_actual();
                const auto active=return_actual_blocks.find(id);
                require(active!=return_actual_blocks.end()&&text(previous,"type")==text(active->second,"type")
                    &&state_fingerprint(protected_source(id,previous,true))==state_fingerprint(protected_source(id,active->second)),
                    "Editorial return archive changed the current owner's protected contract");
                if(reviewed_intentional_exit_goal(previous))require(reviewed_intentional_exit_goal(active->second),
                    "A published intentional-exit objective cannot be removed by an editorial return");
                if(previous.HasKey(L"compileCheck"))require(active->second.HasKey(L"compileCheck")
                    &&fingerprint(without_compiler_guidance(previous).GetNamedObject(L"compileCheck"))==
                        fingerprint(without_compiler_guidance(active->second).GetNamedObject(L"compileCheck")),
                    "A published compilation diagnostic cannot be removed or replaced by an editorial return");
                if(reviewed_crt_output_original(previous))require(reviewed_crt_output_original(active->second).has_value(),
                    "A published reviewed CRT spelling cannot be removed by an editorial return");
                if(math_text_originals.contains(id)){
                    const auto raw_target=original_document.member(original_editorial.at(prefix.Size()),"returnTo");
                    require_math_prompt_contract(original_document.text(original_document.member(raw_target,"previousBlock")),math_text_raw.at(id),true);
                }
                const auto previous_raw=utf8(previous.Stringify());
                (void)read_block(previous,previous_raw);
                result.editorial_returns.push_back({id,previous_raw});
                result.has_editorial_returns=true;
            }else require(!seen[id].contains(after),"Broken editorial chain");
            seen[id].insert(after); editorial[id] = after;
            editorial_nodes[id].push_back(after);prefix.Append(value);
            editorial_edges[id].emplace(before,after);
        }
    }
    for(const auto& revision:result.math_text_revisions)
        require(editorial.contains(revision.id)&&std::ranges::any_of(editorial_edges.at(revision.id),
                [&](const auto& edge){return edge.first==revision.sha256;}),
            "A mathematical prompt archive needs its exact original editorial predecessor");
    for(const auto& revision:result.source_format_revisions)
        require(editorial.contains(revision.id)&&seen.at(revision.id).contains(revision.sha256),
            "A source format archive needs its original exact editorial predecessor");
    for (const auto& refinement : result.order_refinements)
        require(editorial.contains(refinement.id) && seen.at(refinement.id).contains(refinement.sha256),
            "An ordering refinement needs its complete original published editorial provenance");
    for(const auto& [id,reviewed]:revised_choices)
        require(editorial_edges[id].contains({std::string(reviewed->before_full),std::string(reviewed->first_after_full)}),
            "A reviewed option needs its exact first published editorial edge");
    // The generic edit has no lesson-specific allowlist. It is allowed only
    // when the editorial chain authenticates this exact complete predecessor.
    for(const auto& [id,review]:general_choice_revisions) {
        const auto original=review.GetNamedObject(L"block");
        require(editorial.contains(id)&&
                editorial_edges[id].contains({fingerprint(original),
                    editorial.at(id)}),
            "A general choice revision needs its exact archived editorial edge");
    }
    for(const auto& [id,reviewed]:revised_compilers)
        require(editorial_edges[id].contains({std::string(reviewed->before),std::string(reviewed->after)}),
            "A compiler expansion needs its exact first published editorial edge");
    for(const auto& [id,reviewed]:revised_compiler_references)
        require(editorial_edges[id].contains({std::string(reviewed->reference_before),std::string(reviewed->reference_after)}),
            "An expanded reference needs its exact first published editorial edge");
    for (std::size_t p = 0; p < result.current.size(); ++p) {
        const auto blocks = array(pages.GetAt(static_cast<unsigned>(p)).GetObject(), "blocks");
        require(blocks.Size() == result.current[p].blocks.size(), "State map block count differs");
        for (std::size_t b = 0; b < result.current[p].blocks.size(); ++b) {
            const auto id = result.current[p].blocks[b]; const auto block = blocks.GetAt(static_cast<unsigned>(b)).GetObject();
            const auto projected=protected_source(id,block);
            if (const auto reviewed=reviewed_math_label(projected)) {
                require(result.version == 2 && locale == "es" && lesson_id == reviewed->lesson && id == reviewed->identity,
                    "The reviewed mathematical label belongs to its original Spanish activity");
                if(math_text_originals.contains(id))
                    require(editorial_edges[id].contains({std::string(reviewed->before),std::string(reviewed->after)}),
                        "The reviewed mathematical label needs its exact published editorial edge");
                else require(editorial.contains(id) && editorial.at(id) == reviewed->after && seen.at(id).size() == 2 && seen.at(id).contains(std::string(reviewed->before)),
                    "The reviewed mathematical label needs its exact published editorial proof");
            }
            if(math_text_originals.contains(id)){
                require_math_prompt_contract(math_current_raw.at(id),math_text_raw.at(id));
                require_math_prompt_semantics(root,id,block,math_marked_prompts.contains(id));
            }
            if(const auto prior=source_format_originals.find(id);
                    prior!=source_format_originals.end()) {
                require_formatted_question_semantics(root,id,prior->second,block);
            } else if(block.HasKey(L"explanation")) {
                // An ordinary activity can receive its first explanation
                // without a source-format archive (e.g. U1L4 drag-fill).
                // When the authenticated editorial edge proves that adding
                // this field was the exact change, require its own authored
                // semantic marker. Editorial hashes alone do not grant new
                // readable text ownership.
                const auto type=text(block,"type");
                const bool ordinary=type=="multiple_choice"||type=="fill_code"||
                    type=="output_prediction"||type=="order_code"||
                    type=="drag_fill_code"||type=="inline_fill_code";
                if(ordinary) {
                    auto preceding=JsonObject::Parse(block.Stringify());
                    preceding.Remove(L"explanation");
                    const auto revisions=editorial_edges.find(id);
                    if(revisions!=editorial_edges.end() &&
                            revisions->second.contains({fingerprint(preceding),fingerprint(block)}))
                        require_formatted_question_semantics(root,id,preceding,block);
                }
            }
            const auto general=general_choice_revisions.find(id);
            const auto normalized_choice=general!=general_choice_revisions.end()?
                general_choice_projection(projected,general->second):projected;
            const auto digest=state_fingerprint(normalized_choice);
            validate_choice_pair(projected,reviewed_choice_owner(lesson_id,id));
            if(projected.HasKey(L"compileCheck")) {
                const auto spec=reviewed_compile_check(text(projected.GetNamedObject(L"compileCheck"),"diagnostic"));
                require(spec&&result.version==2&&locale=="es"&&lesson_id==spec->lesson&&id==spec->identity,
                    "A reviewed compilation diagnostic belongs to its original Spanish v2 editor");
                require(editorial.contains(id)&&seen.at(id).contains(std::string(spec->payload_sha256)),
                    "A reviewed compilation diagnostic needs its original published editorial provenance");
            }
            if(reviewed_choice_payload(digest))require(result.version==2&&locale=="es"&&revised_choices.contains(id)&&
                revised_choices.at(id)->lesson==lesson_id,"A reviewed option belongs to its original v2 snapshot and owner");
            if(reviewed_compiler_payload(digest))require(result.version==2&&locale=="es"&&revised_compilers.contains(id)&&
                revised_compilers.at(id)->lesson==lesson_id,"A reviewed compiler expansion belongs to its original v2 snapshot and owner");
            if(reviewed_compiler_reference_payload(digest))require(result.version==2&&locale=="es"&&revised_compiler_references.contains(id)&&
                revised_compiler_references.at(id)->lesson==lesson_id,"An expanded reference belongs to its paired reviewed compiler");
            const auto expected = revised_compiler_references.contains(id)?std::string(revised_compiler_references.at(id)->reference_after):
                revised_compilers.contains(id)?std::string(revised_compilers.at(id)->after):
                revised_choices.contains(id)?std::string(revised_choices.at(id)->after_payload):
                refined_order_signatures.contains(id) ? refined_order_signatures.at(id) : result.signatures.at(id);
            require(digest == expected, "State-bearing block changed without a compatible map");
            if (editorial.contains(id)) require(fingerprint(block) == editorial.at(id), "Stale editorial fingerprint");
        }
    }
    for (const auto& retired : result.retired)
        if (editorial.contains(retired.id)) require(retired.sha256 == editorial.at(retired.id),"Stale retired editorial fingerprint");
    if (metadata.HasKey(L"retirementHistory")) {
        const auto history = array(metadata,"retirementHistory"); require(history.Size() <= 100000,"Too many retirement reading snapshots");
        const auto original_history=original_document.elements(original_document.member(
            original_document.member(original_document.root(),"stateLayout"),"retirementHistory"));
        std::size_t history_index=0;
        std::optional<std::size_t> previous_editorial_prefix;
        std::vector<std::string> covered;
        std::map<std::string,std::string> retired_fingerprints;
        for(const auto& retired:result.retired)retired_fingerprints.emplace(retired.id,retired.sha256);
        std::map<std::string,JsonObject> shells;
        for (std::size_t p=0;p<result.current.size();++p) {
            auto shell=JsonObject::Parse(pages.GetAt(static_cast<unsigned>(p)).Stringify());shell.Remove(L"blocks");
            shells.emplace(result.current[p].id,shell);
        }
        for (const auto& value : history) {
            const auto item=value.GetObject();
            allowed_keys(item,{"retired","current","pages","compilerPuzzlesRequired","referenceAfter","activityAfter","textSemantics","editorialPrefix"});
            for(const auto field:{L"retired",L"current",L"pages",L"compilerPuzzlesRequired",L"referenceAfter",L"activityAfter"})
                require(item.HasKey(field),"Incomplete retirement reading snapshot");
            StateRetirementHistory snapshot;
            std::map<std::string,std::string> snapshot_editorial_heads;
            if(item.HasKey(L"editorialPrefix")) {
                const auto declared=item.GetNamedObject(L"editorialPrefix");
                allowed_keys(declared,{"count","sha256"},true);
                const auto revisions=array(metadata,"editorial");
                snapshot.editorial_prefix=integer(declared.GetNamedValue(L"count"),revisions.Size());
                require(!previous_editorial_prefix||*previous_editorial_prefix<=*snapshot.editorial_prefix,
                    "Retirement editorial prefixes must preserve snapshot chronology");
                previous_editorial_prefix=snapshot.editorial_prefix;
                const auto digest=text(declared,"sha256");
                require(valid_hash(digest),"Invalid retirement editorial prefix fingerprint");
                JsonArray prefix;
                for(unsigned index=0;index<revisions.Size();++index) {
                    const auto proof=revisions.GetObjectAt(index);const auto id=text(proof,"id");
                    snapshot_editorial_heads.try_emplace(id,text(proof,"beforeSha256"));
                    if(index<*snapshot.editorial_prefix) {
                        prefix.Append(proof);
                        snapshot_editorial_heads[id]=text(proof,"afterSha256");
                    }
                }
                JsonObject provenance;
                provenance.Insert(L"lessonId",JsonValue::CreateStringValue(key(lesson_id)));
                provenance.Insert(L"locale",JsonValue::CreateStringValue(key(locale)));
                provenance.Insert(L"editorial",prefix);
                require(digest==fingerprint(provenance),
                    "Retirement editorial prefix differs from its complete published history");
            }
            snapshot.has_text_semantics=item.HasKey(L"textSemantics");
            if(snapshot.has_text_semantics) {
                const auto semantics=item.GetNamedValue(L"textSemantics");
                require(semantics.ValueType()==JsonValueType::Null||semantics.ValueType()==JsonValueType::Object,
                    "Historical text semantics must be an object or null");
                // Check the authored token spelling before WinRT Stringify can
                // turn a fractional/exponent index into an integer.
                require_integer_json_numbers(original_document.text(original_document.member(
                    original_history.at(history_index),"textSemantics")));
            }
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
            std::vector<OriginalJson::Slice> original_pages;
            if(original_metadata)original_pages=original_document.elements(original_document.member(original_history.at(history_index),"pages"));
            ++history_index;
            require(snapshot.current.size()==result.current.size()&&historical_pages.Size()==snapshot.current.size(),"Historical reading page count changed");
            std::set<std::string> snapshot_pages;
            std::map<std::string,std::string> snapshot_contracts;
            for(std::size_t p=0;p<snapshot.current.size();++p) {
                const auto& record=snapshot.current[p];snapshot_pages.insert(record.id);
                require(shells.contains(record.id),"Unknown historical reading page");
                const auto page=historical_pages.GetAt(static_cast<unsigned>(p)).GetObject();
                const auto blocks=array(page,"blocks");require(blocks.Size()==record.blocks.size(),"Historical reading block ownership changed");
                std::vector<OriginalJson::Slice> original_blocks;
                if(original_metadata)original_blocks=original_document.elements(original_document.member(original_pages.at(p),"blocks"));
                auto shell=JsonObject::Parse(page.Stringify());shell.Remove(L"blocks");
                require(fingerprint(shell)==fingerprint(shells.at(record.id)),"Historical reading page metadata changed");
                for(std::size_t b=0;b<record.blocks.size();++b) {
                    const auto& id=record.blocks[b];
                    const auto source=blocks.GetAt(static_cast<unsigned>(b)).GetObject();
                    if(snapshot.editorial_prefix)if(const auto expected=snapshot_editorial_heads.find(id);
                            expected!=snapshot_editorial_heads.end())
                        require(fingerprint(source)==expected->second,
                            "Historical reading source differs from its exact editorial prefix");
                    if(snapshot.has_text_semantics&&unique.contains(id))
                        require(fingerprint(source)==retired_fingerprints.at(id),
                            "A semantic retirement batch must preserve its exact archived block");
                    const auto general=general_choice_revisions.find(id);
                    const auto prompt=math_text_originals.find(id);
                    auto projected=case_order_source(id,source,true,snapshot.editorial_prefix);
                    if(const auto format=source_format_originals.find(id);format!=source_format_originals.end()) {
                        // A reading archive keeps an actual published presentation,
                        // not a newly invented spelling of equivalent source code.
                        require(editorial_nodes.contains(id)&&
                            std::ranges::find(editorial_nodes.at(id),fingerprint(source))!=editorial_nodes.at(id).end(),
                            "A historical formatted source needs an exact occurrence in its owner's editorial chain");
                        // The complete original of a closed expansion/refinement
                        // can precede the formatting archive. Its older starter,
                        // reference calls or grouped cards must remain untouched.
                        const auto previous_version=revised_compilers.contains(id)||
                            revised_compiler_references.contains(id)||refined_order_signatures.contains(id);
                        if(!previous_version||state_fingerprint(source)!=result.signatures.at(id))
                            projected=source_format_projection(projected,format->second);
                    }
                    if(prompt!=math_text_originals.end())projected=math_prompt_projection(projected,prompt->second,true);
                    if(prompt!=math_text_originals.end()){
                        require(editorial_nodes.contains(id)&&std::ranges::find(editorial_nodes.at(id),fingerprint(source))!=editorial_nodes.at(id).end(),
                            "A historical mathematical prompt needs an exact occurrence in its owner's editorial chain");
                        require_math_prompt_contract(original_document.text(original_blocks.at(b)),math_text_raw.at(id),true);
                    }
                    const auto normalized=general!=general_choice_revisions.end()?
                        general_choice_projection(projected,general->second,true):projected;
                    const auto digest = state_fingerprint(normalized);
                    if(source.HasKey(L"compileCheck"))require(editorial_nodes.contains(id)&&
                        std::ranges::find(editorial_nodes.at(id),fingerprint(source))!=editorial_nodes.at(id).end(),
                        "A historical compilation diagnostic needs an exact occurrence in its owner's published editorial chain");
                    validate_choice_pair(projected,reviewed_choice_owner(lesson_id,id));
                    snapshot_contracts.emplace(id,digest);
                    if(reviewed_compiler_payload(digest))require(revised_compilers.contains(id)&&revised_compilers.at(id)->lesson==lesson_id&&locale=="es",
                        "An expanded historical compiler needs its active reviewed snapshot");
                    if(reviewed_compiler_reference_payload(digest))require(revised_compiler_references.contains(id)&&revised_compiler_references.at(id)->lesson==lesson_id&&locale=="es",
                        "An expanded historical reference needs its active compiler archive");
                    require(result.signatures.contains(id) && (digest == result.signatures.at(id) ||
                            (refined_order_signatures.contains(id) && digest == refined_order_signatures.at(id)) ||
                            (revised_choices.contains(id)&&digest==revised_choices.at(id)->after_payload)||
                            (revised_compilers.contains(id)&&digest==revised_compilers.at(id)->after)||
                            (revised_compiler_references.contains(id)&&digest==revised_compiler_references.at(id)->reference_after)),
                            "Historical reading snapshot changed an activity contract");
                }
            }
            for(const auto& [id,reviewed]:revised_compilers)if(!reviewed->reference_identity.empty()) {
                const auto compiler=snapshot_contracts.find(id),reference=snapshot_contracts.find(std::string(reviewed->reference_identity));
                require(compiler!=snapshot_contracts.end()&&reference!=snapshot_contracts.end()&&
                    (compiler->second==reviewed->after)==(reference->second==reviewed->reference_after),
                    "A historical expanded report and its reference must belong to the same published revision");
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
    result.reading_revisions=assignment_reading_revisions(metadata,pages,result,lesson_id);
    if(auto revision=remainder_reading_revision(root,metadata,pages,result,lesson_id))
        result.reading_revisions.push_back(std::move(*revision));
    if(auto revision=session_literal_reading_revision(metadata,pages,result,lesson_id))
        result.reading_revisions.push_back(std::move(*revision));
    if(auto revision=append_hypothesis_reading_revision(metadata,pages,result,lesson_id))
        result.reading_revisions.push_back(std::move(*revision));
    if(auto revision=composed_output_reading_revision(metadata,pages,result,lesson_id))
        result.reading_revisions.push_back(std::move(*revision));
    if(auto revision=composed_feedback_reading_revision(metadata,pages,result,lesson_id))
        result.reading_revisions.push_back(std::move(*revision));
    result.positional_reading_pages=compiler_positional_reading_pages(root,metadata,pages,result,lesson_id);
    if(!bibliography_edges.empty()){
        collect_actual();auto actual=return_actual_blocks;
        std::map<std::string,std::optional<TextSemantics>> semantics;
        const auto current_semantics=[&](const std::string& id,const JsonObject& block){
            std::optional<TextSemantics> found;
            if(root.HasKey(L"textSemantics"))for(const auto& value:array(root.GetNamedObject(L"textSemantics"),"fields")){
                const auto field=value.GetObject();if(text(field,"block")!=id||text(field,"path")!="/explanation")continue;
                require(!found,"Duplicate current bibliographic source semantics");found=bibliography_semantics(field.as<IJsonValue>(),id,block);
            }
            return found;
        };
        StateReadingRevision prior;prior.bibliography_only=true;
        auto edge=bibliography_edges.rbegin();auto count=array(metadata,"editorial").Size();
        while(count&&edge!=bibliography_edges.rend()){
            if(edge->ordinal!=count-1)break;
            const auto owner=actual.find(edge->id);if(owner==actual.end()||fingerprint(owner->second)!=edge->after)break;
            auto stored=semantics.find(edge->id);
            if(stored==semantics.end())stored=semantics.emplace(edge->id,current_semantics(edge->id,owner->second)).first;
            // A later metadata-only revision is valid, but does not justify
            // manufacturing a historical page that used a different overlay.
            if(stored->second!=edge->current_semantics)break;
            owner->second=edge->previous;stored->second=edge->previous_semantics;
            prior.editorial_prefix=--count;prior.previous_blocks[edge->id]=utf8(edge->previous.Stringify());
            prior.previous_explanation_semantics[edge->id]=edge->previous_semantics;
            result.reading_revisions.push_back(prior);++edge;
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
OutputCheck read_output_check(const JsonObject& value,std::string_view original,bool cmake=false) {
    allowed_keys(value, {"inputComparison", "cases"});
    OutputCheck result;
    result.input_comparison = comparison(value.HasKey(L"inputComparison") ? text(value, "inputComparison") : "exact", true);
    const auto cases = array(value, "cases"); require(cases.Size() > 0, "Missing output check cases");
    const OriginalJson source(original);
    const auto original_cases = source.elements(source.member(source.root(),"cases"));
    require(original_cases.size() == cases.Size(), "Output check source cases differ from the parsed document");
    std::size_t case_index = 0;
    for (const auto& case_value : cases) {
        const auto item = case_value.GetObject(); allowed_keys(item, {"standardInput", "outputs", "explanation", "reviewFeedback", "target", "standardError", "standardErrorExact", "exitCode"});
        OutputCase record; record.standard_input = text(item, "standardInput"); record.outputs = strings(array(item, "outputs"), true);
        record.explanation = optional_text(item, "explanation");
        if (item.HasKey(L"exitCode")) {
            // Windows.Data.Json erases the distinction between 1 and 1.0.
            // Inspect the exact authored token as well as its bounded value.
            const auto token = source.text(source.member(original_cases[case_index],"exitCode"));
            require(!token.empty() && std::ranges::all_of(token,[](char c) { return c >= '0' && c <= '9'; }),
                "Runtime exitCode requires an integer from 0 through 255");
            record.exit_code = static_cast<int>(integer(item.GetNamedValue(L"exitCode"),255));
        }
        if (item.HasKey(L"standardError")) {
            require(text(item, "standardError") == "nonBlank", "Unknown runtime stderr expectation");
            record.standard_error = StandardErrorExpectation::non_blank;
        }
        if (item.HasKey(L"standardErrorExact")) {
            require(!item.HasKey(L"standardError"), "Choose either nonBlank or exact stderr, never both");
            record.standard_error_exact = text(item, "standardErrorExact");
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
        ++case_index;
    }
    // The pure evaluator also checks matching ambiguity; reject it at load time.
    for (const auto& item : result.cases) {
        FreshExecution execution{1, true, true, 0, item.standard_input, {}, item.executable};
        (void)evaluate_output_check(result, execution, 1);
    }
    return result;
}
Block read_block(const JsonObject& value,std::string_view original,
    const JsonObject* authenticated_source_before) {
    Block result; result.raw_json = original;
    const auto type = text(value, "type");
    const std::map<std::string, BlockType> types{{"heading", BlockType::heading}, {"paragraph", BlockType::paragraph}, {"note", BlockType::note}, {"code", BlockType::code},
        {"multiple_choice", BlockType::multiple_choice}, {"output_prediction", BlockType::output_prediction}, {"fill_code", BlockType::fill_code},
        {"order_code", BlockType::order_code}, {"cpp_compiler", BlockType::cpp_compiler},
        {"inline_fill_code",BlockType::inline_fill_code}, {"drag_fill_code",BlockType::drag_fill_code}, {"image",BlockType::image}, {"simulation",BlockType::simulation}};
    const std::set<std::string> mathematics{"math_puzzle","math_multiple_choice_single","math_multiple_choice_multi","math_numeric_input",
        "math_multi_input","math_expression_input","math_structured_answer","math_equation_fill_blanks","math_ordering_drag","math_number_line",
        "math_slider_interaction","math_matching_drag","math_coordinate_graph_builder","math_geometry_builder","math_rigid_angle_copy","math_segment_transport"};
    require(types.contains(type) || mathematics.contains(type), "Unsupported block type: " + type);
    result.type = mathematics.contains(type) ? BlockType::math_puzzle : types.at(type);
    if(result.type==BlockType::simulation)result.simulation=read_simulation(value);
    if(result.type==BlockType::image){
        allowed_keys(value,{"type","id","imageId","src","alt","caption"});
        result.image=ImageResource{text(value,"imageId"),text(value,"src"),text(value,"alt"),optional_text(value,"caption")};
        if(value.HasKey(L"id")){
            const auto id=value.GetNamedString(L"id");const std::wstring_view visible(id.c_str(),id.size());
            require(visible.find(L'\0')==visible.npos&&std::ranges::any_of(visible,[](wchar_t c){return !narration_space(c);}),"A present image block id must not be blank");
        }
        if(value.HasKey(L"caption"))require(!result.image->caption.empty(),"A present image caption must not be empty");
        validate_image_resource(*result.image);
    }
    if(value.HasKey(L"compileCheck")) {
        // The stateLayout parser authenticated the complete published source
        // archive and its lexical-equivalent current formatting before this
        // runtime block is constructed. Never authorize an unchecked source.
        if(authenticated_source_before)
            validate_compile_check_payload(source_format_projection(value,*authenticated_source_before));
        else validate_compile_check_payload(value);
    }
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
    if(result.type==BlockType::inline_fill_code)for(const auto* field:{L"reviewFeedback",L"explanation"})if(value.HasKey(field)){
        const auto authored=value.GetNamedString(field);
        const std::wstring_view visible(authored.c_str(),authored.size());
        // CppExerciseFeedback accepts an absent field, but a present string
        // must be nonblank under Kotlin's isWhitespace || isSpaceChar policy.
        // Validate before creating/evaluating a puzzle; preserve authored bytes.
        require(std::ranges::any_of(visible,[](wchar_t character){return !narration_space(character);}),
            "Inline fill reviewFeedback/explanation must not be blank");
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
        if (value.HasKey(L"outputCheck")) {
            const OriginalJson source(original);
            result.output_check = read_output_check(value.GetNamedObject(L"outputCheck"),
                source.text(source.member(source.root(),"outputCheck")),result.cmake.has_value());
        }
        if(value.HasKey(L"compileCheck"))result.compile_check=read_compile_check(value.GetNamedObject(L"compileCheck"));
        if (!result.standard_input && result.output_check && std::ranges::any_of(result.output_check->cases,
            [](const auto& item) { return !item.standard_input.empty(); })) result.standard_input = "";
    }
    return result;
}
// Keep this concrete public-string inventory in sync with text_semantics.py.
// No recursive walk through arbitrary JSON is allowed: validators, answers,
// source programs, IDs and unsupported GDI/native labels are never targets.
struct SemanticPublicFields {
    std::map<std::string,std::string> fields;
    static std::string strict_string(const winrt::hstring& value) {
        for(decltype(value.size()) n=0;n<value.size();++n){const auto unit=static_cast<unsigned>(value[n]);
            if(unit>=0xd800&&unit<=0xdbff){require(n+1<value.size()&&value[n+1]>=0xdc00&&value[n+1]<=0xdfff,
                "Text semantic source contains an invalid Unicode scalar");++n;}
            else require(unit<0xdc00||unit>0xdfff,"Text semantic source contains an invalid Unicode scalar");}
        return utf8(value);
    }
    static JsonArray values(const JsonObject& object,std::string_view name) {
        if(!object.HasKey(key(name)))return {};
        const auto value=object.GetNamedValue(key(name));return value.ValueType()==JsonValueType::Array?value.GetArray():JsonArray{};
    }
    static JsonObject object(const JsonObject& parent,std::string_view name) {
        if(!parent.HasKey(key(name)))return {};
        const auto value=parent.GetNamedValue(key(name));return value.ValueType()==JsonValueType::Object?value.GetObject():JsonObject{};
    }
    static std::string kind(const JsonObject& source,std::string_view fallback={}) {
        if(!source.HasKey(L"type")||source.GetNamedValue(L"type").ValueType()!=JsonValueType::String)return std::string(fallback);
        return text(source,"type");
    }
    void string_field(const JsonObject& source,std::string_view name,const std::string& path) {
        if(source.HasKey(key(name))&&source.GetNamedValue(key(name)).ValueType()==JsonValueType::String)
            fields.emplace(path+'/'+std::string(name),strict_string(source.GetNamedString(key(name))));
    }
    void strings(const JsonArray& source,const std::string& path) {
        for(unsigned n=0;n<source.Size();++n)if(source.GetAt(n).ValueType()==JsonValueType::String)
            fields.emplace(path+'/'+std::to_string(n),strict_string(source.GetStringAt(n)));
    }
    void table(const JsonObject& source,const std::string& path) {
        strings(values(source,"headers"),path+"/headers");const auto rows=values(source,"rows");
        for(unsigned n=0;n<rows.Size();++n)if(rows.GetAt(n).ValueType()==JsonValueType::Array)strings(rows.GetArrayAt(n),path+"/rows/"+std::to_string(n));
    }
    void content(const JsonArray& source,const std::string& path,unsigned depth) {
        require(depth<=32,"Semantic rich content is too deeply nested");
        for(unsigned n=0;n<source.Size();++n){const auto value=source.GetAt(n);const auto location=path+'/'+std::to_string(n);
            if(value.ValueType()==JsonValueType::String)fields.emplace(location,strict_string(value.GetString()));
            else if(value.ValueType()==JsonValueType::Object)representation(value.GetObject(),location,depth+1);}
    }
    void representation(const JsonObject& source,const std::string& path,unsigned depth=0) {
        const auto config=source.HasKey(L"config")?object(source,"config"):source;
        const auto location=path+(source.HasKey(L"config")?"/config":"");const auto type=kind(source,"text");
        if(type=="text"||type=="math_formula"){
            for(const auto name:{"latex","text","formula","value"})if(config.HasKey(key(name))){string_field(config,name,location);break;}
        }else if(type=="math_table")table(config,location);
        else if(type=="rich_answer_option")content(values(config,"content"),location+"/content",depth);
    }
    void card(const JsonObject& source,const std::string& path,bool reviewed=false) {
        const auto body=values(source,"content");
        if(body.Size())content(body,path+"/content",0);
        else for(const auto name:{"label","text","value"})if(source.HasKey(key(name))&&source.GetNamedValue(key(name)).ValueType()==JsonValueType::String){if(!text(source,name).empty())string_field(source,name,path);break;}
        string_field(source,"explanation",path);
        if(reviewed)string_field(source,"reviewFeedback",path);
    }
    void response(const JsonObject& source,const std::string& path) {
        for(const auto name:{"label","instruction"})string_field(source,name,path);
        const auto type=kind(source);const auto definitions=values(source,"fields");
        if((type=="math_numeric_input"||type=="math_expression_input")&&!definitions.Size())string_field(source,"unit",path);
        const auto cards=[&](std::string_view name){const auto entries=values(source,name);for(unsigned n=0;n<entries.Size();++n)
            if(entries.GetAt(n).ValueType()==JsonValueType::Object)card(entries.GetObjectAt(n),path+'/'+std::string(name)+'/'+std::to_string(n),name=="options");};
        if(type=="math_multiple_choice_single"||type=="math_multiple_choice_multi")cards("options");
        else if(type=="math_matching_drag"){cards("sources");cards("targets");}
        else if(type=="math_ordering_drag")cards("items");
        const bool has_fields=type=="math_multi_input"||type=="math_structured_answer"||type=="math_equation_fill_blanks"||
            ((type=="math_numeric_input"||type=="math_expression_input")&&definitions.Size());
        if(!has_fields)return;
        for(unsigned n=0;n<definitions.Size();++n)if(definitions.GetAt(n).ValueType()==JsonValueType::Object){
            const auto entry=definitions.GetObjectAt(n);const auto location=path+"/fields/"+std::to_string(n);
            for(const auto name:{"label","unit"})string_field(entry,name,location);
            if(kind(entry,"numeric")=="fixed")string_field(entry,"text",location);
            if(kind(entry)=="dropdown"){const auto choices=values(entry,"options");for(unsigned option=0;option<choices.Size();++option)
                if(choices.GetAt(option).ValueType()==JsonValueType::Object)string_field(choices.GetObjectAt(option),"reviewFeedback",location+"/options/"+std::to_string(option));}
        }
        const auto segments=values(source,"segments");for(unsigned n=0;n<segments.Size();++n)if(segments.GetAt(n).ValueType()==JsonValueType::Object){
            const auto segment=segments.GetObjectAt(n);if(!segment.HasKey(L"field"))string_field(segment,"text",path+"/segments/"+std::to_string(n));}
        if(!source.HasKey(L"segments")&&source.HasKey(L"layout")&&source.GetNamedValue(L"layout").ValueType()==JsonValueType::String&&
                text(source,"layout")=="table"&&values(source,"rows").Size())table(source,path);
    }
    explicit SemanticPublicFields(const JsonObject& source) {
        const auto type=kind(source);
        if(type=="heading"||type=="paragraph"||type=="note")string_field(source,"text","");
        else if(type=="image")for(const auto name:{"alt","caption"})string_field(source,name,"");
        else if(type=="simulation")for(const auto name:{"title","description"})string_field(source,name,"");
        else if(type=="multiple_choice"||type=="fill_code"||type=="output_prediction"||type=="order_code"||type=="drag_fill_code"||type=="inline_fill_code"){
            for(const auto name:{"question","explanation"})string_field(source,name,"");
            if(type=="multiple_choice")strings(values(source,"options"),"/options");
            string_field(source,"reviewFeedback","");strings(values(source,"reviewFeedback"),"/reviewFeedback");
        }else if(type=="cpp_compiler"){
            string_field(object(source,"compileCheck"),"explanation","/compileCheck");const auto cases=values(object(source,"outputCheck"),"cases");
            for(unsigned n=0;n<cases.Size();++n)if(cases.GetAt(n).ValueType()==JsonValueType::Object){
                const auto entry=cases.GetObjectAt(n);const auto path="/outputCheck/cases/"+std::to_string(n);string_field(entry,"explanation",path);
                const auto reviews=values(entry,"reviewFeedback");for(unsigned r=0;r<reviews.Size();++r)if(reviews.GetAt(r).ValueType()==JsonValueType::Object)
                    string_field(reviews.GetObjectAt(r),"text",path+"/reviewFeedback/"+std::to_string(r));
            }
        }else if(type.starts_with("math_")){
            string_field(source,source.HasKey(L"prompt")?"prompt":"question","");const auto feedback=object(source,"feedback");
            for(const auto name:{"correct","incorrect"})string_field(feedback,name,"/feedback");
            for(const auto name:{"representations","tools"}){const auto entries=values(source,name);for(unsigned n=0;n<entries.Size();++n)
                if(entries.GetAt(n).ValueType()==JsonValueType::Object)representation(entries.GetObjectAt(n),'/'+std::string(name)+'/'+std::to_string(n));}
            if(type=="math_puzzle"){const auto entries=values(source,"responses");for(unsigned n=0;n<entries.Size();++n)
                if(entries.GetAt(n).ValueType()==JsonValueType::Object)response(entries.GetObjectAt(n),"/responses/"+std::to_string(n));}
            else response(source,"");
        }
    }
    static std::string trimmed(const std::string& source){const auto wide=key(source);std::wstring_view value(wide.c_str(),wide.size());
        while(!value.empty()&&narration_space(value.front()))value.remove_prefix(1);while(!value.empty()&&narration_space(value.back()))value.remove_suffix(1);return utf8(winrt::hstring(value));}
    std::set<std::string> feedback_origins(const JsonObject& block) const {
        if(!kind(block).starts_with("math_"))return {};
        std::set<std::string> result;
        for(const auto& [path,value]:fields)if((path=="/feedback/correct"||path=="/feedback/incorrect"||path.ends_with("/reviewFeedback"))&&!trimmed(value).empty())result.insert(path);
        const auto labels=[&](const JsonObject& source,const std::string& path){
            const auto add=[&](const JsonObject& object,const std::string& location){const auto found=fields.find(location+"/label");
                const auto id=location.empty()&&kind(block)!="math_puzzle"?"response":optional_text(object,"id");
                if(found!=fields.end()&&!trimmed(found->second).empty()&&trimmed(found->second)!=id)result.insert(location+"/label");};
            add(source,path);const auto definitions=values(source,"fields");for(unsigned n=0;n<definitions.Size();++n)
                if(definitions.GetAt(n).ValueType()==JsonValueType::Object)add(definitions.GetObjectAt(n),path+"/fields/"+std::to_string(n));};
        if(kind(block)=="math_puzzle"){const auto responses=values(block,"responses");for(unsigned n=0;n<responses.Size();++n)
            if(responses.GetAt(n).ValueType()==JsonValueType::Object)labels(responses.GetObjectAt(n),"/responses/"+std::to_string(n));}
        else labels(block,"");return result;
    }
};
void read_text_semantics(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"textSemantics"))return;
    const OriginalJson original(lesson.raw_json);
    require_integer_json_numbers(original.text(original.member(original.root(),"textSemantics")));
    const auto metadata=root.GetNamedObject(L"textSemantics");allowed_keys(metadata,{"version","fields","rootFields"});
    require(integer(metadata.GetNamedValue(L"version"),1)==1,"Unknown textSemantics version");const auto fields=array(metadata,"fields");
    const auto root_fields=metadata.HasKey(L"rootFields")?array(metadata,"rootFields"):JsonArray{};
    require(fields.Size()<=100000&&root_fields.Size()<=100000-fields.Size(),"Too many text semantic fields");
    if(fields.Size())require(lesson.state_layout.version==2,"Text semantics requires the existing stable stateLayout v2");
    const auto descriptor_for=[&](const JsonObject& field,const std::string& source){
        const auto digest=text(field,"sourceSha256");require(valid_hash(digest)&&digest==sha256(source),"Text semantic source fingerprint is stale");
        TextSemantics descriptor;descriptor.source=source;const auto spans=array(field,"spans");require(spans.Size()<=100000,"Too many text semantic spans");
        for(const auto& entry:spans){const auto span=entry.GetObject();allowed_keys(span,{"start","end","text","role","reading"});
            SemanticSpan result;result.start=integer(span.GetNamedValue(L"start"),16*1024*1024);result.end=integer(span.GetNamedValue(L"end"),16*1024*1024);
            result.text=SemanticPublicFields::strict_string(span.GetNamedString(L"text"));result.role=parse_semantic_role(text(span,"role"));
            if(span.HasKey(L"reading"))result.reading=parse_semantic_reading(text(span,"reading"));descriptor.spans.push_back(std::move(result));}
        validate_text_semantics(descriptor);return descriptor;
    };
    std::map<std::string,Block*> owners;for(auto& page:lesson.pages)for(auto& block:page.blocks)owners.emplace(block.stable_id,&block);
    std::map<std::string,std::map<std::string,std::string>> public_fields;
    for(const auto& value:fields){const auto field=value.GetObject();allowed_keys(field,{"block","path","sourceSha256","spans"},true);
        const auto owner=text(field,"block"),path=text(field,"path");const auto at=owners.find(owner);require(at!=owners.end(),"Unknown text semantic owner");
        auto inventory=public_fields.find(owner);if(inventory==public_fields.end())inventory=public_fields.emplace(owner,
            SemanticPublicFields(JsonObject::Parse(key(at->second->raw_json))).fields).first;
        const auto selected=inventory->second.find(path);require(selected!=inventory->second.end(),"Text semantic path is not a supported public string");
        // Exercise questions and explanations are never valid as authored
        // blank passages. This also covers an explanation newly added to a
        // published drag-fill puzzle WITHOUT a source-format archive.
        const auto type=at->second->type;
        const bool ordinary=type==BlockType::multiple_choice||type==BlockType::fill_code||
            type==BlockType::output_prediction||type==BlockType::order_code||
            type==BlockType::drag_fill_code||type==BlockType::inline_fill_code;
        if(ordinary&&(path=="/question"||path=="/explanation"))
            require(authored_nonblank(selected->second),
                "Authored ordinary public prose cannot be blank");
        require(at->second->text_semantics.emplace(path,descriptor_for(field,selected->second)).second,"Duplicate text semantic field");
    }
    for(const auto& [identity,inventory]:public_fields){const auto& block=*owners.at(identity);const auto raw=JsonObject::Parse(key(block.raw_json));
        const auto group=SemanticPublicFields(raw).feedback_origins(raw);const bool annotated=std::ranges::any_of(group,[&](const auto& path){return block.text_semantics.contains(path);});
        if(annotated)require(std::ranges::all_of(group,[&](const auto& path){return block.text_semantics.contains(path);}),
            "Annotating mathematical feedback requires every possible authored feedback origin in that block");
    }
    for(const auto& value:root_fields){const auto field=value.GetObject();allowed_keys(field,{"path","sourceSha256","spans"},true);
        const auto path=text(field,"path");require(path=="/title"&&root.HasKey(L"title")&&root.GetNamedValue(L"title").ValueType()==JsonValueType::String,
            "Root text semantic path is not a supported public string");
        const auto source=SemanticPublicFields::strict_string(root.GetNamedString(L"title"));
        require(lesson.text_semantics.emplace(path,descriptor_for(field,source)).second,"Duplicate root text semantic field");
    }
}
std::map<std::string,TextSemantics> read_case_order_semantics(
        const JsonObject& before,const JsonArray& fields,const std::string& owner) {
    require(fields.Size()<=100000,"Too many archived case semantic fields");
    for(const auto& value:fields) {
        const auto field=value.GetObject();
        require(text(field,"block")==owner&&text(field,"path").starts_with("/outputCheck/cases/"),
            "Case-order semantics must belong to the archived owner's case fields");
    }
    JsonObject overlay;overlay.Insert(L"version",JsonValue::CreateNumberValue(1));overlay.Insert(L"fields",fields);
    JsonObject root;root.Insert(L"textSemantics",overlay);
    Lesson lesson;lesson.state_layout.version=2;lesson.raw_json=utf8(root.Stringify());
    auto block=read_block(before,utf8(before.Stringify()));block.stable_id=owner;
    Page page;page.blocks.push_back(std::move(block));lesson.pages.push_back(std::move(page));
    read_text_semantics(root,lesson);return std::move(lesson.pages.front().blocks.front().text_semantics);
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
void apply_reviewed_order_equivalences(const Lesson& lesson,Block& block) {
    if(lesson.locale!="es"||lesson.state_layout.version!=2||block.type!=BlockType::order_code)return;
    // CPP_ORDENACION_UNA_LINEA_POR_PIEZA: only the five complete response/code
    // contracts reviewed by Work. A different source, owner or canonical order
    // retains ordinary exact-text evaluation. Editorial prose may still vary.
    struct Reviewed {
        std::string_view lesson,identity,payload;
        std::vector<std::vector<std::size_t>> groups;
    };
    static const std::vector<Reviewed> reviewed{
        {"cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar","block_0002_0001",
            "258a6d4d80faadc66eb13ae8816884516ce6fea5fc214d94ebf8381d916bd794",{{2,3}}},
        {"cpp_fundamentals__colecciones_y_recorridos__acumular_contar_y_seleccionar","block_0005_0004",
            "2bb235af23438d36d2d8767e3ea0f0375e071bff8bbf6187712dd664a4fb856a",{{2,3},{6,7}}},
        {"cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz","orden_validar_antes_de_calcular",
            "db9c0be2b1cbbc53902fc10d467795eef8cb8aca7b76adb523aeac586cd070e0",{{2,3}}},
        {"cpp_fundamentals__excepciones_y_recuperacion__recuperar_por_operacion","block_0002_0003",
            "5671aba2d45f3845d3084c367a057a9f519ae9da8e9b2207c428abd2f4bac513",{{3,4},{6,7},{17,18}}},
        {"cpp_fundamentals__archivos_y_programa_completo__extraer_un_tipo_sin_cambiarlo_en_fallo","block_0004_0003",
            "4380d9b5e34bdda15027e6d92939d391ad4b22864f3ded1dac00d22717111af6",{{2,3,4}}}
    };
    const auto found=std::ranges::find_if(reviewed,[&](const auto& item){return item.lesson==lesson.id&&item.identity==block.stable_id;});
    const auto source=block.source_format_original_fragments
        ?[&]{
            for(const auto& archive:lesson.state_layout.source_format_revisions)
                if(archive.id==block.stable_id)return JsonObject::Parse(key(archive.raw_json));
            return JsonObject::Parse(key(block.raw_json));
        }()
        :JsonObject::Parse(key(block.raw_json));
    if(found!=reviewed.end()&&state_fingerprint(source)==found->payload)
        block.order_equivalence_groups=found->groups;
}
void resolve_addresses(Lesson& lesson) {
    auto& layout = lesson.state_layout;
    const auto intentional_exit_owner=[&](const Block& block) {
        if(block.type!=BlockType::cpp_compiler||block.raw_json.empty())return;
        const auto spec=reviewed_intentional_exit_spec(JsonObject::Parse(key(block.raw_json)));
        if(!spec)return;
        require(layout.version==2&&lesson.locale=="es"&&lesson.id==spec->lesson&&block.stable_id==spec->identity,
            "The reviewed intentional-exit goal belongs to its original Spanish v2 editor");
        const auto metadata=JsonObject::Parse(key(layout.raw_json));
        bool original_edge=false;
        for(const auto& value:array(metadata,"editorial")) {
            const auto proof=value.GetObject();
            original_edge=original_edge||(text(proof,"id")==block.stable_id&&text(proof,"beforeSha256")==spec->before);
        }
        require(original_edge,"The intentional-exit goal requires its exact complete original/editorial provenance");
    };
    const auto crt_output_owner=[&](Block& block) {
        if(block.type!=BlockType::cpp_compiler||block.raw_json.empty())return;
        const auto original=reviewed_crt_output_original(JsonObject::Parse(key(block.raw_json)));
        if(!original)return;
        require(layout.version==2&&lesson.locale=="es"&&lesson.id==crt_output_lesson&&block.stable_id==crt_output_identity,
            "The reviewed CRT spelling belongs to its exact Spanish v2 editor");
        const auto metadata=JsonObject::Parse(key(layout.raw_json));bool exact_edge=false;
        for(const auto& value:array(metadata,"editorial")) {
            const auto proof=value.GetObject();
            exact_edge=exact_edge||(text(proof,"id")==block.stable_id&&text(proof,"beforeSha256")==crt_output_before
                &&text(proof,"afterSha256")==crt_output_after);
        }
        require(exact_edge,"The reviewed CRT spelling requires its exact original-to-reviewed editorial edge");
        block.crt_output_original=*original;
    };
    std::map<std::string, StateAddress> addresses;
    std::map<std::string,const OrderRefinement*> refinements;
    for (const auto& refinement : layout.order_refinements) refinements.emplace(refinement.id,&refinement);
    std::map<std::string,const SourceFormatRevision*> source_formats;
    for (const auto& format : layout.source_format_revisions) source_formats.emplace(format.id,&format);
    std::set<std::string> earlier_source_versions;
    for(const auto& refinement:layout.order_refinements)earlier_source_versions.insert(refinement.id);
    for(const auto& revision:layout.compiler_revisions) {
        earlier_source_versions.insert(revision.id);
        for(const auto& reference:revision.references)earlier_source_versions.insert(reference.id);
    }
    const auto case_order_editorial=(!layout.compiler_case_order_revisions.empty()||!layout.compiler_case_revisions.empty())?
        array(JsonObject::Parse(key(layout.raw_json)),"editorial"):JsonArray{};
    const auto case_order_histories=compiler_case_histories(layout,case_order_editorial,lesson.id);
    const auto original_format_source=[&](const std::string& owner) {
        auto original=JsonObject::Parse(key(source_formats.at(owner)->raw_json));
        if(const auto history=case_order_histories.find(owner);history!=case_order_histories.end()&&
                !history->second.client_paths.empty())
            original=compiler_case_projection(original,history->second,true);
        return original;
    };
    const auto historical_format_source=[&](const std::string& owner,const JsonObject& source,
            bool historical=true,std::optional<std::size_t> editorial_prefix={})->std::optional<JsonObject> {
        const auto format=source_formats.find(owner);
        if(format==source_formats.end())return {};
        // read_layout already authenticated the full historical node and
        // its exact contract. Do not attach a later starter/card identity
        // to a snapshot of the original closed expansion or refinement.
        if(historical&&earlier_source_versions.contains(owner)&&state_fingerprint(source)==layout.signatures.at(owner))return {};
        auto original=original_format_source(owner);
        if(const auto history=case_order_histories.find(owner);history!=case_order_histories.end()&&
                !history->second.client_paths.empty()) {
            const auto epoch=compiler_case_epoch(source,history->second,historical,editorial_prefix);
            for(std::size_t index=0;index<epoch;++index) {
                const auto& version=history->second.versions[index];
                if(!version.client_path.empty())copy_compiler_client(original,version.after,version.client_path);
            }
        }
        return original;
    };
    const auto client_epoch=[&](const std::string& owner,const JsonObject& source,
            std::size_t editorial_prefix) {
        const auto history=case_order_histories.find(owner);
        if(history==case_order_histories.end()||history->second.client_paths.empty())return std::string{};
        const auto epoch=compiler_case_epoch(source,history->second,true,editorial_prefix);
        return epoch?history->second.versions[epoch-1].client_epoch:std::string{};
    };
    const auto apply_source_format=[&](Block& block,const JsonObject& source) {
        const auto original=read_block(source,utf8(source.Stringify()));
        require(original.type==block.type,"A source-format archive changed its activity type");
        if(block.type==BlockType::cpp_compiler) {
            block.source_format_original_initial_code=original.initial_code;
            if(!original.project_files.empty())
                block.source_format_original_project_files=original.project_files;
        } else if(block.type==BlockType::order_code)
            block.source_format_original_fragments=original.fragments;
    };
    std::map<std::string,const MathTextRevision*> math_texts;
    for(const auto& revision:layout.math_text_revisions)math_texts.emplace(revision.id,&revision);
    const auto apply_math_text=[&](Block& block,bool archived){
        if(const auto found=math_texts.find(block.stable_id);found!=math_texts.end()){
            const auto& revision=*found->second;require(block.type==BlockType::math_puzzle,"A mathematical prompt archive changed its owner type");
            block.math_evaluation_raw_json=revision.raw_json;
            if(archived&&revision.semantic_before&&revision.semantic_before->source==block.prompt)
                block.text_semantics.emplace("/prompt",*revision.semantic_before);
        }
    };
    const auto format_precedes=[&](const std::string& owner,std::size_t editorial_index) {
        const auto format=source_formats.find(owner);if(format==source_formats.end())return false;
        for(std::size_t index=0;index<editorial_index;++index) {
            const auto proof=case_order_editorial.GetObjectAt(static_cast<unsigned>(index));
            if(text(proof,"id")==owner&&text(proof,"beforeSha256")==format->second->sha256)return true;
        }
        return false;
    };
    std::map<std::string,std::vector<CompilerCaseOrderRevision>> case_orders;
    for(auto& revision:layout.compiler_case_order_revisions) {
        const auto source=JsonObject::Parse(key(revision.raw_json));
        const auto format=format_precedes(revision.id,revision.editorial_index)?
            historical_format_source(revision.id,source,true,revision.editorial_index):std::optional<JsonObject>{};
        auto previous=read_block(source,revision.raw_json,format?&*format:nullptr);
        if(format)apply_source_format(previous,*format);
        revision.previous_initial_code=previous.initial_code;
        revision.previous_project_files=previous.project_files;
        revision.previous_output_check=*previous.output_check;
        revision.previous_source_format_initial_code=previous.source_format_original_initial_code;
        revision.previous_source_format_project_files=previous.source_format_original_project_files;
        revision.previous_client_epoch=client_epoch(revision.id,source,revision.editorial_index);
        case_orders[revision.id].push_back(revision);
    }
    std::map<std::string,std::vector<CompilerCaseRevision>> case_revisions;
    for(auto& revision:layout.compiler_case_revisions) {
        const auto source=JsonObject::Parse(key(revision.raw_json));
        const auto format=format_precedes(revision.id,revision.editorial_index)?
            historical_format_source(revision.id,source,true,revision.editorial_index):std::optional<JsonObject>{};
        auto previous=read_block(source,revision.raw_json,format?&*format:nullptr);
        if(format)apply_source_format(previous,*format);
        revision.previous_initial_code=previous.initial_code;
        revision.previous_project_files=previous.project_files;
        if(!revision.preserves_root_output_check)revision.previous_output_check=*previous.output_check;
        revision.previous_source_format_initial_code=previous.source_format_original_initial_code;
        revision.previous_source_format_project_files=previous.source_format_original_project_files;
        revision.previous_client_epoch=client_epoch(revision.id,source,revision.editorial_index);
        const auto after_source=JsonObject::Parse(key(revision.after_raw_json));
        revision.revised_client_epoch=client_epoch(revision.id,after_source,revision.editorial_index+1);
        if(revision.client_revision) {
            const auto after_format=format_precedes(revision.id,revision.editorial_index+1)?
                historical_format_source(revision.id,after_source,true,revision.editorial_index+1):std::optional<JsonObject>{};
            auto after=read_block(after_source,revision.after_raw_json,after_format?&*after_format:nullptr);
            if(after_format)apply_source_format(after,*after_format);
            auto& client=*revision.client_revision;
            client.revised_initial_code=after.initial_code;client.revised_project_files=after.project_files;
            client.revised_source_format_initial_code=after.source_format_original_initial_code;
            client.revised_source_format_project_files=after.source_format_original_project_files;
        }
        case_revisions[revision.id].push_back(revision);
    }
    std::map<std::string,ChoiceRevision*> choices;
    std::map<std::string,CompilerRevision*> compilers;
    for(auto& revision:layout.compiler_revisions) {
        const auto original=read_block(JsonObject::Parse(key(revision.raw_json)),revision.raw_json);
        require(original.output_check.has_value(),"An original compiler revision needs its exact output contract");
        revision.previous_initial_code=original.initial_code;
        revision.previous_output_check=*original.output_check;compilers.emplace(revision.id,&revision);
    }
    for(auto& revision:layout.choice_revisions) {
        const auto original=read_block(JsonObject::Parse(key(revision.raw_json)),revision.raw_json);
        revision.original_options=original.options;revision.original_correct_index=original.correct_index;
        revision.original_question=original.question;revision.original_explanation=original.explanation;
        revision.original_review_feedback=original.review_feedback;revision.original_review_feedback_options=original.review_feedback_options;
        choices.emplace(revision.id,&revision);
    }
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
            if(source_formats.contains(block.stable_id))
                if(const auto format=historical_format_source(block.stable_id,JsonObject::Parse(key(block.raw_json)),false))
                    apply_source_format(block,*format);
            if (const auto refinement = refinements.find(block.stable_id); refinement != refinements.end()) {
                require(block.type == BlockType::order_code && sha256(order_representation_frame(block)) == refinement->second->representation_signature,
                    "An ordering refinement belongs to another visible representation");
                block.order_refinement = *refinement->second;
            }
            apply_math_text(block,false);
            apply_reviewed_order_equivalences(lesson,block);
            if(block.type==BlockType::math_puzzle)if(const auto reviewed=reviewed_math_label(JsonObject::Parse(key(math_evaluation_source(block))))){
                require(layout.version==2&&lesson.locale=="es"&&lesson.id==reviewed->lesson&&block.stable_id==reviewed->identity,
                    "A mathematical reading revision belongs to its authenticated Spanish activity");
                const auto prior=JsonObject::Parse(key(math_evaluation_source(block)));
                const auto response=prior.GetNamedArray(L"responses").GetObjectAt(0);
                const auto source=response.GetNamedArray(L"sources").GetObjectAt(reviewed->source_index);
                const auto current_label=text(source,"label");
                source.Insert(L"label",JsonValue::CreateStringValue(key(reviewed->label_before)));
                require(fingerprint(prior)==reviewed->before,"A mathematical reading archive differs from the complete reviewed original");
                block.math_label_revision=MathLabelRevision{text(response,"id")+"/source/"+text(source,"id"),
                    std::string(reviewed->label_before),current_label};
            }
            if(const auto choice=choices.find(block.stable_id);choice!=choices.end()) {
                choice->second->representation_signature=sha256(choice_representation_frame(block));
                block.choice_revision=*choice->second;
            }
            if(const auto compiler=compilers.find(block.stable_id);compiler!=compilers.end())block.compiler_revision=*compiler->second;
            if(const auto history=case_orders.find(block.stable_id);history!=case_orders.end())
                block.compiler_case_order_revisions=history->second;
            if(const auto history=case_revisions.find(block.stable_id);history!=case_revisions.end())
                block.compiler_case_revisions=history->second;
            if(const auto history=case_order_histories.find(block.stable_id);history!=case_order_histories.end())
                block.compiler_client_epoch=history->second.versions.back().client_epoch;
            if(block.compile_check) {
                const auto spec=reviewed_compile_check(block.compile_check->diagnostic);
                require(spec&&lesson.locale=="es"&&layout.version==2&&lesson.id==spec->lesson&&block.stable_id==spec->identity,
                    "A reviewed compilation diagnostic belongs to its original Spanish v2 editor");
            }
            intentional_exit_owner(block);
            crt_output_owner(block);
        }
    }
    for (const auto& retired : layout.retired) {
        const auto source=JsonObject::Parse(key(retired.raw_json));
        const auto format=historical_format_source(retired.id,source);
        auto block=read_block(source,retired.raw_json,format?&*format:nullptr);
        block.stable_id = retired.id; block.address = addresses.at(retired.id);
        if(format)apply_source_format(block,*format);
        apply_math_text(block,true);
        intentional_exit_owner(block);
        crt_output_owner(block);
        if(block.compile_check) {
            const auto spec=reviewed_compile_check(block.compile_check->diagnostic);
            require(spec&&lesson.locale=="es"&&layout.version==2&&lesson.id==spec->lesson&&block.stable_id==spec->identity,
                "An archived compilation diagnostic belongs to its original Spanish v2 editor");
        }
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
                const auto& identity=snapshot.current[p].blocks[b];
                const auto format_source=historical_format_source(identity,source,true,snapshot.editorial_prefix);
                auto block=read_block(source,utf8(source.Stringify()),format_source?&*format_source:nullptr);
                block.stable_id=identity;
                if(format_source)apply_source_format(block,*format_source);
                auto ordered_source=source;
                if(const auto history=case_order_histories.find(identity);history!=case_order_histories.end()) {
                    const auto earlier_closed=compilers.contains(identity)&&
                        state_fingerprint(source)==layout.signatures.at(identity)&&
                        (!snapshot.editorial_prefix||std::ranges::none_of(history->second.versions,
                            [&](const auto& version){return version.editorial_index<*snapshot.editorial_prefix;}));
                    if(!earlier_closed) {
                        const auto epoch=compiler_case_epoch(source,history->second,true,snapshot.editorial_prefix);
                        block.compiler_client_epoch=epoch?history->second.versions[epoch-1].client_epoch:std::string{};
                        for(std::size_t index=0;index<epoch;++index) {
                            const auto& version=history->second.versions[index];
                            if(version.case_revision)
                                block.compiler_case_revisions.push_back(layout.compiler_case_revisions.at(version.journal_index));
                            else
                                block.compiler_case_order_revisions.push_back(layout.compiler_case_order_revisions.at(version.journal_index));
                        }
                        ordered_source=compiler_case_projection(source,history->second,true,snapshot.editorial_prefix);
                    }
                }
                const auto protected_source=format_source?
                    source_format_projection(ordered_source,original_format_source(identity)):ordered_source;
                apply_math_text(block,true);
                intentional_exit_owner(block);
                crt_output_owner(block);
                if(block.crt_output_original) {
                    const auto active=stable_block_position(lesson,block.stable_id);
                    const auto archived_editor=std::ranges::find(lesson.retired_blocks,block.stable_id,&Block::stable_id);
                    const auto* current=active?&lesson.pages[active->page].blocks[active->block]:
                        archived_editor!=lesson.retired_blocks.end()?&*archived_editor:nullptr;
                    require(current&&current->crt_output_original==block.crt_output_original,
                        "A historical reviewed CRT spelling must retain its current archived or active contract");
                }
                if(reviewed_intentional_exit_goal(source)) {
                    const auto active=stable_block_position(lesson,block.stable_id);
                    const auto archived_goal=std::ranges::find(lesson.retired_blocks,block.stable_id,&Block::stable_id);
                    const auto* current=active?&lesson.pages[active->page].blocks[active->block]:
                        archived_goal!=lesson.retired_blocks.end()?&*archived_goal:nullptr;
                    require(current&&reviewed_intentional_exit_goal(JsonObject::Parse(key(current->raw_json))),
                        "A historical intentional-exit objective must retain its current archived or active goal");
                }
                if(block.compile_check) {
                    const auto spec=reviewed_compile_check(block.compile_check->diagnostic);
                    require(spec&&lesson.locale=="es"&&layout.version==2&&lesson.id==spec->lesson&&block.stable_id==spec->identity,
                        "A historical compilation diagnostic belongs to its original Spanish v2 editor");
                    const auto position=stable_block_position(lesson,block.stable_id);
                    const auto archived_objective=std::ranges::find(lesson.retired_blocks,block.stable_id,&Block::stable_id);
                    const auto current=position?&lesson.pages[position->page].blocks[position->block]:
                        archived_objective!=lesson.retired_blocks.end()?&*archived_objective:nullptr;
                    require(current&&current->compile_check&&current->compile_check->diagnostic==block.compile_check->diagnostic,
                        "A historical compilation diagnostic must retain its current archived or active objective");
                }
                // A reading snapshot made before the split retains grouped
                // cards and their original response. Only an already-refined
                // snapshot uses the later composition representation.
                if (const auto refinement = refinements.find(block.stable_id); refinement != refinements.end() &&
                    block.type == BlockType::order_code && sha256(order_representation_frame(block)) == refinement->second->representation_signature)
                    block.order_refinement = *refinement->second;
                if(const auto choice=choices.find(block.stable_id);choice!=choices.end()&&
                    block.type==BlockType::multiple_choice&&sha256(choice_representation_frame(block))==choice->second->representation_signature)
                    block.choice_revision=*choice->second;
                if(const auto compiler=compilers.find(block.stable_id);compiler!=compilers.end()&&block.type==BlockType::cpp_compiler&&
                    state_fingerprint(protected_source)==compiler->second->current_payload)block.compiler_revision=*compiler->second;
                block.address=addresses.at(block.stable_id);page.blocks.push_back(std::move(block));
            }
            snapshot.pages.push_back(std::move(page));
        }
        Lesson previous;previous.id=lesson.id;previous.title=lesson.title;previous.locale=lesson.locale;
        previous.state_layout.version=2;previous.pages=snapshot.pages;previous.compiler_puzzles_required=snapshot.compiler_puzzles_required;
        previous.reference_after=snapshot.reference_after;previous.activity_after=snapshot.activity_after;
        if(snapshot.has_text_semantics) {
            // The snapshot is a complete authoring version, including explicit
            // unmarked fields. Do not mix it with a prompt archive or today's
            // overlay, even when the visible wording happens to be identical.
            for(auto& page:previous.pages)for(auto& block:page.blocks)block.text_semantics.clear();
            if(archived.GetNamedValue(L"textSemantics").ValueType()==JsonValueType::Object) {
                auto semantic_root=JsonObject::Parse(key(snapshot.raw_json));
                semantic_root.Insert(L"title",JsonValue::CreateStringValue(key(previous.title)));
                previous.raw_json=snapshot.raw_json;
                read_text_semantics(semantic_root,previous);
            }
            for(const auto& identity:snapshot.retired) {
                const auto source=stable_block_position(previous,identity);
                const auto retired=std::ranges::find(lesson.retired_blocks,identity,&Block::stable_id);
                require(source&&retired!=lesson.retired_blocks.end(),"Missing semantic retirement owner");
                retired->text_semantics=previous.pages[source->page].blocks[source->block].text_semantics;
            }
            snapshot.pages=previous.pages;
            snapshot.text_semantics=std::move(previous.text_semantics);
        }
        validate_reference_relationships(previous);validate_activity_relationships(previous);
    }
}
}

void validate_course_edition_physical_path(
        const std::filesystem::path& content_root,
        const std::filesystem::path& resource) {
    require_physical_course_edition_asset(content_root, resource);
}

std::optional<curriculum::CourseEdition> parse_course_edition_metadata(
        std::string_view source, std::string_view expected_locale) {
    require(known_locale(expected_locale), "Unsupported asset language");
    ensure_winrt_apartment();
    try {
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        require(source.size() <= 32 * 1024 * 1024, "Course-edition metadata too large");
        return read_course_edition(JsonObject::Parse(key(source)), expected_locale, source);
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Course edition JSON/contract error: " + utf8(error.message()));
    }
}

std::optional<std::string> narration_language_tag(std::string_view authored) {
    return narration_tag(winrt::to_hstring(authored));
}

void read_compiler_workflow_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerWorkflowChecks"))return;
    // Bind the new evidence requirement only after the immutable addresses
    // have been checked. Original editor JSON and stdout receipts stay intact.
    const auto checks=array(root,"compilerWorkflowChecks");
    require(checks.Size()==1,"The U3 History workflow requires a unique owner");
    const auto item=checks.GetObjectAt(0);
    allowed_keys(item,{"activity","kind"},true);
    require(text(item,"activity")=="block_0010_0003"&&
            text(item,"kind")=="cmakeSelectiveRecoveryV1",
            "Unknown compiler workflow owner or kind");
    require(lesson.id=="programming_practice_tools__comprobaciones_repetibles__reorganizar_la_descripcion_conservando_el_comprobador"&&
            lesson.locale=="es"&&lesson.state_layout.version==2&&
            lesson.pages.size()==13&&lesson.pages[10].blocks.size()>3,
            "Selective History belongs to the original Spanish U3 lesson");
    auto& owner=lesson.pages[10].blocks[3];
    require(owner.stable_id=="block_0010_0003"&&owner.type==BlockType::cpp_compiler&&
            owner.output_check&&
            owner.cmake&&owner.cmake->target=="comprobar_cajas"&&
            owner.cmake->build_type=="Debug"&&owner.project_files.size()==6,
            "Declared U3 History owner has an incompatible CMake objective");
    const auto objective=selective_recovery::goal(*owner.output_check);
    require(objective.has_value(),
            "Selective History requires its original report or seven individual cases");
    if(objective->individual) {
        // resolve_addresses has authenticated the complete client journal.
        // Case-only revisions or a later permutation may keep this epoch, but
        // the aggregate client and its old receipts cannot grant this goal.
        const bool reviewed_client=!owner.compiler_client_epoch.empty()&&
            std::ranges::any_of(owner.compiler_case_revisions,[&](const auto& revision) {
                if(revision.id!=owner.stable_id||!revision.client_revision||
                    revision.client_revision->cmake_recipe||revision.preserves_root_output_check||
                    revision.client_revision->path!="pruebas/cajas_casos.cpp"||
                    revision.record_sha256!=owner.compiler_client_epoch||
                    revision.revised_client_epoch!=owner.compiler_client_epoch||
                    revision.previous_client_epoch==owner.compiler_client_epoch)return false;
                // The client transition itself must already describe the
                // individual calls; a later case-only rewrite cannot turn a
                // restored aggregate checker into the reviewed client.
                const auto client_goal=selective_recovery::goal(revision.revised_output_check);
                return client_goal&&client_goal->individual;
            });
        require(reviewed_client,
                "Individual History cases require their authenticated checker-client revision");
    }
    owner.compiler_workflow_check=CompilerWorkflowCheck::cmake_selective_recovery_v1;
}

// Presentation-only, declared at the lesson root; these blank cards do not
// become fragments or answers and never allocate a historical state slot.
// A reviewable root declaration attaches independent ordering groups to the
// already mapped activity. It never rewrites fragments, correctOrder, old
// responses, exercise addresses, or reader progress. Editorial prose can vary;
// a proven lexical-only formatting revision keeps the original source digest,
// while changed C++ tokens never borrow that proof.
void read_order_equivalence_groups(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"orderEquivalenceGroups"))return;
    const auto records=array(root,"orderEquivalenceGroups");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            records.Size()>0&&records.Size()<=16,
        "Independent order groups need a bounded Spanish v2 root declaration");
    std::set<std::string> owners;
    for(const auto& value:records) {
        const auto record=value.GetObject();
        allowed_keys(record,{"activity","sourceSha256","groups"},true);
        const auto identity=text(record,"activity");
        const auto digest=text(record,"sourceSha256");
        require(valid_id(identity)&&owners.insert(identity).second&&valid_hash(digest),
            "Independent order group has an invalid owner or source digest");
        Block* owner=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!owner,"Independent order activity has duplicate mapped owners");
                owner=&block;
            }
        require(owner&&owner->type==BlockType::order_code&&
                owner->fragments.size()>=2&&owner->fragments.size()<=256&&
                owner->correct_order.size()==owner->fragments.size()&&
                !owner->raw_json.empty(),
            "Independent order groups require an existing code-order activity");
        for(const auto& fragment:owner->fragments)
            require(!fragment.empty()&&
                    fragment.find_first_not_of(" \t")!=fragment.npos&&
                    fragment.find_first_of("\r\n")==fragment.npos,
                "An independent order card must contain exactly one physical code line");
        auto canonical=owner->correct_order;
        std::ranges::sort(canonical);
        for(std::size_t i=0;i<canonical.size();++i)
            require(canonical[i]==i,"Independent order groups require a complete canonical answer");
        const auto current=JsonObject::Parse(key(owner->raw_json));
        bool source_matches=state_fingerprint(current)==digest;
        if(!source_matches)for(const auto& archive:lesson.state_layout.source_format_revisions) {
            if(archive.id!=identity)continue;
            // read_layout has already authenticated the source-format archive
            // against the immutable historical owner and the actual current
            // C++ tokens. Preserve an existing root rubric across that
            // whitespace-only revision, never across an unreviewed edit.
            const auto before=JsonObject::Parse(key(archive.raw_json));
            source_matches=state_fingerprint(source_format_projection(current,before))==digest;
            break;
        }
        require(source_matches,
            "Independent groups refer to a different evaluated source contract");
        const auto groups=array(record,"groups");
        require(groups.Size()>0&&groups.Size()<=16,
            "Independent order contract needs between 1 and 16 groups");
        std::set<std::size_t> claimed;
        std::vector<std::vector<std::size_t>> result;
        for(const auto& raw:groups) {
            const auto indices=raw.GetArray();
            require(indices.Size()>=2&&indices.Size()<=8,
                "Independent order groups contain 2..8 physical code cards");
            std::vector<std::size_t> group,positions;
            for(const auto& cell:indices) {
                const auto index=integer(cell,owner->fragments.size());
                require(index<owner->fragments.size()&&claimed.insert(index).second,
                    "Independent cards cannot repeat or leave their original owner");
                const auto location=std::ranges::find(owner->correct_order,index);
                require(location!=owner->correct_order.end(),
                    "An independent card is missing from the canonical response");
                group.push_back(index);
                positions.push_back(static_cast<std::size_t>(
                    std::distance(owner->correct_order.begin(),location)));
            }
            std::ranges::sort(positions);
            for(std::size_t i=1;i<positions.size();++i)
                require(positions[i]==positions.front()+i,
                    "An independent order group must span contiguous canonical slots");
            result.push_back(std::move(group));
        }
        // Existing source-pinned reviews continue working without a new field.
        // A later authored root declaration may confirm the same groups but
        // cannot silently change already accepted permutations.
        require(owner->order_equivalence_groups.empty()||
                owner->order_equivalence_groups==result,
            "A new declaration conflicts with an existing reviewed order");
        owner->order_equivalence_groups=std::move(result);
    }
}

void read_order_visual_separators(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"orderVisualSeparators"))return;
    const auto records=array(root,"orderVisualSeparators");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            records.Size()>0&&records.Size()<=16,
        "Optional code separators require a Spanish v2 source and a bounded declaration");
    std::set<std::string> owners;
    for(const auto& value:records) {
        const auto record=value.GetObject();
        allowed_keys(record,{"activity","count"},true);
        const auto identity=text(record,"activity");
        const auto count=integer(record.GetNamedValue(L"count"),4);
        // Every optional blank card is visual only. Do not let an authored
        // count inflate the Win32 drag surface or allocate arbitrary cards.
        require(valid_id(identity)&&count>=1&&count<=4&&owners.insert(identity).second,
            "Invalid, duplicate or out-of-range optional code separator declaration");
        Block* target=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!target,"An optional separator has multiple owners");
                target=&block;
            }
        require(target&&target->type==BlockType::order_code&&
                !target->fragments.empty()&&
                target->fragments.size()==target->correct_order.size(),
            "Visual separators may be attached only to an existing mapped code-order owner");
        target->optional_order_separators=count;
    }
}


std::optional<CmakeGraphRequirement> bind_cmake_source_integrity(
        const CmakeGraphRequirement& graph,const JsonObject& integrity,
        const CppProjectFiles& files) {
    allowed_keys(integrity,{"preserveNonCmake","removePrefix","replaceExpression"});
    require(integrity.HasKey(L"preserveNonCmake")&&
            integrity.GetNamedValue(L"preserveNonCmake").ValueType()==JsonValueType::Boolean&&
            integrity.GetNamedBoolean(L"preserveNonCmake"),
        "CMake source integrity must explicitly preserve original files");
    require(!(integrity.HasKey(L"removePrefix")&&integrity.HasKey(L"replaceExpression")),
        "CMake source integrity permits only one bounded source repair");
    auto bound=graph;
    bound.source_integrity.clear();bound.expression_repair.reset();
    std::string removal_path,removal;
    if(integrity.HasKey(L"removePrefix")) {
        const auto edit=integrity.GetNamedObject(L"removePrefix");
        allowed_keys(edit,{"path","text"},true);
        removal_path=text(edit,"path");removal=text(edit,"text");
        require(is_cpp_source_file(removal_path)&&!removal.empty()&&removal.size()<=512,
            "CMake source repair must remove a short original C++ prefix");
        const auto found=std::ranges::find_if(files,
            [&](const auto& file){return file.first==removal_path;});
        if(found==files.end()||!found->second.starts_with(removal))return {};
    }
    if(integrity.HasKey(L"replaceExpression")) {
        const auto edit=integrity.GetNamedObject(L"replaceExpression");
        allowed_keys(edit,{"path","original"},true);
        const auto path=text(edit,"path");
        const auto found=std::ranges::find_if(files,
            [&](const auto& file){return file.first==path;});
        if(found==files.end()||!std::ranges::any_of(bound.targets,
                [&](const auto& target) {
                    return std::ranges::find(target.sources,path)!=target.sources.end();
                }))return {};
        CppExpressionRepair repair{path,found->second,text(edit,"original")};
        if(!cpp_expression_repair_valid(repair))return {};
        bound.expression_repair=std::move(repair);
    }
    bool found_removal=removal_path.empty();
    for(const auto& [path,source]:files) {
        const auto at=path.find_last_of('/');
        const auto filename=path.substr(at==path.npos?0:at+1);
        if(filename=="CMakeLists.txt"||path.ends_with(".cmake"))continue;
        if(bound.expression_repair&&path==bound.expression_repair->path)continue;
        const bool edited=path==removal_path;
        if(edited)found_removal=true;
        bound.source_integrity.emplace_back(path,sha256(edited?
            std::string_view(source).substr(removal.size()):std::string_view(source)));
    }
    if(!found_removal||(bound.source_integrity.empty()&&!bound.expression_repair))return {};
    return bound;
}

void read_compiler_cmake_graph_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerCmakeGraphChecks"))return;
    const auto records=array(root,"compilerCmakeGraphChecks");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            records.Size()>0&&records.Size()<=64,
        "CMake graph checks require Spanish v2 and 1..64 mapped editors");
    const auto target_name=[](std::string_view value) {
        return !value.empty()&&value.size()<=128&&
            std::ranges::all_of(value,[](unsigned char c) {
                return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                    (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='+';
            });
    };
    std::set<std::string> owners;
    for(const auto& record:records) {
        const auto item=record.GetObject();
        allowed_keys(item,{"activity","project","minimumCmakeVersion","minimumCmakeVersionAtLeast","targets",
            "optionSelection","sourceIntegrity","configureMessageOrder",
            "targetList","initialObservation"});
        require(item.HasKey(L"activity")&&item.HasKey(L"targets"),
            "CMake graph check requires activity and targets");
        const auto identity=text(item,"activity");
        require(valid_id(identity)&&owners.insert(identity).second,
            "Repeated or invalid CMake graph activity");
        Block* owner=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!owner,"Duplicate CMake graph owner in the mapped lesson");
                owner=&block;
            }
        require(owner&&owner->type==BlockType::cpp_compiler&&
                owner->cmake&&owner->output_check&&
                !owner->output_check->cases.empty()&&!owner->compile_check&&
                !owner->project_files.empty(),
            "CMake graph checks require an existing output-check CMake editor");
        CmakeGraphRequirement requirement;
        if(item.HasKey(L"project")) {
            requirement.project_name=text(item,"project");
            require(requirement.project_name.size()<=128,
                "CMake project name is too long");
        }
        if(item.HasKey(L"minimumCmakeVersion")) {
            requirement.minimum_cmake_version=text(item,"minimumCmakeVersion");
            require(curriculum::cmake_numeric_version(
                requirement.minimum_cmake_version).has_value(),
                "CMake minimum requires a numeric major.minor version");
        }
        if(item.HasKey(L"minimumCmakeVersionAtLeast")) {
            const auto value=item.GetNamedValue(L"minimumCmakeVersionAtLeast");
            require(value.ValueType()==JsonValueType::Boolean&&
                    item.HasKey(L"minimumCmakeVersion"),
                "CMake minimum comparison requires a Boolean and numeric minimum");
            requirement.minimum_cmake_version_at_least=value.GetBoolean();
        }
        if(item.HasKey(L"configureMessageOrder")) {
            const auto messages=array(item,"configureMessageOrder");
            require(messages.Size()>0&&messages.Size()<=16,
                "CMake order needs 1..16 original STATUS messages");
            for(const auto& raw:messages) {
                const auto value=raw.GetObject();
                allowed_keys(value,{"path","text"},true);
                const auto path=text(value,"path");
                const auto expected=text(value,"text");
                require(!expected.empty()&&expected.size()<=256&&
                        std::ranges::all_of(expected,[](unsigned char c) {
                            return c>=32&&c!=127;
                        })&&
                        std::ranges::any_of(owner->project_files,
                            [&](const auto& file) {
                                return file.first==path&&
                                    (path=="CMakeLists.txt"||
                                     path.ends_with("/CMakeLists.txt")||
                                     path.ends_with(".cmake"));
                            }),
                    "STATUS message must belong to an original CMake file");
                requirement.configure_message_order.emplace_back(path,expected);
            }
        }
        const auto targets=array(item,"targets");
        require(targets.Size()>0&&targets.Size()<=16,
            "CMake graph requires 1..16 generated targets");
        std::set<std::string> unique;
        for(const auto& t:targets) {
            const auto source=t.GetObject();
            allowed_keys(source,{"name","type","sources","linksTo","exactSources",
                "explicitCxx20","explicitCxxExtensionsOff","requireExplicitMsvcWarnings",
                "publicIncludeRoots","privateIncludeRoots","sourceDirectory",
                "authoredCompileOptions"});
            require(source.HasKey(L"name")&&source.HasKey(L"type"),
                "Each CMake graph target requires a name and type");
            CmakeRequiredTarget wanted;
            wanted.name=text(source,"name");
            wanted.type=text(source,"type");
            require(target_name(wanted.name)&&unique.insert(wanted.name).second&&
                    (wanted.type=="EXECUTABLE"||wanted.type=="STATIC_LIBRARY"),
                "Invalid or duplicated CMake executable/static-library target");
            if(source.HasKey(L"sources")) {
                wanted.sources=strings(array(source,"sources"),false,false);
                require(wanted.sources.size()<=32,
                    "Too many required compiled sources");
                std::set<std::string> seen;
                for(const auto& path:wanted.sources)
                    require(is_cpp_source_file(path)&&seen.insert(path).second&&
                            std::ranges::any_of(owner->project_files,
                                [&](const auto& file){return file.first==path;}),
                        "Required CMake sources must be unique existing C++ files");
            }
            if(source.HasKey(L"linksTo")) {
                wanted.links_to=strings(array(source,"linksTo"),false,false);
                require(wanted.links_to.size()<=16,
                    "Too many CMake library link requirements");
                std::set<std::string> seen;
                for(const auto& target:wanted.links_to)
                    require(target_name(target)&&target!=wanted.name&&
                            seen.insert(target).second,
                        "CMake link targets must be unique and different from their consumer");
            }
            if(source.HasKey(L"exactSources")) {
                const auto value=source.GetNamedValue(L"exactSources");
                require(value.ValueType()==JsonValueType::Boolean,
                    "CMake exactSources must be a Boolean");
                wanted.exact_sources=value.GetBoolean();
                require(!wanted.exact_sources||!wanted.sources.empty(),
                    "An exact CMake source requirement cannot be empty");
            }
            for(const auto [key,flag]:{
                    std::pair{L"explicitCxx20",&wanted.require_explicit_cxx20},
                    std::pair{L"explicitCxxExtensionsOff",
                              &wanted.require_explicit_extensions_off},
                    std::pair{L"requireExplicitMsvcWarnings",
                              &wanted.require_explicit_msvc_warnings}}) {
                if(!source.HasKey(key))continue;
                const auto value=source.GetNamedValue(key);
                require(value.ValueType()==JsonValueType::Boolean,
                    "CMake explicit toolchain requirements must be Boolean");
                *flag=value.GetBoolean();
            }
            const auto valid_include_root=[](std::string_view path) {
                if(path.empty()||path.size()>256||path.back()=='/'||
                   path.front()=='/')return false;
                std::size_t start=0;
                for(;;) {
                    const auto end=path.find('/',start);
                    const auto part=path.substr(start,end==path.npos?
                        path.npos:end-start);
                    if(part.empty()||part=="."||part==".."||
                       !std::ranges::all_of(part,[](unsigned char ch) {
                           return (ch>='a'&&ch<='z')||
                               (ch>='A'&&ch<='Z')||
                               (ch>='0'&&ch<='9')||
                               ch=='_'||ch=='-'||ch=='.'||ch=='+';
                       }))return false;
                    if(end==path.npos)break;
                    start=end+1;
                }
                return true;
            };
            for(const auto [key,roots]:{
                    std::pair{L"publicIncludeRoots",&wanted.public_include_roots},
                    std::pair{L"privateIncludeRoots",&wanted.private_include_roots}}) {
                if(!source.HasKey(key))continue;
                *roots=strings(source.GetNamedArray(key),false,false);
                require(wanted.type=="STATIC_LIBRARY"&&
                        !roots->empty()&&roots->size()<=8,
                    "CMake include roots require 1..8 library-owned paths");
                std::set<std::string> seen_roots;
                for(const auto& path:*roots)
                    require(valid_include_root(path)&&
                            seen_roots.insert(path).second&&
                            std::ranges::any_of(owner->project_files,
                                [&](const auto& file) {
                                    return file.first.starts_with(path+'/')&&
                                        (file.first.ends_with(".h")||
                                         file.first.ends_with(".hpp")||
                                         file.first.ends_with(".hxx"));
                                }),
                        "CMake include roots must contain original headers");
            }
            for(const auto& root:wanted.public_include_roots)
                require(std::ranges::find(wanted.private_include_roots,root)==
                    wanted.private_include_roots.end(),
                    "CMake root cannot be simultaneously PUBLIC and PRIVATE");
            if(source.HasKey(L"authoredCompileOptions")) {
                wanted.authored_compile_options=strings(
                    array(source,"authoredCompileOptions"),false,false);
                require(!wanted.authored_compile_options.empty()&&
                        wanted.authored_compile_options.size()<=8,
                    "CMake requires 1..8 authored compiler options");
                std::set<std::string> seen_options;
                for(const auto& option:wanted.authored_compile_options)
                    require(option.size()>=2&&option.size()<=64&&
                            (option.front()=='/'||option.front()=='-')&&
                            ((option[1]>='a'&&option[1]<='z')||
                             (option[1]>='A'&&option[1]<='Z'))&&
                            std::ranges::all_of(option.substr(2),
                                [](unsigned char c) {
                                    return (c>='a'&&c<='z')||
                                        (c>='A'&&c<='Z')||
                                        (c>='0'&&c<='9')||
                                        c=='_'||c=='.'||c==':'||
                                        c=='+'||c=='-';
                                })&&seen_options.insert(option).second,
                        "CMake compiler options must be distinct safe flags");
            }
            if(source.HasKey(L"sourceDirectory")) {
                wanted.source_directory=text(source,"sourceDirectory");
                const auto path=wanted.source_directory;
                require(wanted.type=="STATIC_LIBRARY"&&!path.empty()&&
                        path.size()<=256&&path.front()!='/'&&path.back()!='/'&&
                        std::ranges::all_of(path,[](unsigned char ch) {
                            return (ch>='a'&&ch<='z')||
                                (ch>='A'&&ch<='Z')||
                                (ch>='0'&&ch<='9')||
                                ch=='_'||ch=='-'||ch=='.'||ch=='+'||
                                ch=='.'||ch=='/';
                        })&&
                        std::ranges::none_of(std::filesystem::path(path),
                            [](const auto& part) {
                                return part=="."||part=="..";
                            })&&
                        std::ranges::any_of(owner->project_files,
                            [&](const auto& file) {
                                return file.first==path+"/CMakeLists.txt";
                            }),
                    "CMake target sourceDirectory must be an original subproject");
            }
            requirement.targets.push_back(std::move(wanted));
        }
        for(const auto& library:requirement.targets)if(
                !library.public_include_roots.empty()||
                !library.private_include_roots.empty())
            require(std::ranges::any_of(requirement.targets,
                [&](const auto& consumer) {
                    return consumer.type=="EXECUTABLE"&&
                        std::ranges::find(consumer.links_to,library.name)!=
                            consumer.links_to.end();
                }),"Include scopes require an explicitly linked executable");
        if(item.HasKey(L"targetList")) {
            const auto list=item.GetNamedObject(L"targetList");
            allowed_keys(list,{"targets","requireAppend"},true);
            CmakeTargetListRequirement goal;
            goal.targets=strings(array(list,"targets"),true,false);
            require(goal.targets.size()>=2&&goal.targets.size()<=16,
                "CMake list requires at least two executable consumers");
            const auto flag=list.GetNamedValue(L"requireAppend");
            require(flag.ValueType()==JsonValueType::Boolean,
                "CMake requireAppend must be Boolean");
            goal.require_append=flag.GetBoolean();
            std::set<std::string> seen_members;
            for(const auto& name:goal.targets) {
                require(seen_members.insert(name).second,
                    "CMake list must not duplicate consumers");
                const auto it=std::ranges::find_if(requirement.targets,
                    [&](const auto& t){return t.name==name;});
                require(it!=requirement.targets.end()&&
                        it->type=="EXECUTABLE"&&
                        it->require_explicit_extensions_off,
                    "CMake list must name executable targets with explicit OFF");
            }
            requirement.target_list=std::move(goal);
        }
        const auto selected=std::ranges::find_if(requirement.targets,
            [&](const auto& target){return target.name==owner->cmake->target;});
        require(selected!=requirement.targets.end()&&selected->type=="EXECUTABLE",
            "A CMake graph check must include its selected executable target");
        for(const auto& target:requirement.targets)
            for(const auto& dependency:target.links_to) {
                const auto found=std::ranges::find_if(requirement.targets,
                    [&](const auto& item){return item.name==dependency;});
                require(found!=requirement.targets.end()&&
                        found->type=="STATIC_LIBRARY"&&
                        target.type=="EXECUTABLE",
                    "CMake links must reference a declared static library from an executable");
            }
        if(item.HasKey(L"optionSelection")) {
            const auto option=item.GetNamedObject(L"optionSelection");
            allowed_keys(option,{"name","default","target","offSource","offStdout"},true);
            require(option.HasKey(L"name")&&option.HasKey(L"default")&&
                    option.HasKey(L"target")&&option.HasKey(L"offSource")&&
                    option.HasKey(L"offStdout"),
                "Binary option requires name, default, target, source and output");
            const auto name=text(option,"name");
            const auto target=text(option,"target");
            const auto off_source=text(option,"offSource");
            const auto off_output=text(option,"offStdout");
            require(target_name(name)&&name.size()<=128&&
                    text(option,"default")=="OFF"&&
                    off_output.size()<=1024*1024&&
                    owner->standard_input.value_or("").empty()&&
                    owner->cmake->options.contains(name)&&
                    owner->cmake->options.at(name)=="ON",
                "Binary option requires an ON editor with authored OFF default");
            const auto library=std::ranges::find_if(requirement.targets,
                [&](const auto& item){return item.name==target;});
            require(library!=requirement.targets.end()&&
                    library->type=="STATIC_LIBRARY"&&
                    library->exact_sources&&!library->sources.empty()&&
                    std::ranges::find(library->sources,off_source)==
                        library->sources.end()&&is_cpp_source_file(off_source)&&
                    std::ranges::any_of(owner->project_files,
                        [&](const auto& file){return file.first==off_source;}),
                "Binary option must switch between distinct original library files");
            requirement.option_selection=CmakeOptionSelectionRequirement{
                name,target,off_source,off_output};
        }
        if(item.HasKey(L"sourceIntegrity")) {
            auto bound=bind_cmake_source_integrity(requirement,
                item.GetNamedObject(L"sourceIntegrity"),owner->project_files);
            require(bound.has_value(),
                "CMake source integrity must bind an authentic bounded repair and original non-CMake files");
            requirement=std::move(*bound);
        }
        if(item.HasKey(L"initialObservation")) {
            require(!owner->compiler_workflow_check&&!owner->named_rule_source_check&&
                    !owner->open_battery_probe&&!owner->boolean_battery_probe&&
                    !owner->reduction_probe&&!owner->integer_battery_probe&&
                    !owner->adjustment_project_probe&&
                    !owner->output_check->rejection_operation&&
                    !owner->output_check->observation_report&&
                    !owner->output_check->assertion_check,
                "CMake initial observation cannot share its owner with another runtime or source prerequisite");
            const auto authored=item.GetNamedObject(L"initialObservation");
            allowed_keys(authored,{"executable","input","output","exitCode","warning"});
            require(authored.HasKey(L"executable")&&authored.HasKey(L"input")&&
                    authored.HasKey(L"output")&&authored.HasKey(L"exitCode"),
                "CMake initial observation needs its executable, input, output and normal failure code");
            CmakeInitialObservation observation;
            observation.executable=text(authored,"executable");
            observation.standard_input=text(authored,"input");
            observation.standard_output=text(authored,"output");
            const auto code=authored.GetNamedValue(L"exitCode");
            require(code.ValueType()==JsonValueType::Number,
                "CMake initial failure exit must be an integer in 1..255");
            const auto number=code.GetNumber();
            require(std::isfinite(number)&&std::trunc(number)==number&&
                    number>=1&&number<=255,
                "CMake initial failure exit must be an integer in 1..255");
            observation.exit_code=static_cast<std::uint32_t>(number);
            if(authored.HasKey(L"warning")) {
                const auto warning=authored.GetNamedObject(L"warning");
                allowed_keys(warning,{"code","path"},true);
                observation.warning=CmakeInitialWarning{
                    text(warning,"code"),text(warning,"path")};
            }
            requirement.initial_observation=std::move(observation);
            require(cmake_initial_observation_valid(requirement)&&
                    std::ranges::any_of(owner->output_check->cases,
                        [&](const auto& output) {
                            return output.executable.value_or(owner->cmake->target)==
                                requirement.initial_observation->executable;
                        }),
                "CMake initial observation needs a protected expression, an existing checker and source-owned MSVC warnings");
        }
        if(owner->adjustment_project_probe)
            require(adjustment_project_graph_valid(*owner->adjustment_project_probe,requirement),
                "Adjustment construction requires its complete shared CMake graph");
        owner->cmake_graph_check=std::move(requirement);
        if(item.HasKey(L"sourceIntegrity")) {
            const auto integrity=item.GetNamedObject(L"sourceIntegrity");
            // Hash the complete authenticated endpoint, including any client
            // source review. Reusing the current hashes hides old receipts.
            // A newly added repair may not exist in an earlier source; leave
            // that endpoint's stronger current contract intact in that case.
            for(auto& revision:owner->compiler_case_order_revisions)
                revision.previous_cmake_graph_check=bind_cmake_source_integrity(
                    *owner->cmake_graph_check,integrity,revision.previous_project_files);
            for(auto& revision:owner->compiler_case_revisions) {
                revision.previous_cmake_graph_check=bind_cmake_source_integrity(
                    *owner->cmake_graph_check,integrity,revision.previous_project_files);
                revision.revised_cmake_graph_check=bind_cmake_source_integrity(
                    *owner->cmake_graph_check,integrity,revision.client_revision?
                        revision.client_revision->revised_project_files:revision.previous_project_files);
            }
        }
    }
}

void read_compiler_named_rule_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerNamedRuleChecks"))return;
    const auto records=array(root,"compilerNamedRuleChecks");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            records.Size()>0&&records.Size()<=64,
        "Named-rule source contracts require Spanish v2 and 1..64 mapped owners");
    std::set<std::string> owners;
    const auto identifier=[](std::string_view value) {
        if(value.empty()||value.size()>128)return false;
        const auto first=static_cast<unsigned char>(value.front());
        if(!((first>='a'&&first<='z')||(first>='A'&&first<='Z')||first=='_'))
            return false;
        return std::ranges::all_of(value,[](unsigned char c){
            return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||
                (c>='0'&&c<='9')||c=='_';
        });
    };
    for(const auto& entry:records) {
        const auto item=entry.GetObject();
        allowed_keys(item,{"activity","path","rules","editablePaths",
            "commentOnlyPaths","executionProbe"});
        require(item.HasKey(L"activity")&&item.HasKey(L"path")&&item.HasKey(L"rules"),
            "Named-rule source requires activity, path and rules");
        const auto identity=text(item,"activity");
        require(valid_id(identity)&&owners.insert(identity).second,
            "Repeated or invalid named-rule activity");
        Block* owner=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!owner,"Named-rule activity has duplicate native owners");
                owner=&block;
            }
        require(owner&&owner->type==BlockType::cpp_compiler&&owner->output_check&&
                !owner->output_check->cases.empty()&&!owner->compile_check,
            "Named-rule prerequisite requires a mapped output-check editor");
        NamedRuleSourceCheck check;
        check.path=text(item,"path");
        if(owner->project_files.empty()) {
            require(check.path.empty()&&!owner->cmake&&!owner->initial_code.empty(),
                "A single-file named-rule source must have an empty path");
        }else {
            require(owner->cmake&&is_cpp_source_file(check.path)&&
                    std::ranges::any_of(owner->project_files,
                        [&](const auto& f){return f.first==check.path;}),
                "Named-rule project source is not an existing authored C++ file");
        }
        if(item.HasKey(L"editablePaths")) {
            auto more=strings(array(item,"editablePaths"),false,false);
            require(more.size()<=32,"Too many optional named-rule editable files");
            std::set<std::string> seen;
            for(const auto& candidate:more)
                require(!check.path.empty()&&candidate!=check.path&&
                    seen.insert(candidate).second&&
                    std::ranges::any_of(owner->project_files,
                        [&](const auto& file){return file.first==candidate;}),
                    "An additional named-rule edit must belong to an original project file");
            check.editable_paths=std::move(more);
        }
        if(item.HasKey(L"commentOnlyPaths")) {
            auto limited=strings(array(item,"commentOnlyPaths"),false,false);
            require(limited.size()<=32,
                "Too many comment-only named-rule source files");
            std::set<std::string> unique;
            for(const auto& candidate:limited) {
                const bool header=candidate.ends_with(".h")||
                    candidate.ends_with(".hpp")||candidate.ends_with(".hxx");
                require(header&&unique.insert(candidate).second&&
                        std::ranges::find(check.editable_paths,candidate)!=
                        check.editable_paths.end(),
                    "Comment-only files must be unique, editable original C++ headers");
            }
            check.comment_only_paths=std::move(limited);
        }
        const auto rules=array(item,"rules");
        require(rules.Size()>0&&rules.Size()<=8,
            "Named-rule source must declare between one and eight functions");
        std::set<std::string> functions;
        for(const auto& rule_value:rules) {
            const auto rule=rule_value.GetObject();
            allowed_keys(rule,{"function","value","purposeWords","minUses"},true);
            NamedRuleRequirement declaration;
            declaration.function=text(rule,"function");
            require(identifier(declaration.function)&&
                    functions.insert(declaration.function).second,
                "Duplicate or invalid named-rule function");
            const auto value=rule.GetNamedValue(L"value");
            require(value.ValueType()==JsonValueType::Number,
                "Named-rule source value must be an integer");
            const auto number=value.GetNumber();
            require(std::isfinite(number)&&std::trunc(number)==number&&
                    number>=static_cast<double>(std::numeric_limits<int>::min())&&
                    number<=static_cast<double>(std::numeric_limits<int>::max()),
                "Named-rule integer outside representable signed range");
            declaration.value=static_cast<int>(number);
            declaration.min_uses=integer(rule.GetNamedValue(L"minUses"),8);
            require(declaration.min_uses>0,
                "Named-rule source requires at least one meaningful use");
            declaration.purpose_words=strings(array(rule,"purposeWords"),true,false);
            require(declaration.purpose_words.size()<=12,
                "Named-rule purpose vocabulary is too large");
            std::set<std::string> unique;
            for(const auto& word:declaration.purpose_words) {
                const bool acceptable=word.size()>=3&&word.size()<=32&&
                    std::ranges::all_of(word,[](unsigned char c) {
                        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
                    });
                require(acceptable&&unique.insert(word).second,
                    "Invalid or duplicate named-rule purpose term");
            }
            check.requirements.push_back(std::move(declaration));
        }
        if(item.HasKey(L"executionProbe")) {
            const bool single_file=owner->project_files.empty();
            const auto declaration=item.GetNamedObject(L"executionProbe");
            allowed_keys(declaration,{"harness","header","cases","batteryCheck",
                "constantVariations"});
            require(declaration.HasKey(L"harness")&&declaration.HasKey(L"header")&&
                    declaration.HasKey(L"cases"),
                "Independent named-rule probes require harness, header and cases");
            NamedRuleExecutionProbe probe;
            probe.harness_path=text(declaration,"harness");
            probe.header=text(declaration,"header");
            if(single_file) {
                require(!owner->cmake&&check.path.empty()&&
                        probe.harness_path.empty()&&probe.header.empty(),
                    "Single-file independent probes require empty harness and header");
            }else{
                require(owner->cmake&&!check.path.empty(),
                    "Project named-rule probes require a CMake editor");
                require(probe.harness_path!=check.path&&
                        is_cpp_source_file(probe.harness_path)&&
                        std::ranges::any_of(owner->project_files,
                            [&](const auto& file){return file.first==probe.harness_path;}),
                    "Independent test harness must replace an existing different C++ file");
                // The old target must build the harness being replaced.
                const auto cml=std::ranges::find_if(owner->project_files,
                    [](const auto& file){return file.first=="CMakeLists.txt";});
                require(cml!=owner->project_files.end()&&
                        cml->second.find("add_executable("+owner->cmake->target+
                            " "+probe.harness_path+")")!=std::string::npos,
                    "Probe cannot target a file outside the original reviewed executable");
            }
            const auto cases=array(declaration,"cases");
            require(cases.Size()>0&&cases.Size()<=32,
                "Independent named-rule probe needs between 1 and 32 comparisons");
            std::set<std::string> covered;
            const auto bounded_signed=[](const IJsonValue& value) {
                require(value.ValueType()==JsonValueType::Number,
                    "Independent probe arguments must be JSON integers");
                const auto number=value.GetNumber();
                require(std::isfinite(number)&&std::trunc(number)==number&&
                        number>=static_cast<double>(std::numeric_limits<int>::min())&&
                        number<=static_cast<double>(std::numeric_limits<int>::max()),
                    "Independent probe integer is outside signed C++ range");
                return static_cast<int>(number);
            };
            for(const auto& cv:cases) {
                const auto one=cv.GetObject();
                allowed_keys(one,{"function","input","expected"},true);
                NamedRuleProbeCase item_case;
                item_case.function=text(one,"function");
                require(functions.contains(item_case.function),
                    "An independent probe must call an authored named-rule function");
                item_case.input=bounded_signed(one.GetNamedValue(L"input"));
                item_case.expected=bounded_signed(one.GetNamedValue(L"expected"));
                covered.insert(item_case.function);
                probe.cases.push_back(std::move(item_case));
            }
            const auto generated=single_file?
                named_rule_single_file_probe_source(probe,owner->initial_code):
                named_rule_probe_source(probe);
            require(covered==functions&&generated.has_value(),
                "Every named function needs independent, unambiguous executable cases");
            if(declaration.HasKey(L"batteryCheck")) {
                const auto& first=owner->output_check->cases.front();
                require(owner->standard_input.value_or("").empty()&&
                        first.standard_input.empty(),
                    "Named-rule batteries require empty initial and first-case standard input");
                require(first.exit_code==0&&(!first.executable||
                        (owner->cmake&&*first.executable==owner->cmake->target)),
                    "Named-rule batteries require public success from the original executable target");
                if(root.HasKey(L"compilerCmakeGraphChecks"))
                    for(const auto& graph:array(root,"compilerCmakeGraphChecks"))
                        require(text(graph.GetObject(),"activity")!=identity,
                            "Named-rule batteries cannot share an owner with a CMake graph check");
                const auto authored=declaration.GetNamedObject(L"batteryCheck");
                allowed_keys(authored,{"cases","failureExitCode"});
                require(authored.HasKey(L"cases"),
                    "Named-rule battery checks require their own authored cases");
                NamedRuleBatterySpec battery;
                battery.source_path=check.path;
                if(authored.HasKey(L"failureExitCode")) {
                    const auto value=authored.GetNamedValue(L"failureExitCode");
                    require(value.ValueType()==JsonValueType::Number,
                        "Named-rule battery failure exit must be a positive integer");
                    const double code=value.GetNumber();
                    require(std::isfinite(code)&&std::trunc(code)==code&&code>=1&&
                            code<=static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
                        "Named-rule battery failure exit is outside the Win32 return range");
                    battery.failure_exit_code=static_cast<std::uint32_t>(code);
                }
                const auto required=array(authored,"cases");
                require(required.Size()>0&&required.Size()<=32,
                    "Named-rule battery requires between 1 and 32 comparisons");
                std::set<std::string> battery_functions;
                for(const auto& value:required) {
                    const auto comparison=value.GetObject();
                    allowed_keys(comparison,{"function","input","expected"},true);
                    NamedRuleProbeCase required_case;
                    required_case.function=text(comparison,"function");
                    require(functions.contains(required_case.function),
                        "A student battery must call an authored named-rule function");
                    required_case.input=bounded_signed(comparison.GetNamedValue(L"input"));
                    required_case.expected=bounded_signed(comparison.GetNamedValue(L"expected"));
                    const auto shared=std::ranges::find_if(probe.cases,
                        [&](const auto& private_case) {
                            return private_case.function==required_case.function&&
                                private_case.input==required_case.input;
                        });
                    require(shared==probe.cases.end()||shared->expected==required_case.expected,
                        "Shared private and student-battery cases cannot disagree on their expected result");
                    battery_functions.insert(required_case.function);
                    battery.cases.push_back(std::move(required_case));
                }
                require(battery_functions==functions&&named_rule_battery_spec_valid(battery),
                    "Every named function needs distinct required student-battery cases");
                const auto& source=single_file?owner->initial_code:
                    std::ranges::find(owner->project_files,check.path,
                        &CppProjectFiles::value_type::first)->second;
                require(named_rule_battery_source_valid(source,battery),
                    "Named-rule battery needs original global int(int) or bool(int) functions");
                if(single_file) {
                    require(named_rule_battery_driver_valid(source),
                        "Named-rule battery needs one instrumentable original main");
                }else{
                    const auto driver=std::ranges::find(owner->project_files,probe.harness_path,
                        &CppProjectFiles::value_type::first);
                    require(driver!=owner->project_files.end()&&
                            named_rule_battery_driver_valid(driver->second),
                        "Named-rule battery needs an instrumentable original checker");
                }
                probe.battery_check=std::move(battery);
            }
            if(declaration.HasKey(L"constantVariations")) {
                if(root.HasKey(L"compilerCmakeGraphChecks"))
                    for(const auto& graph:array(root,"compilerCmakeGraphChecks"))
                        require(text(graph.GetObject(),"activity")!=identity,
                            "Constant variations cannot share an owner with a CMake graph check");
                const auto authored=array(declaration,"constantVariations");
                require(authored.Size()>0&&authored.Size()<=8,
                    "Constant variations require between 1 and 8 alternatives");
                NamedRuleConstantVariationSpec variations;
                variations.source_path=check.path;
                variations.requirements=check.requirements;
                for(const auto& value:authored) {
                    const auto variation=value.GetObject();
                    allowed_keys(variation,{"function","value","cases"},true);
                    NamedRuleConstantVariation alternative;
                    alternative.function=text(variation,"function");
                    alternative.value=bounded_signed(variation.GetNamedValue(L"value"));
                    const auto comparisons=array(variation,"cases");
                    require(comparisons.Size()>0&&comparisons.Size()<=32,
                        "Each constant variation needs between 1 and 32 comparisons");
                    for(const auto& comparison_value:comparisons) {
                        const auto comparison=comparison_value.GetObject();
                        allowed_keys(comparison,{"function","input","expected"},true);
                        alternative.cases.push_back({text(comparison,"function"),
                            bounded_signed(comparison.GetNamedValue(L"input")),
                            bounded_signed(comparison.GetNamedValue(L"expected"))});
                    }
                    variations.variations.push_back(std::move(alternative));
                }
                // The original starter need not contain the required local
                // constant yet: the learner introduces it in the submitted source.
                require(named_rule_constant_variation_spec_valid(variations,probe.cases),
                    "Constant variations need distinct changed target witnesses and preserved other functions");
                probe.constant_variations=std::move(variations);
            }
            check.execution_probe=std::move(probe);
        }
        // An editable C++ driver can print a forged report. Never activate
        // such a rubric unless the runner will build a separate trusted driver.
        const bool editable_cpp=std::ranges::any_of(check.editable_paths,
            [](std::string_view path){return is_cpp_source_file(path);});
        require(!editable_cpp||(check.execution_probe&&
                std::ranges::find(check.editable_paths,
                    check.execution_probe->harness_path)!=
                    check.editable_paths.end()),
            "Editable CMake test executable requires an independent function probe");
        owner->named_rule_source_check=std::move(check);
    }
}

// Root-authored batteries are attached only after the original mapped blocks
// are loaded. Boolean single-file contracts preserve their published function;
// historical CMake contracts retain their exact original call verification.
void read_compiler_open_battery_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerOpenBatteryChecks"))return;
    const auto entries=array(root,"compilerOpenBatteryChecks");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            entries.Size()>0&&entries.Size()<=32,
            "Open batteries require existing Spanish v2 owners");
    const auto authored_int=[](const IJsonValue& value) {
        require(value.ValueType()==JsonValueType::Number,
            "Open CMake battery needs integer input facts");
        const double number=value.GetNumber();
        require(std::isfinite(number)&&std::trunc(number)==number&&
                number>=static_cast<double>(std::numeric_limits<int>::min())&&
                number<=static_cast<double>(std::numeric_limits<int>::max()),
            "Open CMake battery integer is outside the signed int range");
        return static_cast<int>(number);
    };
    const auto source_int=[](const std::string& source) {
        int number{};
        const auto [end,error]=std::from_chars(
            source.data(),source.data()+source.size(),number);
        require(error==std::errc{}&&end==source.data()+source.size(),
            "Original battery calls contain an unsupported numeric token");
        return number;
    };
    std::set<std::string> used;
    for(const auto& entry:entries) {
        const auto item=entry.GetObject();
        if(item.HasKey(L"kind")&&text(item,"kind")=="cmake-adjustment-project-v1") {
            allowed_keys(item,{"activity","kind","libraryPath","headerPath","header",
                "function","libraryTarget","clientPath","clientTarget","testPath","target"},true);
            const auto identity=text(item,"activity");
            require(valid_id(identity)&&used.insert(identity).second,
                "Duplicate or invalid adjustment construction identity");
            Block* owner=nullptr;
            for(auto& page:lesson.pages)for(auto& block:page.blocks)
                if(block.stable_id==identity) {
                    require(!owner,"Adjustment construction has a duplicated mapped identity");
                    owner=&block;
                }
            require(owner&&owner->type==BlockType::cpp_compiler&&owner->cmake&&
                    !owner->project_files.empty()&&owner->source_files.empty()&&
                    !owner->output_check&&!owner->compile_check&&
                    !owner->compiler_workflow_check&&!owner->named_rule_source_check&&
                    !owner->open_battery_probe&&!owner->boolean_battery_probe&&
                    !owner->reduction_probe&&!owner->integer_battery_probe&&
                    !owner->adjustment_project_probe&&!owner->cmake_graph_check,
                "Adjustment construction requires an original unevaluated CMake editor");
            const auto original=JsonObject::Parse(winrt::to_hstring(owner->raw_json));
            require((!original.HasKey(L"language")||text(original,"language")=="cpp")&&
                    !original.HasKey(L"sourceFiles"),
                "Adjustment construction requires its original C++ project");
            AdjustmentProjectSpec spec;
            spec.library_path=text(item,"libraryPath");
            spec.header_path=text(item,"headerPath");
            spec.header=text(item,"header");
            spec.function_name=text(item,"function");
            spec.library_target=text(item,"libraryTarget");
            spec.client_path=text(item,"clientPath");
            spec.client_target=text(item,"clientTarget");
            spec.test_path=text(item,"testPath");
            spec.test_target=text(item,"target");
            spec.client_input=owner->standard_input.value_or("");
            require(adjustment_project_spec_valid(spec)&&
                    owner->cmake->target==spec.client_target,
                "Adjustment construction has invalid paths, names, input or initial client target");
            const auto source=[&](std::string_view path)->const std::string* {
                const auto found=std::ranges::find(owner->project_files,path,
                    &CppProjectFiles::value_type::first);
                return found==owner->project_files.end()?nullptr:&found->second;
            };
            const auto header=source(spec.header_path);
            require(source("CMakeLists.txt")&&source(spec.library_path)&&
                    source(spec.client_path)&&source(spec.test_path)&&header&&
                    adjustment_project_header_valid(*header,spec),
                "Adjustment construction needs its original sources and declared interface");
            require(root.HasKey(L"compilerCmakeGraphChecks"),
                "Adjustment construction requires its shared CMake graph declaration");
            std::size_t graphs=0;
            for(const auto& candidate:array(root,"compilerCmakeGraphChecks"))
                if(text(candidate.GetObject(),"activity")==identity)++graphs;
            require(graphs==1,
                "Adjustment construction requires exactly one matching CMake graph");
            OutputCheck runtime;
            runtime.adjustment_project_check=true;
            runtime.input_comparison=TextComparison::exact;
            OutputCase first;
            first.standard_input.clear();
            first.executable=spec.test_target;
            first.outputs={""};
            first.explanation="El componente, el cliente y la batería cumplieron el contrato "
                "en la misma revisión del proyecto.";
            runtime.cases.push_back(std::move(first));
            owner->adjustment_project_probe=std::move(spec);
            owner->output_check=std::move(runtime);
            continue;
        }
        if(item.HasKey(L"kind")&&text(item,"kind")=="cmake-int-unary-v1") {
            allowed_keys(item,{"activity","kind","libraryPath","function",
                "testPath","target","publishedResult","failureExitCode","cases","prerequisite"});
            require(item.Size()==(item.HasKey(L"prerequisite")?10u:9u),
                "Integer CMake battery has an incomplete declaration");
            const auto declared_text=[](const JsonObject& object,std::string_view name) {
                const auto value=object.GetNamedValue(key(name));
                require(value.ValueType()==JsonValueType::String,
                    "Integer CMake battery properties require authored strings");
                return utf8(value.GetString());
            };
            const auto identity=declared_text(item,"activity");
            require(valid_id(identity)&&used.insert(identity).second,
                "Duplicate or invalid integer battery identity");
            Block* owner=nullptr;
            for(auto& page:lesson.pages)for(auto& block:page.blocks)
                if(block.stable_id==identity) {
                    require(!owner,"Integer battery has duplicated mapped identity");
                    owner=&block;
                }
            require(owner&&owner->type==BlockType::cpp_compiler&&owner->cmake&&
                    !owner->project_files.empty()&&owner->source_files.empty()&&
                    !owner->output_check&&!owner->compile_check&&
                    !owner->compiler_workflow_check&&!owner->named_rule_source_check&&
                    !owner->open_battery_probe&&!owner->boolean_battery_probe&&
                    !owner->reduction_probe&&!owner->integer_battery_probe&&!owner->cmake_graph_check,
                "Integer battery requires an original unevaluated CMake editor");
            const auto owner_json=JsonObject::Parse(winrt::to_hstring(owner->raw_json));
            require((!owner_json.HasKey(L"language")||text(owner_json,"language")=="cpp")&&
                    !owner_json.HasKey(L"sourceFiles"),
                "Integer battery requires the original C++ CMake project");
            IntegerBatterySpec spec;
            spec.library_path=declared_text(item,"libraryPath");
            spec.test_path=declared_text(item,"testPath");
            spec.function_name=declared_text(item,"function");
            spec.test_target=declared_text(item,"target");
            const auto source=[&](std::string_view name)->const std::string* {
                const auto found=std::ranges::find(owner->project_files,name,
                    &CppProjectFiles::value_type::first);
                return found==owner->project_files.end()?nullptr:&found->second;
            };
            const auto library=source(spec.library_path);
            require(library&&source(spec.test_path)&&source("CMakeLists.txt"),
                "Integer battery paths must belong to its original CMake project");
            const auto definition=integer_battery_extract_definition(*library,spec.function_name);
            require(definition.has_value(),
                "Integer battery needs one original global int(int) definition");
            spec.protected_definition=*definition;
            const auto published=declared_text(item,"publishedResult");
            require(published=="success"||published=="failure",
                "Integer battery requires its published comparison result");
            spec.published_result=published=="success"
                ?IntegerBatteryPublishedResult::success:IntegerBatteryPublishedResult::failure;
            const auto failure_exit=item.GetNamedValue(L"failureExitCode");
            if(failure_exit.ValueType()==JsonValueType::String) {
                require(utf8(failure_exit.GetString())=="nonzero",
                    "Integer battery failure exit must be exact or nonzero");
                spec.failure_exit_code.reset();
            } else {
                require(failure_exit.ValueType()==JsonValueType::Number,
                    "Integer battery failure exit must be a positive integer");
                const double code=failure_exit.GetNumber();
                require(std::isfinite(code)&&std::trunc(code)==code&&code>=1&&
                        code<=static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
                    "Integer battery failure exit is outside the Win32 return range");
                spec.failure_exit_code=static_cast<std::uint32_t>(code);
            }
            const auto cases=array(item,"cases");
            require(cases.Size()>0&&cases.Size()<=32,
                "Integer battery needs between 1 and 32 distinct inputs");
            for(const auto& value:cases) {
                const auto declared=value.GetObject();
                allowed_keys(declared,{"input","expected"},true);
                spec.cases.push_back({authored_int(declared.GetNamedValue(L"input")),
                    authored_int(declared.GetNamedValue(L"expected"))});
            }
            if(item.HasKey(L"prerequisite")) {
                const auto previous=item.GetNamedObject(L"prerequisite");
                allowed_keys(previous,{"target","calls","standardInput","standardOutput",
                    "standardError","exitCode"},true);
                IntegerBatteryPrerequisite prerequisite;
                prerequisite.target=declared_text(previous,"target");
                prerequisite.standard_input=declared_text(previous,"standardInput");
                prerequisite.standard_output=declared_text(previous,"standardOutput");
                prerequisite.standard_error=declared_text(previous,"standardError");
                require(authored_int(previous.GetNamedValue(L"exitCode"))==0,
                    "The earlier client must finish normally with zero");
                prerequisite.exit_code=0;
                require(prerequisite.standard_input==owner->standard_input.value_or(""),
                    "The earlier client must retain the original authored input");
                const auto calls=array(previous,"calls");
                require(calls.Size()>0&&calls.Size()<=32,
                    "The earlier client needs between 1 and 32 ordered calls");
                for(const auto& value:calls) {
                    const auto call=value.GetObject();
                    allowed_keys(call,{"input","result"},true);
                    prerequisite.calls.push_back({authored_int(call.GetNamedValue(L"input")),
                        authored_int(call.GetNamedValue(L"result"))});
                }
                spec.prerequisite=std::move(prerequisite);
            }
            require(integer_battery_spec_valid(spec)&&
                    (owner->cmake->target==spec.test_target||
                     (spec.prerequisite&&owner->cmake->target==spec.prerequisite->target)),
                "Integer battery has invalid paths, targets, cases or client requirements");
            OutputCheck runtime;
            runtime.integer_battery_check=true;
            runtime.integer_battery_failure_exit_code=spec.failure_exit_code;
            runtime.input_comparison=TextComparison::exact;
            OutputCase first;
            first.standard_input=owner->standard_input.value_or("");
            first.executable=spec.test_target;
            first.exit_code=spec.published_result==IntegerBatteryPublishedResult::success?0:1;
            first.outputs={""}; // Each independent comparison, not literal messages, decides credit.
            first.explanation="La batería comprobó todos los datos, mostró sus esperados y "
                "resultados, y detectó cada discrepancia sin omitir las comparaciones posteriores.";
            runtime.cases.push_back(std::move(first));
            owner->integer_battery_probe=std::move(spec);
            owner->output_check=std::move(runtime);
            continue;
        }
        if(item.HasKey(L"kind")) {
            allowed_keys(item,{"activity","kind","function","publishedResult","cases"},true);
            require(text(item,"kind")=="boolean-pair-v1",
                "Unsupported open battery kind");
            const auto identity=text(item,"activity");
            require(valid_id(identity)&&used.insert(identity).second,
                "Duplicate or invalid boolean battery identity");
            Block* owner=nullptr;
            for(auto& page:lesson.pages)for(auto& block:page.blocks)
                if(block.stable_id==identity) {
                    require(!owner,"Boolean battery has duplicated mapped identity");
                    owner=&block;
                }
            require(owner&&owner->type==BlockType::cpp_compiler&&
                    !owner->cmake&&owner->project_files.empty()&&owner->source_files.empty()&&
                    !owner->initial_code.empty()&&!owner->output_check&&!owner->compile_check&&
                    !owner->compiler_workflow_check&&!owner->named_rule_source_check&&
                    !owner->open_battery_probe&&!owner->boolean_battery_probe&&
                    !owner->cmake_graph_check,
                "Boolean battery requires an original unevaluated single-file C++ editor");
            const auto owner_json=JsonObject::Parse(winrt::to_hstring(owner->raw_json));
            require(!owner_json.HasKey(L"language")||text(owner_json,"language")=="cpp",
                "Boolean battery source language must be C++");
            BooleanBatterySpec check;
            check.function_name=text(item,"function");
            const auto definition=boolean_battery_extract_definition(
                owner->initial_code,check.function_name);
            require(definition.has_value(),
                "Boolean battery needs one original bool(int,int) function");
            check.protected_definition=*definition;
            const auto published=text(item,"publishedResult");
            require(published=="success"||published=="failure",
                "Boolean battery needs an explicit published result");
            check.published_result=published=="success"
                ?BooleanBatteryPublishedResult::success:BooleanBatteryPublishedResult::failure;
            const auto cases=array(item,"cases");
            require(cases.Size()>0&&cases.Size()<=32,
                "Boolean battery needs between 1 and 32 distinct input pairs");
            for(const auto& element:cases) {
                const auto declared=element.GetObject();
                allowed_keys(declared,{"first","second","expected"},true);
                const auto expected=declared.GetNamedValue(L"expected");
                require(expected.ValueType()==JsonValueType::Boolean,
                    "Boolean battery expected result must be a boolean");
                check.cases.push_back({
                    authored_int(declared.GetNamedValue(L"first")),
                    authored_int(declared.GetNamedValue(L"second")),
                    expected.GetBoolean()
                });
            }
            require(boolean_battery_spec_valid(check),
                "Boolean battery has an invalid function or repeated input pairs");
            OutputCheck runtime;
            runtime.boolean_battery_result=check.published_result;
            runtime.input_comparison=TextComparison::exact;
            OutputCase first;
            first.standard_input=owner->standard_input.value_or("");
            first.outputs={""}; // Actual sensitivity evidence decides this objective.
            first.explanation="La batería ejecutó todas las comparaciones, detectó "
                "los resultados alterados y conservó la función solicitada.";
            runtime.cases.push_back(std::move(first));
            owner->boolean_battery_probe=std::move(check);
            owner->output_check=std::move(runtime);
            continue;
        }
        allowed_keys(item,{"activity","library","test","function",
            "comparison","target","originalCases"},true);
        const auto identity=text(item,"activity");
        require(valid_id(identity)&&used.insert(identity).second,
            "Duplicate or invalid open CMake battery identity");
        Block* owner=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!owner,"Open CMake battery has duplicated mapped identity");
                owner=&block;
            }
        require(owner&&owner->type==BlockType::cpp_compiler&&owner->cmake&&
                !owner->output_check&&!owner->compile_check&&
                owner->source_files.empty()&&!owner->project_files.empty()&&
                !owner->named_rule_source_check&&!owner->cmake_graph_check,
            "Open CMake battery requires an original unevaluated project editor");
        OpenBatteryProbeSpec check;
        check.library_path=text(item,"library");
        check.test_path=text(item,"test");
        check.function_name=text(item,"function");
        check.comparison_name=text(item,"comparison");
        check.test_target=text(item,"target");
        const auto cases=array(item,"originalCases");
        require(cases.Size()>0&&cases.Size()<=32,
            "Open CMake battery needs between 1 and 32 original cases");
        for(const auto& element:cases) {
            const auto original=element.GetObject();
            allowed_keys(original,{"value","lower","upper","initial"},true);
            check.original_cases.push_back({
                authored_int(original.GetNamedValue(L"value")),
                authored_int(original.GetNamedValue(L"lower")),
                authored_int(original.GetNamedValue(L"upper")),
                authored_int(original.GetNamedValue(L"initial"))
            });
        }
        // The case-number map is derived below from the authenticated driver.
        // Validate the only name interpolated into a regex before that pass;
        // the complete spec is checked once all tuple/number pairs are known.
        require(open_battery_identifier(check.comparison_name)&&
                owner->cmake->target==check.test_target,
            "Open CMake battery has an invalid comparison name or target");
        const auto source=[&](std::string_view name)->const std::string* {
            const auto it=std::ranges::find(owner->project_files,name,
                &CppProjectFiles::value_type::first);
            return it==owner->project_files.end()?nullptr:&it->second;
        };
        const auto library=source(check.library_path);
        const auto driver=source(check.test_path);
        const auto cmake=source("CMakeLists.txt");
        require(library&&driver&&cmake,
            "Open CMake battery must use only its original shared project files");
        require(open_battery_mutated_library(*library,check.function_name,
                OpenBatteryMutation::baseline).has_value(),
            "The shared C++ function cannot be instrumented independently");
        require(open_battery_instrumented_comparison(
                *driver,check.comparison_name).has_value(),
            "The original open battery comparator cannot be instrumented independently");
        const std::regex call(R"(\b)"+check.comparison_name+
            R"(\s*\(\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(true|false)\s*,\s*(-?\d+)\s*\))");
        std::vector<std::pair<OpenBatteryCall,int>> authored;
        std::set<int> labels;
        for(std::sregex_iterator found(driver->begin(),driver->end(),call),end;
                found!=end;++found) {
            const auto& m=*found;
            const auto number=source_int(m[1].str());
            require(labels.insert(number).second,
                "Original open battery repeats its case label");
            const OpenBatteryCall original{
                source_int(m[2].str()),source_int(m[3].str()),
                source_int(m[4].str()),source_int(m[5].str())
            };
            const auto expected=open_battery_expected(original);
            require((m[6].str()=="true")==expected.first&&
                    source_int(m[7].str())==expected.second,
                "Original open battery expected answer contradicts the contract");
            require(std::ranges::none_of(authored,[&](const auto& previous) {
                    return previous.first==original;
                }),"Original open battery repeats an input tuple");
            authored.emplace_back(original,number);
        }
        require(authored.size()==check.original_cases.size(),
            "Open CMake battery root case count differs from historical comparisons");
        for(const auto& original:check.original_cases) {
            const auto found=std::ranges::find_if(authored,[&](const auto& previous) {
                return previous.first==original;
            });
            require(found!=authored.end(),
                "Open CMake battery root cases differ from actual historical comparisons");
            // JSON order is not the historical case number. Keep the original
            // association even when authoring lists the same tuples differently.
            check.original_case_numbers.push_back(found->second);
        }
        require(open_battery_probe_spec_valid(check),
            "Open CMake battery has invalid paths, names, cases or case numbers");
        const auto escaped=std::regex_replace(
            check.test_path,std::regex(R"(\.)"),R"(\.)");
        const std::regex target(
            R"(\badd_executable\s*\(\s*)"+check.test_target+
            R"(\s+)"+escaped+R"(\s*\))");
        require(std::regex_search(*cmake,target)&&
                cmake->find("add_library(")!=cmake->npos,
            "Open CMake battery must use its original test executable and library");
        OutputCheck runtime;
        runtime.open_battery_check=true;
        runtime.input_comparison=TextComparison::exact;
        OutputCase first;
        // This checker supplies its own cases and runs with empty input.
        // Preserve the historical client input on the original editor.
        first.standard_input.clear();
        first.executable=check.test_target;
        first.outputs={""}; // Independent mutation evidence, never stdout comparison.
        first.explanation="La batería comprobó sus casos anteriores y el nuevo "
                          "con ejecuciones independientes del componente.";
        runtime.cases.push_back(std::move(first));
        owner->open_battery_probe=std::move(check);
        owner->output_check=std::move(runtime);
    }
}

// A reduction goal preserves the published editor bytes and authenticates
// its stateful reproduction separately from successful process termination.
void read_compiler_reduction_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerReductionChecks"))return;
    const auto entries=array(root,"compilerReductionChecks");
    require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            entries.Size()>0&&entries.Size()<=32,
        "Reduction goals require existing Spanish v2 owners");
    const auto signed_int=[](const IJsonValue& value) {
        require(value.ValueType()==JsonValueType::Number,
            "Reduction facts must be signed C++ integers");
        const auto number=value.GetNumber();
        require(std::isfinite(number)&&std::trunc(number)==number&&
                number>=static_cast<double>(std::numeric_limits<std::int32_t>::min())&&
                number<=static_cast<double>(std::numeric_limits<std::int32_t>::max()),
            "Reduction integer is outside its supported range");
        return static_cast<std::int32_t>(number);
    };
    std::set<std::string> used;
    for(const auto& entry:entries) {
        const auto item=entry.GetObject();
        allowed_keys(item,{"activity","kind","function","state","initialState",
            "calls","observedCall","requiredResult","exitCode","dependencies"},true);
        require(text(item,"kind")=="stateful-vector-int-v1",
            "Unsupported reduction objective kind");
        const auto identity=text(item,"activity");
        require(valid_id(identity)&&used.insert(identity).second,
            "Duplicate or invalid reduction identity");
        Block* owner=nullptr;
        for(auto& page:lesson.pages)for(auto& block:page.blocks)
            if(block.stable_id==identity) {
                require(!owner,"Reduction has a duplicated mapped identity");
                owner=&block;
            }
        require(owner&&owner->type==BlockType::cpp_compiler&&
                !owner->cmake&&owner->project_files.empty()&&owner->source_files.empty()&&
                !owner->initial_code.empty()&&!owner->output_check&&!owner->compile_check&&
                !owner->compiler_workflow_check&&!owner->named_rule_source_check&&
                !owner->open_battery_probe&&!owner->boolean_battery_probe&&
                !owner->reduction_probe&&!owner->cmake_graph_check,
            "Reduction requires an original unevaluated single-file C++ editor");
        const auto owner_json=JsonObject::Parse(winrt::to_hstring(owner->raw_json));
        require(!owner_json.HasKey(L"language")||text(owner_json,"language")=="cpp",
            "Reduction source language must be C++");
        ReductionSpec spec;
        spec.function_name=text(item,"function");
        spec.state_name=text(item,"state");
        spec.initial_source=owner->initial_code;
        const auto definition=reduction_extract_definition(spec.initial_source,spec.function_name);
        require(definition.has_value(),
            "Reduction needs one original int(vector<int>,int) function");
        spec.protected_definition=*definition;
        spec.initial_state=signed_int(item.GetNamedValue(L"initialState"));
        const auto calls=array(item,"calls");
        require(calls.Size()>=2&&calls.Size()<=32,
            "Reduction needs between 2 and 32 ordered calls");
        for(const auto& value:calls) {
            const auto call=value.GetObject();
            allowed_keys(call,{"values","argument","result","stateAfter"},true);
            ReductionCall fact;
            const auto values=array(call,"values");
            require(values.Size()<=64,"Reduction call has too many collection values");
            for(const auto& element:values)fact.values.push_back(signed_int(element));
            fact.argument=signed_int(call.GetNamedValue(L"argument"));
            fact.result=signed_int(call.GetNamedValue(L"result"));
            fact.state_after=signed_int(call.GetNamedValue(L"stateAfter"));
            spec.calls.push_back(std::move(fact));
        }
        spec.observed_call=integer(item.GetNamedValue(L"observedCall"),31);
        require(spec.observed_call>0&&spec.observed_call<spec.calls.size(),
            "The observed reduction call needs earlier preparation");
        spec.required_result=signed_int(item.GetNamedValue(L"requiredResult"));
        require(signed_int(item.GetNamedValue(L"exitCode"))==0,
            "This reduction objective requires normal completion with zero");
        spec.exit_code=0;
        const auto dependencies=array(item,"dependencies");
        require(dependencies.Size()>0&&dependencies.Size()<=32,
            "Reduction needs a bounded explicit dependency contract");
        for(const auto& value:dependencies) {
            const auto dependency=value.GetObject();
            allowed_keys(dependency,{"header","symbols"},true);
            const auto symbols=array(dependency,"symbols");
            require(symbols.Size()>0&&symbols.Size()<=32,
                "A reduction dependency needs a bounded list of symbols");
            spec.dependencies.push_back({text(dependency,"header"),strings(symbols,true,false)});
        }
        require(reduction_spec_valid(spec),
            "Invalid stateful reduction source, facts or dependency contract");
        OutputCheck runtime;
        runtime.reduction_check=true;
        runtime.input_comparison=TextComparison::exact;
        OutputCase first;
        first.standard_input=owner->standard_input.value_or("");
        first.outputs={""}; // The preserved execution and causal observation decide.
        first.explanation="La reducción conservó la función, la preparación y el "
            "resultado calculado que reproduce el fallo.";
        runtime.cases.push_back(std::move(first));
        owner->reduction_probe=std::move(spec);
        owner->output_check=std::move(runtime);
    }
}

void read_compiler_runtime_checks(const JsonObject& root,Lesson& lesson) {
    if(!root.HasKey(L"compilerRuntimeChecks"))return;
    const auto records=array(root,"compilerRuntimeChecks");
    if(lesson.id=="programming_practice_tools__comprobaciones_repetibles__comprobar_acuerdos_internos_con_aserciones") {
        require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            lesson.pages.size()==6&&records.Size()==3,
            "WASI assertion contracts belong to the three original Spanish U3L9 editors");
        const std::array targets{
            std::tuple{2u,3u,"block_0002_0003","wasiAssertionAbortV1",
                AssertionMode::enabled_abort,CompilerWorkflowCheck::assertion_abort_wasi_v1},
            std::tuple{3u,7u,"block_0003_0007","wasiAssertionSuppressedV1",
                AssertionMode::disabled,CompilerWorkflowCheck::assertion_suppressed_wasi_v1},
            std::tuple{4u,3u,"block_0004_0007","wasiAssertionRepairV1",
                AssertionMode::repaired,CompilerWorkflowCheck::assertion_repaired_wasi_v1}
        };
        const auto& initial=lesson.pages[2].blocks[3].initial_code;
        for(std::size_t i=0;i<targets.size();++i) {
            const auto& [page,position,identity,kind,mode,workflow]=targets[i];
            const auto record=records.GetObjectAt(static_cast<unsigned>(i));
            allowed_keys(record,{"activity","kind"},true);
            require(text(record,"activity")==identity&&text(record,"kind")==kind&&
                lesson.pages[page].blocks.size()>position,
                "Unknown, reordered or displaced WASI assertion declaration");
            auto& owner=lesson.pages[page].blocks[position];
            require(owner.stable_id==identity&&owner.type==BlockType::cpp_compiler&&
                !owner.output_check&&!owner.compile_check&&owner.project_files.empty()&&
                owner.source_files.empty()&&!owner.cmake&&
                owner.standard_input==std::optional<std::string>{"-1 5\n"}&&
                owner.initial_code.find("assert(cajas * piezas_por_caja >= piezas);")!=std::string::npos&&
                owner.initial_code.find("const int cajas = piezas / piezas_por_caja;")!=std::string::npos&&
                (mode==AssertionMode::disabled
                    ?owner.initial_code.starts_with("#define NDEBUG\n#include <cassert>")
                    :owner.initial_code.starts_with("#include <cassert>")&&
                     owner.initial_code.find("NDEBUG")==std::string::npos&&
                     (mode!=AssertionMode::repaired||owner.initial_code==initial)),
                "The U3L9 source, NDEBUG order, assertion or prepared input changed");
            OutputCheck check;check.input_comparison=TextComparison::exact;
            check.assertion_check=mode;
            OutputCase run;run.standard_input="-1 5\n";
            if(mode==AssertionMode::enabled_abort) {
                run.outputs={""};
                // Normal exit codes never authenticate a runtime trap. The
                // special evaluator requires separately checked Wasmtime
                // source, assertion expression, callstack and trap evidence.
                run.exit_code=0;
                run.explanation="La aserción activa detectó una condición interna falsa y abortó la ejecución WASI.";
            } else {
                run.outputs={mode==AssertionMode::disabled?"Cajas=1\n":"Cajas=2\n"};
                run.exit_code=1;
                run.standard_error_exact="Cajas: peticion 1, piezas=-1. Se admite un entero de 0 a 20. Revise la cantidad.\n";
                run.explanation=mode==AssertionMode::disabled
                    ?"NDEBUG desactiva la comprobación interna, no el rechazo público; se observa el cálculo anterior."
                    :"La fórmula calcula el mínimo de cajas, mantiene assert y acepta las verificaciones independientes.";
            }
            check.cases.push_back(std::move(run));
            owner.output_check=std::move(check);
            owner.compiler_workflow_check=workflow;
        }
        return;
    }
    if(lesson.id=="programming_practice_tools__comprobaciones_repetibles__automatizar_la_revision_sin_borrar_evidencia") {
        require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            lesson.pages.size()==9&&records.Size()==2,
            "The two observation report editors require the original Spanish U3L8 v2 lesson");
        const std::array targets{
            std::tuple{3u,3u,"block_0003_0003","observationCompleteV1",
                ReportMode::complete,CompilerWorkflowCheck::observation_complete_v1},
            std::tuple{6u,3u,"block_0006_0003","observationDifferencesV1",
                ReportMode::differences,CompilerWorkflowCheck::observation_differences_v1}
        };
        for(std::size_t index=0;index<targets.size();++index){
            const auto& [page,position,identity,kind,mode,workflow]=targets[index];
            const auto record=records.GetObjectAt(static_cast<unsigned>(index));
            allowed_keys(record,{"activity","kind"},true);
            require(text(record,"activity")==identity&&text(record,"kind")==kind&&
                lesson.pages[page].blocks.size()>position,
                "Unknown, unordered or misplaced U3L8 report declaration");
            auto& owner=lesson.pages[page].blocks[position];
            const std::string prepared=mode==ReportMode::complete
                ? "A 0 0\nA 2 1\nB -1 -1\nA 2 1\nA 1-1\nC;=lote 0 1\nC;=lote 0 1\nA 1 -1\n"
                : "N 0 0\nR -1 -1\nR 0 -1\nP 3\nR 0 -1\nZ 9 9\n";
            require(owner.stable_id==identity&&owner.type==BlockType::cpp_compiler&&
                !owner.output_check&&!owner.compile_check&&owner.project_files.empty()&&
                owner.source_files.empty()&&!owner.cmake&&
                owner.standard_input==prepared&&
                report_main_only_edit(owner.initial_code,owner.initial_code)&&
                owner.initial_code.find("bool leer_entero(")!=std::string::npos&&
                owner.initial_code.find("bool leer_registro(")!=std::string::npos&&
                owner.initial_code.find(mode==ReportMode::complete
                    ? "if (hay_anterior && caso == caso_anterior)"
                    : "if (coincide) {\n            continue;")!=std::string::npos,
                "The U3L8 original parser, main, prepared input or historical editor changed");
            OutputCheck objective;
            objective.input_comparison=TextComparison::exact;
            objective.observation_report=mode;
            const auto add=[&](std::string input,std::string explanation) {
                const auto expected=expected_observation_report(mode,input);
                require(expected.has_value(),"The U3L8 report oracle refused a reviewed fixture");
                OutputCase case_goal;
                case_goal.standard_input=std::move(input);
                case_goal.outputs={expected->standard_output};
                case_goal.standard_error_exact=expected->standard_error;
                case_goal.exit_code=expected->exit_code;
                case_goal.explanation=std::move(explanation);
                objective.cases.push_back(std::move(case_goal));
            };
            add(prepared,"Conserva todas las observaciones, sus ocurrencias, número de línea y resumen.");
            add("0 0 0\nN -2 -2",
                "La última línea sin salto final cuenta y ambos casos coinciden.");
            add("","La entrada vacía produce únicamente el resumen de cuatro ceros.");
            add("Z 0 0\nZ 1 2\n\nH 3 3\nQ 2x 2\nH 3 2\nG 3 1\n",
                "Las etiquetas repetidas, línea vacía, número parcial y filas posteriores cuentan por separado.");
            owner.output_check=std::move(objective);
            owner.compiler_workflow_check=workflow;
        }
        return;
    }
    if(lesson.id=="programming_practice_tools__comprobaciones_repetibles__diagnosticos_para_corregir_una_peticion") {
        require(lesson.locale=="es"&&lesson.state_layout.version==2&&
            lesson.pages.size()==9&&records.Size()==2,
            "The two semantic rejection editors require the original Spanish U3L6 v2 lesson");
        const std::array targets{
            std::tuple{3u,3u,"block_0003_0003","semanticRejectionBoxesV1",
                RejectionOperation::boxes,CompilerWorkflowCheck::semantic_rejection_boxes_v1},
            std::tuple{6u,3u,"block_0006_0003","semanticRejectionBalanceV1",
                RejectionOperation::balance,CompilerWorkflowCheck::semantic_rejection_balance_v1}
        };
        for(std::size_t index=0;index<targets.size();++index){
            const auto& [page,position,identity,kind,operation,workflow]=targets[index];
            const auto record=records.GetObjectAt(static_cast<unsigned>(index));
            allowed_keys(record,{"activity","kind"},true);
            require(text(record,"activity")==identity&&text(record,"kind")==kind&&
                lesson.pages[page].blocks.size()>position,
                "Unknown, unordered or misplaced U3L6 semantic runtime declaration");
            auto& owner=lesson.pages[page].blocks[position];
            require(owner.stable_id==identity&&owner.type==BlockType::cpp_compiler&&
                !owner.output_check&&!owner.compile_check&&owner.project_files.empty()&&
                owner.source_files.empty()&&!owner.cmake&&
                owner.standard_input==std::optional<std::string>{
                    operation==RejectionOperation::boxes?"-1 0 21 5\n":"2 3 10 0 0 -1 5 5\n"}&&
                rejection_function_only_edit(owner.initial_code,owner.initial_code,operation)&&
                owner.initial_code.find(operation==RejectionOperation::boxes
                    ? "destino << \"Error\\n\";" : "destino << \"Error de saldo\\n\";")!=std::string::npos,
                "The U3L6 source, input or historical diagnostic function changed");
            OutputCheck objective;
            objective.input_comparison=TextComparison::exact;
            objective.rejection_operation=operation;
            const auto add=[&](std::string input,std::string output,int exit,std::string explanation){
                const auto expected=rejection_expected_for_input(operation,input);
                require(expected&&expected->standard_output==output&&expected->exit_code==exit,
                    "Internal U3L6 diagnostic case is not independently derived");
                OutputCase case_goal;
                case_goal.standard_input=std::move(input);
                case_goal.outputs={std::move(output)};
                case_goal.exit_code=exit;
                case_goal.explanation=std::move(explanation);
                objective.cases.push_back(std::move(case_goal));
            };
            if(operation==RejectionOperation::boxes){
                add("-1 0 21 5\n","Cajas=0\nCajas=2\n",1,
                    "La función informa las peticiones 1 y 3, conserva cada cantidad y escribe en el flujo recibido.");
                add("0 1 20 4","Cajas=0\nCajas=1\nCajas=5\nCajas=1\n",0,
                    "Los cuatro valores permitidos no producen diagnósticos.");
                add("","",0,"Sin peticiones no hay resultados ni rechazos.");
            }else{
                add("2 3 10 0 0 -1 5 5\n","Saldo=-1\nSaldo=0\n",1,
                    "Cada rechazo distingue cobros y devoluciones y solicita revisar sus dominios.");
                add("2 3 0 0 9 0 0 9","Saldo=-1\nSaldo=0\nSaldo=9\nSaldo=-9\n",0,
                    "Los cuatro pares permitidos producen saldos, no rechazos.");
                add("","",0,"Sin pares no hay resultados ni rechazos.");
                OutputCase own;
                own.balance_three_pairs=true;
                own.outputs={""}; // Dynamic stdout is computed independently.
                own.exit_code=1;
                own.explanation="Tres pares propios: rechazo inferior, aceptación y rechazo superior, sin perder campos ni peticiones.";
                objective.cases.push_back(std::move(own));
            }
            owner.output_check=std::move(objective);
            owner.compiler_workflow_check=workflow;
        }
        return;
    }
    require(records.Size()==1,"A reviewed original-project runtime check requires exactly one owner");
    const auto record=records.GetObjectAt(0);
    allowed_keys(record,{"activity","kind"},true);
    require(text(record,"activity")=="block_0009_0008"&&
            text(record,"kind")=="exactOriginalProjectRuntimeV1",
        "Unknown compiler runtime activity or contract");
    require(lesson.id=="programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion"&&
            lesson.locale=="es"&&lesson.state_layout.version==2&&
            lesson.pages.size()==14&&lesson.pages[9].blocks.size()>8,
        "The original-project runtime goal belongs to the Spanish U2L3 lesson");
    auto& owner=lesson.pages[9].blocks[8];
    require(owner.stable_id=="block_0009_0008"&&owner.type==BlockType::cpp_compiler&&
            !owner.output_check&&!owner.compile_check&&owner.project_files.size()==5&&
            owner.cmake&&owner.cmake->target=="revisar"&&owner.cmake->build_type=="Debug"&&
            owner.cmake->options==std::map<std::string,std::string>{{"ACEPTAR_IGUAL","OFF"}}&&
            owner.standard_input==std::optional<std::string>{"6"}&&owner.source_files.empty(),
        "The U2L3 client no longer has its original, unevaluated CMake contract");
    const auto source=std::ranges::find(owner.project_files,std::string("main.cpp"),&CppProjectFiles::value_type::first);
    require(source!=owner.project_files.end()&&
            source->second.find("std::cerr << \"Entrada invalida\";")!=std::string::npos&&
            source->second.find("return 2;")!=std::string::npos,
        "The reviewed original-project goal needs its unchanged reading-rejection client");
    OutputCase check;
    check.standard_input="6";
    check.outputs={""};
    check.executable="revisar";
    check.exit_code=2;
    check.standard_error_exact="Entrada invalida";
    check.explanation="La segunda extracción llega a EOF. El cliente conserva stdout vacío, informa en stderr y termina normalmente con código 2.";
    OutputCheck objective;objective.input_comparison=TextComparison::exact;
    objective.cases.push_back(std::move(check));
    owner.output_check=std::move(objective);
    owner.compiler_workflow_check=CompilerWorkflowCheck::exact_original_project_runtime_v1;
}

std::optional<JsonObject> authenticated_compiler_source(
        const Lesson& lesson,std::size_t page,std::size_t index,const JsonObject& active) {
    if(!active.HasKey(L"compileCheck")||lesson.state_layout.version!=2)return {};
    const auto& identity=lesson.state_layout.current.at(page).blocks.at(index);
    for(const auto& archive:lesson.state_layout.source_format_revisions)
        if(archive.id==identity)
            return JsonObject::Parse(key(archive.raw_json));
    return {};
}
curriculum::Lesson parse_lesson_json(std::string_view source, std::string_view expected_locale) {
    require(known_locale(expected_locale), "Unsupported asset language");
    ensure_winrt_apartment();
    try {
        if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
        require(source.size() <= 32 * 1024 * 1024, "Lesson file too large");
        const auto root = JsonObject::Parse(winrt::to_hstring(source));
        Lesson lesson; lesson.id = text(root, "id"); lesson.title = text(root, "title"); lesson.locale = expected_locale;
        lesson.course_edition = read_course_edition(root, expected_locale, source);
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
            lesson.state_layout = read_layout(root.GetNamedObject(L"stateLayout"), pages, expected_locale, lesson.id,root,source);
        } else {reject_unmapped_reviewed_math(pages);reject_unmapped_reviewed_choices(pages);reject_unmapped_compiler_revisions(pages);}
        bind_course_edition(lesson);
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
            for (unsigned b = 0; b < blocks.Size(); ++b) {
                const auto active=blocks.GetAt(b).GetObject();
                const auto historical=authenticated_compiler_source(lesson,p,b,active);
                page.blocks.push_back(read_block(active,original.text(original_blocks[b]),
                    historical?&*historical:nullptr));
            }
            lesson.pages.push_back(std::move(page));
        }
        resolve_addresses(lesson);
        read_order_equivalence_groups(root,lesson);
        read_order_visual_separators(root,lesson);
        read_compiler_workflow_checks(root,lesson);
        read_compiler_runtime_checks(root,lesson);
        read_compiler_named_rule_checks(root,lesson);
        read_compiler_open_battery_checks(root,lesson);
        read_compiler_reduction_checks(root,lesson);
        read_compiler_cmake_graph_checks(root,lesson);
        validate_compiler_recipe_objectives(root,lesson);
        read_text_semantics(root,lesson);
        validate_reference_relationships(lesson);
        validate_activity_relationships(lesson);
        return lesson;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Curriculum JSON/contract error: " + utf8(error.message()));
    }
}
curriculum::Lesson load_lesson(const std::filesystem::path& path, std::string_view expected_locale) {
    std::ifstream input(state::native_io_path(path), std::ios::binary); require(input.good(), "Cannot open lesson file");
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    require(!input.bad(), "Cannot read lesson file");
    auto lesson = parse_lesson_json(bytes, expected_locale);
    validate_course_edition_file(path, lesson);
    lesson.source_path = path; return lesson;
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
        lesson.course_edition = read_course_edition(root, expected_locale, source);
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
            lesson.state_layout = read_layout(root.GetNamedObject(L"stateLayout"),pages,expected_locale,lesson.id,root,source);
        } else {reject_unmapped_reviewed_math(pages);reject_unmapped_reviewed_choices(pages);reject_unmapped_compiler_revisions(pages);}
        bind_course_edition(lesson);
        read_relationships(root,lesson);
        // Every explicit curricular compiler objective participates in linear
        // progress, independently of the historical lesson-wide opt-in flag.
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
                    const bool declared_runtime=root.HasKey(L"compilerRuntimeChecks")&&(
                        (lesson.id=="programming_practice_tools__construccion_reproducible__opciones_y_revision_de_la_configuracion"&&p==9&&b==8)||
                        (lesson.id=="programming_practice_tools__comprobaciones_repetibles__diagnosticos_para_corregir_una_peticion"&&
                            ((p==3&&b==3)||(p==6&&b==3)))||
                        (lesson.id=="programming_practice_tools__comprobaciones_repetibles__automatizar_la_revision_sin_borrar_evidencia"&&
                            ((p==3&&b==3)||(p==6&&b==3)))||
                        (lesson.id=="programming_practice_tools__comprobaciones_repetibles__comprobar_acuerdos_internos_con_aserciones"&&
                            ((p==2&&b==3)||(p==3&&b==7)||(p==4&&b==3))));
                    // A root-only open battery must load its complete original
                    // project before the progress-side loader can authenticate
                    // its new, separately sealed execution objective.
                    bool declared_battery=false;
                    if(root.HasKey(L"compilerOpenBatteryChecks")) {
                        require(lesson.state_layout.version==2&&
                                p<lesson.state_layout.current.size()&&
                                b<lesson.state_layout.current[p].blocks.size(),
                            "Open battery progress requires an existing v2 mapped owner");
                        const auto identity=lesson.state_layout.current[p].blocks[b];
                        for(const auto& candidate:array(root,"compilerOpenBatteryChecks"))
                            if(text(candidate.GetObject(),"activity")==identity) {
                                declared_battery=true;break;
                            }
                    }
                    bool declared_reduction=false;
                    if(root.HasKey(L"compilerReductionChecks")) {
                        require(lesson.state_layout.version==2&&
                                p<lesson.state_layout.current.size()&&
                                b<lesson.state_layout.current[p].blocks.size(),
                            "Reduction progress requires an existing v2 mapped owner");
                        const auto identity=lesson.state_layout.current[p].blocks[b];
                        for(const auto& candidate:array(root,"compilerReductionChecks"))
                            if(text(candidate.GetObject(),"activity")==identity) {
                                declared_reduction=true;break;
                            }
                    }
                    if(block.HasKey(L"compileCheck")||block.HasKey(L"outputCheck")||
                       declared_runtime||declared_battery||declared_reduction) {
                        // Required compiler progress needs the whole authored
                        // objective and build inputs to verify captured proofs.
                        const auto historical=authenticated_compiler_source(lesson,p,b,block);
                        page.blocks.push_back(read_block(block,raw,historical?&*historical:nullptr));
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
        read_order_equivalence_groups(root,lesson);
        read_order_visual_separators(root,lesson);
        read_compiler_workflow_checks(root,lesson);
        read_compiler_runtime_checks(root,lesson);
        read_compiler_named_rule_checks(root,lesson);
        read_compiler_open_battery_checks(root,lesson);
        read_compiler_reduction_checks(root,lesson);
        read_compiler_cmake_graph_checks(root,lesson);
        validate_compiler_recipe_objectives(root,lesson);
        read_text_semantics(root,lesson);
        validate_reference_relationships(lesson);
        validate_activity_relationships(lesson);
        return lesson;
    } catch (const winrt::hresult_error& error) {
        throw std::runtime_error("Curriculum progress JSON/contract error: " + utf8(error.message()));
    }
}
curriculum::Lesson load_lesson_progress_metadata(const std::filesystem::path& path, std::string_view expected_locale) {
    std::ifstream input(state::native_io_path(path),std::ios::binary); require(input.good(),"Cannot open lesson progress metadata");
    const std::string source{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    require(!input.bad(),"Cannot read lesson progress metadata");
    try { auto lesson = parse_lesson_progress_metadata(source,expected_locale);
        validate_course_edition_file(path, lesson); lesson.source_path = path; return lesson; }
    catch(const std::exception& failure){throw std::runtime_error(path.string()+": "+failure.what());}
}
} // namespace lgaed::windows

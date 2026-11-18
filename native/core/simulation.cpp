#include "core/simulation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace lgaed::curriculum {
namespace {
constexpr std::size_t item_limit = 64;
constexpr std::size_t expanded_trace_limit = 8 * 1024 * 1024;

void require(bool condition, std::string_view message) {
    if (!condition) throw std::invalid_argument(std::string(message));
}

bool whitespace(std::uint32_t c) {
    // The Unicode whitespace set used by the authoring validator's str.isspace.
    return (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x20) || c == 0x85 ||
        c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
        c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}

bool valid_utf8(std::string_view text, bool* nonblank = nullptr) {
    bool visible = false;
    for (std::size_t at = 0; at < text.size();) {
        const auto first = static_cast<unsigned char>(text[at++]);
        std::uint32_t point = first;
        std::size_t following = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { following = 1; point &= 0x1f; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { following = 2; point &= 0x0f; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { following = 3; point &= 0x07; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (following > text.size() - at) return false;
        for (std::size_t n = 0; n < following; ++n) {
            const auto byte = static_cast<unsigned char>(text[at++]);
            if ((byte & 0xc0) != 0x80) return false;
            point = (point << 6) | (byte & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff) || point == 0)
            return false;
        visible = visible || !whitespace(point);
    }
    if (nonblank) *nonblank = visible;
    return true;
}

void public_text(std::string_view text, std::size_t limit, std::string_view field) {
    bool nonblank = false;
    require(text.size() <= limit && valid_utf8(text, &nonblank) && nonblank,
        std::string("Texto de simulación inválido o demasiado largo: ") + std::string(field));
}

constexpr std::array<std::string_view, 14> placeholders{
    "value", "start", "end", "frequency", "maximum", "leaders", "size", "group", "groups",
    "countedIndex", "originIndex", "processedCount", "remainingCount", "previousMaximum"
};

void validate_template(std::string_view text, std::size_t limit, std::string_view field,
                       bool counting_explanation = true) {
    public_text(text, limit, field);
    for (std::size_t at = 0; at < text.size();) {
        require(text[at] != '}', "Una plantilla de simulación tiene una llave de cierre sin apertura.");
        if (text[at] != '{') { ++at; continue; }
        const auto end = text.find('}', at + 1);
        require(end != text.npos, "Una plantilla de simulación tiene una llave sin cerrar.");
        const auto name = text.substr(at + 1, end - at - 1);
        require(std::find(placeholders.begin(), placeholders.end(), name) != placeholders.end(),
            "Una plantilla de simulación utiliza un marcador desconocido o anidado.");
        // outputTemplate is evaluated at the GROUP DECISION, not during an
        // observed-element step. Its source index would always be empty,
        // silently teaching a fabricated position in the calculated output.
        require(counting_explanation || (name != "countedIndex" && name != "originIndex"),
            "La salida de grupos no puede usar índices de un elemento recién contado.");
        at = end + 1;
    }
}

bool text_less(std::string_view left, std::string_view right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
        [](char a, char b) { return static_cast<unsigned char>(a) < static_cast<unsigned char>(b); });
}

bool value_less(const SimulationValue& left, const SimulationValue& right) {
    if (const auto* integer = std::get_if<std::int64_t>(&left))
        return *integer < std::get<std::int64_t>(right);
    return text_less(std::get<std::string>(left), std::get<std::string>(right));
}

void validate_values(const std::vector<SimulationValue>& values, bool text_values, bool ordered) {
    require(values.size() <= item_limit, "Una simulación admite como máximo 64 valores.");
    for (const auto& value : values) {
        if (text_values) {
            const auto* text = std::get_if<std::string>(&value);
            require(text && text->size() <= 128 && valid_utf8(*text) &&
                text->find_first_of("\r\n") == std::string::npos,
                "Cada valor debe ser texto UTF-8 de 0 a 128 bytes, sin NUL ni saltos de línea.");
        } else {
            const auto* integer = std::get_if<std::int64_t>(&value);
            require(integer && *integer >= std::numeric_limits<std::int32_t>::min() &&
                *integer <= std::numeric_limits<std::int32_t>::max(),
                "Cada valor debe ser un entero de 32 bits con signo.");
        }
    }
    require(!ordered || std::is_sorted(values.begin(), values.end(), value_less),
        "Con sort=false, los valores deben estar ordenados de forma ascendente.");
}

bool scenario_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    const auto letter = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
    };
    if (!letter(static_cast<unsigned char>(id.front()))) return false;
    return std::all_of(id.begin() + 1, id.end(), [&](unsigned char c) {
        return letter(c) || (c >= '0' && c <= '9') || c == '-';
    });
}

void validate_contract(const SimulationSpec& spec) {
    require(spec.version == 1 && spec.model == "arrayRunsV1", "Versión o modelo de simulación desconocido.");
    switch (spec.view) {
    case SimulationViewMode::array: case SimulationViewMode::groups: case SimulationViewMode::walkthrough:
    case SimulationViewMode::candidates: case SimulationViewMode::summary: break;
    default: throw std::invalid_argument("Vista de simulación desconocida.");
    }
    public_text(spec.title, 120, "title");
    public_text(spec.description, 4000, "description");
    for (const auto* label : {&spec.labels.array, &spec.labels.start, &spec.labels.end,
             &spec.labels.frequency, &spec.labels.maximum, &spec.labels.leaders, &spec.labels.output})
        public_text(*label, 80, "labels");
    for (const auto* explanation : {&spec.explanations.initial, &spec.explanations.start,
             &spec.explanations.advance, &spec.explanations.new_maximum, &spec.explanations.tie,
             &spec.explanations.below_maximum, &spec.explanations.done, &spec.explanations.empty})
        validate_template(*explanation, 2000, "explanations");
    validate_template(spec.output_template, 256, "outputTemplate", false);
    validate_values(spec.values, spec.text_values, !spec.sort);
    require(spec.scenarios.size() <= 8, "Una simulación admite como máximo 8 escenarios.");
    std::set<std::string> identities;
    for (const auto& scenario : spec.scenarios) {
        require(scenario_id(scenario.id) && identities.insert(scenario.id).second,
            "Un escenario tiene un identificador inválido o repetido.");
        public_text(scenario.label, 80, "scenarios/label");
        validate_values(scenario.values, spec.text_values, !spec.sort);
    }
}

std::string quoted_text(std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result = "\"";
    for (const unsigned char value : text) {
        switch (value) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (value < 0x20) {
                result += "\\u00";
                result += hex[value >> 4];
                result += hex[value & 0x0f];
            } else result += static_cast<char>(value);
        }
    }
    result += '"';
    return result;
}

std::string joined_values(const std::vector<SimulationValue>& values, bool display) {
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) result += ", ";
        result += display ? simulation_value_display_text(values[index]) : simulation_value_text(values[index]);
    }
    return result;
}

std::string_view explanation_template(const SimulationSpec& spec, SimulationPhase phase) {
    switch (phase) {
    case SimulationPhase::initial: return spec.explanations.initial;
    case SimulationPhase::start: return spec.explanations.start;
    case SimulationPhase::advance: return spec.explanations.advance;
    case SimulationPhase::new_maximum: return spec.explanations.new_maximum;
    case SimulationPhase::tie: return spec.explanations.tie;
    case SimulationPhase::below_maximum: return spec.explanations.below_maximum;
    case SimulationPhase::done: return spec.explanations.done;
    case SimulationPhase::empty: return spec.explanations.empty;
    }
    throw std::invalid_argument("Fase de simulación desconocida.");
}

std::string expanded(std::string_view source, const SimulationTrace& trace, const SimulationFrame& frame, bool display) {
    // The end of the half-open observed range is also the number of items
    // processed in the ordered copy. It remains valid at a completed-group
    // decision, at the initial frame, and after all items have been visited.
    require(frame.end <= trace.ordered.size(), "La fase de simulación supera el tamaño del array.");
    const bool has_value = frame.phase == SimulationPhase::start || frame.phase == SimulationPhase::advance ||
        frame.phase == SimulationPhase::new_maximum || frame.phase == SimulationPhase::tie ||
        frame.phase == SimulationPhase::below_maximum;
    // Only the start/advance phases count a new element. A decision about a
    // completed group must not impersonate an additional read. The index in
    // the ordered copy is end-1; its original index comes from the stable
    // permutation calculated once for this trace, including equal values.
    const bool just_counted = frame.phase == SimulationPhase::start ||
        frame.phase == SimulationPhase::advance;
    require(!just_counted || (frame.end > 0 && frame.end <= trace.source_indices.size()),
        "La fase de recuento no tiene una posición de origen válida.");
    const std::string counted = just_counted ? std::to_string(frame.end - 1) : std::string{};
    const std::string origin = just_counted ?
        std::to_string(trace.source_indices.at(frame.end - 1)) : std::string{};
    // A decision compares the just-completed group's frequency with the
    // maximum recorded by the preceding COUNT frame. The current frame may
    // already contain the NEW maximum. Never mistake it for the predecessor.
    const bool decision = frame.phase == SimulationPhase::new_maximum ||
        frame.phase == SimulationPhase::tie || frame.phase == SimulationPhase::below_maximum;
    require(!decision || !trace.frames.empty(),
        "Una comparación de frecuencia necesita un máximo anterior.");
    const std::string previous_maximum = decision ?
        std::to_string(trace.frames.back().maximum) : std::string{};
    const std::array<std::string, 14> replacements{
        has_value ? (display ? simulation_value_display_text(trace.groups.at(frame.group - 1).value)
                            : simulation_value_text(trace.groups.at(frame.group - 1).value)) : std::string{},
        std::to_string(frame.begin), std::to_string(frame.end), std::to_string(frame.frequency),
        std::to_string(frame.maximum), joined_values(frame.leaders, display), std::to_string(trace.ordered.size()),
        std::to_string(frame.group), std::to_string(trace.groups.size()), counted, origin,
        std::to_string(frame.end), std::to_string(trace.ordered.size() - frame.end), previous_maximum
    };
    std::string result;
    for (std::size_t at = 0; at < source.size();) {
        if (source[at] != '{') {
            require(result.size() < expanded_trace_limit, "La traza expandida supera 8 MiB.");
            result += source[at++];
            continue;
        }
        const auto end = source.find('}', at + 1);
        require(end != source.npos, "Marcador de simulación incompleto.");
        const auto name = source.substr(at + 1, end - at - 1);
        const auto found = std::find(placeholders.begin(), placeholders.end(), name);
        require(found != placeholders.end(), "Marcador de simulación desconocido.");
        const auto index = static_cast<std::size_t>(found - placeholders.begin());
        require(replacements[index].size() <= expanded_trace_limit - result.size(),
            "La traza expandida supera 8 MiB.");
        result += replacements[index];
        at = end + 1;
    }
    return result;
}

SimulationTrace make_trace(const SimulationSpec& spec, const std::vector<SimulationValue>& values) {
    SimulationTrace trace;
    trace.original = values;
    trace.source_indices.resize(values.size());
    std::iota(trace.source_indices.begin(), trace.source_indices.end(), std::size_t{0});
    if (spec.sort) std::stable_sort(trace.source_indices.begin(), trace.source_indices.end(),
        [&](std::size_t left, std::size_t right) {
            return value_less(values[left], values[right]);
        });
    trace.ordered.reserve(values.size());
    for (const auto original_index : trace.source_indices)
        trace.ordered.push_back(values[original_index]);
    for (std::size_t begin = 0; begin < trace.ordered.size();) {
        auto end = begin + 1;
        while (end < trace.ordered.size() && trace.ordered[end] == trace.ordered[begin]) ++end;
        trace.groups.push_back({begin, end, trace.ordered[begin]});
        begin = end;
    }
    std::size_t expanded_bytes = 0;
    const auto append = [&](SimulationFrame frame) {
        frame.explanation = expanded(explanation_template(spec, frame.phase), trace, frame, true);
        require(frame.explanation.size() <= expanded_trace_limit - expanded_bytes,
            "La traza expandida supera 8 MiB.");
        expanded_bytes += frame.explanation.size();
        require(frame.output.size() <= expanded_trace_limit - expanded_bytes,
            "La traza expandida supera 8 MiB.");
        expanded_bytes += frame.output.size();
        trace.frames.push_back(std::move(frame));
    };
    SimulationFrame current;
    if (values.empty()) {
        current.phase = SimulationPhase::empty;
        append(current);
        return trace;
    }
    append(current);
    for (std::size_t index = 0; index < trace.groups.size(); ++index) {
        const auto& group = trace.groups[index];
        current.phase = SimulationPhase::start;
        current.begin = group.begin;
        current.end = group.begin + 1;
        current.group = index + 1;
        current.frequency = 1;
        append(current);
        while (current.end < group.end) {
            ++current.end;
            ++current.frequency;
            current.phase = SimulationPhase::advance;
            append(current);
        }
        if (current.frequency > current.maximum) {
            current.phase = SimulationPhase::new_maximum;
            current.maximum = current.frequency;
            current.leaders = {group.value};
        } else if (current.frequency == current.maximum) {
            current.phase = SimulationPhase::tie;
            current.leaders.push_back(group.value);
        } else {
            current.phase = SimulationPhase::below_maximum;
        }
        const auto line = expanded(spec.output_template, trace, current, false);
        require(line.size() < expanded_trace_limit && current.output.size() <= expanded_trace_limit - line.size() - 1,
            "La salida expandida de la simulación supera 8 MiB.");
        current.output += line;
        current.output += '\n';
        trace.candidate_frames.push_back(trace.frames.size());
        append(current);
    }
    current.phase = SimulationPhase::done;
    current.begin = current.end = trace.ordered.size();
    current.frequency = 0;
    append(current);
    return trace;
}

SimulationTrace validated_trace(const SimulationSpec& spec) {
    validate_contract(spec);
    auto trace = make_trace(spec, spec.values);
    // Validate authored scenarios at load time too: the UI never first
    // discovers an overlarge expansion after the learner selects an option.
    for (const auto& scenario : spec.scenarios) (void)make_trace(spec, scenario.values);
    return trace;
}

// The editor's explicit JSON mode accepts only a bounded array of strings.
// This parser is independent of WinRT and does not reinterpret legacy lines.
class TextArrayInput {
public:
    explicit TextArrayInput(std::string_view input) : input_(input) {}
    std::vector<SimulationValue> parse() {
        spaces();
        require(take('['), "La lista debe comenzar con [ y contener sólo textos entre comillas.");
        std::vector<SimulationValue> result;
        spaces();
        if (!take(']')) {
            for (;;) {
                require(result.size() < item_limit, "Una simulación admite como máximo 64 valores.");
                result.emplace_back(string());
                spaces();
                if (take(']')) break;
                require(take(','), "Separa los textos con comas y termina la lista con ].");
                spaces();
            }
        }
        spaces();
        require(at_ == input_.size(), "Hay caracteres después del final de la lista.");
        return result;
    }
private:
    std::string_view input_;
    std::size_t at_ = 0;
    bool take(char expected) {
        if (at_ == input_.size() || input_[at_] != expected) return false;
        ++at_; return true;
    }
    void spaces() {
        while (at_ < input_.size() && (input_[at_] == ' ' || input_[at_] == '\t' ||
                input_[at_] == '\r' || input_[at_] == '\n')) ++at_;
    }
    std::uint32_t hex_quad() {
        require(input_.size() - at_ >= 4, "Un escape Unicode necesita cuatro cifras hexadecimales.");
        std::uint32_t result = 0;
        for (int count = 0; count < 4; ++count) {
            const auto digit = static_cast<unsigned char>(input_[at_++]);
            const int number = digit >= '0' && digit <= '9' ? digit - '0' :
                digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
                digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
            require(number >= 0, "Un escape Unicode contiene una cifra no válida.");
            result = (result << 4) | static_cast<std::uint32_t>(number);
        }
        return result;
    }
    static void append_point(std::string& result, std::uint32_t point) {
        if (point <= 0x7f) result += static_cast<char>(point);
        else if (point <= 0x7ff) {
            result += static_cast<char>(0xc0 | (point >> 6));
            result += static_cast<char>(0x80 | (point & 0x3f));
        } else if (point <= 0xffff) {
            result += static_cast<char>(0xe0 | (point >> 12));
            result += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (point & 0x3f));
        } else {
            result += static_cast<char>(0xf0 | (point >> 18));
            result += static_cast<char>(0x80 | ((point >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((point >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (point & 0x3f));
        }
    }
    std::string string() {
        require(take('"'), "Cada elemento de la lista debe ser un texto entre comillas.");
        std::string result;
        while (at_ < input_.size()) {
            const auto value = static_cast<unsigned char>(input_[at_++]);
            if (value == '"') return result;
            require(value >= 0x20, "Los controles dentro de una cadena deben escribirse como escapes JSON.");
            if (value != '\\') result += static_cast<char>(value);
            else {
                require(at_ < input_.size(), "La cadena termina con un escape incompleto.");
                switch (input_[at_++]) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    auto point = hex_quad();
                    if (point >= 0xd800 && point <= 0xdbff) {
                        require(take('\\') && take('u'), "Un sustituto Unicode alto necesita su pareja baja.");
                        const auto low = hex_quad();
                        require(low >= 0xdc00 && low <= 0xdfff, "La pareja Unicode no es válida.");
                        point = 0x10000 + ((point - 0xd800) << 10) + (low - 0xdc00);
                    } else require(point < 0xdc00 || point > 0xdfff, "Un sustituto Unicode bajo no puede aparecer solo.");
                    append_point(result, point);
                    break;
                }
                default: throw std::invalid_argument("La cadena contiene un escape JSON desconocido.");
                }
            }
            require(result.size() <= 128, "Cada texto decodificado admite como máximo 128 bytes UTF-8.");
        }
        throw std::invalid_argument("Faltan las comillas de cierre de un texto.");
    }
};

std::int64_t decimal(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    require(!value.empty(), "Cada línea debe contener un entero decimal completo.");
    const bool negative = value.front() == '-';
    if (negative || value.front() == '+') value.remove_prefix(1);
    require(!value.empty(), "Un signo aislado no es un entero decimal.");
    const std::uint64_t limit = negative ? 2147483648ULL : 2147483647ULL;
    std::uint64_t result = 0;
    for (const unsigned char digit : value) {
        require(digit >= '0' && digit <= '9', "Cada línea debe contener un entero decimal completo.");
        const auto number = static_cast<std::uint64_t>(digit - '0');
        require(result <= (limit - number) / 10, "El entero está fuera del intervalo de 32 bits con signo.");
        result = result * 10 + number;
    }
    const auto signed_result = static_cast<std::int64_t>(result);
    return negative ? -signed_result : signed_result;
}
std::vector<SimulationValue> parse_values_checked(const SimulationSpec& spec,
        std::string_view input, SimulationInputFormat format) {
    require(format == SimulationInputFormat::lines || format == SimulationInputFormat::json,
        "Formato de entrada de simulación desconocido.");
    require(input.size() <= (format == SimulationInputFormat::json ? 64 * 1024 : 64 * 130),
        "La entrada de simulación supera su límite de tamaño.");
    std::vector<SimulationValue> values;
    if (format == SimulationInputFormat::json) {
        require(spec.text_values, "La lista entre comillas sólo está disponible para datos de texto.");
        values = TextArrayInput(input).parse();
    } else for (std::size_t at = 0; at < input.size();) {
        const auto newline = input.find('\n', at);
        const auto end = newline == input.npos ? input.size() : newline;
        auto value = input.substr(at, end - at);
        if (newline != input.npos && !value.empty() && value.back() == '\r') value.remove_suffix(1);
        require(value.find('\r') == value.npos && !value.empty(),
            spec.text_values ? "Escribe un texto no vacío por línea; para una cadena vacía, elige Lista entre comillas."
                             : "Escribe un valor por línea, sin líneas vacías ni retornos aislados.");
        if (spec.text_values) values.emplace_back(std::string(value));
        else values.emplace_back(decimal(value));
        require(values.size() <= item_limit, "Una simulación admite como máximo 64 valores.");
        if (newline == input.npos) break;
        at = newline + 1;
    }
    validate_values(values, spec.text_values, !spec.sort);
    return values;
}

} // namespace

void validate_simulation_spec(const SimulationSpec& spec) {
    (void)validated_trace(spec);
}

SimulationSession::SimulationSession(SimulationSpec authored)
    : authored_(std::move(authored)), initial_(validated_trace(authored_)) {}

SimulationTrace SimulationSession::take_initial_trace() {
    require(!initial_taken_,
        "La traza inicial de una sesión de simulación ya se transfirió.");
    initial_taken_ = true;
    return std::move(initial_);
}

SimulationTrace SimulationSession::build_trace(std::vector<SimulationValue> values) const {
    validate_values(values, authored_.text_values, !authored_.sort);
    return make_trace(authored_, values);
}

std::vector<SimulationValue> SimulationSession::parse_values(
        std::string_view input, SimulationInputFormat format) const {
    // Conversion of a pending draft must retain the same validity guarantee
    // as the original public API: the calculated trace cannot exceed 8 MiB.
    // Reuse the once-authenticated contract, but still validate THIS input's
    // complete trace before offering a lossless format conversion.
    auto trace = make_trace(authored_, parse_values_checked(authored_, input, format));
    return std::move(trace.original);
}

SimulationTrace SimulationSession::parse_trace(
        std::string_view input, SimulationInputFormat format) const {
    return make_trace(authored_, parse_values_checked(authored_, input, format));
}

SimulationTrace build_simulation_trace(const SimulationSpec& spec,
        std::optional<std::vector<SimulationValue>> inputs) {
    auto original = validated_trace(spec);
    if (!inputs) return original;
    validate_values(*inputs, spec.text_values, !spec.sort);
    return make_trace(spec, *inputs);
}

std::string simulation_value_text(const SimulationValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_string(*integer);
    return std::get<std::string>(value);
}

std::string simulation_value_display_text(const SimulationValue& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return std::to_string(*integer);
    const auto& text = std::get<std::string>(value);
    auto result = quoted_text(text);
    if (text.empty()) result += " (cadena vacía)";
    else if (std::all_of(text.begin(), text.end(), [](char character) { return character == ' '; }))
        result += " (" + std::to_string(text.size()) + (text.size() == 1 ? " espacio)" : " espacios)");
    return result;
}

std::vector<std::string> simulation_count_observation_changes(
        const SimulationTrace& trace, const SimulationFrame& frame) {
    if (frame.phase != SimulationPhase::start &&
        frame.phase != SimulationPhase::advance) return {};
    require(frame.begin < frame.end && frame.end <= trace.ordered.size() &&
            frame.frequency == frame.end - frame.begin,
        "La fase de recuento no corresponde a un intervalo observado válido.");
    const auto observed = frame.end - 1;
    const auto& current_value = trace.ordered.at(observed);
    // A stable permutation is already calculated by the model. Display it
    // only when sorting actually changed the data, matching the native
    // cells and never disclosing an unobserved future element.
    const bool reordered = trace.original != trace.ordered;
    const auto origin_at = [&](std::size_t position) {
        require(trace.source_indices.size() == trace.ordered.size() &&
                position < trace.source_indices.size() &&
                trace.source_indices[position] < trace.original.size() &&
                trace.original[trace.source_indices[position]] == trace.ordered[position],
            "La procedencia calculada de la simulación es inválida.");
        return trace.source_indices[position];
    };
    std::vector<std::string> changes;
    std::string observation = "Dato contado: índice " + std::to_string(observed) +
        " = " + simulation_value_display_text(current_value);
    if (reordered)
        observation += " (origen índice " + std::to_string(origin_at(observed)) + ")";
    changes.push_back(std::move(observation));
    // The array interval [begin,end) has exactly end-begin counted values.
    // end is an exclusive boundary, not an additional element to read.
    changes.push_back("Recuento de este grupo: fin exclusivo " +
        std::to_string(frame.end) + " - inicio " + std::to_string(frame.begin) +
        " = " + std::to_string(frame.frequency) +
        (frame.frequency == 1 ? " elemento contado." : " elementos contados."));
    if (observed == 0) {
        changes.push_back("Primera posición: el índice 0 no tiene un elemento anterior.");
    } else {
        const auto& previous_value = trace.ordered.at(observed - 1);
        const bool equal = previous_value == current_value;
        changes.push_back("Relación con el anterior: índice " +
            std::to_string(observed - 1) + " = " +
            simulation_value_display_text(previous_value) +
            "; índice " + std::to_string(observed) + " = " +
            simulation_value_display_text(current_value) +
            (equal ? ". Son iguales: continúa el grupo." :
                     ". Son diferentes: comienza un grupo nuevo."));
        if (reordered)
            changes.push_back("Procedencia de la comparación: posición ordenada " +
                std::to_string(observed - 1) + " ← posición original " +
                std::to_string(origin_at(observed - 1)) +
                "; posición ordenada " + std::to_string(observed) +
                " ← posición original " + std::to_string(origin_at(observed)) + ".");
    }
    return changes;
}

std::string simulation_group_decision_comparison(
        const SimulationSpec& spec, const SimulationFrame& before,
        const SimulationFrame& decision) {
    const auto phase=decision.phase;
    if(phase!=SimulationPhase::new_maximum &&
       phase!=SimulationPhase::tie &&
       phase!=SimulationPhase::below_maximum)return {};
    // A decision is based on the completed current group, which has
    // frequency>0. The previous frame may be a counted observation or a
    // completed group in the grouped/candidate presentation. Both expose
    // the maximum known BEFORE this decision.
    require(decision.group>0 && decision.frequency>0 &&
            decision.end>decision.begin &&
            decision.frequency==decision.end-decision.begin &&
            ((phase==SimulationPhase::new_maximum &&
              decision.frequency>before.maximum &&
              decision.maximum==decision.frequency)||
             (phase==SimulationPhase::tie &&
              decision.frequency==before.maximum &&
              decision.maximum==before.maximum)||
             (phase==SimulationPhase::below_maximum &&
              decision.frequency<before.maximum &&
              decision.maximum==before.maximum)),
        "Una decisión de simulación no corresponde a su máximo anterior.");
    const char* relation=phase==SimulationPhase::new_maximum?">":
        phase==SimulationPhase::tie?"=":"<";
    const char* consequence=phase==SimulationPhase::new_maximum?
        "Nuevo máximo: reemplaza candidatos.":
        phase==SimulationPhase::tie?
        "Empate: agrega el valor a los candidatos.":
        "No supera el máximo: conserva los candidatos.";
    return "Comparación: "+spec.labels.frequency+" "+
        std::to_string(decision.frequency)+" "+relation+" "+
        spec.labels.maximum+" anterior "+std::to_string(before.maximum)+
        ". "+consequence;
}

std::string simulation_frame_text(const SimulationSpec& spec,
        const SimulationTrace& trace, const SimulationFrame& frame) {
    require(frame.begin <= frame.end && frame.end <= trace.ordered.size() &&
        frame.frequency == frame.end - frame.begin && frame.group <= trace.groups.size(),
        "El estado de simulación contiene un rango inválido.");
    std::string result = spec.title + '\n' + frame.explanation + '\n' + spec.labels.array + ":\n";
    for (std::size_t index = 0; index < trace.ordered.size(); ++index)
        result += std::to_string(index) + ": " + simulation_value_display_text(trace.ordered[index]) + '\n';
    result += spec.labels.start + ": " + std::to_string(frame.begin) + '\n';
    result += spec.labels.end + ": " + std::to_string(frame.end) + '\n';
    result += spec.labels.frequency + ": " + std::to_string(frame.frequency) + '\n';
    result += spec.labels.maximum + ": " + std::to_string(frame.maximum) + '\n';
    // A missing candidate is not the same as an empty-string candidate.
    // Match the visible Windows panel without changing calculated raw output.
    result += spec.labels.leaders + ": " +
        (frame.leaders.empty() ? std::string("ninguno") : joined_values(frame.leaders, true)) + '\n';
    result += spec.labels.output + ":\n" + frame.output;
    return result;
}

SimulationTrace simulation_parse_input_trace(const SimulationSpec& spec, std::string_view input,
        SimulationInputFormat format) {
    validate_simulation_spec(spec);
    return make_trace(spec, parse_values_checked(spec, input, format));
}

std::vector<SimulationValue> simulation_parse_input(const SimulationSpec& spec, std::string_view input,
        SimulationInputFormat format) {
    auto validated = simulation_parse_input_trace(spec, input, format);
    return std::move(validated.original);
}

std::string simulation_input_text(const std::vector<SimulationValue>& values, SimulationInputFormat format) {
    const bool text_values = !values.empty() && std::holds_alternative<std::string>(values.front());
    validate_values(values, text_values, false);
    require(format == SimulationInputFormat::lines || format == SimulationInputFormat::json,
        "Formato de entrada de simulación desconocido.");
    if (format == SimulationInputFormat::json) {
        require(values.empty() || text_values, "La lista entre comillas sólo está disponible para datos de texto.");
        std::string result = "[";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index) result += ", ";
            result += quoted_text(std::get<std::string>(values[index]));
        }
        return result + "]";
    }
    std::string result;
    for (std::size_t index = 0; index < values.size(); ++index) {
        require(!std::holds_alternative<std::string>(values[index]) || !std::get<std::string>(values[index]).empty(),
            "Una cadena vacía necesita el formato Lista entre comillas.");
        if (index) result += '\n';
        result += simulation_value_text(values[index]);
    }
    return result;
}
} // namespace lgaed::curriculum

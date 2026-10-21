#include "core/lesson_state.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace lgaed::state {
namespace {
constexpr std::size_t max_bytes = 128 * 1024 * 1024;
constexpr std::uint32_t max_entries = 1000000;
struct Writer {
    std::vector<std::uint8_t> bytes;
    void number(std::uint64_t value) {
        for (unsigned i = 0; i < 8; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
    }
    void text(std::string_view value) {
        if (value.size() > max_bytes || bytes.size() > max_bytes - value.size())
            throw std::runtime_error("El estado supera el limite de seguridad.");
        number(value.size()); bytes.insert(bytes.end(), value.begin(), value.end());
    }
};
struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t cursor = 0;
    std::uint64_t number() {
        if (bytes.size() - cursor < 8) throw std::runtime_error("Estado incompleto.");
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(bytes[cursor++]) << (i * 8);
        return value;
    }
    std::size_t count() {
        const auto value = number();
        if (value > max_entries) throw std::runtime_error("Demasiadas entradas de estado.");
        return static_cast<std::size_t>(value);
    }
    std::string text() {
        const auto size = number();
        if (size > bytes.size() - cursor) throw std::runtime_error("Texto de estado incompleto.");
        std::string result(reinterpret_cast<const char*>(bytes.data() + cursor), static_cast<std::size_t>(size));
        cursor += static_cast<std::size_t>(size); return result;
    }
};
}

const Value* find(const StateData& data, std::string_view domain, std::string_view key) {
    const auto group = data.preferences.find(std::string(domain));
    if (group == data.preferences.end()) return nullptr;
    const auto value = group->second.find(std::string(key));
    return value == group->second.end() ? nullptr : &value->second;
}
std::string string_value(const StateData& data, std::string_view domain,
                         std::string_view key, std::string_view fallback) {
    const auto value = find(data, domain, key);
    if (value) {
        if (const auto text = std::get_if<std::string>(value)) return *text;
        throw std::runtime_error("El tipo del estado guardado no coincide; se conserva sin modificar.");
    }
    return std::string(fallback);
}
std::int64_t integer_value(const StateData& data, std::string_view domain,
                           std::string_view key, std::int64_t fallback) {
    const auto value = find(data, domain, key);
    if (value) {
        if (const auto number = std::get_if<std::int64_t>(value)) return *number;
        throw std::runtime_error("El tipo del estado guardado no coincide.");
    }
    return fallback;
}
std::vector<std::uint8_t> encode(const StateData& data) {
    if (data.preferences.size() > max_entries) throw std::runtime_error("Demasiados dominios de estado.");
    Writer out; out.text("LearnGameAndEngineDev.state"); out.number(1);
    out.number(data.preferences.size());
    for (const auto& [domain, entries] : data.preferences) {
        if (entries.size() > max_entries) throw std::runtime_error("Demasiadas preferencias.");
        out.text(domain); out.number(entries.size());
        for (const auto& [key, value] : entries) {
            out.text(key); out.number(value.index());
            std::visit([&out](const auto& item) {
                using T = std::decay_t<decltype(item)>;
                if constexpr (std::is_same_v<T, std::string>) out.text(item);
                else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                    if (item.size() > max_entries) throw std::runtime_error("Demasiados valores de estado.");
                    out.number(item.size()); for (const auto& text : item) out.text(text);
                } else if constexpr (std::is_same_v<T, double>) out.number(std::bit_cast<std::uint64_t>(item));
                else out.number(static_cast<std::uint64_t>(item));
            }, value);
        }
    }
    if (out.bytes.size() > max_bytes) throw std::runtime_error("Estado demasiado grande.");
    return out.bytes;
}
StateData decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > max_bytes) throw std::runtime_error("Estado demasiado grande.");
    Reader in{bytes};
    if (in.text() != "LearnGameAndEngineDev.state" || in.number() != 1)
        throw std::runtime_error("Version del estado Windows no compatible.");
    StateData result;
    const auto domains = in.count();
    for (std::size_t d = 0; d < domains; ++d) {
        const auto domain = in.text();
        if (result.preferences.contains(domain)) throw std::runtime_error("Dominio duplicado.");
        auto& entries = result.preferences[domain]; const auto count = in.count();
        for (std::size_t e = 0; e < count; ++e) {
            const auto key = in.text(); Value value;
            switch (in.number()) {
            case 0: value = in.text(); break;
            case 1: value = std::bit_cast<std::int64_t>(in.number()); break;
            case 2: value = std::bit_cast<double>(in.number()); break;
            case 3: {
                const auto boolean = in.number();
                if (boolean > 1) throw std::runtime_error("Booleano danado.");
                value = boolean == 1; break;
            }
            case 4: {
                std::vector<std::string> list; const auto size = in.count();
                for (std::size_t j = 0; j < size; ++j) list.push_back(in.text());
                value = std::move(list); break;
            }
            default: throw std::runtime_error("Tipo de preferencia no reconocido.");
            }
            if (!entries.emplace(key, std::move(value)).second) throw std::runtime_error("Clave duplicada.");
        }
    }
    if (in.cursor != bytes.size()) throw std::runtime_error("Estado con datos adicionales desconocidos.");
    return result;
}
void set_saved_page(StateData& data, std::string_view lesson_id, std::size_t historical_page) {
    if (historical_page > 1000000) throw std::runtime_error("Pagina de estado no valida.");
    auto& entries = data.preferences["learning_progress"];
    const auto prefix = "lesson_" + std::string(lesson_id);
    entries[prefix + "_page"] = static_cast<std::int64_t>(historical_page);
    entries[prefix + "_started"] = true;
}
std::size_t saved_page(const StateData& data, std::string_view lesson_id) {
    const auto value = integer_value(data, "learning_progress", "lesson_" + std::string(lesson_id) + "_page");
    if (value < 0 || value > 1000000) throw std::runtime_error("Pagina guardada no valida.");
    return static_cast<std::size_t>(value);
}
} // namespace lgaed::state

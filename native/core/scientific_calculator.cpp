#include "core/scientific_calculator.hpp"

#include <charconv>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace lgaed::math {
namespace {
struct Parser {
    std::string_view source;
    AngleUnit unit;
    std::size_t cursor = 0, operations = 0, depth = 0;
    [[noreturn]] void error(std::string_view message) const {
        throw std::invalid_argument(std::string(message) + " (posición " + std::to_string(cursor + 1) + ").");
    }
    double checked(double result) {
        if (++operations > 4096) error("La expresión contiene demasiadas operaciones");
        if (!std::isfinite(result)) error("El resultado no es un número real finito");
        return result;
    }
    void whitespace() { while (cursor < source.size() && (source[cursor] == ' ' || source[cursor] == '\t' || source[cursor] == '\r' || source[cursor] == '\n')) ++cursor; }
    bool take(std::string_view token) {
        whitespace(); if (!source.substr(cursor).starts_with(token)) return false; cursor += token.size(); return true;
    }
    double sum() {
        if (++depth > 128) error("Demasiados paréntesis o exponentes anidados");
        struct Restore { std::size_t& depth; ~Restore() { --depth; } } restore{depth};
        auto result = product();
        for (;;) {
            if (take("+")) result = checked(result + product());
            else if (take("-") || take("−")) result = checked(result - product());
            else return result;
        }
    }
    double product() {
        auto result = unary();
        for (;;) {
            if (take("*") || take("×")) result = checked(result * unary());
            else if (take("/") || take("÷")) { const auto value = unary(); if (value == 0) error("No se puede dividir entre cero"); result = checked(result / value); }
            else return result;
        }
    }
    double unary() {
        if (++depth > 128) error("Demasiados signos o exponentes anidados");
        struct Restore { std::size_t& depth; ~Restore() { --depth; } } restore{depth};
        if (take("+")) return unary();
        if (take("-") || take("−")) return checked(-unary());
        auto result = primary();
        // CalculatorExpressionCompiler.postfix: % always divides the preceding
        // value by 100, before exponentiation; it is not C++'s binary remainder.
        for (;;) {
            if (take("%")) result = checked(result / 100.0);
            else if (take("!")) {
                if (result < 0 || result != std::trunc(result) || result > 170) error("El factorial requiere un entero entre 0 y 170");
                auto factorial = 1.0; for (unsigned value = 2; value <= static_cast<unsigned>(result); ++value) factorial *= value;
                result = checked(factorial);
            } else break;
        }
        if (take("^")) result = checked(std::pow(result,unary()));
        return result;
    }
    double function(std::string_view name,double value) {
        const auto angle = unit == AngleUnit::degrees ? value * std::numbers::pi / 180 : value;
        const auto inverse = [&](double result) { return unit == AngleUnit::degrees ? result * 180 / std::numbers::pi : result; };
        if (name == "sin" || name == "sen") return checked(std::sin(angle));
        if (name == "cos") return checked(std::cos(angle));
        if (name == "tan") { if (std::abs(std::cos(angle)) < 1e-14) error("La tangente no está definida en ese ángulo"); return checked(std::tan(angle)); }
        if (name == "asin") return checked(inverse(std::asin(value)));
        if (name == "acos") return checked(inverse(std::acos(value)));
        if (name == "atan") return checked(inverse(std::atan(value)));
        if (name == "ln") return checked(std::log(value));
        if (name == "log") return checked(std::log10(value));
        if (name == "exp") return checked(std::exp(value));
        if (name == "sqrt") return checked(std::sqrt(value));
        if (name == "abs") return checked(std::abs(value));
        error("Función desconocida");
    }
    double primary() {
        if (take("(")) { const auto result = sum(); if (!take(")")) error("Falta cerrar un paréntesis"); return result; }
        if (take("π")) return std::numbers::pi;
        whitespace(); const auto start = cursor;
        while (cursor < source.size() && ((source[cursor] >= 'a' && source[cursor] <= 'z') || (source[cursor] >= 'A' && source[cursor] <= 'Z'))) ++cursor;
        if (cursor > start) {
            std::string name(source.substr(start,cursor - start)); for (auto& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (name == "pi") return std::numbers::pi;
            if (name == "e") return std::numbers::e;
            if (!take("(")) error("La función necesita paréntesis");
            const auto argument = sum(); if (!take(")")) error("Falta cerrar la función");
            return function(name,argument);
        }
        bool digits = false;
        while (cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9') { ++cursor; digits = true; }
        if (cursor < source.size() && source[cursor] == '.') {
            ++cursor; while (cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9') { ++cursor; digits = true; }
        }
        if (!digits) error("Se esperaba un número, una constante o una función");
        if (cursor < source.size() && (source[cursor] == 'e' || source[cursor] == 'E')) {
            ++cursor; if (cursor < source.size() && (source[cursor] == '+' || source[cursor] == '-')) ++cursor;
            const auto exponent = cursor;
            while (cursor < source.size() && source[cursor] >= '0' && source[cursor] <= '9') ++cursor;
            if (cursor == exponent) error("El exponente necesita dígitos");
        }
        double result = 0;
        const auto number = source.substr(start,cursor - start);
        const auto parsed = std::from_chars(number.data(),number.data() + number.size(),result,std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) error("Número no compatible o fuera de rango");
        return checked(result);
    }
};
}
double calculate(std::string_view expression,AngleUnit unit) {
    if (expression.empty() || expression.size() > 8192) throw std::invalid_argument("Escribe una expresión de hasta 8192 caracteres.");
    Parser parser{expression,unit}; const auto result = parser.sum(); parser.whitespace();
    if (parser.cursor != expression.size()) parser.error("Falta un operador o sobra texto");
    return result;
}
std::string format_calculation(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("El resultado no es finito.");
    if (value == 0) value = 0; // Do not present negative zero after a valid calculation.
    char result[128]{};
    const auto parsed = std::to_chars(result,result + sizeof(result),value,std::chars_format::general,15);
    if (parsed.ec != std::errc{}) throw std::runtime_error("No se pudo representar el resultado.");
    return {result,parsed.ptr};
}
} // namespace lgaed::math

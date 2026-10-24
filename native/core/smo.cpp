#include "core/smo.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace lgaed::smo {
namespace {
constexpr std::size_t source_limit = 64 * 1024;
constexpr std::size_t node_limit = 8192;
constexpr std::size_t statement_limit = 4096;
constexpr std::size_t vertex_limit = 1024;
constexpr std::size_t face_limit = 2048;
constexpr std::size_t variable_limit = 256;
constexpr unsigned depth_limit = 128;

double finite(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("El resultado queda fuera del rango FP64 del prototipo SMO.");
    return value == 0 ? 0 : value;
}
void validate_notation(const Notation& notation) {
    if (notation.zero != '0') throw std::invalid_argument("En SMO, 0 sigue representando cero.");
    const auto symbol = [](char value) {
        return static_cast<unsigned char>(value) < 128 && std::isprint(static_cast<unsigned char>(value)) && !std::isalnum(static_cast<unsigned char>(value)) &&
            !std::isspace(static_cast<unsigned char>(value)) && std::string_view("_+-*/=.,;(){}\"'\\").find(value) == std::string_view::npos;
    };
    if (!symbol(notation.ten) || !symbol(notation.eleven) || !symbol(notation.twelve_alias) ||
        notation.ten == notation.eleven || notation.ten == notation.twelve_alias || notation.eleven == notation.twelve_alias)
        throw std::invalid_argument("Los símbolos numéricos SMO deben ser distintos de los operadores y comentarios.");
}
int digit(char value, const Notation& notation) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value == notation.ten) return 10;
    if (value == notation.eleven) return 11;
    return -1;
}
char numeral(int value, const Notation& notation) {
    return value < 10 ? static_cast<char>('0' + value) : value == 10 ? notation.ten : notation.eleven;
}
unsigned exponent_magnitude(double base, double exponent) {
    finite(base); finite(exponent);
    if (!(base > 0)) throw std::invalid_argument("Las potencias del prototipo SMO admiten base positiva; otras bases siguen sin definirse.");
    if (std::trunc(exponent) != exponent || std::abs(exponent) > 64)
        throw std::invalid_argument("El prototipo admite exponentes enteros entre -64 y 64 (magnitudes decimales).");
    return static_cast<unsigned>(std::abs(exponent));
}
double positive_power(double base, unsigned exponent) {
    const auto value = finite(std::pow(base, static_cast<double>(exponent)));
    if (value == 0) throw std::runtime_error("La potencia positiva no cabe en FP64 sin perderse como cero.");
    return value;
}
struct Location { std::size_t line = 1, column = 1; };
[[noreturn]] void fail(Location location, std::string_view reason) {
    throw std::runtime_error("Línea " + std::to_string(location.line) + ", columna " + std::to_string(location.column) + ": " + std::string(reason));
}
enum class TokenKind { end, identifier, number, plus, minus, times, divide, equal, left, right, open, close, comma, semicolon };
struct Token { TokenKind kind = TokenKind::end; std::string text; Location where; };
class Lexer {
    std::string_view source_;
    Notation notation_;
    std::size_t at_ = 0;
    Location location_;
    void advance() {
        if (source_[at_] == '\r') { ++location_.line; location_.column = 1; }
        else if (source_[at_] == '\n') { if (at_ == 0 || source_[at_ - 1] != '\r') ++location_.line; location_.column = 1; }
        else ++location_.column;
        ++at_;
    }
public:
    Lexer(std::string_view source, Notation notation) : source_(source), notation_(notation) {
        validate_notation(notation_);
        if (source_.size() > source_limit) throw std::length_error("El programa SMO supera el límite de 64 KiB del prototipo.");
    }
    Token next() {
        for (;;) {
            while (at_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[at_]))) advance();
            if (source_.substr(at_, 2) != "//") break;
            while (at_ < source_.size() && source_[at_] != '\n' && source_[at_] != '\r') advance();
        }
        const auto where = location_;
        if (at_ == source_.size()) return {TokenKind::end, {}, where};
        const auto first = at_;
        const auto c = source_[at_];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            do advance(); while (at_ < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[at_])) || source_[at_] == '_'));
            return {TokenKind::identifier, std::string(source_.substr(first, at_ - first)), where};
        }
        if (digit(c, notation_) >= 0 || c == '.' || c == notation_.twelve_alias) {
            do advance(); while (at_ < source_.size() && (digit(source_[at_], notation_) >= 0 || source_[at_] == '.' || source_[at_] == notation_.twelve_alias));
            return {TokenKind::number, std::string(source_.substr(first, at_ - first)), where};
        }
        advance();
        switch (c) {
        case '+': return {TokenKind::plus, "+", where}; case '-': return {TokenKind::minus, "-", where};
        case '*': return {TokenKind::times, "*", where}; case '/': return {TokenKind::divide, "/", where};
        case '=': return {TokenKind::equal, "=", where}; case '(': return {TokenKind::left, "(", where};
        case ')': return {TokenKind::right, ")", where}; case '{': return {TokenKind::open, "{", where};
        case '}': return {TokenKind::close, "}", where}; case ',': return {TokenKind::comma, ",", where};
        case ';': return {TokenKind::semicolon, ";", where};
        default: fail(where, "Carácter no reconocido en SMO.");
        }
    }
};
struct Expression {
    enum class Kind { literal, variable, negate, positive, add, subtract, multiply, divide, direct, inverted, alternative };
    Kind kind = Kind::literal;
    Location where;
    Number value;
    std::string literal;
    std::string name;
    std::unique_ptr<Expression> left, right;
};
struct Statement {
    enum class Kind { state, vertex, face, assign, rotation, color, draw };
    Kind kind = Kind::draw;
    Location where;
    std::string name;
    std::vector<std::unique_ptr<Expression>> arguments;
};
using Statements = std::vector<Statement>;
struct Syntax { Statements initial, update, draw; };
class Parser {
    Lexer lexer_;
    Notation notation_;
    Token token_;
    std::size_t nodes_ = 0, statements_ = 0;
    std::set<std::string> variables_;
    bool raw_literals_ = false;
    void validate_literal(const Token& token) const {
        // Grammar only: no FP64 value or range is constructed on the IR path.
        if (token.text.size()==1 && token.text.front()==notation_.twelve_alias) return;
        bool point=false,seen=false;
        for (const auto c:token.text) {
            if (c==notation_.twelve_alias) fail(token.where,"@ es una abreviatura independiente de doce; doce posicional se escribe 10.");
            if (c=='.') { if (point) fail(token.where,"Un número sólo puede tener un punto de base doce.");point=true;continue; }
            if (digit(c,notation_)<0) fail(token.where,"Cifra inválida para base doce.");
            seen=true;
        }
        if (!seen) fail(token.where,"El punto necesita al menos una cifra.");
    }
    void next() { token_ = lexer_.next(); }
    bool take(TokenKind kind) { if (token_.kind != kind) return false; next(); return true; }
    void expect(TokenKind kind, std::string_view reason) { if (!take(kind)) fail(token_.where, reason); }
    std::unique_ptr<Expression> node(Expression::Kind kind, Location where) {
        if (++nodes_ > node_limit) fail(where, "Demasiadas expresiones para este prototipo.");
        auto result = std::make_unique<Expression>(); result->kind = kind; result->where = where; return result;
    }
    std::unique_ptr<Expression> atom(unsigned depth) {
        if (depth > depth_limit) fail(token_.where, "Expresión demasiado profunda.");
        const auto token = token_;
        if (token.kind == TokenKind::number) {
            next(); auto result = node(Expression::Kind::literal, token.where);
            if (raw_literals_) { validate_literal(token);result->literal=token.text; }
            else {
                try { result->value = parse_number(token.text, notation_); }
                catch (const std::exception& error) { fail(token.where, error.what()); }
            }
            return result;
        }
        if (take(TokenKind::minus) || take(TokenKind::plus)) {
            auto result = node(token.kind == TokenKind::minus ? Expression::Kind::negate : Expression::Kind::positive, token.where);
            result->left = atom(depth + 1); return result;
        }
        if (take(TokenKind::left)) {
            auto result = expression(depth + 1); expect(TokenKind::right, "Falta ')' en la expresión."); return result;
        }
        if (token.kind != TokenKind::identifier) fail(token.where, "Se esperaba un número, estado o expresión.");
        next();
        if (!take(TokenKind::left)) {
            auto result = node(Expression::Kind::variable, token.where); result->name = token.text; return result;
        }
        Expression::Kind kind;
        if (token.text == "pdir") kind = Expression::Kind::direct;
        else if (token.text == "pinv") kind = Expression::Kind::inverted;
        else if (token.text == "alternativa") kind = Expression::Kind::alternative;
        else fail(token.where, "Función SMO desconocida: " + token.text);
        auto result = node(kind, token.where); result->left = expression(depth + 1);
        if (kind != Expression::Kind::alternative) { expect(TokenKind::comma, "La potencia requiere base y exponente separados por ','."); result->right = expression(depth + 1); }
        expect(TokenKind::right, "Falta ')' después de la función."); return result;
    }
    std::unique_ptr<Expression> product(unsigned depth) {
        auto result = atom(depth);
        unsigned chain = 0;
        while (token_.kind == TokenKind::times || token_.kind == TokenKind::divide) {
            if (++chain > depth_limit) fail(token_.where, "Cadena de operaciones demasiado profunda.");
            const auto token = token_; next(); auto combined = node(token.kind == TokenKind::times ? Expression::Kind::multiply : Expression::Kind::divide, token.where);
            combined->left = std::move(result); combined->right = atom(depth + 1); result = std::move(combined);
        }
        return result;
    }
    std::unique_ptr<Expression> expression(unsigned depth = 0) {
        auto result = product(depth);
        unsigned chain = 0;
        while (token_.kind == TokenKind::plus || token_.kind == TokenKind::minus) {
            if (++chain > depth_limit) fail(token_.where, "Cadena de operaciones demasiado profunda.");
            const auto token = token_; next(); auto combined = node(token.kind == TokenKind::plus ? Expression::Kind::add : Expression::Kind::subtract, token.where);
            combined->left = std::move(result); combined->right = product(depth + 1); result = std::move(combined);
        }
        return result;
    }
    Statement call(Statement::Kind kind, Location where, std::size_t count) {
        Statement result; result.kind = kind; result.where = where;
        expect(TokenKind::left, "Falta '(' después de la instrucción.");
        for (std::size_t i = 0; i < count; ++i) { if (i) expect(TokenKind::comma, "Falta ',' entre argumentos."); result.arguments.push_back(expression()); }
        expect(TokenKind::right, "Falta ')' después de los argumentos.");
        expect(TokenKind::semicolon, "Falta ';' después de la instrucción."); return result;
    }
    Statement statement(int phase) {
        if (++statements_ > statement_limit) fail(token_.where, "Demasiadas instrucciones para este prototipo.");
        const auto token = token_;
        if (token.kind != TokenKind::identifier) fail(token.where, "Se esperaba una instrucción SMO.");
        next();
        if (phase == 0 && token.text == "estado") {
            const auto name = token_;
            if (name.kind != TokenKind::identifier || name.text == "dt" || name.text == "pdir" || name.text == "pinv" || name.text == "alternativa")
                fail(name.where, "Nombre de estado no válido o reservado.");
            if (variables_.size() >= variable_limit || !variables_.insert(name.text).second) fail(name.where, "Estado duplicado o demasiados estados.");
            next(); expect(TokenKind::equal, "Falta '=' en el estado.");
            Statement result; result.kind = Statement::Kind::state; result.where = token.where; result.name = name.text;
            result.arguments.push_back(expression()); expect(TokenKind::semicolon, "Falta ';' en la declaración."); return result;
        }
        if (phase == 0 && token.text == "vertice") return call(Statement::Kind::vertex, token.where, 3);
        if (phase == 0 && token.text == "cara") return call(Statement::Kind::face, token.where, 3);
        if (phase == 1) {
            if (token.text == "dt") fail(token.where, "dt pertenece al reloj del soporte y no se puede asignar.");
            Statement result; result.kind = Statement::Kind::assign; result.where = token.where; result.name = token.text;
            expect(TokenKind::equal, "En al_actualizar se esperaba una asignación de estado.");
            result.arguments.push_back(expression()); expect(TokenKind::semicolon, "Falta ';' después de la asignación."); return result;
        }
        if (phase == 2 && token.text == "rotacion") return call(Statement::Kind::rotation, token.where, 3);
        if (phase == 2 && token.text == "color") return call(Statement::Kind::color, token.where, 3);
        if (phase == 2 && token.text == "dibujar") return call(Statement::Kind::draw, token.where, 0);
        fail(token.where, "La instrucción no pertenece a esta fase de SMO: " + token.text);
    }
    void block(Statements& output, int phase) {
        expect(TokenKind::open, "Falta '{' para abrir el bloque.");
        while (token_.kind != TokenKind::close) {
            if (token_.kind == TokenKind::end) fail(token_.where, "Falta '}' para cerrar el bloque.");
            output.push_back(statement(phase));
        }
        next();
    }
    void validate(const Expression& expression, bool update, unsigned depth = 0) const {
        if (depth > depth_limit) fail(expression.where, "Expresión demasiado profunda.");
        if (expression.kind == Expression::Kind::variable && !variables_.contains(expression.name) && !(update && expression.name == "dt"))
            fail(expression.where, "Estado desconocido: " + expression.name);
        if (expression.left) validate(*expression.left, update, depth + 1);
        if (expression.right) validate(*expression.right, update, depth + 1);
    }
public:
    Parser(std::string_view source, Notation notation,bool raw_literals=false) : lexer_(source, notation), notation_(notation),raw_literals_(raw_literals) { next(); }
    Syntax parse() {
        Syntax result; bool update = false, draw = false;
        while (token_.kind != TokenKind::end) {
            if (token_.kind == TokenKind::identifier && (token_.text == "al_actualizar" || token_.text == "al_dibujar")) {
                const auto token = token_; next(); const bool updating = token.text == "al_actualizar";
                auto& seen = updating ? update : draw;
                if (seen) fail(token.where, "Bloque SMO duplicado."); seen = true;
                block(updating ? result.update : result.draw, updating ? 1 : 2);
            } else result.initial.push_back(statement(0));
        }
        if (!update || !draw) fail(token_.where, "El programa debe declarar al_actualizar y al_dibujar, aunque sus bloques estén vacíos.");
        for (const auto* phase : {&result.initial, &result.update, &result.draw}) for (const auto& statement : *phase) {
            if (statement.kind == Statement::Kind::assign && !variables_.contains(statement.name)) fail(statement.where, "No existe el estado que se intenta actualizar: " + statement.name);
            for (const auto& argument : statement.arguments) validate(*argument, phase == &result.update);
        }
        return result;
    }
};

class IrExporter {
    ProgramIr result_;
    std::map<std::string,std::uint32_t,std::less<>> slots_;
    std::set<std::string,std::less<>> initialized_;
    std::uint32_t stack_=0;
    void instruction(std::vector<IrInstruction>& code,IrOpcode opcode,Location where,std::uint32_t operand=0) {
        code.push_back({opcode,operand,static_cast<std::uint32_t>(where.line),static_cast<std::uint32_t>(where.column)});
    }
    void push() { ++stack_;result_.max_stack=std::max(result_.max_stack,stack_); }
    void expression(const Expression& value,bool initializing,bool updating,std::vector<IrInstruction>& code) {
        if (value.kind==Expression::Kind::literal) {
            const auto index=static_cast<std::uint32_t>(result_.literals.size());result_.literals.push_back(value.literal);
            instruction(code,IrOpcode::literal,value.where,index);push();return;
        }
        if (value.kind==Expression::Kind::variable) {
            if (updating&&value.name=="dt") instruction(code,IrOpcode::dt,value.where);
            else {
                const auto found=slots_.find(value.name);
                if (found==slots_.end()||(initializing&&!initialized_.contains(value.name)))
                    fail(value.where,"El estado aún no está inicializado: "+value.name);
                instruction(code,IrOpcode::variable,value.where,found->second);
            }
            push();return;
        }
        expression(*value.left,initializing,updating,code);
        IrOpcode opcode;
        switch(value.kind) {
        case Expression::Kind::negate:opcode=IrOpcode::negate;break;
        case Expression::Kind::positive:opcode=IrOpcode::positive;break;
        case Expression::Kind::alternative:opcode=IrOpcode::alternative;break;
        default:
            expression(*value.right,initializing,updating,code);
            if (stack_<2) throw std::logic_error("IR SMO con pila binaria incompleta.");
            --stack_;
            switch(value.kind) {
            case Expression::Kind::add:opcode=IrOpcode::add;break;
            case Expression::Kind::subtract:opcode=IrOpcode::subtract;break;
            case Expression::Kind::multiply:opcode=IrOpcode::multiply;break;
            case Expression::Kind::divide:opcode=IrOpcode::divide;break;
            case Expression::Kind::direct:opcode=IrOpcode::direct_power;break;
            case Expression::Kind::inverted:opcode=IrOpcode::inverted_power;break;
            default:throw std::logic_error("Expresión IR SMO desconocida.");
            }
            break;
        }
        instruction(code,opcode,value.where);
    }
    void statements(const Statements& phase,bool initializing,bool updating,std::vector<IrInstruction>& code) {
        for (const auto& value:phase) {
            if (stack_!=0) throw std::logic_error("IR SMO con pila residual entre instrucciones.");
            for (const auto& argument:value.arguments) expression(*argument,initializing,updating,code);
            IrOpcode opcode;
            switch(value.kind) {
            case Statement::Kind::state:opcode=IrOpcode::state;break;
            case Statement::Kind::vertex:
                opcode=IrOpcode::vertex;if (++result_.vertices>vertex_limit) fail(value.where,"Demasiados vértices para este laboratorio.");break;
            case Statement::Kind::face:
                opcode=IrOpcode::face;if (++result_.faces>face_limit) fail(value.where,"Demasiadas caras para este laboratorio.");break;
            case Statement::Kind::assign:opcode=IrOpcode::assign;break;
            case Statement::Kind::rotation:opcode=IrOpcode::rotation;break;
            case Statement::Kind::color:opcode=IrOpcode::color;break;
            case Statement::Kind::draw:opcode=IrOpcode::draw;break;
            default:throw std::logic_error("Instrucción IR SMO desconocida.");
            }
            if (stack_!=value.arguments.size()) throw std::logic_error("IR SMO con pila de argumentos inválida.");
            const auto operand=(value.kind==Statement::Kind::state||value.kind==Statement::Kind::assign)?slots_.at(value.name):0u;
            instruction(code,opcode,value.where,operand);stack_=0;
            if (value.kind==Statement::Kind::state) initialized_.insert(value.name);
        }
    }
public:
    ProgramIr compile(const Syntax& syntax,Notation notation,bool require_geometry=true) {
        result_.notation=notation;
        for (const auto& value:syntax.initial) if (value.kind==Statement::Kind::state) {
            const auto index=static_cast<std::uint32_t>(result_.states.size());
            slots_.emplace(value.name,index);result_.states.push_back(value.name);
        }
        statements(syntax.initial,true,false,result_.initial);
        if (require_geometry&&(result_.vertices<3||result_.faces==0)) throw std::invalid_argument("Define al menos tres vértices y una cara en SMO.");
        statements(syntax.update,false,true,result_.update);
        statements(syntax.draw,false,false,result_.draw);
        return std::move(result_);
    }
};

using Variables = std::map<std::string, Number, std::less<>>;
Number evaluate(const Expression& expression, const Variables& variables, double dt, bool updating) {
    try {
        if (expression.kind == Expression::Kind::literal) return expression.value;
        if (expression.kind == Expression::Kind::variable) {
            if (updating && expression.name == "dt") return {dt, {}};
            if (const auto found = variables.find(expression.name); found != variables.end()) return found->second;
            throw std::runtime_error("El estado aún no está inicializado: " + expression.name);
        }
        const auto left = evaluate(*expression.left, variables, dt, updating);
        switch (expression.kind) {
        case Expression::Kind::negate: return {finite(-left.value), {}};
        case Expression::Kind::positive: return {left.value, {}};
        case Expression::Kind::alternative:
            if (!left.alternative) throw std::invalid_argument("La expresión no tiene una segunda interpretación.");
            return {*left.alternative, {}};
        default: break;
        }
        const auto right = evaluate(*expression.right, variables, dt, updating);
        switch (expression.kind) {
        case Expression::Kind::add: return {finite(left.value + right.value), {}};
        case Expression::Kind::subtract: return {finite(left.value - right.value), {}};
        case Expression::Kind::multiply: return multiply(left.value, right.value);
        case Expression::Kind::divide: return divide(left.value, right.value);
        case Expression::Kind::direct: return direct_power(left.value, right.value);
        case Expression::Kind::inverted: return inverted_power(left.value, right.value);
        default: throw std::logic_error("Expresión SMO desconocida.");
        }
    } catch (const std::exception& error) {
        if (std::string_view(error.what()).starts_with("Línea ")) throw;
        fail(expression.where, error.what());
    }
}
Vec3 vector(const Statement& statement, const Variables& variables) {
    return {evaluate(*statement.arguments[0], variables, 0, false).value, evaluate(*statement.arguments[1], variables, 0, false).value,
        evaluate(*statement.arguments[2], variables, 0, false).value};
}
} // namespace

Number parse_number(std::string_view text, Notation notation) {
    validate_notation(notation);
    if (text.empty()) throw std::invalid_argument("Número SMO vacío.");
    bool negative = false;
    if (text.front() == '-' || text.front() == '+') { negative = text.front() == '-'; text.remove_prefix(1); }
    if (text.empty()) throw std::invalid_argument("Faltan cifras SMO.");
    if (text.size() == 1 && text.front() == notation.twelve_alias) return {negative ? -12. : 12., {}};
    if (text.find(notation.twelve_alias) != text.npos) throw std::invalid_argument("@ es una abreviatura independiente de doce; doce posicional se escribe 10.");
    bool fraction = false, seen = false, nonzero = false;
    double integer = 0, fractional = 0, place = 1;
    for (const auto c : text) {
        if (c == '.') { if (fraction) throw std::invalid_argument("Un número sólo puede tener un punto de base doce."); fraction = true; continue; }
        const auto value = digit(c, notation);
        if (value < 0) throw std::invalid_argument("Cifra inválida para base doce: " + std::string(1, c));
        seen = true;
        nonzero = nonzero || value != 0;
        if (fraction) { place /= 12; fractional += value * place; }
        else integer = finite(integer * 12 + value);
    }
    if (!seen) throw std::invalid_argument("El punto necesita al menos una cifra.");
    const auto value = finite((integer + fractional) * (negative ? -1 : 1));
    if (nonzero && value == 0) throw std::range_error("La fracción SMO no cabe en FP64 sin perderse como cero.");
    return {value, {}};
}
std::string format_number(double value, Notation notation) {
    validate_notation(notation); value = finite(value);
    if (value == 0) return std::string(1, notation.zero);
    const bool negative = value < 0;
    const auto magnitude = std::abs(static_cast<long double>(value));
    auto integer = std::floor(magnitude); auto fraction = magnitude - integer;
    std::string result;
    do {
        const auto remainder = static_cast<int>(std::fmod(integer, 12.L));
        result += numeral(std::clamp(remainder, 0, 11), notation); integer = std::floor(integer / 12.L);
    } while (integer > 0 && result.size() < 320);
    std::ranges::reverse(result);
    if (fraction > 0) {
        result += '.';
        // Leading fractional zeros do not consume the significant-digit budget:
        // presenting a tiny nonzero value as exact 0 would change SMO semantics.
        unsigned significant = 0;
        for (unsigned i = 0; i < 340 && fraction > 0 && significant < 16; ++i) {
            fraction *= 12;
            const auto part = std::clamp(static_cast<int>(std::floor(fraction)), 0, 11);
            result += numeral(part, notation); fraction -= part;
            if (part != 0 || significant != 0) ++significant;
        }
        if (magnitude < 1 && significant == 0) throw std::range_error("El valor no nulo no se puede mostrar como cero exacto.");
        while (result.back() == notation.zero) result.pop_back();
        if (result.back() == '.') result.pop_back();
    }
    return (negative ? "-" : "") + result;
}
Number multiply(double left, double right) {
    finite(left); finite(right);
    if (left == 0 || right == 0) return {0., left == 0 ? right : left};
    // Experimental block-orientation rule: any negative operand retains a
    // negative orientation, including negative times negative. Not a ring.
    const auto magnitude = finite(std::abs(left) * std::abs(right));
    if (magnitude == 0) throw std::runtime_error("El producto no cabe en FP64 sin perderse como cero.");
    return {(left < 0 || right < 0) ? -magnitude : magnitude, {}};
}
Number divide(double left, double right) {
    finite(left); finite(right);
    if (right == 0) return {left, 0.};
    if (left == 0) return {0, {}};
    const auto magnitude = finite(std::abs(left) / std::abs(right));
    if (magnitude == 0) throw std::runtime_error("El cociente no cabe en FP64 sin perderse como cero.");
    return {(left < 0 || right < 0) ? -magnitude : magnitude, {}};
}
Number direct_power(double base, double exponent) {
    const auto magnitude = exponent_magnitude(base, exponent);
    if (magnitude == 0) return {0., base};
    const auto value = positive_power(base, magnitude);
    return {exponent < 0 ? -value : value, {}};
}
Number inverted_power(double base, double exponent) {
    const auto magnitude = exponent_magnitude(base, exponent);
    if (magnitude == 0) return {0., base};
    // The table fixes exponent two at 1; this is not an invocation of SMO's
    // zero power, whose two values are explicitly handled above.
    const auto value = magnitude == 1 ? base : magnitude == 2 ? 1. : finite(1. / positive_power(base, magnitude - 2));
    if (value == 0) throw std::runtime_error("La potencia invertida no cabe en FP64 sin perderse como cero.");
    return {exponent < 0 ? -value : value, {}};
}

struct Program::Impl {
    Syntax syntax;
    Variables variables;
    Frame geometry;
    explicit Impl(Syntax source) : syntax(std::move(source)) { reset(); }
    void reset() {
        Variables next;
        Frame mesh;
        for (const auto& statement : syntax.initial) {
            if (statement.kind == Statement::Kind::state) next.emplace(statement.name, evaluate(*statement.arguments.front(), next, 0, false));
            else if (statement.kind == Statement::Kind::vertex) {
                if (mesh.vertices.size() == vertex_limit) fail(statement.where, "Demasiados vértices para este laboratorio.");
                mesh.vertices.push_back(vector(statement, next));
            } else if (statement.kind == Statement::Kind::face) {
                if (mesh.faces.size() == face_limit) fail(statement.where, "Demasiadas caras para este laboratorio.");
                const auto values = vector(statement, next); std::array<std::uint32_t, 3> face{};
                const std::array<double, 3> input{values.x, values.y, values.z};
                for (std::size_t i = 0; i < input.size(); ++i) {
                    if (input[i] < 0 || std::trunc(input[i]) != input[i] || input[i] > std::numeric_limits<std::uint32_t>::max())
                        fail(statement.where, "Los índices de cara deben ser enteros no negativos.");
                    face[i] = static_cast<std::uint32_t>(input[i]);
                }
                mesh.faces.push_back(face);
            }
        }
        if (mesh.vertices.size() < 3 || mesh.faces.empty()) throw std::invalid_argument("Define al menos tres vértices y una cara en SMO.");
        for (const auto& face : mesh.faces) for (const auto index : face)
            if (index >= mesh.vertices.size()) throw std::invalid_argument("Una cara SMO hace referencia a un vértice inexistente.");
        variables = std::move(next); geometry = std::move(mesh);
        (void)draw();
    }
    Frame draw() const {
        auto result = geometry;
        for (const auto& statement : syntax.draw) {
            if (statement.kind == Statement::Kind::rotation) result.rotation = vector(statement, variables);
            else if (statement.kind == Statement::Kind::color) {
                result.color = vector(statement, variables);
                if (result.color.x < 0 || result.color.x > 1 || result.color.y < 0 || result.color.y > 1 || result.color.z < 0 || result.color.z > 1)
                    fail(statement.where, "Los componentes de color deben estar entre cero y uno.");
            } else if (statement.kind == Statement::Kind::draw) result.draw_requested = true;
        }
        return result;
    }
    Frame step(double dt) {
        finite(dt); if (dt < 0) throw std::invalid_argument("El tiempo de actualización no puede ser negativo.");
        const auto previous = variables;
        try {
            for (const auto& statement : syntax.update) variables.at(statement.name) = evaluate(*statement.arguments.front(), variables, dt, true);
            return draw();
        } catch (...) { variables = previous; throw; }
    }
};
Program::Program(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Program::Program(Program&&) noexcept = default;
Program& Program::operator=(Program&&) noexcept = default;
Program::~Program() = default;
ProgramIr compile_ir(std::string_view source,Notation notation,bool require_geometry) {
    const auto syntax=Parser(source,notation,true).parse();
    return IrExporter{}.compile(syntax,notation,require_geometry);
}
Program Program::compile(std::string_view source, Notation notation) {
    return Program(std::make_unique<Impl>(Parser(source, notation).parse()));
}
void Program::reset() { if (!impl_) throw std::logic_error("Programa SMO movido."); impl_->reset(); }
Frame Program::frame() { if (!impl_) throw std::logic_error("Programa SMO movido."); return impl_->draw(); }
Frame Program::step(double dt) { if (!impl_) throw std::logic_error("Programa SMO movido."); return impl_->step(dt); }
Number Program::variable(std::string_view name) const {
    if (!impl_) throw std::logic_error("Programa SMO movido.");
    const auto found = impl_->variables.find(name);
    if (found == impl_->variables.end()) throw std::invalid_argument("Estado SMO desconocido: " + std::string(name));
    return found->second;
}
} // namespace lgaed::smo

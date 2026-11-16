#include "core/cpp_expression_repair.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::curriculum;

void require(bool value, std::string_view message) {
    if(!value) throw std::runtime_error(std::string(message));
}

constexpr std::string_view condition = "dato < 0 && dato > 20";
constexpr std::string_view original = R"cpp(#include <iostream>
// La frase «dato < 0 && dato > 20» no es otra expresión del programa.
const char* aviso = "dato < 0 && dato > 20";
const char* ejemplo = R"fin(dato < 0 && dato > 20)fin";
bool aceptar(int dato) {
    return dato < 0 && dato > 20;
}
int main() {
    const bool obtenido = aceptar(7);
    std::cout << "Obtenido=" << obtenido << "\n";
    return obtenido ? 0 : 1;
}
)cpp";

CppExpressionRepair fixture() { return {"src/validacion.cpp", std::string(original), std::string(condition)}; }

std::string changed(const CppExpressionRepair& repair, std::string_view expression) {
    const auto selected = cpp_expression_repair_detail::select(repair);
    require(selected.has_value(), "A test fixture has no authenticated expression");
    const auto begin = selected->source[selected->first].begin;
    const auto end = selected->source[selected->last].end;
    return repair.original_source.substr(0, begin) + std::string(expression) + repair.original_source.substr(end);
}

void contract_and_equivalence() {
    const auto repair = fixture();
    require(cpp_expression_repair_valid(repair), "Comment/literal mentions made the authentic selector ambiguous");
    require(cpp_expression_repair_matches(repair, repair.original_source),
        "Integrity checking incorrectly replaced the independent behavioral verdict");
    for(const auto expression : {
            "dato >= 0 && dato <= 20", "!(dato < 0 || dato > 20)",
            "0 <= dato and dato <= 20", "dato < 0 ? false : dato <= 20",
            "dato >= 0 ? (dato <= 20 ? true : false) : false",
            "! (dato < 0 /* límite inferior */ ||\n dato > 20)",
            "dato >= 0x0 && dato <= 0b10100", "dato >= 0L && dato <= 2'0u",
            "dato >= 0.0 && dato <= 2e1", "dato >= '\\0' && dato <= 20",
            "((dato | 0) >= 0) && ((dato ^ 0) <= 20)", "not (dato < 0 or dato > 20)"}) {
        require(cpp_expression_repair_matches(repair, changed(repair, expression)),
            "A pure arithmetic/logical alternative or internal comment was rejected");
    }
    CppExpressionRepair subtraction{"src/ajuste.cpp",
        "#include \"control/ajuste.h\"\nint ajustar(int valor, int superior) { return valor - superior; }\n",
        "valor - superior"};
    for(const auto expression : {"superior - valor", "-(valor - superior)", "0 - valor + superior"})
        require(cpp_expression_repair_matches(subtraction, changed(subtraction, expression)),
            "An equivalent subtraction repair was rejected");

    auto comments = fixture();
    comments.expression = "dato /* selector */ < 0 && dato > 20";
    require(cpp_expression_repair_valid(comments), "An internal selector comment became a required source spelling");
    comments.original_source = "int f(int dato) { return dato /* source */ < 0 && dato > 20; }\n";
    require(cpp_expression_repair_matches(comments, changed(comments, "dato >= 0 && dato <= 20")),
        "Internal source comments invalidated the selected token interval");

    auto crlf = fixture();
    std::string windows;
    for(const char c : crlf.original_source) { if(c == '\n') windows += '\r'; windows += c; }
    crlf.original_source = std::move(windows);
    require(cpp_expression_repair_matches(crlf, changed(crlf, "!(dato < 0 || dato > 20)")),
        "UTF-8 bytes or CRLF shifted the authenticated offsets");
    auto unicode = fixture();
    unicode.original_source = "// Inicio: áéñ🙂\r\nint f(int señal) { return señal < 0 && señal > 20; }\r\n";
    unicode.expression = "señal < 0 && señal > 20";
    require(cpp_expression_repair_matches(unicode, changed(unicode, "señal >= 0 && señal <= 20")),
        "A UTF-8 identifier was interpreted as a byte offset in another encoding");
}

template<class Predicate>
void compiled_condition(std::string_view expression, Predicate evaluate) {
    const auto repair = fixture();
    require(cpp_expression_repair_matches(repair, changed(repair, expression)),
        "An actually compiled condition was rejected by the integrity contract");
    for(const int dato : {-2147483647 - 1, -21, -1, 0, 1, 7, 19, 20, 21, 2147483647})
        require(evaluate(dato) == (dato >= 0 && dato <= 20),
            "An admitted equivalent condition changed the domain behavior");
}

template<class Expression>
void compiled_adjustment(std::string_view expression, Expression evaluate) {
    const CppExpressionRepair repair{"src/ajuste.cpp",
        "int ajuste(int valor, int superior) { return valor - superior; }\n", "valor - superior"};
    require(cpp_expression_repair_matches(repair, changed(repair, expression)),
        "An actually compiled adjustment was rejected by the integrity contract");
    for(int valor = -20; valor <= 20; ++valor)
        for(int superior = -20; superior <= 20; ++superior)
            require(evaluate(valor, superior) == superior - valor,
                "An admitted equivalent subtraction changed the bounded adjustment");
}

void compiled_equivalents() {
    // Stringification binds the checked replacement to the expression that
    // this test executable actually compiles and evaluates. The behavioral
    // oracle is independent of the integrity/parser result.
#define LGAED_CONDITION(expression) compiled_condition(#expression, [](int dato) { return expression; })
    LGAED_CONDITION(dato >= 0 && dato <= 20);
    LGAED_CONDITION(!(dato < 0 || dato > 20));
    LGAED_CONDITION(dato < 0 ? false : dato <= 20);
    LGAED_CONDITION(not (dato < 0 or dato > 20));
#undef LGAED_CONDITION
#define LGAED_ADJUSTMENT(expression) compiled_adjustment(#expression, [](int valor, int superior) { return expression; })
    LGAED_ADJUSTMENT(superior - valor);
    LGAED_ADJUSTMENT(-(valor - superior));
#undef LGAED_ADJUSTMENT
}

void unique_code_selection() {
    auto repair = fixture();
    repair.original_source += "bool otro(int dato) { return dato < 0 && dato > 20; }\n";
    require(!cpp_expression_repair_valid(repair), "Two real occurrences were selected by textual order");
    for(const auto source : {
            "// dato < 0 && dato > 20\nint main() { return 0; }\n",
            "const char* s = \"dato < 0 && dato > 20\";\n",
            "const char* s = R\"tag(dato < 0 && dato > 20)tag\";\n",
            "#define EXPRESION dato < 0 && dato > 20\nint main() { return 0; }\n",
            "int f(int otrodato) { return otrodato < 0 && otrodato > 20; }\n",
            "int f(int dato) { return dato << 0 && dato >> 20; }\n"}) {
        repair.original_source = source;
        require(!cpp_expression_repair_valid(repair), "A comment, literal, directive or token substring was selected");
    }
    repair = fixture();
    for(const auto selector : {"", " ", "/* comment */", "dato < 0 &&", "dato < (0 && dato > 20",
            "dato < 0 && dato > 20)", "return", "dato;", "dato[0]", "dato,0", "dato = 0"}) {
        repair.expression = selector;
        require(!cpp_expression_repair_valid(repair), "An incomplete/non-expression selector was accepted");
    }
    repair = fixture();
    repair.expression = "dato < 0 && dato > 20";
    repair.original_source = "int f(int dato) { return dato < 0 && dato > 20 /* dato < 0 && dato > 20 */; }";
    require(cpp_expression_repair_valid(repair), "A neighboring comment created a duplicate selector");
}

void immutable_context() {
    const auto repair = fixture();
    const auto repaired = changed(repair, "dato >= 0 && dato <= 20");
    for(const auto& edit : std::vector<std::pair<std::string, std::string>>{
            {"int main()", "int main(int, char**)"}, {"aceptar(7)", "aceptar(8)"},
            {"return obtenido ? 0 : 1;", "return 0;"}, {"Obtenido=", "Informe ficticio="},
            {"#include <iostream>", "#include <cstdio>"}, {"const bool obtenido", "const bool correcto"},
            {"La frase", "La oración"}}) {
        auto candidate = repaired;
        const auto at = candidate.find(edit.first);
        require(at != candidate.npos, "An external-edit fixture no longer matches");
        candidate.replace(at, edit.first.size(), edit.second);
        require(!cpp_expression_repair_matches(repair, candidate),
            "An edit outside the selected expression changed the driver, report, declaration or comment");
    }
    require(!cpp_expression_repair_matches(repair, "int main() { return 0; }\n"),
        "A replacement program was mistaken for a local repair");
    for(const auto expression : {"dato >= 0 && dato <= 20 //", "dato >= 0 && dato <= 20 /*",
            "dato >= 0 && dato <= 20 /* comentario */ //", "R\"fin(", "\"", "'"})
        require(!cpp_expression_repair_matches(repair, changed(repair, expression)),
            "A comment or literal swallowed immutable suffix tokens");

    CppExpressionRepair boundary{"src/a.cpp", "#include \"api.h\"\nint f() { return a+b; }\n", "b"};
    require(!cpp_expression_repair_matches(boundary, changed(boundary, "+b")),
        "A pure standalone expression joined '+' into a different outside '++' token");
    require(cpp_expression_repair_matches(boundary, changed(boundary, " +b")),
        "Safe lexical separation of a unary operator was rejected");
    boundary = {"src/a.cpp", "#include \"api.h\"\nint f() { return a+b; }\n", "a"};
    require(!cpp_expression_repair_matches(boundary, changed(boundary, "a+")),
        "An incomplete replacement was rescued by an immutable suffix token");

    CppExpressionRepair newline{"src/a.cpp", "int f(int dato) { return dato < 0 && dato > 20\n; }\n", std::string(condition)};
    require(cpp_expression_repair_matches(newline, changed(newline, "dato >= 0 && dato <= 20 // comentario")),
        "A line comment safely terminated by the unchanged newline was rejected");
}

void bounded_pure_expression() {
    const auto repair = fixture();
    for(const auto expression : {"", " ", "/* only a comment */", "dato >= 0; dato <= 20", "dato); return true; /*",
            "true } int main() {", "true %> int f() <%", "dato<:0:>", "dato[0]", "dato = 7",
            "dato += 1", "++dato", "dato--", "dato, true", "std::exit(0)", "f(dato)", "(f)(dato)",
            "dato.member", "dato->member", "static_cast<bool>(dato)", "(bool)dato", "sizeof(dato)",
            "new int", "delete dato", "throw dato", "co_await dato", "[] { return true; }()",
            "true ? 1", "true ? : 0", "true : false", "int dato", "#define X true\nX", "%:define X true\nX",
            "dato ## dato", "dato \\u003f 1 : 0", "__LINE__", "__COUNTER__", "0_credito", "'a'_dato",
            "\"dato\"", "R\"fin(dato)fin\"", "'unterminated", "1e+", "0x1.5", "0b102", "'\\n' 'x'"}) {
        require(!cpp_expression_repair_matches(repair, changed(repair, expression)),
            "A call, side effect, lexical escape or malformed expression was admitted");
    }
    for(const auto expression : {"dato > 0 ? dato < 20 : dato == 0", "dato > 0 ? (dato < 20 ? true : dato == 20) : dato == 0",
            "dato >= 0 && dato <= 0x1.4p4", "dato >= U'\\0' && dato <= 20", "dato >= 0 && dato <= 20ULL",
            "(dato >> 0) >= 0 && (dato << 0) <= 20", "(dato % 21) == dato && dato >= 0"})
        require(cpp_expression_repair_matches(repair, changed(repair, expression)),
            "A supported scalar literal, conditional or pure operator was rejected");
    std::string deep(130, '('); deep += "true"; deep.append(130, ')');
    require(!cpp_expression_repair_matches(repair, changed(repair, deep)), "Unbounded recursive nesting was admitted");
}

void source_and_limits() {
    auto repair = fixture();
    for(const auto path : {"src/a.cpp", "src/a.cc", "src/a.cxx", "src/a.c++", "src/Nombre.CPP", "dir-1/a_2.cc"}) {
        repair.path = path;
        require(cpp_expression_repair_valid(repair), "A safe relative C++ path was rejected");
    }
    for(const auto path : {"", "/a.cpp", "../a.cpp", "src/../a.cpp", "./a.cpp", "src/.hidden/a.cpp", "src//a.cpp",
            "src/a.cpp/", "C:/a.cpp", "src\\a.cpp", "src/a c.cpp", "src/á.cpp", "src/a.h", "src/a.txt",
            "src./a.cpp", "src/a.cpp.", "CON.cpp", "src/nul.CPP", "Aux/a.cpp", "src/PrN.other.cpp",
            "com1/a.cpp", "src/COM9.cxx", "LPT1.cpp", "src/lpt9.c++"}) {
        repair.path = path;
        require(!cpp_expression_repair_valid(repair), "An unsafe or non-C++ path was admitted");
    }
    repair = fixture(); repair.path = std::string(253, 'a') + ".cpp";
    require(!cpp_expression_repair_valid(repair), "An overlong path was admitted");
    repair = fixture(); repair.expression += std::string(513 - repair.expression.size(), ' ');
    require(!cpp_expression_repair_valid(repair), "The selector byte limit was ignored");
    repair = fixture(); repair.expression += std::string(512 - repair.expression.size(), ' ');
    require(cpp_expression_repair_valid(repair), "The inclusive selector byte limit was rejected");
    repair = fixture();
    std::string maximum = "true" + std::string(4092, ' ');
    require(cpp_expression_repair_matches(repair, changed(repair, maximum)), "The inclusive replacement byte limit was rejected");
    maximum += ' ';
    require(!cpp_expression_repair_matches(repair, changed(repair, maximum)), "An overlong replacement was admitted");
    auto nul = changed(repair, "true"); nul.insert(nul.begin() + 5, '\0');
    require(!cpp_expression_repair_matches(repair, nul), "A NUL in the candidate was admitted");
    repair.original_source.insert(repair.original_source.begin() + 5, '\0');
    require(!cpp_expression_repair_valid(repair), "A NUL in the original was admitted");

    for(const auto prefix : {"#define dato otro\n", "#if 0\n", "/* before */ #define X true\n",
            "int posicion = __LINE__;\n", "int posicion = __builtin_COLUMN();\n",
            "auto ubicacion = std::source_location::current();\n", "// spliced\\\n", "// spliced\\\r\n"}) {
        repair = fixture(); repair.original_source.insert(0, prefix);
        require(!cpp_expression_repair_valid(repair), "Local preprocessing, position queries or physical splicing was admitted");
    }
    repair = fixture();
    repair.original_source.insert(0, "// __LINE__ source_location __builtin_COLUMN\nconst char* sitio = \"__LINE__\";\n");
    require(cpp_expression_repair_matches(repair, changed(repair, "true")),
        "An inert mention was confused with a query of the source position");
    repair = fixture(); repair.original_source.insert(0, "#include <cstdint> // unchanged dependency\n");
    require(cpp_expression_repair_valid(repair), "A normal include with a trailing comment was rejected");
    repair.original_source.assign(cpp_expression_repair_detail::source_limit + 1, ' ');
    require(!cpp_expression_repair_valid(repair), "The source byte limit was ignored");
}

void framing() {
    const auto repair = fixture();
    const auto frame = cpp_expression_repair_frame(repair);
    require(!frame.empty() && frame.find(cpp_expression_repair_objective_version) != frame.npos,
        "The repair objective has no versioned frame");
    for(int field = 0; field < 3; ++field) {
        auto other = repair;
        if(field == 0) other.path = "other/validacion.cpp";
        if(field == 1) other.original_source.insert(0, "// Another authentic original\n");
        if(field == 2) other.expression = "dato<0&&dato>20";
        require(cpp_expression_repair_frame(other) != frame, "A changed owner source, selector or path reused its frame");
    }
    auto bad = repair; bad.expression = "missing";
    bool rejected = false;
    try { (void)cpp_expression_repair_frame(bad); } catch(const std::invalid_argument&) { rejected = true; }
    require(rejected, "An unauthenticated selector acquired an objective frame");
}
} // namespace

int main() try {
    contract_and_equivalence(); compiled_equivalents(); unique_code_selection(); immutable_context();
    bounded_pure_expression(); source_and_limits(); framing();
    std::cout << "PASS authenticated scalar expression repair, lexical boundaries, equivalents and immutable context\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}

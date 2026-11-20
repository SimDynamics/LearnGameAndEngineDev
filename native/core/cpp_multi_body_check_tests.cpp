#include "core/cpp_multi_body_check.hpp"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view original = R"lgaed(
struct Ciclo {};
class Medida {
public:
    virtual ~Medida() noexcept;
};
class MedidaIdentificada : public Medida {
public:
    ~MedidaIdentificada() noexcept override;
};
Medida::~Medida() noexcept {
    report("base }");
}
MedidaIdentificada::~MedidaIdentificada() noexcept {
    report("derived");
}
std::unique_ptr<Medida> crear_medida(int numero, int codigo, Ciclo& ciclo) {
    (void)numero;
    (void)codigo;
    (void)ciclo;
    return {};
}
int main() { return 0; }
)lgaed";

using lgaed::curriculum::CppEditableBody;
using lgaed::curriculum::CppMultiBodyPolicy;

CppMultiBodyPolicy policy() {
    return {
        std::string(original),
        {
            {"Medida::~Medida() noexcept", true},
            {"MedidaIdentificada::~MedidaIdentificada() noexcept", true},
            {"std::unique_ptr<Medida> crear_medida(int numero, int codigo, Ciclo& ciclo)",
             false},
        },
    };
}

void expect(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

std::string candidate(const CppMultiBodyPolicy& p,
                      const std::vector<std::string>& bodies) {
    const auto spans =
        lgaed::curriculum::cpp_multi_body_detail::spans(
            p.original_source, p.bodies, false);
    if (!spans || spans->size() != bodies.size())
        throw std::runtime_error("Invalid original three-body fixture");
    std::string value = p.original_source;
    for (auto i = bodies.size(); i != 0;) {
        --i;
        const auto span = spans->at(i);
        value.replace(span.begin, span.end - span.begin, bodies[i]);
    }
    return value;
}

} // namespace

int main() try {
    auto p = policy();
    const std::vector<std::string> valid{
        "\n    if (true) { report(\"base\"); }\n",
        "\n    const char* text = \"braces { and } are characters\";\n"
        "    report(text);\n",
        "\n    if (numero == codigo) { return {}; }\n"
        "    (void)ciclo;\n"
        "    return {};\n",
    };
    expect(lgaed::curriculum::cpp_multi_body_matches(p, original),
           "The unedited original was not accepted");
    const auto a = candidate(p, valid);
    expect(lgaed::curriculum::cpp_multi_body_matches(p, a),
           "Three legitimate independent body changes were rejected");

    auto one = candidate(p, {"\n    report(\"other\");\n",
                             "\n    report(\"derived\");\n",
                             "\n    (void)numero; (void)codigo; (void)ciclo;\n    return {};\n"});
    expect(lgaed::curriculum::cpp_multi_body_matches(p, one),
           "A legitimate single-body change was rejected");

    auto edited = a;
    const auto main_at = edited.find("int main()");
    expect(main_at != std::string::npos, "Fixture lost main");
    edited.replace(main_at, std::string("int main()").size(), "int main(int argc)");
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, edited),
           "Changed published main was permitted");

    edited = a;
    const auto old_name = edited.find("class MedidaIdentificada :");
    expect(old_name != std::string::npos, "Fixture lost hierarchy");
    edited.replace(old_name, 5, "struct");
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, edited),
           "Changed published class was permitted");

    auto injection = valid;
    injection[0] = "\n}\nvoid inserted() {}\nvoid replacement() {\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, candidate(p, injection)),
           "An escaped function body was accepted");

    injection = valid;
    injection[2] = "\n#define OWNER 1\n    return {};\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, candidate(p, injection)),
           "A preprocessor directive inside a body was accepted");

    injection = valid;
    injection[1] = "\n    int value = 1 \\\n+ 2;\n    (void)value;\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, candidate(p, injection)),
           "A physical line splice inside a body was accepted");

    injection = valid;
    injection[1] = "\n    // MedidaIdentificada::~MedidaIdentificada() noexcept\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, candidate(p, injection)),
           "A duplicate signature hidden inside a comment was accepted");

    injection = valid;
    injection[1] = "\n    const char* literal = \"unterminated;\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, candidate(p, injection)),
           "An unterminated string was accepted");

    // An editable body cannot print a private witness and exit before
    // the independent driver has checked the remaining cases.
    for (const auto early : {"std::exit(0)", "std::_Exit(0)",
                             "::quick_exit(0)", "ExitProcess(0)",
                             "::abort()", "::TerminateProcess(0, 0)"}) {
        injection = valid;
        injection[0] = "\n    std::fputs(\"LGAED_DUAL_INTERFACE_COMPLETE_V1\", stderr);\n    " +
                       std::string(early) + ";\n";
        expect(!lgaed::curriculum::cpp_multi_body_matches(p,candidate(p,injection)),
               "Premature process termination forged a private completion witness");
    }
    injection = valid;
    injection[0] = "\n    auto finish = &std::exit;\n    finish(0);\n";
    expect(!lgaed::curriculum::cpp_multi_body_matches(p,candidate(p,injection)),
           "A function pointer escaped the process termination source gate");
    injection = valid;
    injection[0] = R"lgaed(
    // std::exit(0) is documentation, not a call.
    const auto note = R"demo(quick_exit(0))demo";
    (void)note;
    report("base");
)lgaed";
    expect(lgaed::curriculum::cpp_multi_body_matches(p,candidate(p,injection)),
           "Comments and raw strings were mistaken for process termination");

    edited = a;
    edited.insert(0, "#define main something_else\n");
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, edited),
           "An extra directive outside body was accepted");

    edited = a;
    edited.push_back('\0');
    expect(!lgaed::curriculum::cpp_multi_body_matches(p, edited),
           "NUL bytes in candidate source were accepted");

    auto bad = p;
    bad.bodies.push_back(p.bodies.front());
    expect(!lgaed::curriculum::cpp_multi_body_matches(bad, original),
           "Duplicated editable signature was accepted");

    bad = p;
    std::swap(bad.bodies[0], bad.bodies[1]);
    expect(!lgaed::curriculum::cpp_multi_body_matches(bad, original),
           "Out-of-order function declarations were accepted");

    bad = p;
    bad.bodies[0].signature = "Medida::~Medida() noexcept // forged";
    expect(!lgaed::curriculum::cpp_multi_body_matches(bad, original),
           "Nonexistent authored signature was accepted");

    std::cout << "PASS original-source-bound generic three-body checks\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "FAIL multi-body source preservation: " << error.what() << '\n';
    return 1;
}

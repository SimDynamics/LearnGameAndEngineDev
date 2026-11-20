#include "core/cpp_member_body_check.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace lgaed::curriculum;

void require(bool v, std::string_view message) {
    if(!v) throw std::runtime_error(std::string(message));
}

int main() {
    try {
        CppMemberBodyCheck check{
R"cpp(#include <iostream>
#include <memory>
class Deposito {
public:
    bool transferir(int origen, int destino);
private:
    std::unique_ptr<int> primero_;
    std::unique_ptr<int> segundo_;
};
bool Deposito::transferir(int origen, int destino) {
    // Only this body may change.
    (void)origen;
    (void)destino;
    return false;
}
int main() { return 0; }
)cpp", "bool Deposito::transferir(int origen, int destino)"};
        const auto span=cpp_member_body_span(check);
        require(span.has_value(), "Missing exact member body");
        require(cpp_member_body_preserves_source(check,check.original_source),
                "Original source lost its own boundary");
        const auto update=[&](std::string_view body) {
            return check.original_source.substr(0,span->begin)+std::string(body)+
                check.original_source.substr(span->end);
        };
        const auto good=update(R"cpp(
    if (origen == destino) return false;
    if (origen < 0 || destino < 0) { return false; }
    auto& desde = origen == 0 ? primero_ : segundo_;
    auto& hacia = destino == 0 ? primero_ : segundo_;
    if (!desde) return false;
    hacia = std::move(desde);
    return true;
)cpp");
        require(cpp_member_body_preserves_source(check,good),
                "Equivalent structured member body was rejected");
        require(cpp_member_body_preserves_source(check,update(
            R"cpp(
 // A quote with braces: "}"; and a raw string R"tag( } { )tag".
 const auto text = R"tag( } { )tag";
 (void)text;
 return false;
)cpp")), "Raw string or comment braces were misread");
        auto changed=good;
        changed.replace(changed.find("std::unique_ptr<int> primero_"),
                        std::string("std::unique_ptr<int> primero_").size(),"int primero_");
        require(!cpp_member_body_preserves_source(check,changed),
                "Changing a protected class member was accepted");
        changed=good;
        changed.replace(changed.find("int main() { return 0; }"),
                        std::string("int main() { return 0; }").size(),
                        "int main() { return 1; }");
        require(!cpp_member_body_preserves_source(check,changed),
                "Changing the provided main was accepted");
        require(!cpp_member_body_preserves_source(check,update(
            "\n#define private public\nreturn false;\n")),
                "A preprocessor directive in the editable body was accepted");
        require(!cpp_member_body_preserves_source(check,update(
            "\n/* prepared\ncomment */#define value 3\nreturn false;\n")),
                "A directive hidden after a multiline comment was accepted");
        require(!cpp_member_body_preserves_source(check,update(
            "\nreturn false;\\\nreturn true;\n")),
                "A C++ physical line splice in the body was accepted");
        require(!cpp_member_body_preserves_source(check,update(
            "\nreturn false; }\n// escaped owner\n")),
                "Closing the method early was accepted");
        require(!cpp_member_body_preserves_source(check,update(
            "\nconst char* a = \"unterminated;\nreturn false;\n")),
                "Unterminated code string was accepted");
        for (const auto early : {"std::exit(0)", "std::_Exit(0)",
                                "::quick_exit(0)", "ExitProcess(0)",
                                "::abort()"}) {
            require(!cpp_member_body_preserves_source(check,update(
                "\n    std::fputs(\"LGAED_OWNER_COMPLETE_V1\", stderr);\n    " +
                std::string(early) + ";\n")),
                "An edited member could exit with a forged private witness");
        }
        require(!cpp_member_body_preserves_source(check,update(
            "\n    auto finish = &std::exit;\n    finish(0);\n")),
            "Aliased std::exit bypassed the member source gate");
        require(cpp_member_body_preserves_source(check,update(R"cpp(
    // Discuss std::exit(0) but do not call it.
    const auto text = R"memo(std::exit(0))memo";
    (void)text;
    return false;
)cpp")), "Text-only mention of exit was rejected");

        auto wrong=check;
        wrong.signature="bool Deposito::vaciar(int origen)";
        require(!cpp_member_body_span(wrong),"A nonexistent signature became a valid owner");
        wrong=check;
        wrong.original_source+="\nbool Deposito::transferir(int origen, int destino) { return false; }\n";
        require(!cpp_member_body_span(wrong),"A duplicate definition claimed another owner");
        CppMemberBodyCheck other{R"cpp(struct Celda { Celda& operator=(const Celda&); };
Celda& Celda::operator=(const Celda& otra) { if (this == &otra) return *this; return *this; }
)cpp", "Celda& Celda::operator=(const Celda& otra)"};
        require(cpp_member_body_span(other).has_value(),
                "Operator assignment body cannot be guarded");
        std::cout<<"PASS portable C++ member body guard: protected surrounding source, nested code, raw text, overloaded operator and hostile edits. No behavior is credited.\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"FAIL C++ member body guard: "<<e.what()<<'\n';
        return 1;
    }
}

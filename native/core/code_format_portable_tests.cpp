#include "core/code_format_compat.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::code_format::Language;
using lgaed::curriculum::code_format::equivalent;

struct Case {
    std::string_view name;
    std::string_view original;
    std::string_view revised;
    Language language;
    bool should_accept;
};

void verify(const Case& example) {
    if (equivalent(example.original, example.revised, example.language) !=
        example.should_accept)
        throw std::runtime_error("Format equivalence mismatch: " +
                                 std::string(example.name));
}
}

int main() {
    try {
        constexpr Case cases[] = {
            {"contextual import at start of expression line",
             "int main(){int import=3;return (\nimport<4>1\n);}",
             "int main() {\n    int import = 3;\n    return (\n"
             "        import < 4 > 1\n    );\n}",Language::cpp,true},
            {"contextual import within a statement",
             "int main(){int import=3;return import<4>1;}",
             "int main() { int import = 3; return import < 4 > 1; }",
             Language::cpp,true},
            {"ordinary exported module header format",
             "export import <project/local.hpp>;\nint main(){return 0;}",
             "export import <project/local.hpp>;\n\nint main() { return 0; }",
             Language::cpp,true},
            {"import header whitespace is significant",
             "import <vector>;", "import < vector >;", Language::cpp,false},
            {"exported header whitespace is significant",
             "export import <project/local.hpp>;",
             "export import <project/ local.hpp>;",Language::cpp,false},
            {"header name after block comment",
             "import <vector> /* note */ ;",
             "import < vector > /* note */ ;",Language::cpp,false},
            {"header name after exported line comment",
             "export import <project/local.hpp> // note\n;",
             "export import <project/ local.hpp> // note\n;",Language::cpp,false},
            {"incomplete import stays protected",
             "import <vector>","import < vector >",Language::cpp,false},
            {"newline after import is not an innocuous separator",
             "import <vector>;","import\n<vector>;",Language::cpp,false},
            {"preprocessor macro parameter adjacency",
             "#define F(x) x\nint y=F(1);",
             "#define F (x) x\nint y=F(1);",Language::cpp,false},
            {"comment before directive still protected",
             "/* note */ #define F(x) x\nint y=F(1);",
             "/* note */ #define F (x) x\nint y=F(1);",Language::cpp,false},
            {"literal standard header with source_location",
             "#include <source_location>\nint main(){return 0;}",
             "#include <source_location>\n\nint main() { return 0; }",
             Language::cpp,true},
            {"literal include named after a line macro",
             "#include \"__LINE__.h\" // source_location\nint main(){return 0;}",
             "#include \"__LINE__.h\" // source_location\n\nint main() { return 0; }",
             Language::cpp,true},
            {"active location query after an inert header",
             "#include <source_location>\nint main(){return std::source_location::current().line();}",
             "#include <source_location>\n\nint main(){return std::source_location::current().line();}",
             Language::cpp,false},
            {"line sensitive builtin remains protected",
             "int x=__LINE__;", "\nint x=__LINE__;",Language::cpp,false},
            {"inert raw string does not block formatting",
             "const char* text=R\"(import <vector> __LINE__)\"; int main(){}",
             "const char* text = R\"(import <vector> __LINE__)\";\nint main() {}",
             Language::cpp,true},
            {"CMake multiline call formatting",
             "add_library(envios STATIC lib.cpp)\nmessage(ok)",
             "add_library(\n envios STATIC lib.cpp\n)\n\nmessage(ok)",
             Language::cmake,true},
            {"CMake adjacent commands require newline",
             "message(one)\nmessage(two)",
             "message(one) message(two)",Language::cmake,false},
            {"CMake bracket comment cannot erase command newline",
             "message(one)\n#[[note]]\nmessage(two)",
             "message(one) #[[note]] message(two)",Language::cmake,false},
            {"CMake newline inside bracket comment is not a separator",
             "message(one)\n#[[first\nsecond]]\nmessage(two)",
             "message(one)\n#[[first\nsecond]] message(two)",Language::cmake,false},
            {"CMake bracket comment may move to its own line",
             "message(one) #[[note]]\nmessage(two)",
             "message(one)\n#[[note]]\nmessage(two)",Language::cmake,true},
            {"CMake line comment may move after its command",
             "message(one) # note\nmessage(two)",
             "message(one)\n# note\nmessage(two)",Language::cmake,true},
            {"CMake line-dependent source not cosmetic",
             "message(CMAKE_CURRENT_LIST_LINE)",
             "\nmessage(CMAKE_CURRENT_LIST_LINE)",Language::cmake,false},
        };
        for (const auto& test : cases) verify(test);
        std::cout << "PASS " << (sizeof(cases) / sizeof(cases[0]))
                  << " portable C++23/CMake source-format comparisons\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL portable source format: " << error.what() << '\n';
        return 1;
    }
}

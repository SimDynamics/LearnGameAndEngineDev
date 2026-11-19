#include "core/code_format_compat.hpp"
#include "core/rejection_diagnostic_facts.hpp"
#include "core/report_observation_check.hpp"
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"

#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace lgaed;
using namespace curriculum;
using code_format::Language;
void check(bool ok,std::string_view message){
    if(!ok)throw std::runtime_error(std::string(message));
}
void lexical_safety(){
    check(code_format::equivalent(
        "import <vector>;\nint main(){return 0;}",
        "import <vector>;\n\nint main() { return 0; }",Language::cpp),
        "Safe formatting outside a C++23 header-unit name was rejected");
    check(code_format::equivalent(
        "export import <local/header.hpp>;\nint main(){return 0;}",
        "export import <local/header.hpp>;\n\nint main() {return 0;}",Language::cpp),
        "Safe formatting of an exported header-unit import was rejected");
    check(!code_format::equivalent(
        "import <vector>;", "import < vector >;",Language::cpp),
        "Spaces inside a header-unit name were treated as cosmetic C++ formatting");
    check(!code_format::equivalent(
        "export import <local/header.hpp>;",
        "export import <local/ header.hpp>;",Language::cpp),
        "An exported header-unit import silently changed its header name");
    check(!code_format::equivalent(
        "import/* note */<vector>;", "import/* note */<vector >;",Language::cpp),
        "A header-unit name following a comment was reformatted unsafely");
    check(!code_format::equivalent(
        "import <vector>;", "import\n<vector>;",Language::cpp),
        "Breaking a header import between import and < was accepted");
    check(!code_format::equivalent(
        "import <vector>;", "import <vector\n>;",Language::cpp),
        "Breaking a header name across physical lines was accepted");
    check(code_format::equivalent(
        "int main(){int number=2;return number;}",
        "int main() {\n    int number = 2;\n\n    return number;\n}\n",Language::cpp),
        "Safe semantic C++ whitespace was rejected");
    check(code_format::equivalent(
        "#include <iostream>\nint main(){return 0;}\n",
        "#include <iostream>\n\nint main() {\n    return 0;\n}\n",Language::cpp),
        "Safe blank rows beside a preprocessor directive were rejected");
    check(code_format::equivalent(
        "%:include <iostream>\nint main(){return 0;}\n",
        "%:include <iostream>\n\nint main() {\n    return 0;\n}\n",Language::cpp),
        "C++ alternative directive spelling rejected safe formatting");
    check(!code_format::equivalent(
        "%:define X 3\nint n=X;","%:\ndefine X 3\nint n=X;",Language::cpp),
        "A digraph directive was broken by an unauthenticated newline");
    check(!code_format::equivalent(
        "/* note */ %:define X 3\nint n=X;",
        "/* note */ %:\ndefine X 3\nint n=X;",Language::cpp),
        "A digraph directive after a block comment escaped safety checks");
    check(!code_format::equivalent(
        "/* note */ #define X 3\nint n=X;",
        "/* note */ #\ndefine X 3\nint n=X;",Language::cpp),
        "A # directive after a block comment escaped safety checks");
    check(code_format::equivalent(
        "const char* note=\"%:define X\";\nint main(){}",
        "const char* note=\"%:define X\";\n\nint main() {}",Language::cpp),
        "Inert %: in a quoted literal blocked safe code formatting");
    check(code_format::equivalent(
        "add_library(envios STATIC lib.cpp)\ntarget_include_directories(envios PUBLIC include)\n",
        "add_library(\n  envios\n  STATIC lib.cpp\n)\n\ntarget_include_directories(\n  envios PUBLIC include\n)\n",Language::cmake),
        "Safe CMake command formatting was rejected");
    check(code_format::equivalent(
        "show(-Da=\"b c\")\nmessage(ok)", "show(-Da=\"b c\")\n\nmessage(ok)",
        Language::cmake), "Unquoted legacy CMake argument blocked safe layout");
    check(code_format::equivalent(
        "show(-Dfoo=$(BAR))\nmessage(ok)", "show(-Dfoo=$(BAR))\n\nmessage(ok)",
        Language::cmake), "Literal make-style CMake reference blocked safe layout");
    check(!code_format::equivalent(
        "show(-Da=\"b c\")", "show(-Da= \"b c\")",Language::cmake),
        "Spacing split one legacy quoted CMake argument into two");
    check(!code_format::equivalent(
        "show(a\" \"b)", "show(a \" \" b)",Language::cmake),
        "Spacing split a mixed unquoted CMake argument into three");
    check(!code_format::equivalent(
        "show(-Dfoo=$(BAR))", "show(-Dfoo=$ (BAR))",Language::cmake),
        "Spacing split a make-style CMake variable reference");
    check(!code_format::equivalent(
        "show(\"x\"\"y\")", "show(\"x\" \"y\")",Language::cmake),
        "Spacing hid a CMake adjacent-quote syntax diagnostic");
    check(code_format::equivalent(
        "message(STATUS \"a\")\nmessage(STATUS \"b\")",
        "message (STATUS \"a\")\n\nmessage\t(STATUS \"b\")",Language::cmake),
        "Safe CMake header spaces and blank command lines were rejected");
    check(code_format::equivalent(
        "message(STATUS \"a\")","message(\n  STATUS \"a\"\n)",Language::cmake),
        "A safe line break inside a command was rejected");
    check(code_format::equivalent(
        "if(TRUE AND (TRUE OR FALSE))\nmessage(STATUS \"yes\")\nendif()",
        "if(\n  TRUE AND (\n    TRUE OR FALSE\n  )\n)\n\nmessage(STATUS \"yes\")\nendif()",
        Language::cmake), "Nested CMake parentheses rejected safe layout");
    check(!code_format::equivalent(
        "message(STATUS \"ok\")","message\n(STATUS \"ok\")",Language::cmake),
        "A physical newline invalidated a CMake command header");
    check(!code_format::equivalent(
        "message(STATUS \"a\")\nmessage(STATUS \"b\")",
        "message(STATUS \"a\") message(STATUS \"b\")",Language::cmake),
        "Removing a command-separating newline was silently allowed");
    check(!code_format::equivalent(
        "message(STATUS \"a\") message(STATUS \"b\")",
        "message(STATUS \"a\")\nmessage(STATUS \"b\")",Language::cmake),
        "An invalid same-line command split was treated as cosmetic");
    check(!code_format::equivalent("int n=1;","int n=2;",Language::cpp),
        "An altered integer literal was accepted");
    check(!code_format::equivalent("int n=x+ +y;","int n=x++y;",Language::cpp),
        "A changed operator boundary was accepted");
    check(!code_format::equivalent("int n=1;// reason\nreturn n;",
            "int n=1;// reason return n;",Language::cpp),
        "A line comment swallowed a statement");
    check(!code_format::equivalent("/* note */ int n=1;","/* other */ int n=1;",Language::cpp),
        "Comment content changed");
    check(!code_format::equivalent("const char* s=\"one\\ntwo\";",
            "const char* s=\"one\ntwo\";",Language::cpp),
        "An escaped C++ newline became physical literal content");
    check(!code_format::equivalent("auto s=R\"x(one\ntwo)x\";",
            "auto s=R\"x(one\n\ntwo)x\";",Language::cpp),
        "The raw literal body changed");
    check(!code_format::equivalent("#define X(a) \\\n ((a)+1)\nint n=X(2);",
            "#define X(a) \\\n ((a)+1)\n\nint n=X(2);",Language::cpp),
        "Backslash continuation was implicitly normalized");
    check(!code_format::equivalent("message(\"a\\nb\")", "message(\"a\nb\")",Language::cmake),
        "CMake quoted string content changed");
    check(!code_format::equivalent("set(A 1) # note\nset(B 2)",
            "set(A 1) # note set(B 2)",Language::cmake),
        "CMake comment swallowed the following command");
    check(!code_format::equivalent("set(A [=[value\nwith line]=])",
            "set(A [=[value\n\nwith line]=])",Language::cmake),
        "CMake bracket value changed");
    check(!code_format::equivalent("int main(){return __LINE__;}",
            "int main(){\nreturn __LINE__;}",Language::cpp),
        "Source __LINE__ changed value after a safe-looking newline");
    check(!code_format::equivalent(
            "auto line=std::source_location::current().line();",
            "auto line=\nstd::source_location::current().line();",Language::cpp),
        "std::source_location was affected by an introduced physical line");
    check(!code_format::equivalent(
            "auto location=__builtin_source_location();",
            "auto location=\n__builtin_source_location();",Language::cpp),
        "Compiler source-location builtin was treated as independent of physical lines");
    check(!code_format::equivalent(
            "#define LOCATION __builtin_source_location()\nint main(){}",
            "#define LOCATION __builtin_source_location()\n\nint main() {}",Language::cpp),
        "A macro containing the source-location builtin accepted new line layout");
    check(code_format::equivalent(
            "const char* note=\"__builtin_source_location\";\nint main(){}",
            "const char* note=\"__builtin_source_location\";\n\nint main(){}",Language::cpp),
        "A literal mention of the builtin blocked harmless spacing");
    check(code_format::equivalent(
            "// __builtin_source_location is documentation\nint main(){}",
            "// __builtin_source_location is documentation\n\nint main(){}",Language::cpp),
        "A comment mention of the builtin blocked harmless spacing");
    check(!code_format::equivalent(
            "int main(){return __builtin_LINE();}",
            "int main(){\nreturn __builtin_LINE();}",Language::cpp),
        "Source __builtin_LINE() changed value after a token-equivalent newline");
    check(!code_format::equivalent(
            "int main(){return __builtin_COLUMN();}",
            "int main(){ return __builtin_COLUMN();}",Language::cpp),
        "Source __builtin_COLUMN() changed value after token-equivalent spacing");
    check(code_format::equivalent(
        R"code(// __LINE__ is documented, not evaluated.
const char* label = "__builtin_COLUMN source_location";
const char* raw = R"tag(__builtin_LINE __LINE__)tag";
int main(){return label[0]=='_' && raw[0]=='_';}
)code",
        R"code(// __LINE__ is documented, not evaluated.
const char* label = "__builtin_COLUMN source_location";
const char* raw = R"tag(__builtin_LINE __LINE__)tag";

int main() {
    return label[0]=='_' && raw[0]=='_';
}
)code",Language::cpp),
        "Harmless strings, raw literals and comments falsely blocked code spacing");
    check(code_format::equivalent(
            "int count__LINE__marker=1;int main(){return count__LINE__marker;}",
            "int count__LINE__marker=1;\nint main(){return count__LINE__marker;}",Language::cpp),
        "A longer identifier containing a macro spelling was treated as a positional macro");
    check(!code_format::equivalent(
            "#define SOURCE_LINE __LINE__\nint line=SOURCE_LINE;",
            "#define SOURCE_LINE __LINE__\n\nint line=SOURCE_LINE;",Language::cpp),
        "A position-sensitive macro directive gained an unverified formatting exception");
    check(!code_format::equivalent(R"qa(auto s="abc"_length;)qa",
            R"qa(auto s="abc" _length;)qa",Language::cpp),
        "Adjacent user-defined literal suffix was detached");
    check(!code_format::equivalent(R"qa(auto s=R"tag(abc)tag"_length;)qa",
            R"qa(auto s=R"tag(abc)tag" _length;)qa",Language::cpp),
        "Adjacent raw-literal suffix was detached");
    check(!code_format::equivalent(
            R"qa(message("${CMAKE_CURRENT_LIST_LINE}"))qa",
            "\nmessage(\"${CMAKE_CURRENT_LIST_LINE}\")",Language::cmake),
        "CMake line introspection silently changed its value");
    check(!code_format::equivalent(
            R"qa(function(where)
  message("${CMAKE_CURRENT_FUNCTION_LIST_LINE}")
endfunction()
where()
)qa",
            R"qa(
function(where)
  message("${CMAKE_CURRENT_FUNCTION_LIST_LINE}")
endfunction()
where()
)qa",Language::cmake),
        "CMake function-definition line introspection silently changed its value");
    check(!code_format::equivalent(
            "if(CMAKE_CURRENT_LIST_LINE)\nmessage(\"ok\")\nendif()",
            "\nif(CMAKE_CURRENT_LIST_LINE)\nmessage(\"ok\")\nendif()",Language::cmake),
        "A bare CMake line-position variable escaped format protection");
    check(!code_format::equivalent(
            R"qa(cmake_minimum_required(VERSION 3.20)
string(CONFIGURE "@CMAKE_CURRENT_LIST_LINE@" value @ONLY)
message("${value}")
)qa",
            R"qa(cmake_minimum_required(VERSION 3.20)

string(CONFIGURE "@CMAKE_CURRENT_LIST_LINE@" value @ONLY)
message("${value}")
)qa",Language::cmake),
        "CMake string(CONFIGURE) @VAR@ substitution changed line without a token change");
    check(!code_format::equivalent(
            R"qa(cmake_minimum_required(VERSION 3.20)
string(CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] value @ONLY)
message("${value}")
)qa",
            R"qa(cmake_minimum_required(VERSION 3.20)

string(CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] value @ONLY)
message("${value}")
)qa",Language::cmake),
        "A bracket argument later expanded by string(CONFIGURE) hid a line change");
    check(!code_format::equivalent(
            R"qa(cmake_minimum_required(VERSION 3.20)
file(CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)
)qa",
            R"qa(cmake_minimum_required(VERSION 3.20)

file(CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY)
)qa",Language::cmake),
        "CMake file(CONFIGURE) expanded an inert-looking bracket template on a new line");
    check(!code_format::equivalent(
            R"qa(cmake_language(CALL file CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            R"qa(
cmake_language(CALL file CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            Language::cmake),
        "CMake-language CALL file(CONFIGURE) bypassed bracket protection");
    check(!code_format::equivalent(
            R"qa(cmake_language(CALL "file" "CONFIGURE" OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            R"qa(
cmake_language(CALL "file" "CONFIGURE" OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            Language::cmake),
        "CMake-language quoted file and CONFIGURE names escaped bracket protection");
    check(!code_format::equivalent(
            R"qa(cmake_language(CALL "string" "CONFIGURE" [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] output @ONLY))qa",
            R"qa(
cmake_language(CALL "string" "CONFIGURE" [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] output @ONLY))qa",
            Language::cmake),
        "CMake-language quoted string and CONFIGURE names escaped bracket protection");
    check(!code_format::equivalent(
            R"qa(cmake_language(CALL [=[file]=] [=[CONFIGURE]=] OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            R"qa(
cmake_language(CALL [=[file]=] [=[CONFIGURE]=] OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            Language::cmake),
        "CMake-language bracket literal command names escaped bracket protection");
    check(!code_format::equivalent(
            R"qa(set(command_name file)
cmake_language(CALL "${command_name}" CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            R"qa(set(command_name file)

cmake_language(CALL "${command_name}" CONFIGURE OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_LIST_LINE@]=] @ONLY))qa",
            Language::cmake),
        "CMake-language dynamically expanded command name escaped bracket protection");
    check(!code_format::equivalent(
            R"qa(set(subcommand CONFIGURE)
cmake_language(CALL file "${subcommand}" OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] @ONLY))qa",
            R"qa(set(subcommand CONFIGURE)

cmake_language(CALL file "${subcommand}" OUTPUT "result.txt" CONTENT [=[@CMAKE_CURRENT_FUNCTION_LIST_LINE@]=] @ONLY))qa",
            Language::cmake),
        "CMake-language dynamically expanded CONFIGURE subcommand escaped protection");
    check(!code_format::equivalent(
            R"qa(set(template [=[@CMAKE_CURRENT_LIST_LINE@]=])
STRING(CONFIGURE "${template}" result @ONLY)
)qa",
            R"qa(set(template [=[@CMAKE_CURRENT_LIST_LINE@]=])

STRING(CONFIGURE "${template}" result @ONLY)
)qa",Language::cmake),
        "An indirect template or case variant bypassed CMake configure protection");
    check(!code_format::equivalent(
            R"qa(cmake_language(CALL string CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] result @ONLY))qa",
            R"qa(
cmake_language(CALL string CONFIGURE [=[@CMAKE_CURRENT_LIST_LINE@]=] result @ONLY))qa",
            Language::cmake),
        "CMake-language CALL of string(CONFIGURE) bypassed bracket protection");
    check(code_format::equivalent(
            R"qa(#[=[ @CMAKE_CURRENT_LIST_LINE@ is comment-only ]=]
string(CONFIGURE "literal" result @ONLY)
)qa",
            R"qa(#[=[ @CMAKE_CURRENT_LIST_LINE@ is comment-only ]=]

string(CONFIGURE "literal" result @ONLY)
)qa",Language::cmake),
        "A bracket comment was incorrectly treated as a configurable argument");
    check(code_format::equivalent(
            R"qa(message([=[@CMAKE_CURRENT_LIST_LINE@]=]))qa",
            R"qa(
message([=[@CMAKE_CURRENT_LIST_LINE@]=]))qa",Language::cmake),
        "An inert CMake bracket literal was misclassified as a substitution");
    check(code_format::equivalent(
            R"qa(#[=[ ${CMAKE_CURRENT_LIST_LINE} in a comment ]=]
message([=[${CMAKE_CURRENT_LIST_LINE}]=])
)qa",
            R"qa(#[=[ ${CMAKE_CURRENT_LIST_LINE} in a comment ]=]

message([=[${CMAKE_CURRENT_LIST_LINE}]=])
)qa",Language::cmake),
        "CMake bracket arguments and bracket comments were mistaken for expansions");
    check(code_format::equivalent(
            "# CMAKE_CURRENT_FUNCTION_LIST_LINE is just documentation\n"
            "message(\"CMAKE_CURRENT_LIST_LINE\")",
            "# CMAKE_CURRENT_FUNCTION_LIST_LINE is just documentation\n\n"
            "message(\"CMAKE_CURRENT_LIST_LINE\")",Language::cmake),
        "CMake literal names in comments or quotes blocked safe source spacing");
    check(code_format::equivalent(
            "set(my_CMAKE_CURRENT_LIST_LINE 7)\nmessage(ok)",
            "set(my_CMAKE_CURRENT_LIST_LINE 7)\n\nmessage(ok)",Language::cmake),
        "A longer inert CMake identifier was treated as a position variable");
    check(!code_format::equivalent("a", "a\n",Language::immutable),
        "Unknown data source was modified");
}
void cpp_ucn_format_boundaries() {
    // Removing one space changes a local declaration into assignment to the
    // global a-alpha: the first program returns 7, the second returns 2.
    constexpr std::string_view before=R"cpp(using a = int;
int a\u03B1 = 7;
int main() {
    a \u03B1 = 2;
    return a\u03B1;
}
)cpp";
    constexpr std::string_view after=R"cpp(using a = int;
int a\u03B1 = 7;
int main() {
    a\u03B1 = 2;
    return a\u03B1;
}
)cpp";
    check(!code_format::equivalent(before,after,Language::cpp)&&
          !code_format::equivalent(after,before,Language::cpp),
        "A changed UCN identifier boundary was accepted as source formatting");
    check(code_format::equivalent(before,before,Language::cpp)&&
          code_format::equivalent(after,after,Language::cpp),
        "An identical UCN source lost its existing contract");
    check(!code_format::equivalent(before,"\n"+std::string(before),Language::cpp),
        "Changed live UCN source exceeded the formatter's lexical guarantee");

    constexpr std::string_view opaque[]{
        R"cpp(// \u03B1 is explanatory text.
int main(){return 0;})cpp",
        R"cpp(/* \u03B1 is explanatory text. */ int main(){return 0;})cpp",
        R"cpp(const char* text = "\\u03B1"; int main(){return 0;})cpp",
        R"cpp(const char* raw = R"note(\u03B1)note"; int main(){return 0;})cpp",
        R"cpp(#define a\u03B1 7
int main(){return 0;})cpp"
    };
    for(const auto source:opaque)
        check(code_format::equivalent(source,"\n"+std::string(source),Language::cpp),
            "Encapsulated UCN text blocked otherwise safe formatting");
    check(code_format::confined_function_body_edit(
        R"cpp(int a\u03B1 = 2; { int \u03B2 = a\u03B1; } return a\u03B1;)cpp"),
        "A formatting-only restriction leaked into the shared function-body editor");
}
void guarded_open_compiler_edits() {
    const auto replace_exact=[](std::string before,std::string_view target,
                                std::string_view replacement) {
        const auto at=before.find(target);
        check(at!=before.npos&&before.find(target,at+target.size())==before.npos,
            "Open compiler edit QA lost its unique source boundary");
        before.replace(at,target.size(),replacement);
        return before;
    };
    // A complete program is the immutable surrounding shell. Only the
    // assigned diagnostic function's body may vary.
    const std::string boxes=R"cpp(#include <iostream>
void informar_rechazo_cajas(std::ostream& destino,int peticion,int piezas) {
    destino << "Error\n";
}

int main(){return 0;}
)cpp";
    constexpr std::string_view body=R"cpp(    destino << "Error\n";)cpp";
    const auto boxes_edit=[&](std::string_view change) {
        return replace_exact(boxes,body,change);
    };
    check(rejection_function_only_edit(boxes,boxes,RejectionOperation::boxes),
        "An unchanged diagnostic body was rejected");
    check(rejection_function_only_edit(boxes,boxes_edit(R"cpp(    // #define is a harmless comment
    destino << "Revisión #1";)cpp"),RejectionOperation::boxes),
        "The diagnostic guard rejected a comment or quoted '#' character");
    check(rejection_function_only_edit(boxes,boxes_edit(R"cpp(    destino << R"tag(Comentario
    #define is not a directive in a raw string
)tag";)cpp"),RejectionOperation::boxes),
        "The diagnostic guard rejected a raw string's literal '#' line");
    for(const auto modified:{
        R"cpp(    #define ALLOW_BAD_RESULT 1
    destino << "Error\n";)cpp",
        R"cpp(    	#  define ALLOW_BAD_RESULT 1
    destino << "Error\n";)cpp",
        R"cpp(    /* leading comment */ #define ALLOW_BAD_RESULT 1
    destino << "Error\n";)cpp",
        R"cpp(    %:define ALLOW_BAD_RESULT 1
    destino << "Error\n";)cpp"
    })check(!rejection_function_only_edit(boxes,boxes_edit(modified),
        RejectionOperation::boxes),
        "A disguised preprocessor directive was accepted in a U3L6 function");

    const std::string report="int main() {\n    return 0;\n}\n";
    const auto report_edit=[&](std::string_view replacement) {
        return replace_exact(report,"    return 0;",replacement);
    };
    check(report_main_only_edit(report,report),
        "An unchanged reporting main was rejected");
    check(report_main_only_edit(report,report_edit(R"cpp(    // #pragma is harmless commentary
    std::cout << "# report";
    return 0;)cpp")),
        "The reporting guard rejected a harmless comment or string");
    for(const auto modified:{
        R"cpp(    #define CONCEAL_MALFORMED 1
    return 0;)cpp",
        R"cpp(      #pragma warning(disable: 4996)
    return 0;)cpp",
        R"cpp(    /* harmless explanation */ #include "other.h"
    return 0;)cpp",
        R"cpp(    %:define CONCEAL_MALFORMED 1
    return 0;)cpp"
    })check(!report_main_only_edit(report,report_edit(modified)),
        "A disguised preprocessor directive was accepted in a U3L8 main");

    check(!code_format::forbidden_preprocessor_in_edit(
        R"cpp(std::cout << R"demo(
    #include "not a real header"
)demo";)cpp"),
        "The common source guard misclassified a raw-string literal");
    check(code_format::forbidden_preprocessor_in_edit(
        " \t/* safe comment */ #undef VERIFIER\n"),
        "The common source guard missed a directive after a comment");
}
void bounded_u3_rejection_inputs() {
    using curriculum::RejectionOperation;
    const auto boxes_eight=curriculum::rejection_expected_for_input(
        RejectionOperation::boxes,"0 1 2 3 4 5 6 7");
    const auto boxes_nine=curriculum::rejection_expected_for_input(
        RejectionOperation::boxes,"0 1 2 3 4 5 6 7 8");
    check(boxes_eight.has_value()&&!boxes_nine.has_value()&&
        boxes_eight->rejected.empty()&&boxes_eight->exit_code==0,
        "U3L6 boxes accepted more than the eight authored requests");
    const auto balances_eight=curriculum::rejection_expected_for_input(
        RejectionOperation::balance,"0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7");
    const auto balances_nine=curriculum::rejection_expected_for_input(
        RejectionOperation::balance,"0 0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 8 8");
    check(balances_eight.has_value()&&!balances_nine.has_value()&&
        balances_eight->rejected.empty()&&balances_eight->exit_code==0,
        "U3L6 balances did not keep their eight-pair domain");
    check(!curriculum::rejection_expected_for_input(
                RejectionOperation::boxes,"2147483648").has_value()&&
          !curriculum::rejection_expected_for_input(
                RejectionOperation::boxes,"-2147483649").has_value()&&
          !curriculum::rejection_expected_for_input(
                RejectionOperation::balance,"0 0 2147483648").has_value(),
          "U3L6 accepted an integer overflow at EOF as valid completed input");
    check(!curriculum::rejection_expected_for_input(
                RejectionOperation::boxes,"0 trailing").has_value(),
          "U3L6 accepted a malformed suffix after valid input");
    const auto adjacent=curriculum::rejection_expected_for_input(
        RejectionOperation::boxes,"0+1");
    const auto whitespace=curriculum::rejection_expected_for_input(
        RejectionOperation::boxes," \r\n\t");
    check(adjacent&&adjacent->rejected.empty()&&
        adjacent->standard_output=="Cajas=0\nCajas=1\n"&&
        whitespace&&whitespace->rejected.empty()&&
        whitespace->standard_output.empty(),
        "U3L6 lost supported signed numbers or clean empty stdin");
}
void u3_rejection_diagnostics_contracts() {
    const auto boxes=rejection_expected_for_input(
        RejectionOperation::boxes,"-1 0 21 5\n");
    check(boxes&&boxes->standard_output=="Cajas=0\nCajas=2\n"&&
        boxes->exit_code==1&&boxes->rejected.size()==2&&
        boxes->rejected[0].request==1&&boxes->rejected[0].first==-1&&
        boxes->rejected[1].request==3&&boxes->rejected[1].first==21,
        "U3L6 boxes lost a rejected request, its position or stdout");
    const std::string messages=
        "Cajas, peticion 1, piezas -1: rango permitido de 0 a 20; revise la cantidad.\n"
        "Cajas, peticion 3, piezas 21: 0 a 20 es el intervalo entero permitido; verifique los datos.\n";
    check(evaluate_rejection_diagnostics(messages,RejectionOperation::boxes,
        boxes->rejected).correct,
        "Equivalent U3L6 diagnostics were rejected when the interval follows the numbers");
    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas 21: rango 0 a 20, revise.\n"
        "Cajas, peticion 3, piezas -1: rango 0 a 20, revise.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 accepted swapped rejection facts");
    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1, peticion 9: rango de 0 a 20; revise.\n"
        "Cajas, peticion 3, piezas 21: rango de 0 a 20; revise.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 allowed a correct request followed by a contradictory request number");
    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1, piezas 19: rango de 0 a 20; revise.\n"
        "Cajas, peticion 3, piezas 21: rango de 0 a 20; revise.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 allowed conflicting quantities in a single rejected request");
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1, piezas -1: rango de 0 a 20; revise.\n"
        "Cajas, peticion 3, piezas 21: rango de 0 a 20; revise.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "A repeated consistent request field was incorrectly rejected");

    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: revise.\n"
        "Cajas, peticion 3, piezas 21: revise.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 accepted diagnostics without the permitted interval");
    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango 0 a 20; no revise los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; no revise los datos.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 accepted an explicitly negated instruction to review rejected input");
    check(!evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango 0 a 20; sin necesidad de revisar.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; no hace falta verificar.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 accepted a denial of the required corrective action");
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango 0 a 20; no modifique la evidencia; revise los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; no modifique la evidencia; revise los datos.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 rejected a later positive review instruction after an unrelated prohibition");
    for(const std::string_view prohibited:{
        "no se recomienda revisar",
        "no es necesario volver a verificar",
        "sin necesidad de volver a comprobar",
        "nunca corrija",
        "jamas revise",
        "no se debe ajustar",
        "no conviene revisar",
        "no hay que revisar"}) {
        const auto first="Cajas, peticion 1, piezas -1: rango 0 a 20; "+
            std::string(prohibited)+".\n";
        const auto second="Cajas, peticion 3, piezas 21: rango 0 a 20; "+
            std::string(prohibited)+".\n";
        check(!evaluate_rejection_diagnostics(first+second,
                RejectionOperation::boxes,boxes->rejected).correct,
            "U3L6 treated an impersonal/negative recommendation as corrective advice");
    }
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango 0 a 20; no se debe alterar el informe; revise los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; no se debe alterar el informe; revise los datos.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 overextended a prohibition to a subsequent affirmative corrective instruction");
    for(const std::string_view observation:{
        "la revision ya se hizo",
        "el informe ya fue revisado",
        "se reviso automaticamente",
        "los datos se verificaron",
        "la verificacion fue automatica",
        "la comprobacion ya estaba hecha",
        "la rectificacion ya esta registrada",
        "la modificacion ya se realizo",
        "el resultado quedo ajustado"
    }){
        const auto first="Cajas, peticion 1, piezas -1: rango 0 a 20; "+
            std::string(observation)+".\n";
        const auto second="Cajas, peticion 3, piezas 21: rango 0 a 20; "+
            std::string(observation)+".\n";
        check(!evaluate_rejection_diagnostics(first+second,
                RejectionOperation::boxes,boxes->rejected).correct,
            "U3L6 mistook a nominal or completed retrospective as corrective advice");
    }
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango 0 a 20; puede revisar los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; conviene verificar la entrada.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 rejected two equivalent affirmative recommendations");
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1. Enteros entre 0 y 20. Verificacion necesaria.\n"
        "Cajas, peticion 3, piezas 21. Enteros entre 0 y 20. Revision obligatoria.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 rejected equivalent affirmative nominal corrective requests");
    check(evaluate_rejection_diagnostics(
        "Cajas, peticion 1, piezas -1: rango de 0 a 20; comprobacion requerida de los datos.\n"
        "Cajas, peticion 3, piezas 21: rango de 0 a 20; verificacion necesaria de la entrada.\n",
        RejectionOperation::boxes,boxes->rejected).correct,
        "U3L6 rejected a nominal review request with an explicit data object");
    for(const std::string_view denied_nominal:{
        "sin verificacion necesaria",
        "verificacion necesaria no",
        "la verificacion necesaria ya fue realizada",
        "se realizo la verificacion necesaria",
        "la verificacion ya se hizo"}) {
        const auto first="Cajas, peticion 1, piezas -1: rango 0 a 20; "+
            std::string(denied_nominal)+".\n";
        const auto second="Cajas, peticion 3, piezas 21: rango 0 a 20; "+
            std::string(denied_nominal)+".\n";
        check(!evaluate_rejection_diagnostics(first+second,
                RejectionOperation::boxes,boxes->rejected).correct,
            "U3L6 mistook a denied or completed nominal review for corrective advice");
    }
    for(const std::string_view advice:{
        "se requiere una revision de los datos",
        "se recomienda una verificacion",
        "es necesaria una comprobacion",
        "solicite una revision",
        "no modifique el informe; revision necesaria"}) {
        const auto first="Cajas, peticion 1, piezas -1: rango 0 a 20; "+
            std::string(advice)+".\n";
        const auto second="Cajas, peticion 3, piezas 21: rango 0 a 20; "+
            std::string(advice)+".\n";
        check(evaluate_rejection_diagnostics(first+second,
                RejectionOperation::boxes,boxes->rejected).correct,
            "U3L6 rejected an equivalent affirmative nominal correction");
    }
    for(const std::string_view prohibited:{
        "no se requiere una revision",
        "no se recomienda una verificacion necesaria",
        "sin necesidad de hacer una comprobacion",
        "no hay que realizar una revision obligatoria",
        "la revision necesaria ya fue realizada"}) {
        const auto first="Cajas, peticion 1, piezas -1: rango 0 a 20; "+
            std::string(prohibited)+".\n";
        const auto second="Cajas, peticion 3, piezas 21: rango 0 a 20; "+
            std::string(prohibited)+".\n";
        check(!evaluate_rejection_diagnostics(first+second,
                RejectionOperation::boxes,boxes->rejected).correct,
            "U3L6 accepted a cancelled or completed nominal correction");
    }
    check(!evaluate_rejection_stream_probe(messages+"unexpected", "",RejectionOperation::boxes).correct,
        "U3L6 accepted missing independent stream separation");
    const auto split=messages.find('\n');
    const std::string probe=messages.substr(0,split+1)+'\x1e'+messages.substr(split+1);
    check(evaluate_rejection_stream_probe(probe,"",RejectionOperation::boxes).correct&&
        !evaluate_rejection_stream_probe(probe,"outside",RejectionOperation::boxes).correct,
        "U3L6 stream probe accepted writing outside the supplied ostream");

    const auto balances=rejection_expected_for_input(
        RejectionOperation::balance,"2 3 10 0 0 -1 5 5\n");
    check(balances&&balances->standard_output=="Saldo=-1\nSaldo=0\n"&&
        balances->exit_code==1&&balances->rejected.size()==2&&
        balances->rejected[0].request==2&&balances->rejected[0].first==10&&
        balances->rejected[0].second==0&&balances->rejected[1].request==3&&
        balances->rejected[1].first==0&&balances->rejected[1].second==-1,
        "U3L6 balance misidentified the rejected operand or accepted negative saldo");
    check(evaluate_rejection_diagnostics(
        "Saldo, peticion 2, cobros 10, devoluciones 0: intervalo 0 a 9; compruebe ambos datos.\n"
        "Saldo, peticion 3, cobros 0, devoluciones -1: de 0 a 9 es el rango valido; verifique.\n",
        RejectionOperation::balance,balances->rejected).correct,
        "U3L6 rejected an equivalent balance diagnostic");
    check(!evaluate_rejection_diagnostics(
        "Saldo, peticion 2, cobros 0, devoluciones 10: intervalo 0 a 9; revise.\n"
        "Saldo, peticion 3, cobros 0, devoluciones -1: intervalo 0 a 9; revise.\n",
        RejectionOperation::balance,balances->rejected).correct,
        "U3L6 accepted transposed cobros/devoluciones");
    check(!evaluate_rejection_diagnostics(
        "Saldo, peticion 2, cobros 10, devoluciones 0, devoluciones 1: intervalo 0 a 9; revise.\n"
        "Saldo, peticion 3, cobros 0, devoluciones -1: intervalo 0 a 9; revise.\n",
        RejectionOperation::balance,balances->rejected).correct,
        "U3L6 credited an originally correct balance with a later contradictory operand");

    const auto valid_balance=rejection_expected_for_input(
        RejectionOperation::balance,"2 3 0 0 9 0 0 9");
    check(valid_balance&&valid_balance->rejected.empty()&&valid_balance->exit_code==0&&
        valid_balance->standard_output=="Saldo=-1\nSaldo=0\nSaldo=9\nSaldo=-9\n",
        "U3L6 rejected a valid negative result or lost the final unterminated pair");
    const auto empty_balance=rejection_expected_for_input(RejectionOperation::balance,"");
    check(empty_balance&&empty_balance->standard_output.empty()&&
        empty_balance->rejected.empty()&&empty_balance->exit_code==0,
        "U3L6 incorrectly diagnosed empty standard input");
    const auto variation=balance_three_pair_variation("-2 3 2 2 12 5");
    check(variation&&variation->standard_output=="Saldo=0\n"&&
        variation->rejected.size()==2&&variation->exit_code==1,
        "U3L6 rejected the required independent student variation");
}
void u3_observation_reports_contracts() {
    constexpr std::string_view complete_input=
        "A 0 0\nA 2 1\nB -1 -1\nA 2 1\nA 1-1\n"
        "C;=lote 0 1\nC;=lote 0 1\nA 1 -1\n";
    constexpr std::string_view complete_stdout=
        "Linea=1 Caso=A Esperado=0 Obtenido=0 COINCIDE\n"
        "Linea=2 Caso=A Esperado=2 Obtenido=1 DIFIERE\n"
        "Linea=3 Caso=B Esperado=-1 Obtenido=-1 COINCIDE\n"
        "Linea=4 Caso=A Esperado=2 Obtenido=1 DIFIERE\n"
        "Linea=6 Caso=C;=lote Esperado=0 Obtenido=1 DIFIERE\n"
        "Linea=7 Caso=C;=lote Esperado=0 Obtenido=1 DIFIERE\n"
        "Linea=8 Caso=A Esperado=1 Obtenido=-1 DIFIERE\n"
        "Resumen: validos=7;coinciden=2;difieren=5;formato=1\n";
    const auto complete=expected_observation_report(ReportMode::complete,complete_input);
    check(complete&&complete->standard_output==complete_stdout&&
        complete->standard_error=="Linea=5:FORMATO\n"&&complete->exit_code==1,
        "U3L8 complete report lost occurrences, columns, labels or format line");
    constexpr std::string_view differences_input="N 0 0\nR -1 -1\nR 0 -1\nP 3\nR 0 -1\nZ 9 9\n";
    constexpr std::string_view differences_stdout=
        "Linea=3 Caso=R Esperado=0 Obtenido=-1 DIFIERE\n"
        "Linea=5 Caso=R Esperado=0 Obtenido=-1 DIFIERE\n"
        "Resumen: validos=5;coinciden=3;difieren=2;formato=1\n";
    const auto differences=expected_observation_report(ReportMode::differences,differences_input);
    check(differences&&differences->standard_output==differences_stdout&&
        differences->standard_error=="Linea=4:FORMATO\n"&&differences->exit_code==1,
        "U3L8 filtered report incorrectly counted or omitted repeated observations");
    check(matches_observation_report(ReportMode::differences,differences_input,
        differences_stdout,"Linea=4:FORMATO\n",1)&&
        !matches_observation_report(ReportMode::differences,differences_input,
        differences_stdout,"Linea=4:FORMATO_ERRONEO\n",1),
        "U3L8 silently accepted an incorrect diagnostic on stderr");
    constexpr std::string_view no_lf="0 0 0\nN -2 -2";
    const auto pair=expected_observation_report(ReportMode::complete,no_lf);
    check(pair&&pair->standard_error.empty()&&pair->exit_code==0&&
        pair->standard_output==
            "Linea=1 Caso=0 Esperado=0 Obtenido=0 COINCIDE\n"
            "Linea=2 Caso=N Esperado=-2 Obtenido=-2 COINCIDE\n"
            "Resumen: validos=2;coinciden=2;difieren=0;formato=0\n",
        "U3L8 lost a final valid line without LF");
    const auto empty=expected_observation_report(ReportMode::differences,"");
    check(empty&&empty->standard_error.empty()&&empty->exit_code==0&&
        empty->standard_output=="Resumen: validos=0;coinciden=0;difieren=0;formato=0\n",
        "U3L8 rejected an empty input instead of reporting zero observations");
    std::string thirteen;
    for(int i=0;i<13;++i)thirteen+="N 0 0\n";
    check(!expected_observation_report(ReportMode::complete,thirteen),
        "U3L8 accepted a thirteenth physical line outside the authored domain");
}
void u3_compiler_core_evaluation() {
    // A model verdict is not an execution receipt: these synthetic captures
    // check the evaluation boundary, while separate Windows tests must prove
    // that MSVC actually compiled and ran the student's source and probes.
    OutputCheck boxes;
    boxes.rejection_operation=RejectionOperation::boxes;
    OutputCase box_case;
    box_case.standard_input="-1 0 21 5\n";
    box_case.outputs={"Cajas=0\nCajas=2\n"};
    box_case.exit_code=1;
    boxes.cases.push_back(box_case);
    FreshExecution run;
    run.source_revision=47;
    run.compilation_succeeded=true;
    run.execution_completed=true;
    run.exit_code=1;
    run.standard_input=box_case.standard_input;
    run.standard_output=box_case.outputs[0];
    run.standard_error=
        "Cajas, peticion 1, piezas -1: rango 0 a 20; revise los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; verifique la cantidad.\n";
    run.rejection_probe_correct=true;
    check(evaluate_output_check(boxes,run,47).verdict==OutputVerdict::correct,
        "U3L6 core failed to accept a complete reviewed rejection with a verified probe");
    run.rejection_probe_correct=false;
    check(evaluate_output_check(boxes,run,47).verdict==OutputVerdict::incorrect,
        "U3L6 core credited a diagnostic despite a failed independent stream probe");
    run.rejection_probe_correct=true;
    run.standard_error=
        "Cajas, peticion 1, piezas 21: rango 0 a 20; revise los datos.\n"
        "Cajas, peticion 3, piezas -1: rango 0 a 20; verifique la cantidad.\n";
    check(evaluate_output_check(boxes,run,47).verdict==OutputVerdict::incorrect,
        "U3L6 core accepted correct stdout with swapped rejected input facts");
    run.standard_error=
        "Cajas, peticion 1, piezas -1: rango 0 a 20; revise los datos.\n"
        "Cajas, peticion 3, piezas 21: rango 0 a 20; verifique la cantidad.\n";
    run.exit_code=0;run.execution_succeeded=true;
    check(evaluate_output_check(boxes,run,47).verdict==OutputVerdict::incorrect,
        "U3L6 core ignored the authored normal nonzero return status");
    run.exit_code=1;run.execution_succeeded=false;
    check(evaluate_output_check(boxes,run,48).verdict==OutputVerdict::unavailable,
        "U3L6 core credited a capture from an older editor revision");

    OutputCheck report;
    report.observation_report=ReportMode::complete;
    OutputCase case_report;
    case_report.standard_input="A 0 0\nA 1-1\nB 2 2\n";
    const auto expected=expected_observation_report(ReportMode::complete,case_report.standard_input);
    check(expected&&expected->exit_code==1&&expected->standard_error=="Linea=2:FORMATO\n",
        "U3L8 QA fixture lost its deliberate malformed line");
    case_report.outputs={expected->standard_output};
    case_report.standard_error_exact=expected->standard_error;
    case_report.exit_code=expected->exit_code;
    report.cases.push_back(case_report);
    FreshExecution observed;
    observed.source_revision=18;
    observed.compilation_succeeded=true;
    observed.execution_completed=true;
    observed.exit_code=expected->exit_code;
    observed.standard_input=case_report.standard_input;
    observed.standard_output=expected->standard_output;
    observed.standard_error=expected->standard_error;
    observed.report_probe_correct=true;
    check(evaluate_output_check(report,observed,18).verdict==OutputVerdict::correct,
        "U3L8 core rejected a correct derived report with its independent probes");
    observed.report_probe_correct=false;
    check(evaluate_output_check(report,observed,18).verdict==OutputVerdict::incorrect,
        "U3L8 core credited a hard-coded prepared report without probe evidence");
    observed.report_probe_correct=true;
    observed.standard_error="Linea=2:FORMATO_ERRONEO\n";
    check(evaluate_output_check(report,observed,18).verdict==OutputVerdict::incorrect,
        "U3L8 core accepted an incorrect stderr spelling despite valid stdout");
    observed.standard_error=expected->standard_error;
    observed.standard_output+="fabricated";
    check(evaluate_output_check(report,observed,18).verdict==OutputVerdict::incorrect,
        "U3L8 core accepted extraneous stdout after the summary");
}
void historical_frames(){
    Lesson lesson;lesson.locale="es";lesson.id="format-compat-native";
    Block compiler;compiler.type=BlockType::cpp_compiler;compiler.stable_id="block_0002_0004";
    compiler.address.page=2;compiler.address.block=4;
    compiler.initial_code="int main(){return 0;}\n";
    compiler.project_files={{"CMakeLists.txt","add_executable(example main.cpp)\n"},
        {"main.cpp","int main(){return 0;}\n"}};
    compiler.source_files={"main.cpp"};compiler.output_check=OutputCheck{};
    compiler.output_check->cases.push_back(OutputCase{.standard_input="",.outputs={""}});
    const auto original_objective=reference_objective_frame(lesson,compiler);
    const auto original_exercise=exercise_key(lesson,compiler);
    const auto original_draft=draft_key("programming",lesson,compiler);
    compiler.source_format_original_initial_code=compiler.initial_code;
    compiler.source_format_original_project_files=compiler.project_files;
    compiler.initial_code="int main() {\n    return 0;\n}\n";
    compiler.project_files[0].second="add_executable(\n  example main.cpp\n)\n";
    compiler.project_files[1].second=compiler.initial_code;
    check(reference_objective_frame(lesson,compiler)==original_objective &&
        exercise_key(lesson,compiler)==original_exercise &&
        draft_key("programming",lesson,compiler)==original_draft,
        "Safe code formatting changed compiler receipts, reference or draft identities");
    compiler.output_check->cases.front().outputs={"changed"};
    check(reference_objective_frame(lesson,compiler)!=original_objective,
        "An altered expected stdout reused the original compiler objective");
    Block order;order.type=BlockType::order_code;order.stable_id="block_0002_0005";
    order.address.page=2;order.address.block=5;
    order.fragments={"int main(){","return 0;","}"};order.correct_order={0,1,2};
    const auto frame=order_representation_frame(order);
    order.source_format_original_fragments=order.fragments;
    order.fragments[0]="int main() {\n";order.fragments[1]="    return 0;";
    check(order_representation_frame(order)==frame&&
        exercise_key(lesson,order)=="exercise_format-compat-native_page_2_block_5",
        "Formatting order fragments rewrote their historical answer identity");
    order.correct_order={1,0,2};
    check(order_representation_frame(order)!=frame,
        "An altered canonical order was treated as presentation-only");
    state::StateData prior;prior.preferences["native_windows"]["sealed-progress"]="immutable";
    auto roundtrip=state::decode(state::encode(prior));
    check(roundtrip==prior,"Format-only state projection rewrote unrelated stored progress");
}
}
int main(){
    try{
        lexical_safety();cpp_ucn_format_boundaries();historical_frames();
        guarded_open_compiler_edits();bounded_u3_rejection_inputs();
        u3_rejection_diagnostics_contracts();u3_observation_reports_contracts();
        u3_compiler_core_evaluation();
        std::cout<<"PASS safe C++/CMake formatting; protected comments/literals/macros and historic compiler/order state frames\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL code format compatibility: "<<e.what()<<'\n';return 1;
    }
}

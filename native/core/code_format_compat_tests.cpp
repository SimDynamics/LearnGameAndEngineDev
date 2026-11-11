#include "core/code_format_compat.hpp"
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
        "int main(){int number=2;return number;}",
        "int main() {\n    int number = 2;\n\n    return number;\n}\n",Language::cpp),
        "Safe semantic C++ whitespace was rejected");
    check(code_format::equivalent(
        "#include <iostream>\nint main(){return 0;}\n",
        "#include <iostream>\n\nint main() {\n    return 0;\n}\n",Language::cpp),
        "Safe blank rows beside a preprocessor directive were rejected");
    check(code_format::equivalent(
        "add_library(envios STATIC lib.cpp)\ntarget_include_directories(envios PUBLIC include)\n",
        "add_library(\n  envios\n  STATIC lib.cpp\n)\n\ntarget_include_directories(\n  envios PUBLIC include\n)\n",Language::cmake),
        "Safe CMake command formatting was rejected");
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
    check(!code_format::equivalent("a", "a\n",Language::immutable),
        "Unknown data source was modified");
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
        lexical_safety();historical_frames();
        std::cout<<"PASS safe C++/CMake formatting; protected comments/literals/macros and historic compiler/order state frames\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL code format compatibility: "<<e.what()<<'\n';return 1;
    }
}

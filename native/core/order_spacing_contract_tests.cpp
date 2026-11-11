#include "core/lesson.hpp"
#include "core/order_source_text.hpp"
#include "core/lesson_state.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::curriculum;
void check(bool condition,std::string_view message){
    if(!condition)throw std::runtime_error(std::string(message));
}
Block example(std::vector<std::string> source,std::vector<std::size_t> answer){
    Block block;block.type=BlockType::order_code;block.stable_id="block_0002_0003";
    block.address.page=2;block.address.block=3;
    block.fragments=std::move(source);block.correct_order=std::move(answer);
    return block;
}
bool correct(const Block& b,std::initializer_list<std::size_t> order){
    std::vector<std::size_t> submitted(order);
    return evaluate_order(b,submitted).correct;
}
void preserve_original_contract(){
    auto original=example({"", "    int piezas{4};", " \t ", "    return piezas;\n",
        "\n\n", "    std::cout << piezas;", " \r\n"},{1,2,5,4,3,0,6});
    Lesson lesson;lesson.id="formatting_contract";lesson.locale="es";
    const auto canonical=order_representation_frame(original);
    const auto key=exercise_key(lesson,original);
    const auto draft=draft_key("programming",lesson,original);
    const auto prior=original;
    check(correct(original,{1,2,5,4,3,0,6}),
        "The existing canonical answer is no longer correct");
    check(!correct(original,{0,1,6,5,2,4,3}),
        "An undeclared blank historical fragment was incorrectly ignored");
    check(!correct(original,{1,2,5,4,3,6,0}),
        "Two different original blank fragments were treated as interchangeable");
    check(!correct(original,{1,2,5,4,3,0}),
        "A missing original fragment was accepted");
    check(!correct(original,{1,1,5,4,3,0,6}),
        "A duplicate original fragment was accepted");
    for(std::size_t count=1;count<=4;++count) {
        auto decorated=original;
        decorated.optional_order_separators=count;
        check(correct(decorated,{1,2,5,4,3,0,6})&&
            !correct(decorated,{0,1,6,5,2,4,3})&&
            decorated.fragments==prior.fragments&&
            decorated.correct_order==prior.correct_order&&
            order_representation_frame(decorated)==canonical&&
            exercise_key(lesson,decorated)==key&&
            draft_key("programming",lesson,decorated)==draft,
            "A presentation-only separator changed original order, source or state identity");
    }
    lgaed::state::StateData stored;
    stored.preferences["learning_progress"][key]=std::vector<std::string>{"saved-index-order","1,2,5,4,3,0,6"};
    stored.preferences["native_windows"]["original-reference"]=std::string("source-locked");
    const auto bytes=lgaed::state::encode(stored);
    check(lgaed::state::decode(bytes)==stored,
        "Original responses, references or progress were changed on state reopen");
}
void preserve_copied_source_with_empty_cards(){
    const std::vector<std::string> fragments={
        "", "    int value=4;", "", "    return value;", "\n",
        "set(TEXT \"one\\ntwo\")", "  set(TEXT \"one\ntwo\")"};
    check(ordered_source_text(fragments,std::vector<std::size_t>{0,1,2,3})==
        "\n    int value=4;\n\n    return value;",
        "Copy collapsed the leading or internal empty source cards");
    check(ordered_source_text(fragments,std::vector<std::size_t>{1,2,3,4})==
        "    int value=4;\n\n    return value;\n\n",
        "Copy collapsed a trailing authored blank physical line");
    check(ordered_source_text(fragments,std::vector<std::size_t>{5,6})==
        "set(TEXT \"one\\ntwo\")\n  set(TEXT \"one\ntwo\")",
        "Copy rewrote the distinction between a CMake quoted newline and backslash-n");
    const std::vector<std::string> authored_blanks={
        "int a;\n", "", "int b;", "", ""};
    check(ordered_source_text(authored_blanks,std::vector<std::size_t>{0,1,2})==
        "int a;\n\nint b;",
        "A blank authored card after an existing LF lost its physical row");
    check(ordered_source_text(authored_blanks,std::vector<std::size_t>{0,1,2,3,4})==
        "int a;\n\nint b;\n\n",
        "Multiple trailing authored empty cards were merged or omitted");
    check(ordered_source_text(authored_blanks,std::vector<std::size_t>{1,3,2})==
        "\n\nint b;",
        "Two leading authored empty cards were merged");

    const std::vector<std::string> continued={
        "#define SUM(x) \\\n", "    ((x) + 1)", "std::cout << \"\\n\";",
        "const char* code=R\"cpp(line one\nline two)cpp\";",
        "// A line comment", "int next=2;", ""};
    check(ordered_source_text(continued,std::vector<std::size_t>{0,1})==
        "#define SUM(x) \\\n    ((x) + 1)",
        "Copy inserted a second physical newline inside a continued macro");
    check(ordered_source_text(continued,std::vector<std::size_t>{2,3})==
        "std::cout << \"\\n\";\nconst char* code=R\"cpp(line one\nline two)cpp\";",
        "Copy confused a C++ escaped newline and physical raw-string newline");
    check(ordered_source_text(continued,std::vector<std::size_t>{4,5})==
        "// A line comment\nint next=2;",
        "Copy concatenated a subsequent statement into a line comment");
    check(ordered_source_text(continued,std::vector<std::size_t>{0,1,6})==
        "#define SUM(x) \\\n    ((x) + 1)\n",
        "An explicit trailing blank source card was lost after a continued macro");
    const std::vector<std::string> cmake={
        "set(SCRIPT \"first\n", "second\")\n", "message(STATUS \"ready\")"};
    check(ordered_source_text(cmake,std::vector<std::size_t>{0,1,2})==
        "set(SCRIPT \"first\nsecond\")\nmessage(STATUS \"ready\")",
        "Copy inserted a second newline into an existing CMake string or command boundary");
    check(ordered_source_text(fragments,std::vector<std::size_t>{}).empty(),
        "An empty unsolved order introduced source bytes");
    bool rejected=false;
    try{(void)ordered_source_text(fragments,std::vector<std::size_t>{1,99});}
    catch(const std::out_of_range&){rejected=true;}
    check(rejected,"Copy accepted an invented source-card identity");
}

void preserve_drag_fill_template_and_historical_answers(){
    Block drag;drag.type=BlockType::drag_fill_code;
    drag.stable_id="block_0005_0006";drag.address.page=5;drag.address.block=6;
    drag.code_template="int main() {\n\n    const int group=4;\n    []\n\n    []\n}\n";
    drag.choices={"std::cout << group;", "return 0;", "return 1;"};
    drag.correct_order={0,1};
    Lesson lesson;lesson.id="drag_spacing_contract";lesson.locale="es";
    const auto key=exercise_key(lesson,drag);
    const auto stored=drag;
    const std::vector<std::size_t> correct{0,1},wrong{1,0},missing{0},duplicate{0,0};
    check(evaluate_drag_answers(drag,correct).correct,
        "A legitimate drag-fill result was rejected by template-only blank lines");
    check(!evaluate_drag_answers(drag,wrong).correct&&
          !evaluate_drag_answers(drag,missing).correct&&
          !evaluate_drag_answers(drag,duplicate).correct,
        "A missing, reused or reordered code fragment passed after adding visual separation");
    check(drag.code_template==stored.code_template&&
          drag.choices==stored.choices&&drag.correct_order==stored.correct_order&&
          exercise_key(lesson,drag)==key,
        "Drag-fill source, answer index, reference or saved response identity changed");
    check(drag.code_template.find("\n\n")!=std::string::npos&&
          drag.code_template.find("    []")!=std::string::npos,
        "The drag-fill source lost its meaningful indentation or vertical whitespace");
}

void preserve_equivalence_and_meaningful_newlines(){
    auto grouped=example({"", "    abrir();", "\n", "    cerrar();", "    registrar();", " \t"},{1,2,3,4,0,5});
    grouped.order_equivalence_groups={{1,3}};
    check(correct(grouped,{3,2,1,4,0,5}),"A reviewed equivalent instruction was rejected");
    check(!correct(grouped,{3,0,1,2,4,5}),
        "The original whitespace fragment was omitted from grading");
    check(!correct(grouped,{4,2,1,3,0,5}),
        "An unrelated instruction inherited the reviewed equivalence");
    grouped.order_equivalence_groups={{1,1}};
    check(!evaluate_order(grouped,grouped.correct_order).correct,
        "A malformed equivalence group was accepted");

    auto cmake=example({"  set(TEXT \"uno\n dos\")", "  set(TEXT \"uno dos\")",
        "", "  message(STATUS \"texto\")\n\n"},{0,2,3,1});
    check(correct(cmake,{0,2,3,1}),"CMake source line breaks lost canonical grading");
    check(!correct(cmake,{2,0,3,1}),"An original blank card was silently exempted");
    check(!correct(cmake,{1,2,3,0}),"A meaningful CMake newline was normalized");

    auto cpp=example({"std::cout << \"\\n\";", "std::cout << \"\n\";", " \t "},{0,2,1});
    check(correct(cpp,{0,2,1}),"The original literal and physical newline no longer differ");
    check(!correct(cpp,{1,2,0}),"Literal backslash-n and actual newline were exchanged");
    cpp.optional_order_separators=3;
    check(!correct(cpp,{1,2,0}),"Visual-only cards weakened original source comparison");
    auto blank=example({""," \t","\r\n"},{0,1,2});
    check(correct(blank,{0,1,2})&&!correct(blank,{2,1,0}),
        "Preexisting whitespace-only fragments changed their historical answer");
}
}
int main(){
    try {
        preserve_original_contract();
        preserve_equivalence_and_meaningful_newlines();
        preserve_drag_fill_template_and_historical_answers();
        preserve_copied_source_with_empty_cards();
        std::cout<<"PASS exact historical order/source, explicit optional presentation only, state and references\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL order/code separator contract: "<<e.what()<<'\n';
        return 1;
    }
}

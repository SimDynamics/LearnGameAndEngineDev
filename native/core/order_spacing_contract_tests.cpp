#include "core/lesson.hpp"
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
        std::cout<<"PASS exact historical order/source, explicit optional presentation only, state and references\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL order/code separator contract: "<<e.what()<<'\n';
        return 1;
    }
}

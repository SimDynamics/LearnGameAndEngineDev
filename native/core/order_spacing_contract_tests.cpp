#include "core/lesson.hpp"
#include "core/lesson_state.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace lgaed::curriculum;

void check(bool accepted,std::string_view reason){
    if(!accepted)throw std::runtime_error(std::string(reason));
}
Block ordered(std::vector<std::string> fragments,std::vector<std::size_t> canonical){
    Block block;
    block.type=BlockType::order_code;
    block.stable_id="block_0002_0003";
    block.address.page=2;
    block.address.block=3;
    block.fragments=std::move(fragments);
    block.correct_order=std::move(canonical);
    block.explanation="Las instrucciones tienen este orden.";
    block.review_feedback="Revisa el orden.";
    return block;
}
bool correct(const Block& block,std::initializer_list<std::size_t> order){
    const std::vector<std::size_t> response(order);
    return evaluate_order(block,response).correct;
}
void optional_decorative_cards(){
    auto block=ordered({"", "    int piezas{4};", "\t  ", "    return piezas;\n",
        "\n\n", "    std::cout << piezas;", " \r\n"}, {1,2,5,4,3,0,6});
    Lesson lesson;
    lesson.id="formatting_contract";
    lesson.locale="es";
    const auto original_frame=order_representation_frame(block);
    const auto original_key=exercise_key(lesson,block);
    const auto original_draft=draft_key("programming",lesson,block);
    const auto original_fragments=block.fragments;
    const auto original_order=block.correct_order;
    // Blank cards remain physical and can be dragged anywhere, without
    // becoming tokens, required instructions or new persisted owners.
    check(correct(block,{1,2,5,4,3,0,6}),
        "The canonical ordering with visual blank cards is not correct");
    check(correct(block,{0,1,6,5,2,4,3}),
        "Moving blanks among correctly ordered statements changed the verdict");
    check(correct(block,{1,5,3,2,0,4,6}),
        "Interleaving empty cards altered the correct relative statement order");
    check(!correct(block,{1,3,0,2,5,4,6}),
        "A meaningful instruction reversal was masked by decorative blanks");
    check(!correct(block,{1,1,5,2,4,3,6}),
        "A duplicated statement index forged a complete physical arrangement");
    check(!correct(block,{1,2,5,4,3,0}),
        "A missing physical card was silently accepted as a complete draft");
    check(block.fragments==original_fragments&&block.correct_order==original_order&&
        order_representation_frame(block)==original_frame&&
        exercise_key(lesson,block)==original_key&&
        draft_key("programming",lesson,block)==original_draft,
        "Decorative grading rewrote source, historical response/draft keys or canonical representation");

    lgaed::state::StateData saved;
    saved.preferences["learning_progress"][original_key]=std::vector<std::string>{
        "historical-raw-order","1,2,5,4,3,0,6"};
    saved.preferences["native_windows"]["unrelated_reference"]=std::string("authenticated-old-reference");
    const auto restored=lgaed::state::decode(lgaed::state::encode(saved));
    check(restored==saved,"Original response/reference bytes changed across a state round trip");
}
void equivalence_groups_keep_their_scope(){
    auto block=ordered({"", "    abrir();", "\n", "    cerrar();", "    registrar();", " \t"},
        {1,2,3,4,0,5});
    block.order_equivalence_groups={{1,3}};
    check(correct(block,{3,0,1,2,4,5}),
        "Reviewed equivalent instructions no longer exchange their approved slots");
    check(!correct(block,{4,0,3,2,1,5}),
        "Optional blanks broadened the approved equivalence to an unrelated instruction");
    block.order_equivalence_groups={{1,1}};
    check(!evaluate_order(block,block.correct_order).correct,
        "A duplicated equivalence owner was accepted");
}
void source_newlines_remain_significant(){
    auto block=ordered({"  set(TEXT \"uno\n dos\")", "  set(TEXT \"uno dos\")", "",
        "  message(STATUS \"texto\")\n\n"}, {0,2,3,1});
    check(correct(block,{2,0,3,1}),
        "An optional spacer changed a CMake source ordering");
    check(!correct(block,{2,1,3,0}),
        "A meaningful CMake newline was normalized into an ordinary space");
    check(block.fragments[0].find('\n')!=std::string::npos&&
        block.fragments[3].ends_with("\n\n"),
        "An authored CMake or C++ line ending was destroyed");

    auto different=ordered({"std::cout << \"\\n\";", "std::cout << \"\n\";", " \t "},{0,2,1});
    check(!correct(different,{1,2,0}),
        "A C++ escape sequence was confused with a physical newline");
    check(correct(different,{2,0,1}),
        "Decorative spaces blocked exact C++ text ordering");
    auto empty=ordered({""," \t","\r\n"},{0,1,2});
    check(!evaluate_order(empty,empty.correct_order).correct,
        "An ordering without any actual instructions was awarded a correct answer");
}
} // namespace

int main(){
    try{
        optional_decorative_cards();
        equivalence_groups_keep_their_scope();
        source_newlines_remain_significant();
        std::cout<<"PASS original order keys and source bytes, optional blank drag cards, exact C++/CMake lines, historical state\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"FAIL order/code spacing contract: "<<error.what()<<'\n';
        return 1;
    }
}

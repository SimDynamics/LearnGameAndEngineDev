// Focused lexical policy regressions for P3 authoring contracts.
// Only the immutable original lesson may declare the class/alias structure.
// No program submitted by a learner is executed by this test.
#include "core/cpp_class_structure_check.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
} // namespace

int main() try {
    namespace cs=lgaed::curriculum::cpp_class_structure;
    const auto original=cs::lexical(R"source(
#include <memory>
%:include <vector>
// #if 0
/* #define Owner NotTheOwner */
class Owner {
public:
    virtual ~Owner() = default;
};
namespace Unrelated { class Owner {}; }
using Owners = std::vector<std::unique_ptr<Owner>>;
)source");
    require(original.has_value(),
        "A valid literal include or an inert comment blocked class authoring");
    require(cs::defined_class(*original,"Owner").has_value(),
        "The real global Owner was hidden by a namespaced namesake");
    require(cs::global_alias(*original,"Owners",{
        "id:using","id:Owners","op:=","id:std","op:::",
        "id:vector","op:<","id:std","op:::",
        "id:unique_ptr","op:<","id:Owner","op:>>","op:;"}),
        "The original global polymorphic alias was lost");

    constexpr std::array<std::string_view,8> ambiguous{{
        "#if 0\nclass Owner {};\n#endif\n",
        "#ifdef OWNER_PRESENT\nclass Owner {};\n#endif\n",
        "#ifndef OWNER_PRESENT\nclass Owner {};\n#endif\n",
        "#define Owner Other\nclass Owner {};\n",
        "#undef Owner\nclass Owner {};\n",
        "#include OWNER_HEADER\nclass Owner {};\n",
        "#line 22\nclass Owner {};\n",
        "%:if 0\nclass Owner {};\n%:endif\n"
    }};
    for(const auto source:ambiguous) {
        require(!cs::lexical(source),
            "A conditional or macro directive authenticated an unknown class");
    }
    const auto inert_literal=cs::lexical(R"source(
const char* note=R"doc(#if 0
class FakeOwner {};
#endif
)doc";
class Owner {};
)source");
    require(inert_literal.has_value()&&
        cs::defined_class(*inert_literal,"Owner").has_value()&&
        !cs::defined_class(*inert_literal,"FakeOwner"),
        "Preprocessor-like words inside a raw string were treated as directives");

    // These lookalikes have real C++ braces and global names, but cannot
    // authenticate a concrete owner or a two-interface implementation.
    for(const auto candidate:{
            "enum class Owner { value };",
            "enum struct Owner { value };",
            "template<class T> class Owner { int count_; };",
            "template<typename T> struct Owner { int count_; };"
        }) {
        const auto tokens=cs::lexical(candidate);
        require(tokens&& !cs::defined_class(*tokens,"Owner"),
            "An enum or a class template was accepted as a concrete Owner");
    }
    const auto generic_base=cs::lexical(R"source(
struct Base {};
template<class T> struct Owner : public Base {};
)source");
    require(generic_base&& !cs::defined_class(*generic_base,"Owner","Base"),
        "A class template impersonated concrete public inheritance");
    const auto generic_pair=cs::lexical(R"source(
struct Left {};
struct Right {};
template<class T> class Combined : public Left, public Right {};
)source");
    require(generic_pair&&
        !cs::defined_public_bases(*generic_pair,"Combined",{"Left","Right"}),
        "A class template impersonated a concrete two-interface owner");
    const auto distinct_plain=cs::lexical(R"source(
template<class T> struct Other {};
struct Owner {};
struct Left {};
struct Right {};
struct Combined : public Left, public Right {};
)source");
    require(distinct_plain&&
        cs::defined_class(*distinct_plain,"Owner")&&
        cs::defined_public_bases(*distinct_plain,"Combined",{"Left","Right"}),
        "A preceding terminated template hid later real global classes");

    // A data member must be an individual object's field. It is not enough
    // that a method or an inline static field has the required type and name.
    const auto instance=cs::lexical(R"source(
class Owner {
    std::unique_ptr<int[]> items_{};
    int count_ = 0;
};
)source");
    const auto owner=instance?cs::defined_class(*instance,"Owner"):std::nullopt;
    require(instance&&owner&&
        cs::direct_field(*instance,*owner,{
            "id:std","op:::","id:unique_ptr","op:<","id:int",
            "op:[","op:]","op:>","id:items_"})&&
        cs::direct_int_fields(*instance,*owner,{"count_"}),
        "The genuine array owner/count instance fields were rejected");

    const auto method=cs::lexical(R"source(
class Owner {
    int count_() const noexcept { return 0; }
};
)source");
    const auto method_owner=method?cs::defined_class(*method,"Owner"):
        std::nullopt;
    require(method&&method_owner&&
        !cs::direct_int_fields(*method,*method_owner,{"count_"}),
        "A zero-argument member function impersonated an int data field");

    // A previous static member function may use C++ digraph braces. Those
    // braces end its declaration even without a trailing semicolon: the
    // next field belongs to each instance, not to the static method.
    const auto digraph=cs::lexical(R"source(
#include <memory>
class Owner {
    static void helper() <% %>
    std::unique_ptr<int[]> items_{};
    int count_ = 0;
};
)source");
    const auto digraph_owner=digraph?
        cs::defined_class(*digraph,"Owner"):std::nullopt;
    require(digraph&&digraph_owner&&
        cs::direct_field(*digraph,*digraph_owner,{
            "id:std","op:::","id:unique_ptr","op:<","id:int",
            "op:[","op:]","op:>","id:items_"})&&
        cs::direct_int_fields(*digraph,*digraph_owner,{"count_"}),
        "An unrelated static digraph method hid real per-instance fields");

    // The same boundary cannot make a genuinely static field look owned.
    const auto digraph_shared=cs::lexical(R"source(
#include <memory>
class Owner {
    static void helper() <% %>
    static std::unique_ptr<int[]> items_;
};
)source");
    const auto digraph_shared_owner=digraph_shared?
        cs::defined_class(*digraph_shared,"Owner"):std::nullopt;
    require(digraph_shared&&digraph_shared_owner&&
        !cs::direct_field(*digraph_shared,*digraph_shared_owner,{
            "id:std","op:::","id:unique_ptr","op:<","id:int",
            "op:[","op:]","op:>","id:items_"}),
        "An actual static field passed after a digraph-delimited method");

    const auto shared=cs::lexical(R"source(
class Owner {
    inline static std::unique_ptr<int[]> items_{};
    static inline int count_ = 0;
};
)source");
    const auto shared_owner=shared?cs::defined_class(*shared,"Owner"):
        std::nullopt;
    require(shared&&shared_owner&&
        !cs::direct_field(*shared,*shared_owner,{
            "id:std","op:::","id:unique_ptr","op:<","id:int",
            "op:[","op:]","op:>","id:items_"})&&
        !cs::direct_int_fields(*shared,*shared_owner,{"count_"}),
        "Class-wide static storage impersonated per-object ownership");

    std::cout<<"PASS scoped C++ authoring: literal includes and inert notes "
                "accepted; preprocessor-generated or disabled owners rejected\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL lexical class authoring: "<<error.what()<<'\n';
    return 1;
}

#include "core/cpp_local_role_probe.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace lgaed::curriculum;
void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
CppLocalRoleProbe probe(double replacement,std::string output) {
    return {replacement,{std::move(output)},false,std::nullopt,{}};
}
CppLocalRoleCheck library() {
    auto after=probe(2,"Inicio: 7\nAhora: 12");
    after.after_update=true;after.expected_increase=3;
    auto before=probe(19,"Inicio: 19\nAhora: 10");
    before.before_update_of="current";
    return {{{"saved",CppLocalRoleValueType::integer,7,{before}},
             {"initial",CppLocalRoleValueType::integer,7,{probe(11,"Inicio: 11\nAhora: 14")}},
             {"current",CppLocalRoleValueType::integer,7,{after}}}};
}
CppLocalRoleCheck station() {
    return {{{"first",CppLocalRoleValueType::real,1.5,{probe(2.5,"Cable: 4.75")}},
             {"second",CppLocalRoleValueType::real,2.25,{probe(4.25,"Cable: 5.75")}},
             {"total",CppLocalRoleValueType::real,3.75,{probe(9.25,"Cable: 9.25")}}}};
}
std::size_t slot(const CppLocalRolePlan& plan,std::string_view name) {
    for(std::size_t i=0;i<plan.candidates.size();++i)
        if(plan.candidates[i].name==name)return i;
    throw std::runtime_error("Missing candidate "+std::string(name));
}
void contracts_are_bounded_and_typed() {
    auto spec=library();
    require(cpp_local_role_check_valid(spec),"Dependent role may precede its destination");
    for(unsigned scenario=0;scenario<19;++scenario) {
        auto bad=spec;
        switch(scenario) {
        case 0:bad.roles.clear();break;
        case 1:bad.roles.push_back(bad.roles.front());break;
        case 2:bad.roles[0].id="bad-name";break;
        case 3:bad.roles[0].id=std::string(65,'a');break;
        case 4:bad.roles[0].initial_value=7.25;break;
        case 5:bad.roles[0].initial_value=2147483648.0;break;
        case 6:bad.roles[0].probes[0].replacement=7;break;
        case 7:bad.roles[2].probes[0].replacement=0;break;
        case 8:bad.roles[2].probes[0].expected_increase.reset();break;
        case 9:bad.roles[2].probes[0].expected_increase=0;break;
        case 10:bad.roles[1].probes[0].expected_increase=3;break;
        case 11:bad.roles[0].probes[0].before_update_of="absent";break;
        case 12:bad.roles[0].probes[0].before_update_of="saved";break;
        case 13:bad.roles[2].probes[0].before_update_of="saved";break;
        case 14:bad.roles[0].probes[0].expected_stdout={"x\n","x\r\n"};break;
        case 15:bad.roles[0].probes[0].expected_stdout={std::string(4097,'x')};break;
        case 16:bad.roles[0].probes[0].expected_stdout={std::string("x\0y",3)};break;
        case 17:bad.roles[0].probes.push_back(bad.roles[0].probes[0]);break;
        case 18:bad.roles[0].value_type=static_cast<CppLocalRoleValueType>(98);break;
        }
        require(!cpp_local_role_check_valid(bad),"Invalid scalar role contract was accepted");
    }
    for(double bad:{std::numeric_limits<double>::infinity(),
                    std::numeric_limits<double>::quiet_NaN(),1000000.25}) {
        auto real=station();real.roles[0].initial_value=bad;
        require(!cpp_local_role_check_valid(real),"Unbounded or nonfinite real was accepted");
    }
    const auto real=station();
    require(cpp_local_role_check_valid(real)&&
        cpp_local_role_expected_output(real.roles[0].probes[0],"Cable: 4.75")&&
        !cpp_local_role_expected_output(real.roles[0].probes[0],"Cable: 4.750001"),
        "Private stdout gained a floating tolerance");
    auto lf=probe(1,"a\nb");
    require(cpp_local_role_expected_output(lf,"a\r\nb"),"CRLF normalization was lost");
    auto changed=real;changed.roles[0].probes[0].replacement=2.75;
    require(cpp_local_role_check_frame(real)!=cpp_local_role_check_frame(changed),
        "Different double replacements share a specification frame");
}
void initializers_names_and_grouping_are_preserved() {
    const std::string declarations[]{
        "double a=1.5,b=2.25,total=a+b;",
        "const double a{1.5},b(2.25),total{a+b};",
        "constexpr auto a=1.5,b=2.25,total=a+b;",
        "double a=3.0/2.0,b=static_cast<double>(2.25),total=(a+b);",
        "double a,b,total;a=1.5;b=2.25;total=a+b;",
        "double a{},b{},total{};a=1.5;b=2.25;total=a+b;",
        "double a=0;a=1.5;double b=0;b=2.25;double total=0;total=a+b;",
        "double a{};a=0.5;a=1.5;double b{};b=0;b=2.25;double total=a+b;"
    };
    for(const auto& declaration:declarations) {
        const auto source="#include <iostream>\nint main(){"+declaration+
            "std::cout<<\"Cable: \"<<total;}";
        const auto plan=cpp_local_role_plan(source,station());
        require(plan&&plan->candidates.size()==3,"Equivalent scalar initialization was not located");
        for(std::size_t role=0;role<3;++role) {
            const auto varied=cpp_local_role_variant(*plan,station(),role,0,role);
            require(varied&&varied->find("using lgaed_role_probe_84c7_type = decltype(")!=std::string::npos&&
                varied->find("static_assert(::std::is_same_v<::std::remove_cv_t<decltype(")!=std::string::npos,
                "Const/inferred scalar was not shadowed with its actual original type");
        }
        require(source.find("lgaed_role_probe_")==source.npos,"A learner source was modified");
    }
    const auto reordered=cpp_local_role_plan(
        "int main(){double unused{1.5};double segundo{2.25};"
        "double primero{1.5};double juntos{primero+segundo};}",station());
    require(reordered&&reordered->candidates.size()==4&&
        cpp_local_role_variant(*reordered,station(),0,0,slot(*reordered,"primero")),
        "A renamed/reordered local or unrelated same-valued candidate was rejected");
}
void temporal_roles_retain_distinct_objects_and_real_updates() {
    for(const auto* source:{
        "int main(){int x=7;const int saved=x;int delta=3;x+=delta;"
        "std::cout<<saved<<x;}",
        "int main(){const int saved=7;int x=saved; x=x+3;std::cout<<saved<<x;}",
        "int main(){int x{7},saved{x},delta{3};x+=1;x+=2;std::cout<<saved<<x;}",
        "int main(){int x{};x=7;int saved{};saved=x;x+=3;std::cout<<saved<<x;}"
    }) {
        const auto spec=library();
        const auto plan=cpp_local_role_plan(source,spec);
        require(plan.has_value(),"A valid forward/reversed/grouped copy was not located");
        const auto current=slot(*plan,"x"),saved=slot(*plan,"saved");
        const auto snapshot=cpp_local_role_variant(*plan,spec,0,0,saved,current);
        const auto after=cpp_local_role_variant(*plan,spec,2,0,current);
        require(snapshot&&after&&
            snapshot->find("decltype(saved)")!=snapshot->npos&&
            after->find("!= 3LL) return 1;")!=after->npos&&
            after->find("2147483647LL")!=after->npos,
            "Copy checkpoint, exact actual increase or overflow guard was lost");
        require(!cpp_local_role_variant(*plan,spec,0,0,current,current),
            "One object was accepted as both the snapshot and its updated source");
    }
    const auto late=cpp_local_role_plan(
        "int main(){int x=7;x+=3;int saved=x;std::cout<<saved<<x;}",library());
    require(late&&!cpp_local_role_variant(*late,library(),0,0,slot(*late,"saved"),slot(*late,"x")),
        "A copy made after the state update was treated as an earlier snapshot");
    const auto fixed=cpp_local_role_plan(
        "int main(){int x=7;int saved=x;x=10;std::cout<<saved<<x;}",library());
    require(fixed&&!cpp_local_role_variant(*fixed,library(),2,0,slot(*fixed,"x")),
        "A fixed replacement was accepted as an update of the preceding state");
}
void unsafe_or_ambiguous_sources_have_no_plan() {
    for(const auto* source:{
        "#define VALUE 7\nint main(){int x=VALUE;}",
        "int main(){int lgaed_role_probe_84c7_type=7;}",
        "int helper(){int x=7;return x;}int main(){return helper();}",
        "int main(){int x=7;std::cout<<__LINE__<<x;}",
        "int main(){int x=7;}int main(){int x=8;}",
        "int main(){int x;std::cout<<x;x=7;}"
    })require(!cpp_local_role_plan(source,library()),"Unsafe source acquired a scalar plan");
    std::string source="int main(){";
    for(unsigned i=0;i<17;++i)source+="int x"+std::to_string(i)+"=7;";
    source+="}";
    require(!cpp_local_role_plan(source,library()),"Candidate bound was not enforced");
}
}
int main() {
    try {
        contracts_are_bounded_and_typed();
        initializers_names_and_grouping_are_preserved();
        temporal_roles_retain_distinct_objects_and_real_updates();
        unsafe_or_ambiguous_sources_have_no_plan();
        std::cout<<"PASS local scalar roles: typed bounds, name-free candidates, grouped/const/constexpr/auto initializers, independent copy checkpoints, exact increases and protected source frames.\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL local scalar roles: "<<error.what()<<'\n';return 1;
    }
}

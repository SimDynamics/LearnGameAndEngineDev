#include "core/cpp_local_string_probe.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using lgaed::curriculum::CppLocalStringCheck;
using lgaed::curriculum::CppLocalStringMoment;
using lgaed::curriculum::CppLocalStringProbe;
using lgaed::curriculum::cpp_local_string_check_valid;
using lgaed::curriculum::cpp_local_string_expected_output;
using lgaed::curriculum::cpp_local_string_override;

void require(bool value,std::string_view description) {
    if(!value)throw std::runtime_error(std::string(description));
}
constexpr auto initialized=CppLocalStringMoment::after_initialization;
constexpr auto updated=CppLocalStringMoment::after_first_assignment;
constexpr auto printing=CppLocalStringMoment::before_output;
const std::string source=R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
}
)CPP";
const std::string revision=R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    codigo = "07";
    etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
}
)CPP";

void independent_source_variations() {
    const auto building=cpp_local_string_override(source,
        {"edificio","Sala Norte",initialized,{}});
    const auto code=cpp_local_string_override(source,
        {"codigo","003",initialized,{}});
    const auto control=cpp_local_string_override(source,
        {"etiqueta","ETIQUETA_CONTROL",printing,{}});
    require(building&&code&&control,"A root-level std::string could not be instrumented");
    const auto injected=[&](const std::string& name,const std::string& replacement){
        return name+" = std::string{"+
            lgaed::curriculum::cpp_local_string_detail::cpp_bytes_literal(replacement)+"};";
    };
    require(building->find(injected("edificio","Sala Norte"))!=building->npos&&
            building->find(injected("codigo","003"))==building->npos&&
            building->find("std::string codigo{\"04\"};")!=building->npos&&
            building->find("std::string etiqueta = edificio + \"-\" + codigo;")!=building->npos,
        "Mutating the building changed the stored code or companion variable");
    require(code->find(injected("codigo","003"))!=code->npos&&
            code->find(injected("edificio","Sala Norte"))==code->npos&&
            code->find("std::string edificio{\"Aula\"};")!=code->npos,
        "Mutating the code changed the original building");
    require(control->find(injected("etiqueta","ETIQUETA_CONTROL"))!=control->npos&&
            control->find(injected("etiqueta","ETIQUETA_CONTROL"))<
                control->find("std::cout << etiqueta;")&&
            control->find("std::string etiqueta = edificio + \"-\" + codigo;")<
                control->find(injected("etiqueta","ETIQUETA_CONTROL")),
        "The control must overwrite the local tag immediately before its output");
    require(source.find("ETIQUETA_CONTROL")==source.npos&&
            source.find("Sala Norte")==source.npos&&source.find("003")==source.npos,
        "A private probe altered the student's source");
}
void after_real_assignment_and_noexcept() {
    const auto new_code=cpp_local_string_override(revision,
        {"codigo","B2",updated,{}});
    require(new_code&&new_code->find("codigo = \"07\";")!=new_code->npos&&
            new_code->find("codigo = \"07\";")<
                new_code->find("codigo = std::string{")&&
            new_code->find("codigo = std::string{")<
                new_code->find("etiqueta = edificio + \"-\" + codigo;"),
        "The second assignment probe did not follow the actual new code");
    for(const auto suffix:{"noexcept","noexcept(true)","noexcept(noexcept(4))"}) {
        const std::string src=std::string("#include <string>\nint main() ")+suffix+
            " {std::string codigo{\"04\"}; codigo=\"07\"; std::string etiqueta=codigo; }\n";
        require(cpp_local_string_override(src,{"codigo","B2",updated,{}}).has_value(),
            "A valid main() noexcept was not recognized");
    }
    require(!cpp_local_string_override(source,{"codigo","B2",updated,{}}),
        "An after-assignment probe was attached without a corresponding assignment");
}
void exact_text_bytes_and_type_checks() {
    const std::string data="003";
    const auto literal=lgaed::curriculum::cpp_local_string_detail::cpp_bytes_literal(data);
    require(literal==R"("\060\060\063")",
        "An initial zero was lost in the C++ literal encoding");
    const std::string utf8="\xC3\xB1";
    require(lgaed::curriculum::cpp_local_string_detail::cpp_bytes_literal(utf8)==
            R"("\303\261")",
        "UTF-8 bytes did not survive the portable private encoding");
    CppLocalStringCheck check{{{"codigo","003",initialized,{"Aula-003","Aula-003\n"}},
                               {"etiqueta","CONTROL",printing,{"CONTROL","CONTROL\n"}}}};
    require(cpp_local_string_check_valid(check)&&
            cpp_local_string_expected_output(check.probes[0],"Aula-003\r\n")&&
            !cpp_local_string_expected_output(check.probes[0],"Aula-03")&&
            !cpp_local_string_expected_output(check.probes[0],"Aula-003-X"),
        "The hidden string stdout alternatives were not checked exactly");
    check.probes.push_back(check.probes[0]);
    require(!cpp_local_string_check_valid(check),
        "Repeated probes using the same variable and phase were accepted");
    check.probes.pop_back();
    check.probes[0].expected_stdout={"Aula-003\n","Aula-003\r\n"};
    require(!cpp_local_string_check_valid(check),
        "Equivalent LF and CRLF observations were counted twice");
    check.probes[0].replacement=std::string("ab\0cd",5);
    require(!cpp_local_string_check_valid(check),
        "A hidden C++ string probe admitted an embedded NUL");
}

void unsafe_or_ambiguous_positions_reject() {
    const std::string front="#include <iostream>\n#include <string>\nint main() {\n";
    const std::string back="\n std::cout << etiqueta;\n}\n";
    for(const auto invalid:{
        "const std::string etiqueta{\"Aula-04\"};",
        "std::string *etiqueta = nullptr;",
        "std::string etiqueta{\"A\"}, alternativa{\"B\"};",
        "std::string etiqueta{\"A\"}; std::string etiqueta{\"B\"};",
        "if(true) {std::string etiqueta{\"Aula-04\"};}",
        "for(int n=0;n<1;++n){std::string etiqueta{\"Aula-04\"};}",
        "std::string etiqueta{\"A\"}; if(true){std::cout << etiqueta;}"
    }) {
        const auto bad=front+invalid+back;
        if(std::string_view(invalid).find("if(true){std::cout")!=std::string_view::npos) {
            // The extra cout outside the if is still valid; only the
            // nested output statement must NOT be used as evidence.
            const auto inside=front+
                "std::string etiqueta{\"A\"}; if(true){std::cout<<etiqueta;}}";
            require(!cpp_local_string_override(inside,
                {"etiqueta","CONTROL",printing,{}}),
                "A nested output was treated as a root-level print");
        }else {
            require(!cpp_local_string_override(bad,
                {"etiqueta","X",initialized,{}}),
                "An ambiguous or differently owned local string was instrumented");
        }
    }
    require(!cpp_local_string_override(
        "#define etiqueta otro\n"+front+
        "std::string etiqueta{\"04\"};\nstd::cout<<etiqueta;\n}",
        {"etiqueta","CONTROL",printing,{}}),
        "A macro-controlled local string bypassed the source guard");
    require(!cpp_local_string_override(
        "#include <string>\nstd::string etiqueta{\"global\"};\nint main(){std::cout<<etiqueta;}",
        {"etiqueta","CONTROL",printing,{}}),
        "A global variable was mutated as though it were main-local");
    require(!cpp_local_string_override(
        "#include <string>\nint main(){std::string etiqueta{\"ok\"}; std::cout<<\"etiqueta\";}",
        {"etiqueta","CONTROL",printing,{}}),
        "Mentioning the identifier in a literal counted as a real read");
    require(!cpp_local_string_override(
        "#include <string>\nint main(){std::string etiqueta{\"ok\"}; std::cout<<etiqueta;}",
        {"etiqueta","",printing,{}}),
        "An empty replacement escaped the private-probe contract");
}
} // namespace

int main() {
    try {
        independent_source_variations();
        after_real_assignment_and_noexcept();
        exact_text_bytes_and_type_checks();
        unsafe_or_ambiguous_positions_reject();
        std::cout<<"PASS local string source probes: independent text values, "
            "reassignment, real output reads, UTF-8 byte preservation, "
            "noexcept and fail-closed ambiguous scopes.\n";
    }catch(const std::exception& error) {
        std::cerr<<"FAIL local string source probes: "<<error.what()<<'\n';
        return 1;
    }
}

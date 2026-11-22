// Run genuine private source mutations through a real C++23 compiler on
// disposable QA paths. These examples are contract fixtures, not Work's
// curricular solution nor executed student submissions.
#include "core/cpp_local_string_probe.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using lgaed::curriculum::CppLocalStringMoment;
using lgaed::curriculum::CppLocalStringProbe;
using lgaed::curriculum::cpp_local_string_override;
constexpr auto init=CppLocalStringMoment::after_initialization;
constexpr auto update=CppLocalStringMoment::after_first_assignment;
constexpr auto output=CppLocalStringMoment::before_output;

struct Variation {
    CppLocalStringProbe probe;
    std::string expected;
};
struct Scenario {
    std::string name,source,visible;
    std::vector<Variation> variants;
};
void write(const std::filesystem::path& path,std::string_view bytes) {
    std::ofstream file(path,std::ios::binary | std::ios::trunc);
    if(!file)throw std::runtime_error("Cannot create private C++ fixture");
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!file)throw std::runtime_error("Cannot write private C++ fixture");
}
void emit(const std::filesystem::path& directory,std::string_view name,
          std::string_view source,std::string_view expected,bool matches) {
    const auto prefix=directory/std::string(name);
    write(prefix.string()+".cpp",source);
    std::string criterion=matches?"A\n":"R\n";
    criterion+=expected;
    write(prefix.string()+".expected",criterion);
}
}
int main(int argc,char** argv) try {
    if(argc!=2)return 2;
    const std::filesystem::path directory=argv[1];
    std::filesystem::create_directories(directory);
    const std::vector<Scenario> scenarios{
        {"u2l7_build_label",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"codigo","003",init,{}},"Aula-003"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}},
        {"u2l7_update_label",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    codigo = "07";
    etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-07",
            {{{"codigo","B2",update,{}},"Aula-B2"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}},
        {"u2l7_equivalent_auto",
            R"CPP(#include <iostream>
#include <string>
int main() noexcept {
    auto edificio=std::string{"Aula"};
    auto codigo=std::string{"04"};
    auto etiqueta=edificio+"-"+codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}},
        {"u2l7_grouped_string_declarations",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"}, codigo{"04"};
    std::string etiqueta{edificio+"-"+codigo};
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"codigo","003",init,{}},"Aula-003"}}},
        {"u2l7_delayed_string_assignments",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio, codigo;
    edificio = "Aula";
    codigo = "04";
    std::string etiqueta = edificio+"-"+codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"codigo","003",init,{}},"Aula-003"}}},
        {"u2l7_delayed_member_assign",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio, codigo;
    edificio.assign("Aula");
    codigo.assign("04");
    std::string etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"codigo","003",init,{}},"Aula-003"}}},
        {"u2l7_trailing_main",
            R"CPP(#include <iostream>
#include <string>
auto main() noexcept -> int {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta=edificio+"-"+codigo;
    std::cout<<etiqueta;
})CPP",
            "Aula-04",
            {{{"codigo","003",init,{}},"Aula-003"}}},
        {"u2l7_member_named_etiqueta",
            R"CPP(#include <iostream>
#include <string>
struct Datos {std::string etiqueta{"EXTERNA"};};
int main() {
    Datos datos;
    std::string copia{datos.etiqueta}, etiqueta{"Aula-04"};
    std::cout << datos.etiqueta;
    std::cout << etiqueta;
})CPP",
            "EXTERNAAula-04",
            {{{"etiqueta","CONTROL",init,{}},"EXTERNACONTROL"},
             {{"etiqueta","CONTROL",output,{}},"EXTERNACONTROL"}}},
        {"u2l7_member_assign_update",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    codigo.assign("07");
    etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-07",
            {{{"codigo","B2",update,{}},"Aula-B2"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}},
        {"u2l7_append_default_string",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio, codigo;
    edificio.append("Aula");
    codigo.append("04");
    std::string etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-04",
            {{{"edificio","Sala Norte",init,{}},"Sala Norte-04"},
             {{"codigo","003",init,{}},"Aula-003"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}},
        {"u2l7_replace_updated_string",
            R"CPP(#include <iostream>
#include <string>
int main() {
    std::string edificio{"Aula"};
    std::string codigo{"04"};
    std::string etiqueta = edificio + "-" + codigo;
    codigo.replace(0, codigo.size(), "07");
    etiqueta = edificio + "-" + codigo;
    std::cout << etiqueta;
})CPP",
            "Aula-07",
            {{{"codigo","B2",update,{}},"Aula-B2"},
             {{"etiqueta","CONTROL",output,{}},"CONTROL"}}}
    };
    std::size_t accepted=0,rejected=0;
    for(const auto& scenario:scenarios) {
        emit(directory,scenario.name,scenario.source,scenario.visible,true);
        ++accepted;
        for(std::size_t i=0;i<scenario.variants.size();++i) {
            const auto& trial=scenario.variants[i];
            const auto adjusted=cpp_local_string_override(scenario.source,trial.probe);
            if(!adjusted)throw std::runtime_error(
                "Cannot instrument accepted local string "+scenario.name);
            emit(directory,scenario.name+"_probe_"+std::to_string(i),
                *adjusted,trial.expected,true);
            ++accepted;
        }
    }
    // Hard-coded output keeps the first public answer but cannot respond to
    // a variation of the actual text variable.
    constexpr std::string_view fixed=R"CPP(#include <iostream>
#include <string>
int main(){std::string edificio{"Aula"};std::string codigo{"04"};
std::string etiqueta=edificio+"-"+codigo;std::cout<<"Aula-04";})CPP";
    const auto fixed_building=cpp_local_string_override(fixed,
        {"edificio","Sala Norte",init,{}});
    const auto fixed_code=cpp_local_string_override(fixed,
        {"codigo","003",init,{}});
    if(!fixed_building||!fixed_code)
        throw std::runtime_error("A hard-coded observation hid its declared inputs");
    emit(directory,"reject_fixed_building",*fixed_building,"Sala Norte-04",false);
    emit(directory,"reject_fixed_code",*fixed_code,"Aula-003",false);
    rejected+=2;
    constexpr std::string_view fixed_update=R"CPP(#include <iostream>
#include <string>
int main(){std::string edificio{"Aula"};std::string codigo{"04"};
std::string etiqueta=edificio+"-"+codigo;codigo="07";
etiqueta=edificio+"-"+codigo;std::cout<<"Aula-07";})CPP";
    const auto missed_update=cpp_local_string_override(fixed_update,
        {"codigo","B2",update,{}});
    if(!missed_update)throw std::runtime_error("A fixed updated label escaped the probe");
    emit(directory,"reject_fixed_updated_code",*missed_update,"Aula-B2",false);
    ++rejected;
    // A student may construct etiqueta but print its inputs instead; both
    // public outputs look identical. No valid before-output probe exists
    // because the output statement does NOT read etiqueta itself.
    constexpr std::string_view unread=R"CPP(#include <iostream>
#include <string>
int main(){std::string edificio{"Aula"};std::string codigo{"04"};
std::string etiqueta=edificio+"-"+codigo;std::cout<<edificio<<"-"<<codigo;})CPP";
    if(cpp_local_string_override(unread,{"etiqueta","CONTROL",output,{}}))
        throw std::runtime_error("An unread etiqueta passed the private output control");
    if(accepted!=34||rejected!=3)
        throw std::runtime_error("Private source fixture count changed unexpectedly");
    std::cout<<"PASS generated 37 private C++ string cases (34 positive, "
                "3 hard-coded negative) and one structurally rejected unread etiqueta\n";
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL local string C++ fixture: "<<error.what()<<'\n';
    return 1;
}

#include "core/identifier_rename_compat.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using lgaed::curriculum::identifier_rename::SourceSlot;
using lgaed::curriculum::identifier_rename::SourcePlan;
using lgaed::curriculum::identifier_rename::NamedRename;
using lgaed::curriculum::identifier_rename::declared_tokens_only;
using lgaed::curriculum::code_format::Language;

void require(bool value, const char* message) {
    if(!value)throw std::runtime_error(message);
}
std::vector<SourceSlot> old_sources() {
    return {
        {"initialCode",Language::cpp,
         "#include <iostream>\n"
         "extern int maxima;\n"
         "int main() { std::cout << \"Maxima:\" << maxima << \"\\n\"; }\n"},
        {"projectFiles/0/initialCode",Language::cpp,
         "int maxima = 8; // maxima text stays unchanged\n"},
        {"projectFiles/1/initialCode",Language::cmake,
         "project(original)\n"}
    };
}
std::vector<SourceSlot> valid_sources() {
    auto result=old_sources();
    result[0].source=
        "#include <iostream>\n"
        "extern int frecuencia_maxima_encontrada;\n"
        "int main() { std::cout << \"Maxima:\" << frecuencia_maxima_encontrada << \"\\n\"; }\n";
    result[1].source=
        "int frecuencia_maxima_encontrada = 8; // maxima text stays unchanged\n";
    return result;
}
std::vector<SourcePlan> valid_plan() {
    return {
        {"initialCode",{{"maxima","frecuencia_maxima_encontrada",2}}},
        {"projectFiles/0/initialCode",{{"maxima","frecuencia_maxima_encontrada",1}}}
    };
}
void success() {
    const auto before=old_sources(),after=valid_sources(),plan=valid_plan();
    require(declared_tokens_only(before,after,plan),
        "The same valid identifier rename must be allowed across two files");
    require(!lgaed::curriculum::code_format::equivalent(
        before[0].source,after[0].source,Language::cpp),
        "Identifier renames must not be misclassified as source formatting");
}
void negative_contracts() {
    const auto before=old_sources(),after=valid_sources(),plan=valid_plan();
    auto revision=after;
    revision[0].source.replace(revision[0].source.find("\"Maxima:\""),9,"\"Changed:\"");
    require(!declared_tokens_only(before,revision,plan),
        "A changed output literal passed as a rename");
    revision=after;
    revision[1].source.replace(revision[1].source.find("= 8"),3,"= 9");
    require(!declared_tokens_only(before,revision,plan),
        "A changed constant passed as a rename");
    revision=after;
    revision[0].source.replace(revision[0].source.find("<< frecuencia_maxima_encontrada"),
        2,"<=");
    require(!declared_tokens_only(before,revision,plan),
        "A changed operator passed as a rename");
    revision=after;
    revision[2].source="project(changed)\n";
    require(!declared_tokens_only(before,revision,plan),
        "A changed CMake source passed as a C++ identifier rename");
    revision=after;
    revision[0].source.replace(revision[0].source.find("frecuencia_maxima_encontrada"),
        std::string("frecuencia_maxima_encontrada").size(),"maxima");
    require(!declared_tokens_only(before,revision,plan),
        "A partially renamed declaration passed the counted contract");
}
void preprocessor_and_other_sources() {
    auto before=old_sources(),after=valid_sources();
    auto plan=valid_plan();
    before.push_back({"projectFiles/2/initialCode",Language::cpp,
        "#define maxima 0\n"});
    after.push_back(before.back());
    require(!declared_tokens_only(before,after,plan),
        "A macro in an unchanged translation unit was ignored");
    before.back().source="#define frecuencia_maxima_encontrada 0\n";
    after.back()=before.back();
    require(!declared_tokens_only(before,after,plan),
        "A macro taking the new identifier was ignored");
    before.back().source="int read(int maxima) { return maxima; }\n";
    after.back()=before.back();
    require(!declared_tokens_only(before,after,plan),
        "Unrenamed identifier in another source was ignored");
    before.back().source="const char* label=\"maxima\";\n";
    after.back()=before.back();
    require(declared_tokens_only(before,after,plan),
        "An unrelated string literal incorrectly counts as a C++ reference");
}
void cmake_external_macro_contract() {
    auto before=old_sources(),after=valid_sources();
    const auto plan=valid_plan();
    for(const std::string_view symbol:{"maxima","frecuencia_maxima_encontrada"}) {
        before[2].source="target_compile_definitions(ejercicio PRIVATE "+
            std::string(symbol)+"=7)\n";
        after[2].source=before[2].source;
        require(!declared_tokens_only(before,after,plan),
            "A macro defined by CMake was incorrectly accepted");
    }
    before[2].source=
        "# maxima and frecuencia_maxima_encontrada are inert comments\n"
        "project(original)\n";
    after[2].source=before[2].source;
    require(declared_tokens_only(before,after,plan),
        "A comment in CMake was mistaken for an executable macro definition");
}
void observable_identifier_spelling() {
    auto before=old_sources(),after=valid_sources();
    auto plan=valid_plan();
    before[0].source="const char* maxima() { return __func__; }\n";
    after[0].source=
        "const char* frecuencia_maxima_encontrada() { return __func__; }\n";
    before[1].source="int unrelated = 8;\n";
    after[1]=before[1];
    plan.pop_back();plan[0].names[0].occurrences=1;
    require(!declared_tokens_only(before,after,plan),
        "__func__ observes the renamed function name despite identical other tokens");

    for(const std::string_view observer:{
            "__FUNCTION__", "__PRETTY_FUNCTION__", "__FUNCSIG__",
            "__FUNCDNAME__", "typeid"}) {
        before[0].source="const char* maxima() { return "+
            std::string(observer)+"; }\n";
        after[0].source="const char* frecuencia_maxima_encontrada() { return "+
            std::string(observer)+"; }\n";
        require(!declared_tokens_only(before,after,plan),
            "C++ function/type spelling observer passed identifier-only validation");
    }

    for(const std::string_view macro:{
            "#define STRINGIFY(x) #x",
            "#define CONCAT(a,b) a##b"}) {
        before[0].source=std::string(macro)+
            "\nint maxima = 4;\nint main(){return maxima;}\n";
        after[0].source=std::string(macro)+
            "\nint frecuencia_maxima_encontrada = 4;"
            "\nint main(){return frecuencia_maxima_encontrada;}\n";
        plan[0].names[0].occurrences=2;
        require(!declared_tokens_only(before,after,plan),
            "An unchanged macro could stringify or paste the new identifier");
    }

    before[0].source=
        "int maxima = 4; // __func__ only in this comment\n"
        "const char* note=\"__PRETTY_FUNCTION__\";\n";
    after[0].source=
        "int frecuencia_maxima_encontrada = 4; // __func__ only in this comment\n"
        "const char* note=\"__PRETTY_FUNCTION__\";\n";
    plan[0].names[0].occurrences=1;
    require(declared_tokens_only(before,after,plan),
        "An inert comment or literal was mistaken for an observer");
}
void portable_identifier_spellings() {
    using lgaed::curriculum::identifier_rename::identifier;
    for(const std::string_view name:{
            "maxima","frecuencia_maxima_encontrada","Nombre9","a_b_c"})
        require(identifier(name),"A valid ASCII identifier was rejected");
    for(const std::string_view name:{
            "new","import","final","override","__internal","1nombre",
            "_local","nombre-con-guion","frecuencia_máxima","niño","maximañ","mi__variable"})
        require(!identifier(name),
            "A reserved, non-ASCII or invalid source spelling bypassed native validation");
    // The whole source plan must enforce this invariant, not only its helper.
    auto before=old_sources(),after=valid_sources(),plan=valid_plan();
    plan[0].names[0].replacement="frecuencia_máxima";
    require(!declared_tokens_only(before,after,plan),
        "Native rename plan accepted a locale-dependent Unicode destination");
}
void byte_exact_rename_does_not_reformat() {
    const auto before=old_sources(),valid=valid_sources(),plan=valid_plan();
    require(declared_tokens_only(before,valid,plan),
        "The original token-only rename stopped matching exact source bytes");
    auto changed=valid;
    const auto rewrite=[&](std::string_view old,std::string_view replacement) {
        changed=valid;
        const auto offset=changed[0].source.find(old);
        require(offset!=std::string::npos,"Missing original physical formatting");
        changed[0].source.replace(offset,old.size(),replacement);
        require(!declared_tokens_only(before,changed,plan),
            "A physical source change was accepted as an identifier-only revision");
    };
    rewrite("extern int","extern  int");
    rewrite("std::cout <<","std::cout  <<");
    rewrite("#include <iostream>\n","#include <iostream>\r\n");
    rewrite("int main()","int\tmain()");
}
void name_conflicts() {
    auto before=old_sources(),after=valid_sources(),plan=valid_plan();
    plan[1].names[0].replacement="otro_destino";
    after[1].source="int otro_destino = 8; // maxima text stays unchanged\n";
    require(!declared_tokens_only(before,after,plan),
        "The same old C++ name adopted inconsistent destinations");
    before=old_sources();after=valid_sources();plan=valid_plan();
    before[1].source="int mayores = 8;\n";
    after[1].source="int frecuencia_maxima_encontrada = 8;\n";
    plan[1].names[0]={"mayores","frecuencia_maxima_encontrada",1};
    require(!declared_tokens_only(before,after,plan),
        "Two previously distinct symbols merged into one name");
    before=old_sources();after=valid_sources();plan=valid_plan();
    plan[0].names[0].replacement="new";
    require(!declared_tokens_only(before,after,plan),
        "A C++ keyword was accepted as a destination");
}
} // namespace

int main() try {
    success();
    negative_contracts();
    preprocessor_and_other_sources();
    cmake_external_macro_contract();
    observable_identifier_spelling();
    portable_identifier_spellings();
    byte_exact_rename_does_not_reformat();
    name_conflicts();
    std::cout << "PASS native C++ lexical identifier-rename proof, cross-file owner "
              << "consistency, macros, existing names and immutable tokens\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

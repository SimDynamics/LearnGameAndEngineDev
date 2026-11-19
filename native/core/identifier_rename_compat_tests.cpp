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
    name_conflicts();
    std::cout << "PASS native C++ lexical identifier-rename proof, cross-file owner "
              << "consistency, macros, existing names and immutable tokens\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}

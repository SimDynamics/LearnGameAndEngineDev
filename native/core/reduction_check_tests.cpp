#include "core/reduction_check.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::curriculum;
void require(bool condition,std::string_view reason) {
    if(!condition)throw std::runtime_error(std::string(reason));
}
std::string replace(std::string source,std::string_view before,std::string_view after) {
    const auto position=source.find(before);
    require(position!=source.npos,"The source test has lost its intended edit");
    source.replace(position,before.size(),after);return source;
}
constexpr std::string_view initial=R"cpp(#include <iostream>
#include <vector>
#include <string>

int acumulado{0};

int contar_altos(std::vector<int> datos, int minimo) {
    for (int valor : datos) {
        if (valor >= minimo) {
            ++acumulado;
        }
    }
    return acumulado;
}

void mostrar_detalle(const std::vector<int>& datos) {
    const std::string etiqueta{"Datos del informe"};
    std::cout << etiqueta << '=' << datos.size() << '\n';
}

int main() {
    const std::vector<int> recibo{4, 0, 8};
    mostrar_detalle(recibo);
    const int primera = contar_altos({2}, 2);
    const int segunda = contar_altos({}, 2);
    std::cout << "Primera=" << primera << ";Segunda=" << segunda << '\n';
    std::cout << "Informe terminado\n";
    return 0;
}
)cpp";
ReductionSpec contract() {
    ReductionSpec spec;
    spec.function_name="contar_altos";spec.state_name="acumulado";
    spec.initial_source=initial;
    const auto function=reduction_extract_definition(initial,spec.function_name);
    require(function.has_value(),"The original definition was not admitted");
    spec.protected_definition=*function;
    spec.calls={{{2},2,1,1},{{},2,1,1}};
    spec.observed_call=1;spec.required_result=0;
    spec.dependencies={{"vector",{"std::vector"}},
        {"iostream",{"std::cout","std::cerr","std::clog"}},
        {"string",{"std::string","std::to_string"}},
        {"cstdio",{"std::printf","std::puts"}}};
    require(reduction_spec_valid(spec),"The original reduction contract is invalid");
    return spec;
}
std::string program(const ReductionSpec& spec,std::string_view main,
        std::string_view headers="#include <iostream>\n#include <vector>\n") {
    return std::string(headers)+"\nint acumulado{0};\n\n"+
        spec.protected_definition+"\n\n"+std::string(main);
}
std::string reference(const ReductionSpec& spec) {
    return program(spec,R"cpp(int main() {
    const int primera = contar_altos({2}, 2);
    const int segunda = contar_altos({}, 2);
    std::cout << "Primera=" << primera << ";Segunda=" << segunda << '\n';
    return 0;
}
)cpp");
}
std::string ignored_first(const ReductionSpec& spec) {
    return program(spec,R"cpp(int main() {
    contar_altos({2}, 2);
    const int segunda = contar_altos({}, 2);
    std::printf(segunda ? "Segunda=%d\n" : "Sin valores=%d\n", segunda);
    return segunda == 1 ? 0 : 7;
}
)cpp","#include <cstdio>\n#include <vector>\n");
}
std::vector<ReductionRun> observations(const ReductionSpec& spec) {
    const auto plan=*reduction_plan(spec);
    std::vector<ReductionRun> runs;
    for(const auto& phase:plan) {
        ReductionRun run;
        run.program_frame="same-program";run.compiled=true;run.executed=true;run.completed_normally=true;
        run.phase=phase;run.exit_code=phase.replacement?7:0;
        auto before=spec.initial_state;
        for(std::size_t i=0;i<spec.calls.size();++i) {
            const auto& call=spec.calls[i];
            run.calls.push_back({call.values,call.argument,before,call.state_after,call.result,
                i==spec.observed_call&&phase.replacement?*phase.replacement:call.result});
            before=call.state_after;
        }
        run.stdout_bytes="Primera=1; Segunda="+std::to_string(run.calls[spec.observed_call].delivered)+".\n";
        runs.push_back(std::move(run));
    }
    return runs;
}
void source_contracts() {
    const auto spec=contract();
    const auto reduced=reference(spec);
    require(reduction_source_check(reduced,spec).accepted,"The independent reference was rejected");
    require(reduction_source_check(ignored_first(spec),spec).accepted,
        "Ignored first return, conditional printf and private nonzero exits must be admitted");
    const auto notice_removed=replace(std::string(initial),"    std::cout << \"Informe terminado\\n\";\n","");
    require(reduction_source_check(notice_removed,spec).accepted,
        "Removing only the final accessory notice must suffice; the helper may remain");
    auto unused_helper=replace(notice_removed,"    mostrar_detalle(recibo);\n","");
    require(reduction_source_check(unused_helper,spec).accepted,"A retained unused helper was rejected");
    require(!reduction_source_check(replace(unused_helper,"#include <string>\n",""),spec).accepted,
        "An unused helper still needs its directly declared string dependency");
    for(const auto& zero:std::array<std::string,7>{"int acumulado{};","int acumulado;","int acumulado=0;",
            "int acumulado(+0);","int acumulado{-0};","int acumulado = -0;","int acumulado(0);"}) {
        require(reduction_source_check(replace(reduced,"int acumulado{0};",zero),spec).accepted,
            "An equivalent explicit/global zero initialization was rejected");
    }
    auto condition=replace(reduced,"std::cout << \"Primera=\"", "if (segunda < 2) std::cout << \"Primera=\"");
    require(reduction_source_check(condition,spec).accepted,
        "A relational less-than was mistaken for an unclosed template declaration");
    auto converted=program(spec,R"cpp(int main() {
    contar_altos({2},2);
    int valor=contar_altos({},2);
    std::cerr << "Consulta vacia: " << std::to_string(valor) << ".\n";
}
)cpp","#include <iostream>\n#include <vector>\n#include <string>\n");
    require(reduction_source_check(converted,spec).accepted,"A different channel and to_string representation was rejected");
    const auto using_names=replace(reduced,"int main() {","using namespace std;\nint main() {");
    const auto unqualified=replace(using_names,"std::cout","cout");
    require(reduction_source_check(unqualified,spec).accepted,"An imported standard output name was rejected");
    require(!reduction_source_check(replace(unqualified,"#include <iostream>\n",""),spec).accepted,
        "using namespace std hid a directly required output header");

    const std::array<std::string,8> cosmetic{
        std::string(initial),
        "// Inert new comment\n"+std::string(initial),
        replace(std::string(initial),"const int primera","int primera"),
        replace(std::string(initial),"return 0;\n",""),
        replace(std::string(initial),"Informe terminado","Otro aviso"),
        replace(std::string(initial),"\"Primera=\"","\"\""),
        replace(std::string(initial),"\"Informe terminado\\n\"","'\\n'"),
        replace(replace(std::string(initial),"mostrar_detalle(","informar_detalle("),"mostrar_detalle(recibo)","informar_detalle(recibo)")
    };
    for(const auto& unchanged:cosmetic)
        require(!reduction_source_check(unchanged,spec).accepted,
            "Names, comments, qualifiers, literals or return-zero changes manufactured a reduction");
    for(const auto& name:std::array<std::string,5>{"to_string","c_str","setw","setprecision","setfill"}) {
        const auto renamed=replace(replace(std::string(initial),"mostrar_detalle(",name+"("),
            "mostrar_detalle(recibo)",name+"(recibo)");
        require(!reduction_source_check(renamed,spec).accepted,
            "Renaming a helper to a standard adapter's unqualified name manufactured a reduction");
    }
    const auto merged=replace(std::string(initial),
        "<< '\\n';\n    std::cout << \"Informe terminado\\n\";",
        "<< '\\n' << \"Informe terminado\\n\";");
    require(!reduction_source_check(merged,spec).accepted,
        "Merging two cout statements retained every emission and is not a reduction");
    auto api_only=replace(std::string(initial),
        "std::cout << \"Primera=\" << primera << \";Segunda=\" << segunda << '\\n';\n    std::cout << \"Informe terminado\\n\";",
        "std::printf(\"Primera=%d;Segunda=%d\\nInforme terminado\\n\", primera, segunda);");
    api_only="#include <cstdio>\n"+api_only;
    require(!reduction_source_check(api_only,spec).accepted,
        "Changing only the output API and preserving all emitted data manufactured a reduction");
    for(const auto& invalid:std::array<std::string,7>{
            replace(reduced,"++acumulado;","acumulado=0;"),
            replace(reduced,"int acumulado{0};","int acumulado{1};"),
            replace(reduced,"int acumulado{0};","int acumulado{0};\nint acumulado;"),
            replace(reduced,"int main() {","int main() { int acumulado{0};"),
            replace(reduced,"#include <vector>\n",""),
            "#define ALIAS 1\n"+reduced,
            reduced+"\nint lgaed_reduction_marker=0;\n"})
        require(!reduction_source_check(invalid,spec).accepted,"A source-preservation/dependency guard was bypassed");
    // Source structure and runtime observations answer separate questions.
    const auto fixed=program(spec,"int main(){contar_altos({2},2);contar_altos({},2);std::cout << 1;}\n");
    require(reduction_source_check(fixed,spec).accepted,"A runtime-only counterexample must reach the observation verifier");
    require(reduction_instrument(initial,spec,{}).has_value(),"Normality of the unedited original must remain observable");
    require(reduction_instrument(replace(notice_removed,"#include <string>\n",""),spec,{}).has_value(),
        "Private normality must not silently turn a missing public dependency into source credit");
    std::cout<<"PASS reduction structure, equivalent drivers, direct dependencies and cosmetic counterexamples\n";
}
void grammar_and_frames() {
    const auto spec=contract();
    auto equivalent=spec;
    equivalent.initial_source="// Header note\n"+equivalent.initial_source;
    equivalent.protected_definition=replace(equivalent.protected_definition,"{","{ /* harmless */");
    require(reduction_spec_valid(equivalent)&&reduction_spec_frame(equivalent)==reduction_spec_frame(spec),
        "Inert comments changed the authenticated reduction contract");
    auto changed=spec;changed.calls.front().argument=3;
    require(reduction_spec_frame(changed)!=reduction_spec_frame(spec),"A changed call borrowed the old objective");
    changed=spec;changed.observed_call=0;require(!reduction_spec_valid(changed),"The preparation call became the observed dependent call");
    changed=spec;changed.required_result=1;require(!reduction_spec_valid(changed),"A correct result was declared as the reproduced contradiction");
    changed=spec;changed.exit_code=1;require(!reduction_spec_valid(changed),"The reproduction goal accepted nonzero public exit");
    changed=spec;changed.calls.resize(1);require(!reduction_spec_valid(changed),"A one-call reproduction was admitted");
    changed=spec;changed.calls.front().values.resize(65);require(!reduction_spec_valid(changed),"An excessive input sequence was admitted");
    changed=spec;changed.dependencies.push_back(changed.dependencies.front());
    require(!reduction_spec_valid(changed),"A duplicated dependency declaration was admitted");
    changed=spec;changed.dependencies.front().header="../vector";
    require(!reduction_spec_valid(changed),"An unsafe dependency header was admitted");
    changed=spec;changed.dependencies.front().symbols={"std::vector","std::vector"};
    require(!reduction_spec_valid(changed),"A duplicate service was admitted");
    for(const auto& replacement:std::array<std::string,5>{
            "static int contar_altos","inline int contar_altos","long contar_altos","int& contar_altos","int* contar_altos"})
        require(!reduction_extract_definition(replace(std::string(initial),"int contar_altos",replacement),"contar_altos"),
            "A qualified or differently typed protected function was admitted");
    require(!reduction_extract_definition(std::string(initial)+
        "\nint& contar_altos(int a,int b,int c){static int value=0;return value;}\n","contar_altos"),
        "A reference-return overload escaped the definition guard");
    const auto prototype="int contar_altos(std::vector<int> datos, int minimo);\n";
    require(reduction_extract_definition(prototype+std::string(initial),"contar_altos").has_value(),
        "The same exact global prototype was rejected");
    require(!reduction_extract_definition(replace(spec.protected_definition,"return acumulado;","return sizeof(__func__);"),"contar_altos"),
        "A body depending on lambda-sensitive function introspection was admitted");
    for(const auto& text:std::array<std::string,4>{"int acumulado{00};","int acumulado{0u};",
            "int acumulado{0x0};","int acumulado();"}) {
        changed=spec;changed.initial_source=replace(changed.initial_source,"int acumulado{0};",text);
        require(!reduction_spec_valid(changed),"An ambiguous/unsupported state initializer was admitted");
    }
    for(const auto actual:std::array<std::int32_t,2>{std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
        changed=spec;changed.calls[1].result=actual;
        changed.required_result=actual==std::numeric_limits<std::int32_t>::min()?actual+1:actual-1;
        const auto plan=reduction_plan(changed);
        require(plan&&plan->size()==3&&!plan->front().replacement&&
            (*plan)[1].replacement!=(*plan)[2].replacement&&
            (*plan)[2].replacement!=actual,"The int32 boundary collapsed independent interventions");
    }
    std::cout<<"PASS reduction grammar, authenticated objective and distinct boundary interventions\n";
}
void traces_and_observation() {
    const auto spec=contract();
    const auto trace=reduction_parse_trace(
        "visible:LGAED_REDUCTION_CALL:0,2,0,1,1,1,1,2\r\n"
        "LGAED_REDUCTION_CALL:1,2,1,1,1,1,0\nLGAED_REDUCTION_END\nsuffix");
    require(trace&&trace->completed_normally&&trace->calls.size()==2&&
            trace->calls.front().values==std::vector<std::int32_t>{2}&&
            trace->calls.back().values.empty()&&trace->visible=="visible:suffix",
            "Real call/state/results were not separated from the original stderr");
    require(reduction_parse_trace("plain")->completed_normally==false,"Missing final witness became normal");
    for(const auto& invalid:std::array<std::string,7>{
            "LGAED_REDUCTION_END", "LGAED_REDUCTION_END\nLGAED_REDUCTION_END\n",
            "LGAED_REDUCTION_CALL:1,2,0,1,1,1,1,2\n",
            "LGAED_REDUCTION_CALL:0,2,0,1,1,1,0,2\n",
            "LGAED_REDUCTION_CALL:0,2,0,1,1,1,1,2147483648\n",
            "LGAED_REDUCTION_END\nLGAED_REDUCTION_CALL:0,2,0,1,1,1,0\n",
            "LGAED_REDUCTION_CALL:0,2,0,1,1,1,0\nLGAED_REDUCTION_END extra\n"})
        require(!reduction_parse_trace(invalid),"A malformed/reordered private trace was accepted");
    auto runs=observations(spec);
    require(reduction_verify(spec,runs,"same-program").accepted,
        "First return plus a final-punctuation observed result was rejected");
    for(std::size_t i=0;i<runs.size();++i) {
        runs[i].stdout_bytes.clear();
        runs[i].stderr_bytes=(i==0?"La consulta vacia devuelve ":"Nueva observacion: ")+
            std::to_string(runs[i].calls.back().delivered)+".\n";
    }
    require(reduction_verify(spec,runs,"same-program").accepted,
        "Different labels and stderr without the first returned value were rejected");
    for(std::size_t i=0;i<runs.size();++i) {
        const auto delivered=std::to_string(runs[i].calls.back().delivered);
        runs[i].stdout_bytes=delivered+" "+delivered;
        runs[i].stderr_bytes.clear();
    }
    require(reduction_verify(spec,runs,"same-program").accepted,"Multiple visible copies lost their common causal source");
    runs=observations(spec);
    for(auto& run:runs)run.stdout_bytes="Segunda=1\n";
    require(!reduction_verify(spec,runs,"same-program").accepted,"A fixed one was treated as calculated observation");
    runs=observations(spec);
    runs[0].stdout_bytes="Segunda=1; aviso=1\n";
    runs[1].stdout_bytes="Segunda=1; aviso=0\n";
    runs[2].stdout_bytes="Segunda=1; aviso=0\n";
    require(!reduction_verify(spec,runs,"same-program").accepted,"A fixed result plus changing binary flag passed both interventions");
    runs=observations(spec);
    runs[0].stdout_bytes="1.0";runs[1].stdout_bytes="0.0";runs[2].stdout_bytes="2.0";
    require(!reduction_verify(spec,runs,"same-program").accepted,"A fractional spelling was accidentally split into integer evidence");
    const auto rejected=[&](auto change) {
        auto copy=observations(spec);change(copy);
        require(!reduction_verify(spec,copy,"same-program").accepted,"An incomplete/altered reproduction earned credit");
    };
    rejected([](auto& v){v[0].exit_code=1;});
    rejected([](auto& v){v[1].completed_normally=false;});
    rejected([](auto& v){v[0].program_frame="another";});
    rejected([](auto& v){v[0].calls.erase(v[0].calls.begin());});
    rejected([](auto& v){std::swap(v[1].calls[0],v[1].calls[1]);});
    rejected([](auto& v){v[0].calls[0].state_before=1;});
    rejected([](auto& v){v[1].calls[1].state_before=0;});
    rejected([](auto& v){v[2].calls[1].result=2;});
    rejected([](auto& v){v[2].calls[0].delivered=2;});
    rejected([](auto& v){v[2].phase=v[1].phase;});
    rejected([](auto& v){v.pop_back();});
    std::cout<<"PASS ordered same-process observations, full normality and causal integer output\n";
}
void instrumentation() {
    const auto spec=contract();
    const auto source=reference(spec);
    const auto plan=*reduction_plan(spec);
    for(const auto& phase:plan) {
        const auto generated=reduction_instrument(source,spec,phase);
        require(generated&&generated->find("#line 1\n")!=generated->npos&&
                generated->find("lgaed_reduction_before=acumulado")!=generated->npos&&
                generated->find("lgaed_reduction_after=acumulado")!=generated->npos&&
                generated->find(spec.protected_definition.substr(spec.protected_definition.find('{')+1,
                    spec.protected_definition.rfind('}')-spec.protected_definition.find('{')-1))!=generated->npos,
                "Instrumentation replaced the actual protected calculation");
        require(generated->find("#line 1 \"")==generated->npos,
                "Instrumentation supplied a different source-file identity");
    }
    require(!reduction_instrument(source,spec,ReductionPhase{99}),
        "An unplanned intervention was admitted");
    require(!reduction_instrument(replace(source,"++acumulado;","acumulado=0;"),spec,{}),
        "A repaired body entered the private reproduction");
    std::cout<<"PASS protected-body lambda, original source identity and bounded phase plan\n";
}
}
int main() {
    try {
        source_contracts();grammar_and_frames();traces_and_observation();instrumentation();
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"FAIL reduction core: "<<error.what()<<'\n';return 1;
    }
}

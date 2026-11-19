#include "core/simulation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed::curriculum;

void check(bool condition, const char* reason) {
    if (!condition) throw std::runtime_error(reason);
}
template<class F> void rejected(F operation, const char* reason) {
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(reason);
}

std::vector<SimulationValue> integers(std::initializer_list<std::int64_t> values) {
    std::vector<SimulationValue> result;
    for (const auto value : values) result.emplace_back(value);
    return result;
}
std::vector<SimulationValue> words(std::initializer_list<const char*> values) {
    std::vector<SimulationValue> result;
    for (const auto* value : values) result.emplace_back(std::string(value));
    return result;
}

SimulationSpec example() {
    SimulationSpec spec;
    spec.title = "Recorrer valores agrupados";
    spec.description = "Observa el rango y conserva todos los valores que empatan en frecuencia.";
    spec.labels = {"Datos", "Inicio", "Fin", "Frecuencia", "Máxima", "Mayores", "Salida"};
    spec.explanations = {
        "Hay {size} datos y {groups} grupos.",
        "Empieza el grupo {group}: {value}, [{start}, {end}), frecuencia {frequency}.",
        "Avanza hasta {end}; se han visto {frequency} valores {value}.",
        "{value} alcanza {maximum}; ahora permanecen {leaders}.",
        "{value} empata en {maximum}; permanecen {leaders}.",
        "{frequency} es menor que {maximum}; permanecen {leaders}.",
        "Fin={end}; máxima={maximum}; mayores={leaders}; valor={value}.",
        "No hay datos: tamaño {size}, grupos {groups}, máxima {maximum}, valor={value}."
    };
    spec.output_template = "{value}:{frequency}";
    return spec;
}

void runs_and_completion() {
    auto spec = example();
    spec.values = integers({-8, 3, 3, 3, 3, 10});
    const auto trace = build_simulation_trace(spec);
    check(trace.groups == std::vector<SimulationGroup>{{0, 1, std::int64_t{-8}},
        {1, 5, std::int64_t{3}}, {5, 6, std::int64_t{10}}}, "Incorrect half-open groups");
    check(trace.frames.size() == 11 && trace.candidate_frames == std::vector<std::size_t>{2, 7, 9},
        "The progression skipped or invented a step");
    const auto& start = trace.frames[3];
    check(start.phase == SimulationPhase::start && start.begin == 1 && start.end == 2 &&
        start.group == 2 && start.frequency == 1 && start.maximum == 1 &&
        start.leaders == integers({-8}) && start.output == "-8:1\n", "Group start changed completed evidence");
    const auto& advance = trace.frames[6];
    check(advance.phase == SimulationPhase::advance && advance.end == 5 && advance.frequency == 4 &&
        advance.maximum == 1 && advance.leaders == integers({-8}), "Maximum changed before group completion");
    const auto& replacement = trace.frames[7];
    check(replacement.phase == SimulationPhase::new_maximum && replacement.maximum == 4 &&
        replacement.leaders == integers({3}) && replacement.output == "-8:1\n3:4\n", "New maximum was not calculated");
    const auto& smaller = trace.frames[9];
    check(smaller.phase == SimulationPhase::below_maximum && smaller.begin == 5 && smaller.end == 6 &&
        smaller.frequency == 1 && smaller.maximum == 4 && smaller.leaders == integers({3}),
        "Smaller group displaced the maximum");
    const auto& done = trace.frames.back();
    check(done.phase == SimulationPhase::done && done.begin == 6 && done.end == 6 && done.frequency == 0 &&
        done.group == 3 && done.maximum == 4 && done.leaders == integers({3}) &&
        done.output == "-8:1\n3:4\n10:1\n", "Final state or accumulated output is wrong");
    check(done.explanation == "Fin=6; máxima=4; mayores=3; valor=.", "Done dereferenced its past-end position");
    check(trace.frames.front().explanation == "Hay 6 datos y 3 grupos.", "Initial template does not use calculated counts");
}

void ties_and_text() {
    auto spec = example();
    spec.values = integers({-3, -3, 0, 2, 2, 7, 7});
    const auto numbers = build_simulation_trace(spec);
    check(numbers.frames.back().maximum == 2 && numbers.frames.back().leaders == integers({-3, 2, 7}),
        "Negative values or repeated maximum ties were lost");
    check(numbers.frames[numbers.candidate_frames[1]].phase == SimulationPhase::below_maximum,
        "Singleton zero falsely tied with a pair");
    spec.text_values = true;
    spec.values = words({"patio", "sol", "faro", "patio", "sol"});
    const auto text = build_simulation_trace(spec);
    check(text.ordered == words({"faro", "patio", "patio", "sol", "sol"}) && text.original == spec.values,
        "Text sorting modified original data or grouped unequal values");
    check(text.frames.back().leaders == words({"patio", "sol"}) && text.frames.back().maximum == 2 &&
        text.frames.back().output == "faro:1\npatio:2\nsol:2\n", "Text frequencies and ties disagree");
    spec.values = words({"á", "a", "Z", "😀", "Á"});
    check(build_simulation_trace(spec).ordered == words({"Z", "a", "Á", "á", "😀"}),
        "Text sort silently used a locale or folded case/accents");
    spec.values = words({"{size}"});
    check(build_simulation_trace(spec).frames.back().output == "{size}:1\n",
        "A value was evaluated recursively as a template");
}

void counted_observation_changes_are_portable() {
    auto spec=example();
    spec.values=integers({7,3,7,2});
    const auto original=spec;
    const auto trace=build_simulation_trace(spec);
    const auto changes=[&](std::size_t frame) {
        return simulation_count_observation_changes(trace,trace.frames.at(frame));
    };
    check(changes(0).empty()&&changes(2).empty()&&changes(4).empty()&&
          changes(7).empty()&&changes(8).empty(),
        "A decision, initial or terminal state invented a counted observation");
    check(changes(1)==std::vector<std::string>{
        "Dato contado: índice 0 = 2 (origen índice 3)",
        "Recuento de este grupo: fin exclusivo 1 - inicio 0 = 1 elemento contado.",
        "Primera posición: el índice 0 no tiene un elemento anterior."},
        "The first observation changed its sorted index, original index or exclusive count");
    check(changes(3)==std::vector<std::string>{
        "Dato contado: índice 1 = 3 (origen índice 1)",
        "Recuento de este grupo: fin exclusivo 2 - inicio 1 = 1 elemento contado.",
        "Relación con el anterior: índice 0 = 2; índice 1 = 3. Son diferentes: comienza un grupo nuevo.",
        "Procedencia de la comparación: posición ordenada 0 ← posición original 3; posición ordenada 1 ← posición original 1."},
        "A new group failed to explain two already-counted neighbors and their origins");
    check(changes(6)==std::vector<std::string>{
        "Dato contado: índice 3 = 7 (origen índice 2)",
        "Recuento de este grupo: fin exclusivo 4 - inicio 2 = 2 elementos contados.",
        "Relación con el anterior: índice 2 = 7; índice 3 = 7. Son iguales: continúa el grupo.",
        "Procedencia de la comparación: posición ordenada 2 ← posición original 0; posición ordenada 3 ← posición original 2."},
        "Stable duplicate origins or frequency were lost during the group advance");

    auto sorted=example();sorted.values=integers({2,3,7,7});
    const auto ordered=build_simulation_trace(sorted);
    const auto already=simulation_count_observation_changes(ordered,ordered.frames.at(6));
    check(already.size()==3&&already[0]=="Dato contado: índice 3 = 7"&&
          already[1]=="Recuento de este grupo: fin exclusivo 4 - inicio 2 = 2 elementos contados."&&
          already[2].find("Son iguales: continúa el grupo.")!=std::string::npos,
        "An already sorted array invented provenance or lost a real counted duplicate");
    sorted.sort=false;
    const auto unsorted_disabled=build_simulation_trace(sorted);
    check(simulation_count_observation_changes(unsorted_disabled,
            unsorted_disabled.frames.at(6))==already,
        "Disabling sorting changed the visible observation of an already ordered array");

    auto words_spec=example();words_spec.text_values=true;
    words_spec.values=words({"patio","","patio"});
    const auto words_trace=build_simulation_trace(words_spec);
    const auto first_text=simulation_count_observation_changes(words_trace,
        words_trace.frames.at(1));
    check(first_text.size()==3&&
          first_text[0]=="Dato contado: índice 0 = \"\" (cadena vacía) (origen índice 1)"&&
          first_text[1]=="Recuento de este grupo: fin exclusivo 1 - inicio 0 = 1 elemento contado.",
        "An empty text label did not count as one real observed element");
    const auto repeat_text=simulation_count_observation_changes(words_trace,
        words_trace.frames.at(4));
    check(repeat_text.size()==4&&
          repeat_text[0]=="Dato contado: índice 2 = \"patio\" (origen índice 2)"&&
          repeat_text[1]=="Recuento de este grupo: fin exclusivo 3 - inicio 1 = 2 elementos contados."&&
          repeat_text[2].find("\"patio\"; índice 2 = \"patio\"")!=std::string::npos&&
          repeat_text[3].find("posición original 0; posición ordenada 2 ← posición original 2.")!=std::string::npos,
        "Repeated text lost its stable original positions or group-specific frequency");
    words_spec.values=words({" ",""});
    const auto spaces=build_simulation_trace(words_spec);
    const auto distinct=simulation_count_observation_changes(spaces,spaces.frames.at(3));
    check(distinct.size()==4&&
          distinct[0]=="Dato contado: índice 1 = \" \" (1 espacio) (origen índice 0)"&&
          distinct[2].find("\"\" (cadena vacía); índice 1 = \" \" (1 espacio). Son diferentes:")!=std::string::npos,
        "An empty string was conflated with a significant space in a counted comparison");

    auto invalid=trace.frames.at(1);
    invalid.end=0;
    rejected([&]{(void)simulation_count_observation_changes(trace,invalid);},
        "A count with an empty range accessed end-1");
    invalid=trace.frames.at(1);invalid.frequency=0;
    rejected([&]{(void)simulation_count_observation_changes(trace,invalid);},
        "An inaccurate frequency was narrated as a valid range length");
    auto bad_trace=trace;bad_trace.source_indices[0]=trace.original.size();
    rejected([&]{(void)simulation_count_observation_changes(bad_trace,trace.frames.at(1));},
        "A nonexistent source position was accepted as reliable provenance");
    bad_trace=trace;bad_trace.source_indices.pop_back();
    rejected([&]{(void)simulation_count_observation_changes(bad_trace,trace.frames.at(1));},
        "An incomplete source permutation was accepted for a counted observation");
    check(spec==original&&trace==build_simulation_trace(spec),
        "Portable count narration mutated authored data or the calculated trace");
}

void calculated_group_decisions_are_portable() {
    auto spec=example();
    spec.values=integers({-8,3,3,3,3,10});
    const auto authored=spec;
    const auto trace=build_simulation_trace(spec);
    const auto statement=[&](std::size_t frame) {
        return simulation_group_decision_comparison(
            spec,trace.frames.at(frame-1),trace.frames.at(frame));
    };
    check(statement(2)=="Comparación: Frecuencia 1 > Máxima anterior 0. Nuevo máximo: reemplaza candidatos."&&
          statement(7)=="Comparación: Frecuencia 4 > Máxima anterior 1. Nuevo máximo: reemplaza candidatos."&&
          statement(9)=="Comparación: Frecuencia 1 < Máxima anterior 4. No supera el máximo: conserva los candidatos.",
        "The decision narrated the already updated maximum instead of the previous one");
    check(simulation_group_decision_comparison(spec,trace.frames.front(),trace.frames.at(2))==statement(2)&&
          simulation_group_decision_comparison(spec,trace.frames.at(2),trace.frames.at(7))==statement(7)&&
          simulation_group_decision_comparison(spec,trace.frames.at(7),trace.frames.at(9))==statement(9),
        "Grouped presentation compared the wrong completed group's former maximum");
    check(statement(1).empty()&&statement(3).empty()&&
          statement(4).empty()&&statement(10).empty(),
        "A counted observation or terminal frame fabricated a group decision");

    auto tie_spec=example();tie_spec.values=integers({9,4,9,4});
    const auto tie=build_simulation_trace(tie_spec);
    check(tie.candidate_frames==std::vector<std::size_t>{3,6}&&
          simulation_group_decision_comparison(tie_spec,tie.frames.at(5),tie.frames.at(6))==
              "Comparación: Frecuencia 2 = Máxima anterior 2. Empate: agrega el valor a los candidatos."&&
          simulation_group_decision_comparison(tie_spec,tie.frames.at(3),tie.frames.at(6))==
              "Comparación: Frecuencia 2 = Máxima anterior 2. Empate: agrega el valor a los candidatos.",
        "A tie failed to retain both candidates or used the wrong maximum for a grouped jump");
    auto custom=spec;
    custom.labels.frequency="Apariciones";
    custom.labels.maximum="Récord";
    check(simulation_group_decision_comparison(custom,trace.frames.at(6),trace.frames.at(7))==
          "Comparación: Apariciones 4 > Récord anterior 1. Nuevo máximo: reemplaza candidatos.",
        "The portable group decision ignored the authored labels");

    auto invalid=trace.frames.at(7);
    invalid.frequency=1;
    rejected([&]{(void)simulation_group_decision_comparison(spec,trace.frames.at(6),invalid);},
        "A forged decision with inconsistent group frequency was narrated");
    invalid=trace.frames.at(7);invalid.maximum=99;
    rejected([&]{(void)simulation_group_decision_comparison(spec,trace.frames.at(6),invalid);},
        "A decision announcing an unrelated maximum was narrated");
    invalid=trace.frames.at(7);invalid.phase=SimulationPhase::tie;
    rejected([&]{(void)simulation_group_decision_comparison(spec,trace.frames.at(6),invalid);},
        "A fabricated equality with a different prior maximum was narrated");
    check(spec==authored&&trace==build_simulation_trace(spec),
        "Decision explanations modified the authored specification or the calculated trace");
}

void counted_source_positions_and_templates() {
    auto spec = example();
    spec.values = integers({7, 3, 7, 2});
    spec.explanations.initial = "Antes de contar: {countedIndex}|{originIndex}.";
    spec.explanations.start = "Contado {countedIndex}, origen {originIndex}, valor {value}.";
    spec.explanations.advance = "Avanza {countedIndex}, origen {originIndex}, frecuencia {frequency}.";
    spec.explanations.new_maximum = "Decisión sin nueva lectura {countedIndex}|{originIndex}.";
    spec.explanations.done = "Terminado {countedIndex}|{originIndex}.";
    spec.explanations.empty = "Vacío {countedIndex}|{originIndex}.";
    const auto trace = build_simulation_trace(spec);
    check(trace.original == integers({7, 3, 7, 2}) &&
            trace.ordered == integers({2, 3, 7, 7}) &&
            trace.source_indices == std::vector<std::size_t>{3, 1, 0, 2},
        "Stable numeric provenance did not preserve duplicate source positions");
    check(trace.frames.size() == 9 &&
            trace.frames[0].explanation == "Antes de contar: |." &&
            trace.frames[1].explanation == "Contado 0, origen 3, valor 2." &&
            trace.frames[3].explanation == "Contado 1, origen 1, valor 3." &&
            trace.frames[5].explanation == "Contado 2, origen 0, valor 7." &&
            trace.frames[6].explanation == "Avanza 3, origen 2, frecuencia 2." &&
            trace.frames[7].explanation == "Decisión sin nueva lectura |." &&
            trace.frames.back().explanation == "Terminado |.",
        "Counted positions escaped their start/advance phases or mixed up duplicate origins");
    check(build_simulation_trace(spec,integers({2, 7, 2, 7})).source_indices ==
            std::vector<std::size_t>{0, 2, 1, 3} &&
            spec.values == integers({7, 3, 7, 2}),
        "A user-applied dataset changed the authored source or lost stable provenance");

    spec.text_values = true;
    spec.values = words({"patio", "", "patio"});
    const auto text = build_simulation_trace(spec);
    check(text.ordered == words({"", "patio", "patio"}) &&
            text.source_indices == std::vector<std::size_t>{1, 0, 2} &&
            text.frames[1].explanation ==
                "Contado 0, origen 1, valor \"\" (cadena vacía)." &&
            text.frames[3].explanation == "Contado 1, origen 0, valor \"patio\"." &&
            text.frames[4].explanation == "Avanza 2, origen 2, frecuencia 2.",
        "Empty text or repeated UTF-8 labels lost their original positions");
    const auto typed = simulation_parse_input_trace(spec,
        R"(["patio", "", "patio"])", SimulationInputFormat::json);
    check(typed == text && typed.original == spec.values,
        "An edited text list produced different provenance or narration than the authored values");

    spec.sort = false;
    spec.values = words({"", "", "patio"});
    const auto ordered = build_simulation_trace(spec);
    check(ordered.source_indices == std::vector<std::size_t>{0, 1, 2} &&
            ordered.frames[1].explanation ==
                "Contado 0, origen 0, valor \"\" (cadena vacía)." &&
            ordered.frames[2].explanation == "Avanza 1, origen 1, frecuencia 2.",
        "An already ordered dataset needs an identity origin mapping");
    spec.values.clear();
    const auto empty = build_simulation_trace(spec);
    check(empty.source_indices.empty() && empty.ordered.empty() &&
            empty.frames.size() == 1 &&
            empty.frames[0].explanation == "Vacío |.",
        "An empty collection invented a source position or an extra read");
}

void processed_remaining_counts() {
    auto spec = example();
    spec.values = integers({5, 2, 5});
    spec.explanations.initial = "Contados {processedCount}; faltan {remainingCount} de {size}.";
    spec.explanations.start = "Posición {countedIndex}, origen {originIndex}: {processedCount} contados, faltan {remainingCount}.";
    spec.explanations.advance = spec.explanations.start;
    spec.explanations.new_maximum = "{processedCount} procesados; {remainingCount} por contar; máximo {maximum}.";
    spec.explanations.done = "Terminados {processedCount}; quedan {remainingCount}.";
    spec.explanations.empty = "Vacío: {processedCount} procesados y {remainingCount} restantes.";
    // A group output may report totals already observed, unlike the
    // per-element source index, which is restricted to count frames.
    spec.output_template = "{value}:{processedCount}/{remainingCount}";
    const auto authored = spec;
    const auto trace = build_simulation_trace(spec);
    check(trace.ordered == integers({2, 5, 5}) &&
        trace.source_indices == std::vector<std::size_t>{1, 0, 2} &&
        trace.frames.size() == 7, "The progress fixture lost the sorted source");
    check(trace.frames[0].explanation == "Contados 0; faltan 3 de 3." &&
        trace.frames[1].explanation == "Posición 0, origen 1: 1 contados, faltan 2." &&
        trace.frames[2].explanation == "1 procesados; 2 por contar; máximo 1." &&
        trace.frames[3].explanation == "Posición 1, origen 0: 2 contados, faltan 1." &&
        trace.frames[4].explanation == "Posición 2, origen 2: 3 contados, faltan 0." &&
        trace.frames[5].explanation == "3 procesados; 0 por contar; máximo 2." &&
        trace.frames[6].explanation == "Terminados 3; quedan 0.",
        "The number processed or remaining disagreed with the actual observed range");
    check(trace.frames[2].output == "2:1/2\n" &&
        trace.frames[5].output == "2:1/2\n5:3/0\n" &&
        trace.frames.back().output == trace.frames[5].output,
        "The calculated stdout used guessed indices or counted a decision as another element");
    check(spec == authored && build_simulation_trace(spec) == trace,
        "A second progress trace changed the authored source or calculated totals");

    const auto applied = build_simulation_trace(spec, integers({9, 9, 9}));
    check(applied.frames[1].explanation ==
        "Posición 0, origen 0: 1 contados, faltan 2." &&
        applied.frames[3].explanation ==
        "Posición 2, origen 2: 3 contados, faltan 0." &&
        applied.frames.back().output == "9:3/0\n" &&
        spec == authored,
        "Editing a dataset borrowed previous progress or source-index values");

    const auto empty = build_simulation_trace(spec, integers({}));
    check(empty.source_indices.empty() && empty.frames.size() == 1 &&
        empty.frames.front().explanation == "Vacío: 0 procesados y 0 restantes." &&
        empty.frames.front().output.empty(),
        "The zero-element run invented a remaining value or a result line");
    spec.text_values = true;
    spec.values = words({"patio", "", "patio"});
    const auto text = build_simulation_trace(spec);
    check(text.source_indices == std::vector<std::size_t>{1, 0, 2} &&
        text.frames[1].explanation ==
            "Posición 0, origen 1: 1 contados, faltan 2." &&
        text.frames[4].explanation ==
            "Posición 2, origen 2: 3 contados, faltan 0." &&
        text.frames.back().output == ":1/2\npatio:3/0\n",
        "Empty or repeated text values changed processed counts or raw output");
}

void previous_maximum_decision_explanations() {
    auto spec = example();
    // Five groups cover new maximum, equal maximum, another increase,
    // below maximum and a final tie, preserving the original source.
    spec.values = integers({7, 1, 7, 3, 3, 2, 5});
    spec.explanations.initial = "Inicio: {previousMaximum}.";
    spec.explanations.start = "Contar: {previousMaximum}.";
    spec.explanations.advance = "Continuar: {previousMaximum}.";
    spec.explanations.new_maximum = "Antes {previousMaximum}; ahora {maximum}; frecuencia {frequency}.";
    spec.explanations.tie = "Empate {frequency} con {previousMaximum}; máximo {maximum}.";
    spec.explanations.below_maximum = "Menor {frequency} que {previousMaximum}; sigue {maximum}.";
    spec.explanations.done = "Fin: {previousMaximum}.";
    spec.explanations.empty = "Vacío: {previousMaximum}.";
    spec.output_template = "{value}:{frequency}/{previousMaximum}";
    const auto original = spec;
    const auto trace = build_simulation_trace(spec);
    check(trace.ordered == integers({1, 2, 3, 3, 5, 7, 7}) &&
        trace.candidate_frames == std::vector<std::size_t>{2, 4, 7, 9, 12} &&
        trace.frames.size() == 14,
        "The previous-maximum fixture lost a real count or decision");
    check(trace.frames[0].explanation == "Inicio: ." &&
        trace.frames[1].explanation == "Contar: ." &&
        trace.frames[6].explanation == "Continuar: ." &&
        trace.frames[13].explanation == "Fin: .",
        "An unmade decision invented a previous maximum");
    check(trace.frames[2].phase == SimulationPhase::new_maximum &&
        trace.frames[2].explanation == "Antes 0; ahora 1; frecuencia 1." &&
        trace.frames[4].phase == SimulationPhase::tie &&
        trace.frames[4].explanation == "Empate 1 con 1; máximo 1." &&
        trace.frames[7].phase == SimulationPhase::new_maximum &&
        trace.frames[7].explanation == "Antes 1; ahora 2; frecuencia 2." &&
        trace.frames[9].phase == SimulationPhase::below_maximum &&
        trace.frames[9].explanation == "Menor 1 que 2; sigue 2." &&
        trace.frames[12].phase == SimulationPhase::tie &&
        trace.frames[12].explanation == "Empate 2 con 2; máximo 2.",
        "The decision compared against its own updated maximum, not the previous maximum");
    const std::string expected_output = "1:1/0\n2:1/1\n3:2/1\n5:1/2\n7:2/2\n";
    check(trace.frames.back().output == expected_output &&
        trace.frames[7].output == "1:1/0\n2:1/1\n3:2/1\n" &&
        spec == original,
        "Previous-maximum output changed a frequency, leader or the original data");
    const auto empty = build_simulation_trace(spec, integers({}));
    check(empty.frames.size() == 1 &&
        empty.frames[0].explanation == "Vacío: ." &&
        empty.frames[0].output.empty(),
        "An empty collection invented a group decision or previous maximum");

    spec.text_values = true;
    spec.values = words({"patio", "", "patio"});
    const auto labels = build_simulation_trace(spec);
    check(labels.source_indices == std::vector<std::size_t>{1, 0, 2} &&
        labels.frames[2].explanation == "Antes 0; ahora 1; frecuencia 1." &&
        labels.frames[5].explanation == "Antes 1; ahora 2; frecuencia 2." &&
        labels.frames.back().output == ":1/0\npatio:2/1\n",
        "Empty labels or stable text order changed the actual previous maximum");
}

void validated_simulation_session() {
    auto authored = example();
    authored.values = integers({7,3,7,2});
    authored.scenarios = {{"empty","Sin datos",{}},
        {"tie","Repeticiones",integers({4,1,4,1})}};
    const auto original = authored;
    const auto expected = build_simulation_trace(original);
    SimulationSession session(authored);
    check(session.specification()==original,
        "The validated session did not keep its immutable authored contract");
    check(session.take_initial_trace()==expected,
        "Transferring the initial trace changed its calculated groups or original positions");
    rejected([&] { (void)session.take_initial_trace(); },
        "An already transferred initial trace was allowed to be reused");

    // The UI may outlive the caller's original specification. The snapshot
    // retains previously authenticated text, scenarios, and answer-free data.
    authored.values = integers({-9});
    authored.scenarios[0].values = {std::string("incompatible")};
    rejected([&] { validate_simulation_spec(authored); },
        "The independent strict entry point failed to reject a mutated scenario");
    check(session.specification()==original,
        "Mutating the external authoring source changed the validated session");

    for(const auto& edited : {integers({2,9,2}),integers({}),
            integers({3,3,3,3}),integers({4,1,4,1})}){
        const auto strict=build_simulation_trace(original,edited);
        const auto cached=session.build_trace(edited);
        check(cached==strict && cached.original==edited,
            "An edited/scenario trace differs from the strict full-validation path");
    }
    for(const auto* input : {"", "2\n9\n2", "4\r\n1\r\n4\r\n1\r\n", "-2147483648\n2147483647"}){
        check(session.parse_values(input)==simulation_parse_input(original,input) &&
            session.parse_trace(input)==simulation_parse_input_trace(original,input),
            "Parsed editable integers differ between validated and stateless paths");
    }
    for(const auto* invalid : {"\n","2\n\n9","2147483648","1.0","-2147483649"}){
        rejected([&] { (void)session.parse_values(invalid); },
            "The validated session accepted an invalid integer draft");
        rejected([&] { (void)session.parse_trace(invalid); },
            "The validated session accepted an invalid integer trace");
    }
    rejected([&] { (void)session.parse_trace("[]",SimulationInputFormat::json); },
        "A validated numeric session accepted JSON text mode");
    rejected([&] { (void)session.build_trace(words({"2"})); },
        "The validated session accepted a different data type");
    rejected([&] { (void)session.build_trace(integers({2147483648LL})); },
        "The validated session accepted an integer outside 32 bits");

    auto text=example();
    text.text_values=true;
    text.values=words({"patio","","patio"});
    text.scenarios={{"empty","Vacío",{}},
        {"space","Cadena vacía y espacio",words({""," "})}};
    const auto original_text=text;
    SimulationSession words_session(text);
    check(words_session.take_initial_trace()==build_simulation_trace(original_text),
        "A text session lost empty-string grouping or stable UTF-8 provenance");
    text.values=words({"otro"});
    text.scenarios.front().values={std::int64_t{1}};
    rejected([&]{validate_simulation_spec(text);},
        "A forged source mutation was accepted by the strict text contract");
    for(const auto* draft : {"[]",R"([""])",R"(["patio", "", "patio"])",R"(["", " "])"}){
        const auto values=words_session.parse_values(draft,SimulationInputFormat::json);
        check(values==simulation_parse_input(original_text,draft,SimulationInputFormat::json) &&
            words_session.parse_trace(draft,SimulationInputFormat::json)==
                build_simulation_trace(original_text,values),
            "The validated text list lost its empty/space distinctions");
    }
    for(const auto* invalid : {R"(["a",])",R"(["\u0000"])",R"(["\n"])",
            R"(["patio" ""])"}) {
        rejected([&]{(void)words_session.parse_values(invalid,SimulationInputFormat::json);},
            "A reusable text session accepted a forbidden JSON draft");
    }
    rejected([&]{(void)words_session.build_trace(integers({1}));},
        "The reusable text session accepted numeric data");
    auto untrusted=original_text;
    untrusted.scenarios.front().values={std::int64_t{1}};
    rejected([&]{(void)SimulationSession(untrusted);},
        "A session was created from a contract with a malformed future scenario");

    auto sorted=example();
    sorted.sort=false;
    sorted.values=integers({1,1,2});
    sorted.scenarios.clear();
    SimulationSession ordered(sorted);
    check(ordered.take_initial_trace().source_indices==
            std::vector<std::size_t>{0,1,2},
        "A preordered session changed its original positions");
    rejected([&]{(void)ordered.build_trace(integers({2,1}));},
        "A session bypassed the authored sort=false ordering contract");
    rejected([&]{(void)ordered.parse_values("2\n1");},
        "The reusable line parser accepted data out of order");
}

// The reusable native session must behave like the old public APIs for every
// small edited input, not only for a few hand-picked authored scenarios.
// Build one valid immutable model and apply hundreds of independent inputs
// without resetting it, including empty strings and non-ASCII labels.
void exhaustive_cached_session_replay() {
    auto numeric=example();
    numeric.values=integers({-1,0,1});
    numeric.scenarios={{"empty","Sin elementos",{}},
        {"tie","Valores empatados",integers({1,-1,1,-1})}};
    const auto original_numeric=numeric;
    SimulationSession numbers_session(numeric);
    check(numbers_session.take_initial_trace()==build_simulation_trace(original_numeric),
        "A cached numeric session changed the initial authored trace");
    std::size_t numeric_count=0;
    for(std::size_t length=0;length<=5;++length){
        std::size_t combinations=1;
        for(std::size_t digit=0;digit<length;++digit)combinations*=3;
        for(std::size_t pattern=0;pattern<combinations;++pattern){
            std::size_t encoded=pattern;
            std::vector<SimulationValue> values;
            for(std::size_t position=0;position<length;++position){
                values.emplace_back(static_cast<std::int64_t>(encoded%3)-1);
                encoded/=3;
            }
            const auto legacy=build_simulation_trace(original_numeric,values);
            const auto cached=numbers_session.build_trace(values);
            const auto literal=simulation_input_text(values);
            check(cached==legacy&&numbers_session.parse_trace(literal)==legacy&&
                numbers_session.parse_values(literal)==values,
                "A cached numeric session reused a prior edit's calculated frames");
            ++numeric_count;
        }
    }
    check(numeric_count==364&&numbers_session.specification()==original_numeric,
        "The exhaustive numeric cache test skipped inputs or changed its authored source");

    auto text=example();
    text.text_values=true;
    text.values=words({"patio","","patio"});
    text.scenarios={{"empty","Colección vacía",{}},
        {"spaces","Cadena vacía y espacio",words({""," "})}};
    const auto original_text=text;
    SimulationSession strings_session(text);
    check(strings_session.take_initial_trace()==build_simulation_trace(original_text),
        "A cached text session changed the original empty-label grouping");
    constexpr std::array<std::string_view,4> labels{"", " ", "patio", "á"};
    std::size_t text_count=0;
    for(std::size_t length=0;length<=4;++length){
        std::size_t combinations=1;
        for(std::size_t digit=0;digit<length;++digit)combinations*=labels.size();
        for(std::size_t pattern=0;pattern<combinations;++pattern){
            std::size_t encoded=pattern;
            std::vector<SimulationValue> values;
            for(std::size_t position=0;position<length;++position){
                values.emplace_back(std::string(labels[encoded%labels.size()]));
                encoded/=labels.size();
            }
            const auto legacy=build_simulation_trace(original_text,values);
            const auto cached=strings_session.build_trace(values);
            const auto json=simulation_input_text(values,SimulationInputFormat::json);
            check(cached==legacy&&
                strings_session.parse_trace(json,SimulationInputFormat::json)==legacy&&
                strings_session.parse_values(json,SimulationInputFormat::json)==values,
                "A cached text session merged empty labels or retained a prior origin mapping");
            ++text_count;
        }
    }
    check(text_count==341&&strings_session.specification()==original_text,
        "The exhaustive text cache test skipped scenarios or altered its authoring");

    // A rejection must not poison the next valid edit. Also keep the
    // authoring-bound sort=false invariant even after an invalid attempt.
    auto ordered=example();
    ordered.values=integers({-1,0,1});
    ordered.sort=false;
    ordered.scenarios.clear();
    SimulationSession ordered_session(ordered);
    const auto baseline=ordered_session.take_initial_trace();
    rejected([&]{(void)ordered_session.parse_trace("2\n1");},
        "A cached sort=false session accepted unsorted edited input");
    rejected([&]{(void)ordered_session.build_trace(integers({1,0}));},
        "A cached session accepted an unsorted direct trace");
    check(ordered_session.parse_trace("-1\n0\n1")==baseline&&
        ordered_session.build_trace(integers({-1,0,1}))==baseline,
        "Rejecting an edit poisoned the valid original sort=false run");
}

void boundary_states() {
    auto spec = example();
    const auto empty = build_simulation_trace(spec);
    check(empty.ordered.empty() && empty.groups.empty() && empty.candidate_frames.empty() && empty.frames.size() == 1,
        "Empty input fabricated a group");
    check(empty.frames[0].phase == SimulationPhase::empty && empty.frames[0].maximum == 0 &&
        empty.frames[0].leaders.empty() && empty.frames[0].output.empty(), "Empty state fabricated a winner");
    spec.values = integers({0});
    auto trace = build_simulation_trace(spec);
    check(trace.frames.size() == 4 && trace.groups[0].begin == 0 && trace.groups[0].end == 1 &&
        trace.frames.back().leaders == integers({0}), "A single zero was confused with an empty array");
    spec.values = integers({9, 9, 9});
    trace = build_simulation_trace(spec);
    check(trace.groups.size() == 1 && trace.frames.back().maximum == 3 &&
        trace.frames.back().leaders == integers({9}), "All-equal data has a wrong frequency");
    spec.values = integers({1, 2, 3});
    trace = build_simulation_trace(spec);
    check(trace.frames.back().maximum == 1 && trace.frames.back().leaders == spec.values &&
        trace.frames[trace.candidate_frames[1]].phase == SimulationPhase::tie,
        "All-unique values did not all tie");
    spec.values = integers({std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::min(), 0});
    trace = build_simulation_trace(spec);
    check(trace.ordered == integers({std::numeric_limits<std::int32_t>::min(), 0,
        std::numeric_limits<std::int32_t>::max()}), "Signed limits sorted incorrectly");
    for (const auto& frame : trace.frames)
        check(frame.begin <= frame.end && frame.end <= trace.ordered.size() &&
            frame.frequency == frame.end - frame.begin, "An observed range extends beyond the data");
}

void replay_inputs_and_accessibility() {
    auto spec = example();
    spec.values = integers({10, -8, 3, 3, 3, 3});
    const auto original_spec = spec;
    const auto baseline = build_simulation_trace(spec);
    const auto saved = baseline;
    for (std::size_t cursor = baseline.frames.size(); cursor > 0; --cursor)
        check(baseline.frames[cursor - 1] == saved.frames[cursor - 1], "Going backwards changed a prior state");
    check(build_simulation_trace(spec) == saved && spec == original_spec, "Reset/replay changed the source or trace");
    const auto changed = build_simulation_trace(spec, integers({4, 4, 2}));
    check(changed.frames.back().leaders == integers({4}) && changed.frames.back().maximum == 2 &&
        changed.frames.back().output == "2:1\n4:2\n" && spec == original_spec && baseline == saved,
        "Edited input retained old results or changed its authored scenario");
    rejected([&] { (void)build_simulation_trace(spec, words({"4"})); }, "Mixed input type was accepted");
    check(baseline == saved, "Rejected input damaged an existing trace");
    const auto parsed = simulation_parse_input(spec, " -2147483648\r\n+0\r\n2147483647\n");
    check(parsed == integers({std::numeric_limits<std::int32_t>::min(), 0, std::numeric_limits<std::int32_t>::max()}),
        "Complete signed decimal lines were not parsed");
    check(simulation_input_text(parsed) == "-2147483648\n0\n2147483647" &&
        simulation_parse_input(spec, simulation_input_text(parsed)) == parsed,
        "Input formatting does not round trip");
    check(simulation_parse_input(spec, "").empty(), "Empty editor did not make an empty collection");
    for (const auto* invalid : {"2147483648", "-2147483649", "+", "1+2", "0x10", "1 2", "1\n\n", "\n", "1\r2"})
        rejected([&] { (void)simulation_parse_input(spec, invalid); }, "Invalid numeric input was accepted");
    spec.text_values = true;
    spec.values = words({"patio"});
    check(simulation_parse_input(spec, " faro \r\npatio\n") == words({" faro ", "patio"}),
        "Text input lost meaningful spaces or CRLF was stored in a value");
    spec = example();
    spec.values = integers({-8, 3, 3, 3, 3, 10});
    const auto trace = build_simulation_trace(spec);
    const auto accessible = simulation_frame_text(spec, trace, trace.frames.back());
    check(accessible.find("0: -8\n") != accessible.npos && accessible.find("5: 10\n") != accessible.npos &&
        accessible.find("Inicio: 6\nFin: 6\nFrecuencia: 0\nMáxima: 4\nMayores: 3\n") != accessible.npos &&
        accessible.ends_with("Salida:\n-8:1\n3:4\n10:1\n"), "Accessible text differs from the visible calculated state");
    auto invalid_frame = trace.frames.back();
    invalid_frame.end = trace.ordered.size() + 1;
    rejected([&] { (void)simulation_frame_text(spec, trace, invalid_frame); }, "Accessible summary accepted an invalid range");
}

void empty_text_values_and_labels() {
    auto spec = example();
    spec.text_values = true;
    spec.scenarios = {{"none", "Sin elementos", {}}, {"empty", "Una cadena vacía", words({""})},
        {"mixed", "Una vacía y dos iguales", words({"", "patio", "patio"})},
        {"spaces", "Vacía y espacio", words({"", " "})}};
    validate_simulation_spec(spec);
    auto trace = build_simulation_trace(spec);
    check(trace.groups.empty() && trace.frames.size() == 1 && trace.frames.front().maximum == 0 &&
        trace.frames.front().leaders.empty(), "An empty text collection fabricated a group or candidate");
    const auto no_candidate = simulation_frame_text(spec, trace, trace.frames.front());
    check(no_candidate.find("Máxima: 0\nMayores: ninguno\n") != std::string::npos &&
        no_candidate.ends_with("Salida:\n"),
        "An empty collection must have no candidate, not an empty-string winner");
    spec.values = words({""});
    trace = build_simulation_trace(spec);
    check(trace.groups == std::vector<SimulationGroup>{{0, 1, std::string{}}} &&
        trace.frames.size() == 4 && trace.frames.back().maximum == 1 &&
        trace.frames.back().leaders == words({""}) && trace.frames.back().output == ":1\n",
        "A single empty text was confused with an empty collection");
    const auto single_candidate = simulation_frame_text(spec, trace, trace.frames.back());
    check(single_candidate.find("Máxima: 1\nMayores: \"\" (cadena vacía)\n") != std::string::npos &&
        single_candidate.ends_with("Salida:\n:1\n"),
        "An empty-string winner must remain distinct from no candidate");
    check(trace.frames[1].explanation.find("\"\" (cadena vacía)") != std::string::npos &&
        trace.frames.back().explanation.find("mayores=\"\" (cadena vacía)") != std::string::npos,
        "The explanation omitted the empty value or its candidate");
    spec.values = words({"patio", "", "patio"});
    trace = build_simulation_trace(spec);
    check(trace.original == words({"patio", "", "patio"}) && trace.ordered == words({"", "patio", "patio"}) &&
        trace.groups == std::vector<SimulationGroup>{{0, 1, std::string{}}, {1, 3, std::string("patio")}} &&
        trace.candidate_frames == std::vector<std::size_t>{2, 5},
        "Empty text grouping changed source values or half-open boundaries");
    check(trace.frames[2].maximum == 1 && trace.frames[2].leaders == words({""}) &&
        trace.frames[2].output == ":1\n" && trace.frames[5].maximum == 2 &&
        trace.frames[5].leaders == words({"patio"}) && trace.frames[5].output == ":1\npatio:2\n",
        "Candidate snapshots failed to replace the empty winner after the second group");
    spec.values = words({" ", ""});
    trace = build_simulation_trace(spec);
    check(trace.groups == std::vector<SimulationGroup>{{0, 1, std::string{}}, {1, 2, std::string(" ")}} &&
        trace.frames.back().maximum == 1 && trace.frames.back().leaders == words({"", " "}) &&
        trace.frames[4].phase == SimulationPhase::tie && trace.frames.back().output == ":1\n :1\n",
        "Empty text and one space were merged or a presentation label entered the raw output");
    const auto readable = simulation_frame_text(spec, trace, trace.frames.back());
    check(readable.find("0: \"\" (cadena vacía)\n1: \" \" (1 espacio)\n") != std::string::npos &&
        readable.find("Mayores: \"\" (cadena vacía), \" \" (1 espacio)\n") != std::string::npos &&
        readable.ends_with("Salida:\n:1\n :1\n"), "Accessible state cannot distinguish empty text from spaces");
    check(simulation_value_text(std::string{}) == "" &&
        simulation_value_text(std::string(" ")) == " " &&
        simulation_value_display_text(std::string("  ")) == "\"  \" (2 espacios)" &&
        simulation_value_display_text(std::string("patio")) == "\"patio\"" &&
        simulation_value_display_text(std::string("\"\"")) == "\"\\\"\\\"\"" &&
        simulation_value_display_text(std::string("\t")) == "\"\\t\"",
        "Display labels replaced data or confused literal quotes and whitespace");
    spec.output_template = "{leaders}";
    check(build_simulation_trace(spec).frames.back().output == "\n,  \n",
        "An empty first candidate swallowed the separator in raw output");
    spec.sort = false;
    spec.values = words({"", "", " "});
    trace = build_simulation_trace(spec);
    check(trace.frames.back().maximum == 2 && trace.frames.back().leaders == words({""}),
        "Sorted repeated empty text did not retain its frequency");
    spec.scenarios.clear();
    spec.values = words({" ", ""});
    rejected([&] { validate_simulation_spec(spec); }, "Unsorted empty text passed sort=false");
}



void explicit_text_input_presentation_is_lossless() {
    // These are presentation defaults, not a change to accepted input. Data
    // requiring visible escapes must be initially offered in the JSON list,
    // even if the collection has no empty-string element.
    check(!simulation_prefer_quoted_text_input({}) &&
        !simulation_prefer_quoted_text_input(integers({1, 2})) &&
        !simulation_prefer_quoted_text_input(words({"patio", "sol"})) &&
        !simulation_prefer_quoted_text_input(words({" ", "  ", "á", "😀"})),
        "Ordinary text or integer data unexpectedly switched to JSON input");
    check(simulation_prefer_quoted_text_input(words({""})) &&
        simulation_prefer_quoted_text_input(words({"patio", ""})) &&
        simulation_prefer_quoted_text_input(words({"\t"})) &&
        simulation_prefer_quoted_text_input(words({"\""})) &&
        simulation_prefer_quoted_text_input(words({"\\"})) &&
        simulation_prefer_quoted_text_input(words({"\xC2\xA0"})) &&
        simulation_prefer_quoted_text_input(words({"\xE2\x80\x8B"})) &&
        simulation_prefer_quoted_text_input(words({"\xE2\x80\xA8"})) &&
        simulation_prefer_quoted_text_input(words({"\xEF\xB8\x8F"})) &&
        simulation_prefer_quoted_text_input(words({"\xF3\xA0\x84\x80"})),
        "A hidden, escaped or empty text item would open in ambiguous line input");
    const auto original=words({"\xE2\x80\x8B","\xE2\x80\xA8"});
    const auto serialized=simulation_input_text(original,SimulationInputFormat::json);
    auto spec=example();
    spec.text_values=true;
    spec.scenarios.clear();
    spec.values=original;
    check(simulation_prefer_quoted_text_input(original) &&
        serialized==R"(["\u200b", "\u2028"])" &&
        simulation_parse_input(spec,serialized,SimulationInputFormat::json)==original &&
        spec.values==original,
        "The preferred JSON editor format changed invisible source values");
}

void unicode_invisible_labels_remain_distinct_and_reversible() {
    auto spec = example();
    spec.text_values = true;
    spec.scenarios.clear();
    const auto json = SimulationInputFormat::json;
    const std::vector<std::pair<std::string, std::string>> cases{
        {std::string("\x7f"), R"("\u007f")"},
        {std::string("\xC2\x85"), R"("\u0085")"},
        {std::string("\xC2\xA0"), R"("\u00a0")"},
        {std::string("\xC2\xAD"), R"("\u00ad")"},
        {std::string("\xE2\x80\x8B"), R"("\u200b")"},
        {std::string("\xE2\x80\xA8"), R"("\u2028")"},
        {std::string("\xE2\x80\xA9"), R"("\u2029")"},
        {std::string("\xE2\x81\xA0"), R"("\u2060")"},
        {std::string("\xE3\x80\x80"), R"("\u3000")"},
        {std::string("\xEF\xBB\xBF"), R"("\ufeff")"},
        {std::string("\xEF\xB8\x80"), R"("\ufe00")"},
        {std::string("\xEF\xB8\x8F"), R"("\ufe0f")"},
        {std::string("\xF3\xA0\x84\x80"), R"("\udb40\udd00")"},
        {std::string("\xF3\xA0\x87\xAF"), R"("\udb40\uddef")"},
    };
    for (const auto& [raw, label] : cases) {
        spec.values = {raw};
        validate_simulation_spec(spec);
        const auto trace = build_simulation_trace(spec);
        const auto input = simulation_input_text({raw}, json);
        check(simulation_value_display_text(raw) == label &&
            simulation_value_text(raw) == raw &&
            input == "[" + label + "]" &&
            simulation_parse_input(spec, input, json) == std::vector<SimulationValue>{raw} &&
            trace.groups.size() == 1 && trace.groups[0].value == SimulationValue{raw} &&
            trace.frames.back().output == raw + ":1\n" &&
            trace.frames[1].explanation.find(label) != std::string::npos,
            "An invisible Unicode label changed identity, output or its reversible JSON spelling");
    }
    // Even when appended to a visible character, a variation selector must
    // be narrated explicitly rather than altering the text's glyph.
    const std::string selector_bmp = "\xEF\xB8\x8F";
    const std::string selector_supplementary = "\xF3\xA0\x84\x80";
    const auto joined = std::string("A") + selector_bmp + selector_supplementary;
    check(simulation_value_display_text(joined) ==
            R"("A\ufe0f\udb40\udd00")" &&
        simulation_value_text(joined) == joined &&
        simulation_input_text({joined},json) ==
            R"(["A\ufe0f\udb40\udd00"])" &&
        simulation_parse_input(spec, R"(["A\ufe0f\udb40\udd00"])", json) ==
            std::vector<SimulationValue>{joined},
        "A visible character plus Unicode selectors was not reversible");
    const std::string nbspace = "\xC2\xA0";
    const std::string zero_width = "\xE2\x80\x8B";
    const std::string paragraph = "\xE2\x80\xA9";
    spec.values = {std::string{}, std::string{" "}, nbspace, zero_width, paragraph};
    const auto original = spec;
    const auto trace = build_simulation_trace(spec);
    const auto readable = simulation_frame_text(spec, trace, trace.frames.back());
    check(trace.groups.size() == 5 && trace.frames.back().maximum == 1 &&
        trace.frames.back().leaders == spec.values &&
        readable.find(R"("" (cadena vacía))") != std::string::npos &&
        readable.find(R"(" " (1 espacio))") != std::string::npos &&
        readable.find(R"("\u00a0")") != std::string::npos &&
        readable.find(R"("\u200b")") != std::string::npos &&
        readable.find(R"("\u2029")") != std::string::npos &&
        trace.frames.back().output.find(nbspace + ":1\n") != std::string::npos &&
        trace.frames.back().output.find(zero_width + ":1\n") != std::string::npos &&
        trace.frames.back().output.find(paragraph + ":1\n") != std::string::npos &&
        spec == original,
        "Unicode invisible labels became indistinguishable or entered the raw output");
}

void explicit_text_list_input() {
    auto spec = example();
    spec.text_values = true;
    const auto json = SimulationInputFormat::json;
    const auto lines = SimulationInputFormat::lines;
    check(simulation_parse_input(spec, "").empty() &&
        simulation_parse_input(spec, "[]", json).empty() &&
        simulation_parse_input(spec, " [ \r\n\t ] ", json).empty(),
        "An empty collection was not preserved in each explicit input format");
    check(simulation_parse_input(spec, R"([""])", json) == words({""}) &&
        simulation_parse_input(spec, R"(["", "patio", "patio"])", json) == words({"", "patio", "patio"}) &&
        simulation_parse_input(spec, R"(["", " "])", json) == words({"", " "}),
        "Explicit lists confused empty values, spaces or repeated words");
    check(simulation_parse_input(spec, "\"\"\n[\"patio\"]\n ") == words({"\"\"", "[\"patio\"]", " "}),
        "An earlier literal line was automatically reinterpreted as JSON");
    const auto escaped = words({"", " ", "  ", "\t", "\b\f", "\"", "\\", "/", "á", "😀", "{value}"});
    const auto formatted = simulation_input_text(escaped, json);
    check(formatted.starts_with("[\"\", \" \", \"  \", \"\\t\", \"\\b\\f\", \"\\\"\", \"\\\\\"") &&
        simulation_parse_input(spec, formatted, json) == escaped,
        "The explicit text-list serializer lost an empty item, escape or UTF-8 value");
    check(simulation_input_text({}, json) == "[]" && simulation_input_text(words({""}), json) == "[\"\"]" &&
        simulation_input_text(words({"", "patio", "patio"}), json) == "[\"\", \"patio\", \"patio\"]",
        "List serialization does not expose the authored distinction");
    check(simulation_parse_input(spec, R"(["\u0061", "\u00e1", "\uD83D\uDE00", "\/", "\u0020"])", json) ==
        words({"a", "á", "😀", "/", " "}), "JSON Unicode escapes did not decode to the exact text values");
    check(simulation_parse_input(spec, "[\r\n  \"\",\r\n  \"patio\"\r\n]\r\n", json) == words({"", "patio"}),
        "JSON layout whitespace entered the values");
    rejected([&] { (void)simulation_input_text(words({""}), lines); },
        "Lines silently serialized one empty text as no values");
    for (const auto* invalid : {"", " ", "null", "{}", "[1]", "[true]", "[null]", "[[]]",
             "[\"a\",]", "[,\"a\"]", "[\"a\" \"b\"]", "[\"\"]x", "[\"a\"", "[\"a",
             "[\"\\q\"]", "[\"\\u00xy\"]", "[\"\\ud800\"]", "[\"\\udc00\"]",
             "[\"\\ud800\\u0041\"]", "[\"\\u0000\"]", "[\"\\r\"]", "[\"\\n\"]",
             "[\"a\nb\"]", "[\"a\tb\"]"})
        rejected([&] { (void)simulation_parse_input(spec, invalid, json); }, "Malformed or forbidden text list accepted");
    rejected([&] { (void)simulation_parse_input(spec, "\n", lines); },
        "A legacy blank line acquired an ambiguous text value");
    rejected([&] { (void)simulation_parse_input(spec, "[\"\xc0\x80\"]", json); }, "Invalid UTF-8 list accepted");
    rejected([&] { (void)simulation_parse_input(spec, "[\"" + std::string(129, 'x') + "\"]", json); },
        "A list exceeded the decoded text-byte limit");
    std::string multibyte;
    for (int i = 0; i < 64; ++i) multibyte += "á";
    check(simulation_parse_input(spec, "[\"" + multibyte + "\"]", json) ==
        std::vector<SimulationValue>{multibyte}, "A 128-byte UTF-8 text was rejected");
    rejected([&] { (void)simulation_parse_input(spec, "[\"" + multibyte + "á\"]", json); },
        "The UTF-8 limit was counted as characters");
    std::string maximum = "[", excessive = "[";
    for (int i = 0; i < 65; ++i) {
        if (i) excessive += ", ";
        excessive += "\"\"";
        if (i < 64) { if (i) maximum += ", "; maximum += "\"\""; }
    }
    maximum += "]"; excessive += "]";
    check(simulation_parse_input(spec, maximum, json).size() == 64, "The maximum number of empty values was rejected");
    rejected([&] { (void)simulation_parse_input(spec, excessive, json); }, "A sixty-fifth empty item was accepted");
    rejected([&] { (void)simulation_parse_input(spec, std::string(65537, ' '), json); },
        "The encoded list had no total input bound");
    spec.sort = false;
    check(simulation_parse_input(spec, R"(["", " "])", json) == words({"", " "}), "Sorted empty values were rejected");
    rejected([&] { (void)simulation_parse_input(spec, R"([" ", ""])", json); },
        "JSON editing bypassed the ascending-order contract");
    rejected([&] { (void)simulation_parse_input(spec, "[]", static_cast<SimulationInputFormat>(50)); },
        "Unknown input format was accepted");
    spec = example();
    rejected([&] { (void)simulation_parse_input(spec, "[]", json); }, "JSON mode changed the integer editor contract");
    rejected([&] { (void)simulation_input_text(integers({1}), json); }, "Integer values were serialized into a text list");
}

void contract_rejections() {
    const auto good = example();
    const auto invalid = [&](auto edit, const char* reason) {
        auto spec = good;
        edit(spec);
        rejected([&] { validate_simulation_spec(spec); }, reason);
    };
    invalid([](auto& s) { s.version = 2; }, "Unknown version accepted");
    invalid([](auto& s) { s.model = "cppInterpreter"; }, "Unknown model accepted");
    invalid([](auto& s) { s.view = static_cast<SimulationViewMode>(80); }, "Unknown view accepted");
    invalid([](auto& s) { s.title.assign(121, 'x'); }, "Long title accepted");
    invalid([](auto& s) { s.description.assign(4001, 'x'); }, "Long description accepted");
    invalid([](auto& s) { s.labels.array.assign(81, 'x'); }, "Long label accepted");
    invalid([](auto& s) { s.labels.array = "\xE3\x80\x80"; }, "Unicode-whitespace label accepted");
    invalid([](auto& s) { s.labels.end = ""; }, "Missing label accepted");
    invalid([](auto& s) { s.explanations.empty.assign(2001, 'x'); }, "Long explanation accepted");
    invalid([](auto& s) { s.explanations.tie = ""; }, "Missing explanation accepted");
    invalid([](auto& s) { s.output_template.assign(257, 'x'); }, "Long output template accepted");
    for (const auto* broken : {"{", "}", "{}", "{unknown}", "{{value}}", "{value}}", "{maximum+1}"}) {
        invalid([&](auto& s) { s.explanations.initial = broken; }, "Invalid explanation marker accepted");
        invalid([&](auto& s) { s.output_template = broken; }, "Invalid output marker accepted");
    }
    for(const auto* counted : {"{countedIndex}", "{originIndex}", "{value}:{originIndex}"}) {
        invalid([&](auto& s) { s.output_template = counted; },
            "A completed-group output accepted the position of an element it did not read");
        auto explanatory = good;
        explanatory.explanations.start = counted;
        explanatory.explanations.advance = counted;
        validate_simulation_spec(explanatory);
    }
    invalid([](auto& s) { s.values.assign(65, std::int64_t{0}); }, "More than 64 values accepted");
    invalid([](auto& s) { s.values = {std::int64_t{0}, std::string("0")}; }, "Mixed types accepted");
    invalid([](auto& s) { s.values = {std::numeric_limits<std::int64_t>::max()}; }, "Oversized integer accepted");
    invalid([](auto& s) { s.sort = false; s.values = integers({2, 1}); }, "Unsorted input accepted with sort disabled");
    for (const auto& broken : {std::string(129, 'x'), std::string("x\ny"), std::string("x\ry"),
             std::string("x\0y", 3), std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
             std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x82", 2)})
        invalid([&](auto& s) { s.text_values = true; s.values = {broken}; }, "Invalid UTF-8 text value accepted");
    invalid([](auto& s) { s.title = std::string("a\0b", 3); }, "NUL in public text accepted");
    invalid([](auto& s) { s.scenarios = {{"again", "Uno", {}}, {"again", "Dos", {}}}; }, "Duplicate scenario accepted");
    invalid([](auto& s) { s.scenarios = {{"1wrong", "Uno", {}}}; }, "Invalid scenario identity accepted");
    invalid([](auto& s) { s.scenarios = {{"okay", " ", {}}}; }, "Blank scenario label accepted");
    invalid([](auto& s) { s.scenarios = {{"okay", "Uno", words({"a"})}}; }, "Wrong scenario type accepted");
    invalid([](auto& s) { s.sort = false; s.scenarios = {{"okay", "Uno", integers({2, 1})}}; },
        "Unsorted authored scenario accepted with sort disabled");
    invalid([](auto& s) { for (int i = 0; i < 9; ++i) s.scenarios.push_back({"case_" + std::to_string(i), "Caso", {}}); },
        "More than eight scenarios accepted");
    auto sorted = good;
    sorted.sort = false;
    sorted.values = integers({-1, -1, 2});
    check(build_simulation_trace(sorted).ordered == sorted.values, "Duplicate sorted input was rejected");
    rejected([&] { (void)simulation_parse_input(sorted, "2\n1"); }, "Edited unsorted input was accepted");
}

// Exhaust every array of length 0..6 over {-1, 0, 1}. The oracle uses
// independently sorted values and counts, not the engine's group/trace code.
void exhaustive_small_arrays() {
    auto spec=example();
    for(std::size_t size=0;size<=6;++size){
        std::size_t combinations=1;
        for(std::size_t i=0;i<size;++i)combinations*=3;
        for(std::size_t pattern=0;pattern<combinations;++pattern){
            const auto verify=[&](bool condition,const char* reason){
                if(!condition)throw std::runtime_error(
                    "Exhaustive simulation length="+std::to_string(size)+
                    " pattern="+std::to_string(pattern)+": "+reason);
            };
            std::vector<std::int64_t> original;
            std::size_t digits=pattern;
            for(std::size_t i=0;i<size;++i){
                original.push_back(static_cast<std::int64_t>(digits%3)-1);
                digits/=3;
            }
            spec.values.clear();
            for(const auto value:original)spec.values.emplace_back(value);
            const auto trace=build_simulation_trace(spec);
            auto ordered=original;
            std::sort(ordered.begin(),ordered.end());
            std::vector<SimulationValue> expected_values;
            for(const auto value:ordered)expected_values.emplace_back(value);
            verify(trace.original==spec.values&&trace.ordered==expected_values,
                "Original data or ordered copy changed");
            // Independently match each ordered value to its first unused
            // source occurrence. This oracle is deliberately not a second
            // stable_sort over the same data.
            std::vector<std::size_t> expected_sources;
            std::vector<bool> source_used(original.size(),false);
            for(const auto& value:ordered){
                bool found=false;
                for(std::size_t source=0;source<original.size();++source){
                    if(source_used[source]||original[source]!=value)continue;
                    source_used[source]=true;
                    expected_sources.push_back(source);
                    found=true;
                    break;
                }
                verify(found,"Stable source index could not be recovered");
            }
            verify(trace.source_indices==expected_sources,
                "The counted-element origin mapping changed for a repeated number");
            // An explicitly preordered lesson must produce the same group
            // decisions without sorting or rewriting any authored element.
            auto preordered=spec;
            preordered.sort=false;
            preordered.values=expected_values;
            const auto direct=build_simulation_trace(preordered);
            verify(direct.original==expected_values&&direct.ordered==expected_values&&
                direct.groups==trace.groups&&direct.frames==trace.frames&&
                direct.candidate_frames==trace.candidate_frames,
                "sort=false changed a validated ordered input or its decisions");
            if(spec.values!=expected_values){
                auto unsorted_spec=spec;
                unsorted_spec.sort=false;
                rejected([&]{(void)build_simulation_trace(unsorted_spec);},
                    "sort=false silently accepted unsorted authored data");
                std::string unsorted_lines;
                for(const auto& item:spec.values){
                    if(!unsorted_lines.empty())unsorted_lines+='\n';
                    unsorted_lines+=simulation_value_text(item);
                }
                rejected([&]{(void)simulation_parse_input_trace(preordered,unsorted_lines);},
                    "sort=false silently accepted an unsorted editor draft");
            }
            if(ordered.empty()){
                verify(trace.groups.empty()&&trace.candidate_frames.empty()&&
                    trace.frames.size()==1&&
                    trace.frames.front().phase==SimulationPhase::empty&&
                    trace.frames.front().output.empty()&&trace.frames.front().leaders.empty(),
                    "An empty input produced an invented step or candidate");
                continue;
            }
            verify(trace.frames.front().phase==SimulationPhase::initial&&
                trace.frames.front().maximum==0&&trace.frames.front().output.empty()&&
                trace.frames.front().leaders.empty(),
                "Initial frame exposed a computed result");
            std::size_t frame_index=1,group_index=0,maximum=0;
            std::vector<SimulationValue> leaders;
            std::string expected_output;
            for(std::size_t begin=0;begin<ordered.size();){
                std::size_t end=begin+1;
                while(end<ordered.size()&&ordered[end]==ordered[begin])++end;
                const auto frequency=end-begin;
                verify(group_index<trace.groups.size()&&
                    trace.groups[group_index].begin==begin&&
                    trace.groups[group_index].end==end&&
                    trace.groups[group_index].value==SimulationValue{ordered[begin]},
                    "A group has an incorrect half-open range or value");
                for(std::size_t counted=1;counted<=frequency;++counted){
                    verify(frame_index<trace.frames.size(),"Missing an observed-element frame");
                    const auto& frame=trace.frames[frame_index++];
                    verify(frame.phase==(counted==1?SimulationPhase::start:SimulationPhase::advance)&&
                        frame.group==group_index+1&&frame.begin==begin&&frame.end==begin+counted&&
                        frame.frequency==counted&&frame.maximum==maximum&&
                        frame.leaders==leaders&&frame.output==expected_output,
                        "Counting a value skipped a step or exposed a future decision");
                }
                const auto decision_position=frame_index;
                verify(frame_index<trace.frames.size(),"Missing group decision");
                const auto& decision=trace.frames[frame_index++];
                const auto phase=frequency>maximum?SimulationPhase::new_maximum:
                    frequency==maximum?SimulationPhase::tie:SimulationPhase::below_maximum;
                if(frequency>maximum){
                    maximum=frequency;
                    leaders={SimulationValue{ordered[begin]}};
                }else if(frequency==maximum)leaders.emplace_back(ordered[begin]);
                expected_output+=std::to_string(ordered[begin])+":"+
                    std::to_string(frequency)+"\n";
                verify(decision.phase==phase&&decision.group==group_index+1&&
                    decision.begin==begin&&decision.end==end&&
                    decision.frequency==frequency&&decision.maximum==maximum&&
                    decision.leaders==leaders&&decision.output==expected_output&&
                    group_index<trace.candidate_frames.size()&&
                    trace.candidate_frames[group_index]==decision_position,
                    "Decision frame changed the maximum, tie, positions or accumulated output");
                begin=end;++group_index;
            }
            verify(group_index==trace.groups.size()&&group_index==trace.candidate_frames.size()&&
                frame_index+1==trace.frames.size(),
                "An extra or missing step changed the finished trace");
            const auto& done=trace.frames.back();
            verify(done.phase==SimulationPhase::done&&
                done.begin==size&&done.end==size&&done.frequency==0&&
                done.maximum==maximum&&done.leaders==leaders&&
                done.output==expected_output,
                "Final step changed the observed result or read beyond the array");
        }
    }
}

// Exhaust text arrays using the explicitly documented UTF-8 byte order.
// Rank is determined by the fixture, not by the engine comparator.
void exhaustive_small_text_arrays() {
    constexpr std::array<std::string_view,4> labels{"", " ", "patio", "á"};
    auto spec=example();spec.text_values=true;
    for(std::size_t size=0;size<=4;++size) {
        std::size_t combinations=1;
        for(std::size_t i=0;i<size;++i)combinations*=labels.size();
        for(std::size_t pattern=0;pattern<combinations;++pattern) {
            const auto verify=[&](bool condition,const char* reason){
                if(!condition)throw std::runtime_error(
                    "Exhaustive text simulation length="+std::to_string(size)+
                    " pattern="+std::to_string(pattern)+": "+reason);
            };
            std::array<std::size_t,labels.size()> counts{};
            spec.values.clear();
            auto encoded=pattern;
            for(std::size_t i=0;i<size;++i) {
                const auto index=encoded%labels.size();
                encoded/=labels.size();
                spec.values.emplace_back(std::string(labels[index]));
                ++counts[index];
            }
            const auto trace=build_simulation_trace(spec);
            std::vector<SimulationValue> expected_order;
            for(std::size_t i=0;i<labels.size();++i)
                for(std::size_t count=0;count<counts[i];++count)
                    expected_order.emplace_back(std::string(labels[i]));
            verify(trace.original==spec.values&&trace.ordered==expected_order,
                "UTF-8 byte order or source data was lost");
            std::vector<bool> source_used(spec.values.size(),false);
            std::vector<std::size_t> expected_sources;
            for(const auto& value:expected_order){
                bool found=false;
                for(std::size_t source=0;source<spec.values.size();++source){
                    if(source_used[source]||spec.values[source]!=value)continue;
                    source_used[source]=true;
                    expected_sources.push_back(source);
                    found=true;
                    break;
                }
                verify(found,"A repeated text value has no remaining source owner");
            }
            verify(trace.source_indices==expected_sources,
                "The empty, space or UTF-8 text origin was not stable");
            auto preordered=spec;
            preordered.sort=false;
            preordered.values=expected_order;
            const auto direct=build_simulation_trace(preordered);
            verify(direct.original==expected_order&&direct.ordered==expected_order&&
                direct.groups==trace.groups&&direct.frames==trace.frames&&
                direct.candidate_frames==trace.candidate_frames,
                "sort=false changed text grouping, an empty label or a decision");
            if(spec.values!=expected_order){
                auto unsorted_spec=spec;
                unsorted_spec.sort=false;
                rejected([&]{(void)build_simulation_trace(unsorted_spec);},
                    "sort=false accepted unsorted text authored data");
                const auto draft=simulation_input_text(spec.values,SimulationInputFormat::json);
                rejected([&]{(void)simulation_parse_input_trace(
                    preordered,draft,SimulationInputFormat::json);},
                    "sort=false accepted an unsorted text editor draft");
            }
            std::size_t maximum=0,position=0,group_index=0;
            std::vector<SimulationValue> leaders;
            std::string output;
            for(std::size_t i=0;i<labels.size();++i) {
                if(counts[i]==0)continue;
                const auto value=SimulationValue{std::string(labels[i])};
                verify(group_index<trace.groups.size()&&
                    trace.groups[group_index].begin==position&&
                    trace.groups[group_index].end==position+counts[i]&&
                    trace.groups[group_index].value==value,
                    "Empty, space-only and ordinary text groups were merged");
                const auto phase=counts[i]>maximum?SimulationPhase::new_maximum:
                    counts[i]==maximum?SimulationPhase::tie:SimulationPhase::below_maximum;
                if(counts[i]>maximum) {
                    maximum=counts[i];leaders={value};
                }else if(counts[i]==maximum)leaders.push_back(value);
                output+=std::string(labels[i])+":"+std::to_string(counts[i])+"\n";
                verify(group_index<trace.candidate_frames.size(),
                    "The text group's completed decision is missing");
                const auto& decision=trace.frames.at(trace.candidate_frames[group_index]);
                verify(decision.phase==phase&&decision.maximum==maximum&&
                    decision.leaders==leaders&&decision.output==output&&
                    decision.frequency==counts[i]&&decision.group==group_index+1,
                    "An empty label or an equal-frequency text candidate changed the decision");
                position+=counts[i];++group_index;
            }
            verify(group_index==trace.groups.size()&&
                group_index==trace.candidate_frames.size(),
                "A text group was duplicated or omitted");
            const auto& terminal=trace.frames.back();
            if(size==0)verify(terminal.phase==SimulationPhase::empty&&terminal.output.empty()&&
                    terminal.leaders.empty(),"Empty text array fabricated a group");
            else verify(terminal.phase==SimulationPhase::done&&
                    terminal.maximum==maximum&&terminal.leaders==leaders&&
                    terminal.output==output&&terminal.end==size,
                    "Text output, candidates or final range changed");
        }
    }
}

void parsed_trace_reuse() {
    auto spec=example();
    spec.values=integers({7,1,7});
    spec.scenarios={{"tie","Dos iguales",integers({4,4,2,2})},
                    {"empty","Sin elementos",{}}};
    const auto original=spec;
    for(const auto* input:{"", "5\n3\n5", "-2147483648\n0\n2147483647"}) {
        auto trace=simulation_parse_input_trace(spec,input);
        check(trace.original==simulation_parse_input(spec,input)&&
            trace==build_simulation_trace(spec,trace.original),
            "Parsed integer trace is not equivalent to a separately validated replay");
    }
    rejected([&]{(void)simulation_parse_input_trace(spec,"2\n\n4");},
        "Trace input accepted an empty integer line");
    rejected([&]{(void)simulation_parse_input_trace(spec,"2147483648");},
        "Trace input accepted an overflowing integer");
    check(spec==original,"Parsed integer input modified authored data or scenarios");

    spec=example();spec.text_values=true;
    spec.values=words({"patio",""});
    spec.scenarios={{"zero","Cadena vacía",words({""})},
                    {"spaces","Espacio significativo",words({" "})}};
    const auto authored_text=spec;
    const auto format=SimulationInputFormat::json;
    for(const auto* input:{"[]", "[\"\"]", "[\"\", \" \"]",
                           "[\"\", \"patio\", \"patio\"]", "[\"\\u00e1\", \"a\"]"}) {
        auto trace=simulation_parse_input_trace(spec,input,format);
        check(trace.original==simulation_parse_input(spec,input,format)&&
            trace==build_simulation_trace(spec,trace.original),
            "Parsed text trace changed Unicode, empty labels or output");
    }
    const auto empty=simulation_parse_input_trace(spec,"[\"\", \" \"]",format);
    check(empty.frames.back().leaders==words({""," "})&&
        empty.frames.back().maximum==1&&empty.frames.back().output==":1\n :1\n",
        "Reused trace failed to distinguish the empty label from a space");
    rejected([&]{(void)simulation_parse_input_trace(spec,"[\"\",]",format);},
        "Parsed trace accepted a malformed text list");
    const auto bracketed=simulation_parse_input_trace(
        spec,R"(["a", "a"])",SimulationInputFormat::lines);
    check(bracketed.original==words({R"(["a", "a"])"})&&
        bracketed.frames.back().output==R"(["a", "a"]:1)" "\n",
        "Ordinary text lines were interpreted as a JSON list instead of a literal");
    check(spec==authored_text,"Parsed text input modified authored data or scenarios");
}

void expansion_bounds_and_modes() {
    auto spec = example();
    spec.values = integers({2, 1, 2});
    const auto expected = build_simulation_trace(spec);
    for (const auto mode : {SimulationViewMode::array, SimulationViewMode::groups, SimulationViewMode::walkthrough,
             SimulationViewMode::candidates, SimulationViewMode::summary}) {
        spec.view = mode;
        check(build_simulation_trace(spec) == expected, "Presentation mode changed the calculated data");
    }
    spec = example();
    spec.text_values = true;
    std::string repeated;
    while (repeated.size() + 9 <= 2000) repeated += "{leaders}";
    spec.explanations.new_maximum = spec.explanations.tie = spec.explanations.done = repeated;
    std::vector<SimulationValue> huge;
    for (int i = 0; i < 64; ++i) {
        auto word = std::to_string(i);
        word.append(128 - word.size(), 'x');
        huge.emplace_back(std::move(word));
    }
    const auto original = spec;
    spec.values = huge;
    rejected([&] { validate_simulation_spec(spec); }, "Authored expansion above 8 MiB was accepted");
    spec = original;
    spec.scenarios = {{"long", "Muchas palabras", huge}};
    rejected([&] { validate_simulation_spec(spec); }, "Oversized scenario expansion escaped load-time validation");
    spec = original;
    validate_simulation_spec(spec);
    rejected([&] { (void)build_simulation_trace(spec, huge); }, "Oversized edited expansion was accepted");
    const auto encoded=simulation_input_text(huge,SimulationInputFormat::json);
    rejected([&] { (void)simulation_parse_input_trace(spec,encoded,SimulationInputFormat::json); },
        "Reusable parsed trace bypassed the 8 MiB expansion limit");
    // A cached/validated native session must retain the SAME bound when
    // applying data or merely converting a pending draft to another format.
    // Skipping unchanged authored-scenario revalidation may never skip the
    // new edited trace's 8 MiB budget or accept a half-updated view.
    SimulationSession session(spec);
    const auto initial=session.take_initial_trace();
    check(initial==build_simulation_trace(original),
        "A cached session changed the initial large-template contract");
    rejected([&] { (void)session.build_trace(huge); },
        "A cached session bypassed the 8 MiB bound on edited data");
    rejected([&] { (void)session.parse_trace(encoded,SimulationInputFormat::json); },
        "A cached session bypassed the 8 MiB bound on applied input");
    rejected([&] { (void)session.parse_values(encoded,SimulationInputFormat::json); },
        "Format conversion accepted a draft whose resulting trace exceeds 8 MiB");
    check(session.specification()==original&&spec==original,
        "Rejecting a large draft changed validated authoring or source state");
}
} // namespace

int main() {
    try {
        runs_and_completion();
        ties_and_text();
        counted_observation_changes_are_portable();
        calculated_group_decisions_are_portable();
        counted_source_positions_and_templates();
        processed_remaining_counts();
        previous_maximum_decision_explanations();
        validated_simulation_session();
        exhaustive_cached_session_replay();
        boundary_states();
        replay_inputs_and_accessibility();
        empty_text_values_and_labels();
        unicode_invisible_labels_remain_distinct_and_reversible();
        explicit_text_input_presentation_is_lossless();
        explicit_text_list_input();
        contract_rejections();
        expansion_bounds_and_modes();
        parsed_trace_reuse();
        exhaustive_small_arrays();
        exhaustive_small_text_arrays();
        std::cout << "PASS native simulation: calculated groups, ranges, frequencies, maxima/ties, text order, "
            "edited inputs, exact replay, accessible state, strict contracts and bounded expansion\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL native simulation: " << error.what() << '\n';
        return 1;
    }
}

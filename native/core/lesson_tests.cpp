#include "core/lesson.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "core/compiler_revision_test_fixture.hpp"
#include "core/compile_check_test_fixture.hpp"
#include "core/math_label_test_fixture.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace lgaed::curriculum;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action&& action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
void replace_once(std::string& text, std::string_view old_value, std::string_view new_value) {
    const auto index = text.find(old_value); check(index != text.npos, "Test fixture target missing");
    text.replace(index, old_value.size(), new_value);
}
void independently_movable_order_lines_preserve_the_original_contract() {
    using namespace winrt::Windows::Data::Json;
    using lgaed::windows::parse_lesson_json;
    const auto directory = std::filesystem::path(lgaed::build::cpp_tool_host).parent_path().parent_path()/"test-fixtures/order-lines";
    const auto bytes = lgaed::state::read_bytes(directory/"manifest.json");
    const auto manifest = JsonObject::Parse(winrt::to_hstring(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())));
    check(manifest.GetNamedString(L"baseline") == L"aa230c6575210c2154e59dee60ff87cdeb8e615b", "Ordering fixtures lost their immutable published source");
    const auto entries = manifest.GetNamedArray(L"entries");
    check(entries.Size() == 14, "Ordering fixtures omitted an affected activity");
    std::size_t original_count = 0, line_count = 0, empty_lines = 0, identical_closings = 0, historical_grouped = 0;
    const auto escapes = [](const std::vector<std::string>& values) {
        std::size_t count = 0;
        for (const auto& text : values) for (std::size_t at = 0; (at = text.find("\\n",at)) != text.npos; at += 2) ++count;
        return count;
    };
    for (const auto& value : entries) {
        const auto entry = value.GetObject();
        const auto old_path = directory/winrt::to_string(entry.GetNamedString(L"old"));
        const auto current_path = directory/winrt::to_string(entry.GetNamedString(L"refined"));
        check(lgaed::state::sha256(lgaed::state::read_bytes(old_path)) == winrt::to_string(entry.GetNamedString(L"oldSha256")),
            "Ordering migration fixture differs from its pinned Git bytes");
        const auto before = lgaed::windows::load_lesson(old_path);
        const auto current = lgaed::windows::load_lesson(current_path);
        const auto metadata = lgaed::windows::load_lesson_progress_metadata(current_path);
        const auto stable_id = winrt::to_string(entry.GetNamedString(L"stableId"));
        const auto position = stable_block_position(current,stable_id);
        check(position.has_value(), "Refined ordering lost its historical identity");
        const auto& block = current.pages[position->page].blocks[position->block];
        check(block.order_refinement.has_value(), "Ordering provenance did not reach the native block model");
        const auto& refinement = *block.order_refinement;
        const auto old_position = stable_block_position(before,stable_id);
        const auto& old = old_position ? before.pages[old_position->page].blocks[old_position->block]
            : before.pages[block.address.page].blocks[block.address.block];
        check(old.fragments == refinement.original_fragments && old.correct_order == refinement.original_correct_order &&
            exercise_key(before,old) == exercise_key(current,block), "Refining an ordering changed its original answer or response address");
        const auto projected = stable_block_position(metadata,stable_id);
        check(projected && metadata.pages[projected->page].blocks[projected->block].order_refinement &&
            metadata.pages[projected->page].blocks[projected->block].fragments == block.fragments &&
            exercise_key(metadata,metadata.pages[projected->page].blocks[projected->block]) == exercise_key(current,block),
            "Catalog progress lost the refined representation or historical response address");
        check(refinement.groups.size() == old.fragments.size() && refinement.original_fragments.size() == static_cast<std::size_t>(entry.GetNamedNumber(L"oldPieces")) &&
            block.fragments.size() == static_cast<std::size_t>(entry.GetNamedNumber(L"newPieces")), "Refinement omitted an original group or real line");
        check(refinement.representation_signature == lgaed::state::sha256(order_representation_frame(block)),
            "Refined composition identity differs from the original fragment representation frame");
        std::vector<std::size_t> expanded;
        for (const auto group : old.correct_order) expanded.insert(expanded.end(),refinement.groups[group].begin(),refinement.groups[group].end());
        check(expanded == block.correct_order && evaluate_order(block,expanded).correct, "Refining groups changed the canonical reconstructed program");
        check(escapes(old.fragments) == escapes(block.fragments), "A written newline escape was split into another movable line");
        for (const auto& line : block.fragments)
            check(line.find('\n') == line.npos && line.find_first_not_of(" \t\r\f\v") != line.npos,
                "A grouped or decorative empty fragment remained an evaluated card");
        for (const auto& fragment : old.fragments) {
            for (std::size_t start = 0; start < fragment.size();) {
                const auto end = fragment.find('\n',start);
                const auto line = std::string_view(fragment).substr(start,(end == fragment.npos ? fragment.size() : end)-start);
                if (line.find_first_not_of(" \t\r\f\v") == line.npos) ++empty_lines;
                if (end == fragment.npos) break;
                start = end + 1;
            }
        }
        for (const auto& history : current.state_layout.retirement_history) for (const auto& page : history.pages) for (const auto& archived : page.blocks)
            if (archived.stable_id == stable_id && archived.fragments == old.fragments) {
                check(!archived.order_refinement && archived.correct_order == old.correct_order,
                    "An older reading snapshot was silently changed from groups to lines");
                ++historical_grouped;
            }
        auto duplicate = expanded; duplicate.back() = duplicate.front();
        check(!evaluate_order(block,duplicate).correct, "A repeated physical card was accepted because its text matched");
        auto missing = expanded; missing.pop_back();
        check(!evaluate_order(block,missing).correct, "An omitted line completed the ordering");
        auto outside = expanded; outside.back() = block.fragments.size();
        check(!evaluate_order(block,outside).correct, "An out-of-range line completed the ordering");
        bool checked_closings = false;
        for (std::size_t left = 0; left < expanded.size() && !checked_closings; ++left)
            for (std::size_t right = left + 1; right < expanded.size(); ++right)
                if (block.fragments[expanded[left]] == "}" && block.fragments[expanded[left]] == block.fragments[expanded[right]]) {
                    auto equivalent = expanded; std::swap(equivalent[left],equivalent[right]);
                    check(evaluate_order(block,equivalent).correct, "Identical closing lines were treated as different answers");
                    auto different = block; different.fragments[equivalent[left]] = " }";
                    check(!evaluate_order(different,equivalent).correct, "Ordering equivalence normalized a changed indentation");
                    ++identical_closings; checked_closings = true; break;
                }
        if (original_count == 0) {
            for (const auto change : {"source","order","id","duplicate","snapshot","snapshot-prose","proof","origin","extra","missing","empty","type"}) {
                const auto root = JsonObject::Parse(winrt::to_hstring(current.raw_json));
                const auto puzzle = root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
                const auto layout = root.GetNamedObject(L"stateLayout");
                const auto records = layout.GetNamedArray(L"orderRefinements");
                const auto record = records.GetAt(0).GetObject();
                if (std::string_view(change) == "source") puzzle.GetNamedArray(L"fragments").SetAt(0,JsonValue::CreateStringValue(L"changed source"));
                else if (std::string_view(change) == "order") {const auto order = puzzle.GetNamedArray(L"correctOrder");const auto first = order.GetAt(0);order.SetAt(0,order.GetAt(1));order.SetAt(1,first);}
                else if (std::string_view(change) == "id") record.Insert(L"id",JsonValue::CreateStringValue(L"missing"));
                else if (std::string_view(change) == "duplicate") records.Append(JsonObject::Parse(record.Stringify()));
                else if (std::string_view(change) == "snapshot") record.GetNamedObject(L"block").GetNamedArray(L"fragments").SetAt(0,JsonValue::CreateStringValue(L"changed snapshot"));
                else if (std::string_view(change) == "snapshot-prose") record.GetNamedObject(L"block").Insert(L"question",JsonValue::CreateStringValue(L"invented original wording"));
                else if (std::string_view(change) == "proof") layout.GetNamedArray(L"editorial").RemoveAtEnd();
                else if (std::string_view(change) == "origin") layout.GetNamedArray(L"origin").GetAt(static_cast<unsigned>(block.address.page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(block.address.block)).GetObject().Insert(L"sha256",JsonValue::CreateStringValue(L"0000000000000000000000000000000000000000000000000000000000000000"));
                else if (std::string_view(change) == "extra") record.Insert(L"groups",JsonArray{});
                else if (std::string_view(change) == "missing") layout.Remove(L"orderRefinements");
                else if (std::string_view(change) == "empty") layout.Insert(L"orderRefinements",JsonArray{});
                else puzzle.Insert(L"type",JsonValue::CreateStringValue(L"paragraph"));
                const auto invalid = winrt::to_string(root.Stringify());
                rejects([&] { (void)parse_lesson_json(invalid); }, "An unverified ordering refinement passed the native loader");
                rejects([&] { (void)lgaed::windows::parse_lesson_progress_metadata(invalid); }, "An unverified ordering refinement passed the catalog loader");
            }
        }
        original_count += old.fragments.size(); line_count += block.fragments.size();
    }
    check(original_count == 65 && line_count == 124 && empty_lines == 8 && identical_closings == 2 && historical_grouped >= 2,
        "Ordering census, identical closing lines or grouped historical reading provenance changed");
    const auto edge_fixture = [](std::string_view original, std::string_view original_hash,
            std::string_view refined_lines = R"lines(["std::cout << \"\\n\";","return 0;"])lines",
            std::string_view refined_hash = "c5a31ad817dbc6a12830d5c69482a58a1e986eb6fce91469c75fc38252108a23") {
        // Golden canonical hashes are independent of the native splitter. Each
        // malformed original still owns a valid signature, so rejection must
        // inspect the split contract rather than stop at an unrelated hash.
        auto root = JsonObject::Parse(LR"({"id":"order_lines_edge","title":"Ordering edges","pages":[{"blocks":[{"type":"order_code","fragments":["std::cout << \"\\n\";","return 0;"],"correctOrder":[0,1]}]}],"stateLayout":{"version":2,"locale":"es","origin":[],"current":[{"id":"page","blocks":["block"]}],"family":{"es":{"sha256":"0000000000000000000000000000000000000000000000000000000000000000","origin":[]}},"additions":[],"editorial":[],"orderRefinements":[]}})");
        const auto puzzle=root.GetNamedArray(L"pages").GetAt(0).GetObject().GetNamedArray(L"blocks").GetAt(0).GetObject();
        const auto lines=JsonArray::Parse(winrt::to_hstring(refined_lines));JsonArray order;
        for(unsigned index=0;index<lines.Size();++index)order.Append(JsonValue::CreateNumberValue(index));
        puzzle.Insert(L"fragments",lines);puzzle.Insert(L"correctOrder",order);
        auto origin = JsonArray::Parse(LR"([{"id":"page","blocks":[{"id":"block","sha256":""}]}])");
        origin.GetAt(0).GetObject().GetNamedArray(L"blocks").GetAt(0).GetObject().Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(original_hash)));
        const auto layout = root.GetNamedObject(L"stateLayout");
        layout.Insert(L"origin",origin);layout.GetNamedObject(L"family").GetNamedObject(L"es").Insert(L"origin",origin);
        const auto snapshot = JsonObject::Parse(LR"({"type":"order_code","fragments":[],"correctOrder":[0]})");
        snapshot.GetNamedArray(L"fragments").Append(JsonValue::CreateStringValue(winrt::to_hstring(original)));
        const auto record = JsonObject::Parse(LR"({"id":"block"})");record.Insert(L"block",snapshot);
        layout.GetNamedArray(L"orderRefinements").Append(record);
        const auto proof = JsonObject::Parse(LR"({"id":"block","beforeSha256":"","afterSha256":"c5a31ad817dbc6a12830d5c69482a58a1e986eb6fce91469c75fc38252108a23"})");
        proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(original_hash)));
        proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(refined_hash)));
        layout.GetNamedArray(L"editorial").Append(proof);
        return winrt::to_string(root.Stringify());
    };
    const auto crlf = parse_lesson_json(edge_fixture("std::cout << \"\\n\";\r\nreturn 0;",
        "d3c20f94afc015dc82eb38f647849f15a05af8d2070f270e0c4370dc267800ad"));
    check(crlf.pages[0].blocks[0].fragments == std::vector<std::string>{"std::cout << \"\\n\";","return 0;"} &&
        crlf.pages[0].blocks[0].order_refinement->original_fragments[0] == "std::cout << \"\\n\";\r\nreturn 0;",
        "CRLF refinement changed a written escape or discarded original separators");
    for (const auto& [original,hash] : std::vector<std::pair<std::string,std::string>>{
            {"A\rB","12cd99df22bedbddb2181e081ac488bbda76023332159a6de1ca402393b0c36f"},
            {"","63b8871f92f911a0c96d8b81ecb282f7314626c49ab715fc643e7a06c9fa8a76"},
            {" \t\n\t","53de842e12adeef8e22118b784da5610d4e29b199f33dd6f5a6b13bfe5cccfa6"}}) {
        const auto invalid = edge_fixture(original,hash);
        rejects([&] { (void)parse_lesson_json(invalid); }, "A bare CR, empty or decorative original card acquired a new representation");
        rejects([&] { (void)lgaed::windows::parse_lesson_progress_metadata(invalid); }, "Malformed original order lines passed the metadata projection");
    }
    struct SemanticEdge {std::string_view original, before_hash, refined_lines, after_hash;};
    for(const auto& edge:std::vector<SemanticEdge>{
            {"R\"(uno\n\ndos)\"\nreturn 0;","6afc8387d72ae1879dd9631944e664046702ec40944d1896a8e766e95fc0f3c2",
                R"lines(["R\"(uno","dos)\"","return 0;"])lines","3f745333b5ffba4da2a4bfe06b033b494fb50e82cc09cc31cdf500fb80826b38"},
            {"u8R\"tag(uno\n\ndos)tag\"\nreturn 0;","8d64815b6f271d60f8f95709a845bb68bbf89584fcf74080ef94fb527c713962",
                R"lines(["u8R\"tag(uno","dos)tag\"","return 0;"])lines","6085cea034695360e372e9b4d65e354d07ef027aea833ca06f291d95eaa69a31"},
            {"#define SALUDO \\\n    \"Hola\"\nreturn 0;","f3e86f21ff58e3f037db17978ca5a0d1b79529799a052aabde5294960db352d6",
                R"lines(["#define SALUDO \\","    \"Hola\"","return 0;"])lines","45ebe68c520c47ca9925e9edc1884553f5c82a5c01b832d6a1955455a11a6a9a"},
            {"#define SALUDO \\\r\n    \"Hola\"\r\nreturn 0;","1b605f9b99a3afbeb9f44aa7c181f2e270758fe6b3fb7041ed61e2b0eec9646a",
                R"lines(["#define SALUDO \\","    \"Hola\"","return 0;"])lines","45ebe68c520c47ca9925e9edc1884553f5c82a5c01b832d6a1955455a11a6a9a"},
            {"R\"(uno\nreturn 0;","90543095eeb0ec807fd40e22cd7706e6ad31c615feb971af9c72222066a91f09",
                R"lines(["R\"(uno","return 0;"])lines","654398cc6a4d961b7fa89fda30401f8117cde9ff4fb9daa14c147b1d1b901474"}}) {
        const auto invalid=edge_fixture(edge.original,edge.before_hash,edge.refined_lines,edge.after_hash);
        // Both snapshots and current arrays have matching golden hashes. These
        // negatives would pass the old blank-line-dropping expansion itself.
        rejects([&] { (void)parse_lesson_json(invalid); },"Refinement changed a multiline raw literal or line continuation");
        rejects([&] { (void)lgaed::windows::parse_lesson_progress_metadata(invalid); },"Catalog accepted a semantic source change during line refinement");
    }
    const auto raw_single=parse_lesson_json(edge_fixture("u8R\"tag(uno\\ndos)tag\"\nreturn 0;",
        "a78be23711475d58b30f448eef097f0b938428bdbf0a6b66ab878caef3c137f3",
        R"lines(["u8R\"tag(uno\\ndos)tag\"","return 0;"])lines","dd9f4329eaa34dcdad83f9fea9ff1f8f0ec07af5523e28a2ca72c17beedf64bb"));
    check(raw_single.pages[0].blocks[0].fragments==std::vector<std::string>{"u8R\"tag(uno\\ndos)tag\"","return 0;"},
        "A single-line raw literal containing a written escape was changed or unnecessarily disabled");
    std::cout << "PASS all 14 ordering refinements: 65 groups to 124 real lines, 8 archived blank lines, original response keys, catalog metadata, grouped reading archives, exact duplicate-line equivalence and negative provenance/source guards\n";
}
void reviewed_order_equivalences_are_source_bound() {
    using namespace winrt::Windows::Data::Json;
    using lgaed::windows::parse_lesson_json;
    struct Reviewed {
        std::string_view path,lesson,identity,payload,changed_source,changed_order;
        std::vector<std::vector<std::size_t>> groups;
        std::vector<std::pair<std::size_t,std::size_t>> rejected_swaps;
        std::size_t accepted_count;
    };
    const std::vector<Reviewed> cases{
        {"objetos_y_estado/lesson_representar_procesar_y_recuperar.json",
            "cpp_fundamentals__objetos_y_estado__representar_procesar_y_recuperar","block_0002_0001",
            "258a6d4d80faadc66eb13ae8816884516ce6fea5fc214d94ebf8381d916bd794",
            "8228af5ce9bc0ecebc96a13b35a1603945531b71ee380e56ac358a60a77f0261",
            "135a1cbe862094a0d43b4594b02d4aa34e162e6e39aa7663014df242d4efa8f6",{{2,3}},{{5,8}},2},
        {"colecciones_y_recorridos/lesson_acumular_contar_y_seleccionar.json",
            "cpp_fundamentals__colecciones_y_recorridos__acumular_contar_y_seleccionar","block_0005_0004",
            "2bb235af23438d36d2d8767e3ea0f0375e071bff8bbf6187712dd664a4fb856a",
            "d02b8e8ab818ad582edfdcd7cb002c9959d7cffa6f41cf3c5872527b5bcabe79",
            "e1da4ca2025ec2eeead4ee3cd6d84ccf26384b398e815d38aac7bb4a205ae122",{{2,3},{6,7}},{{7,5}},4},
        {"funciones_y_contratos/lesson_construir_un_proyecto_desde_su_interfaz.json",
            "cpp_fundamentals__funciones_y_contratos__construir_un_proyecto_desde_su_interfaz","orden_validar_antes_de_calcular",
            "db9c0be2b1cbbc53902fc10d467795eef8cb8aca7b76adb523aeac586cd070e0",
            "587b5e99d4ef43debdd40ad0ad065b68a492df4a9e83947759fca2ea09e06e50",
            "ef6294814a96ea737f733bb69090343d2216fb3616b50dbf941c736493cafbb1",{{2,3}},{{0,4},{0,1}},2},
        {"excepciones_y_recuperacion/lesson_recuperar_por_operacion.json",
            "cpp_fundamentals__excepciones_y_recuperacion__recuperar_por_operacion","block_0002_0003",
            "5671aba2d45f3845d3084c367a057a9f519ae9da8e9b2207c428abd2f4bac513",
            "5bc21f5c8d717f58f0addb3757c49c73f3b3a29ad21cb203011900f299a17dba",
            "36419863e044fff8e9394ec7704a53b2202f178bab68a4280d933927f283f956",{{3,4},{6,7},{17,18}},{{13,17},{5,10}},8},
        {"archivos_y_programa_completo/lesson_extraer_un_tipo_sin_cambiarlo_en_fallo.json",
            "cpp_fundamentals__archivos_y_programa_completo__extraer_un_tipo_sin_cambiarlo_en_fallo","block_0004_0003",
            "4380d9b5e34bdda15027e6d92939d391ad4b22864f3ded1dac00d22717111af6",
            "465f87245763c22a1f7fc53f3a8356be994ec212862c262e6afb03613a4c770a",
            "d90c1adea8ee23ba29bcf79b457272762c2d5acc876c6258a4f9dc8eb7395a02",{{2,3,4}},{{0,5},{0,6}},6}
    };
    // A valid one-block v2 origin isolates the equivalence guard from unrelated
    // retirement/editorial manifests. Digests below are independent Python
    // state_payload goldens, including valid but unreviewed source/order edits.
    const auto fixture=[](const Reviewed& entry,const Block& block,std::string_view change) {
        const auto root=JsonObject::Parse(LR"({"id":"","title":"Reviewed orders","pages":[{"blocks":[]}],"stateLayout":{"version":2,"locale":"es","origin":[],"current":[{"id":"page","blocks":[]}],"family":{},"additions":[],"editorial":[]}})");
        const auto puzzle=JsonObject::Parse(winrt::to_hstring(block.raw_json));
        const auto language=change=="locale"?"en":"es";
        const auto identity=change=="identity"?std::string("different_identity"):std::string(entry.identity);
        const auto owner=change=="lesson"?std::string(entry.lesson)+"_other":std::string(entry.lesson);
        auto digest=entry.payload;
        if(change=="source"||change=="stale-source") {
            const auto fragments=puzzle.GetNamedArray(L"fragments");
            fragments.SetAt(0,JsonValue::CreateStringValue(winrt::to_hstring(winrt::to_string(fragments.GetAt(0).GetString())+" // sin revision")));
            if(change=="source")digest=entry.changed_source;
        } else if(change=="order"||change=="stale-order") {
            const auto order=puzzle.GetNamedArray(L"correctOrder");const auto first=order.GetAt(0);
            order.SetAt(0,order.GetAt(1));order.SetAt(1,first);
            if(change=="order")digest=entry.changed_order;
        } else if(change=="editorial") {
            for(const auto field:{L"question",L"explanation",L"reviewFeedback"})
                puzzle.Insert(field,JsonValue::CreateStringValue(L"Redacción editorial revisada"));
        }
        root.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(owner)));
        root.GetNamedArray(L"pages").GetAt(0).GetObject().GetNamedArray(L"blocks").Append(puzzle);
        const auto origin=JsonArray::Parse(LR"([{"id":"page","blocks":[{"id":"","sha256":""}]}])");
        const auto original=origin.GetAt(0).GetObject().GetNamedArray(L"blocks").GetAt(0).GetObject();
        original.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        original.Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(digest)));
        const auto layout=root.GetNamedObject(L"stateLayout");layout.Insert(L"origin",origin);
        layout.Insert(L"locale",JsonValue::CreateStringValue(winrt::to_hstring(language)));
        layout.GetNamedArray(L"current").GetAt(0).GetObject().GetNamedArray(L"blocks").Append(JsonValue::CreateStringValue(winrt::to_hstring(identity)));
        const auto family=JsonObject::Parse(LR"({"sha256":"0000000000000000000000000000000000000000000000000000000000000000","origin":[]})");
        family.Insert(L"origin",origin);layout.GetNamedObject(L"family").Insert(winrt::to_hstring(language),family);
        return winrt::to_string(root.Stringify());
    };
    const auto folder=std::filesystem::path(lgaed::build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    std::size_t accepted=0,additional=0;
    for(const auto& entry:cases) {
        const auto lesson=lgaed::windows::load_lesson(folder/entry.path);
        check(lesson.id==entry.lesson,"Reviewed ordering fixture changed its full lesson identity");
        const auto position=stable_block_position(lesson,entry.identity);
        check(position.has_value(),"Reviewed ordering fixture lost its stable identity");
        const auto& block=lesson.pages[position->page].blocks[position->block];
        check(block.order_equivalence_groups==entry.groups,"An exact reviewed ordering lost its independent fragments");
        const auto metadata=lgaed::windows::load_lesson_progress_metadata(folder/entry.path);
        const auto projected=stable_block_position(metadata,entry.identity);
        check(projected&&metadata.pages[projected->page].blocks[projected->block].order_equivalence_groups==entry.groups,
            "Catalog projection lost reviewed ordering equivalences");
        auto original=block;original.order_equivalence_groups.clear();
        check(order_representation_frame(block)==order_representation_frame(original)&&
            exercise_key(lesson,block)==exercise_key(lesson,original)&&draft_key("programming",lesson,block)==draft_key("programming",lesson,original),
            "Reviewed equivalences changed canonical representation, historical response address or draft key");
        std::vector<std::vector<std::size_t>> orders{block.correct_order};
        for(auto group:entry.groups) {
            std::vector<std::size_t> slots;
            for(std::size_t slot=0;slot<block.correct_order.size();++slot)
                if(std::ranges::find(group,block.correct_order[slot])!=group.end())slots.push_back(slot);
            check(slots.size()==group.size(),"Reviewed fixture group omitted a canonical card");
            std::ranges::sort(group);std::vector<std::vector<std::size_t>> next;
            do {for(auto order:orders){for(std::size_t index=0;index<slots.size();++index)order[slots[index]]=group[index];next.push_back(std::move(order));}}
            while(std::next_permutation(group.begin(),group.end()));
            orders=std::move(next);
        }
        check(orders.size()==entry.accepted_count,"Reviewed ordering permutation census changed");
        for(const auto& order:orders) {
            check(evaluate_order(block,order).correct,"An explicitly reviewed independent ordering was rejected");
            ++accepted;if(!evaluate_order(original,order).correct)++additional;
        }
        auto missing=block.correct_order;missing.pop_back();
        auto duplicate=block.correct_order;duplicate.back()=duplicate.front();
        auto outside=block.correct_order;outside.back()=block.fragments.size();
        check(!evaluate_order(block,missing).correct&&!evaluate_order(block,duplicate).correct&&!evaluate_order(block,outside).correct,
            "Reviewed groups accepted missing, reused or unknown physical cards");
        for(const auto [left,right]:entry.rejected_swaps) {
            auto wrong=block.correct_order;
            std::iter_swap(std::ranges::find(wrong,left),std::ranges::find(wrong,right));
            check(!evaluate_order(block,wrong).correct,"Reviewed groups accepted a dependency, scope or validation-before-assignment violation");
        }
        for(const auto& snapshot:lesson.state_layout.retirement_history)for(const auto& page:snapshot.pages)for(const auto& archived:page.blocks)
            check(archived.order_equivalence_groups.empty(),"A historical reading snapshot silently acquired current answer equivalences");
        for(const auto change:{"exact","editorial","lesson","identity","locale","source","order"}) {
            const auto text=fixture(entry,block,change);const auto language=std::string_view(change)=="locale"?"en":"es";
            const auto current=parse_lesson_json(text,language);
            const auto summary=lgaed::windows::parse_lesson_progress_metadata(text,language);
            const auto expected=std::string_view(change)=="exact"||std::string_view(change)=="editorial"?entry.groups:std::vector<std::vector<std::size_t>>{};
            check(current.pages[0].blocks[0].order_equivalence_groups==expected&&summary.pages[0].blocks[0].order_equivalence_groups==expected,
                "Reviewed ordering guard ignored locale, owner, identity or protected source, or wrongly pinned editorial prose");
            check(evaluate_order(current.pages[0].blocks[0],current.pages[0].blocks[0].correct_order).correct,
                "An unreviewed valid contract lost its own canonical answer");
        }
        for(const auto change:{"stale-source","stale-order"}) {
            const auto text=fixture(entry,block,change);
            rejects([&]{(void)parse_lesson_json(text);},"A stale protected source or canonical order passed the reviewed loader");
            rejects([&]{(void)lgaed::windows::parse_lesson_progress_metadata(text);},"A stale protected ordering passed catalog projection");
        }
    }
    check(accepted==22&&additional==17,"The five reviewed orderings no longer expose exactly 22 valid reconstructions");
    Block identical;identical.type=BlockType::order_code;identical.fragments={"A","B","A","C"};
    identical.correct_order={0,1,2,3};identical.order_equivalence_groups={{0,1}};
    check(evaluate_order(identical,std::vector<std::size_t>{1,0,2,3}).correct&&
        evaluate_order(identical,std::vector<std::size_t>{1,2,0,3}).correct,
        "Text-identical physical pieces lost equivalent source reconstruction inside a reviewed group");
    check(!evaluate_order(identical,std::vector<std::size_t>{1,3,0,2}).correct,
        "Reviewed text equivalence moved a different statement into an unreviewed slot");
    auto indented=identical;indented.fragments[2]=" A";
    check(!evaluate_order(indented,std::vector<std::size_t>{1,2,0,3}).correct,"Reviewed text equivalence normalized indentation");
    for(const auto& groups:std::vector<std::vector<std::vector<std::size_t>>>{{{0}},{{0,1},{1,2}},{{0,4}},{{0,0}}}) {
        auto malformed=identical;malformed.order_equivalence_groups=groups;
        check(!evaluate_order(malformed,malformed.correct_order).correct,"Malformed independent fragment groups bypassed canonical validation");
    }
    std::cout<<"PASS five source-bound ordering equivalences: 22 reviewed orders, 17 additional reconstructions, exact text/card ownership, dependency/scope/validation negatives, locale/identity/payload guards and unchanged historical representation\n";
}
void reviewed_choice_revisions_preserve_the_published_contract() {
    using namespace winrt::Windows::Data::Json;
    const auto folder=std::filesystem::path(lgaed::build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    for(const auto& plan:lgaed::test::choice_revision_cases()) {
        const auto integrated=lgaed::windows::load_lesson(folder/plan.filename);
        const auto before=lgaed::test::choice_revision_before(integrated,plan.identity);
        const auto current=lgaed::test::revise_choice_lesson(before,plan.identity);
        const auto recovered=lgaed::test::choice_revision_before(current,plan.identity);
        check(JsonObject::Parse(winrt::to_hstring(recovered.raw_json)).Stringify()==JsonObject::Parse(winrt::to_hstring(before.raw_json)).Stringify(),
            "An integrated choice fixture did not recover its exact complete previous lesson");
        const auto metadata=lgaed::windows::parse_lesson_progress_metadata(current.raw_json);
        const auto position=stable_block_position(current,plan.identity);check(position.has_value(),"Reviewed choice lost its stable identity");
        const auto& old=before.pages[position->page].blocks[position->block];const auto& block=current.pages[position->page].blocks[position->block];
        const auto live=stable_block_position(integrated,plan.identity);
        check(live==position&&old.options!=block.options,"A choice proposal changed its authored identity or retained the old options");
        const auto live_fingerprint=lgaed::test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(
            integrated.pages[live->page].blocks[live->block].raw_json)));
        if(integrated.pages[live->page].blocks[live->block].choice_revision) {
            rejects([&]{(void)lgaed::test::revise_choice_lesson(integrated,plan.identity);},
                "An integrated choice was accepted as a new historical transition");
            check(live_fingerprint==plan.after_full,"Historical choice revision did not reproduce the current authored payload");
        } else check(plan.allow_unexpanded_source&&live_fingerprint==plan.before_full,
            "An unintegrated choice proposal changed its independently pinned source");
        check(block.explanation==(plan.explanation?plan.explanation:old.explanation),
            "The choice proposal omitted its exact paired explanation or changed unrelated prose");
        check(block.choice_revision&&block.choice_revision->original_options==old.options&&block.choice_revision->original_review_feedback_options==old.review_feedback_options
            &&block.choice_revision->original_question==old.question&&block.choice_revision->original_explanation==old.explanation
            &&block.choice_revision->original_correct_index==old.correct_index&&block.choice_revision->sha256==plan.before_full,
            "Reviewed choice lost its complete original option/question/feedback contract");
        check(block.options[plan.option]==plan.replacement&&block.correct_index==old.correct_index&&current.state_layout.lineage==before.state_layout.lineage
            &&current.state_layout.signatures==before.state_layout.signatures&&exercise_key(current,block)==exercise_key(before,old)
            &&draft_key("programming",current,block)==draft_key("programming",before,old),"Reviewed choice changed correctness, family or historical slots");
        check(metadata.pages[position->page].blocks[position->block].choice_revision&&
            metadata.pages[position->page].blocks[position->block].choice_revision->original_review_feedback_options==old.review_feedback_options&&
            metadata.pages[position->page].blocks[position->block].choice_revision->representation_signature==block.choice_revision->representation_signature,
            "Catalog projection discarded reviewed choice provenance or representation");
        check(evaluate_choice(block,*block.correct_index).correct&&evaluate_choice(block,plan.option).correct==(plan.option==*block.correct_index),
            "Reviewed choice changed an authored verdict");
        std::string retired_paragraph;
        for(const auto& page:before.pages)for(const auto& candidate:page.blocks)
            if(retired_paragraph.empty()&&candidate.type==BlockType::paragraph)retired_paragraph=candidate.stable_id;
        check(!retired_paragraph.empty(),"Choice archive QA needs a real published paragraph");
        const std::vector<std::string> retirement{retired_paragraph};
        const auto retired_before=lgaed::test::retire_lesson(before,retirement,before.compiler_puzzles_required);
        const auto revised_after_retirement=lgaed::test::revise_choice_lesson(retired_before,plan.identity);
        const auto retired_after=lgaed::test::retire_lesson(current,retirement,current.compiler_puzzles_required);
        lgaed::state::StateData data;lgaed::state::Response prior;prior.selected=static_cast<std::int64_t>(plan.option);
        prior.correct=evaluate_choice(old,plan.option).correct;if(!prior.correct)lgaed::state::mark_response_reviewed(prior,old);
        const auto historical_key=exercise_key(before,old);lgaed::state::put_response(data,historical_key,prior);
        const auto original_json=lgaed::state::string_value(data,"learning_progress",historical_key);
        for(const bool after_revision:{false,true}) {
            auto archived=after_revision?retired_after:revised_after_retirement;
            check(archived.state_layout.retirement_history.size()==1,"Choice archive QA unexpectedly changed retirement batches");
            const auto& snapshot=archived.state_layout.retirement_history.front();archived.pages=snapshot.pages;archived.state_layout.current=snapshot.current;
            const auto archived_position=stable_block_position(archived,plan.identity);check(archived_position.has_value(),"Retirement lost the archived choice");
            const auto& archived_block=archived.pages[archived_position->page].blocks[archived_position->block];
            check(archived_block.choice_revision.has_value()==after_revision&&archived_block.options==(after_revision?block.options:old.options),
                "Choice revision was omitted from a later reading archive or attributed to an earlier one");
            check(exercise_key(archived,archived_block)==historical_key&&lgaed::state::response_storage_key(archived,archived_block)==
                (after_revision?lgaed::state::response_storage_key(current,block):historical_key),
                "An archived choice read another representation's response slot");
            const auto untouched=data;const auto restored=lgaed::state::get_response(data,archived,archived_block);
            check(restored&&restored->historical_choice==after_revision&&restored->correct==prior.correct&&restored->selected==prior.selected
                &&lgaed::state::selected_choice_text(*restored,archived_block)==old.options[plan.option]
                &&lgaed::state::choice_incorrect_feedback(*restored,archived_block)==incorrect_feedback(old,plan.option)&&data==untouched,
                "Archived reading attributed a corrected option or new guide to the original selection");
            lgaed::state::Response selected;selected.selected=static_cast<std::int64_t>(plan.option);selected.correct=evaluate_choice(block,plan.option).correct;
            if(!selected.correct)lgaed::state::mark_response_reviewed(selected,block);
            auto with_current=data;lgaed::state::put_response(with_current,current,block,selected);
            const auto latest=lgaed::state::get_response(with_current,archived,archived_block);
            check(latest&&!latest->historical_choice&&lgaed::state::selected_choice_text(*latest,archived_block)==(after_revision?block.options[plan.option]:old.options[plan.option])
                &&lgaed::state::choice_incorrect_feedback(*latest,archived_block)==incorrect_feedback(after_revision?block:old,plan.option)
                &&lgaed::state::string_value(with_current,"learning_progress",historical_key)==original_json,
                "Before/after reading archives confused current sidecar guidance or rewrote the old response");
        }
        for(const auto change:{"snapshot","snapshot-prose","missing","duplicate","extra","identity","lesson","proof","option","order","correct","review","origin","unmapped","v1","locale"}) {
            const auto root=JsonObject::Parse(winrt::to_hstring(current.raw_json));const auto layout=root.GetNamedObject(L"stateLayout");
            const auto records=layout.GetNamedArray(L"choiceRevisions");
            const auto record=records.GetAt(records.Size()-1).GetObject();
            check(winrt::to_string(record.GetNamedString(L"id"))==plan.identity,"Choice negative case targets another revision archive");
            const auto puzzle=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
            const auto target=std::string_view(change);
            if(target=="snapshot")record.GetNamedObject(L"block").GetNamedArray(L"options").SetAt(0,JsonValue::CreateStringValue(L"Changed original"));
            else if(target=="snapshot-prose")record.GetNamedObject(L"block").Insert(L"question",JsonValue::CreateStringValue(L"Changed original prompt"));
            else if(target=="missing")layout.Remove(L"choiceRevisions");
            else if(target=="duplicate")layout.GetNamedArray(L"choiceRevisions").Append(JsonObject::Parse(record.Stringify()));
            else if(target=="extra")record.Insert(L"options",JsonArray{});
            else if(target=="identity")record.Insert(L"id",JsonValue::CreateStringValue(L"unknown"));
            else if(target=="lesson")root.Insert(L"id",JsonValue::CreateStringValue(L"different_owner"));
            else if(target=="proof")layout.GetNamedArray(L"editorial").RemoveAtEnd();
            else if(target=="option")puzzle.GetNamedArray(L"options").SetAt(1,JsonValue::CreateStringValue(L"Another option"));
            else if(target=="order"){const auto options=puzzle.GetNamedArray(L"options");const auto first=options.GetAt(0);options.SetAt(0,options.GetAt(1));options.SetAt(1,first);}
            else if(target=="correct")puzzle.Insert(L"correctIndex",JsonValue::CreateNumberValue(static_cast<double>((*block.correct_index+1)%block.options.size())));
            else if(target=="review")puzzle.GetNamedArray(L"reviewFeedback").SetAt(1,JsonValue::CreateStringValue(L"Changed current guide without proof"));
            else if(target=="origin")layout.GetNamedArray(L"origin").GetAt(static_cast<unsigned>(block.address.page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(block.address.block)).GetObject().Insert(L"sha256",JsonValue::CreateStringValue(L"0000000000000000000000000000000000000000000000000000000000000000"));
            else if(target=="unmapped")root.Remove(L"stateLayout");
            else if(target=="v1"){JsonObject v1;v1.Insert(L"version",JsonValue::CreateNumberValue(1));v1.Insert(L"origin",layout.GetNamedArray(L"origin"));v1.Insert(L"current",layout.GetNamedArray(L"current"));root.Insert(L"stateLayout",v1);}
            else layout.Insert(L"locale",JsonValue::CreateStringValue(L"en"));
            const auto invalid=winrt::to_string(root.Stringify());
            rejects([&]{(void)lgaed::windows::parse_lesson_json(invalid);},"Unapproved choice/source/provenance revision passed the full loader");
            rejects([&]{(void)lgaed::windows::parse_lesson_progress_metadata(invalid);},"Unapproved choice revision passed catalog projection");
        }
        if(plan.explanation) {
            for(const bool historical:{false,true}) {
                const auto root=JsonObject::Parse(winrt::to_hstring(historical?retired_after.raw_json:current.raw_json));
                const auto pages=historical?root.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory").GetObjectAt(0).GetNamedArray(L"pages")
                    :root.GetNamedArray(L"pages");
                const auto target=pages.GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
                target.Insert(L"explanation",JsonValue::CreateStringValue(winrt::to_hstring(old.explanation)));
                if(!historical) {
                    // Even a continuous later editorial edge cannot split
                    // this one explicitly paired option/explanation revision.
                    JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(plan.identity)));
                    proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(plan.after_full)));
                    proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(lgaed::test::compile_check_test_fingerprint(target))));
                    root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
                }
                const auto invalid=winrt::to_string(root.Stringify());
                rejects([&]{(void)lgaed::windows::parse_lesson_json(invalid);},"A token option borrowed the old explanation from another revision");
                rejects([&]{(void)lgaed::windows::parse_lesson_progress_metadata(invalid);},"Catalog accepted a mixed token option/explanation revision");
            }
        }
        if(std::string_view(plan.identity)=="block_0006_0004") {
            const auto root=JsonObject::Parse(winrt::to_hstring(current.raw_json));
            root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject().Insert(L"explanation",JsonValue::CreateStringValue(L"QA subsequent editorial text"));
            JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(plan.identity)));
            proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(plan.after_full)));
            proof.Insert(L"afterSha256",JsonValue::CreateStringValue(L"33057f95b36e8f88e11b57a6dd5a40c8410593884be99e226d182126560919de"));
            root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(proof);
            const auto later=lgaed::windows::parse_lesson_json(winrt::to_string(root.Stringify()));
            check(later.pages[position->page].blocks[position->block].choice_revision->original_explanation==old.explanation,
                "A later editorial explanation replaced the historical choice snapshot");
        }
    }
    std::cout<<"PASS exact choice revisions, including reviewed in-memory proposals: original snapshot/family/keys, full and catalog projection, reviewed first-edge provenance and later editorial text, negative source/ownership/map guards\n";
}
void reviewed_session_literal_pair(std::string_view before_filename,std::string_view after_filename,
        std::string_view before_sha256,std::string_view after_sha256,std::size_t editorial_prefix) {
    using namespace winrt::Windows::Data::Json;
    using namespace lgaed;
    constexpr std::string_view identity="block_0001_0004";
    constexpr std::string_view old_full="7246b74a0c6b46f2c14b26ea813cfc85a0d691348aea8ada18c279d46ce19156";
    constexpr std::string_view new_full="09a016cfe0637a8be64ccd67e0ec8f3bd57e8d63ca5e03c39ecbe760d7bacad1";
    std::array<wchar_t,32768> executable{};
    const auto count=GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    check(count&&count<executable.size(),"Cannot resolve immutable session editorial fixtures");
    const auto directory=std::filesystem::path(std::wstring_view(executable.data(),count)).parent_path()/"test-fixtures/u11-session-editorial";
    const auto before_bytes=state::read_bytes(directory/before_filename),after_bytes=state::read_bytes(directory/after_filename);
    check(state::sha256(before_bytes)==before_sha256&&state::sha256(after_bytes)==after_sha256,
        "The session literal fixtures differ from their independent Git/candidate bytes");
    const auto before=windows::load_lesson(directory/before_filename),after=windows::load_lesson(directory/after_filename);
    const auto old_metadata=windows::parse_lesson_progress_metadata(before.raw_json),metadata=windows::parse_lesson_progress_metadata(after.raw_json);
    const auto old_at=stable_block_position(before,identity),at=stable_block_position(after,identity);
    check(old_at&&at&&*old_at==*at&&at->page==1&&at->block==4,"The session literal clarification moved its original identity/address");
    const auto& previous=before.pages[1].blocks[4];const auto& current=after.pages[1].blocks[4];
    check(test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(previous.raw_json)))==old_full
        &&test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(current.raw_json)))==new_full,
        "The session literal clarification changed another field of its complete reviewed activity");
    check(previous.explanation=="La creación completa guardó cero. El literal100 se rechaza y su candidato no sustituye ese estado."
        &&current.explanation=="La creación completa guardó cero. El literal 100 se rechaza y su candidato no sustituye ese estado."
        &&current.id==previous.id&&current.type==BlockType::multiple_choice&&current.question==previous.question
        &&current.options==previous.options&&current.correct_index==previous.correct_index&&current.correct_index==1
        &&current.review_feedback_options==previous.review_feedback_options,
        "The exact single-space review changed correctness, question, options, owner or guides");
    check(before.state_layout.reading_revisions.empty()&&old_metadata.state_layout.reading_revisions.empty()
        &&before.state_layout.signatures==after.state_layout.signatures&&before.state_layout.lineage==after.state_layout.lineage
        &&exercise_key(before,previous)==exercise_key(after,current)
        &&exercise_key(metadata,metadata.pages[1].blocks[4])==exercise_key(before,previous),
        "The session literal review changed its original state/family or annotated unchanged historical prose");
    for(const auto* model:{&after,&metadata}) {
        const auto& revisions=model->state_layout.reading_revisions;
        check(revisions.size()==1&&revisions[0].editorial_prefix==editorial_prefix&&revisions[0].previous_blocks.size()==1
            &&revisions[0].previous_blocks.contains(std::string(identity)),"The session reader lacks its complete authenticated editorial predecessor");
        check(test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(revisions[0].previous_blocks.at(std::string(identity)))))==old_full,
            "Runtime or catalog reading provenance invented a previous session feedback payload");
    }
    check(after.state_layout.reading_revisions[0].previous_blocks==metadata.state_layout.reading_revisions[0].previous_blocks,
        "The catalog and runtime disagree about the exact old session feedback");
    for(const auto selected:{std::size_t{0},std::size_t{1}}) {
        state::StateData data;state::prepare_lesson_state(data,before);state::Response response;
        response.selected=static_cast<std::int64_t>(selected);response.correct=evaluate_choice(previous,selected).correct;
        if(!response.correct)state::mark_response_reviewed(response,previous);
        state::put_response(data,before,previous,response);const auto saved=data;
        const auto retained=state::get_response(data,after,current);
        check(retained&&retained->selected==response.selected&&retained->correct==response.correct
            &&state::response_reviewed(*retained,current)==!response.correct&&data==saved,
            "A single-space feedback correction changed a previous correct/rejected answer or its reviewed state");
    }
    const auto document=JsonObject::Parse(winrt::to_hstring(after.raw_json));
    const auto old_editorial=JsonObject::Parse(winrt::to_hstring(before.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    const auto editorial=document.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    check(old_editorial.Size()==editorial_prefix&&editorial.Size()==editorial_prefix+1,
        "The session fixture lost its complete original editorial prefix");
    for(unsigned index=0;index<old_editorial.Size();++index)
        check(test::compile_check_test_fingerprint(old_editorial.GetObjectAt(index))==test::compile_check_test_fingerprint(editorial.GetObjectAt(index)),
            "The session literal correction replaced an earlier editorial proof");
    const auto target=[](const JsonObject& root){return root.GetNamedArray(L"pages").GetObjectAt(1).GetNamedArray(L"blocks").GetObjectAt(4);};
    const auto refresh=[&](const JsonObject& root){root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(static_cast<unsigned>(editorial_prefix))
        .Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(target(root)))));};
    const auto no_annotation=[](const JsonObject& root,std::string_view locale="es") {
        const auto raw=winrt::to_string(root.Stringify());
        check(windows::parse_lesson_json(raw,locale).state_layout.reading_revisions.empty(),
            "Unreviewed generic session prose acquired an authenticated old audio script");
        check(windows::parse_lesson_progress_metadata(raw,locale).state_layout.reading_revisions.empty(),
            "Catalog metadata attributed old session audio to another payload, owner or history");
    };
    {
        // A valid prose publication before this exact final correction belongs
        // to its real predecessor. Keep it rather than pinning a second total
        // edge count or combining old feedback with an invented older page.
        const auto extended=JsonObject::Parse(document.Stringify());
        const auto paragraph=extended.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(1);
        check(paragraph.GetNamedString(L"type")==L"paragraph","The extra session prefix lost its prose owner");
        const auto original=test::compile_check_test_fingerprint(paragraph);
        const auto revised=winrt::to_string(paragraph.GetNamedString(L"text"))+"\n\nOtra precisión editorial anterior a la corrección del literal.";
        paragraph.Insert(L"text",JsonValue::CreateStringValue(winrt::to_hstring(revised)));
        JsonObject proof;proof.Insert(L"id",JsonValue::CreateStringValue(L"block_0000_0001"));
        proof.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(original)));
        proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(paragraph))));
        JsonArray enlarged;
        for(unsigned index=0;index<editorial_prefix;++index)enlarged.Append(editorial.GetAt(index));
        enlarged.Append(proof);enlarged.Append(editorial.GetAt(static_cast<unsigned>(editorial_prefix)));
        extended.GetNamedObject(L"stateLayout").Insert(L"editorial",enlarged);
        const auto raw=winrt::to_string(extended.Stringify());
        const auto runtime=windows::parse_lesson_json(raw),catalog=windows::parse_lesson_progress_metadata(raw);
        for(const auto* model:{&runtime,&catalog}) {
            const auto& revisions=model->state_layout.reading_revisions;
            check(revisions.size()==1&&revisions[0].editorial_prefix==editorial_prefix+1
                &&revisions[0].previous_blocks==after.state_layout.reading_revisions[0].previous_blocks
                &&model->pages[0].blocks[1].text==revised,
                "An additional valid preceding prose edge hid the exact session archive or lost its newer prefix");
        }
    }
    // Compatible generic editorial prose still loads; none of these coherent
    // variants may borrow the closed correction's old feedback/audio identity.
    for(const auto change:{"wording","guide","owner","family","prefix","position","locale","later"}) {
        const auto altered=JsonObject::Parse(document.Stringify());const auto layout=altered.GetNamedObject(L"stateLayout");
        const auto mode=std::string_view(change);
        if(mode=="wording") {target(altered).Insert(L"explanation",JsonValue::CreateStringValue(L"Otra explicación editorial coherente."));refresh(altered);}
        else if(mode=="guide") {target(altered).GetNamedArray(L"reviewFeedback").SetAt(0,JsonValue::CreateStringValue(L"Otra guía editorial coherente."));refresh(altered);}
        else if(mode=="owner")altered.Insert(L"id",JsonValue::CreateStringValue(L"another_session_owner"));
        else if(mode=="family")layout.GetNamedObject(L"family").GetNamedObject(L"fr").Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="prefix")layout.GetNamedArray(L"editorial").GetObjectAt(0).Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="position") {
            const auto blocks=altered.GetNamedArray(L"pages").GetObjectAt(1).GetNamedArray(L"blocks"),map=layout.GetNamedArray(L"current").GetObjectAt(1).GetNamedArray(L"blocks");
            const auto old=blocks.GetAt(3),id=map.GetAt(3);blocks.SetAt(3,blocks.GetAt(4));blocks.SetAt(4,old);map.SetAt(3,map.GetAt(4));map.SetAt(4,id);
        } else if(mode=="locale") {
            layout.Insert(L"locale",JsonValue::CreateStringValue(L"en"));
            layout.GetNamedObject(L"family").Insert(L"en",JsonObject::Parse(layout.GetNamedObject(L"family").GetNamedObject(L"es").Stringify()));
        } else {
            const auto paragraph=altered.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks").GetObjectAt(1);
            const auto old=test::compile_check_test_fingerprint(paragraph);paragraph.Insert(L"text",JsonValue::CreateStringValue(L"Otra prosa posterior compatible."));
            JsonObject edge;edge.Insert(L"id",JsonValue::CreateStringValue(L"block_0000_0001"));
            edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(old)));
            edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(paragraph))));layout.GetNamedArray(L"editorial").Append(edge);
        }
        no_annotation(altered,mode=="locale"?"en":"es");
    }
    for(const auto change:{"identity","type","correct","option","proof","proof_before","proof_after","origin","injected"}) {
        const auto altered=JsonObject::Parse(document.Stringify());const auto block=target(altered),layout=altered.GetNamedObject(L"stateLayout");
        const auto mode=std::string_view(change);
        if(mode=="identity")block.Insert(L"id",JsonValue::CreateStringValue(L"another_activity"));
        else if(mode=="type")block.Insert(L"type",JsonValue::CreateStringValue(L"fill_code"));
        else if(mode=="correct")block.Insert(L"correctIndex",JsonValue::CreateNumberValue(0));
        else if(mode=="option")block.GetNamedArray(L"options").SetAt(0,JsonValue::CreateStringValue(L"Otra opción no autorizada"));
        else if(mode=="proof")layout.GetNamedArray(L"editorial").RemoveAtEnd();
        else if(mode=="proof_before"||mode=="proof_after")layout.GetNamedArray(L"editorial").GetObjectAt(static_cast<unsigned>(editorial_prefix))
            .Insert(mode=="proof_before"?L"beforeSha256":L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else if(mode=="origin")layout.GetNamedArray(L"origin").GetObjectAt(1).GetNamedArray(L"blocks").GetObjectAt(4).Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(std::string(64,'0'))));
        else layout.Insert(L"reading_revisions",JsonArray{});
        if(mode=="identity"||mode=="type"||mode=="correct"||mode=="option")refresh(altered);
        const auto raw=winrt::to_string(altered.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw);},"An incompatible session contract or unproved correction passed the native loader");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"Catalog accepted an incompatible session contract or injected reading archive");
    }
    check(state::read_bytes(directory/before_filename)==before_bytes&&state::read_bytes(directory/after_filename)==after_bytes,
        "The session literal loader checks rewrote their immutable source fixtures");
    std::cout<<"PASS U11L22 literal spacing "<<editorial_prefix<<"->"<<editorial_prefix+1
        <<": exact immutable pair, complete old feedback/prefix in runtime and catalog, valid additional preceding prose retained, original state/answers/guides, eight coherent generic variants without old-audio provenance, nine incompatible/injected contracts rejected; no execution or hearing claimed\n";
}
void reviewed_session_literal_supplies_only_authenticated_reading_provenance() {
    reviewed_session_literal_pair("session-before.json","session-after.json",
        "35c2ec10d50202f99fd957d77410f2fa987575f6b97c995d5354c1ed8be53d3f",
        "a44d7467cdd2f4aa0aa12b6f428ba6b595b20a9019c8d5976ea6c7be69c96f49",14);
    reviewed_session_literal_pair("session-current-before.json","session-current-after.json",
        "9012b0d2a268a5df7fde68a74922748262c7b1d08e21d6d21225743db76c2933",
        "9e248574ca08044e9d533f58cba482f033b7ea0e9fc091b9939bcce2c824b8ba",24);
}
void mathematical_puzzle_identity_remains_optional() {
    using namespace lgaed::windows;
    // Same valid, unnamed puzzle used by the reader's mathematical keyboard
    // fixture; reviewed label permissions must not require an authored id.
    constexpr auto source=R"({"id":"qa_reader_math_keyboard","title":"Prueba.","pages":[{"blocks":[{"type":"math_puzzle","prompt":"Escribe un número.","responses":[{"id":"value","type":"math_numeric_input"}]}]}]})";
    const auto runtime=parse_lesson_json(source),catalog=parse_lesson_progress_metadata(source);
    for(const auto* lesson:{&runtime,&catalog}){
        const auto& block=lesson->pages.at(0).blocks.at(0);
        check(block.type==BlockType::math_puzzle&&block.id.empty()&&block.prompt=="Escribe un número."
            &&block.raw_json.find("\"responses\":[{\"id\":\"value\"")!=std::string::npos&&!block.math_label_revision,
            "A valid mathematical puzzle without an authored id failed loading or lost its response");
    }
    check(exercise_key(runtime,runtime.pages[0].blocks[0])==exercise_key(catalog,catalog.pages[0].blocks[0]),
        "An unnamed mathematical puzzle changed its catalog progress address");
    auto invalid=std::string(source);replace_once(invalid,"\"type\":\"math_puzzle\"","\"type\":\"math_puzzle\",\"id\":42");
    rejects([&]{(void)parse_lesson_json(invalid);},"An optional mathematical identity accepted a present non-string value");
    rejects([&]{(void)parse_lesson_progress_metadata(invalid);},"Catalog metadata accepted a present non-string mathematical identity");
    std::cout<<"PASS mathematical puzzle id remains optional in full loading and catalog progress, with present identity type validation retained\n";
}
void reviewed_matching_label_preserves_historical_contract() {
    using namespace winrt::Windows::Data::Json;
    using lgaed::windows::parse_lesson_json;
    const auto root = std::filesystem::path(lgaed::build::source_root);
    const auto lesson = lgaed::windows::load_lesson(root / "app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido/lesson_construir_y_comprobar_un_modelo_de_pedidos.json");
    const auto document = JsonObject::Parse(winrt::to_hstring(lesson.raw_json));
    const auto block = document.GetNamedArray(L"pages").GetAt(7).GetObject().GetNamedArray(L"blocks").GetAt(1).GetObject();
    const auto source = block.GetNamedArray(L"responses").GetAt(0).GetObject().GetNamedArray(L"sources").GetAt(0).GetObject();
    constexpr auto revised = L"Construcción desde código y unidades que termina normalmente";
    if (source.GetNamedString(L"label") != revised) {
        source.Insert(L"label",JsonValue::CreateStringValue(revised));
        document.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Append(JsonObject::Parse(LR"({"id":"block_0007_0001","beforeSha256":"ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d","afterSha256":"20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b"})"));
    }
    const auto raw = winrt::to_string(document.Stringify());
    const auto current = parse_lesson_json(raw);
    const auto metadata = lgaed::windows::parse_lesson_progress_metadata(raw);
    check(current.state_layout.signatures == lesson.state_layout.signatures && current.state_layout.lineage == lesson.state_layout.lineage &&
        current.state_layout.raw_json.find("ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d") != std::string::npos,
        "Reviewed matching label regenerated its origin or state family");
    check(exercise_key(current,current.pages[7].blocks[1]) == exercise_key(lesson,lesson.pages[7].blocks[1]) &&
        exercise_key(metadata,metadata.pages[7].blocks[1]) == exercise_key(lesson,lesson.pages[7].blocks[1]),
        "Reviewed matching label moved saved answers or metadata progress");
    check(current.pages[7].blocks[1].raw_json.find("desde código y unidades") != std::string::npos,
        "Historical state compatibility hid the corrected matching label");
    const auto label_response=block.GetNamedArray(L"responses").GetObjectAt(0);
    const auto region=winrt::to_string(label_response.GetNamedString(L"id"))+"/source/"+winrt::to_string(source.GetNamedString(L"id"));
    const MathLabelRevision expected{region,"Construcción que termina normalmente",winrt::to_string(revised)};
    check(region=="contratos/source/construccion"&&current.pages[7].blocks[1].math_label_revision==expected
        &&metadata.pages[7].blocks[1].math_label_revision==expected,
        "The reviewed matching label lacks its exact derived reading key and old/current text in runtime or catalog metadata");
    const auto previous_document=JsonObject::Parse(winrt::to_hstring(raw));
    const auto previous_block=previous_document.GetNamedArray(L"pages").GetObjectAt(7).GetNamedArray(L"blocks").GetObjectAt(1);
    previous_block.GetNamedArray(L"responses").GetObjectAt(0).GetNamedArray(L"sources").GetObjectAt(0)
        .Insert(L"label",JsonValue::CreateStringValue(L"Construcción que termina normalmente"));
    check(lgaed::test::compile_check_test_fingerprint(previous_block)=="ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d",
        "The original matching-label metadata sample is not the complete authenticated historical block");
    JsonArray previous_proofs;unsigned removed=0;
    for(const auto& value:previous_document.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial")){
        const auto proof=value.GetObject();
        if(proof.GetNamedString(L"id")==L"block_0007_0001"
            &&proof.GetNamedString(L"beforeSha256")==L"ef442a897e83b7550727c623445788d12299b77c9df6bbbf7fd67bbbf4743b1d"
            &&proof.GetNamedString(L"afterSha256")==L"20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b")++removed;
        else previous_proofs.Append(value);
    }
    check(removed==1,"The matching-label metadata sample did not remove exactly its closed editorial edge");
    previous_document.GetNamedObject(L"stateLayout").Insert(L"editorial",previous_proofs);
    const auto previous_raw=winrt::to_string(previous_document.Stringify());
    const auto previous=parse_lesson_json(previous_raw),previous_metadata=lgaed::windows::parse_lesson_progress_metadata(previous_raw);
    check(!previous.pages[7].blocks[1].math_label_revision&&!previous_metadata.pages[7].blocks[1].math_label_revision
        &&previous.state_layout.signatures==current.state_layout.signatures&&previous.state_layout.lineage==current.state_layout.lineage
        &&exercise_key(previous,previous.pages[7].blocks[1])==exercise_key(current,current.pages[7].blocks[1]),
        "The original matching label acquired a spurious reading revision or changed its protected progress contract");
    for (const auto change : {"pair", "label", "feedback", "identity", "lesson", "proof", "origin", "unmapped", "v1"}) {
        const auto bad = JsonObject::Parse(winrt::to_hstring(raw));
        const auto puzzle = bad.GetNamedArray(L"pages").GetAt(7).GetObject().GetNamedArray(L"blocks").GetAt(1).GetObject();
        const auto response = puzzle.GetNamedArray(L"responses").GetAt(0).GetObject();
        if (std::string_view(change) == "pair") response.GetNamedObject(L"validator").GetNamedObject(L"expected").Insert(L"construccion",JsonValue::CreateStringValue(L"valor_completo"));
        else if (std::string_view(change) == "label") response.GetNamedArray(L"sources").GetAt(0).GetObject().Insert(L"label",JsonValue::CreateStringValue(L"Other label"));
        else if (std::string_view(change) == "feedback") puzzle.GetNamedObject(L"feedback").Insert(L"correct",JsonValue::CreateStringValue(L"Other feedback"));
        else if (std::string_view(change) == "identity") puzzle.Insert(L"id",JsonValue::CreateStringValue(L"other_puzzle"));
        else if (std::string_view(change) == "lesson") bad.Insert(L"id",JsonValue::CreateStringValue(L"other_lesson"));
        else if (std::string_view(change) == "proof") bad.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
        else if (std::string_view(change) == "unmapped") bad.Remove(L"stateLayout");
        else if (std::string_view(change) == "v1") {
            const auto original = bad.GetNamedObject(L"stateLayout");
            auto v1 = JsonObject();v1.Insert(L"version",JsonValue::CreateNumberValue(1));
            v1.Insert(L"origin",original.GetNamedArray(L"origin"));v1.Insert(L"current",original.GetNamedArray(L"current"));
            bad.Insert(L"stateLayout",v1);
        }
        else bad.GetNamedObject(L"stateLayout").GetNamedArray(L"origin").GetAt(7).GetObject().GetNamedArray(L"blocks").GetAt(1).GetObject()
            .Insert(L"sha256",JsonValue::CreateStringValue(L"20d9dfd3fc510cdcbeb5951b20ac028451e6a2ad402b035d4e70ea20007c649b"));
        const auto invalid = winrt::to_string(bad.Stringify());
        rejects([&] { (void)parse_lesson_json(invalid); },"Unapproved mathematical change passed the runtime loader");
        rejects([&] { (void)lgaed::windows::parse_lesson_progress_metadata(invalid); },"Unapproved mathematical change passed catalog metadata");
    }
    std::cout << "PASS exact matching label revision, preserved state/metadata addresses and negative response/provenance guards\n";
}
void reviewed_u11_domain_label_preserves_historical_contract(){
    using namespace winrt::Windows::Data::Json;
    using namespace lgaed;
    const auto path=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos/lesson_construir_un_interprete_con_historial_y_pruebas.json";
    const auto bytes=state::read_bytes(path);
    const auto fixture=test::u11_domain_label_fixture(windows::load_lesson(path));
    const auto& old=fixture.before.pages[fixture.position.page].blocks[fixture.position.block];
    const auto& current=fixture.after.pages[fixture.position.page].blocks[fixture.position.block];
    const auto metadata=windows::parse_lesson_progress_metadata(fixture.after.raw_json);
    check(fixture.before.state_layout.signatures==fixture.after.state_layout.signatures
        &&fixture.before.state_layout.lineage==fixture.after.state_layout.lineage,
        "The exact U11 domain label changed the original response family");
    check(exercise_key(fixture.before,old)==exercise_key(fixture.after,current)
        &&exercise_key(metadata,metadata.pages[8].blocks[2])==exercise_key(fixture.before,old),
        "The U11 domain label moved its state or catalog address");
    const auto corrected=JsonObject::Parse(winrt::to_hstring(current.raw_json));
    check(corrected.GetNamedArray(L"responses").GetObjectAt(0).GetNamedArray(L"sources").GetObjectAt(5).GetNamedString(L"label")==L"99+2;",
        "The historical response projection hid the corrected U11 source label");
    const auto label_response=corrected.GetNamedArray(L"responses").GetObjectAt(0),label_source=label_response.GetNamedArray(L"sources").GetObjectAt(5);
    const auto region=winrt::to_string(label_response.GetNamedString(L"id"))+"/source/"+winrt::to_string(label_source.GetNamedString(L"id"));
    const MathLabelRevision expected{region,"100+1;","99+2;"};
    const auto previous_metadata=windows::parse_lesson_progress_metadata(fixture.before.raw_json);
    check(region=="casos/source/dominio"&&current.math_label_revision==expected
        &&metadata.pages[8].blocks[2].math_label_revision==expected&&!old.math_label_revision
        &&!previous_metadata.pages[8].blocks[2].math_label_revision,
        "The closed U11 label reading metadata changed its source key/texts, lost catalog parity or aliased the original block");
    const auto reconstructed=test::u11_domain_label_fixture(fixture.after);
    check(reconstructed.before.raw_json==fixture.before.raw_json&&reconstructed.after.raw_json==fixture.after.raw_json,
        "An integrated U11 matching fixture became an unchanged or incomplete revision");
    for(const auto change:{"pair","label","feedback","identity","lesson","proof","origin","unmapped","v1"}){
        const auto bad=JsonObject::Parse(winrt::to_hstring(fixture.after.raw_json));
        const auto puzzle=bad.GetNamedArray(L"pages").GetObjectAt(8).GetNamedArray(L"blocks").GetObjectAt(2);
        const auto response=puzzle.GetNamedArray(L"responses").GetObjectAt(0);
        if(std::string_view(change)=="pair")response.GetNamedObject(L"validator").GetNamedObject(L"expected").Insert(L"dominio",JsonValue::CreateStringValue(L"completa"));
        else if(std::string_view(change)=="label")response.GetNamedArray(L"sources").GetObjectAt(5).Insert(L"label",JsonValue::CreateStringValue(L"99+3;"));
        else if(std::string_view(change)=="feedback")puzzle.GetNamedObject(L"feedback").Insert(L"correct",JsonValue::CreateStringValue(L"Other feedback"));
        else if(std::string_view(change)=="identity")puzzle.Insert(L"id",JsonValue::CreateStringValue(L"another_puzzle"));
        else if(std::string_view(change)=="lesson")bad.Insert(L"id",JsonValue::CreateStringValue(L"another_lesson"));
        else if(std::string_view(change)=="proof")bad.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").RemoveAtEnd();
        else if(std::string_view(change)=="origin")bad.GetNamedObject(L"stateLayout").GetNamedArray(L"origin").GetObjectAt(8).GetNamedArray(L"blocks").GetObjectAt(2).Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(test::u11_domain_after)));
        else if(std::string_view(change)=="unmapped")bad.Remove(L"stateLayout");
        else {const auto layout=bad.GetNamedObject(L"stateLayout");JsonObject v1;v1.Insert(L"version",JsonValue::CreateNumberValue(1));
            v1.Insert(L"origin",layout.GetNamedArray(L"origin"));v1.Insert(L"current",layout.GetNamedArray(L"current"));bad.Insert(L"stateLayout",v1);}
        const auto raw=winrt::to_string(bad.Stringify());
        rejects([&]{(void)windows::parse_lesson_json(raw);},"The runtime accepted an unreviewed U11 matching revision");
        rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"Catalog metadata accepted an unreviewed U11 matching revision");
    }
    check(state::read_bytes(path)==bytes,"U11 matching contract QA modified the curricular asset");
    std::cout<<"PASS closed U11 domain label 100+1 to99+2: full before/after pins, immutable family/address, catalog, integrated-source fixture and negative payload/owner/proof guards\n";
}
std::string root_property(std::string_view source, std::string_view name, std::string_view value) {
    // Replace a property through the JSON object, including when Work has already
    // integrated it. Appending another root key would make negative fixtures ambiguous.
    auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(source));
    root.Insert(winrt::to_hstring(name),winrt::Windows::Data::Json::JsonValue::Parse(winrt::to_hstring(value)));
    return winrt::to_string(root.Stringify());
}
void narration_metadata_tests(const Lesson& real_lesson) {
    using lgaed::windows::parse_lesson_json;
    using winrt::Windows::Data::Json::JsonValue;
    constexpr auto basic = R"({"id":"narration","title":"Original visible title","pages":[{"blocks":[{"type":"paragraph","text":"Original visible text."}]}]})";
    for (const auto asset_locale : {"es", "en", "fr"}) {
        const auto lesson = parse_lesson_json(basic, asset_locale);
        check(!lesson.narration_language && !lesson.pages[0].narration_language,
            "An asset/device language was substituted for the reader's narration preference");
    }
    // These are the actual aliases/order of NarrationLanguage.fromTag, including
    // its intentional en-UK exact alias versus generic en-* US fallback.
    for (const auto& [authored, expected] : std::vector<std::pair<std::string, std::string>>{
            {"es", "es"}, {"es-MX", "es"}, {" ES_ar ", "es"}, {"SPANISH", "es"}, {"ESPAÑOL", "es"},
            {"en", "en-US"}, {"English", "en-US"}, {" en_US ", "en-US"}, {"en-CA", "en-US"},
            {"en-GB", "en-GB"}, {"EN_gb_oed", "en-GB"}, {"en-UK", "en-GB"}, {"en-UK-extra", "en-US"},
            {"fr", "fr"}, {"Fr_CA", "fr"}, {"FRENCH", "fr"}, {"FRANÇAIS", "fr"},
            {"\xc2\xa0\xe3\x80\x80" "fr" "\xe2\x80\xaf", "fr"}}) {
        const auto value = winrt::to_string(JsonValue::CreateStringValue(winrt::to_hstring(authored)).Stringify());
        for (const auto name : {"language", "locale", "lang", "idioma"}) {
            const auto lesson = parse_lesson_json(root_property(basic, name, value));
            check(lesson.narration_language == expected && lesson.pages[0].narration_language == expected,
                "An explicit Android narration alias/key was lost or mapped to the wrong canonical language");
            check(lesson.title == "Original visible title" && lesson.pages[0].blocks[0].text == "Original visible text.",
                "Resolving narration metadata modified the visible curriculum");
        }
    }
    for (const auto invalid : {R"("")", R"("  ")", R"("de-DE")", R"("cpp")", "null", "false", "42", "[]", "{}"}) {
        const auto absent = parse_lesson_json(root_property(basic, "language", invalid));
        check(!absent.narration_language && !absent.pages[0].narration_language,
            "Unrecognized narration metadata was treated as an explicit language");
        const auto fallback = parse_lesson_json(root_property(absent.raw_json, "metadata", R"({"idioma":"French"})"));
        check(fallback.narration_language == "fr" && fallback.pages[0].narration_language == "fr",
            "Unrecognized narration metadata hid a valid lower-priority source");
    }
    const auto ordered = parse_lesson_json(R"({"id":"ordered","title":"Unchanged","language":"fr","locale":"es","lang":"en-US","idioma":"en-GB","metadata":{"language":"en-US"},"pages":[
        {"language":"es","metadata":{"language":"en-GB"},"blocks":[{"type":"paragraph","text":"A"}]},
        {"language":"unknown","metadata":{"language":null,"locale":"en_GB","lang":"es"},"blocks":[{"type":"paragraph","text":"B"}]},
        {"metadata":"not an object","blocks":[{"type":"paragraph","text":"C"}]},
        {"language":"unknown","locale":"en_US","lang":"es","blocks":[{"type":"paragraph","text":"D"}]},
        {"language":false,"locale":[],"lang":{},"idioma":"Spanish","metadata":{"language":"en-GB"},"blocks":[{"type":"paragraph","text":"E"}]}
        ]})");
    check(ordered.narration_language == "fr" && ordered.pages[0].narration_language == "es" &&
        ordered.pages[1].narration_language == "en-GB" && ordered.pages[2].narration_language == "fr" &&
        ordered.pages[3].narration_language == "en-US" && ordered.pages[4].narration_language == "es",
        "Narration precedence must be page keys, page.metadata keys, lesson keys, then lesson.metadata keys");
    const auto root_key_order = parse_lesson_json(root_property(root_property(basic, "language", R"("unknown")"),
        "locale", R"("en-GB")"));
    check(root_key_order.narration_language == "en-GB", "An unknown first language key hid a subsequent valid alias");
    const auto root_metadata = parse_lesson_json(root_property(basic, "metadata",
        R"({"language":"unknown","locale":null,"lang":"French","idioma":"Spanish"})"));
    check(root_metadata.narration_language == "fr" && root_metadata.pages[0].narration_language == "fr",
        "Lesson metadata key priority or page inheritance differs from Android");
    check(!real_lesson.narration_language && !real_lesson.pages[0].narration_language,
        "stateLayout.locale was incorrectly used as authoritative narration metadata");
    const auto explicit_language = parse_lesson_json(root_property(real_lesson.raw_json, "language", R"("French")"));
    check(explicit_language.narration_language == "fr" && explicit_language.pages[0].narration_language == "fr" &&
        explicit_language.locale == real_lesson.locale && explicit_language.state_layout.locale == real_lesson.state_layout.locale &&
        explicit_language.state_layout.lineage == real_lesson.state_layout.lineage &&
        explicit_language.state_layout.signatures == real_lesson.state_layout.signatures &&
        explicit_language.pages[0].blocks[0].text == real_lesson.pages[0].blocks[0].text &&
        explicit_language.raw_json.find(explicit_language.pages[0].blocks[0].raw_json) != std::string::npos,
        "Explicit narration metadata changed asset language, state ownership, fingerprints, or original block text");
}
void retirement_contract_tests(const Lesson& first) {
    using namespace winrt::Windows::Data::Json;
    using lgaed::windows::parse_lesson_json;
    const std::vector<std::pair<std::string,std::vector<std::string>>> plans{
        {"lesson_primer_programa_y_salida.json",{"ejecucion_original_consigna","ejecucion_original_editor"}},
        {"lesson_texto_secuencia_y_formato.json",{"ejecucion_original_texto_secuencia_y_formato_consigna","ejecucion_original_texto_secuencia_y_formato_editor"}},
        {"lesson_fuente_compilacion_y_diagnostico.json",{"ejecucion_original_fuente_compilacion_y_diagnostico_consigna","ejecucion_original_fuente_compilacion_y_diagnostico_editor"}}};
    std::size_t useful = 0;
    for (const auto& [filename, ids] : plans) {
        const auto before = lgaed::test::load_pre_retirement_lesson(filename);
        const auto after = lgaed::test::retire_lesson(before,ids);
        check(after.retired_blocks.size() == 2 && after.state_layout.retired.size() == 2,
            "Retired U1 instructions/editor lost their complete archive");
        check(after.compiler_puzzles_required && after.state_layout.lineage == before.state_layout.lineage &&
                after.state_layout.legacy_lineage == before.state_layout.legacy_lineage &&
                after.state_layout.signatures == before.state_layout.signatures,
            "Retirement changed the family, original address or activity contract");
        check(after.state_layout.retirement_history.size()==1,"Retirement lost its previous reading presentation");
        const auto& history=after.state_layout.retirement_history.front();
        const auto old_root=JsonObject::Parse(winrt::to_hstring(before.raw_json));
        const auto old_snapshot=JsonObject::Parse(winrt::to_hstring(history.raw_json));
        check(history.pages.size()==before.pages.size()&&history.current.size()==before.state_layout.current.size()&&
                history.compiler_puzzles_required==before.compiler_puzzles_required&&
                old_snapshot.GetNamedArray(L"pages").Stringify()==old_root.GetNamedArray(L"pages").Stringify()&&
                old_snapshot.GetNamedArray(L"current").Stringify()==old_root.GetNamedObject(L"stateLayout").GetNamedArray(L"current").Stringify()&&
                old_snapshot.GetNamedArray(L"activityAfter").Stringify()==old_root.GetNamedArray(L"activityAfter").Stringify(),
            "Retirement reading archive lost original wording, identity order, hidden-editor dependencies or flow");
        for(std::size_t p=0;p<history.pages.size();++p)for(std::size_t b=0;b<history.pages[p].blocks.size();++b)
            check(history.pages[p].blocks[b].stable_id==before.pages[p].blocks[b].stable_id&&
                    history.pages[p].blocks[b].text==before.pages[p].blocks[b].text&&
                    history.pages[p].blocks[b].question==before.pages[p].blocks[b].question,
                "Historical reading payloads were replaced with the active text");
        for (const auto& page : after.pages) for (const auto& block : page.blocks) {
            const auto position = stable_block_position(before,block.stable_id);
            check(position.has_value(),"Retirement manufactured a new active identity");
            const auto& old = before.pages[position->page].blocks[position->block];
            check(exercise_key(before,old) == exercise_key(after,block) &&
                    draft_key("programming",before,old) == draft_key("programming",after,block),
                "A useful activity moved to a different saved response or draft slot");
            if (block.type == BlockType::cpp_compiler) ++useful;
        }
        for (const auto& archived : after.retired_blocks) {
            const auto position = stable_block_position(before,archived.stable_id);
            check(position.has_value() && retired_block(after,archived.stable_id) &&
                    !stable_block_position(after,archived.stable_id),"Archived identity is still an active exercise");
            const auto& old = before.pages[position->page].blocks[position->block];
            check(JsonObject::Parse(winrt::to_hstring(old.raw_json)).Stringify() ==
                    JsonObject::Parse(winrt::to_hstring(archived.raw_json)).Stringify() &&
                    draft_key("programming",before,old) == draft_key("programming",after,archived),
                "An archived compiler contract or original draft address changed");
            check(active_block_successor(after,archived.stable_id).has_value(),
                "Reader bookmark inside a retired block has no active destination");
        }
        const auto progress = lgaed::windows::parse_lesson_progress_metadata(after.raw_json);
        check(progress.compiler_puzzles_required && progress.retired_blocks.size() == 2,
            "Catalog metadata lost explicit mandatory-flow or retired activity ownership");
        for (const auto& page : progress.pages) for (const auto& block : page.blocks)
            if (block.type == BlockType::cpp_compiler) check(block.output_check.has_value(),
                "Catalog discarded a compiler's actual authored completion objective");
        auto damaged = JsonObject::Parse(winrt::to_hstring(after.raw_json));
        auto retired = damaged.GetNamedObject(L"stateLayout").GetNamedArray(L"retired");
        auto editor = retired.GetAt(1).GetObject().GetNamedObject(L"block");
        editor.Insert(L"initialCode",JsonValue::CreateStringValue(L"int changed;"));
        rejects([&]{(void)parse_lesson_json(winrt::to_string(damaged.Stringify()));},
            "Retirement bypassed the original compiler-source contract");
        for (const auto invalid : {"null","1",R"("true")","[]","{}"})
            rejects([&]{(void)parse_lesson_json(root_property(after.raw_json,"compilerPuzzlesRequired",invalid));},
                "Mandatory compiler-flow flag accepted a nonboolean value");
        auto invalid_graph=JsonObject::Parse(winrt::to_hstring(after.raw_json));
        auto previous=invalid_graph.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory").GetAt(0).GetObject();
        JsonArray unknown;unknown.Append(JsonValue::CreateStringValue(L"unrelated_activity"));
        previous.GetNamedArray(L"activityAfter").GetAt(0).GetObject().Insert(L"requires",unknown);
        rejects([&]{(void)parse_lesson_json(winrt::to_string(invalid_graph.Stringify()));},
            "A historical reading relationship referenced an unrelated activity");
        auto old_archive=JsonObject::Parse(winrt::to_hstring(after.raw_json));old_archive.GetNamedObject(L"stateLayout").Remove(L"retirementHistory");
        check(parse_lesson_json(winrt::to_string(old_archive.Stringify())).retired_blocks.size()==2,
            "Older compatible retired archives unexpectedly require invented reading provenance");
        auto added_root=JsonObject::Parse(winrt::to_hstring(after.raw_json));
        auto added_layout=added_root.GetNamedObject(L"stateLayout");
        constexpr auto added_id=L"qa_addition_after_retirement";
        std::size_t reserved=after.state_layout.origin.size();
        for(const auto& [locale,family]:after.state_layout.family){(void)locale;reserved=std::max(reserved,family.origin.size());}
        for(const auto& addition:after.state_layout.additions)reserved=std::max(reserved,addition.storage.page+1);
        JsonObject addition,storage;
        addition.Insert(L"id",JsonValue::CreateStringValue(added_id));
        addition.Insert(L"sha256",JsonValue::CreateStringValue(winrt::to_hstring(
            after.state_layout.signatures.at(after.retired_blocks.front().stable_id))));
        storage.Insert(L"page",JsonValue::CreateNumberValue(static_cast<double>(reserved)));
        storage.Insert(L"block",JsonValue::CreateNumberValue(0));addition.Insert(L"storage",storage);
        addition.Insert(L"progressPage",JsonValue::CreateStringValue(winrt::to_hstring(after.state_layout.current.front().id)));
        added_layout.GetNamedArray(L"additions").Append(addition);
        added_layout.GetNamedArray(L"current").GetAt(0).GetObject().GetNamedArray(L"blocks").Append(JsonValue::CreateStringValue(added_id));
        added_root.GetNamedArray(L"pages").GetAt(0).GetObject().GetNamedArray(L"blocks").Append(
            JsonObject::Parse(winrt::to_hstring(after.retired_blocks.front().raw_json)));
        const auto added=parse_lesson_json(winrt::to_string(added_root.Stringify()));
        const auto added_position=stable_block_position(added,"qa_addition_after_retirement");
        check(added_position&&added.pages[added_position->page].blocks[added_position->block].address.page==reserved&&
                added.state_layout.retirement_history.front().sha256==history.sha256,
            "A later v2 addition was rejected or rewrote the previous reading presentation");
        auto missing_original=JsonObject::Parse(added_root.Stringify());
        auto missing_snapshot=missing_original.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory").GetAt(0).GetObject();
        missing_snapshot.GetNamedArray(L"pages").GetAt(0).GetObject().GetNamedArray(L"blocks").RemoveAt(0);
        missing_snapshot.GetNamedArray(L"current").GetAt(0).GetObject().GetNamedArray(L"blocks").RemoveAt(0);
        rejects([&]{(void)parse_lesson_json(winrt::to_string(missing_original.Stringify()));},
            "An original reading identity was incorrectly treated as a later addition");
    }
    check(useful == 6,"U1 retirement lost an independent useful compiler activity");
    const auto final = lgaed::windows::load_lesson(first.source_path.parent_path()/"lesson_resolver_probar_y_recuperar.json");
    for (const auto& page : final.pages) for (const auto& block : page.blocks) if (block.type == BlockType::cpp_compiler) ++useful;
    check(useful == 8,"The eight useful U1 compiler practices were not preserved");
    std::cout << "Retired U1 compiler contracts, original slots, active bookmarks and eight useful editors: PASS\n";
}
void integrated_retirement_contract_tests() {
    using namespace winrt::Windows::Data::Json;
    const auto folder=std::filesystem::path(lgaed::build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion";
    struct Plan {
        const char* filename;
        std::array<std::string,2> compiler_ids;
        std::vector<std::string> reading_ids;
        std::string_view refined_order;
    };
    const std::array<Plan,3> plans{{
        {"lesson_primer_programa_y_salida.json",{"ejecucion_original_consigna","ejecucion_original_editor"},
            {"block_0005_0002","block_0005_0008"},"block_0003_0002"},
        {"lesson_texto_secuencia_y_formato.json",{"ejecucion_original_texto_secuencia_y_formato_consigna","ejecucion_original_texto_secuencia_y_formato_editor"},
            {"block_0007_0002","block_0007_0003"},"block_0004_0002"},
        {"lesson_fuente_compilacion_y_diagnostico.json",{"ejecucion_original_fuente_compilacion_y_diagnostico_consigna","ejecucion_original_fuente_compilacion_y_diagnostico_editor"},{},{}}
    }};
    std::size_t useful=0;
    for(const auto& plan:plans){
        const auto before=lgaed::test::load_pre_retirement_lesson(plan.filename);
        const auto current=lgaed::windows::load_lesson(folder/plan.filename);
        std::set<std::string> expected_ids(plan.compiler_ids.begin(),plan.compiler_ids.end()),actual_ids,layout_ids;
        expected_ids.insert(plan.reading_ids.begin(),plan.reading_ids.end());
        for(const auto& block:current.retired_blocks)actual_ids.insert(block.stable_id);
        for(const auto& record:current.state_layout.retired)layout_ids.insert(record.id);
        check(current.compiler_puzzles_required&&current.retired_blocks.size()==expected_ids.size()
            &&current.state_layout.retired.size()==expected_ids.size()&&actual_ids==expected_ids&&layout_ids==expected_ids
            &&current.state_layout.retirement_history.size()==(plan.reading_ids.empty()?1u:2u),
            "The integrated U1 asset changed mandatory objectives, its exact archived identities or retirement batches");
        check(current.state_layout.retirement_history.front().retired==std::vector<std::string>(plan.compiler_ids.begin(),plan.compiler_ids.end())
            &&(plan.reading_ids.empty()||current.state_layout.retirement_history.back().retired==plan.reading_ids),
            "Integrated retirement merged or rewrote the published compiler and reading archive batches");
        check(current.state_layout.lineage==before.state_layout.lineage&&current.state_layout.legacy_lineage==before.state_layout.legacy_lineage
            &&current.state_layout.signatures==before.state_layout.signatures,
            "Integrated retirement changed historical state ownership");
        const auto archived=JsonObject::Parse(winrt::to_hstring(current.state_layout.retirement_history.front().raw_json));
        const auto historical=JsonObject::Parse(winrt::to_hstring(before.raw_json));
        check(archived.GetNamedArray(L"pages").Stringify()==historical.GetNamedArray(L"pages").Stringify()
            &&archived.GetNamedArray(L"current").Stringify()==historical.GetNamedObject(L"stateLayout").GetNamedArray(L"current").Stringify(),
            "The integrated archive differs from its independently pinned d1933ed presentation");
        rejects([&]{(void)lgaed::test::retire_lesson(current,plan.compiler_ids);},"An integrated asset could silently retire the same identities twice");
        if(!plan.reading_ids.empty())rejects([&]{(void)lgaed::test::retire_lesson(current,plan.reading_ids);},
            "An integrated asset could silently retire the same reading references twice");
        for(const auto& identity:expected_ids)check(retired_block(current,identity)&&!stable_block_position(current,identity)
            &&active_block_successor(current,identity).has_value(),"An integrated retired identity remained active or lost its successor");
        for(const auto& block:current.retired_blocks){
            const auto origin=stable_block_position(before,block.stable_id);check(origin.has_value(),"Integrated retirement invented an archived identity");
            const auto& old=before.pages[origin->page].blocks[origin->block];
            check(block.type==old.type&&JsonObject::Parse(winrt::to_hstring(block.raw_json)).Stringify()==JsonObject::Parse(winrt::to_hstring(old.raw_json)).Stringify()
                &&exercise_key(current,block)==exercise_key(before,old)&&draft_key("programming",current,block)==draft_key("programming",before,old),
                "An integrated archived compiler or reading reference lost its complete published source or historical slots");
        }
        check(current.state_layout.order_refinements.size()==(plan.refined_order.empty()?0u:1u),
            "Integrated retirement lost or added a grouped ordering provenance record");
        if(!plan.refined_order.empty()){
            const auto position=stable_block_position(current,plan.refined_order),origin=stable_block_position(before,plan.refined_order);
            check(position&&origin,"An integrated refined ordering lost its original activity identity");
            const auto& block=current.pages[position->page].blocks[position->block];
            const auto& old=before.pages[origin->page].blocks[origin->block];
            check(block.order_refinement&&block.order_refinement->id==plan.refined_order
                &&block.order_refinement->original_fragments==old.fragments&&block.order_refinement->original_correct_order==old.correct_order
                &&JsonObject::Parse(winrt::to_hstring(block.order_refinement->raw_json)).Stringify()==JsonObject::Parse(winrt::to_hstring(old.raw_json)).Stringify(),
                "Integrated line refinement changed its independently pinned original grouped ordering");
            bool preserved=false;
            for(const auto& page:current.state_layout.retirement_history.back().pages)for(const auto& archived_order:page.blocks)
                if(archived_order.stable_id==plan.refined_order)preserved=order_representation_frame(archived_order)==order_representation_frame(block);
            check(preserved,"The later reading archive lost the already-refined ordering representation");
        }
        for(const auto& page:current.pages)for(const auto& block:page.blocks){
            const auto origin=stable_block_position(before,block.stable_id);check(origin.has_value(),"Integrated retirement invented an active identity");
            const auto& old=before.pages[origin->page].blocks[origin->block];
            check(exercise_key(current,block)==exercise_key(before,old)&&draft_key("programming",current,block)==draft_key("programming",before,old),
                "An integrated active block lost its historical response or draft slot");
            if(block.type==BlockType::cpp_compiler){++useful;check(block.output_check.has_value()
                &&reference_objective_frame(current,block)==reference_objective_frame(before,old),
                "A useful integrated compiler changed its program, answer or fresh-execution objective");}
        }
        for(const auto& relation:current.activity_after)for(const auto& required:relation.prerequisites)
            check(!retired_block(current,required),"A current useful activity still requires a retired execution");
    }
    const auto last=lgaed::windows::load_lesson(folder/"lesson_resolver_probar_y_recuperar.json");
    check(last.compiler_puzzles_required,"The two final useful U1 compiler objectives stopped being mandatory");
    for(const auto& page:last.pages)for(const auto& block:page.blocks)if(block.type==BlockType::cpp_compiler){
        ++useful;check(block.output_check.has_value(),"A final useful U1 compiler lost its objective");}
    check(useful==8,"Current U1 no longer contains exactly eight useful mandatory compilers");
    std::cout<<"Integrated U1 assets: exact compiler/reading retirement batches and d1933ed source, preserved ordering provenance/slots, eight mandatory useful objectives and no repeated retirement: PASS\n";
}
void standard_error_objective_tests(const Lesson& real_lesson) {
    using lgaed::windows::parse_lesson_json;
    using winrt::Windows::Data::Json::JsonObject;
    using winrt::Windows::Data::Json::JsonValue;
    constexpr auto source = R"json({"id":"stderr_objective","title":"Runtime channels","pages":[{"blocks":[{"type":"cpp_compiler","initialCode":"int main(){}","outputCheck":{"cases":[{"standardInput":"","outputs":["Indice invalido:Fin"],"explanation":"Both objectives checked.","standardError":"nonBlank"}]}}]}]})json";
    const auto model = parse_lesson_json(source);
    const auto& block = model.pages.front().blocks.front();
    const auto& goal = *block.output_check;
    check(goal.cases.front().standard_error == StandardErrorExpectation::non_blank, "Runtime stderr requirement was not loaded");
    FreshExecution execution{7,true,true,0,"","Indice invalido:Fin",{}};
    auto result = evaluate_output_check(goal,execution,7);
    check(result.verdict == OutputVerdict::incorrect && result.stdout_correct && !result.stderr_correct &&
        !result.feedback.empty() && result.feedback != goal.cases.front().explanation,
        "Stdout-only success falsely completed or revealed the complete stderr objective");
    for (const auto whitespace : {"", " ", "\t\r\n\v\f "}) {
        execution.standard_error = whitespace;
        result = evaluate_output_check(goal,execution,7);
        check(result.verdict == OutputVerdict::incorrect && result.stdout_correct && !result.stderr_correct,
            "Whitespace-only runtime stderr counted as a diagnostic");
    }
    for (const auto diagnostic : {"invalid vector subscript", "vector::_M_range_check: __n (which is 5)", " \tMSVC runtime diagnostic\r\n"}) {
        execution.standard_error = diagnostic;
        result = evaluate_output_check(goal,execution,7);
        check(result.verdict == OutputVerdict::correct && result.stdout_correct && result.stderr_correct &&
            result.feedback == goal.cases.front().explanation,
            "A library-specific runtime diagnostic was rejected or its complete feedback was lost");
    }
    execution.standard_output += '\n';
    result = evaluate_output_check(goal,execution,7);
    check(result.verdict == OutputVerdict::incorrect && !result.stdout_correct && result.stderr_correct,
        "Runtime stderr hid a significant stdout character difference");
    execution.standard_output = "Indice invalido:Fin";
    for (const auto revision : {6u,8u})
        check(evaluate_output_check(goal,execution,revision).verdict == OutputVerdict::unavailable,
            "A stale runtime diagnostic was reused after editing");
    execution.compilation_succeeded = false;
    check(evaluate_output_check(goal,execution,7).verdict == OutputVerdict::unavailable,
        "Build stderr was treated as a successful runtime diagnostic");
    execution.compilation_succeeded = true; execution.execution_succeeded = false;
    check(evaluate_output_check(goal,execution,7).verdict == OutputVerdict::unavailable,
        "A failed runtime was credited for its stderr");
    execution.execution_succeeded = true; execution.exit_code = 1;
    check(evaluate_output_check(goal,execution,7).verdict == OutputVerdict::unavailable,
        "A nonzero exit status was credited for its stderr");
    execution.exit_code = 0;
    auto legacy = goal; legacy.cases.front().standard_error = StandardErrorExpectation::ignored;
    execution.standard_error.clear();
    check(evaluate_output_check(legacy,execution,7).verdict == OutputVerdict::correct,
        "An absent stderr requirement changed the historical stdout contract");
    for (const auto invalid : {R"("nonEmpty")", R"("ignored")", R"("nonblank")", R"("")", "null", "true", "42", "[]", "{}"}) {
        auto corrupt = std::string(source); replace_once(corrupt,R"("nonBlank")",invalid);
        rejects([&] { (void)parse_lesson_json(corrupt); }, "An unsupported runtime stderr requirement was accepted");
    }
    const auto targeted = parse_lesson_json(R"json({"id":"targeted_stderr","title":"Targeted channels","pages":[{"blocks":[{"type":"cpp_compiler","projectFiles":[{"path":"main.cpp","initialCode":"int main(){}"},{"path":"CMakeLists.txt","initialCode":"add_executable(first main.cpp)\nadd_executable(second main.cpp)"}],"cmake":{"target":"first"},"outputCheck":{"cases":[{"standardInput":"","outputs":["same"],"target":"first"},{"standardInput":"","outputs":["same"],"target":"second","standardError":"nonBlank"}]}}]}]})json");
    execution.standard_output = "same"; execution.executable = "first";
    const auto& targeted_goal = *targeted.pages.front().blocks.front().output_check;
    check(evaluate_output_check(targeted_goal,execution,7).verdict == OutputVerdict::correct,
        "A different CMake target inherited the stderr objective");
    execution.executable = "second";
    check(evaluate_output_check(targeted_goal,execution,7).verdict == OutputVerdict::incorrect,
        "The target-specific stderr objective was ignored");
    execution.standard_error = "diagnostic";
    check(evaluate_output_check(targeted_goal,execution,7).verdict == OutputVerdict::correct,
        "The selected CMake target could not complete its own stderr objective");
    execution.executable = "third";
    check(evaluate_output_check(targeted_goal,execution,7).verdict == OutputVerdict::unavailable,
        "A diagnostic from an unauthored target completed another objective");

    // Adding the new criterion preserves every original slot/draft signature;
    // only native completion evidence must be obtained again for the full goal.
    auto upgraded_root = JsonObject::Parse(winrt::to_hstring(real_lesson.raw_json));
    std::optional<StatePosition> location;
    for (std::size_t p = 0; p < real_lesson.pages.size() && !location; ++p)
        for (std::size_t b = 0; b < real_lesson.pages[p].blocks.size(); ++b)
            if (real_lesson.pages[p].blocks[b].output_check) { location = StatePosition{p,b}; break; }
    check(location.has_value(), "The historical state fixture lost its compiler goal");
    upgraded_root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(location->page)).GetObject().GetNamedArray(L"blocks")
        .GetAt(static_cast<unsigned>(location->block)).GetObject().GetNamedObject(L"outputCheck").GetNamedArray(L"cases")
        .GetAt(0).GetObject().Insert(L"standardError",JsonValue::CreateStringValue(L"nonBlank"));
    const auto upgraded = parse_lesson_json(winrt::to_string(upgraded_root.Stringify()));
    const auto& before = real_lesson.pages[location->page].blocks[location->block];
    const auto& after = upgraded.pages[location->page].blocks[location->block];
    check(real_lesson.state_layout.lineage == upgraded.state_layout.lineage &&
        real_lesson.state_layout.signatures == upgraded.state_layout.signatures &&
        exercise_key(real_lesson,before) == exercise_key(upgraded,after) &&
        draft_key("programming",real_lesson,before) == draft_key("programming",upgraded,after),
        "Adding runtime stderr changed historical state addresses or drafts");
    check(reference_objective_frame(real_lesson,before) != reference_objective_frame(upgraded,after),
        "An old stdout proof could be inherited by the new complete objective");
    auto restored = after; restored.output_check->cases.front().standard_error = StandardErrorExpectation::ignored;
    check(reference_objective_frame(real_lesson,before) == reference_objective_frame(upgraded,restored),
        "Stdout-only objective framing changed despite the absent stderr requirement");
    std::cout << "Runtime stderr objectives, partial stdout, exact output, target/revision ownership and stable slots: PASS\n";
}
}
void reviewed_compiler_expansions_keep_the_original_contract() {
    using namespace winrt::Windows::Data::Json;using namespace lgaed;
    const auto directory=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/texto_interpretacion_y_flujos";
    check(test::compiler_revision_cases().size()==10,"The closed compiler revision inventory lost one of its ten exact owners");
    std::size_t late_aggregate_owners=0;
    for(const auto& plan:test::compiler_revision_cases()) {
        const auto integrated=windows::load_lesson(directory/plan.filename);
        const auto before=test::compiler_revision_before(integrated,plan.identity);const auto position=stable_block_position(before,plan.identity);
        check(position.has_value(),"A reviewed compiler disappeared from its published identity");
        const auto current=test::revise_compiler_lesson(before,plan.identity);const auto& old=before.pages[position->page].blocks[position->block];
        const auto& block=current.pages[position->page].blocks[position->block];
        const auto live=stable_block_position(integrated,plan.identity);
        check(live==position,"A compiler proposal changed its current authored identity");
        const auto live_fingerprint=test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(
            integrated.pages[live->page].blocks[live->block].raw_json)));
        if(integrated.pages[live->page].blocks[live->block].compiler_revision) {
            rejects([&]{(void)test::revise_compiler_lesson(integrated,plan.identity);},
                "An integrated compiler was accepted as a new historical transition");
            check(live_fingerprint==plan.after_full,"Historical compiler expansion did not reproduce the current authored payload");
        } else check(plan.allow_unexpanded_source&&live_fingerprint==plan.before_full,
            "An unintegrated proposal changed its independently pinned authored source");
        check(block.compiler_revision&&block.compiler_revision->sha256==plan.before_full&&block.compiler_revision->current_payload==plan.after_full&&
            block.compiler_revision->previous_output_check.cases.size()==old.output_check->cases.size()
            &&block.compiler_revision->previous_initial_code==old.initial_code,"The complete compiler expansion archive did not reach the model");
        auto expected_initial=old.initial_code;
        if(plan.initial_insert){const auto at=expected_initial.rfind("    return 0;\n}\n");
            check(at!=std::string::npos,"The reviewed starter lost its final return");expected_initial.insert(at,plan.initial_insert);}
        check(current.state_layout.lineage==before.state_layout.lineage&&exercise_key(current,block)==exercise_key(before,old)&&
            draft_key("programming",current,block)==draft_key("programming",before,old)&&block.initial_code==expected_initial&&block.standard_input==old.standard_input,
            "A compiler expansion changed family, address, program or prepared input");
        check(block.compiler_revision->starter_append==
            (plan.initial_insert?std::optional<std::string>{plan.initial_insert}:std::nullopt),
            "A compiler exposed an unreviewed starter insert or omitted its authenticated added calls");
        if(plan.initial_insert)check(old.initial_code.size()==7771&&block.initial_code.size()==7820
            &&state::sha256(old.initial_code)=="6377abdeb131d6cbed90bc1993da42604854f60669321a3ed236c835faeec2c6"
            &&state::sha256(block.initial_code)=="36a5c835a62bcb62134cbec7f5d0af63424b713f2d58b7f3ac8fccc21b1c6382"
            &&block.compiler_revision->starter_append->size()==49,
            "The second U11L15 starter changed outside its exact 49-byte fifth call");
        check(reference_objective_frame(current,block)!=reference_objective_frame(before,old),"An expanded compiler objective reused its historical execution seal");
        if(plan.reports_before){
            ++late_aggregate_owners;
            check(old.output_check&&block.output_check&&old.output_check->cases.size()==1&&block.output_check->cases.size()==1,
                "A late compiler battery turned its printed reports into separate execution cases");
            const auto& previous=old.output_check->cases.front();const auto& expanded=block.output_check->cases.front();
            check(previous.standard_input.empty()&&expanded.standard_input.empty()&&previous.outputs.size()==1&&expanded.outputs.size()==1,
                "A late in-program battery changed its aggregate stdin/stdout contract");
            const auto& original=previous.outputs.front();const auto& revised=expanded.outputs.front();
            const auto reports=[](std::string_view value){std::size_t count=0,at=0;
                while((at=value.find("Caso ",at))!=value.npos){++count;at+=5;}return count;};
            check(original.size()==plan.bytes_before&&revised.size()==plan.bytes_after
                &&static_cast<std::size_t>(std::count(original.begin(),original.end(),'\n'))==plan.lines_before
                &&static_cast<std::size_t>(std::count(revised.begin(),revised.end(),'\n'))==plan.lines_after
                &&reports(original)==plan.reports_before&&reports(revised)==plan.reports_after
                &&revised==original+plan.suffix,
                "A late compiler expansion changed a previous report, appended another suffix or confused reports with receipts");
            check(plan.initial_insert||(old.initial_code==block.initial_code&&old.initial_code.find("Caso 15")==std::string::npos),
                "The independent late compiler construction changed its original minimal starter");
        }
        const auto json=test::compiler_revision_json(before,plan.identity);const auto summary=windows::parse_lesson_progress_metadata(json,"es");
        const auto& summarized=summary.pages[position->page].blocks[position->block];
        check(summarized.compiler_revision&&summarized.compiler_revision->previous_initial_code==old.initial_code
            &&summarized.compiler_revision->starter_append==block.compiler_revision->starter_append
            &&summarized.initial_code==expected_initial,"Catalog metadata lost an expanded compiler objective or its exact old/new starter");
        std::string retirement;
        for(const auto& page:before.pages)for(const auto& candidate:page.blocks)if(retirement.empty()&&candidate.type==BlockType::paragraph)retirement=candidate.stable_id;
        const std::vector<std::string> retiring{retirement};check(!retirement.empty(),"Compiler revision QA needs an independent paragraph for reading history");
        const auto retired_before=test::retire_lesson(before,retiring,before.compiler_puzzles_required);
        const auto revised_after_retirement=test::revise_compiler_lesson(retired_before,plan.identity);
        const auto retired_after=test::retire_lesson(current,retiring,current.compiler_puzzles_required);
        for(const bool after:{false,true}) {
            const auto& archived=after?retired_after:revised_after_retirement;const auto& history=archived.state_layout.retirement_history.front();
            const auto& historical=history.pages[position->page].blocks[position->block];
            check(historical.compiler_revision.has_value()==after&&historical.output_check->cases.size()==(after?block.output_check->cases.size():old.output_check->cases.size()),
                "A reading archive confused the original and expanded compiler objective");
            check(historical.initial_code==(after?expected_initial:old.initial_code)
                &&(!after||(historical.compiler_revision->previous_initial_code==old.initial_code
                    &&historical.compiler_revision->starter_append==block.compiler_revision->starter_append)),
                "A reading archive lost the authenticated starter or exposed new calls as an old objective");
        }
        if(plan.reference_identity)for(const bool replace_compiler:{false,true}) {
            const auto mixed=JsonObject::Parse(winrt::to_hstring(retired_after.raw_json));
            const auto history=mixed.GetNamedObject(L"stateLayout").GetNamedArray(L"retirementHistory").GetAt(0).GetObject();
            const auto reference_position=stable_block_position(before,plan.reference_identity);
            const auto mixed_position=replace_compiler?position:reference_position;
            const auto& original=before.pages[mixed_position->page].blocks[mixed_position->block];
            history.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(mixed_position->page)).GetObject().GetNamedArray(L"blocks").SetAt(static_cast<unsigned>(mixed_position->block),JsonObject::Parse(winrt::to_hstring(original.raw_json)));
            const auto invalid=winrt::to_string(mixed.Stringify());
            rejects([&]{(void)windows::parse_lesson_json(invalid,"es");},"A historical snapshot mixed old/new compiler and reference versions");
            rejects([&]{(void)windows::parse_lesson_progress_metadata(invalid,"es");},"Catalog history accepted a mixed compiler/reference version");
        }
        for(const auto mode:{"archive","snapshot","payload","owner","proof","unmapped","reference-archive","reference-original","reference-source","compiler-only"}) {
            const auto reference_mode=std::string_view(mode).starts_with("reference-")||std::string_view(mode)=="compiler-only";
            if(reference_mode&&!plan.reference_identity)continue;
            const auto invalid=JsonObject::Parse(winrt::to_hstring(json));const auto metadata=invalid.GetNamedObject(L"stateLayout");
            const auto records=metadata.GetNamedArray(L"compilerRevisions");
            const auto expansion=records.GetAt(records.Size()-1).GetObject();
            check(winrt::to_string(expansion.GetNamedString(L"id"))==plan.identity,"Compiler negative case targets another revision archive");
            const auto target=invalid.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
            if(std::string_view(mode)=="archive")metadata.Remove(L"compilerRevisions");
            if(std::string_view(mode)=="snapshot")expansion.GetNamedObject(L"block").Insert(L"initialCode",JsonValue::CreateStringValue(L"unreviewed"));
            if(std::string_view(mode)=="payload")target.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetAt(0).GetObject().Insert(L"standardInput",JsonValue::CreateStringValue(L"unreviewed"));
            if(std::string_view(mode)=="owner")invalid.Insert(L"id",JsonValue::CreateStringValue(L"another_owner"));
            if(std::string_view(mode)=="proof")metadata.GetNamedArray(L"editorial").RemoveAtEnd();
            if(std::string_view(mode)=="unmapped")invalid.Remove(L"stateLayout");
            if(reference_mode) {
                const auto reference_position=stable_block_position(current,plan.reference_identity);
                const auto reference=invalid.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(reference_position->page)).GetObject().GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(reference_position->block)).GetObject();
                if(std::string_view(mode)=="reference-archive")expansion.Remove(L"references");
                if(std::string_view(mode)=="reference-original")expansion.GetNamedArray(L"references").GetAt(0).GetObject().GetNamedObject(L"block").Insert(L"code",JsonValue::CreateStringValue(L"changed original reference"));
                if(std::string_view(mode)=="reference-source")reference.Insert(L"code",JsonValue::CreateStringValue(L"unreviewed replacement program"));
                if(std::string_view(mode)=="compiler-only")reference.Insert(L"code",expansion.GetNamedArray(L"references").GetAt(0).GetObject().GetNamedObject(L"block").GetNamedValue(L"code"));
            }
            rejects([&]{(void)windows::parse_lesson_json(winrt::to_string(invalid.Stringify()),"es");},"An unreviewed compiler expansion bypassed provenance validation");
            rejects([&]{(void)windows::parse_lesson_progress_metadata(winrt::to_string(invalid.Stringify()),"es");},"Catalog projection accepted a forged compiler expansion");
        }
        if(plan.initial_insert)for(const auto mode:{"expected-only","starter-only","starter-changed"}){
            const auto invalid=JsonObject::Parse(winrt::to_hstring(json)),metadata=invalid.GetNamedObject(L"stateLayout");
            const auto target=invalid.GetNamedArray(L"pages").GetObjectAt(static_cast<unsigned>(position->page)).GetNamedArray(L"blocks").GetObjectAt(static_cast<unsigned>(position->block));
            if(std::string_view(mode)=="expected-only")target.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(old.initial_code)));
            else if(std::string_view(mode)=="starter-only")target.Insert(L"outputCheck",JsonObject::Parse(winrt::to_hstring(old.raw_json)).GetNamedValue(L"outputCheck"));
            else{auto changed=block.initial_code;replace_once(changed,"probar(\"2;2;\");","probar(\"2;3;\");");target.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(changed)));}
            const auto digest=test::compile_check_test_fingerprint(target);
            if(std::string_view(mode)=="expected-only")check(digest=="24819112ab0cd2468968824326b0b7eb4c445bc801dd1e64af7be08d6b3c7794",
                "The rejected expected-only fixture no longer addresses its exact superseded proposal");
            // Give the changed payload its honest digest: rejection must come
            // from the closed reviewed pair, not merely a stale proof string.
            for(const auto& value:metadata.GetNamedArray(L"editorial")){const auto proof=value.GetObject();
                if(winrt::to_string(proof.GetNamedString(L"id"))==plan.identity)proof.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(digest)));}
            const auto raw=winrt::to_string(invalid.Stringify());
            rejects([&]{(void)windows::parse_lesson_json(raw);},"A partial or modified starter/output pair became an authorized compiler revision");
            rejects([&]{(void)windows::parse_lesson_progress_metadata(raw);},"Catalog accepted a partial or modified starter/output pair");
        }
    }
    check(late_aggregate_owners==3,"The dedicated U11L15/L16 aggregate contracts were not all exercised");
    std::cout<<"PASS ten exact compiler expansions: immutable originals, distinct execution goals and stable addresses; paired starter/output/reference pins, historical reading and catalog projection, expected-only/partial/forged rejection; U11L15/L16 each retain one aggregate stdout execution case\n";
}
void reviewed_compile_diagnostics_require_the_intended_source_change() {
    using namespace lgaed;
    using namespace winrt::Windows::Data::Json;
    const auto folder=std::filesystem::path(build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals/datos_propios_y_estado_valido";
    for(const auto& plan:test::compile_check_cases) {
        const auto path=folder/plan.filename;const auto bytes=state::read_bytes(path);
        const auto integrated=windows::load_lesson(path);
        const auto original=test::load_pre_compile_check_lesson(plan.filename);
        const auto position=stable_block_position(original,plan.identity);
        check(position.has_value(),"A reviewed compile diagnostic lost its original editor");
        const auto& prior=original.pages[position->page].blocks[position->block];
        check(!has_compiler_objective(prior),"A free historical compiler was silently assigned a new objective");
        const auto live=stable_block_position(integrated,plan.identity);
        check(live==position,"The integrated diagnostic moved from its stable historical identity");
        const auto& authored=integrated.pages[live->page].blocks[live->block];
        check(authored.compile_check&&authored.compile_check->diagnostic==plan.diagnostic&&!authored.compile_check->explanation.empty(),
            "The current authored diagnostic lost its contract or Work explanation");
        const auto lesson=test::with_compile_check(original,plan.identity,plan.diagnostic,authored.compile_check->explanation);
        const auto& block=lesson.pages[position->page].blocks[position->block];
        check(test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(block.raw_json)))==
            test::compile_check_test_fingerprint(JsonObject::Parse(winrt::to_hstring(authored.raw_json))),
            "Historical adoption did not reconstruct the complete integrated diagnostic payload");
        rejects([&]{(void)test::with_compile_check(integrated,plan.identity,plan.diagnostic);},
            "A diagnostic was silently adopted twice");
        check(has_compiler_objective(block)&&block.compile_check&&!block.output_check&&
                block.initial_code==prior.initial_code&&lesson.state_layout.signatures==original.state_layout.signatures&&
                exercise_key(lesson,block)==exercise_key(original,prior)&&
                draft_key("programming",lesson,block)==draft_key("programming",original,prior),
            "Adding the compile diagnostic changed an original program, response or draft address");
        const auto metadata=windows::parse_lesson_progress_metadata(lesson.raw_json);
        check(metadata.pages[position->page].blocks[position->block].compile_check==block.compile_check,
            "The catalog projection discarded the compiler diagnostic objective");
        const auto alternative=test::with_compile_check(original,plan.identity,plan.diagnostic,"Different editorial explanation.");
        check(reference_objective_frame(lesson,block)==reference_objective_frame(alternative,alternative.pages[position->page].blocks[position->block]),
            "Editing only the diagnostic explanation changed execution proof identity");
        auto candidate=block.initial_code;const auto insertion=candidate.rfind("    return 0;");
        check(insertion!=candidate.npos,"The reviewed diagnostic source lost its own final return");
        candidate.insert(insertion,std::string("    ")+plan.statement+'\n');
        const auto prepared=prepare_compile_diagnostic_source(block,candidate);
        check(prepared&&prepared->candidate_source==candidate&&prepared->expected_msvc_error==plan.error&&
                prepared->first_line==prepared->last_line&&prepared->first_line>1&&
                std::ranges::count(prepared->control_source,'\n')==std::ranges::count(candidate,'\n')&&
                prepared->control_source.substr(0,insertion)==candidate.substr(0,insertion)&&
                prepared->control_source.substr(insertion+4+std::string_view(plan.statement).size())==candidate.substr(insertion+4+std::string_view(plan.statement).size()),
            "The diagnostic control altered surrounding source, physical rows or candidate identity");
        check(!prepare_compile_diagnostic_source(block,prepared->control_source)&&
                !prepare_compile_diagnostic_source(block,block.initial_code),
            "The initial/control program counted as obtaining the intentional rejection");
        check(prepare_compile_diagnostic_source(block,"// comment containing "+std::string(plan.statement)+"\n"+candidate).has_value(),
            "An irrelevant comment changed the reviewed token contract");
        auto duplicate=candidate;duplicate.insert(insertion,std::string(plan.statement)+'\n');
        auto malformed=candidate;malformed.insert(insertion,"not_valid_cpp ???;\n");
        auto directive=candidate;directive.insert(0,"#line 900 \"forged.cpp\"\n");
        auto commented=block.initial_code;commented.insert(insertion,std::string("// ")+plan.statement+'\n');
        auto in_expression=block.initial_code;in_expression.insert(insertion+11,plan.statement);
        auto before_main=block.initial_code;before_main.insert(before_main.find("int main()"),std::string(plan.statement)+'\n');
        auto changed=candidate;replace_once(changed,"return 0;","return 1;");
        for(const auto& invalid:{duplicate,malformed,directive,commented,in_expression,before_main,changed,candidate+"// splice\\\n",candidate+std::string(1,'\0')})
            check(!prepare_compile_diagnostic_source(block,invalid),
                "An unrelated source edit, nonstatement insertion, duplicate, comment or directive satisfied a reviewed diagnostic");
        auto incompatible=block;incompatible.standard_input="";
        check(!prepare_compile_diagnostic_source(incompatible,candidate),"A diagnostic gained runtime input semantics");
        if(std::string_view(plan.diagnostic)=="const-member-call") {
            auto mutable_object=candidate;replace_once(mutable_object,"const Dimension fija","Dimension fija");
            check(!prepare_compile_diagnostic_source(block,mutable_object),"Removing const was accepted as the const rejection activity");
            auto inside_declaration=block.initial_code;
            inside_declaration.insert(inside_declaration.find("fija{2, 5}")+std::string_view("fija{2, 5}").size(),plan.statement);
            check(!prepare_compile_diagnostic_source(block,inside_declaration),"A diagnostic statement inserted before a declaration's semicolon was accepted");
            auto multiline=block.initial_code;multiline.insert(insertion,"    fija /* comment */ . cambiar_a (\n4 , 2\n) ;\n");
            const auto spanned=prepare_compile_diagnostic_source(block,multiline);
            check(spanned&&spanned->last_line==spanned->first_line+2,"Whitespace/comments or multiline diagnostic location were lost");
        }else if(std::string_view(plan.diagnostic).starts_with("explicit-")) {
            auto implicit=candidate;replace_once(implicit,"explicit Contador","Contador");
            check(!prepare_compile_diagnostic_source(block,implicit),"Removing explicit was accepted as its rejection activity");
        }else {
            auto swapped=candidate;replace_once(swapped,"Caja(Ancho ancho, Alto alto)","Caja(Alto alto, Ancho ancho)");
            check(!prepare_compile_diagnostic_source(block,swapped),"Changing the named-argument interface satisfied the swapped-argument activity");
        }
        for(const auto mode:{"unknown","blank","unknown-key","input","output","project","source","owner","proof","unmapped"}) {
            const auto root=JsonObject::Parse(winrt::to_hstring(lesson.raw_json)),layout=root.GetNamedObject(L"stateLayout");
            const auto source=root.GetNamedArray(L"pages").GetAt(static_cast<unsigned>(position->page)).GetObject()
                .GetNamedArray(L"blocks").GetAt(static_cast<unsigned>(position->block)).GetObject();
            const auto contract=source.GetNamedObject(L"compileCheck");
            if(std::string_view(mode)=="unknown")contract.Insert(L"diagnostic",JsonValue::CreateStringValue(L"any-build-error"));
            if(std::string_view(mode)=="blank")contract.Insert(L"explanation",JsonValue::CreateStringValue(L" \t\r\n"));
            if(std::string_view(mode)=="unknown-key")contract.Insert(L"acceptAnyFailure",JsonValue::CreateBooleanValue(true));
            if(std::string_view(mode)=="input")source.Insert(L"initialStandardInput",JsonValue::CreateStringValue(L""));
            if(std::string_view(mode)=="output")source.Insert(L"outputCheck",JsonObject::Parse(L"{\"cases\":[{\"standardInput\":\"\",\"outputs\":[\"\"]}]}"));
            if(std::string_view(mode)=="project")source.Insert(L"projectFiles",JsonArray{});
            if(std::string_view(mode)=="source")source.Insert(L"initialCode",JsonValue::CreateStringValue(L"int main(){}"));
            if(std::string_view(mode)=="owner")root.Insert(L"id",JsonValue::CreateStringValue(L"unreviewed_owner"));
            const auto editorial=layout.GetNamedArray(L"editorial");
            if(std::string_view(mode)=="proof")editorial.RemoveAtEnd();
            else editorial.GetAt(editorial.Size()-1).GetObject().Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(source))));
            if(std::string_view(mode)=="unmapped")root.Remove(L"stateLayout");
            const auto invalid=winrt::to_string(root.Stringify());
            rejects([&]{(void)windows::parse_lesson_json(invalid);},"An unreviewed compiler diagnostic bypassed source/owner/schema/provenance validation");
            rejects([&]{(void)windows::parse_lesson_progress_metadata(invalid);},"Catalog projection accepted an unreviewed compiler diagnostic");
        }
        auto flow=lesson;flow.reference_after.clear();flow.activity_after.clear();
        Block reference;reference.type=BlockType::paragraph;reference.stable_id="qa_diagnostic_reference";
        Block puzzle;puzzle.type=BlockType::multiple_choice;puzzle.stable_id="qa_diagnostic_puzzle";
        flow.pages.push_back({"qa_after_diagnostic",{reference,puzzle},{}});
        flow.reference_after.push_back({block.stable_id,{reference.stable_id}});
        flow.activity_after.push_back({puzzle.stable_id,{block.stable_id}});
        validate_reference_relationships(flow);validate_activity_relationships(flow);
        check(compiler_is_puzzle_prerequisite(flow,block)&&
                reference_activity(flow,flow.pages.back().blocks.front())==position&&
                activity_prerequisites(flow,flow.pages.back().blocks.back())==std::vector<StatePosition>{*position},
            "A compiler diagnostic could not own the existing reference, prerequisite and reader gate");
        check(state::read_bytes(path)==bytes,"Diagnostic QA changed an authored lesson asset");
    }
    std::cout<<"PASS five compile diagnostic contracts: original owners/sources/addresses, catalog/objective identity, one real statement inside main, comments/multiline ranges, interface protection and forged contract refusal\n";
}
void inline_feedback_optional_contract_tests() {
    using namespace winrt::Windows::Data::Json;
    using lgaed::windows::parse_lesson_json;
    const auto assets=std::filesystem::path(lgaed::build::source_root)/"app/src/main/assets/content/es/programming/cpp_fundamentals";
    struct Location {const char* file;std::size_t page,block;};
    const std::array<Location,9> locations{{
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",7,6},
        {"objetos_y_estado/lesson_enteros_y_aproximaciones.json",3,3},
        {"objetos_y_estado/lesson_declarar_e_inicializar.json",2,4},
        {"objetos_y_estado/lesson_estado_asignacion_y_copia.json",5,4},
        {"objetos_y_estado/lesson_nombres_con_intencion.json",3,4},
        {"objetos_y_estado/lesson_conversiones_y_deduccion.json",3,5},
        {"objetos_y_estado/lesson_cadenas_y_texto.json",3,4},
        {"objetos_y_estado/lesson_caracteres_y_valores_logicos.json",2,3},
        {"programas_y_ejecucion/lesson_texto_secuencia_y_formato.json",4,1}
    }};
    const auto isolated=[](const JsonObject& block){return parse_lesson_json(
        "{\"id\":\"qa_inline_feedback_contract\",\"title\":\"QA\",\"pages\":[{\"blocks\":["+
        winrt::to_string(block.Stringify())+"]}]}");};
    std::vector<Block> current;
    for(const auto& location:locations){
        const auto path=assets/location.file;const auto bytes=lgaed::state::read_bytes(path);
        const auto lesson=lgaed::windows::load_lesson(path);
        const auto& block=lesson.pages.at(location.page).blocks.at(location.block);
        check(block.type==BlockType::inline_fill_code,"The authored inline-feedback location changed its exercise family");
        const auto authored=JsonObject::Parse(winrt::to_hstring(block.raw_json));
        check(!block.review_feedback.empty()&&!block.explanation.empty()&&
            block.review_feedback==winrt::to_string(authored.GetNamedString(L"reviewFeedback"))&&
            block.explanation==winrt::to_string(authored.GetNamedString(L"explanation")),
            "The current inline-feedback parser lost an authored guide or complete success explanation");
        auto wrong=block.answers;wrong.front()="QA_INCORRECT_INLINE_ANSWER";
        check(!evaluate_hole_answers(block,wrong).correct&&evaluate_hole_answers(block,wrong).feedback==block.review_feedback&&
            evaluate_hole_answers(block,block.answers).correct&&evaluate_hole_answers(block,block.answers).feedback==block.explanation,
            "An inline exercise exposed the success explanation before success or lost its specific incorrect guide");
        for(std::size_t hole=0;hole<block.answer_alternatives.size();++hole)
            for(const auto& alternative:block.answer_alternatives[hole]){
                auto answers=block.answers;answers[hole]=alternative;
                check(evaluate_hole_answers(block,answers).correct&&evaluate_hole_answers(block,answers).feedback==block.explanation,
                    "Adding inline feedback changed an authored per-hole alternative");
            }
        auto absent=JsonObject::Parse(winrt::to_hstring(block.raw_json));absent.Remove(L"reviewFeedback");absent.Remove(L"explanation");
        const auto optional=isolated(absent).pages.front().blocks.front();
        check(evaluate_hole_answers(optional,optional.answers).correct&&evaluate_hole_answers(optional,optional.answers).feedback.empty()&&
            !evaluate_hole_answers(optional,wrong).correct&&evaluate_hole_answers(optional,wrong).feedback.empty(),
            "Absent optional inline feedback changed the historical verdict or invented authored content");
        check(lgaed::state::read_bytes(path)==bytes,"Inline-feedback QA modified an authored lesson asset");
        current.push_back(block);
    }
    std::cout<<"PASS current inline-feedback fields, complete guides/explanations, answers/alternatives and optional absence across nine actual locations\n";
    for(const auto& block:current)for(const auto* field:{L"reviewFeedback",L"explanation"}){
        for(const auto* blank:{L"",L" \t\r\n",L"\u00a0",L"\u2007",L"\u202f",L"\u2028",L"\u3000",L"\u001c"}){
            auto malformed=JsonObject::Parse(winrt::to_hstring(block.raw_json));
            malformed.Insert(field,JsonValue::CreateStringValue(blank));
            rejects([&]{(void)isolated(malformed);},"A present blank inline guide/explanation was accepted instead of failing before an answer could be evaluated");
        }
        // Kotlin Char.isWhitespace || Character.isSpaceChar excludes these format characters.
        // They remain authored bytes; the parser must not silently normalize them away.
        for(const auto* nonblank:{L"Guía válida.",L"\u200b",L"\ufeff",L"\u0085"}){
            auto valid=JsonObject::Parse(winrt::to_hstring(block.raw_json));
            valid.Insert(field,JsonValue::CreateStringValue(nonblank));
            const auto retained=isolated(valid).pages.front().blocks.front();
            check((std::wstring_view(field)==L"reviewFeedback"?retained.review_feedback:retained.explanation)==winrt::to_string(nonblank),
                "Inline feedback changed non-whitespace authored characters or applied another blank-text policy");
        }
    }
    std::cout<<"PASS optional inline feedback rejects only present Kotlin-blank fields before evaluation\n";
}
int main() {
    try {
        using lgaed::windows::parse_lesson_json;
        const auto root = std::filesystem::path(lgaed::build::source_root);
        inline_feedback_optional_contract_tests();
        mathematical_puzzle_identity_remains_optional();
        const auto path = root / "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json";
        // Exact published input for migration/legacy-coordinate regressions.
        const auto lesson = lgaed::test::load_pre_retirement_lesson(path.filename().string());
        narration_metadata_tests(lesson);
        independently_movable_order_lines_preserve_the_original_contract();
        reviewed_order_equivalences_are_source_bound();
        reviewed_choice_revisions_preserve_the_published_contract();
        reviewed_session_literal_supplies_only_authenticated_reading_provenance();
        reviewed_compiler_expansions_keep_the_original_contract();
        reviewed_compile_diagnostics_require_the_intended_source_change();
        reviewed_matching_label_preserves_historical_contract();
        reviewed_u11_domain_label_preserves_historical_contract();
        retirement_contract_tests(lesson);
        integrated_retirement_contract_tests();
        standard_error_objective_tests(lesson);
        {
            const auto reviewed=parse_lesson_json(R"({"id":"reviews","title":"Reviews","pages":[{"blocks":[{"type":"multiple_choice","question":"Q","options":["A","B","C"],"correctIndex":1,"explanation":"Success","reviewFeedback":["Review A","Review B","Review C"]}]}]})");
            const auto& choice=reviewed.pages[0].blocks[0];
            check(!evaluate_choice(choice,0).correct&&evaluate_choice(choice,0).feedback=="Review A"
                &&evaluate_choice(choice,2).feedback=="Review C"&&evaluate_choice(choice,1).feedback=="Success",
                "Per-option authored feedback was lost or exposed a success explanation before a correct answer");
            check(incorrect_feedback(choice,0)=="Review A"&&incorrect_feedback(choice).empty(),
                "Restoring an incorrect selection lost its own review or invented one without a selection");
        }
        check(lesson.id == "cpp_fundamentals__programas_y_ejecucion__primer_programa_y_salida", "Original lesson ID changed");
        check(lesson.pages.size() == 6 && lesson.pages[4].blocks.size() == 8, "Real lesson structure differs");
        check(lesson.state_layout.version == 2 && lesson.state_layout.family.size() == 3, "Historical language families were lost");
        check(lesson.state_layout.lineage == "9048491c38ff0e6d1cc9dbb47cc695cd96b50fc993bc0996255928b2ac64b6c5" &&
            lesson.state_layout.legacy_lineage == "88ca92f3c2d0938b7fbe360ce738dc3923be5b5608fdce3a14e386ac943b6d9a",
            "Cross-language state lineage differs from the independently calculated Android framing");
        const auto& prediction = lesson.pages[4].blocks[1];
        const auto& original = lesson.pages[4].blocks[3];
        const auto& modification = lesson.pages[4].blocks[6];
        check(prediction.id == "prediccion_zona_inicial" && prediction.address.page == 4 && prediction.address.block == 3, "Prediction no longer owns its historical slot");
        check(original.stable_id == "ejecucion_original_editor" && original.address.page == 6 && original.address.block == 1 &&
            original.address.addition_namespace.size() == 64, "New editor is not isolated in its allocated namespace");
        check(original.address.addition_namespace == "a1696bcfce824941b813aef3f0443d4b20cfddfaf2d95b5ecb82c71ea0b6a7cb", "Android addition namespace differs");
        check(modification.address.page == 4 && modification.address.block == 2 && modification.address.addition_namespace.empty(), "Modification editor moved to a new draft address");
        check(draft_key("programming", lesson, original) != draft_key("programming", lesson, modification), "Editor drafts collide");
        check(exercise_key(lesson, prediction).ends_with("_page_4_block_3"), "Exercise key no longer matches Android");
        check(original.initial_code == modification.initial_code && original.initial_code.ends_with("}\n"), "Source/indentation/LF was changed");
        const auto linked_json = [&](std::string_view relations) {
            return root_property(lesson.raw_json,"referenceAfter",relations);
        };
        check(reference_activity(lesson,lesson.pages[5].blocks[0]) == StatePosition{4,6},
            "Integrated reference title is visible before its own compiler objective");
        const auto linked_source = linked_json(R"([{"activity":"block_0004_0002","blocks":["block_0005_0000","block_0005_0001","block_0005_0002","block_0005_0003"]},{"activity":"block_0005_0006","blocks":["block_0005_0007","block_0005_0008"]}])");
        check(linked_source.find("\"referenceAfter\"") == linked_source.rfind("\"referenceAfter\""),
            "Reference fixture duplicated an existing root property");
        const auto linked = parse_lesson_json(linked_source);
        check(linked.state_layout.lineage == lesson.state_layout.lineage && linked.state_layout.signatures == lesson.state_layout.signatures,
            "Presentation-only references changed historical fingerprints or state lineage");
        check(reference_activity(linked,linked.pages[5].blocks[0]) == StatePosition{4,6} &&
              reference_activity(linked,linked.pages[5].blocks[2]) == StatePosition{4,6} &&
              reference_activity(linked,linked.pages[5].blocks[8]) == StatePosition{5,6} &&
              !reference_activity(linked,linked.pages[5].blocks[5]), "Reference points to a nearby quiz instead of its own compiler");
        auto changed_goal = linked.pages[5].blocks[6]; changed_goal.output_check->cases[0].outputs = {"Different exact goal"};
        check(reference_objective_frame(linked,changed_goal) != reference_objective_frame(linked,linked.pages[5].blocks[6]),
            "Reference completion seal is not bound to its authored output objective");
        for (const auto invalid_relation : {
                R"([{"activity":"missing","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":["missing"]}])",
                R"([{"activity":"block_0004_0004","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0006"]}])",
                R"([{"activity":"block_0004_0002","blocks":["block_0004_0004"]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0008","block_0005_0008"]}])",
                R"([{"activity":"block_0004_0002","blocks":["block_0005_0008"]},{"activity":"block_0005_0006","blocks":["block_0005_0008"]}])",
                R"([{"activity":"block_0005_0006","blocks":[]}])",
                R"([{"activity":"block_0005_0006","blocks":["block_0005_0008"],"unknown":true}])"})
            rejects([&] { (void)parse_lesson_json(linked_json(invalid_relation)); }, "Malformed or unsafe reference relationship accepted");
        check(parse_lesson_json(linked_json("[]")).reference_after.empty(), "Reference fixture failed to replace the integrated property");

        const auto dependent_json = [&](std::string_view relations) {
            return root_property(lesson.raw_json,"activityAfter",relations);
        };
        const auto dependent = parse_lesson_json(dependent_json(
            R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]},{"activity":"block_0005_0006","requires":["ejecucion_original_editor","block_0004_0002"]}])"));
        check(dependent.activity_after.size() == 2 &&
              activity_prerequisites(dependent,dependent.pages[4].blocks[6]) == std::vector<StatePosition>{{4,3}} &&
              activity_prerequisites(dependent,dependent.pages[5].blocks[6]) == std::vector<StatePosition>{{4,3},{4,6}} &&
              activity_prerequisites(dependent,dependent.pages[4].blocks[3]).empty(),
            "Activity dependencies lost their authored owners/order or changed an unrelated editor");
        check(dependent.state_layout.lineage == lesson.state_layout.lineage &&
              dependent.state_layout.legacy_lineage == lesson.state_layout.legacy_lineage &&
              dependent.state_layout.signatures == lesson.state_layout.signatures &&
              draft_key("programming",dependent,dependent.pages[4].blocks[3]) == draft_key("programming",lesson,original) &&
              draft_key("programming",dependent,dependent.pages[4].blocks[6]) == draft_key("programming",lesson,modification) &&
              reference_activity(dependent,dependent.pages[5].blocks[0]) == StatePosition{4,6},
            "Activity-only metadata changed state fingerprints, historical draft owners or integrated references");
        const auto protected_puzzle = parse_lesson_json(dependent_json(
            R"([{"activity":"block_0004_0004","requires":["ejecucion_original_editor","block_0004_0002"]}])"));
        const auto& protected_block = protected_puzzle.pages[4].blocks[7];
        check(protected_block.type == BlockType::multiple_choice &&
            activity_prerequisites(protected_puzzle,protected_block) == std::vector<StatePosition>{{4,3},{4,6}} &&
            protected_block.question == lesson.pages[4].blocks[7].question &&
            protected_block.options == lesson.pages[4].blocks[7].options &&
            protected_block.correct_index == lesson.pages[4].blocks[7].correct_index &&
            exercise_key(protected_puzzle,protected_block) == exercise_key(lesson,lesson.pages[4].blocks[7]) &&
            protected_puzzle.state_layout.signatures == lesson.state_layout.signatures,
            "A protected puzzle lost its prerequisite ownership, complete answer or original state slot");
        for (const auto type : {BlockType::cpp_compiler,BlockType::multiple_choice,BlockType::output_prediction,
                BlockType::fill_code,BlockType::order_code,BlockType::inline_fill_code,BlockType::drag_fill_code,BlockType::math_puzzle}) {
            auto typed = protected_puzzle; typed.pages[4].blocks[7].type = type;
            validate_activity_relationships(typed);
        }
        for (const auto type : {BlockType::heading,BlockType::paragraph,BlockType::note,BlockType::code}) {
            auto typed = protected_puzzle; typed.pages[4].blocks[7].type = type;
            rejects([&] { validate_activity_relationships(typed); }, "Presentation-only content was accepted as a dependent activity");
        }
        check(!compiler_is_puzzle_prerequisite(dependent,dependent.pages[4].blocks[3]) &&
            !compiler_is_puzzle_prerequisite(dependent,dependent.pages[4].blocks[6]),
            "An isolated legacy compiler chain became a mandatory puzzle prerequisite");
        const auto transitive_puzzle = parse_lesson_json(dependent_json(
            R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]},{"activity":"block_0004_0004","requires":["block_0004_0002"]}])"));
        check(!transitive_puzzle.compiler_puzzles_required &&
            compiler_is_puzzle_prerequisite(transitive_puzzle,transitive_puzzle.pages[4].blocks[3]) &&
            compiler_is_puzzle_prerequisite(transitive_puzzle,transitive_puzzle.pages[4].blocks[6]) &&
            !compiler_is_puzzle_prerequisite(transitive_puzzle,transitive_puzzle.pages[5].blocks[6]) &&
            !compiler_is_puzzle_prerequisite(transitive_puzzle,transitive_puzzle.pages[4].blocks[7]),
            "A puzzle's direct/transitive source gates were lost, widened to an unrelated editor or applied to the puzzle itself");
        const auto projected_puzzle = lgaed::windows::parse_lesson_progress_metadata(protected_puzzle.raw_json);
        check(projected_puzzle.activity_after.size() == 1 &&
            projected_puzzle.pages[4].blocks[3].output_check.has_value() &&
            projected_puzzle.pages[4].blocks[6].output_check.has_value() &&
            activity_prerequisites(projected_puzzle,projected_puzzle.pages[4].blocks[7]) == std::vector<StatePosition>{{4,3},{4,6}},
            "Catalog projection lost puzzle prerequisites or verifiable compiler goals without mandatory mode");
        auto compiler_without_check = dependent;
        compiler_without_check.pages[5].blocks[6].output_check.reset();
        validate_activity_relationships(compiler_without_check);
        compiler_without_check.pages[4].blocks[3].output_check.reset();
        rejects([&] { validate_activity_relationships(compiler_without_check); },
            "A prerequisite without a verifiable output objective can unlock another compiler");
        auto unstable_activity = dependent; unstable_activity.state_layout.version = 0;
        rejects([&] { validate_activity_relationships(unstable_activity); }, "Activity dependencies accepted transient display positions");
        for (const auto invalid_relation : {
                R"({"activity":"block_0004_0002","requires":["ejecucion_original_editor"]})",
                R"([{"activity":"missing","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["missing"]}])",
                R"([{"activity":"modificar_mensaje_titulo","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0004_0004"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0004_0002"]}])",
                R"([{"activity":"ejecucion_original_editor","requires":["block_0004_0002"]}])",
                R"([{"activity":"block_0004_0002","requires":["block_0005_0006"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor","ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]},{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":[]}])",
                R"([{"activity":"block_0004_0002","requires":[""]}])",
                R"([{"activity":"block_0004_0002","requires":[3]}])",
                R"([{"activity":"block_0004_0002","requires":"ejecucion_original_editor"}])",
                R"([{"activity":"block_0004_0002"}])",
                R"([{"requires":["ejecucion_original_editor"]}])",
                R"([{"activity":"block_0004_0002","requires":["ejecucion_original_editor"],"unknown":true}])",
                R"([{"activity":"ejecucion_original_editor","requires":["block_0004_0002"]},{"activity":"block_0004_0002","requires":["ejecucion_original_editor"]}])"})
        {
            rejects([&] { (void)parse_lesson_json(dependent_json(invalid_relation)); }, "Malformed or unsafe activity dependency accepted");
            rejects([&] { (void)lgaed::windows::parse_lesson_progress_metadata(dependent_json(invalid_relation)); },
                "Catalog projection accepted a malformed or unsafe activity dependency");
        }
        check(!original.standard_input && !modification.standard_input, "Input invented for a lesson that does not require it");
        check(evaluate_text_answer(prediction, "Zona inicial").correct, "Exact authored prediction rejected");
        for (const auto value : {"\"Zona inicial\"", "Zona inicial ", "Zona inicial\n", " Zona inicial", "Zona  inicial"})
            check(!evaluate_text_answer(prediction, value).correct, "Exact output accepted unauthored characters");
        check(uses_single_line_answer(prediction), "Single-line Enter contract lost");
        check(!evaluate_choice(lesson.pages[4].blocks[7], 0).correct && evaluate_choice(lesson.pages[4].blocks[7], 2).correct, "Conceptual choice changed");
        check(evaluate_order(lesson.pages[3].blocks[2], std::vector<std::size_t>{1,3,0,2}).correct, "Authored code ordering rejected");
        check(!evaluate_order(lesson.pages[3].blocks[2], std::vector<std::size_t>{1,1,0,2}).correct, "Duplicate code fragment accepted");

        FreshExecution execution{5, true, true, 0, "", "Caja recibida", {}};
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::correct, "Fresh expected output rejected");
        check(evaluate_output_check(*modification.output_check, execution, 6).verdict == OutputVerdict::unavailable, "Stale execution reused after an edit");
        execution.compilation_succeeded = false;
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::unavailable, "Compilation failure became incorrect answer");
        execution.compilation_succeeded = true; execution.execution_succeeded = false;
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::unavailable, "Runtime failure became incorrect answer");
        execution.execution_succeeded = true; execution.standard_output = "Zona segura";
        auto review = evaluate_output_check(*modification.output_check, execution, 5);
        check(review.verdict == OutputVerdict::incorrect && !review.feedback.empty(), "Authored targeted review was lost");
        execution.standard_output = "unrecognized output"; review = evaluate_output_check(*modification.output_check, execution, 5);
        check(review.verdict == OutputVerdict::incorrect && review.feedback.empty(), "Invented feedback for an unknown output");
        execution.standard_output = "Caja recibida\n";
        check(evaluate_output_check(*modification.output_check, execution, 5).verdict == OutputVerdict::incorrect, "Console checker trimmed significant LF");
        execution.standard_output = "Zona inicial";
        check(evaluate_output_check(*original.output_check, execution, 5).verdict == OutputVerdict::correct, "Original editor checks modification output");

        check(normalize_cpp_answer("  cout  <<  \"a  b\"  ") == "cout << \"a  b\"", "Literal spacing was collapsed");
        check(normalize_cpp_answer(" R\"tag(a \t\nb)tag\" ") == "R\"tag(a \t\nb)tag\"", "Raw literal whitespace was collapsed");
        check(normalize_cpp_answer("1'000  +  2") == "1'000 + 2", "Digit separator was mistaken for a char literal");
        auto fill = lesson.pages[1].blocks[4];
        check(evaluate_text_answer(fill, "  \"Muelle cerrado\"  ").correct && !evaluate_text_answer(fill, "\"Muelle  cerrado\"").correct,
            "Fill compatibility/literal significance changed");
        fill.code = "cout << \"___\";"; fill.answers = {"a  b"};
        check(evaluate_text_answer(fill, "a  b").correct && !evaluate_text_answer(fill, "a b").correct && !evaluate_text_answer(fill, " a  b").correct,
            "Template literal hole lost its exact characters");
        const auto single_block = [&](std::string_view block) {
            return parse_lesson_json("{\"id\":\"fixture\",\"title\":\"Fixture\",\"pages\":[{\"blocks\":[" + std::string(block) + "]}]}");
        };
        auto inline_lesson = single_block(R"({"type":"inline_fill_code","question":"Completa","template":"std::cout << \"[]\" << [];","answers":["a  b","!="] ,"answerAlternatives":[["a  b","c\td"],["!=","not_eq"]]})");
        const auto& inline_fill = inline_lesson.pages[0].blocks[0];
        check(evaluate_hole_answers(inline_fill,std::vector<std::string>{"a  b"," not_eq "}).correct &&
            evaluate_hole_answers(inline_fill,std::vector<std::string>{"c\td","!="}).correct,"Independent inline alternatives were interpreted as extra holes");
        check(!evaluate_hole_answers(inline_fill,std::vector<std::string>{"a b","!="}).correct &&
            !evaluate_hole_answers(inline_fill,std::vector<std::string>{"a  b","=="}).correct &&
            !evaluate_hole_answers(inline_fill,std::vector<std::string>{"a  b"}).correct,"Inline fill lost literal spaces, operator ownership or hole count");
        auto raw_inline = inline_fill; raw_inline.code_template = "auto text = R\"tag([])tag\"; auto value = [];";
        raw_inline.answers = {"a\t\nb","1'000"}; raw_inline.answer_alternatives.clear();
        check(evaluate_hole_answers(raw_inline,std::vector<std::string>{"a\t\nb"," 1'000 "}).correct &&
            !evaluate_hole_answers(raw_inline,std::vector<std::string>{"a b","1'000"}).correct,"Raw inline literal or digit separator lost exact characters");
        auto drag_lesson = single_block(R"({"type":"drag_fill_code","question":"Ordena","template":"int x = 0[] int y = 1[]","choices":[";",";","}"],"correctOrder":[0,1]})");
        const auto& drag = drag_lesson.pages[0].blocks[0];
        check(evaluate_drag_answers(drag,std::vector<std::size_t>{0,1}).correct && evaluate_drag_answers(drag,std::vector<std::size_t>{1,0}).correct,
            "Identical physical cards stopped being interchangeable as on Android");
        check(!evaluate_drag_answers(drag,std::vector<std::size_t>{0,0}).correct && !evaluate_drag_answers(drag,std::vector<std::size_t>{0,2}).correct &&
            !evaluate_drag_answers(drag,std::vector<std::size_t>{0,100}).correct,"A reused, wrong or unknown physical card was accepted");
        for (const auto malformed : {
            R"({"type":"inline_fill_code","template":"[] []","answers":["a"]})",
            R"({"type":"inline_fill_code","template":"[]","answers":["a"],"answerAlternatives":[["b","a"]]})",
            R"({"type":"inline_fill_code","template":"[]","answers":["a"],"answerAlternatives":[["a"],["b"]]})",
            R"({"type":"drag_fill_code","template":"[] []","choices":[";"],"correctOrder":[0,0]})",
            R"({"type":"drag_fill_code","template":"[]","choices":[";"],"correctOrder":[1]})"})
            rejects([&] { (void)single_block(malformed); },"Malformed per-hole contract accepted");
        auto numeric_lesson = single_block(R"({"type":"fill_code","code":"return ___;","answers":["3","3.0"],"answerComparison":"cpp_decimal_literal_value"})");
        auto numeric = numeric_lesson.pages[0].blocks[0];
        for (const auto accepted : {"3","3.","3.0",".3e1","30e-1","300.00e-2","3'0'0e-2"," 3.000\t","3e+0","3E-0000"})
            check(evaluate_text_answer(numeric,accepted).correct,"Exact equivalent decimal spelling rejected");
        for (const auto rejected : {"03","0x3","0b11","3f","3L","3.0f","3.0L","+3","-3","(3)","1+2","3 /*x*/","3 0","3,0","3''0","'3","3'","3e+","3e-1","3.0000000000000001","2147483648"})
            check(!evaluate_text_answer(numeric,rejected).correct,"Invalid token, type or different exact decimal value accepted");
        numeric.answers = {"9007199254740993.0"};
        check(evaluate_text_answer(numeric,"90071992547409930e-1").correct && !evaluate_text_answer(numeric,"9007199254740992.0").correct,
            "Decimal comparison rounded through IEEE double");
        numeric.answers = {"1e" + std::string(512,'9')};
        check(evaluate_text_answer(numeric,"10e" + std::string(511,'9') + "8").correct,"Large exact decimal exponent was bounded by machine integer or expanded");
        numeric.answers = {"1e-1000"};
        check(evaluate_text_answer(numeric,"10e-1001").correct && !evaluate_text_answer(numeric,"1e-1001").correct,"Negative exact exponents compare incorrectly");
        numeric.answers = {"0"};
        check(evaluate_text_answer(numeric,"0e-" + std::string(512,'9')).correct && evaluate_text_answer(numeric,"0." + std::string(16382,'0')).correct,
            "Exact decimal zero or parser boundary changed");
        rejects([&] { (void)evaluate_text_answer(numeric,"0." + std::string(16383,'0')); },"Technical decimal parser bound was silently treated as wrong answer");
        for (const auto malformed : {
            R"({"type":"fill_code","answers":["3","4"],"answerComparison":"cpp_decimal_literal_value"})",
            R"({"type":"fill_code","answers":["3f"],"answerComparison":"cpp_decimal_literal_value"})",
            R"({"type":"fill_code","answers":["3"],"answerComparison":"unknown"})"})
            rejects([&] { (void)single_block(malformed); },"Invalid authored numeric profile accepted");
        const auto exact_math = R"({ "type": "math_numeric_input", "prompt": "x [ ] { } \\\"", "value": 0.10000000000000000000000000000001, "scale": 1.2300e-17, "validator": {"type":"numeric","value":"0.1"} })";
        const auto mathematical = single_block(exact_math);
        check(mathematical.pages[0].blocks[0].type == BlockType::math_puzzle && mathematical.pages[0].blocks[0].math_type == "math_numeric_input" &&
            mathematical.pages[0].blocks[0].raw_json == exact_math && mathematical.raw_json.find(exact_math) != std::string::npos,
            "Math original JSON decimals/escapes/spacing were rounded or reserialized");
        rejects([&] { (void)parse_lesson_json(root_property(mathematical.raw_json,"stateLayout","{}")); },"A decimal state fingerprint was silently rounded");
        for (const auto filename : {"lesson_texto_secuencia_y_formato.json","lesson_fuente_compilacion_y_diagnostico.json"}) {
            const auto indexed = lgaed::windows::load_lesson(path.parent_path() / filename);
            bool has_new_type = false;
            for (const auto& page : indexed.pages) for (const auto& block : page.blocks) {
                check(indexed.raw_json.find(block.raw_json) != std::string::npos,"Original block no longer addresses the loaded curricular source");
                if (block.type == BlockType::inline_fill_code) { check(evaluate_hole_answers(block,block.answers).correct,"Real indexed inline answer rejected"); has_new_type = true; }
                if (block.type == BlockType::drag_fill_code) { check(evaluate_drag_answers(block,block.correct_order).correct,"Real indexed drag answer rejected"); has_new_type = true; }
            }
            check(has_new_type,"Indexed lesson fixture no longer contains migrated fill puzzles");
        }
        const auto real_numeric = lgaed::windows::load_lesson(root / "app/src/main/assets/content/es/programming/cpp_fundamentals/errores_y_construccion_gradual/lesson_requisitos_casos_y_primera_version.json");
        bool has_numeric = false;
        for (const auto& page : real_numeric.pages) for (const auto& block : page.blocks)
            if (block.answer_comparison == AnswerComparison::cpp_decimal_literal_value) { check(evaluate_text_answer(block,"30e-1").correct,"Real exact decimal profile lost its value"); has_numeric = true; }
        check(has_numeric,"Exact numeric fixture is not exercising its real profile");
        const auto real_math = lgaed::windows::load_lesson(root / "app/src/main/assets/content/es/math_physics/math_course_01/fracciones_y_equivalencia/lesson_fraccion_y_unidad.json");
        bool has_math = false;
        for (const auto& page : real_math.pages) for (const auto& block : page.blocks) {
            check(real_math.raw_json.find(block.raw_json) != std::string::npos,"Real math block was reserialized");
            if (block.type == BlockType::math_puzzle) { check(!block.prompt.empty() && block.math_type == "math_puzzle","Real mathematical prompt/type was lost"); has_math = true; }
        }
        check(has_math,"Published mathematics lesson did not load its actual puzzles");
        const auto ranges = emphasis_ranges("uno dos uno", std::vector<Emphasis>{{"uno",2,"italic"},{"dos",0,"bold"},{"uno",0,"bold"}});
        check(ranges.size() == 2 && ranges[0].start == 4 && ranges[1].start == 8 && ranges[1].style == "italic", "Emphasis matching ignores source occurrence");
        for (std::size_t page = 0; page < lesson.pages.size(); ++page) check(display_page(lesson, storage_page(lesson, page)) == page, "Page map is not invertible");

        auto corrupt = lesson.raw_json;
        replace_once(corrupt, "\"correctIndex\": 1", "\"correctIndex\": 0");
        rejects([&] { (void)parse_lesson_json(corrupt); }, "State-bearing answer change passed a stale map");
        rejects([&] { (void)parse_lesson_json(lesson.raw_json, "fr"); }, "Wrong-language state family accepted");
        rejects([&] { (void)parse_lesson_json("{\"id\":\"x\",\"title\":\"x\",\"pages\":[{\"blocks\":[{\"type\":\"unknown\"}]}]}"); }, "Unsupported block silently ignored");
        rejects([&] { (void)parse_lesson_json("{ malformed"); }, "Malformed JSON accepted");
        const auto basic = parse_lesson_json("{\"id\":\"demo\",\"title\":\"Demo\",\"pages\":[{\"blocks\":[{\"type\":\"output_prediction\",\"answers\":[\"ok\"]}]}]}");
        check(evaluate_text_answer(basic.pages[0].blocks[0], " ok\n").correct, "Legacy trimmed prediction tolerance regressed");
        check(parse_lesson_json(root_property(basic.raw_json,"activityAfter","[]")).activity_after.empty(),
            "Empty activity metadata requires an unnecessary legacy state migration");
        rejects([&] { (void)parse_lesson_json(root_property(basic.raw_json,"activityAfter",
            R"([{"activity":"block_0_0","requires":["block_0_0"]}])")); }, "Nonempty activity relationships without stable state identities accepted");
        const auto input_lesson = parse_lesson_json("{\"id\":\"stdin\",\"title\":\"Input\",\"pages\":[{\"blocks\":[{\"type\":\"cpp_compiler\",\"initialCode\":\"int main(){}\\n\",\"initialStandardInput\":\" \\t \\n\"}]}]}");
        check(input_lesson.pages[0].blocks[0].standard_input == " \t \n", "Explicit prepared input lost significant whitespace");
        for (const auto language : {"en", "fr"}) {
            const auto translated = lgaed::windows::load_lesson(root / "app/src/main/assets/content" / language /
                "programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json", language);
            check(translated.id == lesson.id && translated.pages.size() == 6 && translated.state_layout.version == 0,
                "Existing untranslated-layout family was reinterpreted or lost");
            bool note = false;
            for (const auto& page : translated.pages) for (const auto& block : page.blocks)
                if (block.type == BlockType::note && !block.text.empty()) note = true;
            check(note, "Translated note text was omitted");
        }
        std::cout << "Real U1L2, exact outputs, stable v2 families/slots, fresh compilation verdicts, C++ literals, emphasis and malformed contracts: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}

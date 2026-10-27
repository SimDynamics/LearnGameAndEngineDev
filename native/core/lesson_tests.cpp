#include "core/lesson.hpp"
#include "core/retirement_test_fixture.hpp"
#include "core/choice_revision_test_fixture.hpp"
#include "platform/windows/content_loader.hpp"
#include "native_build_config.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
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
        const auto before=lgaed::windows::load_lesson(folder/plan.filename);
        const auto current=lgaed::test::revise_choice_lesson(before,plan.identity);
        const auto metadata=lgaed::windows::parse_lesson_progress_metadata(current.raw_json);
        const auto position=stable_block_position(current,plan.identity);check(position.has_value(),"Reviewed choice lost its stable identity");
        const auto& old=before.pages[position->page].blocks[position->block];const auto& block=current.pages[position->page].blocks[position->block];
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
            const auto record=layout.GetNamedArray(L"choiceRevisions").GetAt(0).GetObject();
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
    std::cout<<"PASS five exact choice revisions: original snapshot/family/keys, full and catalog projection, reviewed first-edge provenance and later editorial text, negative source/ownership/map guards\n";
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
int main() {
    try {
        using lgaed::windows::parse_lesson_json;
        const auto root = std::filesystem::path(lgaed::build::source_root);
        const auto path = root / "app/src/main/assets/content/es/programming/cpp_fundamentals/programas_y_ejecucion/lesson_primer_programa_y_salida.json";
        // Exact published input for migration/legacy-coordinate regressions.
        const auto lesson = lgaed::test::load_pre_retirement_lesson(path.filename().string());
        narration_metadata_tests(lesson);
        independently_movable_order_lines_preserve_the_original_contract();
        reviewed_order_equivalences_are_source_bound();
        reviewed_choice_revisions_preserve_the_published_contract();
        reviewed_matching_label_preserves_historical_contract();
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

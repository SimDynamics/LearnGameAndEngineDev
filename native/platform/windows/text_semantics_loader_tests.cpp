#include "core/compile_check_test_fixture.hpp"
#include "platform/windows/content_loader.hpp"
#include "platform/windows/state_store.hpp"
#include "native_build_config.hpp"
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace {
using namespace lgaed;
using namespace winrt::Windows::Data::Json;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
JsonValue string(std::string_view value){return JsonValue::CreateStringValue(winrt::to_hstring(value));}
JsonObject fixture(const JsonObject* mathematical=nullptr){
    auto root=JsonObject::Parse(LR"({"id":"qa_semantics","title":"QA","pages":[{"blocks":[
        {"type":"paragraph","text":"\ud83d\ude00 Usa `int p;`. Salida: N=1a|1b.","emphasis":[{"text":"Usa","style":"bold"}]},
        {"type":"multiple_choice","id":"choice","question":"Elige N=1.","options":["N=1","N=2"],"correctIndex":0,"explanation":"Resultado: N=1.","reviewFeedback":["Bien: N=1","Revisa: N=2"]},
        {"type":"math_puzzle","schemaVersion":1,"id":"nested","prompt":"Relaciona N=1.","representations":[{"type":"math_table","headers":["Salida"],"rows":[["N=1","texto"]]}],"responses":[{"id":"pairs","type":"math_matching_drag","sources":[{"id":"a","label":"a"}],"targets":[{"id":"b","content":[{"type":"text","text":"N=1","mathMode":"text"}]}],"validator":{"type":"matching","expected":{"a":"b"}}}],"feedback":{"correct":"Correcto N=1","incorrect":"Comprueba N=2"}},
        {"type":"cpp_compiler","initialCode":"int main() {}","outputCheck":{"cases":[{"standardInput":"","outputs":["N=1"],"explanation":"Salida: N=1.","reviewFeedback":[{"outputs":["N=2"],"text":"Revisa N=2."}]}]}}
    ]}]})");
    const auto blocks=root.GetNamedArray(L"pages").GetObjectAt(0).GetNamedArray(L"blocks");
    if(mathematical)blocks.SetAt(2,*mathematical);
    JsonArray origin_blocks,current_blocks;
    for(unsigned n=0;n<blocks.Size();++n){
        auto protected_block=JsonObject::Parse(blocks.GetObjectAt(n).Stringify());
        if(n==0){protected_block.Remove(L"text");protected_block.Remove(L"emphasis");}
        if(n==1)for(const auto field:{L"question",L"explanation",L"reviewFeedback"})protected_block.Remove(field);
        if(n==3){const auto item=protected_block.GetNamedObject(L"outputCheck").GetNamedArray(L"cases").GetObjectAt(0);item.Remove(L"explanation");item.Remove(L"reviewFeedback");}
        const auto identity="block_0000_000"+std::to_string(n);JsonObject record;
        record.Insert(L"id",string(identity));record.Insert(L"sha256",string(test::compile_check_test_fingerprint(protected_block)));
        origin_blocks.Append(record);current_blocks.Append(string(identity));
    }
    JsonObject old_page;old_page.Insert(L"id",string("page_0000"));old_page.Insert(L"blocks",origin_blocks);JsonArray origin;origin.Append(old_page);
    JsonObject current_page;current_page.Insert(L"id",string("page_0000"));current_page.Insert(L"blocks",current_blocks);JsonArray current;current.Append(current_page);
    JsonObject family_record;family_record.Insert(L"sha256",string(std::string(64,'0')));family_record.Insert(L"origin",origin);
    JsonObject family;family.Insert(L"es",family_record);JsonObject layout;
    layout.Insert(L"version",JsonValue::CreateNumberValue(2));layout.Insert(L"locale",string("es"));layout.Insert(L"origin",origin);
    layout.Insert(L"current",current);layout.Insert(L"family",family);layout.Insert(L"additions",JsonArray{});layout.Insert(L"editorial",JsonArray{});
    root.Insert(L"stateLayout",layout);return root;
}
JsonObject annotate(JsonObject root,unsigned block,std::string_view path,std::string_view source,std::size_t first,std::size_t last,std::string_view selected,std::string_view role){
    JsonObject span;span.Insert(L"start",JsonValue::CreateNumberValue(static_cast<double>(first)));span.Insert(L"end",JsonValue::CreateNumberValue(static_cast<double>(last)));
    span.Insert(L"text",string(selected));span.Insert(L"role",string(role));JsonArray spans;if(first!=last)spans.Append(span);
    JsonObject field;field.Insert(L"block",string("block_0000_000"+std::to_string(block)));field.Insert(L"path",string(path));
    field.Insert(L"sourceSha256",string(state::sha256(source)));field.Insert(L"spans",spans);
    if(!root.HasKey(L"textSemantics")){JsonObject metadata;metadata.Insert(L"version",JsonValue::CreateNumberValue(1));metadata.Insert(L"fields",JsonArray{});root.Insert(L"textSemantics",metadata);}
    root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields").Append(field);return field;
}
void rejected(const JsonObject& root){const auto source=winrt::to_string(root.Stringify());bool full=false,metadata=false;
    try{(void)windows::parse_lesson_json(source);}catch(const std::exception&){full=true;}
    try{(void)windows::parse_lesson_progress_metadata(source);}catch(const std::exception&){metadata=true;}
    require(full&&metadata,"Malformed text semantics passed a full or progress loader");
}
void independent_region(const JsonObject& mathematical,std::string_view path,std::string_view source){
    auto root=fixture(&mathematical);const auto before=windows::parse_lesson_json(winrt::to_string(root.Stringify()));
    annotate(root,2,path,source,0,winrt::to_hstring(source).size(),source,"output");
    const auto current=winrt::to_string(root.Stringify());
    const auto after=windows::parse_lesson_json(current),progress=windows::parse_lesson_progress_metadata(current);
    const auto& block=after.pages[0].blocks[2];
    require(block.text_semantics.size()==1&&block.text_semantics.at(std::string(path)).source==source&&
        progress.pages[0].blocks[2].text_semantics==block.text_semantics,
        "An independent native region required annotations on its unmarked siblings or lost full/progress parity");
    require(before.state_layout.raw_json==after.state_layout.raw_json&&
        before.pages[0].blocks[2].raw_json==block.raw_json&&before.pages[0].blocks[2].stable_id==block.stable_id,
        "An independent annotation rewrote the original payload, identity or stable state layout");
}
void independent_regions(){
    const auto inline_fields=JsonObject::Parse(LR"({"type":"math_puzzle","schemaVersion":1,"prompt":"Completa","representations":[{"type":"math_table","headers":["Salida","Nombre"],"rows":[["N=1","texto"]]}],"responses":[{"id":"r","type":"math_structured_answer","fields":[{"id":"x","type":"fixed","label":"Cantidad","text":"N=1","unit":"m"}],"segments":[{"text":"Vale"},{"field":"x"},{"text":"fin"}],"validator":{"type":"fields","fields":{}}}]})");
    for(const auto& [path,source]:std::initializer_list<std::pair<std::string_view,std::string_view>>{
        {"/representations/0/headers/0","Salida"},{"/representations/0/rows/0/0","N=1"},
        {"/responses/0/segments/0/text","Vale"},{"/responses/0/fields/0/text","N=1"},
        {"/responses/0/fields/0/unit","m"},{"/responses/0/fields/0/label","Cantidad"}})
        independent_region(inline_fields,path,source);
    auto table=JsonObject::Parse(inline_fields.Stringify());auto response=table.GetNamedArray(L"responses").GetObjectAt(0);
    response.Remove(L"segments");response.Insert(L"layout",string("table"));response.Insert(L"headers",JsonArray::Parse(LR"(["Dato","Unidad"])"));
    response.Insert(L"rows",JsonArray::Parse(LR"([[{"field":"x"},"otra celda"]])"));
    for(const auto& [path,source]:std::initializer_list<std::pair<std::string_view,std::string_view>>{
        {"/responses/0/headers/0","Dato"},{"/responses/0/rows/0/1","otra celda"},
        {"/responses/0/fields/0/text","N=1"},{"/responses/0/fields/0/label","Cantidad"},
        {"/responses/0/fields/0/unit","m"}})independent_region(table,path,source);
    auto ordinary=JsonObject::Parse(table.Stringify());response=ordinary.GetNamedArray(L"responses").GetObjectAt(0);
    for(const auto property:{L"layout",L"headers",L"rows"})response.Remove(property);
    independent_region(ordinary,"/responses/0/fields/0/label","Cantidad");
    independent_region(ordinary,"/responses/0/fields/0/text","N=1");
    independent_region(ordinary,"/responses/0/fields/0/unit","m");
    auto nested=JsonObject::Parse(inline_fields.Stringify());const auto table_spec=nested.GetNamedArray(L"representations").GetObjectAt(0);
    JsonObject card;card.Insert(L"type",string("rich_answer_option"));JsonArray content;content.Append(table_spec);card.Insert(L"content",content);
    nested.GetNamedArray(L"representations").SetAt(0,card);
    independent_region(nested,"/representations/0/content/0/headers/0","Salida");
    independent_region(nested,"/representations/0/content/0/rows/0/0","N=1");
}
void current_character_intent(){
    const auto path=std::filesystem::path(build::source_root)/
        "app/src/main/assets/content/es/programming/cpp_fundamentals/archivos_y_programa_completo/lesson_clasificar_caracteres_y_normalizar_una_copia.json";
    const auto read_asset=[&]{std::ifstream input(path,std::ios::binary);require(bool(input),"Missing current character-option curriculum");
        return std::string{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};};
    const auto raw=read_asset();const auto root=JsonObject::Parse(winrt::to_hstring(raw));
    const auto before=windows::parse_lesson_json(winrt::to_string(root.Stringify()));
    const auto position=curriculum::stable_block_position(before,"block_0007_0004");
    require(position.has_value(),"The current character option lost its stable owner");
    const auto& original=before.pages[position->page].blocks[position->block];
    std::optional<unsigned> selected;
    const auto fields=root.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
    for(const auto& value:fields){const auto field=value.GetObject();
        if(field.GetNamedString(L"block")!=L"block_0007_0004"||field.GetNamedString(L"path")!=L"/options/0")continue;
        const auto spans=field.GetNamedArray(L"spans");
        for(unsigned n=0;n<spans.Size();++n){const auto span=spans.GetObjectAt(n);if(span.GetNamedString(L"text")!=L"!")continue;
            require(!selected&&span.GetNamedString(L"role")==L"data"&&span.GetNamedString(L"reading")==L"natural",
                "The current punctuation intent no longer has its reviewed source/data role");
            span.Insert(L"reading",string("characters"));selected=n;}
    }
    require(selected.has_value(),"The current character option lost its exact authored punctuation span");
    const auto source=winrt::to_string(root.Stringify());const auto after=windows::parse_lesson_json(source),progress=windows::parse_lesson_progress_metadata(source);
    auto expected=original.text_semantics;expected.at("/options/0").spans[*selected].reading=curriculum::SemanticReading::characters;
    require(after.pages[position->page].blocks[position->block].text_semantics==expected&&
        progress.pages[position->page].blocks[position->block].text_semantics==expected,
        "Characters intent changed the existing role/source/spans or diverged between full and progress loaders");
    require(after.state_layout.raw_json==before.state_layout.raw_json&&read_asset()==raw,
        "Characters intent rewrote the historical state layout or authored asset");
    for(std::size_t p=0;p<before.pages.size();++p)for(std::size_t b=0;b<before.pages[p].blocks.size();++b){
        const auto& old=before.pages[p].blocks[b];const auto& current=after.pages[p].blocks[b];
        require(old.raw_json==current.raw_json&&old.stable_id==current.stable_id&&old.address.page==current.address.page&&old.address.block==current.address.block,
            "Characters authoring changed curriculum, an answer payload or its historical address");}
    for(const auto reading:{"auto","natural","technical","characters"}){
        auto synthetic=fixture();const auto field=annotate(synthetic,1,"/options/1","N=2",0,3,"N=2","data");
        field.GetNamedArray(L"spans").GetObjectAt(0).Insert(L"reading",string(reading));
        const auto text=winrt::to_string(synthetic.Stringify());const auto full=windows::parse_lesson_json(text),metadata=windows::parse_lesson_progress_metadata(text);
        const auto& descriptor=full.pages[0].blocks[1].text_semantics.at("/options/1");
        require(curriculum::semantic_reading_name(descriptor.spans.front().reading)==reading&&descriptor.source=="N=2"&&
            descriptor.spans.front().role==curriculum::SemanticRole::data&&metadata.pages[0].blocks[1].text_semantics==full.pages[0].blocks[1].text_semantics,
            "An existing reading intent or its data role changed while loading characters support");
    }
    for(const auto reading:{"character","Characters","characters ","phonetic",""}){
        auto bad=fixture();const auto field=annotate(bad,1,"/options/1","N=2",0,3,"N=2","data");
        field.GetNamedArray(L"spans").GetObjectAt(0).Insert(L"reading",string(reading));rejected(bad);
    }
    for(const auto* json:{L"null",L"true",L"1",L"[]",L"{}"}){
        auto bad=fixture();const auto field=annotate(bad,1,"/options/1","N=2",0,3,"N=2","data");
        field.GetNamedArray(L"spans").GetObjectAt(0).Insert(L"reading",JsonValue::Parse(json));rejected(bad);
    }
}
}
int main() try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const auto original=fixture();const auto before=windows::parse_lesson_json(winrt::to_string(original.Stringify()));
    auto current=JsonObject::Parse(original.Stringify());
    const std::string prose="\xf0\x9f\x98\x80 Usa `int p;`. Salida: N=1a|1b.";
    annotate(current,0,"/text",prose,8,14,"int p;","cpp");
    annotate(current,1,"/options/1","N=2",0,3,"N=2","output");
    annotate(current,1,"/reviewFeedback/1","Revisa: N=2",0,0,"","prose");
    annotate(current,2,"/responses/0/targets/0/content/0/text","N=1",0,3,"N=1","output");
    annotate(current,2,"/feedback/incorrect","Comprueba N=2",10,13,"N=2","output");
    annotate(current,2,"/feedback/correct","Correcto N=1",0,0,"","prose");
    annotate(current,2,"/representations/0/rows/0/0","N=1",0,3,"N=1","data");
    annotate(current,2,"/representations/0/rows/0/1","texto",0,0,"","prose");
    annotate(current,3,"/outputCheck/cases/0/explanation","Salida: N=1.",8,11,"N=1","output");
    annotate(current,3,"/outputCheck/cases/0/reviewFeedback/0/text","Revisa N=2.",7,10,"N=2","output");
    const auto source=winrt::to_string(current.Stringify());
    const auto after=windows::parse_lesson_json(source),progress=windows::parse_lesson_progress_metadata(source);
    require(after.state_layout.raw_json==before.state_layout.raw_json,"Annotations changed the historical state layout");
    for(std::size_t n=0;n<before.pages[0].blocks.size();++n){
        const auto& a=before.pages[0].blocks[n];const auto& b=after.pages[0].blocks[n];
        require(a.raw_json==b.raw_json&&a.stable_id==b.stable_id&&a.address.page==b.address.page&&a.address.block==b.address.block&&
            a.address.addition_namespace==b.address.addition_namespace&&a.address.progress_page==b.address.progress_page,
            "Annotations changed a source, answer payload or historical address");
        require(progress.pages[0].blocks[n].text_semantics==b.text_semantics,"Full/progress loaders disagree about explicit text ownership");
    }
    require(after.pages[0].blocks[0].text_semantics.at("/text").source==prose,"Unicode source was normalized");
    require(after.pages[0].blocks[1].text_semantics.at("/reviewFeedback/1").spans.empty(),"Explicit prose-only field disappeared");
    auto partial=JsonObject::Parse(current.Stringify());const auto partial_fields=partial.GetNamedObject(L"textSemantics").GetNamedArray(L"fields");
    for(unsigned n=0;n<partial_fields.Size();++n)if(partial_fields.GetObjectAt(n).GetNamedString(L"path")==L"/representations/0/rows/0/1"){partial_fields.RemoveAt(n);break;}
    const auto partial_source=winrt::to_string(partial.Stringify());
    const auto partial_full=windows::parse_lesson_json(partial_source),partial_progress=windows::parse_lesson_progress_metadata(partial_source);
    require(partial_full.pages[0].blocks[2].text_semantics.contains("/representations/0/rows/0/0")&&
        !partial_full.pages[0].blocks[2].text_semantics.contains("/representations/0/rows/0/1")&&
        partial_full.pages[0].blocks[2].text_semantics==partial_progress.pages[0].blocks[2].text_semantics,
        "A native table cell could not keep its own annotation with an unmarked neighbor");
    independent_regions();
    current_character_intent();
    auto partial_feedback=JsonObject::Parse(original.Stringify());
    annotate(partial_feedback,2,"/feedback/correct","Correcto N=1",0,0,"","prose");rejected(partial_feedback);
    for(const auto path:{"/answers/0","/initialCode","/outputCheck/cases/0/outputs/0","/outputCheck/cases/0/standardInput"}){
        auto bad=JsonObject::Parse(original.Stringify());annotate(bad,3,path,"N=1",0,3,"N=1","output");rejected(bad);}
    for(const auto path:{"/responses/0/validator/expected/a","/responses/00/targets/0/content/0/text","/id"}){
        auto bad=JsonObject::Parse(original.Stringify());annotate(bad,2,path,"N=1",0,3,"N=1","output");rejected(bad);}
    for(unsigned mode=0;mode<9;++mode){auto bad=JsonObject::Parse(original.Stringify());const auto field=annotate(bad,0,"/text",prose,8,14,"int p;","cpp");
        const auto span=field.GetNamedArray(L"spans").GetObjectAt(0);
        if(mode==0)field.Insert(L"sourceSha256",string(std::string(64,'0')));
        if(mode==1)field.Insert(L"block",string("unknown"));
        if(mode==2)span.Insert(L"start",JsonValue::CreateBooleanValue(true));
        if(mode==3)span.Insert(L"text",string("int q;"));
        if(mode==4){span.Insert(L"start",JsonValue::CreateNumberValue(0));span.Insert(L"end",JsonValue::CreateNumberValue(1));span.Insert(L"text",string("\xf0\x9f\x98\x80"));}
        if(mode==5)span.Insert(L"role",string("red"));
        if(mode==6)span.Insert(L"reading",string("phonetic"));
        if(mode==7)field.GetNamedArray(L"spans").Append(span);
        if(mode==8)bad.GetNamedObject(L"textSemantics").GetNamedArray(L"fields").Append(field);
        rejected(bad);
    }
    auto lexical=source;const auto token=lexical.find("\"version\":1");require(token!=lexical.npos,"Missing lexical version fixture");lexical.replace(token,11,"\"version\":1.0");
    bool invalid_integer=false;try{(void)windows::parse_lesson_json(lexical);}catch(const std::exception&){invalid_integer=true;}require(invalid_integer,"A floating semantic version was accepted");
    std::cout<<"PASS exact annotation binding, nested public fields, stale/hidden/Unicode rejection and unchanged state/raw payloads; no UI/audio/progress writes.\n";return 0;
}catch(const winrt::hresult_error& error){std::cerr<<winrt::to_string(error.message())<<'\n';return 1;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

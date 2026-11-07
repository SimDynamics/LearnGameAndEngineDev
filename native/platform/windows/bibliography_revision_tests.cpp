#include "core/compile_check_test_fixture.hpp"
#include "core/narration_state.hpp"
#include "platform/windows/bibliography_revision.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "presentation/windows/bibliography_resume_policy.hpp"
#include "presentation/windows/narration_legacy_policy.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace lgaed;
using namespace winrt::Windows::Data::Json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);require(input.good(),"Missing QA bibliography fixture");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
JsonObject json(std::string_view raw){return JsonObject::Parse(winrt::to_hstring(raw));}
JsonValue string(std::string_view value){return JsonValue::CreateStringValue(winrt::to_hstring(value));}
std::string checked_file(const std::filesystem::path& directory,const JsonObject& descriptor){
    const auto filename=winrt::to_string(descriptor.GetNamedString(L"file"));
    require(std::filesystem::path(filename).filename()==std::filesystem::path(filename),"QA fixture name escaped its directory");
    const auto result=bytes(directory/filename);
    require(state::sha256(result)==winrt::to_string(descriptor.GetNamedString(L"sha256")),"QA bibliography fixture fingerprint changed");return result;
}
const curriculum::Block& block(const curriculum::Lesson& lesson,std::string_view owner){const auto at=curriculum::stable_block_position(lesson,owner);
    require(at.has_value(),"QA bibliography owner missing");return lesson.pages[at->page].blocks[at->block];}
std::optional<curriculum::TextSemantics> explanation(const curriculum::Block& value){const auto at=value.text_semantics.find("/explanation");
    return at==value.text_semantics.end()?std::nullopt:std::optional{at->second};}
void rejects(const JsonObject& root){for(const bool progress:{false,true}){bool rejected=false;
    try{const auto raw=winrt::to_string(root.Stringify());if(progress)(void)windows::parse_lesson_progress_metadata(raw);else(void)windows::parse_lesson_json(raw);}
    catch(...){rejected=true;}require(rejected,"A forged bibliography archive was admitted");}}
std::size_t archive_count(const curriculum::Lesson& lesson){return std::ranges::count_if(lesson.state_layout.reading_revisions,
    [](const auto& revision){return revision.bibliography_only;});}
void loader(const std::filesystem::path& directory,const JsonObject& manifest){
    require(manifest.GetNamedString(L"commit")==L"06d31ba73be4b0c7672c7113df2340c9edbb2c04","Bibliography fixture lost its immutable publication");
    const auto lessons=manifest.GetNamedArray(L"lessons");require(lessons.Size()==3,"Expected the three real bibliography fixture lessons");
    std::size_t owners=0,citations=0;
    for(const auto& value:lessons){const auto item=value.GetObject();const auto before_raw=checked_file(directory,item.GetNamedObject(L"before"));
        const auto after_raw=checked_file(directory,item.GetNamedObject(L"after"));
        const auto before=windows::parse_lesson_json(before_raw),after=windows::parse_lesson_json(after_raw);
        const auto progress=windows::parse_lesson_progress_metadata(after_raw);
        const auto definitions=item.GetNamedArray(L"owners");owners+=definitions.Size();
        require(before.id==after.id&&before.state_layout.lineage==after.state_layout.lineage,"Bibliography changed the lesson family");
        require(archive_count(after)==definitions.Size()&&archive_count(progress)==definitions.Size(),"The bibliography tail lost an authenticated prefix");
        const auto first=std::ranges::min_element(after.state_layout.reading_revisions,{},&curriculum::StateReadingRevision::editorial_prefix);
        require(first!=after.state_layout.reading_revisions.end()&&first->bibliography_only,"Missing first bibliographic archive");
        require(first->editorial_prefix==json(before_raw).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Size(),"Bibliography archive reconstructed an invented prefix");
        require(first->previous_blocks.size()==definitions.Size()&&first->previous_explanation_semantics.size()==definitions.Size(),"The full previous bibliography prefix is incomplete");
        for(const auto& definition:definitions){const auto expected=definition.GetObject();const auto owner=winrt::to_string(expected.GetNamedString(L"id"));
            citations+=static_cast<std::size_t>(expected.GetNamedNumber(L"citations"));
            const auto& old=block(before,owner);const auto& current=block(after,owner);
            require(windows::bibliography::only_parenthetical_locator_removal(old.explanation,current.explanation),"A real authored citation pair failed its general matcher");
            require(old.options==current.options&&old.answers==current.answers&&old.question==current.question&&old.code==current.code,
                "The bibliography QA changed a question, answer or source program");
            require(test::compile_check_test_fingerprint(json(first->previous_blocks.at(owner)))==test::compile_check_test_fingerprint(json(old.raw_json)),
                "An archived before block differs from the real published block");
            require(first->previous_explanation_semantics.at(owner)==explanation(old),"A previous descriptor was inferred, lost or borrowed");
            require(explanation(block(progress,owner))==explanation(current),"The catalog projection lost current bibliography semantics");
        }
        // Every invalid archive is parsed through both application loaders.
        for(unsigned variant=0;variant<6;++variant){auto changed=json(after_raw);const auto edges=changed.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
            const auto archived=edges.GetObjectAt(edges.Size()-1).GetNamedObject(L"bibliography");
            if(variant==0)archived.Insert(L"historySha256",string(std::string(64,'0')));
            if(variant==1)archived.GetNamedObject(L"previousBlock").Insert(L"explanation",string("Forged previous prose."));
            if(variant==2)archived.GetNamedObject(L"currentBlock").Insert(L"question",string("Changed question."));
            if(variant==3)archived.Remove(L"previousTextSemantics");
            if(variant==4){auto descriptor=JsonObject{};descriptor.Insert(L"block",string("another-owner"));descriptor.Insert(L"path",string("/explanation"));
                descriptor.Insert(L"sourceSha256",string(std::string(64,'0')));descriptor.Insert(L"spans",JsonArray{});archived.Insert(L"previousTextSemantics",descriptor);}
            if(variant==5)archived.Insert(L"unknown",JsonValue::CreateBooleanValue(true));
            rejects(changed);
        }
        // An overlay-only change is allowed, but its old pair is not a proof
        // that the newly marked field existed in that historical page script.
        auto marked=json(after_raw);const auto edge=marked.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").GetObjectAt(
            marked.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Size()-1);
        const auto owner=winrt::to_string(edge.GetNamedString(L"id"));const auto& current=block(after,owner);
        auto overlay=marked.GetNamedObject(L"textSemantics");auto fields=overlay.GetNamedArray(L"fields");bool replaced=false;
        for(unsigned i=0;i<fields.Size();++i){const auto field=fields.GetObjectAt(i);
            if(field.GetNamedString(L"block")==winrt::to_hstring(owner)&&field.GetNamedString(L"path")==L"/explanation"){
                auto spans=field.GetNamedArray(L"spans");
                if(spans.Size())spans.GetObjectAt(0).Insert(L"role",string("prose"));
                else{auto span=JsonObject{};span.Insert(L"start",JsonValue::CreateNumberValue(0));span.Insert(L"end",JsonValue::CreateNumberValue(1));
                    span.Insert(L"text",string(current.explanation.substr(0,1)));span.Insert(L"role",string("prose"));spans.Append(span);}
                replaced=true;break;
            }
        }
        if(!replaced){auto field=JsonObject{};field.Insert(L"block",string(owner));field.Insert(L"path",string("/explanation"));
            field.Insert(L"sourceSha256",string(state::sha256(current.explanation)));field.Insert(L"spans",JsonArray{});fields.Append(field);}
        require(archive_count(windows::parse_lesson_json(winrt::to_string(marked.Stringify())))==0,
            "A later unarchived descriptor fabricated an earlier page prefix");
    }
    require(owners==10&&citations==11,"The real QA bibliography inventory changed");
}
void predicate(const std::filesystem::path& directory,const JsonObject& manifest){
    const auto descriptor=manifest.GetNamedObject(L"predicateCases");const auto raw=checked_file(directory,descriptor);
    const auto cases=JsonArray::Parse(winrt::to_hstring(raw));require(cases.Size()==descriptor.GetNamedNumber(L"cases")&&cases.Size()>0,"The shared predicate corpus is incomplete");
    for(unsigned i=0;i<cases.Size();++i){const auto item=cases.GetObjectAt(i);const auto before=winrt::to_string(item.GetNamedString(L"before")),after=winrt::to_string(item.GetNamedString(L"after"));
        const bool actual=windows::bibliography::only_parenthetical_locator_removal(before,after);
        if(actual!=item.GetNamedBoolean(L"expected"))throw std::runtime_error("Native bibliography predicate disagrees with shared case "+std::to_string(i)+": "+before);
    }
}
struct Part{struct Region{std::string key,anchor;}region;struct Sentence{std::string prepared,text;std::size_t start=0,end=0;}sentence;std::size_t occurrence=0;};
void pauses(){
    using namespace presentation::bibliography_resume;
    const auto part=[](std::string owner,std::string text,std::size_t start){return Part{{owner,owner},{text,text,start,start+text.size()},0};};
    const std::vector<Part> before{part("neighbor","Opening.",0),part("guide","Reasoning.",0),part("guide","Author, 2024, p. 8",10),part("later","Later.",0)};
    auto after=before;after[2]=part("guide","Author, 2024",10);
    const auto boundary=[&](const auto& old,const auto& now,std::string_view region,std::size_t at,bool finished=false){return first_changed_segment<Part>(old,now,region,at,finished,
        [](const auto& a,const auto& b){return same_recording_sentence(a,b);});};
    require(!boundary(before,after,"guide",0)&&!boundary(before,after,"guide",1),"A compatible earlier pause was rewound");
    require(boundary(before,after,"guide",2)==2&&boundary(before,after,"guide",3)==2&&boundary(before,after,"guide",3,true)==2,
        "A changed citation inherited a later or finished pause");
    auto removed=before;removed.erase(removed.begin()+2);require(!boundary(before,removed,"guide",2),"Deleting obsolete tail invented replacement audio");
    auto additional=before;additional.insert(additional.begin()+3,part("guide","Added.",30));
    require(!boundary(before,additional,"guide",2)&&boundary(before,additional,"guide",3)==3,"An earlier pause bypassed or prematurely replayed an added sentence");
    require(boundary(before,additional,"guide",2,true)==3,"A finished old guide hid its new tail");
    auto incompatible=after;incompatible[1].sentence.start=1;require(boundary(before,incompatible,"guide",1)==1,"Different source offsets were treated as one recording");
    incompatible=after;incompatible[1].occurrence=1;require(boundary(before,incompatible,"guide",1)==1,"Repeated audio lost its source occurrence owner");
    incompatible=after;incompatible[1].region.anchor="foreign";require(boundary(before,incompatible,"guide",1)==1,"A foreign source owner inherited a recording");
    narration::History history;for(unsigned slot=0;slot<3;++slot){narration::Position saved{static_cast<std::int64_t>(slot),410,0,false,
            narration::HeardPrefix{std::string(64,'a'),410000,900000}};
        history.entries.push_back({"old-"+std::to_string(slot),"stable-"+std::to_string(slot),saved,"qa-bibliography-model"});}
    for(unsigned slot=0;slot<3;++slot){auto copy=history;const auto original=copy;
        auto position=copy.entries[slot].position;
        if(const auto at=boundary(before,after,"guide",static_cast<std::size_t>(position.segment))){position.segment=*at;position.milliseconds=0;
            position.finished=false;position.heard_prefix.reset();position.heard_through=std::min(position.heard_through,position.segment);}
        const auto old_key=copy.entries[slot].key;
        require(presentation::narration_legacy::rekey_authenticated_bookmark(copy,old_key,"new-"+std::to_string(slot),position,"qa-bibliography-model"),
            "Authenticated bibliography model could not reuse its existing bookmark slot");
        require(copy.entries.size()==3,"Bibliography created a fourth bookmark");
        if(slot<2)require(copy.entries[slot].position==original.entries[slot].position,"An exact earlier recording lost its compatible offset");
        else require(copy.entries[slot].position.milliseconds==0&&!copy.entries[slot].position.heard_prefix,"Changed citation inherited listening credit");
        for(unsigned other=0;other<3;++other)if(other!=slot)require(copy.entries[other]==original.entries[other],"Bibliography changed another saved pause");
    }
}
}
int main(int argc,char** argv){try{
    windows::ensure_winrt_apartment();require(argc==2,"Pass the absolute isolated bibliography fixture directory");
    const std::filesystem::path directory(argv[1]);require(directory.is_absolute(),"Bibliography QA directory must be absolute");
    const auto manifest=json(bytes(directory/"manifest.json"));predicate(directory,manifest);loader(directory,manifest);pauses();
    std::cout<<"PASS shared bibliography predicate, 10 real guides/11 citations, authenticated loader archives and three-slot pause models; no UI, execution or audio\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}

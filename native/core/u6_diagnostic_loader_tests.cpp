#include "core/u6_diagnostic_test_fixture.hpp"
#include "core/compiler_flow.hpp"

#include <functional>
#include <iostream>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;
using namespace winrt::Windows::Data::Json;
void require(bool value,std::string_view message) {if(!value)throw std::runtime_error(std::string(message));}
template<class Action>void rejects(Action&& action,std::string_view message,std::string_view expected={}) {
    try{action();}
    catch(const std::exception& error){
        require(expected.empty()||std::string_view(error.what()).find(expected)!=std::string_view::npos,
            std::string(message)+": rejected for another cause: "+error.what());return;
    }
    throw std::runtime_error(std::string(message)+": accepted an invalid document");
}
const Block& editor(const Lesson& lesson,std::string_view identity) {
    const auto position=stable_block_position(lesson,identity);require(position.has_value(),"Missing exact U6 loader fixture identity");
    return lesson.pages[position->page].blocks[position->block];
}
JsonObject editor(const JsonObject& root,std::string_view identity) {
    const auto current=root.GetNamedObject(L"stateLayout").GetNamedArray(L"current");
    const auto pages=root.GetNamedArray(L"pages");
    for(unsigned p=0;p<current.Size();++p){
        const auto ids=current.GetObjectAt(p).GetNamedArray(L"blocks");
        for(unsigned b=0;b<ids.Size();++b)if(winrt::to_string(ids.GetStringAt(b))==identity)
            return pages.GetObjectAt(p).GetNamedArray(L"blocks").GetObjectAt(b);
    }
    throw std::runtime_error("Missing exact U6 JSON editor identity");
}
void refresh_edge(const JsonObject& root,std::string_view identity) {
    const auto edges=root.GetNamedObject(L"stateLayout").GetNamedArray(L"editorial");
    for(unsigned index=edges.Size();index>0;--index){
        const auto item=edges.GetObjectAt(index-1);
        if(winrt::to_string(item.GetNamedString(L"id"))==identity){
            item.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(editor(root,identity)))));return;
        }
    }
    throw std::runtime_error("Missing exact U6 goal provenance edge");
}
void reject_both(const JsonObject& root,std::string_view message,std::string_view expected={},std::string_view locale="es") {
    const auto raw=winrt::to_string(root.Stringify());
    rejects([&]{(void)parse_lesson_json(raw,locale);},message,expected);
    rejects([&]{(void)parse_lesson_progress_metadata(raw,locale);},message,expected);
}
// One real retirement record in the format emitted by stable_state_layout.retire.
// All page shells, ordered IDs, source blocks and relationships are deep copies.
JsonObject retire_first_heading(const Lesson& source) {
    const auto root=JsonObject::Parse(winrt::to_hstring(source.raw_json));
    const auto layout=root.GetNamedObject(L"stateLayout");
    const auto pages=root.GetNamedArray(L"pages"),current=layout.GetNamedArray(L"current");
    const auto blocks=pages.GetObjectAt(0).GetNamedArray(L"blocks"),ids=current.GetObjectAt(0).GetNamedArray(L"blocks");
    require(ids.Size()>1&&ids.GetStringAt(0)==L"block_0000_0000"&&blocks.GetObjectAt(0).GetNamedString(L"type")==L"heading",
        "U6 retirement fixture no longer begins with its original heading");
    const auto identity=ids.GetStringAt(0);JsonObject snapshot;
    JsonArray retired;retired.Append(JsonValue::CreateStringValue(identity));snapshot.Insert(L"retired",retired);
    snapshot.Insert(L"current",JsonArray::Parse(current.Stringify()));
    snapshot.Insert(L"pages",JsonArray::Parse(pages.Stringify()));
    snapshot.Insert(L"compilerPuzzlesRequired",root.HasKey(L"compilerPuzzlesRequired")?
        root.GetNamedValue(L"compilerPuzzlesRequired"):JsonValue::CreateBooleanValue(false));
    for(const auto name:{L"referenceAfter",L"activityAfter"})snapshot.Insert(name,
        root.HasKey(name)?JsonArray::Parse(root.GetNamedArray(name).Stringify()):JsonArray{});
    JsonObject record;record.Insert(L"id",JsonValue::CreateStringValue(identity));
    record.Insert(L"page",current.GetObjectAt(0).GetNamedValue(L"id"));record.Insert(L"next",ids.GetAt(1));
    record.Insert(L"block",JsonObject::Parse(blocks.GetObjectAt(0).Stringify()));
    if(!layout.HasKey(L"retired"))layout.Insert(L"retired",JsonArray{});
    layout.GetNamedArray(L"retired").Append(record);
    if(!layout.HasKey(L"retirementHistory"))layout.Insert(L"retirementHistory",JsonArray{});
    layout.GetNamedArray(L"retirementHistory").Append(snapshot);
    ids.RemoveAt(0);blocks.RemoveAt(0);return root;
}
JsonObject explicit_return_without_goal(const Lesson& adopted,std::string_view identity) {
    const auto root=JsonObject::Parse(winrt::to_hstring(adopted.raw_json));
    const auto block=editor(root,identity);const auto preceding=JsonObject::Parse(block.Stringify());
    block.Remove(L"compileCheck");const auto layout=root.GetNamedObject(L"stateLayout");
    JsonObject provenance;provenance.Insert(L"lessonId",root.GetNamedValue(L"id"));
    provenance.Insert(L"locale",layout.GetNamedValue(L"locale"));provenance.Insert(L"editorial",layout.GetNamedArray(L"editorial"));
    JsonObject returns;returns.Insert(L"revision",JsonValue::CreateNumberValue(0));
    returns.Insert(L"historySha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(provenance))));
    returns.Insert(L"previousBlock",preceding);JsonObject edge;
    edge.Insert(L"id",JsonValue::CreateStringValue(winrt::to_hstring(identity)));
    edge.Insert(L"beforeSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(preceding))));
    edge.Insert(L"afterSha256",JsonValue::CreateStringValue(winrt::to_hstring(test::compile_check_test_fingerprint(block))));
    edge.Insert(L"returnTo",returns);layout.GetNamedArray(L"editorial").Append(edge);return root;
}
}

int main() try {
    unsigned passed=0,failed=0;
    std::map<std::string,Lesson> sources;
    const auto check=[&](std::string name,const std::function<void()>& action){
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    for(const auto& spec:test::u6_diagnostic_cases){
        if(!sources.contains(std::string(spec.file)))sources.emplace(std::string(spec.file),test::load_pre_u6_diagnostic_lesson(spec.file));
        const auto& before=sources.at(std::string(spec.file));const auto after=test::with_u6_diagnostic(before,spec);
        const auto& old=editor(before,spec.identity);const auto& goal=editor(after,spec.identity);
        const auto name=std::string(spec.diagnostic);
        check(name+" exact runtime/catalog adoption",[&]{
            const auto metadata=parse_lesson_progress_metadata(after.raw_json);const auto& projected=editor(metadata,spec.identity);
            require(!has_compiler_objective(old)&&goal.compile_check&&projected.compile_check
                &&goal.compile_check==projected.compile_check&&goal.compile_check->diagnostic==spec.diagnostic
                &&compiler_requires_completion(goal)&&compiler_requires_completion(projected),
                "A reviewed U6 goal disappeared, changed identity or became optional in catalog metadata");
            require(goal.initial_code==old.initial_code&&goal.project_files==old.project_files&&goal.source_files==old.source_files
                &&projected.initial_code==goal.initial_code&&projected.project_files==goal.project_files&&projected.source_files==goal.source_files,
                "Runtime/catalog adoption changed a complete U6 file or selected translation unit");
            require(exercise_key(before,old)==exercise_key(after,goal)&&draft_key("programming",before,old)==draft_key("programming",after,goal)
                &&before.state_layout.lineage==after.state_layout.lineage&&before.state_layout.signatures==after.state_layout.signatures,
                "Adopting a diagnostic changed historical family ownership or draft/progress slots");
            rejects([&]{(void)reference_objective_frame(before,old);},"A free U6 editor unexpectedly acquired an objective identity");
            require(reference_objective_frame(after,goal)==reference_objective_frame(metadata,projected),
                "The new diagnostic differed between catalog and runtime");
        });
        check(name+" source project owner and edge negatives",[&]{
            for(const std::string_view mode:{"main","file-source","path","source-selection","file-added","file-removed",
                    "output","stdin","cmake","diagnostic","owner","locale","identity","edge","before","unmapped","v1"}){
                const auto root=JsonObject::Parse(winrt::to_hstring(after.raw_json));const auto block=editor(root,spec.identity);
                const auto layout=root.GetNamedObject(L"stateLayout");const auto edges=layout.GetNamedArray(L"editorial");
                if(mode=="main")block.Insert(L"initialCode",JsonValue::CreateStringValue(winrt::to_hstring(old.initial_code+"\n// altered")));
                if(mode=="file-source")block.GetNamedArray(L"projectFiles").GetObjectAt(0).Insert(L"initialCode",JsonValue::CreateStringValue(L"// altered"));
                if(mode=="path")block.GetNamedArray(L"projectFiles").GetObjectAt(0).Insert(L"path",JsonValue::CreateStringValue(L"other.hpp"));
                if(mode=="source-selection")block.GetNamedArray(L"sourceFiles").Append(JsonValue::CreateStringValue(L"other.cpp"));
                if(mode=="file-added")block.GetNamedArray(L"projectFiles").Append(JsonObject::Parse(LR"json({"path":"extra.cpp","initialCode":"int extra;"})json"));
                if(mode=="file-removed")block.GetNamedArray(L"projectFiles").RemoveAtEnd();
                if(mode=="output")block.Insert(L"outputCheck",JsonObject::Parse(LR"json({"cases":[{"standardInput":"","outputs":[""]}]})json"));
                if(mode=="stdin")block.Insert(L"initialStandardInput",JsonValue::CreateStringValue(L""));
                if(mode=="cmake")block.Insert(L"cmake",JsonObject::Parse(LR"json({"target":"main"})json"));
                if(mode=="diagnostic")block.GetNamedObject(L"compileCheck").Insert(L"diagnostic",JsonValue::CreateStringValue(L"explicit-assignment"));
                refresh_edge(root,spec.identity);
                if(mode=="owner")root.Insert(L"id",JsonValue::CreateStringValue(L"another_lesson_owner"));
                if(mode=="locale")layout.Insert(L"locale",JsonValue::CreateStringValue(L"fr"));
                if(mode=="identity"){
                    const auto location=stable_block_position(after,spec.identity);const auto ids=layout.GetNamedArray(L"current")
                        .GetObjectAt(static_cast<unsigned>(location->page)).GetNamedArray(L"blocks");
                    const auto index=static_cast<unsigned>(location->block),prior=index-1;
                    const auto first=ids.GetAt(index),second=ids.GetAt(prior);ids.SetAt(index,second);ids.SetAt(prior,first);
                }
                if(mode=="edge")edges.RemoveAtEnd();
                if(mode=="before")edges.GetObjectAt(edges.Size()-1).Insert(L"beforeSha256",JsonValue::CreateStringValue(L"0000000000000000000000000000000000000000000000000000000000000000"));
                if(mode=="unmapped")root.Remove(L"stateLayout");
                if(mode=="v1"){
                    JsonObject v1;v1.Insert(L"version",JsonValue::CreateNumberValue(1));v1.Insert(L"origin",layout.GetNamedArray(L"origin"));
                    v1.Insert(L"current",layout.GetNamedArray(L"current"));root.Insert(L"stateLayout",v1);
                }
                reject_both(root,std::string("U6 mutation ")+std::string(mode));
            }
        });
        check(name+" explicit editorial return cannot remove the published goal",[&]{
            const auto returned=explicit_return_without_goal(after,spec.identity);
            // The complete preceding block, before/after digests, original
            // revision index and prefix-history digest are all authenticated.
            // Require the new conservation guard, never a stale hash failure.
            reject_both(returned,"A complete editorial return removed the U6 goal",
                "A published compilation diagnostic cannot be removed or replaced by an editorial return");
        });
        check(name+" authentic retirement snapshots retain their current goal",[&]{
            const auto retired=retire_first_heading(after);const auto raw=winrt::to_string(retired.Stringify());
            const auto runtime=parse_lesson_json(raw),metadata=parse_lesson_progress_metadata(raw);
            require(runtime.state_layout.retirement_history.size()==1&&metadata.state_layout.retirement_history.size()==1
                &&editor(runtime,spec.identity).compile_check==goal.compile_check&&editor(metadata,spec.identity).compile_check==goal.compile_check,
                "A valid U6 retirement snapshot lost its active diagnostic goal");
            const auto& snapshot=runtime.state_layout.retirement_history.front();
            const auto location=stable_block_position(after,spec.identity);
            require(snapshot.pages.at(location->page).blocks.at(location->block).compile_check==goal.compile_check,
                "The real retirement archive lost its reviewed compiler objective");
            // Restore the exact pre-adoption active block and its actual
            // original editorial prefix. The snapshot still records the goal;
            // every source/slot hash remains valid because its state alias is
            // intentional. Only the missing current objective must reject it.
            const auto removed=JsonObject::Parse(retired.Stringify());editor(removed,spec.identity).Remove(L"compileCheck");
            removed.GetNamedObject(L"stateLayout").Insert(L"editorial",
                JsonArray::Parse(JsonObject::Parse(winrt::to_hstring(before.raw_json)).GetNamedObject(L"stateLayout").GetNamedArray(L"editorial").Stringify()));
            reject_both(removed,"An archived U6 goal disappeared from its current owner",
                "A historical compilation diagnostic must retain its current archived or active objective");
        });
    }
    std::cout<<"U6 diagnostic loader: "<<passed<<" groups passed, "<<failed<<" failed; no compilation, UI or audio.\n";
    return failed?1:0;
} catch(const std::exception& error){std::cerr<<"FAIL U6 diagnostic loader setup: "<<error.what()<<'\n';return 1;}
catch(const winrt::hresult_error& error){std::cerr<<"FAIL U6 diagnostic loader Windows setup: "<<error.code().value<<'\n';return 1;}
catch(...){std::cerr<<"FAIL U6 diagnostic loader setup: unexpected exception\n";return 1;}

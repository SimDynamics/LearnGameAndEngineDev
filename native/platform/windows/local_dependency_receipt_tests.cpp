// Focused persistence QA, not a curricular publication or an execution of an
// archived application binary. V1 format fixtures use the published V1 formula
// and private observations produced by the CURRENT real Windows runner.
#include "core/lesson.hpp"
#include "core/lesson_state.hpp"
#include "platform/windows/compiler_completion.hpp"
#include "platform/windows/cpp_runner.hpp"
#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace lgaed;
using namespace lgaed::curriculum;
using namespace lgaed::windows;

void require(bool condition,std::string_view message) {
    if(!condition)throw std::runtime_error(std::string(message));
}
struct Contract { std::string name,source,fixed;Block block; };
Block compiler(std::string name,std::string output,std::string input={}) {
    Block block;
    block.type=BlockType::cpp_compiler;block.stable_id=std::move(name);
    block.language="cpp";block.initial_code="#include <iostream>\nint main(){}\n";
    block.standard_input=input;block.output_check.emplace();
    OutputCase visible;
    visible.standard_input=std::move(input);visible.outputs={std::move(output)};
    visible.explanation="One public case.";
    block.output_check->cases.push_back(std::move(visible));
    return block;
}
std::array<Contract,5> contracts() {
    std::array<Contract,5> result;
    auto& integer=result[0];integer.name="integer";integer.block=compiler(integer.name,"7");
    integer.source="#include <iostream>\nint main(){int dato=7;std::cout<<dato;}";
    integer.fixed="#include <iostream>\nint main(){int dato=7;std::cout<<7;}";
    integer.block.local_int_check=CppLocalIntCheck{{{"dato",13,{"13"}}}};

    auto& scalar=result[1];scalar.name="char-bool";scalar.block=compiler(scalar.name,"C 1");
    scalar.source="#include <iostream>\nint main(){char pasillo='C';bool abierta=true;std::cout<<pasillo<<' '<<abierta;}";
    scalar.fixed="#include <iostream>\nint main(){char pasillo='C';bool abierta=true;std::cout<<\"C 1\";}";
    scalar.block.local_int_check=CppLocalIntCheck{{
        {"pasillo",'D',{"D 1"},false,std::nullopt,CppLocalValueType::character},
        {"abierta",0,{"C 0"},false,std::nullopt,CppLocalValueType::boolean}}};

    auto& text=result[2];text.name="string";text.block=compiler(text.name,"Aula");
    text.source="#include <iostream>\n#include <string>\nint main(){std::string edificio=\"Aula\";std::string etiqueta=edificio;std::cout<<etiqueta;}";
    text.fixed="#include <iostream>\n#include <string>\nint main(){std::string edificio=\"Aula\";std::string etiqueta=\"Aula\";std::cout<<etiqueta;}";
    text.block.local_string_check=CppLocalStringCheck{{
        {"edificio","Control",CppLocalStringMoment::after_initialization,{"Control"}},
        {"etiqueta","Prueba",CppLocalStringMoment::before_output,{"Prueba"}}}};

    auto& input=result[3];input.name="input";input.block=compiler(input.name,"7","4");
    input.source="#include <iostream>\nint main(){int dato{};std::cin>>dato;std::cout<<dato+3;}";
    input.fixed="#include <iostream>\nint main(){int dato{};std::cin>>dato;std::cout<<7;}";
    input.block.input_check=CppInputCheck{{{"2",{"5"}}}};

    auto& role=result[4];role.name="roles";role.block=compiler(role.name,"1.5");
    role.source="#include <iostream>\nint main(){double medida=1.5;std::cout<<medida;}";
    role.fixed="#include <iostream>\nint main(){double medida=1.5;std::cout<<1.5;}";
    role.block.local_role_check=CppLocalRoleCheck{{
        {"measurement",CppLocalRoleValueType::real,1.5,{{2.5,{"2.5"},false,std::nullopt,{}}}}}};
    return result;
}
void without_private(Block& block) {
    block.local_int_check.reset();block.local_string_check.reset();
    block.input_check.reset();block.local_role_check.reset();
}
void without_private(CppRunRequest& request) {
    request.local_int_check.reset();request.local_string_check.reset();
    request.input_check.reset();request.local_role_check.reset();
}
bool private_correct(const Block& block,const FreshExecution& result) {
    if(block.local_int_check)return result.local_int_check_correct;
    if(block.local_string_check)return result.local_string_check_correct;
    if(block.input_check)return result.input_check_correct;
    return result.local_role_check_correct;
}
std::string& private_fingerprint(const Block& block,FreshExecution& result) {
    if(block.local_int_check)return result.local_int_request_fingerprint;
    if(block.local_string_check)return result.local_string_request_fingerprint;
    if(block.input_check)return result.input_request_fingerprint;
    return result.local_role_request_fingerprint;
}
void change_spec(CppRunRequest& request) {
    if(request.local_int_check)++request.local_int_check->probes.front().replacement;
    else if(request.local_string_check)request.local_string_check->probes.front().replacement+=" changed";
    else if(request.input_check)request.input_check->probes.front().outputs.front()+=" changed";
    else request.local_role_check->roles.front().probes.front().replacement+=1;
}
void preserve(const state::StateData& after,const state::StateData& before) {
    for(const auto& [domain,values]:before.preferences)
        for(const auto& [key,value]:values)
            require(after.preferences.at(domain).at(key)==value,
                "A dependency attempt changed a historical key or unrelated preference");
}
std::string root_key(const Lesson& lesson,const Block& block) {
    return "compiler-puzzle/v1/"+lesson.locale+'/'+lesson.id+'/'+exercise_key(lesson,block)+'/'+
        state::sha256(reference_objective_frame(lesson,block))+'/';
}
std::string current_key(const Lesson& lesson,const Block& block) {
    const auto suffix=block.local_int_check?"local-int/v2/":block.local_string_check?"local-string/v2/":
        block.input_check?"input/v1/":"local-roles/v1/";
    return root_key(lesson,block)+suffix;
}
std::string field(std::string_view value) {return std::to_string(value.size())+':'+std::string(value);}
std::string v1_local_seal(std::string_view key,std::string_view program,const Block& block) {
    // Exact published preimages, used only to construct a compatible saved
    // format after CURRENT real private evidence has been checked below.
    if(block.local_int_check) {
        auto frame=std::string("compiler-local-int-complete-v1/")+field(key)+field(program);
        for(const auto& probe:block.local_int_check->probes) {
            if(probe.value_type!=CppLocalValueType::integer)
                frame+=field("value-type-v1")+field(probe.value_type==CppLocalValueType::character?"char":"bool");
            frame+=field(probe.variable)+field(std::to_string(probe.replacement));
            if(probe.after_update)frame+=field("after-update-v1");
            if(probe.expected_increase)frame+=field("exact-increase-v1")+field(std::to_string(*probe.expected_increase));
            for(const auto& output:probe.expected_stdout)frame+=field(output);
        }
        return state::sha256(frame);
    }
    auto frame=std::string("compiler-local-string-complete-v1/")+field(key)+field(program);
    for(const auto& probe:block.local_string_check->probes) {
        frame+=field(probe.variable)+field(probe.replacement)+field(std::to_string(static_cast<int>(probe.moment)));
        for(const auto& output:probe.expected_stdout)frame+=field(output);
    }
    return state::sha256(frame);
}
state::StateData public_history(const Lesson& original,const CppRunRequest& request,
        const CppRunResult& observation) {
    auto plain=request;without_private(plain);
    const auto captured=capture_cpp_execution(plain,observation);
    state::StateData data;
    data.preferences["learning_progress"]["unrelated"]=std::int64_t{42};
    require(record_compiler_puzzle_case(data,original,original.pages.front().blocks.front(),
        plain,captured,"Real historical public execution"),"Could not record the real public execution");
    return data;
}
void check_review_negatives(const Lesson& lesson,const Block& block,const CppRunRequest& request,
        const FreshExecution& captured,const state::StateData& before) {
    for(int index=0;index<9;++index) {
        auto changed=request;auto proof=captured;auto data=before;
        switch(index) {
        case 0:++proof.source_revision;break;
        case 1:proof.compilation_succeeded=false;break;
        case 2:proof.execution_completed=false;proof.execution_succeeded=false;break;
        case 3:private_fingerprint(block,proof).clear();break;
        case 4:private_fingerprint(block,proof)="foreign";break;
        case 5:changed.source_utf8+="\n// other source\n";break;
        case 6:changed.stdin_utf8+=" ";break;
        case 7:changed.project_key+="other";break;
        case 8:change_spec(changed);break;
        }
        require(!record_compiler_puzzle_review(data,lesson,block,changed,proof,"Unbound rejection")&&data==before,
            "An empty, stale, wrong-phase/source/input/project/spec capture rewrote a review");
    }
    auto empty=before;
    require(!record_compiler_puzzle_review(empty,lesson,block,request,FreshExecution{},"Empty capture")&&empty==before,
        "An empty execution capture created a private review");
}
void legacy_compatibility(const Lesson& lesson,const Lesson& original,const CppRunRequest& good_request,
        const CppRunResult& good_result,const FreshExecution& good_capture,
        const state::StateData& reviewed_failure) {
    const auto& block=lesson.pages.front().blocks.front();
    if(!block.local_int_check&&!block.local_string_check)return;
    require(private_correct(block,good_capture),"A V1 fixture needs CURRENT real private evidence");
    auto saved=public_history(original,good_request,good_result);
    const auto old=root_key(lesson,block),key=current_key(lesson,block);
    const auto suffix=block.local_int_check?"local-int/v1":"local-string/v1";
    const auto program=compiler_program_frame(good_request);
    saved.preferences["native_windows"][old+suffix]=v1_local_seal(old,program,block);
    const auto legacy=saved;
    validate_compiler_puzzle_state(saved,lesson,block);
    require(compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==1&&saved==legacy,
        "An authentic private V1 format lost its read-only compatibility");
    prepare_compiler_puzzle_revisions(saved,lesson);
    require(compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==1&&
        state::find(saved,"native_windows",key+suffix)&&
        state::find(saved,"native_windows",key+"legacy-adopted/v1"),"An authenticated V1 proof was not adopted into V2");
    preserve(saved,legacy);
    const auto once=saved;prepare_compiler_puzzle_revisions(saved,lesson);
    require(saved==once,"V1 adoption was not idempotent");

    for(int index=0;index<6;++index) {
        auto corrupted=legacy;
        auto& values=corrupted.preferences["native_windows"];
        switch(index) {
        case 0:values[old+suffix]=std::string("forged");break;
        case 1:values[old+"cases"]=std::vector<std::string>{"foreign"};break;
        case 2:values[old+"completed"]=std::string("forged");break;
        case 3:values[old+"program"]=program+"other";break;
        case 4:values[old+suffix]=true;break;
        case 5:values[key+"legacy-adopted/v1"]=std::string("true");break;
        }
        const auto before=corrupted;bool rejected=false;
        try{prepare_compiler_puzzle_revisions(corrupted,lesson);}catch(const std::exception&){rejected=true;}
        require(rejected&&corrupted==before,"Malformed V1 or adoption provenance was accepted or partly mutated");
    }
    reset_compiler_puzzle(saved,lesson,block);
    require(!compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==0,
        "Reset revived an authentic V1 private receipt");
    prepare_compiler_puzzle_revisions(saved,lesson);
    require(!compiler_puzzle_completed(saved,lesson,block),"Preparation resurrected a reset V1 proof");
    preserve(saved,legacy);

    // A separately saved V2 rejection wins over a later compatible V1 import.
    auto attempt=legacy;
    for(const auto& [name,value]:reviewed_failure.preferences.at("native_windows"))
        if(name.starts_with(key))attempt.preferences["native_windows"][name]=value;
    require(!compiler_puzzle_completed(attempt,lesson,block),"A new V2 attempt fell back to old private credit");
    prepare_compiler_puzzle_revisions(attempt,lesson);
    require(!compiler_puzzle_completed(attempt,lesson,block),"Adoption overwrote an existing V2 rejection");
    preserve(attempt,legacy);
    std::cout<<"PASS compatible V1 format "<<block.stable_id<<": real current private evidence, exact old seals, "
        "read/adopt once, six corruptions rejected without mutation, Reset and a new V2 attempt prevent resurrection.\n";
}
void run_contract(const Contract& contract) {
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"local-receipt-compat-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64())+L"-"+winrt::to_hstring(contract.name).c_str(),false});
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
        !std::filesystem::exists(environment.paths.root),"Receipt QA would open an existing or personal profile");
    initialize_profile_directories(environment.paths);
    Lesson lesson;lesson.id="isolated_dependency_receipts_"+contract.name;lesson.locale="es";
    lesson.compiler_puzzles_required=true;Page page;page.blocks.push_back(contract.block);lesson.pages.push_back(std::move(page));
    auto original=lesson;without_private(original.pages.front().blocks.front());
    const auto& block=lesson.pages.front().blocks.front();
    const auto& former=original.pages.front().blocks.front();
    const auto request=[&](std::string source,std::uint64_t number) {
        CppRunRequest current;current.request_id=number;current.revision=number+100;
        current.source_utf8=std::move(source);current.stdin_utf8=block.standard_input.value_or("");
        current.project_key=draft_key("programming",lesson,block);current.projects_directory=environment.paths.projects;
        current.compile_timeout=std::chrono::seconds(60);current.execution_timeout=std::chrono::seconds(8);
        current.local_int_check=block.local_int_check;current.local_string_check=block.local_string_check;
        current.input_check=block.input_check;current.local_role_check=block.local_role_check;
        return current;
    };
    const auto good=request(contract.source,101),bad=request(contract.fixed,102);
    require(compiler_runtime_request_allowed(block,good)&&compiler_runtime_request_allowed(block,bad),
        "Receipt QA requires valid private plans even for the fixed-output counterexample");
    const auto good_result=run_cpp(good),bad_result=run_cpp(bad);
    const auto good_capture=capture_cpp_execution(good,good_result),bad_capture=capture_cpp_execution(bad,bad_result);
    for(const auto* result:{&good_result,&bad_result})
        require(result->error_utf8.empty()&&result->compilation.succeeded()&&result->execution.succeeded(),
            "A real receipt QA public compilation or execution failed");
    require(evaluate_cpp_output_check(*block.output_check,good_capture,good.revision).verdict==OutputVerdict::correct&&
        evaluate_cpp_output_check(*block.output_check,bad_capture,bad.revision).verdict==OutputVerdict::correct&&
        private_correct(block,good_capture)&&!private_correct(block,bad_capture),
        "Receipt QA did not distinguish real correct and fixed-output private observations");
    auto saved=public_history(original,bad,bad_result);
    state::Draft draft;draft.source=contract.fixed;draft.input=bad.stdin_utf8;draft.selection_start=3;draft.selection_end=9;
    state::put_draft(saved,draft_key("programming",lesson,block),draft);
    const auto legacy=saved;
    require(compiler_puzzle_completed(saved,original,former)&&compiler_puzzle_case_count(saved,original,former)==1&&
        !compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==0,
        "A historical stdout result was promoted into a dependency epoch");
    const std::string feedback="Incorrecto. Comprueba que el resultado dependa de los datos.";
    require(record_compiler_puzzle_review(saved,lesson,block,bad,bad_capture,feedback)&&
        compiler_puzzle_review(saved,lesson,block,bad)==feedback&&!compiler_puzzle_completed(saved,lesson,block),
        "A public-correct/private-incorrect result did not retain its bound rejection");
    preserve(saved,legacy);check_review_negatives(lesson,block,bad,bad_capture,saved);
    auto new_identity=bad;new_identity.request_id+=1000;new_identity.revision+=1000;
    require(compiler_puzzle_review(saved,lesson,block,new_identity)==feedback,
        "Reopening with new ephemeral IDs discarded the exact source/spec rejection");
    for(int index=0;index<4;++index) {
        auto changed=bad;auto owner=block;
        if(index==0)changed.source_utf8+="\n// changed draft\n";
        else if(index==1)changed.stdin_utf8+=" ";
        else if(index==2)changed.project_key+="other";
        else {
            change_spec(changed);owner.local_int_check=changed.local_int_check;
            owner.local_string_check=changed.local_string_check;owner.input_check=changed.input_check;
            owner.local_role_check=changed.local_role_check;
        }
        require(compiler_puzzle_review(saved,lesson,owner,changed).empty(),
            "Saved feedback was replayed for another source, input, project or private spec");
    }
    const auto key=current_key(lesson,block);
    for(const auto* suffix:{"rules","feedback","seal"}) {
        auto altered=saved;altered.preferences["native_windows"][key+"review/"+suffix]=std::string("foreign");
        require(compiler_puzzle_review(altered,lesson,block,bad).empty(),"A changed persisted rejection was accepted");
    }
    auto changed_capture=saved;
    changed_capture.preferences["native_windows"][key+"review/capture"]=std::vector<std::string>{};
    require(compiler_puzzle_review(changed_capture,lesson,block,bad).empty(),"An empty review capture was accepted");
    {
        state::StateStore writer(environment.paths.root);require(writer.empty(),"Receipt QA StateStore was not fresh");
        (void)writer.load();writer.save(saved);
    }
    {
        state::StateStore reader(environment.paths.root);const auto reopened=reader.load();
        require(reopened==saved&&compiler_puzzle_review(reopened,lesson,block,bad)==feedback,
            "Disk reopen lost the exact private rejection or an existing receipt");
    }
    const auto rejected=saved;
    legacy_compatibility(lesson,original,good,good_result,good_capture,rejected);
    invalidate_compiler_puzzle_attempt(saved,lesson,block,good);
    require(compiler_puzzle_review(saved,lesson,block,good).empty()&&
        !compiler_puzzle_completed(saved,lesson,block),"Editing carried the prior rejection or invented completion");
    preserve(saved,legacy);
    require(record_compiler_puzzle_case(saved,lesson,block,good,good_capture,"Dependencia comprobada")&&
        compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==1&&
        compiler_puzzle_review(saved,lesson,block,good).empty(),"The different correct source failed to earn its own epoch");
    preserve(saved,legacy);
    reset_compiler_puzzle(saved,lesson,block);prepare_compiler_puzzle_revisions(saved,lesson);
    require(!compiler_puzzle_completed(saved,lesson,block)&&compiler_puzzle_case_count(saved,lesson,block)==0&&
        compiler_puzzle_completed(saved,original,former),"Reset removed stdout history or revived dependency credit");
    preserve(saved,legacy);
    {
        state::StateStore writer(environment.paths.root);(void)writer.load();writer.save(saved);
    }
    {
        state::StateStore reader(environment.paths.root);const auto reopened=reader.load();
        require(reopened==saved&&!compiler_puzzle_completed(reopened,lesson,block)&&
            compiler_puzzle_completed(reopened,original,former),"Disk reopen revived a reset objective or lost old stdout");
    }
    std::cout<<"PASS dependency review "<<contract.name<<": real public-correct/private-failed run, nine capture negatives plus empty capture, "
        "four replay and four saved-review tamper negatives, exact disk reopen, different corrected source, preserved stdout/draft, scoped Reset.\n";
}

void joint_contract_evolution() {
    const auto environment=resolve_windows_environment({ProfileId::Codex,
        L"joint-local-receipts-"+std::to_wstring(GetCurrentProcessId())+L"-"+
        std::to_wstring(GetTickCount64()),false});
    require(environment.options.qa_instance&&!environment.options.qa_instance->empty()&&
        !std::filesystem::exists(environment.paths.root),"Joint receipt QA would open an existing profile");
    initialize_profile_directories(environment.paths);
    const std::string source="#include <iostream>\nint main(){int primero=2;int segundo=3;"
        "int tercero=4;int cuarto=5;std::cout<<primero+segundo+tercero+cuarto;}";
    Lesson scalar;scalar.id="isolated_joint_local_int_receipts";scalar.locale="es";
    scalar.compiler_puzzles_required=true;
    Page page;page.blocks.push_back(compiler("joint-integer","14"));
    page.blocks.front().initial_code=source;
    page.blocks.front().local_int_check=CppLocalIntCheck{{{"primero",5,{"17"}}}};
    scalar.pages.push_back(std::move(page));
    auto joint=scalar;
    CppLocalIntProbe pair{"primero",5,{"28"}};
    pair.additional_replacements={{"segundo",7},{"tercero",11}};
    joint.pages.front().blocks.front().local_int_check->probes.push_back(pair);
    auto appended=joint;
    appended.pages.front().blocks.front().local_int_check->probes.push_back({"segundo",13,{"24"}});
    const std::array lessons{scalar,joint,appended};
    const auto& first=lessons.front().pages.front().blocks.front();
    auto public_only=first;public_only.local_int_check.reset();
    require(reference_objective_frame(scalar,first)==reference_objective_frame(scalar,public_only),
        "An empty additional-replacement vector changed the published scalar objective");
    const auto saved_draft_key=draft_key("programming",scalar,first);
    const auto make_request=[&](const Lesson& lesson,std::uint64_t number) {
        const auto& block=lesson.pages.front().blocks.front();
        CppRunRequest request;request.request_id=number;request.revision=number+500;
        request.source_utf8=source;request.stdin_utf8=block.standard_input.value_or("");
        request.project_key=draft_key("programming",lesson,block);
        request.projects_directory=environment.paths.projects;
        request.compile_timeout=std::chrono::seconds(60);request.execution_timeout=std::chrono::seconds(8);
        request.local_int_check=block.local_int_check;
        return request;
    };
    std::array<CppRunRequest,3> requests;
    std::array<CppRunResult,3> results;
    std::array<FreshExecution,3> captures;
    std::array<state::StateData,3> histories;
    state::StateData saved;
    saved.preferences["learning_progress"]["unrelated"]=std::int64_t{42};
    saved.preferences["native_windows"]["reader-setting"]=std::string("unchanged");
    state::Draft draft;draft.source=source+"\n// unfinished saved edit\n";
    draft.input="saved input is not an execution";draft.selection_start=3;draft.selection_end=19;
    state::put_draft(saved,saved_draft_key,draft);
    const auto verify_draft=[&](const state::StateData& data) {
        const auto actual=state::get_draft(data,saved_draft_key,source);
        require(actual.has_saved&&actual.source==draft.source&&actual.input==draft.input&&
            actual.selection_start==draft.selection_start&&actual.selection_end==draft.selection_end,
            "A joint contract revision changed the source, input or selection of the existing draft");
    };
    const auto reopen=[&](const state::StateData& data) {
        {state::StateStore writer(environment.paths.root);(void)writer.load();writer.save(data);}
        state::StateStore reader(environment.paths.root);const auto reopened=reader.load();
        require(reopened==data,"A joint contract disk reopen changed saved history");
        verify_draft(reopened);return reopened;
    };
    for(std::size_t stage=0;stage<lessons.size();++stage) {
        const auto& lesson=lessons[stage];const auto& block=lesson.pages.front().blocks.front();
        requests[stage]=make_request(lesson,100+stage);
        const auto& request=requests[stage];
        require(request.project_key==saved_draft_key&&compiler_runtime_request_allowed(block,request),
            "A joint revision moved the draft or rejected the valid real source");
        const auto fingerprint=cpp_local_int_request_fingerprint(request);
        require(!fingerprint.empty(),"A valid joint receipt request lacks its complete frame");
        if(stage==0) {
            // Freeze a published preimage independently of the new serializer.
            // The original scalar override inserts precisely this assignment.
            auto variant=source;const auto at=variant.find("int primero=2;");
            require(at!=variant.npos,"The historical scalar preimage lost its declaration");
            variant.insert(at+std::string_view("int primero=2;").size(),"\n    primero = 5;\n");
            const auto historical=std::string("compiler-local-int-dependency-v1/")+
                field(request.project_key)+field(source)+field(request.stdin_utf8)+
                field("primero")+field("5")+field(state::sha256(variant))+field("17");
            require(fingerprint==state::sha256(historical),
                "A scalar request no longer has its byte-exact published fingerprint");
        }else {
            const auto& previous=lessons[stage-1];const auto& old=previous.pages.front().blocks.front();
            require(current_key(lesson,block)!=current_key(previous,old)&&
                fingerprint!=cpp_local_int_request_fingerprint(requests[stage-1]),
                "Appending a joint or later scalar probe reused the earlier objective or capture frame");
            const auto before=saved;
            validate_compiler_puzzle_state(saved,lesson,block);
            require(saved==before&&!compiler_puzzle_completed(saved,lesson,block)&&
                compiler_puzzle_case_count(saved,lesson,block)==0,
                "The stronger plan read, mutated or credited an old private receipt");
            prepare_compiler_puzzle_revisions(saved,lesson);preserve(saved,before);
            const auto prepared=saved;
            require(!record_compiler_puzzle_case(saved,lesson,block,request,captures[stage-1],"borrowed")&&
                !record_compiler_puzzle_review(saved,lesson,block,request,captures[stage-1],"borrowed")&&
                saved==prepared,"A stronger plan borrowed an earlier success or review capture");
            require(!capture_cpp_execution(request,results[stage-1]).local_int_check_correct,
                "A previous private execution was rebound to an appended plan");
            auto relocated=saved;
            const auto old_key=current_key(previous,old),new_key=current_key(lesson,block);
            for(const auto& [name,value]:before.preferences.at("native_windows"))
                if(name.starts_with(old_key))
                    relocated.preferences["native_windows"][new_key+name.substr(old_key.size())]=value;
            const auto forged=relocated;bool rejected=false;
            try{validate_compiler_puzzle_state(relocated,lesson,block);}catch(const std::exception&){rejected=true;}
            require(rejected&&relocated==forged,"Relocated old seals were accepted or altered during rejection");
        }
        results[stage]=run_cpp(request);const auto& result=results[stage];
        require(result.error_utf8.empty()&&result.compilation.succeeded()&&result.execution.succeeded()&&
            result.target_type=="EXECUTABLE"&&result.artifact.extension()==L".exe"&&
            std::filesystem::is_regular_file(result.artifact)&&
            result.local_int_evidence.size()==block.local_int_check->probes.size(),
            "A real MSVC joint receipt run did not execute all public and private phases");
        captures[stage]=capture_cpp_execution(request,result);
        require(captures[stage].local_int_check_correct&&
            captures[stage].local_int_request_fingerprint==fingerprint&&
            evaluate_cpp_output_check(*block.output_check,captures[stage],request.revision).verdict==OutputVerdict::correct,
            "The actual joint source did not earn its public case and exact private evidence");
        const auto before=saved;
        require(record_compiler_puzzle_case(saved,lesson,block,request,captures[stage],"Fresh joint dependency evidence"),
            "Fresh joint evidence failed to create its own receipt");
        preserve(saved,before);verify_draft(saved);
        if(stage==0) {
            const auto key=current_key(lesson,block);
            require(state::string_value(saved,"native_windows",key+"local-int/v1")==
                v1_local_seal(key,compiler_program_frame(request),block),
                "The published scalar completion preimage changed");
        }
        saved=reopen(saved);
        for(std::size_t old=0;old<=stage;++old) {
            const auto& older=lessons[old];const auto& owner=older.pages.front().blocks.front();
            require(compiler_puzzle_completed(saved,older,owner)&&compiler_puzzle_case_count(saved,older,owner)==1,
                "A joint revision lost an earlier success or inserted private public-case receipts");
        }
        histories[stage]=saved;
    }
    const auto& joint_block=lessons[1].pages.front().blocks.front();
    for(int mutation=0;mutation<8;++mutation) {
        auto changed=lessons[1];auto& owner=changed.pages.front().blocks.front();
        auto& probes=owner.local_int_check->probes;auto& replacements=probes.back().additional_replacements;
        switch(mutation) {
        case 0:++replacements.front().replacement;break;
        case 1:replacements.front().variable="cuarto";break;
        case 2:std::reverse(replacements.begin(),replacements.end());break;
        case 3:replacements.push_back({"cuarto",13});break;
        case 4:replacements.pop_back();break;
        case 5:probes.back().expected_stdout.front()="29";break;
        case 6:++probes.front().replacement;break;
        case 7:probes.push_back({"segundo",13,{"24"}});break;
        }
        auto request=requests[1];request.local_int_check=owner.local_int_check;
        const auto frame=cpp_local_int_request_fingerprint(request);
        require(compiler_runtime_request_allowed(owner,request)&&!frame.empty()&&
            frame!=cpp_local_int_request_fingerprint(requests[1])&&
            reference_objective_frame(changed,owner)!=reference_objective_frame(lessons[1],joint_block)&&
            !capture_cpp_execution(request,results[1]).local_int_check_correct,
            "A changed joint name, value, order, count, expectation or plan retained an earlier identity");
    }
    auto tampered=results[1];
    tampered.local_int_evidence.back().instrumented_source_sha256=
        results[0].local_int_evidence.front().instrumented_source_sha256;
    const auto rejected_joint=capture_cpp_execution(requests[1],tampered);
    require(!rejected_joint.local_int_check_correct&&rejected_joint.local_int_check_feedback==
        "Comprueba cómo se combinan los valores de las variables 'primero', 'segundo', 'tercero'. "
        "La comprobación con otros valores no produjo el resultado esperado.",
        "A scalar-only source impersonated a joint run or its feedback exposed values/claimed only one variable");
    const auto& final=lessons.back();const auto& owner=final.pages.front().blocks.front();
    reset_compiler_puzzle(saved,final,owner);prepare_compiler_puzzle_revisions(saved,final);
    preserve(saved,histories[1]);saved=reopen(saved);
    require(!compiler_puzzle_completed(saved,final,owner)&&compiler_puzzle_case_count(saved,final,owner)==0&&
        compiler_puzzle_completed(saved,lessons[0],first)&&
        compiler_puzzle_completed(saved,lessons[1],joint_block),
        "Reset resurrected the expanded plan or erased either historical private achievement");
    std::cout<<"PASS joint local-int receipt evolution: three real MSVC requests, nine public/private phases, "
        "scalar -> joint -> appended scalar, exact legacy preimages, eight changed-plan bindings, "
        "no capture/seal borrowing, preserved old achievements/draft, disk reopen and scoped Reset.\n";
}
}
int main()try {
    winrt::init_apartment();
    for(const auto& contract:contracts())run_contract(contract);
    joint_contract_evolution();
    std::cout<<"NOTE no-plan scope: no private execution is claimed or persisted without a valid frame; "
        "visible structural guidance is covered by the separate LessonView QA.\n";
    return 0;
}catch(const winrt::hresult_error& error) {
    std::cerr<<"FAIL dependency receipts: "<<winrt::to_string(error.message())<<'\n';return 1;
}catch(const std::exception& error) {
    std::cerr<<"FAIL dependency receipts: "<<error.what()<<'\n';return 1;
}

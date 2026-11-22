#include "core/integer_battery_check.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed::curriculum;
void require(bool condition,std::string_view reason) {
    if(!condition)throw std::runtime_error(std::string(reason));
}
std::string replace(std::string source,std::string_view before,std::string_view after) {
    const auto found=source.find(before);require(found!=source.npos,"Missing intended source edit");
    source.replace(found,before.size(),after);return source;
}
constexpr std::string_view boxes=R"cpp(#include "envio/cajas.h"

int cajas_para(int piezas) {
    return piezas / 4;
}
)cpp";
constexpr std::string_view batches=R"cpp(#include "lotes/cantidad.h"

int lotes_para(int piezas) {
    return (piezas + 5) / 6;
}
)cpp";
IntegerBatterySpec contract(bool defective) {
    IntegerBatterySpec spec;
    spec.library_path=defective?"src/cajas.cpp":"motor/cantidad.cpp";
    spec.test_path=defective?"pruebas/cajas_casos.cpp":"pruebas/lotes_casos.cpp";
    spec.function_name=defective?"cajas_para":"lotes_para";
    spec.test_target=defective?"comprobar_cajas":"verificar_lotes";
    const auto definition=integer_battery_extract_definition(defective?boxes:batches,spec.function_name);
    require(definition.has_value(),"Authentic original function was rejected");
    spec.protected_definition=*definition;
    if(defective) {
        spec.cases={{0,0},{4,1},{8,2},{20,5},{1,1},{5,2},{17,5}};
        spec.published_result=IntegerBatteryPublishedResult::failure;
        spec.failure_exit_code.reset();
        spec.prerequisite=IntegerBatteryPrerequisite{"consultar_cajas",{{4,1},{8,2}},"","1:2","",0};
    }else spec.cases={{0,0},{6,1},{7,2},{18,3}};
    require(integer_battery_spec_valid(spec),"Authentic battery specification was rejected");
    return spec;
}
enum class Format { rows,alpha,numeric,groups,conditional,numbers,always_ok,reordered_ok };
std::string report(const IntegerBatterySpec& spec,const IntegerBatteryRun& run,Format format) {
    std::string result;
    // Deferred reports can reorder the cases, including failures first.
    auto reported=run.calls;std::ranges::reverse(reported);
    if(format==Format::conditional)std::stable_sort(reported.begin(),reported.end(),[&](const auto& a,const auto& b) {
        const auto expected=[&](const auto& value) {
            return std::ranges::find(spec.cases,value.input,&IntegerBatteryCase::input)->expected;
        };
        return (a.result!=expected(a))>(b.result!=expected(b));
    });
    for(auto entry=reported.begin();entry!=reported.end();++entry) {
        const auto found=std::ranges::find(spec.cases,entry->input,&IntegerBatteryCase::input);
        require(found!=spec.cases.end(),"Unknown synthetic report input");
        const auto input=std::to_string(entry->input),expected=std::to_string(found->expected),
            actual=std::to_string(entry->result);
        const bool good=found->expected==entry->result;
        if(format==Format::reordered_ok)
            result+=good?"dato="+input+" esperado="+expected+" obtenido="+actual+" OK\n":
                "obtenido="+actual+" dato="+input+" esperado="+expected+" OK\n";
        else if(format==Format::conditional)
            result+=good?"dato="+input+" esperado="+expected+" obtenido="+actual+" coincide\n":
                "esperaba="+expected+" obtuve="+actual+" para="+input+" difiere\n";
        else if(format==Format::alpha)
            result+="resultado="+actual+" previsto="+expected+" dato="+input+
                " coincide="+(good?"true":"false")+"\n";
        else if(format==Format::numeric)
            result+="dato="+input+" esperado="+expected+" obtenido="+actual+
                " coincide="+(good?"1":"0")+"\n";
        else if(format==Format::groups)
            result+="Dato: "+input+"\nEsperado: "+expected+"\nObtenido: "+actual+
                "\nEstado: "+(good?"OK":"FALLO")+"\n\n";
        else if(format==Format::numbers)
            result+=input+" "+expected+" "+actual+"\n";
        else result+="piezas="+input+" esperado="+expected+" obtenido="+actual+
            (good||format==Format::always_ok?" OK\n":" FALLO\n");
    }
    return result;
}
std::vector<IntegerBatteryRun> observations(const IntegerBatterySpec& spec,Format format=Format::rows) {
    std::vector<IntegerBatteryRun> runs;
    for(std::size_t r=0;r<spec.cases.size()+2;++r) {
        IntegerBatteryRun run;
        run.program_frame="authentic-current-project";
        run.compiled=run.executed=run.completed_normally=true;
        if(r==0)run.phase={IntegerBatteryMutation::control,{}};
        else if(r==spec.cases.size()+1)run.phase={IntegerBatteryMutation::published,{}};
        else run.phase={IntegerBatteryMutation::mutate,r-1};
        bool failures=false;
        for(std::size_t i=0;i<spec.cases.size();++i) {
            const auto& c=spec.cases[i];auto actual=c.expected;
            if(run.phase.mutation==IntegerBatteryMutation::published&&
               spec.published_result==IntegerBatteryPublishedResult::failure)
                actual=c.input/4;
            else if(run.phase.target==i)
                actual=c.expected==std::numeric_limits<std::int32_t>::min()?c.expected+1:c.expected-1;
            run.calls.push_back({c.input,actual});
            failures=failures||actual!=c.expected;
        }
        // A different call/report order in alternate phases is legitimate.
        if(r%2)std::ranges::reverse(run.calls);
        run.exit_code=failures?1u:0u;
        auto text=report(spec,run,format);
        if(r%2)run.stderr_bytes=std::move(text);else run.stdout_bytes=std::move(text);
        runs.push_back(std::move(run));
    }
    return runs;
}
bool accepted(const IntegerBatterySpec& spec,const std::vector<IntegerBatteryRun>& runs) {
    return integer_battery_verify(spec,runs,"authentic-current-project").accepted;
}
void source_and_authoring() {
    auto spec=contract(true);
    require(integer_battery_preserves_function(boxes,spec),"Original protected source was rejected");
    const auto forged_call=std::string(boxes)+R"cpp(
const char* injected_trace = R"tag(LGAED_INTEGER_BATTERY_CALL:0,4,1)tag";
)cpp";
    require(!integer_battery_extract_definition(forged_call,"cajas_para"),
        "An integer-battery call marker inside a student literal was accepted");
    require(!integer_battery_instrument(forged_call,spec,{IntegerBatteryMutation::control,{}}),
        "An untrusted integer-battery call marker reached native execution");
    const auto forged_end=std::string(boxes)+
        "\nconst char* end_marker = \"LGAED_INTEGER_BATTERY_END\";\n";
    require(!integer_battery_extract_definition(forged_end,"cajas_para"),
        "A student string impersonated the integer-battery completion witness");
    const auto documented=std::string(boxes)+
        "\n// LGAED_INTEGER_BATTERY_END is documentation, not an executed record.\n";
    require(integer_battery_extract_definition(documented,"cajas_para").has_value(),
        "An inert comment was mistaken for a private execution record");
    require(integer_battery_preserves_function(replace(std::string(boxes),"return piezas / 4;",
        "return /* preserve defect */ piezas / 4 ;"),spec),"Whitespace/comments changed the protected body");
    require(!integer_battery_preserves_function(replace(std::string(boxes),"piezas / 4","(piezas + 3) / 4"),spec),
        "Repairing the library before building the battery was accepted");
    require(integer_battery_extract_definition(
        "int cajas_para(int piezas);\n"+std::string(boxes),"cajas_para").has_value(),
        "Exact global prototype was rejected");
    for(const auto& prefix:std::array<std::string,5>{"static int","inline int","int&","int*","long"})
        require(!integer_battery_extract_definition(replace(std::string(boxes),"int cajas_para",prefix+" cajas_para"),"cajas_para"),
            "An unsupported declaration was admitted");
    for(const auto& invalid:std::array<std::string,6>{
            std::string(boxes)+"\nint cajas_para(int a,int b){return a+b;}",
            std::string(boxes)+"\nint& cajas_para(int a,int b);",
            "#define VALUE 1\n"+std::string(boxes),
            "namespace nested {\n"+std::string(boxes)+"\n}",
            replace(std::string(boxes),"return piezas / 4;","return sizeof(__func__);"),
            replace(std::string(boxes),"int piezas","int piezas=0")})
        require(!integer_battery_extract_definition(invalid,"cajas_para"),
            "Ambiguous, nested, preprocessed or lambda-sensitive source was admitted");
    require(!integer_battery_extract_definition("int main(int n){return n;}","main"),
        "main became a protected component");
    require(integer_battery_driver_valid("#include <iostream>\nint main(){return 0;}\n"),
        "A normal editable driver was rejected");
    require(!integer_battery_driver_valid("int lgaed_integer_battery_phase=0;"),
        "The driver could inspect a private implementation name");
    require(!integer_battery_driver_valid("#define ASSERT(x) x\nint main(){return 0;}"),
        "An editable macro bypassed the bounded source contract");
    auto changed=spec;changed.failure_exit_code=0;
    require(!integer_battery_spec_valid(changed),"Failure was allowed to return success");
    changed=spec;changed.cases.push_back(changed.cases.front());
    require(!integer_battery_spec_valid(changed),"Duplicate expected input was admitted");
    changed=spec;changed.library_path="../src/cajas.cpp";
    require(!integer_battery_spec_valid(changed),"Unsafe library path was admitted");
    changed=spec;changed.prerequisite->target=changed.test_target;
    require(!integer_battery_spec_valid(changed),"The checker replaced its prerequisite client");
    changed=spec;changed.prerequisite->exit_code=1;
    require(!integer_battery_spec_valid(changed),"Nonzero client completion was admitted");
    changed=spec;changed.prerequisite->calls.push_back(changed.prerequisite->calls.front());
    require(integer_battery_spec_valid(changed),"Legitimate repeated ordered client call was rejected");
    changed=spec;changed.prerequisite->standard_output.push_back('\0');
    require(!integer_battery_spec_valid(changed),"A NUL entered the declared client stream");
    changed=spec;changed.protected_definition="// harmless\n"+changed.protected_definition;
    require(integer_battery_spec_frame(changed)==integer_battery_spec_frame(spec),
        "Inert comments changed the objective identity");
    for(unsigned field=0;field<4;++field) {
        changed=spec;
        if(field==0)changed.cases[0].expected=9;
        if(field==1)changed.failure_exit_code=257;
        if(field==2)changed.prerequisite->calls[0].result=2;
        if(field==3)changed.prerequisite->standard_output="other";
        require(integer_battery_spec_frame(changed)!=integer_battery_spec_frame(spec),
            "Changed requirements borrowed the preceding objective frame");
    }
    std::cout<<"PASS integer battery source protection, authoring limits and complete objective frame\n";
}
void diagnostics() {
    for(const bool defective:{false,true}) {
        const auto spec=contract(defective);
        for(const auto format:{Format::rows,Format::alpha,Format::numeric,Format::groups,Format::conditional})
            require(accepted(spec,observations(spec,format)),
                "Equivalent deferred/reordered, channel-independent per-case reporting was rejected");
        for(const auto format:{Format::numbers,Format::always_ok,Format::reordered_ok})
            require(!accepted(spec,observations(spec,format)),
                "Printed quantities or a fixed OK masqueraded as a comparison verdict");
        auto runs=observations(spec);
        auto fixed=runs.front();
        for(auto& run:runs) {
            run.stdout_bytes=report(spec,fixed,Format::rows)+"Fallos="+std::to_string(run.exit_code)+"\n";
            run.stderr_bytes.clear();
        }
        require(!accepted(spec,runs),"A fixed full table plus changing generic summary was accepted");
        runs=observations(spec);
        for(auto& run:runs){run.stdout_bytes="Fallos="+std::to_string(run.exit_code)+"\n";run.stderr_bytes.clear();}
        require(!accepted(spec,runs),"A generic summary replaced the individual diagnoses");
        runs=observations(spec);
        auto& text=runs[1].stderr_bytes;
        text.erase(text.find('\n')+1);
        require(!accepted(spec,runs),"Only the first reported failure replaced a complete battery");
        runs=observations(spec);
        runs.front().stdout_bytes+=runs.front().stdout_bytes;
        require(!accepted(spec,runs),"Ambiguous duplicate case records were accepted");
        runs=observations(spec);
        runs[1].stderr_bytes=report(spec,runs[1],Format::groups);
        require(!accepted(spec,runs),"A summary changed the control's record segmentation");
    }
    std::cout<<"PASS integer battery causal record attribution, verdict changes and reporting counterexamples\n";
}
void provenance_and_execution() {
    const auto boxes_spec=contract(true),batches_spec=contract(false);
    auto runs=observations(boxes_spec);
    for(auto& run:runs)if(run.exit_code)run.exit_code=257;
    require(accepted(boxes_spec,runs),"A normal nonzero failure allowed by envios was rejected");
    runs=observations(batches_spec);runs[1].exit_code=257;
    require(!accepted(batches_spec,runs),"lotes accepted a failure code other than one");
    runs=observations(boxes_spec);runs[1].exit_code=0xffffffffu;
    require(accepted(boxes_spec,runs),"The complete normal DWORD was truncated");
    for(unsigned fault=0;fault<10;++fault) {
        runs=observations(boxes_spec);
        if(fault==0)runs[0].calls.clear(); // copied component, no shared-library calls
        if(fault==1)runs[1].calls.pop_back();
        if(fault==2)runs[1].calls.push_back(runs[1].calls.front());
        if(fault==3)runs[1].completed_normally=false;
        if(fault==4)runs[1].exit_code=0;
        if(fault==5)runs[0].exit_code=1;
        if(fault==6)runs[1].program_frame="another-project";
        if(fault==7)runs[1].phase.target=1;
        if(fault==8)runs[1].compiled=false;
        if(fault==9)runs.pop_back();
        require(!accepted(boxes_spec,runs),"Incomplete, copied, crashed or unrelated evidence gained credit");
    }
    runs=observations(boxes_spec);
    runs.back().calls[0].result=100;
    require(!accepted(boxes_spec,runs),"A published result unrelated to its visible case report passed");
    auto expected_success=boxes_spec;expected_success.published_result=IntegerBatteryPublishedResult::success;
    require(!accepted(expected_success,observations(boxes_spec)),"Published failures borrowed a success declaration");
    for(const auto value:std::array<std::int32_t,2>{
            std::numeric_limits<std::int32_t>::min(),std::numeric_limits<std::int32_t>::max()}) {
        auto boundary=batches_spec;boundary.cases={{4,value}};
        const auto plan=integer_battery_plan(boundary);
        require(plan&&plan->size()==3&&(*plan)[1].target==0,"Boundary mutation plan is incomplete");
        require(accepted(boundary,observations(boundary)),"A boundary integer mutation overflowed");
    }
    std::cout<<"PASS integer battery complete calls, authentic phases, normality and distinct exit policies\n";
}
void traces_and_instrumentation() {
    const auto spec=contract(true);
    const auto trace=integer_battery_parse_trace(
        "prefijoLGAED_INTEGER_BATTERY_CALL:0,4,1\r\n"
        "LGAED_INTEGER_BATTERY_CALL:1,8,2\n"
        "diagnostico\nLGAED_INTEGER_BATTERY_END\nfinal");
    require(trace&&trace->completed_normally&&trace->calls==
        std::vector<IntegerBatteryObservation>{{4,1},{8,2}}&&trace->visible=="prefijodiagnostico\nfinal",
        "Client call order, CRLF or visible stderr were corrupted");
    const auto abrupt=integer_battery_parse_trace("LGAED_INTEGER_BATTERY_CALL:0,4,1\n");
    require(abrupt&&!abrupt->completed_normally,"Absent normal completion was fabricated");
    for(const auto invalid:std::array<std::string_view,7>{
            "LGAED_INTEGER_BATTERY_CALL:1,4,1\n",
            "LGAED_INTEGER_BATTERY_CALL:0,4,1,9\n",
            "LGAED_INTEGER_BATTERY_CALL:0,2147483648,1\n",
            "LGAED_INTEGER_BATTERY_CALL:0,4,1",
            "LGAED_INTEGER_BATTERY_END\nLGAED_INTEGER_BATTERY_END\n",
            "LGAED_INTEGER_BATTERY_END\nLGAED_INTEGER_BATTERY_CALL:0,4,1\n",
            "LGAED_INTEGER_BATTERY_UNKNOWN\n"})
        require(!integer_battery_parse_trace(invalid),"Malformed private trace was accepted");
    const auto plan=*integer_battery_plan(spec);
    require(plan.size()==9&&plan.front().mutation==IntegerBatteryMutation::control&&
        plan.back().mutation==IntegerBatteryMutation::published,"Incorrect independent seven-case plan");
    for(const auto& phase:plan) {
        const auto source=integer_battery_instrument(boxes,spec,phase);
        require(source&&source->find("return piezas / 4;")!=source->npos&&
            source->find("#line 1\n")!=source->npos&&source->find("std::fwrite")!=source->npos,
            "The instrumented library lost its body, source identity or format-independent trace");
    }
    require(!integer_battery_instrument(boxes,spec,{IntegerBatteryMutation::mutate,99}),
        "An unauthored mutation target was generated");
    require(integer_battery_instrument(boxes,spec,{IntegerBatteryMutation::published,{}}).has_value(),
        "Published instrumentation needed to observe the prerequisite client was rejected");
    require(integer_battery_instrument_driver("int main(){return 0;}",spec).has_value(),
        "The empty checker could not receive an independent normal-completion witness");
    require(integer_battery_driver_has_main("int main(int argc,char** argv){return argc+(argv==nullptr);}"),
        "An ordinary argc/argv main was not identified");
    require(integer_battery_driver_has_main("auto main()->int {return 0;}"),
        "A trailing-return main was not identified");
    require(!integer_battery_instrument_driver("int helper(){return 0;}",spec),
        "A helper translation unit received an extra completion witness");
    std::cout<<"PASS integer battery robust traces, original library instrumentation and client observations\n";
}
}
int main()try {
    source_and_authoring();diagnostics();provenance_and_execution();traces_and_instrumentation();
    return 0;
}catch(const std::exception& error) {
    std::cerr<<"FAIL integer battery: "<<error.what()<<'\n';return 1;
}

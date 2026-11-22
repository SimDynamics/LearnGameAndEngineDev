#include "core/cpp_input_probe.hpp"

#include <iostream>
#include <stdexcept>

using namespace lgaed::curriculum;

namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
CppInputCheck check() {
    return {{{"Leo 2",{"Leo: 5","Leo: 5\n"}},
             {"Rita 2",{"Rita: 5","Rita: 5\n"}},
             {"Leo 4",{"Leo: 7","Leo: 7\n"}}}};
}
}

int main() {
    try {
        const auto original=check();
        require(cpp_input_check_valid(original),"Independent input alternatives rejected");
        require(cpp_input_expected_output("Leo: 5",original.probes[0]),"Exact output rejected");
        require(cpp_input_expected_output("Leo: 5\r\n",original.probes[0]),"CRLF not normalized");
        require(!cpp_input_expected_output("Leo: 5\n\n",original.probes[0]),"Extra newline accepted");
        require(!cpp_input_expected_output(" Leo: 5",original.probes[0]),"Unexpected whitespace accepted");
        require(!cpp_input_expected_output("Rita: 7",original.probes[0]),"Fixed public output accepted");
        require(!cpp_input_expected_output("Leo: 5\r",original.probes[0]),"Bare CR erased");
        auto changed=original;
        changed.probes[0].standard_input="Ana 2";
        require(changed!=original,"Changed input omitted from equality");
        changed=original;changed.probes[0].outputs[0]="Leo: 6";
        require(changed!=original,"Changed expectation omitted from equality");
        require(!cpp_input_check_valid({}),"Empty checks accepted");
        changed=original;changed.probes.resize(9);
        require(!cpp_input_check_valid(changed),"Unbounded probe count accepted");
        changed=original;changed.probes.push_back(original.probes[0]);
        require(!cpp_input_check_valid(changed),"Repeated input accepted");
        changed=original;changed.probes[0].outputs.clear();
        require(!cpp_input_check_valid(changed),"Missing outputs accepted");
        changed=original;changed.probes[0].outputs={"0","1","2","3","4"};
        require(!cpp_input_check_valid(changed),"Unbounded output alternatives accepted");
        changed=original;changed.probes[0].outputs={"x\n","x\r\n"};
        require(!cpp_input_check_valid(changed),"Duplicate canonical output accepted");
        changed=original;changed.probes[0].standard_input=std::string(4096,'x');
        require(cpp_input_check_valid(changed),"Input boundary rejected");
        changed.probes[0].standard_input+='x';
        require(!cpp_input_check_valid(changed),"Oversized input accepted");
        changed=original;changed.probes[0].outputs={std::string(4096,'x')};
        require(cpp_input_check_valid(changed),"Output boundary rejected");
        changed.probes[0].outputs[0]+='x';
        require(!cpp_input_check_valid(changed),"Oversized output accepted");
        changed=original;changed.probes[0].standard_input.push_back('\0');
        require(!cpp_input_check_valid(changed),"NUL input accepted");
        changed=original;changed.probes[0].outputs[0].push_back('\0');
        require(!cpp_input_check_valid(changed),"NUL output accepted");
        require(cpp_input_check_valid({{{"",{""}}}}),"Empty input or output is not a missing value");
        require(cpp_input_case_key(" \tLeo\r\n2\v\f",true)==
                cpp_input_case_key("Leo 2",true),"ASCII token separators differ");
        require(cpp_input_case_key("ab c",true)!=cpp_input_case_key("a bc",true),
                "Token boundaries are ambiguous");
        require(cpp_input_case_key("Leo\xC2\xA0" "2",true)!=
                cpp_input_case_key("Leo 2",true),"Unicode data treated as ASCII whitespace");
        require(cpp_input_case_key("Leo\r\n2",false)!=
                cpp_input_case_key("Leo\n2",false),"Exact input unexpectedly normalizes newlines");
        require(cpp_input_case_key(" \r\n\t",true).empty(),"Empty tokens are not empty input");
        std::cout<<"PASS input probes: exact outputs, ASCII input keys, bounds and independent alternatives.\n";
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}

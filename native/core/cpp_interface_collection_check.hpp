#pragma once

#include "core/cpp_multi_body_check.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// General, author-declared two-body contract: move a unique owner into a
// collection, then report each live object through its public interface.
// All provided types, their methods, the factory, vector alias and main
// remain byte-for-byte identical to the published source.
struct CppInterfaceCollectionPolicy {
    std::string original_source;
    std::string base_class;
    std::string simple_class;
    std::string identified_class;
    std::string vector_alias;
    std::string add_function;
    std::string report_function;
    std::string write_method;
    std::string number_method;
    std::string destination_parameter;
    std::string source_parameter;
    std::string data_parameter;
    std::string stream_parameter;
    std::string count_label;
    std::string index_separator;
    std::string number_label;
    std::string code_label;
    bool operator==(const CppInterfaceCollectionPolicy&) const = default;
};

inline constexpr std::string_view cpp_interface_collection_completion_marker =
    "LGAED_INTERFACE_COLLECTION_COMPLETE_V1";

namespace cpp_interface_collection_detail {

inline bool identifier(std::string_view value) {
    if(value.empty() || value.size()>96 || value.starts_with("__") ||
       (value.size()>1 && value[0]=='_' && value[1]>='A' && value[1]<='Z'))
        return false;
    const auto alpha=[](unsigned char c) {
        return c=='_' || (c>='a'&&c<='z') || (c>='A'&&c<='Z');
    };
    if(!alpha(static_cast<unsigned char>(value[0])))return false;
    return std::all_of(value.begin()+1,value.end(),[&](unsigned char c) {
        return alpha(c)||(c>='0'&&c<='9');
    });
}

inline bool literal(std::string_view value) {
    return !value.empty() && value.size()<=64 &&
        std::all_of(value.begin(),value.end(),[](unsigned char c) {
            // The label is inserted into a generated C++ string literal.
            return c>=32 && c<=126 && c!='"' && c!='\\';
        });
}

inline std::vector<CppEditableBody> bodies(
        const CppInterfaceCollectionPolicy& policy) {
    return {
        {"bool "+policy.add_function+"("+policy.vector_alias+"& "+
             policy.destination_parameter+", std::unique_ptr<"+
             policy.base_class+">& "+policy.source_parameter+")",false},
        {"void "+policy.report_function+"(const "+policy.vector_alias+"& "+
             policy.data_parameter+", std::ostream& "+
             policy.stream_parameter+")",false}
    };
}

inline bool valid(const CppInterfaceCollectionPolicy& policy) {
    for(std::string_view name:{
            std::string_view(policy.base_class),
            std::string_view(policy.simple_class),
            std::string_view(policy.identified_class),
            std::string_view(policy.vector_alias),
            std::string_view(policy.add_function),
            std::string_view(policy.report_function),
            std::string_view(policy.write_method),
            std::string_view(policy.number_method),
            std::string_view(policy.destination_parameter),
            std::string_view(policy.source_parameter),
            std::string_view(policy.data_parameter),
            std::string_view(policy.stream_parameter)})
        if(!identifier(name))return false;
    if(!literal(policy.count_label)||!literal(policy.index_separator)||
       !literal(policy.number_label)||!literal(policy.code_label))
        return false;
    const auto& source=policy.original_source;
    if(!cpp_multi_body_detail::valid_source(source) ||
       !cpp_multi_body_detail::spans(source,bodies(policy),false))
        return false;
    // Prove that an actual polymorphic vector of unique owners is present.
    // The immutable source gate then prevents the candidate from changing it.
    return source.find("class "+policy.base_class+" {")!=source.npos &&
        source.find("class "+policy.simple_class+" : public "+
            policy.base_class+" {")!=source.npos &&
        source.find("class "+policy.identified_class+" : public "+
            policy.base_class+" {")!=source.npos &&
        source.find("virtual ~"+policy.base_class+"() = default;")!=
            source.npos &&
        source.find("virtual int "+policy.number_method+
            "() const noexcept = 0;")!=source.npos &&
        source.find("virtual void "+policy.write_method+
            "(std::ostream& "+policy.stream_parameter+") const = 0;")!=
            source.npos &&
        source.find("using "+policy.vector_alias+" = std::vector<"
            "std::unique_ptr<"+policy.base_class+">>;")!=source.npos;
}

inline void substitute(std::string& source,std::string_view name,
                       std::string_view value) {
    for(std::size_t at=0;(at=source.find(name,at))!=source.npos;) {
        source.replace(at,name.size(),value);
        at+=value.size();
    }
}

} // namespace cpp_interface_collection_detail

inline bool cpp_interface_collection_matches(
        const CppInterfaceCollectionPolicy& policy,
        std::string_view candidate) {
    return cpp_interface_collection_detail::valid(policy) &&
        cpp_multi_body_matches({policy.original_source,
                                cpp_interface_collection_detail::bodies(policy)},
                               candidate);
}

// The probe is a *different* program from the visible stdout oracle.
// It tests real ownership identity across vector growth, rejection of an
// empty source, stable order, virtual dispatch for a third implementation,
// independent streams, repeated reports and RAII cleanup. No pointer is
// examined after its object has been destroyed.
inline std::optional<std::string> cpp_interface_collection_private_driver(
        const CppInterfaceCollectionPolicy& policy,
        std::string_view candidate) {
    if(!cpp_interface_collection_matches(policy,candidate))return {};
    constexpr std::string_view prelude=R"lgaed(#include <array>
#include <climits>
#include <cstdio>
#include <sstream>
#include <string>
#define main lgaed_published_original_main
)lgaed";
    constexpr std::string_view driver=R"lgaed(
#undef main
class LgaedOtherSample final : public @BASE@ {
public:
    LgaedOtherSample(int number,int extra,int& destroyed,int& written)
      : number_{number},extra_{extra},destroyed_{destroyed},written_{written} {}
    ~LgaedOtherSample() override { ++destroyed_; }
    int @NUMBER@() const noexcept override { return number_; }
    void @WRITE@(std::ostream& output) const override {
        ++written_;
        output << "Adicional:" << extra_;
    }
private:
    int number_,extra_;
    int& destroyed_;
    int& written_;
};
int main() {
    @VECTOR@ collection;
    std::unique_ptr<@BASE@> empty;
    if(@ADD@(collection,empty)||empty||!collection.empty())return 1;
    std::string lines;
    std::vector<const @BASE@*> identities;
    for(int index=0;index<28;++index) {
        const int kind=index%2;
        const int number=index==0?INT_MIN:index==1?INT_MAX:
                         index%4==0?0:index%4==1?-8:12;
        const int code=index%5==0?0:index%5==1?INT_MIN:
                       index%5==2?INT_MAX:index%5==3?-9:11;
        std::unique_ptr<@BASE@> source;
        if(kind==0) source=std::make_unique<@SIMPLE@>(number);
        else source=std::make_unique<@IDENTIFIED@>(number,code);
        const auto* address=source.get();
        const auto previous=collection.size();
        if(!@ADD@(collection,source)||source||
           collection.size()!=previous+1||
           collection.back().get()!=address)return 2;
        identities.push_back(address);
        for(std::size_t i=0;i<previous;++i)
            if(collection[i].get()!=identities[i])return 3;
        lines+=std::to_string(index)+"@SEPARATOR@@NUMBER_LABEL@"+
               std::to_string(number);
        if(kind)lines+="@CODE_LABEL@"+std::to_string(code);
        lines+="\n";
    }
    {
        // Refusing an empty owner must not destroy, insert or reorder others.
        std::vector<const @BASE@*> before=identities;
        if(@ADD@(collection,empty)||empty||
           collection.size()!=before.size())return 4;
        for(std::size_t i=0;i<before.size();++i)
            if(collection[i].get()!=before[i])return 5;
    }
    const auto expected=std::string("@COUNT_LABEL@")+
                        std::to_string(collection.size())+"\n"+lines;
    std::ostringstream primary;
    @REPORT@(collection,primary);
    if(primary.str()!=expected)return 6;
    std::ostringstream secondary;
    @REPORT@(collection,secondary);
    if(secondary.str()!=expected)return 7;
    for(std::size_t i=0;i<identities.size();++i)
        if(collection[i].get()!=identities[i])return 8;

    int destroyed=0,written=0;
    {
        auto other=std::make_unique<LgaedOtherSample>(
            -17,93,destroyed,written);
        auto* same=other.get();
        std::unique_ptr<@BASE@> third=std::move(other);
        if(!@ADD@(collection,third)||third||
           collection.back().get()!=same)return 9;
        std::ostringstream out;
        @REPORT@(collection,out);
        const auto with_third=std::string("@COUNT_LABEL@")+
            std::to_string(collection.size())+"\n"+lines+
            "28@SEPARATOR@Adicional:93\n";
        if(out.str()!=with_third || written!=1 || destroyed)return 10;
        std::ostringstream again;
        @REPORT@(collection,again);
        if(again.str()!=with_third || written!=2 || destroyed)return 11;
        std::unique_ptr<@BASE@> rejected;
        const auto count=collection.size();
        if(@ADD@(collection,rejected)||rejected||
           collection.size()!=count||written!=2||destroyed)return 12;
        for(std::size_t i=0;i<identities.size();++i)
            if(collection[i].get()!=identities[i])return 13;
    }
    if(destroyed)return 14;
    collection.clear();
    if(!collection.empty()||destroyed!=1)return 15;
    std::ostringstream after_clear;
    @REPORT@(collection,after_clear);
    if(after_clear.str()!=std::string("@COUNT_LABEL@")+"0\n")return 16;
    std::fputs("LGAED_INTERFACE_COLLECTION_COMPLETE_V1",stderr);
    return 0;
}
)lgaed";
    std::string witness(driver);
    for(const auto& [name,value]:
            std::array<std::pair<std::string_view,std::string_view>,12>{{
        {"@BASE@",policy.base_class},
        {"@SIMPLE@",policy.simple_class},
        {"@IDENTIFIED@",policy.identified_class},
        {"@NUMBER@",policy.number_method},
        {"@VECTOR@",policy.vector_alias},
        {"@ADD@",policy.add_function},
        {"@REPORT@",policy.report_function},
        {"@WRITE@",policy.write_method},
        {"@COUNT_LABEL@",policy.count_label},
        {"@SEPARATOR@",policy.index_separator},
        {"@NUMBER_LABEL@",policy.number_label},
        {"@CODE_LABEL@",policy.code_label}
    }})cpp_interface_collection_detail::substitute(witness,name,value);
    return std::string(prelude)+std::string(candidate)+witness;
}

} // namespace lgaed::curriculum

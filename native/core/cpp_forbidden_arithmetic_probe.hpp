#pragma once

#include "core/cpp_expression_repair.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {
namespace cpp_forbidden_arithmetic_detail {
using Token=cpp_expression_repair_detail::Token;
inline constexpr std::string_view divide_name="lgaed_forbidden_division_2f8ad109";
inline constexpr std::string_view remainder_name="lgaed_forbidden_remainder_2f8ad109";

// This opt-in observer handles complete elementary statements, not arbitrary
// C++. A partly recognized expression must never escape as an executable
// counterfactual. In particular, (a)/b is not a valid suffix of f(a)/b.
class Instrumenter {
    struct Expression {
        std::size_t begin=0,end=0;
        std::string text;
        bool stream=false;
    };
    struct Replacement {std::size_t begin,end;std::string text;};
    struct Local {bool initialized=false;};
    std::string_view source_;
    std::vector<Token> code_;
    std::size_t at_=0,body_begin_=0;
    std::vector<std::map<std::string,Local>> scopes_;
    std::vector<Replacement> replacements_;
    std::set<std::size_t> covered_;
    bool invalid_=false;

    bool is(std::string_view value)const{return at_<code_.size()&&code_[at_].value==value;}
    bool take(std::string_view value){if(!is(value))return false;++at_;return true;}
    bool expect(std::string_view value){if(take(value))return true;invalid_=true;return false;}
    static bool name(std::string_view token){
        if(!token.starts_with("id:"))return false;
        const auto value=token.substr(3);
        if(value=="true"||value=="false"||value=="nullptr"||
           !cpp_expression_repair_detail::identifier(value))return false;
        if(!code_format::identifier_start(static_cast<unsigned char>(value.front())))return false;
        return std::ranges::all_of(value.substr(1),[](unsigned char c){return code_format::identifier_part(c);});
    }
    Local* local(std::string_view value){
        for(auto i=scopes_.rbegin();i!=scopes_.rend();++i)
            if(const auto found=i->find(std::string(value));found!=i->end())return &found->second;
        return nullptr;
    }
    static bool scalar_type(std::string_view value){
        return value=="id:int"||value=="id:signed"||value=="id:unsigned"||
            value=="id:short"||value=="id:long"||value=="id:char"||
            value=="id:char8_t"||value=="id:char16_t"||value=="id:char32_t"||
            value=="id:wchar_t"||value=="id:bool"||value=="id:float"||
            value=="id:double"||value=="id:auto";
    }
    static bool cast_type(std::string_view value){
        return scalar_type(value)&&value!="id:auto";
    }
    bool starts_type()const{return at_<code_.size()&&(scalar_type(code_[at_].value)||
        is("id:const")||is("id:constexpr"));}
    static bool standard_output(std::string_view value){
        return value=="cout"||value=="cerr"||value=="clog";
    }
    static bool standard_manipulator(std::string_view value){
        return value=="endl"||value=="flush"||value=="boolalpha"||value=="noboolalpha";
    }
    static bool standard_include(std::string_view value){
        if(!cpp_expression_repair_detail::include_directive(value))return false;
        value.remove_prefix(10);
        value.remove_prefix(value.starts_with("%:")?2:1);
        while(!value.empty()&&code_format::space(value.front()))value.remove_prefix(1);
        value.remove_prefix(7);
        while(!value.empty()&&code_format::space(value.front()))value.remove_prefix(1);
        if(value.empty()||value.front()!='<')return false;
        const auto closing=value.find('>');
        if(closing==value.npos)return false;
        const auto header=value.substr(1,closing-1);
        // A private/local header could define macros, user types or executable
        // initializers not present in the syntax tree. Admit only the standard
        // elementary headers required by this deliberately bounded grammar.
        constexpr std::string_view allowed[]{"iostream","ostream","ios","iosfwd",
            "iomanip","limits","climits","cstdint","cstdlib","type_traits"};
        return std::ranges::find(allowed,header)!=std::end(allowed);
    }
    Expression atom(std::size_t depth){
        if(depth>128||at_>=code_.size()){invalid_=true;return {};}
        const auto first=at_;
        if(take("id:static_cast")){
            if(!expect("op:<"))return {};
            bool type=false;
            while(at_<code_.size()&&(cast_type(code_[at_].value)||is("id:const")||is("id:volatile"))){
                type=type||cast_type(code_[at_].value);++at_;
            }
            if(!type||!expect("op:>")||!expect("op:(")){invalid_=true;return {};}
            auto value=conditional(depth+1);
            if(invalid_||value.stream||!expect("op:)")){invalid_=true;return {};}
            const auto begin=code_[first].begin,end=code_[at_-1].end;
            value.text=std::string(source_.substr(begin,value.begin-begin))+value.text+
                std::string(source_.substr(value.end,end-value.end));
            value.begin=begin;value.end=end;return value;
        }
        if(cast_type(code_[at_].value)&&at_+1<code_.size()&&code_[at_+1].value=="op:("){
            // A known primitive type performs a conversion, not an opaque
            // function call. Require exactly one fully parsed operand.
            at_+=2;auto value=conditional(depth+1);
            if(invalid_||value.stream||!expect("op:)")){invalid_=true;return {};}
            const auto begin=code_[first].begin,end=code_[at_-1].end;
            value.text=std::string(source_.substr(begin,value.begin-begin))+value.text+
                std::string(source_.substr(value.end,end-value.end));
            value.begin=begin;value.end=end;return value;
        }
        if(take("op:(")){
            auto value=conditional(depth+1);
            if(!expect("op:)"))return {};
            const auto begin=code_[first].begin,end=code_[at_-1].end;
            value.text=std::string(source_.substr(begin,value.begin-begin))+value.text+
                std::string(source_.substr(value.end,end-value.end));
            value.begin=begin;value.end=end;return value;
        }
        if(is("op:+")||is("op:-")||is("op:!")||is("op:~")||is("id:not")||is("id:compl")){
            ++at_;auto value=atom(depth+1);
            if(invalid_||value.stream){invalid_=true;return {};}
            const auto begin=code_[first].begin;
            value.text=std::string(source_.substr(begin,value.begin-begin))+value.text;
            value.begin=begin;return value;
        }
        const auto& token=code_[at_];bool stream=false;
        if(token.value.starts_with("number:")){
            if(!cpp_expression_repair_detail::number(std::string_view(token.value).substr(7)))invalid_=true;
            ++at_;
        }else if(token.value.starts_with("literal:")){
            // The lexer keeps an underscore UDL suffix inside the literal
            // token. Ordinary quoted/raw literals end at their closing quote;
            // a suffix could invoke a user function and is never admitted.
            if(!token.value.ends_with('"')&&!token.value.ends_with('\''))invalid_=true;
            ++at_;
        }else if(take("id:true")||take("id:false")){
        }else if(take("id:std")){
            if(!expect("op:::")||at_>=code_.size()||!code_[at_].value.starts_with("id:"))return {};
            const auto value=std::string_view(code_[at_].value).substr(3);
            stream=standard_output(value);
            if(!stream&&!standard_manipulator(value))invalid_=true;
            ++at_;
        }else if(name(token.value)){
            const auto value=std::string_view(token.value).substr(3);
            if(auto found=local(value)){
                if(!found->initialized)invalid_=true;
            }else{
                stream=standard_output(value);
                if(!stream&&!standard_manipulator(value))invalid_=true;
            }
            ++at_;
        }else{invalid_=true;return {};}
        if(invalid_)return {};
        const auto begin=code_[first].begin,end=code_[at_-1].end;
        // Reject postfix syntax at the full atom boundary. Otherwise a call,
        // index or member could hide an operation outside our expression tree.
        if(is("op:(")||is("op:[")||is("op:.")||is("op:->")||is("op:::")||
           is("op:++")||is("op:--")){invalid_=true;return {};}
        return {begin,end,std::string(source_.substr(begin,end-begin)),stream};
    }
    Expression binary(int minimum,std::size_t depth){
        auto left=atom(depth);
        while(!invalid_&&at_<code_.size()){
            const auto level=cpp_expression_repair_detail::precedence(code_[at_].value);
            if(level<minimum)break;
            const auto operation=at_++;const auto op=code_[operation].value;
            auto right=binary(level+1,depth+1);
            if(invalid_)return {};
            if(op=="op:/"||op=="op:%"){
                if(left.stream||right.stream){invalid_=true;return {};}
                const auto helper=op=="op:/"?divide_name:remainder_name;
                left.text=std::string(helper)+"(("+left.text+"),("+right.text+"))";
                covered_.insert(operation);
            }else{
                if(right.stream||(left.stream&&op!="op:<<")){invalid_=true;return {};}
                left.text+=source_.substr(left.end,right.begin-left.end);
                left.text+=right.text;
            }
            left.end=right.end;
        }
        return left;
    }
    Expression conditional(std::size_t depth=0){
        if(depth>128){invalid_=true;return {};}
        auto condition=binary(1,depth);
        if(invalid_||!take("op:?"))return condition;
        if(condition.stream){invalid_=true;return {};}
        auto yes=conditional(depth+1);
        if(invalid_||!expect("op::"))return {};
        auto no=conditional(depth+1);
        if(invalid_||yes.stream||no.stream){invalid_=true;return {};}
        condition.text+=source_.substr(condition.end,yes.begin-condition.end);
        condition.text+=yes.text;
        condition.text+=source_.substr(yes.end,no.begin-yes.end);
        condition.text+=no.text;condition.end=no.end;return condition;
    }
    bool expression(bool allow_stream=false){
        const auto value=conditional();
        if(invalid_||(!allow_stream&&value.stream))return false;
        replacements_.push_back({value.begin,value.end,value.text});return true;
    }
    bool using_standard(){
        if(!expect("id:using"))return false;
        if(take("id:namespace"))return expect("id:std")&&expect("op:;");
        if(!expect("id:std")||!expect("op:::")||at_>=code_.size())return false;
        const auto& token=code_[at_++].value;
        if(!token.starts_with("id:")||(!standard_output(std::string_view(token).substr(3))&&
            !standard_manipulator(std::string_view(token).substr(3))))return false;
        return expect("op:;");
    }
    bool declaration(){
        bool type=false;
        while(at_<code_.size()&&(scalar_type(code_[at_].value)||is("id:const")||is("id:constexpr"))){
            type=type||scalar_type(code_[at_].value);++at_;
        }
        if(!type)return false;
        for(;;){
            if(at_>=code_.size()||!name(code_[at_].value))return false;
            const auto variable=code_[at_++].value.substr(3);
            if(scopes_.back().contains(variable))return false;
            // A newly declared name is already in scope in its initializer.
            // Reading it there must not accidentally resolve an outer local.
            scopes_.back().emplace(variable,Local{});
            bool initialized=false;
            if(take("op:=")){
                if(take("op:{")){
                    if(!is("op:}")&&!expression())return false;
                    if(!expect("op:}"))return false;
                }else if(!expression())return false;
                initialized=true;
            }else if(take("op:{")){
                if(!is("op:}")&&!expression())return false;
                if(!expect("op:}"))return false;
                initialized=true;
            }else if(take("op:(")){
                // An empty () is a function declaration, not an initialized
                // scalar. A call must never acquire a fictitious safe body.
                if(is("op:)")||!expression()||!expect("op:)"))return false;
                initialized=true;
            }
            scopes_.back().at(variable).initialized=initialized;
            if(take("op:;"))return true;
            if(!expect("op:,"))return false;
        }
    }
    bool statement(std::size_t depth){
        if(depth>128||invalid_||at_>=code_.size())return false;
        if(take("op:;"))return true;
        if(take("op:{")){
            scopes_.emplace_back();
            while(!is("op:}"))if(!statement(depth+1))return false;
            ++at_;scopes_.pop_back();return true;
        }
        if(take("id:if")){
            if(!expect("op:(")||!expression()||!expect("op:)"))return false;
            const auto before=scopes_;
            // A controlled substatement has its own scope even when its
            // braces are omitted. Keep its declarations out of both joins.
            scopes_.emplace_back();
            if(!statement(depth+1))return false;
            scopes_.pop_back();
            const auto yes=scopes_;scopes_=before;
            if(take("id:else")){
                scopes_.emplace_back();
                if(!statement(depth+1))return false;
                scopes_.pop_back();
                for(std::size_t i=0;i<before.size();++i)for(auto& [name,value]:scopes_[i])
                    value.initialized=value.initialized&&yes[i].at(name).initialized;
            }else scopes_=before;
            return true;
        }
        if(take("id:return"))return !is("op:;")&&expression()&&expect("op:;");
        if(is("id:using"))return using_standard();
        if(starts_type())return declaration();
        if(name(code_[at_].value)&&at_+1<code_.size()&&code_[at_+1].value=="op:="){
            auto* destination=local(std::string_view(code_[at_].value).substr(3));
            if(!destination)return false;
            at_+=2;if(!expression()||!expect("op:;"))return false;
            destination->initialized=true;return true;
        }
        return expression(true)&&expect("op:;");
    }
    bool main_signature(){
        const bool trailing=take("id:auto");
        if(!trailing&&!expect("id:int"))return false;
        if(!expect("id:main")||!expect("op:("))return false;
        (void)take("id:void");
        if(!expect("op:)"))return false;
        if(take("id:noexcept")&&take("op:(")){
            // The exception specification is unevaluated. Admit only the
            // ordinary literal forms, never an unparsed expression or call.
            if(!take("id:true")&&!take("id:false"))return false;
            if(!expect("op:)"))return false;
        }
        if(trailing&&(!expect("op:->")||!expect("id:int")))return false;
        if(!expect("op:{"))return false;
        body_begin_=code_[at_-1].end;return true;
    }
public:
    Instrumenter(std::string_view source,std::vector<Token> tokens):source_(source),code_(std::move(tokens)){}
    std::optional<std::string> run(){
        // Local preprocessing and opaque translation-unit code could hide an
        // operator before main. This observer admits includes, std output
        // using declarations and one ordinary main, with no helper functions.
        std::size_t operators=0;
        for(const auto& token:code_){
            if(token.value=="op:/="||token.value=="op:%=")return {};
            if(token.value=="op:/"||token.value=="op:%")++operators;
            if(token.value=="id:"+std::string(divide_name)||token.value=="id:"+std::string(remainder_name))return {};
        }
        if(operators>128)return {};
        while(at_<code_.size()){
            if(code_[at_].value.starts_with("directive:")){
                if(!standard_include(code_[at_].value))return {};
                ++at_;
            }else if(is("id:using")){
                if(!using_standard())return {};
            }else break;
        }
        if(!main_signature())return {};
        scopes_.emplace_back();
        while(!is("op:}"))if(!statement(0))return {};
        ++at_;while(take("op:;")){}
        if(invalid_||at_!=code_.size()||covered_.size()!=operators)return {};
        std::ranges::sort(replacements_,{},&Replacement::begin);
        std::size_t previous=body_begin_;
        for(const auto& replacement:replacements_){
            if(replacement.begin<previous||replacement.end<replacement.begin)return {};
            previous=replacement.end;
        }
        std::string result(source_);
        for(auto i=replacements_.rbegin();i!=replacements_.rend();++i)
            result.replace(i->begin,i->end-i->begin,i->text);
        if(operators){
            const auto helper=[](std::string_view name,char operation){
                return "\n    const auto "+std::string(name)+
                    " = [](auto lhs, auto rhs) -> decltype(lhs "+std::string(1,operation)+" rhs) {\n"
                    "        static_assert(::std::is_arithmetic_v<decltype(lhs)> && ::std::is_arithmetic_v<decltype(rhs)>);\n"
                    "        ::std::_Exit(91);\n    };\n";
            };
            result.insert(body_begin_,helper(divide_name,'/')+helper(remainder_name,'%'));
            result.insert(0,"#include <cstdlib>\n#include <type_traits>\n");
        }
        return result;
    }
};
} // namespace cpp_forbidden_arithmetic_detail

// Preserve C++ short-circuit and conditional semantics by replacing only the
// reached primitive division/remainder nodes. Their helpers have the original
// result type in unevaluated decltype, but terminate before either operation.
// Thus zero, negative divisors and MIN/-1 never cause actual private arithmetic.
[[nodiscard]] inline std::optional<std::string> cpp_forbid_arithmetic(std::string_view source){
    auto tokens=cpp_expression_repair_detail::tokens(source);
    if(!tokens||!cpp_expression_repair_detail::source_tokens_valid(*tokens))return {};
    std::erase_if(*tokens,[](const auto& token){return token.value.starts_with("comment:");});
    return cpp_forbidden_arithmetic_detail::Instrumenter(source,std::move(*tokens)).run();
}
} // namespace lgaed::curriculum

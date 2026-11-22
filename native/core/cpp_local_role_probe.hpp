#pragma once

#include "core/cpp_local_int_probe.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lgaed::curriculum {

// Role names belong to the authored contract, never to the student's source.
// A role keeps one candidate for all its independent observations. Different
// roles may share a candidate, except the two objects in beforeUpdateOf.
enum class CppLocalRoleValueType { integer, real };
struct CppLocalRoleProbe {
    double replacement = 0;
    std::vector<std::string> expected_stdout;
    bool after_update = false;
    std::optional<std::int32_t> expected_increase;
    std::string before_update_of;
    bool operator==(const CppLocalRoleProbe&) const = default;
};
struct CppLocalRole {
    std::string id;
    CppLocalRoleValueType value_type = CppLocalRoleValueType::integer;
    double initial_value = 0;
    std::vector<CppLocalRoleProbe> probes;
    bool operator==(const CppLocalRole&) const = default;
};
struct CppLocalRoleCheck {
    std::vector<CppLocalRole> roles;
    bool operator==(const CppLocalRoleCheck&) const = default;
};

inline bool cpp_local_role_id_valid(std::string_view id) {
    if(id.empty()||id.size()>64)return false;
    const auto start=[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
    };
    if(!start(static_cast<unsigned char>(id.front())))return false;
    return std::ranges::all_of(id,[&](unsigned char c) {
        return start(c)||(c>='0'&&c<='9');
    });
}
inline bool cpp_local_role_value_valid(double value,CppLocalRoleValueType type) {
    if(!std::isfinite(value))return false;
    if(type==CppLocalRoleValueType::integer)
        return value>=-2147483648.0&&value<=2147483647.0&&std::trunc(value)==value;
    return type==CppLocalRoleValueType::real&&std::abs(value)<=1000000.0;
}
inline bool cpp_local_role_check_valid(const CppLocalRoleCheck& check) {
    if(check.roles.empty()||check.roles.size()>4)return false;
    std::set<std::string> names;
    std::size_t total=0;
    for(const auto& role:check.roles) {
        if(!cpp_local_role_id_valid(role.id)||!names.insert(role.id).second||
           !cpp_local_role_value_valid(role.initial_value,role.value_type)||
           role.probes.empty())return false;
        total+=role.probes.size();
        if(total>8)return false;
        for(std::size_t index=0;index<role.probes.size();++index) {
            const auto& probe=role.probes[index];
            if(!cpp_local_role_value_valid(probe.replacement,role.value_type)||
               probe.expected_stdout.empty()||probe.expected_stdout.size()>4||
               (probe.after_update&&!probe.before_update_of.empty()))return false;
            if(probe.after_update) {
                if(role.value_type!=CppLocalRoleValueType::integer||
                   !probe.expected_increase||*probe.expected_increase<=0||
                   probe.replacement==0)return false;
            }else if(probe.expected_increase||probe.replacement==role.initial_value)
                return false;
            if(!probe.before_update_of.empty()) {
                if(role.value_type!=CppLocalRoleValueType::integer||
                   probe.before_update_of==role.id||
                   !cpp_local_role_id_valid(probe.before_update_of))return false;
                const auto other=std::ranges::find(check.roles,probe.before_update_of,
                    &CppLocalRole::id);
                if(other==check.roles.end()||other->value_type!=CppLocalRoleValueType::integer||
                   std::ranges::any_of(other->probes,[](const auto& p) {
                       return !p.before_update_of.empty();
                   })||!std::ranges::any_of(other->probes,[](const auto& p) {
                       return p.after_update&&p.expected_increase.has_value();
                   }))return false;
            }
            std::set<std::string> outputs;
            for(const auto& output:probe.expected_stdout)
                if(output.empty()||output.size()>4096||output.find('\0')!=output.npos||
                   !outputs.insert(cpp_local_int_canonical_stdout(output)).second)return false;
            for(std::size_t prior=0;prior<index;++prior) {
                const auto& old=role.probes[prior];
                if(old.replacement==probe.replacement&&old.after_update==probe.after_update&&
                   old.before_update_of==probe.before_update_of)return false;
            }
        }
    }
    return true;
}
inline bool cpp_local_role_expected_output(const CppLocalRoleProbe& probe,
                                           std::string_view observed) {
    const auto canonical=cpp_local_int_canonical_stdout(observed);
    return std::ranges::any_of(probe.expected_stdout,[&](const auto& expected) {
        return canonical==cpp_local_int_canonical_stdout(expected);
    });
}

namespace cpp_local_role_detail {
inline std::string field(std::string_view text) {
    return std::to_string(text.size())+":"+std::string(text);
}
// An exact binary64 representation makes the frame independent of locale,
// formatting defaults and JSON's spelling of an integral number such as 7.0.
inline std::string value_frame(double value) {
    const auto bits=std::bit_cast<std::uint64_t>(value==0?0.0:value);
    constexpr char hex[]="0123456789abcdef";
    std::string result(16,'0');
    for(std::size_t i=0;i<result.size();++i)
        result[i]=hex[(bits>>((15-i)*4))&15];
    return result;
}
inline std::string literal(double value) {
    // Decimal max_digits10 round-trips the authored binary64 value. A suffix
    // is deliberately absent: the generated literal has C++ type double.
    std::array<char,64> bytes{};
    const auto [end,error]=std::to_chars(bytes.data(),bytes.data()+bytes.size(),
        value==0?0.0:value,std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if(error!=std::errc{})return {};
    std::string text(bytes.data(),end);
    if(text.find_first_of(".eE")==text.npos)text+=".0";
    return text;
}
using Tokens=cpp_local_int_probe_detail::Tokens;
using Token=cpp_local_int_probe_detail::Token;
constexpr std::string_view reserved="lgaed_role_probe_84c7_";

enum class DeclaredType { integer, real, inferred };
struct Declaration {
    std::string name;
    DeclaredType type=DeclaredType::integer;
    std::size_t begin=0,end=0;
    bool initialized=false,constant_expression=false;
};
struct DeclarationGroup {
    std::vector<Declaration> declarations;
    std::vector<std::size_t> commas;
    std::size_t first_name=0,end=0;
};

// This is a bounded declarator locator, not an initializer evaluator. C++
// still evaluates every original initializer and proves the actual type.
inline std::optional<DeclarationGroup> declaration_group(
        const Tokens& code,std::size_t first,std::size_t closing) {
    auto at=first;
    bool constant_expression=false;
    std::optional<DeclaredType> type;
    bool signed_seen=false,int_seen=false;
    for(;at<closing;++at) {
        const auto& token=code[at].value;
        if(token=="id:const")continue;
        if(token=="id:constexpr") { constant_expression=true;continue; }
        if(token=="id:int") {
            if(int_seen||(type&&*type!=DeclaredType::integer))return {};
            int_seen=true;type=DeclaredType::integer;continue;
        }
        if(token=="id:signed") {
            if(signed_seen||(type&&*type!=DeclaredType::integer))return {};
            signed_seen=true;type=DeclaredType::integer;continue;
        }
        if(token=="id:double"||token=="id:auto") {
            if(type)return {};
            type=token=="id:double"?DeclaredType::real:DeclaredType::inferred;
            continue;
        }
        break;
    }
    if(!type||at>=closing)return {};
    DeclarationGroup group;
    group.first_name=at;
    for(;;) {
        if(at+1>=closing||!code[at].value.starts_with("id:"))return {};
        const auto name=std::string_view(code[at].value).substr(3);
        if(!cpp_local_probe_identifier(name))return {};
        const auto& next=code[at+1].value;
        if(next!="op:="&&next!="op:{"&&next!="op:("&&
           next!="op:,"&&next!="op:;")return {};
        std::size_t parens=0,braces=0,brackets=0,end=at+1;
        for(;end<closing;++end) {
            const auto& token=code[end].value;
            if(!parens&&!braces&&!brackets&&(token=="op:,"||token=="op:;"))break;
            if(token=="op:(")++parens;
            else if(token=="op:{")++braces;
            else if(token=="op:[")++brackets;
            else if(token=="op:)") { if(!parens)return {};--parens; }
            else if(token=="op:}") { if(!braces)return {};--braces; }
            else if(token=="op:]") { if(!brackets)return {};--brackets; }
        }
        if(end>=closing||parens||braces||brackets)return {};
        group.declarations.push_back({std::string(name),*type,first,end,
            next!="op:,"&&next!="op:;",constant_expression});
        if(code[end].value=="op:;") { group.end=end;return group; }
        group.commas.push_back(end);
        at=end+1;
    }
}

inline std::vector<DeclarationGroup> declaration_groups(const Tokens& code,
        std::size_t opening,std::size_t closing) {
    std::vector<DeclarationGroup> groups;
    std::size_t braces=1,parens=0,brackets=0;
    for(std::size_t i=opening+1;i<closing;++i) {
        const auto& token=code[i].value;
        if(braces==1&&!parens&&!brackets&&i>0&&
           cpp_local_int_probe_detail::boundary(code[i-1].value)) {
            if(auto group=declaration_group(code,i,closing)) {
                i=group->end;
                groups.push_back(std::move(*group));
                continue;
            }
        }
        if(token=="op:{")++braces;
        else if(token=="op:}") { if(!braces)return {};--braces; }
        else if(token=="op:(")++parens;
        else if(token=="op:)") { if(!parens)return {};--parens; }
        else if(token=="op:[")++brackets;
        else if(token=="op:]") { if(!brackets)return {};--brackets; }
    }
    return groups;
}
} // namespace cpp_local_role_detail

inline std::string cpp_local_role_check_frame(const CppLocalRoleCheck& check) {
    if(!cpp_local_role_check_valid(check))return {};
    using cpp_local_role_detail::field;
    using cpp_local_role_detail::value_frame;
    std::string frame="compiler-local-role-spec-v1/"+field(std::to_string(check.roles.size()));
    for(const auto& role:check.roles) {
        frame+=field(role.id)+field(role.value_type==CppLocalRoleValueType::integer?"int":"double")+
            field(value_frame(role.initial_value))+field(std::to_string(role.probes.size()));
        for(const auto& probe:role.probes) {
            frame+=field(value_frame(probe.replacement))+field(probe.after_update?"after":"initial")+
                field(probe.expected_increase?std::to_string(*probe.expected_increase):"")+
                field(probe.before_update_of)+field(std::to_string(probe.expected_stdout.size()));
            for(const auto& output:probe.expected_stdout)frame+=field(output);
        }
    }
    return frame;
}

struct CppLocalRoleCandidate {
    std::string name;
    cpp_local_role_detail::DeclaredType declared_type=cpp_local_role_detail::DeclaredType::integer;
    std::size_t initialization_end=0; // token index in the private normalized source
    bool constant_expression=false;
};
struct CppLocalRolePlan {
    std::string normalized_source;
    cpp_local_role_detail::Tokens code;
    std::pair<std::size_t,std::size_t> main_body;
    std::vector<CppLocalRoleCandidate> candidates;
};

inline std::optional<CppLocalRolePlan> cpp_local_role_plan(
        std::string_view source,const CppLocalRoleCheck& check) {
    if(!cpp_local_role_check_valid(check))return {};
    auto scanned=cpp_local_int_probe_detail::lexical(source);
    if(!scanned)return {};
    if(std::ranges::any_of(*scanned,[](const auto& token) {
        return token.value.starts_with("id:"+std::string(cpp_local_role_detail::reserved));
    }))return {};
    auto body=cpp_local_int_probe_detail::main_body(*scanned);
    if(!body)return {};
    auto groups=cpp_local_role_detail::declaration_groups(*scanned,body->first,body->second);
    struct Split { std::size_t at; std::string prefix; };
    std::vector<Split> splits;
    for(const auto& group:groups) {
        const auto first=(*scanned)[group.declarations.front().begin].begin;
        const auto name=(*scanned)[group.first_name].begin;
        for(const auto comma:group.commas)
            splits.push_back({(*scanned)[comma].begin,std::string(source.substr(first,name-first))});
    }
    std::string normalized(source);
    for(auto split=splits.rbegin();split!=splits.rend();++split)
        normalized.replace(split->at,1,"; "+split->prefix);
    scanned=cpp_local_int_probe_detail::lexical(normalized);
    if(!scanned)return {};
    body=cpp_local_int_probe_detail::main_body(*scanned);
    if(!body)return {};
    groups=cpp_local_role_detail::declaration_groups(*scanned,body->first,body->second);
    std::vector<CppLocalRoleCandidate> candidates;
    std::set<std::string> names;
    for(const auto& group:groups)for(const auto& declaration:group.declarations) {
        if(!group.commas.empty())return {}; // every accepted group was split above
        if(!names.insert(declaration.name).second)return {};
        auto initialization=declaration.end;
        // A value-initialized scalar may establish its exercise value in a
        // following assignment: double a{}; a=1.5;. The existing locator
        // accepts it only BEFORE any read, capture, copy or state update and
        // only if its RHS does not read this local. A perturbation before that
        // assignment would simply be overwritten, so keep one object/slot at
        // its effective establishment instead of generating ambiguous aliases.
        const auto assignment=cpp_local_int_probe_detail::first_assignment(
            *scanned,initialization+1,body->second,declaration.name);
        if(assignment) {
            initialization=*assignment;
            for(std::size_t count=0;count<32;++count) {
                const auto next=initialization+1;
                if(next+1>=body->second||(*scanned)[next].value!="id:"+declaration.name||
                   (*scanned)[next+1].value!="op:=")break;
                const auto established=cpp_local_int_probe_detail::first_assignment(
                    *scanned,next,body->second,declaration.name);
                if(!established)break;
                initialization=*established;
            }
        }
        else if(!declaration.initialized)continue;
        candidates.push_back({declaration.name,declaration.type,initialization,
            declaration.constant_expression});
        if(candidates.size()>16)return {};
    }
    if(candidates.empty())return {};
    return CppLocalRolePlan{std::move(normalized),std::move(*scanned),*body,std::move(candidates)};
}

inline bool cpp_local_role_candidate_type(const CppLocalRoleCandidate& candidate,
                                           CppLocalRoleValueType type) {
    using Type=cpp_local_role_detail::DeclaredType;
    return candidate.declared_type==Type::inferred||
        (candidate.declared_type==Type::integer&&type==CppLocalRoleValueType::integer)||
        (candidate.declared_type==Type::real&&type==CppLocalRoleValueType::real);
}

namespace cpp_local_role_detail {
inline std::optional<std::pair<std::size_t,std::size_t>> update(
        const CppLocalRolePlan& plan,const CppLocalRoleCandidate& candidate) {
    const auto first=cpp_local_int_probe_detail::first_state_update(plan.code,
        candidate.initialization_end+1,plan.main_body.second,candidate.name);
    if(!first)return {};
    auto last=first->second;
    // A split +1;+2 is the same observed +3, provided no intervening use,
    // copy, call, declaration or branch separates those state changes.
    for(std::size_t count=0;count<32;++count) {
        const auto next=cpp_local_int_probe_detail::first_state_update(plan.code,
            last+1,plan.main_body.second,candidate.name);
        if(!next||next->first!=last+1)break;
        last=next->second;
    }
    return std::pair{first->first,last};
}
inline std::string initial_guard(const CppLocalRole& role,
        const CppLocalRoleCandidate& candidate) {
    const auto& name=candidate.name;
    const auto type=role.value_type==CppLocalRoleValueType::integer?"int":"double";
    std::string guard="\nstatic_assert(::std::is_same_v<::std::remove_cv_t<decltype("+name+
        ")>, "+type+">);\n";
    if(role.value_type==CppLocalRoleValueType::integer) {
        guard+="if ("+name+" != "+std::to_string(static_cast<std::int32_t>(role.initial_value))+
            ") return 1;\n";
    }else{
        const std::string low=std::string(reserved)+"low",high=std::string(reserved)+"high";
        const std::string index=std::string(reserved)+"step";
        guard+="double "+low+" = "+literal(role.initial_value)+", "+high+" = "+
            literal(role.initial_value)+";\nfor (int "+index+"=0; "+index+"<4; ++"+index+
            ") { "+low+"=::std::nextafter("+low+", -::std::numeric_limits<double>::infinity()); "+
            high+"=::std::nextafter("+high+", ::std::numeric_limits<double>::infinity()); }\n"+
            "if (!::std::isfinite(static_cast<double>("+name+")) || "+name+" < "+low+
            " || "+name+" > "+high+") return 1;\n";
    }
    return guard;
}
inline std::string shadow(const CppLocalRoleCandidate& candidate,std::string_view replacement) {
    const auto alias=std::string(reserved)+"type";
    // decltype is captured BEFORE the new identifier enters its scope. This
    // keeps const/constexpr and inferred scalar types without casting away cv.
    return "\n{\nusing "+alias+" = decltype("+candidate.name+");\n"+
        (candidate.constant_expression?"constexpr ":"")+alias+" "+candidate.name+
        " = "+std::string(replacement)+";\n";
}
} // namespace cpp_local_role_detail

// The optional other slot is required only by beforeUpdateOf. The worker binds
// that slot to the referenced role, then captures this exact variant's hash.
inline std::optional<std::string> cpp_local_role_variant(
        const CppLocalRolePlan& plan,const CppLocalRoleCheck& check,
        std::size_t role_index,std::size_t probe_index,std::size_t candidate_index,
        std::optional<std::size_t> other_candidate=std::nullopt) {
    if(!cpp_local_role_check_valid(check)||role_index>=check.roles.size()||
       candidate_index>=plan.candidates.size())return {};
    const auto& role=check.roles[role_index];
    if(probe_index>=role.probes.size())return {};
    const auto& probe=role.probes[probe_index];
    const auto& candidate=plan.candidates[candidate_index];
    if(!cpp_local_role_candidate_type(candidate,role.value_type))return {};
    const auto& code=plan.code;
    const auto initialized=code[candidate.initialization_end].end;
    std::size_t checkpoint=initialized;
    std::map<std::size_t,std::string> insertions;
    insertions[initialized]+=cpp_local_role_detail::initial_guard(role,candidate);
    std::string replacement=role.value_type==CppLocalRoleValueType::integer?
        std::to_string(static_cast<std::int32_t>(probe.replacement)):
        cpp_local_role_detail::literal(probe.replacement);
    if(probe.after_update) {
        if(other_candidate)return {};
        const auto changed=cpp_local_role_detail::update(plan,candidate);
        if(!changed)return {};
        const auto start=code[changed->first].begin;
        checkpoint=code[changed->second].end;
        const auto before=std::string(cpp_local_role_detail::reserved)+"before";
        const auto varied=std::string(cpp_local_role_detail::reserved)+"varied";
        insertions[start]+="\nconst long long "+before+"=static_cast<long long>("+candidate.name+");\n";
        auto& after=insertions[checkpoint];
        after+="\nif (static_cast<long long>("+candidate.name+") - "+before+" != "+
            std::to_string(*probe.expected_increase)+"LL) return 1;\n";
        after+="const long long "+varied+"=static_cast<long long>("+candidate.name+") + ("+
            std::to_string(static_cast<std::int32_t>(probe.replacement))+"LL);\n";
        after+="if ("+varied+">2147483647LL || "+varied+"<(-2147483647LL-1)) return 1;\n";
        replacement="static_cast<int>("+varied+")";
    }else if(!probe.before_update_of.empty()) {
        if(!other_candidate||*other_candidate>=plan.candidates.size()||
           *other_candidate==candidate_index)return {};
        const auto& other=plan.candidates[*other_candidate];
        if(other.name==candidate.name)return {};
        if(!cpp_local_role_candidate_type(other,CppLocalRoleValueType::integer))return {};
        const auto changed=cpp_local_role_detail::update(plan,other);
        if(!changed)return {};
        checkpoint=code[changed->first].begin;
        // Both independent objects must already have acquired their values.
        // In a reversed copy the historical object may be the original datum.
        if(initialized>checkpoint||code[other.initialization_end].end>checkpoint)return {};
    }else if(other_candidate)return {};
    if(replacement.empty()||checkpoint>=code[plan.main_body.second].begin)return {};
    insertions[checkpoint]+=cpp_local_role_detail::shadow(candidate,replacement);
    insertions[code[plan.main_body.second].begin]+="\n}\n";
    std::string result="#include <type_traits>\n#include <cmath>\n#include <limits>\n";
    std::size_t offset=0;
    for(const auto& [position,text]:insertions) {
        if(position<offset||position>plan.normalized_source.size())return {};
        result+=std::string_view(plan.normalized_source).substr(offset,position-offset);
        result+=text;
        offset=position;
    }
    result+=std::string_view(plan.normalized_source).substr(offset);
    return result;
}

} // namespace lgaed::curriculum

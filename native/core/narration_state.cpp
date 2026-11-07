#include "core/narration_state.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>

namespace lgaed::narration {
namespace {
void validate_semantic_plan(const SemanticPlanProof& value) {
    if((value.policy!="windows-semantic-v1"&&value.policy!="windows-semantic-v2")||value.regions.empty()||value.regions.size()>2048)
        throw std::invalid_argument("Invalid bounded semantic narration provenance");
    std::size_t bytes=0,spans=0;std::set<std::string> owners;
    for(const auto& region:value.regions){
        if(region.owner.empty()||region.owner.size()>4096||!owners.insert(region.owner).second
            ||(region.context!="auto"&&region.context!="cpp")
            ||(region.math_mode!="auto"&&region.math_mode!="text"&&region.math_mode!="inline"&&region.math_mode!="block"))
            throw std::invalid_argument("Invalid exact semantic narration owner");
        curriculum::validate_text_semantics({region.source,{}});bytes+=region.owner.size()+region.source.size();
        if(region.semantics){
            curriculum::validate_text_semantics(*region.semantics);
            if(region.semantics->source!=region.source)throw std::invalid_argument("Stale semantic narration source");
            if(value.policy=="windows-semantic-v1"&&std::ranges::any_of(region.semantics->spans,[](const auto& span){
                return span.reading==curriculum::SemanticReading::characters;}))
                throw std::invalid_argument("Literal-character reading did not exist in the V1 narration frontend");
            spans+=region.semantics->spans.size();for(const auto& span:region.semantics->spans)bytes+=span.text.size()+64;
        }
        if(bytes>4*1024*1024||spans>100000)throw std::invalid_argument("Semantic narration provenance exceeds its bound");
    }
}
std::vector<std::string> semantic_records(const Bookmark& bookmark) {
    const auto& plan=*bookmark.semantic_plan;validate_semantic_plan(plan);
    std::vector<std::string> rows{plan.policy,bookmark.key,bookmark.page,std::to_string(plan.regions.size())};
    for(const auto& region:plan.regions){
        rows.insert(rows.end(),{region.owner,region.source,region.context,region.math_mode,region.semantics?"1":"0"});
        if(region.semantics){rows.push_back(std::to_string(region.semantics->spans.size()));
            for(const auto& span:region.semantics->spans)rows.insert(rows.end(),{std::to_string(span.start),std::to_string(span.end),span.text,
                std::string(curriculum::semantic_role_name(span.role)),std::string(curriculum::semantic_reading_name(span.reading))});}
    }
    return rows;
}
std::optional<SemanticPlanProof> read_semantic_plan(const state::StateData& data,const std::string& suffix,const Bookmark& bookmark) {
    const auto* raw=state::find(data,resume_domain,"semantic_plan_v1_"+suffix);if(!raw)return {};
    const auto* rows=std::get_if<std::vector<std::string>>(raw);
    if(!rows||rows->size()<4||rows->size()>600000)throw std::invalid_argument("Invalid typed semantic narration provenance");
    std::size_t at=0;
    const auto next=[&]()->const std::string&{if(at>=rows->size())throw std::invalid_argument("Truncated semantic narration provenance");return rows->at(at++);};
    const auto number=[&](){const auto& text=next();std::size_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||text!=std::to_string(value))throw std::invalid_argument("Invalid semantic provenance count");return value;};
    SemanticPlanProof result;result.policy=next();
    if(next()!=bookmark.key||next()!=bookmark.page)throw std::invalid_argument("Semantic provenance belongs to another bookmark");
    const auto count=number();if(!count||count>2048)throw std::invalid_argument("Too many semantic provenance regions");
    for(std::size_t i=0;i<count;++i){SemanticRegionProof region;region.owner=next();region.source=next();region.context=next();region.math_mode=next();
        const auto marked=next();if(marked!="0"&&marked!="1")throw std::invalid_argument("Invalid semantic provenance presence");
        if(marked=="1"){curriculum::TextSemantics field{region.source,{}};const auto count_spans=number();
            if(count_spans>100000)throw std::invalid_argument("Too many stored semantic spans");
            for(std::size_t s=0;s<count_spans;++s){curriculum::SemanticSpan span;span.start=number();span.end=number();span.text=next();
                span.role=curriculum::parse_semantic_role(next());span.reading=curriculum::parse_semantic_reading(next());field.spans.push_back(std::move(span));}
            region.semantics=std::move(field);}
        result.regions.push_back(std::move(region));}
    if(at!=rows->size())throw std::invalid_argument("Trailing semantic narration provenance");validate_semantic_plan(result);return result;
}
bool valid_prefix(const HeardPrefix& value) noexcept {
    return value.fingerprint.size()==64&&std::ranges::all_of(value.fingerprint,[](char c){
        return(c>='0'&&c<='9')||(c>='a'&&c<='f');})&&value.microseconds>=0
        &&value.duration_microseconds>0&&value.microseconds<=value.duration_microseconds;
}
std::optional<HeardPrefix> read_prefix(const state::StateData& data,const std::string& suffix){
    const auto* raw=state::find(data,resume_domain,"heard_prefix_v1_"+suffix);
    const auto* values=raw?std::get_if<std::vector<std::string>>(raw):nullptr;
    if(!values||values->size()!=3)return {};
    HeardPrefix result;result.fingerprint=values->at(0);
    const auto number=[](const std::string& text,std::int64_t& value){
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&text==std::to_string(value);
    };
    if(!number(values->at(1),result.microseconds)||!number(values->at(2),result.duration_microseconds)
        ||!valid_prefix(result))return {};
    return result;
}
}
bool heard_prefix_matches(const Position& position,std::string_view fingerprint,
        std::int64_t duration_microseconds) noexcept {
    return position.heard_prefix&&valid_prefix(*position.heard_prefix)
        &&position.heard_prefix->fingerprint==fingerprint
        &&position.heard_prefix->duration_microseconds==duration_microseconds;
}
History read_history(const state::StateData& data){
    History history;
    for(int i=0;i<3;++i){const auto suffix=std::to_string(i);const auto key=state::string_value(data,resume_domain,"key_"+suffix);
        if(key.empty()&&state::find(data,resume_domain,"semantic_plan_v1_"+suffix))
            throw std::invalid_argument("Semantic provenance has no existing bookmark owner");
        if(key.empty()||std::ranges::any_of(history.entries,[&](const auto& entry){return entry.key==key;}))continue;
        const auto finish=state::find(data,resume_domain,"finished_"+suffix);
        history.entries.push_back({key,state::string_value(data,resume_domain,"stable_"+suffix),{
            state::integer_value(data,resume_domain,"segment_"+suffix),state::integer_value(data,resume_domain,"position_"+suffix),
            std::max<std::int64_t>(0,state::integer_value(data,resume_domain,"heard_"+suffix)),finish&&std::holds_alternative<bool>(*finish)&&std::get<bool>(*finish)},
            state::string_value(data,resume_domain,"page_"+suffix)});
        history.entries.back().position.heard_prefix=read_prefix(data,suffix);
        history.entries.back().semantic_plan=read_semantic_plan(data,suffix,history.entries.back());
    }
    history.active_key=state::string_value(data,resume_domain,"active_key");history.active_owner=state::string_value(data,resume_domain,"active_owner");return history;
}
void write_history(state::StateData& data,const History& history){
    if(history.entries.size()>3)throw std::invalid_argument("Narration history exceeds three pages");
    for(const auto& entry:history.entries)if(entry.semantic_plan){
        if(entry.key.empty()||entry.page.empty())throw std::invalid_argument("Semantic provenance requires an existing keyed page bookmark");
        validate_semantic_plan(*entry.semantic_plan);
    }
    auto& prefs=data.preferences[resume_domain];prefs["active_key"]=history.active_key;prefs["active_owner"]=history.active_owner;
    for(std::size_t i=0;i<3;++i){const auto suffix=std::to_string(i);
        if(i>=history.entries.size()){for(const auto prefix:{"key_","segment_","position_","finished_","heard_","stable_","page_","heard_prefix_v1_","semantic_plan_v1_"})prefs.erase(prefix+suffix);continue;}
        const auto& entry=history.entries[i];prefs["key_"+suffix]=entry.key;prefs["segment_"+suffix]=entry.position.segment;
        prefs["position_"+suffix]=entry.position.milliseconds;prefs["finished_"+suffix]=entry.position.finished;prefs["heard_"+suffix]=entry.position.heard_through;
        if(entry.stable.empty())prefs.erase("stable_"+suffix);else prefs["stable_"+suffix]=entry.stable;
        if(entry.page.empty())prefs.erase("page_"+suffix);else prefs["page_"+suffix]=entry.page;
        if(entry.semantic_plan)prefs["semantic_plan_v1_"+suffix]=semantic_records(entry);else prefs.erase("semantic_plan_v1_"+suffix);
        if(entry.position.heard_prefix&&valid_prefix(*entry.position.heard_prefix)){
            const auto& prefix=*entry.position.heard_prefix;
            prefs["heard_prefix_v1_"+suffix]=std::vector<std::string>{prefix.fingerprint,
                std::to_string(prefix.microseconds),std::to_string(prefix.duration_microseconds)};
        }else prefs.erase("heard_prefix_v1_"+suffix);
    }
}
std::optional<Position> restore(const History& history,std::string_view key,std::size_t segments){
    const auto found=std::ranges::find(history.entries,key,&Bookmark::key);if(found==history.entries.end())return {};
    auto value=found->position;if(value.segment<0||value.milliseconds<0||static_cast<std::size_t>(value.segment)>=segments
        ||(value.finished&&static_cast<std::size_t>(value.segment)+1!=segments))return {};
    if(value.heard_prefix&&!valid_prefix(*value.heard_prefix))value.heard_prefix.reset();return value;
}
History begin(History history,std::string key,std::string owner,Position value,std::string stable,std::string page){
    if(key.empty()||owner.empty()||value.segment<0||value.milliseconds<0)throw std::invalid_argument("Invalid narration owner or position");
    std::optional<SemanticPlanProof> semantic_plan;
    if(const auto known=std::ranges::find(history.entries,key,&Bookmark::key);known!=history.entries.end()){
        semantic_plan=known->semantic_plan;
        if(stable.empty())stable=known->stable;
        if(page.empty())page=known->page;
    }else value.heard_prefix.reset();
    // Page identity is additive provenance in this same bounded bookmark.
    // A voice/provider change replaces its page slot without inheriting the old
    // recording's offset. Old entries without proven provenance are not guessed.
    std::erase_if(history.entries,[&](const auto& entry){return entry.key==key||(!page.empty()&&entry.page==page);});value.finished=false;
    history.entries.insert(history.entries.begin(),{key,std::move(stable),value,std::move(page),std::move(semantic_plan)});if(history.entries.size()>3)history.entries.resize(3);
    history.active_key=std::move(key);history.active_owner=std::move(owner);return history;
}
History checkpoint(History history,std::string_view key,std::string_view owner,Position value){
    if(owner.empty()||history.active_key!=key||history.active_owner!=owner||value.segment<0||value.milliseconds<0)return history;
    for(auto& entry:history.entries)if(entry.key==key)entry.position=value;
    if(value.finished){history.active_key.clear();history.active_owner.clear();}return history;
}
Position seek(Position value,std::int64_t segment,std::int64_t milliseconds){
    segment=std::max<std::int64_t>(0,segment);if(segment!=value.segment)value.heard_prefix.reset();
    value.segment=segment;value.milliseconds=std::max<std::int64_t>(0,milliseconds);value.finished=false;return value;
}
}

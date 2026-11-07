#include "platform/windows/bibliography_revision.hpp"
#include <windows.h>
#include <winrt/Windows.Data.Text.h>
#include <algorithm>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lgaed::windows::bibliography {
namespace {
using Text=std::wstring_view;
using Unicode=winrt::Windows::Data::Text::UnicodeCharacters;
using Category=winrt::Windows::Data::Text::UnicodeGeneralCategory;
constexpr auto missing=Text::npos;
bool ascii_space(wchar_t c){return c==L' '||c==L'\t'||c==L'\r'||c==L'\n';}
bool whitespace(wchar_t c){return (c>=9&&c<=13)||(c>=0x1c&&c<=0x20)||c==0x85||c==0xa0||c==0x1680||
    (c>=0x2000&&c<=0x200a)||c==0x2028||c==0x2029||c==0x202f||c==0x205f||c==0x3000;}
Text trim(Text text){while(!text.empty()&&ascii_space(text.front()))text.remove_prefix(1);
    while(!text.empty()&&ascii_space(text.back()))text.remove_suffix(1);return text;}
std::uint32_t point(Text text,std::size_t at,std::size_t& width){
    const auto first=static_cast<std::uint32_t>(text[at]);width=1;
    if(first>=0xd800&&first<=0xdbff){
        if(at+1>=text.size()||text[at+1]<0xdc00||text[at+1]>0xdfff)throw std::invalid_argument("Invalid Unicode scalar");
        width=2;return 0x10000+((first-0xd800)<<10)+(text[at+1]-0xdc00);
    }
    if(first>=0xdc00&&first<=0xdfff)throw std::invalid_argument("Invalid Unicode scalar");return first;
}
bool word(Text text,std::size_t at){
    if(text[at]==L'_')return true;
    if(text[at]>=0xdc00&&text[at]<=0xdfff&&at&&text[at-1]>=0xd800&&text[at-1]<=0xdbff)--at;
    std::size_t width=0;const auto category=Unicode::GetGeneralCategory(point(text,at,width));
    return category<=Category::OtherLetter||category==Category::DecimalDigitNumber||
        category==Category::LetterNumber||category==Category::OtherNumber;
}
bool author(Text text){
    text=trim(text);if(text.empty())return false;bool uppercase=false;
    for(std::size_t at=0;at<text.size();){std::size_t width=0;const auto value=point(text,at,width);at+=width;
        if(value<128&&std::wstring_view(L" \t\r\n.,&-'").find(static_cast<wchar_t>(value))!=missing)continue;
        if(value==0x2019)continue;
        const auto category=Unicode::GetGeneralCategory(value);
        if(category>Category::EnclosingMark)return false;
        uppercase=uppercase||Unicode::IsUppercase(value);
    }
    return uppercase;
}
std::wstring lower(Text text){
    if(text.empty())return {};
    const auto size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr,0);
    if(!size)throw std::invalid_argument("Cannot normalize locator spelling");
    std::wstring result(static_cast<std::size_t>(size),L'\0');
    if(!LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,text.data(),static_cast<int>(text.size()),result.data(),size,nullptr,nullptr,0))
        throw std::invalid_argument("Cannot normalize locator spelling");
    return result;
}
bool locators(Text text){
    static const std::wstring space=LR"([ \t\r\n])";
    static const std::wstring label=LR"((?:p\.|pp\.|párr\.|párrs\.|párrafo(?:s)?|sección|secciones|sec\.|secs\.|tabla(?:s)?|capítulo(?:s)?|cap\.|caps\.|figura(?:s)?|fig\.|figs\.|apéndice(?:s)?|anexo(?:s)?|artículo(?:s)?|art\.|arts\.|page(?:s)?|paragraph(?:s)?|para\.|paras\.|section(?:s)?|chapter(?:s)?|table(?:s)?|figure(?:s)?|appendix|appendices|§{1,2}))";
    static const std::wstring number=LR"((?:[0-9]+(?:\.[0-9]+)*[a-z]?|[ivxlcdm]+))";
    static const std::wstring value=number+L"(?:"+space+LR"(*[-–—])"+space+L"*"+number+L")?";
    static const std::wstring locator=label+space+L"+"+value;
    static const std::wregex pattern(locator+L"(?:(?:"+space+L"*,"+space+L"*|"+space+L"+(?:y|e|and|et|&)"+space+L"+)"+locator+L")*");
    const auto normalized=lower(trim(text));return std::regex_match(normalized,pattern);
}
struct Component{std::wstring separator,head,tail;};
std::optional<std::vector<Component>> components(Text text){
    static const std::wregex header(LR"(([^0-9()]+?),[ \t\r\n]+[0-9]{4}[a-z]?([\s\S]*))");
    std::vector<Component> result;
    for(std::size_t first=0,index=0;;++index){
        const auto end=text.find(L';',first);const auto part=text.substr(first,end==missing?missing:end-first);
        std::match_results<Text::const_iterator> found;
        if(std::regex_match(part.begin(),part.end(),found,header)&&author(Text(found[1].first,found[1].second))){
            const Text tail(found[2].first,found[2].second);
            if(!tail.empty()&&(tail.front()!=L','||!locators(tail.substr(1))))return {};
            result.push_back({index?L";":L"",std::wstring(part.substr(0,part.size()-tail.size())),std::wstring(tail)});
        }else if(!result.empty()&&locators(part))result.back().tail+=L';'+std::wstring(part);
        else return {};
        if(end==missing)break;first=end+1;
    }
    if(result.empty())return {};return result;
}
bool closing_quote(wchar_t c){return std::wstring_view(L"\"'”’»").find(c)!=missing;}
std::optional<std::size_t> protected_end(Text text,std::size_t at){
    // Preserve raw C++ strings, including their delimiter and arbitrary quotes.
    for(const Text prefix:{L"u8R\"",L"uR\"",L"UR\"",L"LR\"",L"R\""})if(text.substr(at).starts_with(prefix)){
        const auto first=at+prefix.size();std::size_t cursor=first,count=0;
        while(cursor<text.size()&&text[cursor]!=L'('&&count<=16){
            if(whitespace(text[cursor])||text[cursor]==L')'||text[cursor]==L'\\')break;
            std::size_t width=0;(void)point(text,cursor,width);cursor+=width;++count;
        }
        if(cursor<text.size()&&text[cursor]==L'('&&count<=16){
            const auto closing=L")"+std::wstring(text.substr(first,cursor-first))+L'"';
            const auto end=text.find(closing,cursor+1);return end==missing?missing:end+closing.size();
        }
        break;
    }
    const auto c=text[at];
    if(c==L'`'||text.substr(at).starts_with(L"~~~")){
        std::size_t width=1;while(at+width<text.size()&&text[at+width]==c)++width;
        const auto end=text.find(std::wstring(width,c),at+width);return end==missing?missing:end+width;
    }
    if(text.substr(at).starts_with(L"//")){const auto end=text.find(L'\n',at+2);return end==missing?text.size():end;}
    if(text.substr(at).starts_with(L"/*")){const auto end=text.find(L"*/",at+2);return end==missing?missing:end+2;}
    wchar_t closing=0;
    switch(c){case L'"':case L'\'':closing=c;break;case L'“':closing=L'”';break;case L'‘':closing=L'’';break;case L'«':closing=L'»';break;default:return {};}
    if((c==L'\''||c==L'‘')&&at&&at+1<text.size()&&word(text,at-1)&&word(text,at+1))return {};
    for(auto cursor=at+1;cursor<text.size();++cursor){
        if(text[cursor]==L'\\'){++cursor;continue;}
        if(text[cursor]==closing)return cursor+1;
    }
    return missing;
}
struct Citation{std::size_t start,end;std::vector<Component> values;};
bool callable(Text text){
    std::size_t at=0;
    const auto name=[&](){
        if(at==text.size()||!word(text,at))return false;
        std::size_t width=0;if(Unicode::GetGeneralCategory(point(text,at,width))==Category::DecimalDigitNumber)return false;
        do{(void)point(text,at,width);at+=width;}while(at<text.size()&&word(text,at));return true;
    };
    if(!name())return false;
    while(at<text.size()){
        while(at<text.size()&&whitespace(text[at]))++at;
        if(text.substr(at).starts_with(L"::")||text.substr(at).starts_with(L"->"))at+=2;
        else if(at<text.size()&&text[at]==L'.')++at;else return false;
        while(at<text.size()&&whitespace(text[at]))++at;
        if(!name())return false;
    }
    return true;
}
bool ambiguous_code_prefix(Text source,std::size_t stop){
    const auto line=source.substr(0,stop).find_last_of(L'\n');auto cursor=line==missing?0:line+1;
    std::wstring visible;
    while(cursor<stop){
        if(const auto end=protected_end(source,cursor)){
            if(*end==missing||*end>stop)return true;
            visible+=L' ';cursor=*end;
        }else visible+=source[cursor++];
    }
    const auto last=visible.find_last_of(L".!?;");auto clause=Text(visible).substr(last==missing?0:last+1);
    while(!clause.empty()&&whitespace(clause.front()))clause.remove_prefix(1);
    while(!clause.empty()&&whitespace(clause.back()))clause.remove_suffix(1);
    if(clause.empty())return false;
    if(callable(clause)||clause.find_first_of(L"=+*/%<>[]{}|&^~?\\")!=missing||clause.find(L"->")!=missing||clause.find(L"::")!=missing)return true;
    for(std::size_t at=1;at+1<clause.size();++at)if(clause[at]==L'-'&&whitespace(clause[at-1])&&whitespace(clause[at+1]))return true;
    static const auto keywords=[](){std::set<std::wstring> result;std::wistringstream input(
        L"alignas alignof and and_eq asm auto bitand bitor bool break case catch "
        L"char char8_t char16_t char32_t class compl concept const consteval constexpr constinit const_cast continue "
        L"co_await co_return co_yield decltype default delete do double dynamic_cast else enum explicit export extern "
        L"false float for friend goto if inline int long mutable namespace new noexcept not not_eq nullptr operator or "
        L"or_eq private protected public register reinterpret_cast requires return short signed sizeof static static_assert "
        L"static_cast struct switch template this thread_local throw true try typedef typeid typename union unsigned using "
        L"virtual void volatile wchar_t while xor xor_eq final override import module");
        for(std::wstring token;input>>token;)result.insert(std::move(token));return result;}();
    static const std::set<std::wstring> alternatives{L"and",L"and_eq",L"bitand",L"bitor",L"compl",L"not",L"not_eq",L"or",L"or_eq",L"xor",L"xor_eq"};
    bool first=true;
    for(std::size_t at=0;at<clause.size();){std::size_t width=0;const auto cp=point(clause,at,width);
        if(!word(clause,at)||Unicode::GetGeneralCategory(cp)==Category::DecimalDigitNumber){at+=width;continue;}
        const auto begin=at;
        do{(void)point(clause,at,width);at+=width;}while(at<clause.size()&&word(clause,at));
        const auto token=std::wstring(clause.substr(begin,at-begin));
        if((first&&keywords.contains(token))||alternatives.contains(token))return true;first=false;
    }
    return false;
}
std::optional<std::vector<Citation>> citations(Text text){
    std::vector<Citation> result;std::size_t at=0;
    while(at<text.size()){
        if(const auto end=protected_end(text,at)){if(*end==missing)return {};at=*end;continue;}
        if(text[at]==L')')return {};
        if(text[at]!=L'('){++at;continue;}
        const auto first=at;auto cursor=at+1;unsigned depth=1;bool nested=false;
        while(cursor<text.size()&&depth){
            if(const auto end=protected_end(text,cursor)){if(*end==missing)return {};cursor=*end;continue;}
            if(text[cursor]==L'('){++depth;nested=true;}else if(text[cursor]==L')')--depth;
            ++cursor;
        }
        if(depth)return {};
        auto preceding=text.substr(0,first);while(!preceding.empty()&&ascii_space(preceding.back()))preceding.remove_suffix(1);
        const bool boundary=(!first||whitespace(text[first-1]))&&(preceding.empty()||!closing_quote(preceding.back()))&&
            (cursor==text.size()||whitespace(text[cursor])||std::wstring_view(L".,!?…").find(text[cursor])!=missing)&&!ambiguous_code_prefix(text,first);
        if(!nested&&boundary)if(auto parsed=components(text.substr(first+1,cursor-first-2)))result.push_back({first,cursor,std::move(*parsed)});
        at=cursor;
    }
    return result;
}
}
bool only_parenthetical_locator_removal(std::string_view before,std::string_view after){
    try{
        constexpr std::size_t limit=8*1024*1024;
        if(before.empty()||after.empty()||before==after||before.size()>limit||after.size()>limit)return false;
        const auto old_value=winrt::to_hstring(before),new_value=winrt::to_hstring(after);
        const Text old_text(old_value),new_text(new_value);
        if(std::ranges::all_of(old_text,whitespace)||std::ranges::all_of(new_text,whitespace))return false;
        const auto old=citations(old_text),now=citations(new_text);
        if(!old||!now||old->size()!=now->size())return false;
        std::size_t old_end=0,new_end=0;bool removed=false;
        for(std::size_t at=0;at<old->size();++at){const auto& first=(*old)[at];const auto& second=(*now)[at];
            if(old_text.substr(old_end,first.start-old_end)!=new_text.substr(new_end,second.start-new_end)||first.values.size()!=second.values.size())return false;
            for(std::size_t n=0;n<first.values.size();++n){const auto& a=first.values[n];const auto& b=second.values[n];
                if(a.separator!=b.separator||a.head!=b.head)return false;
                if(a.tail==b.tail)continue;
                if(!a.tail.empty()&&b.tail.empty())removed=true;else return false;
            }
            old_end=first.end;new_end=second.end;
        }
        return removed&&old_text.substr(old_end)==new_text.substr(new_end);
    }catch(...){return false;}
}
}

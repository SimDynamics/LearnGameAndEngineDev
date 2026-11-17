#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lgaed::curriculum {
namespace cmake_message_trace_detail {

// Only recover argument delimiters; CMake's trace supplies evaluated values.
// Grammar: https://cmake.org/cmake/help/latest/manual/cmake-language.7.html
class Delimiters {
public:
    Delimiters(std::string_view source,std::size_t position)
        :source_(source),position_(position) {}

    std::optional<std::vector<bool>> message() { return command("message"); }

    std::optional<std::vector<bool>> command(std::string_view name) {
        const auto line_start=position_;
        if(position_==0&&source_.substr(0,3)=="\xef\xbb\xbf")position_=3;
        if(!trivia()||source_.substr(line_start,position_-line_start).find('\n')!=
                std::string_view::npos)return {};
        for(const auto expected:name) {
            if(position_==source_.size())return {};
            auto actual=source_[position_++];
            if(actual>='A'&&actual<='Z')actual=static_cast<char>(actual-'A'+'a');
            if(actual!=expected)return {};
        }
        if(!trivia()||!take('('))return {};
        std::vector<bool> unquoted;
        std::size_t depth=1;
        while(trivia()&&position_<source_.size()) {
            const auto character=source_[position_];
            if(character==')') {
                ++position_;
                if(--depth==0)return unquoted;
                unquoted.push_back(true);
            } else if(character=='(') {
                ++position_;++depth;unquoted.push_back(true);
            } else if(character=='"') {
                if(!quoted(false))return {};
                unquoted.push_back(false);
            } else if(const auto length=bracket_length(position_)) {
                if(!bracket(length))return {};
                unquoted.push_back(false);
            } else {
                if(!unquoted_argument())return {};
                unquoted.push_back(true);
            }
        }
        return {};
    }

private:
    static bool space(char value) {
        return value==' '||value=='\t'||value=='\r'||value=='\n';
    }
    static bool identifier(char value) {
        return (value>='a'&&value<='z')||(value>='A'&&value<='Z')||
            (value>='0'&&value<='9')||value=='_';
    }
    bool take(char value) {
        if(position_==source_.size()||source_[position_]!=value)return false;
        ++position_;return true;
    }
    std::size_t bracket_length(std::size_t start) const {
        if(start==source_.size()||source_[start]!='[')return 0;
        auto end=start+1;
        while(end<source_.size()&&source_[end]=='=')++end;
        return end<source_.size()&&source_[end]=='['?end-start+1:0;
    }
    bool bracket(std::size_t length) {
        const std::string close="]"+std::string(length-2,'=')+"]";
        const auto end=source_.find(close,position_+length);
        if(end==std::string_view::npos)return false;
        position_=end+length;return true;
    }
    bool trivia() {
        while(position_<source_.size()) {
            if(space(source_[position_])) {++position_;continue;}
            if(source_[position_]!='#')return true;
            ++position_;
            if(const auto length=bracket_length(position_)) {
                if(!bracket(length))return false;
            } else {
                const auto end=source_.find('\n',position_);
                position_=end==std::string_view::npos?source_.size():end;
            }
        }
        return true;
    }
    bool quoted(bool legacy) {
        ++position_;
        while(position_<source_.size()) {
            const auto character=source_[position_++];
            if(character=='"')return true;
            if(character=='\0'||(legacy&&(character=='\n'||character=='\r'||
                    character=='#'||character=='('||character==')')))return false;
            if(character=='\\') {
                if(position_==source_.size()||source_[position_]=='\0')return false;
                if(legacy&&(source_[position_]=='\n'||source_[position_]=='\r'))return false;
                ++position_;
            }
        }
        return false;
    }
    bool unquoted_argument() {
        const auto begin=position_;
        while(position_<source_.size()) {
            const auto character=source_[position_];
            if(space(character)||character=='('||character==')'||character=='#')break;
            if(character=='\0')return false;
            if(character=='\\') {
                if(position_+1==source_.size()||source_[position_+1]=='\0'||
                        source_[position_+1]=='\n')return false;
                position_+=2;
            } else if(character=='"') {
                // CMake also accepts embedded quotes in legacy unquoted tokens.
                if(!quoted(true))return false;
            } else if(character=='$'&&position_+1<source_.size()&&
                    source_[position_+1]=='(') {
                auto end=position_+2;
                while(end<source_.size()&&identifier(source_[end]))++end;
                if(end<source_.size()&&source_[end]==')')position_=end+1;
                else ++position_;
            } else ++position_;
        }
        return position_!=begin;
    }

    std::string_view source_;
    std::size_t position_;
};

// Match CMake's list expansion: remove empty elements, split at unescaped ';'
// outside balanced [], and remove only the backslash escaping a semicolon.
// https://github.com/Kitware/CMake/blob/v3.31.6/Source/cmList.cxx (cmList::Insert)
inline void append_unquoted(std::string_view value,std::vector<std::string>& args) {
    std::string item;
    std::ptrdiff_t brackets=0;
    for(std::size_t index=0;index<value.size();++index) {
        const auto character=value[index];
        if(character=='\\'&&index+1<value.size()&&value[index+1]==';') {
            item.push_back(';');++index;
        } else if(character==';'&&brackets==0) {
            if(!item.empty()) {args.push_back(item);item.clear();}
        } else {
            if(character=='[')++brackets;
            else if(character==']')--brackets;
            item.push_back(character);
        }
    }
    if(!item.empty())args.push_back(item);
}

inline std::optional<std::string> status(const std::vector<std::string>& args) {
    if(args.empty()||args.front()!="STATUS")return {};
    std::string result;
    for(std::size_t index=1;index<args.size();++index)result+=args[index];
    return result;
}

} // namespace cmake_message_trace_detail

// The caller must first attribute this executed trace event to an original
// authored file. trace-expand expands variables but preserves syntactic argument
// boundaries; it does NOT perform unquoted list expansion or record delimiters.
// See PrintCommandTrace versus ExpandArguments in:
// https://github.com/Kitware/CMake/blob/v3.31.6/Source/cmMakefile.cxx
// Recover delimiters only when needed, then follow message's exact STATUS mode
// and separator-free concatenation. No source evaluation or output matching.
inline std::optional<std::string> cmake_status_message_from_trace(
        std::string_view original_source,std::size_t event_line,
        const std::vector<std::string>& expanded_args) {
    bool delimiters_needed=false;
    for(const auto& argument:expanded_args)
        if(argument.empty()||argument.find(';')!=std::string::npos)delimiters_needed=true;
    if(!delimiters_needed)return cmake_message_trace_detail::status(expanded_args);

    // The project already has this source-size limit. A failed attribution must
    // never turn an ambiguous quoted argument into a successful STATUS message.
    if(event_line==0||original_source.size()>8*1024*1024)return {};
    std::size_t offset=0;
    for(std::size_t line=1;line<event_line;++line) {
        const auto newline=original_source.find('\n',offset);
        if(newline==std::string_view::npos)return {};
        offset=newline+1;
    }
    const auto unquoted=cmake_message_trace_detail::Delimiters(original_source,offset).message();
    if(!unquoted||unquoted->size()!=expanded_args.size())return {};
    std::vector<std::string> effective_args;
    for(std::size_t index=0;index<expanded_args.size();++index) {
        if((*unquoted)[index])
            cmake_message_trace_detail::append_unquoted(expanded_args[index],effective_args);
        else effective_args.push_back(expanded_args[index]);
    }
    return cmake_message_trace_detail::status(effective_args);
}

} // namespace lgaed::curriculum

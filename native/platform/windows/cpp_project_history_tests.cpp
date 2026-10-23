#include "platform/windows/cpp_project_history.hpp"
#include "platform/windows/state_store.hpp"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace lgaed;
using namespace lgaed::windows;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}require(rejected,"Invalid history operation was accepted");}
void number(std::vector<std::uint8_t>& bytes,std::uint64_t value,unsigned count){for(int shift=static_cast<int>((count-1)*8);shift>=0;shift-=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
void text(std::vector<std::uint8_t>& bytes,std::string_view value){number(bytes,value.size(),4);bytes.insert(bytes.end(),value.begin(),value.end());}
std::vector<std::uint8_t> android_record(std::string_view key){
    // Original DataOutputStream framing, independent of the Windows writer.
    std::vector<std::uint8_t> bytes;text(bytes,"lgp-cpp-local-history-v1");text(bytes,key);text(bytes,"9a8a7874-b19a-4700-86ac-88bd102a63d5");number(bytes,1792536389797,8);text(bytes,"Revisión Android");
    number(bytes,2,4);text(bytes,"main.cpp");text(bytes,"    return 0;\r\n");text(bytes,"include/nota.h");text(bytes,"// niño 𝑥\n");
    number(bytes,1,4);text(bytes,"main.cpp");number(bytes,1,4);text(bytes,"main.cpp");text(bytes,state::make_indentation_metadata("    return 0;\r\n",{0}));return bytes;
}
void write(const std::filesystem::path& path,std::span<const std::uint8_t> bytes){std::filesystem::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));require(static_cast<bool>(stream),"Cannot create isolated history fixture");}
}
int main(int argc,char** argv){try{
    require(argc==2,"Supply the existing isolated QA output directory");const auto root=std::filesystem::absolute(argv[1])/("cpp-history-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    require(!std::filesystem::exists(root),"History QA must not reuse a profile");const std::string identity="programming/test__unit__lesson/4/7",key=state::sha256(identity);
    const auto bytes=android_record(key);const auto id=state::sha256(bytes);const auto original=decode_cpp_revision(bytes,id,key);
    require(original.files==curriculum::CppProjectFiles{{"main.cpp","    return 0;\r\n"},{"include/nota.h","// niño 𝑥\n"}}&&original.sources==std::vector<std::string>{"main.cpp"}&&original.created_at==1792536389797,"Original Android binary history changed source, order, Unicode or timestamps");
    require(state::indentation_units(original.files[0].second,original.indentation.at("main.cpp"))==std::vector<std::int64_t>{0},"Original tab identity was lost in the binary history");
    CppProjectHistory history(root,identity);write(root/key/(id+".revision"),bytes);
    require(history.load(id).files==original.files&&history.summaries().size()==1,"The active history did not reuse the original Android record");
    const curriculum::CppProjectFiles current{{"main.cpp","    return 7;\n"},{"include/nota.h","// personal\t \r\n"},{"nuevo.cpp","// nuevo\n"}};
    const auto recorded=history.record(current,{"main.cpp","nuevo.cpp"},"Prueba actual",{{"main.cpp",state::make_indentation_metadata(current[0].second,{0})}});
    require(recorded.files==current&&history.load(recorded.id).files==current&&history.summaries().size()==2,"Recording/reopening changed another revision or source bytes");
    require(state::read_bytes(root/key/(id+".revision"))==bytes,"Registering a new revision rewrote the imported record");
    const auto statuses=history.statuses(current,original);require(statuses.size()==3&&statuses[0].path=="include/nota.h"&&statuses[0].change==CppHistoryChange::modified&&statuses[2].change==CppHistoryChange::added,"History file statuses do not compare the current draft against the chosen revision");
    const auto restored=history.restore_file("main.cpp",current,original);
    require(restored==curriculum::CppProjectFiles{{"main.cpp",original.files[0].second},current[1],current[2]},"Restoring one file replaced or normalized an unrelated file");
    const auto removed=history.restore_file("nuevo.cpp",current,original);require(removed==curriculum::CppProjectFiles{current[0],current[1]},"Removing a file absent from the revision changed another source");
    const auto differences=history.diff("main.cpp",current,original);require(differences.size()==2&&differences[0].type==CppHistoryLineType::removed&&differences[0].text=="    return 0;"&&differences[0].ending=="\r\n"&&differences[1].type==CppHistoryLineType::added&&differences[1].ending=="\n","History diff lost exact indentation, source or CRLF/LF differences");
    auto missing_final=original;missing_final.files={{"main.cpp","a\r\nb"}};const auto exact=history.diff("main.cpp",{{"main.cpp","a\r\nc\r"}},missing_final);
    require(exact.size()==3&&exact[0].type==CppHistoryLineType::context&&exact[0].ending=="\r\n"&&exact[1].ending.empty()&&exact[2].ending=="\r","History diff hid a missing final newline or CR-only line ending");
    auto corrupt=bytes;corrupt.back()^=1;rejects([&]{(void)decode_cpp_revision(corrupt,id,key);});rejects([&]{(void)decode_cpp_revision(bytes,id,state::sha256("other project"));});
    auto invalid_unicode=bytes;const std::string label="Revisión Android";const auto at=std::search(invalid_unicode.begin(),invalid_unicode.end(),label.begin(),label.end(),
        [](std::uint8_t byte,char letter){return byte==static_cast<unsigned char>(letter);});require(at!=invalid_unicode.end(),"Android fixture label is missing");*at=0xff;
    rejects([&]{(void)decode_cpp_revision(invalid_unicode,state::sha256(invalid_unicode),key);});
    auto bad=original;bad.project_key=state::sha256("another project");rejects([&]{(void)history.restore_file("main.cpp",current,bad);});rejects([&]{(void)history.load("../main");});
    write(root/key/(id+".revision"),corrupt);rejects([&]{(void)history.summaries();});write(root/key/(id+".revision"),bytes);
    rejects([&]{(void)history.record({{"main.cpp",""},{"main.cpp","duplicate"}},{"main.cpp"},"duplicate");});
    rejects([&]{(void)history.record({{"../outside.cpp",""}},{},"outside");});rejects([&]{(void)history.record(current,{"include/nota.h"},"header");});
    rejects([&]{(void)history.record(current,{},"");});
    require(history.summaries().size()==2&&state::read_bytes(root/key/(id+".revision"))==bytes,"Rejected history operations changed valid records");
    const auto base=std::filesystem::weakly_canonical(std::filesystem::absolute(argv[1])),resolved=std::filesystem::weakly_canonical(root);
    require(base.is_absolute()&&resolved.is_absolute()&&resolved.parent_path()==base&&resolved.filename().string().starts_with("cpp-history-"),"Refusing to remove a history path outside this test's own QA directory");
    state::require_safe_path(resolved);std::filesystem::remove_all(state::native_io_path(resolved));
    std::cout<<"PASS original Android revision codec/import compatibility, immutable atomic registration, exact Unicode/whitespace/indentation, per-file restore/diff, corruption and ownership checks\n";return 0;
}catch(const std::exception& error){std::cerr<<"Project history failure: "<<error.what()<<'\n';return 1;}}

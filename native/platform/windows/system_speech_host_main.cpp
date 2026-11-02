#include "platform/windows/system_speech_process.hpp"
#include <vector>

int wmain(int argc,wchar_t** argv){
    std::vector<std::wstring_view> arguments;
    for(int i=1;i<argc;++i)arguments.emplace_back(argv[i]);
    return lgaed::windows::run_system_speech_host(arguments);
}

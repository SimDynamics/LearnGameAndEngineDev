#include <windows.h>
#include <string>
#include <string_view>

namespace {
std::wstring quote(std::wstring_view value){
    std::wstring result=L"\"";std::size_t slashes=0;
    for(const auto ch:value){
        if(ch==L'\\'){++slashes;continue;}
        result.append(slashes*(ch==L'\"'?2:1),L'\\');slashes=0;
        if(ch==L'\"')result.push_back(L'\\');result.push_back(ch);
    }
    result.append(slashes*2,L'\\');result.push_back(L'\"');return result;
}
int fail(const char* action){
    const auto code=GetLastError();const auto message=std::string(action)+" (Windows error "+std::to_string(code)+")\n";
    DWORD written=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),message.data(),static_cast<DWORD>(message.size()),&written,nullptr);return 125;
}
}

// The runner creates this host suspended, in its existing bounded job, with a
// new hidden console. Only this process's console is changed; neither Windows
// preferences nor the launching terminal's code page are touched.
int wmain(int argc,wchar_t** argv){
    if(argc<2)return 125;
    const auto title=L"LearnGameAndEngineDev.CppTool."+std::to_wstring(GetCurrentProcessId());
    SetConsoleTitleW(title.c_str());
    if(!SetConsoleCP(CP_UTF8)||!SetConsoleOutputCP(CP_UTF8))return fail("Set private tool console UTF-8");
    if(const auto window=GetConsoleWindow();window&&IsWindowVisible(window))return fail("Tool console must remain hidden");
    std::wstring command=quote(argv[1]);for(int index=2;index<argc;++index)command+=L" "+quote(argv[index]);
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    // Inherit our private UTF-8 console while retaining the runner's separate
    // binary stdout/stderr handles. Descendants remain in the same parent job.
    if(!CreateProcessW(argv[1],command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process))return fail("Start native build tool");
    CloseHandle(process.hThread);
    if(WaitForSingleObject(process.hProcess,INFINITE)!=WAIT_OBJECT_0){CloseHandle(process.hProcess);return fail("Wait for native build tool");}
    DWORD status=125;if(!GetExitCodeProcess(process.hProcess,&status)){CloseHandle(process.hProcess);return fail("Read native build tool status");}
    CloseHandle(process.hProcess);ExitProcess(status);
}

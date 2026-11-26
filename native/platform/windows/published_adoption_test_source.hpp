#pragma once
// Windows test-only reader of immutable Git blobs. No lesson, working tree,
// user profile, or historical receipt is rewritten to construct a fixture.
#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Security.Cryptography.h>
#include <winrt/Windows.Security.Cryptography.Core.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

namespace lgaed::qa {
namespace adoption_detail {
struct Handle {
    HANDLE value{};
    Handle()=default;
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    ~Handle(){close();}
    void close() noexcept {
        if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);
        value=nullptr;
    }
};
inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Missing adoption QA pin manifest");
    return {std::istreambuf_iterator<char>{file},std::istreambuf_iterator<char>{}};
}
inline std::string git_blob(const std::filesystem::path& root,std::string_view pin) {
    if(pin.size()!=40||pin.find_first_not_of("0123456789abcdef")!=pin.npos)
        throw std::runtime_error("Invalid pre-adoption Git object pin");
    std::wstring git(32768,L'\0');
    const auto count=SearchPathW(nullptr,L"git.exe",nullptr,
        static_cast<DWORD>(git.size()),git.data(),nullptr);
    if(!count||count>=git.size())
        throw std::runtime_error("Git is required for authentic historical Windows QA");
    git.resize(count);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
    Handle reader,writer,nul,process,thread;
    if(!CreatePipe(&reader.value,&writer.value,&security,0)||
       !SetHandleInformation(reader.value,HANDLE_FLAG_INHERIT,0))
        throw std::runtime_error("Cannot capture immutable Git object");
    nul.value=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,&security,OPEN_EXISTING,0,nullptr);
    if(nul.value==INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot isolate Git QA standard streams");
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=nul.value;
    startup.hStdOutput=writer.value;
    startup.hStdError=nul.value;
    PROCESS_INFORMATION child{};
    std::wstring command=L"\""+git+
        L"\" --no-optional-locks --no-replace-objects cat-file blob "+
        std::wstring(pin.begin(),pin.end());
    // A regular local clone already owns these objects. Disable lazy fetching
    // in the child so a missing historical object fails without network I/O.
    const auto old_size=GetEnvironmentVariableW(L"GIT_NO_LAZY_FETCH",nullptr,0);
    std::wstring old_value(old_size,L'\0');
    if(old_size) {
        const auto used=GetEnvironmentVariableW(L"GIT_NO_LAZY_FETCH",
            old_value.data(),old_size);
        old_value.resize(used);
    }
    if(!SetEnvironmentVariableW(L"GIT_NO_LAZY_FETCH",L"1"))
        throw std::runtime_error("Cannot isolate historical Git lookup");
    const auto started=CreateProcessW(git.c_str(),command.data(),nullptr,nullptr,
        TRUE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&child);
    SetEnvironmentVariableW(L"GIT_NO_LAZY_FETCH",old_size?old_value.c_str():nullptr);
    if(!started)throw std::runtime_error("Cannot start historical Git lookup");
    process.value=child.hProcess;
    thread.value=child.hThread;
    writer.close();
    nul.close();
    std::array<char,16384> chunk{};
    std::string payload;
    DWORD length{};
    for(;;) {
        if(!ReadFile(reader.value,chunk.data(),static_cast<DWORD>(chunk.size()),&length,nullptr)) {
            if(GetLastError()!=ERROR_BROKEN_PIPE) {
                TerminateProcess(process.value,1);
                WaitForSingleObject(process.value,5000);
                throw std::runtime_error("Cannot read historical Git object");
            }
            break;
        }
        if(!length)break;
        payload.append(chunk.data(),length);
        if(payload.size()>32u*1024u*1024u) {
            TerminateProcess(process.value,1);
            WaitForSingleObject(process.value,5000);
            throw std::runtime_error("Historical Git object exceeded the QA size limit");
        }
    }
    DWORD exit_code{};
    if(WaitForSingleObject(process.value,15000)!=WAIT_OBJECT_0||
       !GetExitCodeProcess(process.value,&exit_code)||exit_code!=0||payload.empty())
        throw std::runtime_error("Missing pinned Git object; obtain the authentic history before Windows QA");
    // Independently verify the same Git blob frame used by the Python tests.
    using namespace winrt::Windows::Security::Cryptography;
    using namespace winrt::Windows::Security::Cryptography::Core;
    std::string framed="blob "+std::to_string(payload.size());
    framed.push_back('\0');
    framed+=payload;
    const auto* bytes=reinterpret_cast<const std::uint8_t*>(framed.data());
    const auto buffer=CryptographicBuffer::CreateFromByteArray(
        winrt::array_view<const std::uint8_t>{bytes,bytes+framed.size()});
    const auto provider=HashAlgorithmProvider::OpenAlgorithm(HashAlgorithmNames::Sha1());
    const auto digest=winrt::to_string(CryptographicBuffer::EncodeToHexString(
        provider.HashData(buffer)));
    if(digest!=pin)throw std::runtime_error("Historical Git blob differs from its authenticated pin");
    return payload;
}
} // namespace adoption_detail

inline std::string published_adoption_source(const std::filesystem::path& root,
        std::string_view relative) {
    const auto manifest=winrt::Windows::Data::Json::JsonObject::Parse(
        winrt::to_hstring(adoption_detail::read_text(
            root/"scripts/curriculum/published_adoption_pins.json")));
    const auto blobs=manifest.GetNamedObject(L"blobs");
    const auto path=winrt::to_hstring(relative);
    if(!blobs.HasKey(path))
        throw std::runtime_error("No authenticated pre-adoption source: "+std::string(relative));
    return adoption_detail::git_blob(root,winrt::to_string(blobs.GetNamedString(path)));
}
} // namespace lgaed::qa

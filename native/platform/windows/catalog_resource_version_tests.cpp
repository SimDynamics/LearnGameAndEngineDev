// Executable Win32 cache-invalidation check: only a fresh temporary QA file.
#include "platform/windows/catalog_resource_version.hpp"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

int main() {
    const auto temporary = fs::temp_directory_path() /
        (L"lgaed-catalog-change-time-" + std::to_wstring(GetCurrentProcessId()) +
         L"-" + std::to_wstring(GetTickCount64()));
    if(!fs::create_directory(temporary))
        throw std::runtime_error("A QA directory already existed");
    struct Cleanup {
        fs::path path;
        ~Cleanup(){std::error_code error; fs::remove_all(path,error);}
    } cleanup{temporary};
    try {
        const auto target = temporary / L"course-edition-qa.json";
        auto write = [&](std::string_view value) {
            std::ofstream file(target,std::ios::binary|std::ios::trunc);
            file.write(value.data(),static_cast<std::streamsize>(value.size()));
            file.close();
            if(!file)throw std::runtime_error("Cannot write disposable QA file");
        };
        const std::string original = R"({"title":"Unidad nueva"})";
        const std::string changed = R"({"title":"Unidad falsa"})";
        if(original.size()!=changed.size())
            throw std::runtime_error("QA mutation did not preserve the file length");
        write(original);
        const auto stamp=fs::last_write_time(target);
        const auto size=fs::file_size(target);
        const auto change_time=lgaed::windows::catalog_resource_change_time(target);
        Sleep(25);
        write(changed);
        fs::last_write_time(target,stamp);
        if(fs::file_size(target)!=size||fs::last_write_time(target)!=stamp)
            throw std::runtime_error("The test did not reproduce equal size and modification date");
        const auto replaced=lgaed::windows::catalog_resource_change_time(target);
        if(change_time==replaced)
            throw std::runtime_error("A same-size edited file retained its cached revision");
        std::cout<<"PASS: Win32 cache revision changes after equal-length replacement "
                    "and LastWriteTime restoration\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}

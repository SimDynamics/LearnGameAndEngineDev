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
        // Check both the ordinary installed path and the extended Win32
        // spelling used for authentic edition resources beyond MAX_PATH.
        const auto verify_change = [&](const fs::path& target) {
            const auto write = [&](std::string_view value) {
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
            if(lgaed::windows::catalog_resource_change_time(target)!=change_time)
                throw std::runtime_error("An unchanged file did not retain its own revision");
            Sleep(25);
            write(changed);
            fs::last_write_time(target,stamp);
            if(fs::file_size(target)!=size||fs::last_write_time(target)!=stamp)
                throw std::runtime_error("The test did not reproduce equal size and modification date");
            const auto replaced=lgaed::windows::catalog_resource_change_time(target);
            if(change_time==replaced)
                throw std::runtime_error("A same-size edited file retained its cached revision");
        };
        verify_change(temporary / L"course-edition-qa.json");
        const auto extended_root=fs::path(L"\\\\?\\"+fs::absolute(temporary).wstring());
        auto deep=extended_root;
        for(int index=0;index<6;++index)
            deep/=L"long_course_edition_resources_000000000000000000000000000";
        if(deep.wstring().size() <= MAX_PATH)
            throw std::runtime_error("Extended-path fixture does not exceed MAX_PATH");
        fs::create_directories(deep);
        struct LongPathCleanup {
            fs::path path;
            ~LongPathCleanup(){std::error_code error;fs::remove_all(path,error);}
        } long_cleanup{extended_root};
        verify_change(deep / L"lesson_primera.json");
        std::cout<<"PASS: Win32 cache revisions detect equal-length replacement "
                    "after LastWriteTime restoration, including extended paths\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        return 1;
    }
}

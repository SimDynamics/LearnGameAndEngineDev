#pragma once

#include "platform/windows/environment.hpp"
#include "platform/windows/state_store.hpp"

#include <windows.h>
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace lgaed::windows {
namespace narration_file_detail {
inline std::wstring normalized(const std::filesystem::path& path){
    auto value=state::native_io_path(path).lexically_normal().native();
    std::replace(value.begin(),value.end(),L'/',L'\\');
    while(value.size()>7&&value.back()==L'\\')value.pop_back();return value;
}
inline bool same(std::wstring_view left,std::wstring_view right){
    return left.size()==right.size()&&CompareStringOrdinal(left.data(),static_cast<int>(left.size()),
        right.data(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;
}
inline std::filesystem::path final_path(HANDLE handle){
    const auto size=GetFinalPathNameByHandleW(handle,nullptr,0,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!size)throw std::runtime_error("No se pudo obtener la ruta física de la grabación WAV.");
    std::wstring value(size,L'\0');const auto copied=GetFinalPathNameByHandleW(handle,value.data(),size,FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    if(!copied||copied>=size)throw std::runtime_error("La ruta física de la grabación WAV no es válida.");
    value.resize(copied);return value;
}
inline bool isolated_qa_cache(const WindowsEnvironment& environment){
    const auto& qa=environment.options.qa_instance;if(!qa||!is_valid_qa_instance(*qa))return false;
    const auto root=environment.paths.root.lexically_normal();
    return root.is_absolute()&&root.filename().native()==*qa&&root.parent_path().filename()==L"qa"&&
        root.parent_path().parent_path().filename().native()==std::wstring(profile_name(environment.options.profile))&&
        root.parent_path().parent_path().parent_path().filename()==L"LearnGameAndEngineDev"&&
        same(normalized(environment.paths.cache),normalized(root/L"cache"));
}
}

// Pure policy for paths obtained from the already-open recording/cache handles.
// Personal profiles retain exact physical spelling. A redirected QA recording
// must keep its exact relative filename inside that QA's own resolved cache.
inline bool narration_recording_location_allowed(const WindowsEnvironment& environment,
        const std::filesystem::path& requested,const std::filesystem::path& physical_recording,
        const std::filesystem::path& physical_cache){
    if(!requested.is_absolute()||!physical_recording.is_absolute())return false;
    const auto expected=narration_file_detail::normalized(requested),actual=narration_file_detail::normalized(physical_recording);
    if(narration_file_detail::same(expected,actual))return true;
    if(!narration_file_detail::isolated_qa_cache(environment)||physical_cache.empty()||!physical_cache.is_absolute())return false;
    const auto logical_cache=narration_file_detail::normalized(environment.paths.cache);
    if(expected.size()<=logical_cache.size()+1||expected[logical_cache.size()]!=L'\\'||
        !narration_file_detail::same(std::wstring_view(expected).substr(0,logical_cache.size()),logical_cache))return false;
    const auto relative=expected.substr(logical_cache.size()+1);
    if(relative.find(L':')!=relative.npos)return false;
    return narration_file_detail::same(actual,narration_file_detail::normalized(physical_cache/relative));
}

[[nodiscard]] inline std::shared_ptr<void> require_narration_recording_handle(void* raw_handle,const std::filesystem::path& requested,
        const WindowsEnvironment& environment){
    try{
        BY_HANDLE_FILE_INFORMATION file_information{};
        if(GetFileType(static_cast<HANDLE>(raw_handle))!=FILE_TYPE_DISK||
            !GetFileInformationByHandle(static_cast<HANDLE>(raw_handle),&file_information)||
            (file_information.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
            throw std::runtime_error("La grabación WAV no es un archivo normal.");
        state::require_safe_path(requested);
        const auto actual=narration_file_detail::final_path(static_cast<HANDLE>(raw_handle));
        state::require_safe_path(actual);
        if(narration_recording_location_allowed(environment,requested,actual,{}))return {};
        if(!narration_file_detail::isolated_qa_cache(environment))
            throw std::runtime_error("La grabación WAV no coincide con su ruta física autorizada.");
        state::require_safe_path(environment.paths.cache);
        struct Directory {HANDLE value=INVALID_HANDLE_VALUE;~Directory(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
        auto cache=std::make_shared<Directory>();
        cache->value=CreateFileW(state::native_io_path(environment.paths.cache).c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(cache->value==INVALID_HANDLE_VALUE)throw std::runtime_error("No se pudo comprobar la caché de narración de esta QA.");
        const auto physical_cache=narration_file_detail::final_path(cache->value);
        state::require_safe_path(physical_cache);
        if(!narration_recording_location_allowed(environment,requested,actual,physical_cache))
            throw std::runtime_error("La grabación WAV está fuera de la caché autorizada de esta QA.");
        return cache;
    }catch(const std::exception& failure){
        const auto bytes=requested.u8string();
        throw std::runtime_error("No se pudo verificar la grabación WAV "+std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size())+": "+failure.what());
    }
}
} // namespace lgaed::windows

#pragma once
#include "core/lesson_state.hpp"
#include <windows.h>

namespace lgaed::presentation {
inline std::string interface_language(const state::StateData& data){
    const auto selected=state::string_value(data,"native_windows","ui_language","auto");
    if(selected=="es"||selected=="en"||selected=="fr")return selected;
    wchar_t name[LOCALE_NAME_MAX_LENGTH]{};
    if(GetUserDefaultLocaleName(name,LOCALE_NAME_MAX_LENGTH)){
        const std::wstring_view tag(name);
        if(tag.starts_with(L"es"))return "es";
        if(tag.starts_with(L"fr"))return "fr";
    }
    return "en";
}
}

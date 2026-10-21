#pragma once

#include <windows.h>
#include <roapi.h>
#include <winrt/base.h>

#include <stdexcept>

namespace lgaed::windows {

// WinRT activation factories are cached by C++/WinRT. Keep a caller's apartment
// alive for the complete thread rather than uninitializing it between JSON calls.
inline void ensure_winrt_apartment() {
    struct ThreadApartment {
        bool owned = false;
        ThreadApartment() {
            const auto result = RoInitialize(RO_INIT_SINGLETHREADED);
            if (FAILED(result) && result != RPC_E_CHANGED_MODE) throw std::runtime_error("Cannot initialize Windows Runtime apartment");
            owned = SUCCEEDED(result);
        }
        ~ThreadApartment() {
            if (owned) { winrt::clear_factory_cache(); RoUninitialize(); }
        }
    };
    thread_local ThreadApartment apartment;
    (void)apartment;
}

} // namespace lgaed::windows

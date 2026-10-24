#pragma once
#include "presentation/windows/graphics_view.hpp"
#include "presentation/windows/smo_program_session.hpp"
#include "platform/windows/environment.hpp"
#include <functional>
#include <string>

namespace lgaed::presentation {
struct SmoLabSnapshot {
    SmoProgramBackend backend=SmoProgramBackend::compiled_binary32;
    std::string source, sample, status, error;
    bool running=false, benchmarking=false;
    std::size_t vertices=0, faces=0;
    // Compatibility field: elapsed host round trip for the GPU program and
    // readback. It is neither CPU-interpreter time nor a GPU timestamp.
    double interpreter_ms=0;
    SmoSurfaceSnapshot surface;
};
[[nodiscard]] HWND create_smo_view(HWND,const WindowsEnvironment&,std::function<void()> on_back,
                                  GraphicsBackend=GraphicsBackend::automatic,
                                  SmoProgramBackend=SmoProgramBackend::compiled_binary32);
[[nodiscard]] SmoLabSnapshot inspect_smo_lab(HWND,bool read_pixels=true);
}

#pragma once
#include "presentation/windows/smo_program_render.hpp"
#include <functional>

namespace lgaed::presentation {
enum class SmoProgramBackend {compiled_binary32,experimental_f12};
struct SmoProgramUpdate {
    SmoProgramBackend backend=SmoProgramBackend::compiled_binary32;
    std::uint64_t generation=0,frames=0;
    std::uint32_t vertices=0,faces=0;
    bool ready=false;
    double elapsed_ms=0; //host round trip, NOT a GPU timestamp or FPS claim
    SmoComputeInfo device;
    SmoProgramImage image;
    std::string error;
};
// One owner thread for the numerical GPU context, including OpenGL. A source
// change cancels the previous generation and suppresses all stale publication.
// The worker is idle while paused, hidden or during a separate benchmark.
class SmoProgramSession {
public:
    SmoProgramSession(SmoComputeApi,SmoComputeOptions,std::uint32_t width,std::uint32_t height,std::function<void()> changed,
                      SmoProgramBackend backend=SmoProgramBackend::compiled_binary32);
    ~SmoProgramSession();
    SmoProgramSession(const SmoProgramSession&)=delete;
    SmoProgramSession& operator=(const SmoProgramSession&)=delete;
    void execute(std::string);
    void clear();
    void running(bool,bool synchronize=true);
    void visible(bool);
    // Pause first when requesting a stable idle boundary, e.g. for a benchmark.
    // Pending source preparation and an executing frame are never reported idle.
    [[nodiscard]] bool idle()const;
    [[nodiscard]] std::optional<SmoProgramUpdate> take();
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
} //namespace lgaed::presentation

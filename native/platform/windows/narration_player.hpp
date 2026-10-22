#pragma once
#include <filesystem>
#include <memory>

namespace lgaed::windows {
class NarrationPlayer {
public:
    NarrationPlayer();~NarrationPlayer();
    NarrationPlayer(const NarrationPlayer&)=delete;NarrationPlayer& operator=(const NarrationPlayer&)=delete;
    void play(const std::filesystem::path&,double milliseconds,double rate=1);
    void seek(double milliseconds);
    void set_rate(double);
    [[nodiscard]] double position()const;
    [[nodiscard]] bool ended()const;
    [[nodiscard]] bool ready()const;
    [[nodiscard]] bool allocated()const;
    void tick();
    // Close and release the decoder. The controller persists the returned
    // offset; resumption creates a new player for the same cached recording.
    [[nodiscard]] double release();
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}

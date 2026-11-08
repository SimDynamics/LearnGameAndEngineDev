#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace lgaed::windows {
// Only values actually obtained from MediaPlayerFailedEventArgs are present.
// Empty ErrorMessage and a zero HRESULT remain distinct from unavailable data.
struct NarrationPlayerFailure {
    std::optional<std::int32_t> error;
    std::optional<std::int32_t> extended_error_code;
    std::optional<std::string> error_message;
    bool operator==(const NarrationPlayerFailure&)const=default;
};
class NarrationPlayerError : public std::runtime_error {
public:
    explicit NarrationPlayerError(NarrationPlayerFailure);
    [[nodiscard]] const NarrationPlayerFailure& diagnostic()const noexcept{return diagnostic_;}
private:
    NarrationPlayerFailure diagnostic_;
};
class NarrationPlayer {
public:
    NarrationPlayer();~NarrationPlayer();
    NarrationPlayer(const NarrationPlayer&)=delete;NarrationPlayer& operator=(const NarrationPlayer&)=delete;
    void play(const std::filesystem::path&,double milliseconds,double rate=1);
    void seek(double milliseconds);
    void set_rate(double);
    // The actual MediaPlaybackSession rate once playback has started.
    [[nodiscard]] double playback_rate()const;
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

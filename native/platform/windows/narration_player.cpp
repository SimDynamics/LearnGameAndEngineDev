#include "platform/windows/narration_player.hpp"
#include "platform/windows/winrt_apartment.hpp"
#include "core/narration_speed.hpp"
#include <windows.h>
#include <shlwapi.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>
#include <winrt/Windows.Media.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>

namespace lgaed::windows {
using namespace winrt::Windows::Media::Playback;
struct NarrationPlayer::Impl {
    struct Events{std::atomic<bool> opened=false,ended=false;std::atomic<unsigned> error=0;};
    MediaPlayer player{nullptr};std::shared_ptr<Events> events;double requested=0,rate=1;bool started=false,seeking=false;
    void tick(){if(!player)return;if(events->error.load())throw std::runtime_error("Windows no pudo reproducir la grabación de voz.");
        if(events->opened.load()&&!started){player.PlaybackSession().Position(std::chrono::milliseconds(static_cast<std::int64_t>(requested)));
            player.PlaybackSession().PlaybackRate(rate);player.Play();started=true;seeking=requested>0;}
        if(started&&seeking){const auto actual=player.PlaybackSession().Position().count()/10000.0;if(std::abs(actual-requested)<150||events->ended.load())seeking=false;}}
    double position()const{if(!player||!started||seeking)return requested;return std::max(0.0,player.PlaybackSession().Position().count()/10000.0);}
    double release(){const auto offset=position();if(player){player.Close();player=nullptr;}events.reset();requested=offset;started=false;return offset;}
};
NarrationPlayer::NarrationPlayer():impl_(std::make_unique<Impl>()){}
NarrationPlayer::~NarrationPlayer(){try{(void)impl_->release();}catch(...) {}}
void NarrationPlayer::play(const std::filesystem::path& file,double milliseconds,double rate){
    ensure_winrt_apartment();(void)impl_->release();impl_->requested=std::max(0.0,milliseconds);impl_->rate=narration::clamp_playback_rate(rate);
    std::wstring uri(32768,L'\0');DWORD count=static_cast<DWORD>(uri.size());if(FAILED(UrlCreateFromPathW(file.c_str(),uri.data(),&count,0)))throw std::runtime_error("No se pudo abrir la grabación de voz.");uri.resize(count);
    impl_->events=std::make_shared<Impl::Events>();const auto events=impl_->events;
    impl_->player=MediaPlayer();impl_->player.AutoPlay(false);impl_->player.AudioCategory(MediaPlayerAudioCategory::Speech);
    impl_->player.CommandManager().IsEnabled(false);
    impl_->player.MediaOpened([events](const auto&,const auto&){events->opened.store(true);});
    impl_->player.MediaEnded([events](const auto&,const auto&){events->ended.store(true);});
    impl_->player.MediaFailed([events](const auto&,const auto&){events->error.store(1);});
    impl_->player.Source(winrt::Windows::Media::Core::MediaSource::CreateFromUri(winrt::Windows::Foundation::Uri(uri)));
}
void NarrationPlayer::tick(){impl_->tick();}
void NarrationPlayer::seek(double value){impl_->requested=std::max(0.0,value);if(impl_->player&&impl_->started){impl_->events->ended.store(false);impl_->seeking=true;impl_->player.PlaybackSession().Position(std::chrono::milliseconds(static_cast<std::int64_t>(impl_->requested)));}}
void NarrationPlayer::set_rate(double value){impl_->rate=narration::clamp_playback_rate(value);if(impl_->player&&impl_->started)impl_->player.PlaybackSession().PlaybackRate(impl_->rate);}
double NarrationPlayer::playback_rate()const{return impl_->player&&impl_->started?impl_->player.PlaybackSession().PlaybackRate():impl_->rate;}
double NarrationPlayer::position()const{return impl_->position();}
bool NarrationPlayer::ended()const{return impl_->events&&impl_->events->ended.load();}
bool NarrationPlayer::ready()const{return impl_->player&&impl_->started&&!impl_->seeking;}
bool NarrationPlayer::allocated()const{return impl_->player!=nullptr;}
double NarrationPlayer::release(){return impl_->release();}
}

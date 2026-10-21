#pragma once
#include <memory>
#include <string_view>
#include "audio/mixer.hpp"
namespace tp {
// Local presentation only. Missing audio devices quietly disable playback.
class Audio {
public:
    Audio();
    ~Audio();
    void setEnabled(bool enabled);
    void play(bool success);
    void play(SoundEvent event);
    void configure(const AudioSettings& settings);
    void update(const SoundScene& scene, bool audible = true);
    void nextTrack();
    std::string_view musicTrack() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

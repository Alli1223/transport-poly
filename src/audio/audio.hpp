#pragma once
#include <memory>
namespace tp {
// Local presentation only. Missing audio devices quietly disable playback.
class Audio {
public:
    Audio();
    ~Audio();
    void setEnabled(bool enabled);
    void play(bool success);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}

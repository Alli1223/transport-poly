#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include "audio/audio.hpp"
#include <atomic>
#include <algorithm>
#include <cmath>

namespace tp {
struct Audio::Impl {
    ma_device device{};
    bool initialized = false;
    bool attempted = false;
    std::atomic<bool> enabled{false};
    std::atomic<int> pending{0};
    int note = 0;
    unsigned position = 0;
    static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto& self = *static_cast<Impl*>(device->pUserData);
        auto* samples = static_cast<float*>(output);
        const int next = self.pending.exchange(0);
        if (next) { self.note = next; self.position = 0; }
        constexpr unsigned duration = 7200;
        for (ma_uint32 f = 0; f < frames; ++f) {
            float sample = 0;
            if (self.enabled.load(std::memory_order_relaxed) && self.note && self.position < duration) {
                const double t = self.position / 48000.0;
                const double hz = self.note == 1 ? (self.position < duration / 2 ? 523.25 : 659.25) : 196.0;
                const double envelope = std::min(1.0, self.position / 240.0) * (1.0 - self.position / double(duration));
                sample = static_cast<float>(std::sin(6.283185307179586 * hz * t) * envelope * 0.12);
                ++self.position;
            }
            samples[f*2] = samples[f*2+1] = sample;
        }
    }
};
Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() { if (impl_->initialized) ma_device_uninit(&impl_->device); }
void Audio::setEnabled(bool enabled) {
    impl_->enabled = enabled;
    if (!enabled || impl_->attempted) return;
    impl_->attempted = true;
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = 48000;
    config.dataCallback = Impl::callback;
    config.pUserData = impl_.get();
    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) return;
    impl_->initialized = true;
    ma_device_start(&impl_->device);
}
void Audio::play(bool success) {
    if (impl_->enabled && impl_->initialized) impl_->pending = success ? 1 : 2;
}
}

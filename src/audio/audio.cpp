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
#include <mutex>

namespace tp {
struct Audio::Impl {
    ma_device device{};
    bool initialized = false, attempted = false;
    std::mutex mutex;
    AudioSettings settings{}, callbackSettings{};
    SoundScene scene{}, callbackScene{};
    bool audible = true, callbackAudible = true;
    std::atomic<int> pending{0};
    std::atomic<bool> skipTrack{false};
    std::atomic<unsigned> track{0};
    AudioMixer mixer;
    static void callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto& self = *static_cast<Impl*>(device->pUserData);
        // Never wait for the game thread, allocate memory or read simulation state here.
        if (self.mutex.try_lock()) {
            self.callbackSettings = self.settings;
            self.callbackScene = self.scene;
            self.callbackAudible = self.audible;
            self.mutex.unlock();
        }
        auto settings = self.callbackSettings;
        settings.enabled = settings.enabled && self.callbackAudible;
        const int event = self.pending.exchange(0);
        if (self.skipTrack.exchange(false)) self.mixer.nextTrack();
        if (event && settings.enabled) self.mixer.trigger(static_cast<SoundEvent>(event - 1));
        self.mixer.render(static_cast<float*>(output), frames, settings, self.callbackScene);
        self.track = self.mixer.currentTrack();
    }
};
Audio::Audio() : impl_(std::make_unique<Impl>()) {}
Audio::~Audio() { if (impl_->initialized) ma_device_uninit(&impl_->device); }
void Audio::setEnabled(bool enabled) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->settings.enabled = enabled;
    }
    if (!enabled || impl_->attempted) return;
    impl_->attempted = true;
    auto config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate = AudioMixer::sampleRate;
    config.dataCallback = Impl::callback;
    config.pUserData = impl_.get();
    if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) return;
    impl_->initialized = true;
    ma_device_start(&impl_->device);
}
void Audio::configure(const AudioSettings& settings) {
    { std::lock_guard lock(impl_->mutex); impl_->settings = settings; }
    setEnabled(settings.enabled);
}
void Audio::update(const SoundScene& scene, bool audible) {
    std::lock_guard lock(impl_->mutex);
    impl_->scene = scene; impl_->audible = audible;
}
void Audio::play(bool success) { play(success ? SoundEvent::Confirm : SoundEvent::Error); }
void Audio::nextTrack() { impl_->skipTrack = true; }
std::string_view Audio::musicTrack() const {
    constexpr std::string_view titles[] = {"Sidings Shuffle", "Harbour Groove", "Mainline Express"};
    return titles[impl_->track.load() % 3];
}
void Audio::play(SoundEvent event) {
    if (!impl_->initialized) return;
    const int value = static_cast<int>(event) + 1;
    if (event == SoundEvent::Confirm) {
        int empty = 0;
        impl_->pending.compare_exchange_strong(empty, value); // clicks cannot replace a purchase/error cue
    } else impl_->pending = value;
}
}

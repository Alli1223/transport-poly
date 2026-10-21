#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace tp {
enum class SoundKind { Steam, Diesel, Electric, Road, Tram, Ship, RailWheels };
enum class SoundEvent { Confirm, Error, Build, Demolish, Buy, Notify };
struct AudioSettings {
    bool enabled = true;
    float master = 0.75f, music = 0.55f, vehicles = 0.8f, ambience = 0.6f, interface = 0.75f;
};
struct SoundSource {
    std::uint32_t id = 0;
    SoundKind kind = SoundKind::Steam;
    float gain = 0, pan = 0, speed = 0;
};
struct SoundScene {
    std::array<SoundSource, 24> sources{};
    std::size_t count = 0;
    float countryside = 0, water = 0, town = 0, industry = 0, farm = 0, night = 0;
    bool paused = false;
};
// Camera distance is in world tiles. This also makes zooming out naturally quieten vehicles.
float soundAttenuation(double distance);

// Device-independent, allocation-free stereo synthesizer. Only the audio thread owns this state.
// All music and effects are original synthesized arrangements; no external recordings are needed.
class AudioMixer {
public:
    static constexpr unsigned sampleRate = 48000;
    AudioMixer();
    void trigger(SoundEvent event);
    void nextTrack();
    unsigned currentTrack() const { return static_cast<unsigned>((step_ / 512) % 3); }
    void render(float* stereo, std::size_t frames, const AudioSettings& settings, const SoundScene& scene);
private:
    struct Voice {
        int instrument = 0;
        double phase = 0, frequency = 0;
        float age = 0, duration = 0, gain = 0, pan = 0;
        float filter = 0;
    };
    struct Engine {
        std::uint32_t id = 0;
        SoundKind kind = SoundKind::Steam;
        float gain = 0, target = 0, pan = 0, speed = 0;
        double phase = 0, wheel = 0;
        float horn = 0, cooldown = 0;
        float brake = 0, smoothSpeed = 0, lowNoise = 0, midNoise = 0;
        std::uint32_t noiseSeed = 1;
        bool stopped = true;
    };
    std::array<float, 2048> sine_{};
    std::array<Voice, 56> music_{}, effects_{};
    std::array<Engine, 24> engines_{};
    std::array<float, 48000> delayL_{}, delayR_{};
    std::size_t delayPos_ = 0;
    std::uint32_t random_ = 0x83ac91u;
    std::uint64_t step_ = 0, sample_ = 0;
    double nextStep_ = 0;
    float master_ = 0, musicGain_ = 0, vehicleGain_ = 0, ambienceGain_ = 0, interfaceGain_ = 0;
    float filteredNoise_ = 0;
    float sine(double phase) const;
    float noise();
    void note(std::array<Voice, 56>& voices, int instrument, int midi, float duration, float gain, float pan = 0);
    void sequence();
    void voices(std::array<Voice, 56>& bank, float& left, float& right);
};
}

#include "test.hpp"
#ifdef TP_TEST_RENDERING
#include "audio/mixer.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace tp;
namespace {
double energy(const std::vector<float>& samples, int channel = -1) {
    double sum = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) if (channel < 0 || static_cast<int>(i % 2) == channel) sum += samples[i] * samples[i];
    return sum / samples.size();
}
}
TEST_CASE("audio: original music mixes safely and category and master muting work") {
    auto mixer = std::make_unique<AudioMixer>();
    AudioSettings settings;
    SoundScene scene;
    std::vector<float> samples(48000 * 2);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) > .00001);
    CHECK(std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 1; }));
    settings.music = 0;
    mixer->render(samples.data(), 48000, settings, scene); // volume fades
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) < 1e-12);
    for (auto event : {SoundEvent::Confirm, SoundEvent::Error, SoundEvent::Build, SoundEvent::Demolish, SoundEvent::Buy, SoundEvent::Notify}) mixer->trigger(event);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) > .000001);
    settings.enabled = false;
    scene.countryside = scene.water = scene.industry = 1;
    mixer->render(samples.data(), 48000, settings, scene);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) < 1e-12);
}
TEST_CASE("audio: nearby vehicles are louder, stereo positioned and quiet when paused") {
    CHECK(soundAttenuation(6) > soundAttenuation(30) * 3);
    CHECK(soundAttenuation(30) > soundAttenuation(100) * 5);
    CHECK_EQ(soundAttenuation(150), 0.0f);
    AudioSettings settings; settings.music = 0;
    SoundScene scene;
    scene.count = 1;
    scene.sources[0] = {1, SoundKind::Steam, soundAttenuation(6), 1, .7f};
    auto mixer = std::make_unique<AudioMixer>();
    std::vector<float> samples(48000 * 2);
    mixer->render(samples.data(), 48000, settings, scene);
    const double near = energy(samples);
    CHECK(near > .00001);
    CHECK(energy(samples, 1) > energy(samples, 0) * 3);
    scene.sources[0].gain = soundAttenuation(100);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(near > energy(samples) * 50);
    scene.paused = true;
    mixer->render(samples.data(), 48000, settings, scene);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) < 1e-12);
}
TEST_CASE("audio: every engine type and 24 simultaneous sources stay finite") {
    auto mixer = std::make_unique<AudioMixer>();
    AudioSettings settings;
    SoundScene scene;
    scene.count = scene.sources.size();
    for (std::size_t i = 0; i < scene.count; ++i)
        scene.sources[i] = {static_cast<std::uint32_t>(i + 1), static_cast<SoundKind>(i % 7), 1, i % 2 ? -1.0f : 1.0f, 1};
    scene.countryside = scene.water = scene.town = scene.industry = scene.farm = scene.night = 1;
    std::vector<float> samples(48000 * 2);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) > .001);
    CHECK(std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 1; }));
}
TEST_CASE("audio: soundtrack cycles three original arrangements and carriage clatter stops with the train") {
    auto mixer = std::make_unique<AudioMixer>();
    AudioSettings settings;
    SoundScene scene;
    std::vector<float> samples(48000 * 2);
    for (unsigned i = 0; i < 3; ++i) {
        CHECK_EQ(mixer->currentTrack(), i);
        mixer->render(samples.data(), 48000, settings, scene);
        CHECK(energy(samples) > .00001);
        mixer->nextTrack();
    }
    CHECK_EQ(mixer->currentTrack(), 0u);
    mixer = std::make_unique<AudioMixer>();
    settings.music = 0;
    scene.count = 1; scene.sources[0] = {7, SoundKind::RailWheels, 1, 0, .7f};
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) > .00001);
    scene.sources[0].speed = 0;
    mixer->render(samples.data(), 48000, settings, scene);
    mixer->render(samples.data(), 48000, settings, scene);
    CHECK(energy(samples) < 1e-12);
}
#endif

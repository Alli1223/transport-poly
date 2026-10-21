#include "audio/mixer.hpp"
#include <algorithm>
#include <cmath>

namespace tp {
namespace {
constexpr double tau = 6.283185307179586;
constexpr float dt = 1.0f / AudioMixer::sampleRate;
void stereo(float value, float pan, float& left, float& right) {
    left += value * (0.75f - pan * 0.25f);
    right += value * (0.75f + pan * 0.25f);
}
float approach(float current, float target) { return current + (target - current) * 0.0015f; }
}
float soundAttenuation(double distance) {
    if (distance >= 150.0) return 0;
    const double d = std::max(0.0, distance) / 18.0;
    const double fade = std::clamp((150.0 - distance) / 45.0, 0.0, 1.0);
    return static_cast<float>(fade / (1.0 + d * d));
}
AudioMixer::AudioMixer() {
    for (std::size_t i = 0; i < sine_.size(); ++i) sine_[i] = static_cast<float>(std::sin(tau * i / sine_.size()));
}
float AudioMixer::sine(double phase) const {
    phase -= std::floor(phase);
    const double index = phase * sine_.size();
    const auto i = static_cast<std::size_t>(index);
    return sine_[i & 2047] + (sine_[(i + 1) & 2047] - sine_[i & 2047]) * static_cast<float>(index - i);
}
float AudioMixer::noise() {
    random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5;
    return static_cast<float>(random_ & 0xffffu) / 32768.0f - 1.0f;
}
void AudioMixer::note(std::array<Voice, 56>& bank, int instrument, int midi, float duration, float gain, float pan) {
    auto it = std::find_if(bank.begin(), bank.end(), [](const Voice& v) { return v.age >= v.duration; });
    if (it == bank.end()) it = std::min_element(bank.begin(), bank.end(), [](const Voice& a, const Voice& b) {
        return a.duration - a.age < b.duration - b.age;
    });
    *it = {instrument, 0, 440.0 * std::pow(2.0, (midi - 69) / 12.0), 0, duration, gain, pan};
}
void AudioMixer::trigger(SoundEvent event) {
    switch (event) {
    case SoundEvent::Confirm:
        note(effects_, 1, 79, .15f, .16f); note(effects_, 1, 84, .23f, .12f); break;
    case SoundEvent::Error:
        note(effects_, 0, 45, .22f, .23f); note(effects_, 0, 46, .17f, .10f); break;
    case SoundEvent::Build:
        note(effects_, 5, 48, .12f, .24f); note(effects_, 2, 72, .12f, .14f); break;
    case SoundEvent::Demolish:
        note(effects_, 5, 36, .32f, .28f); note(effects_, 4, 48, .22f, .20f); break;
    case SoundEvent::Buy:
        for (int n : {72, 76, 79, 83}) note(effects_, 1, n, .45f, .09f); break;
    case SoundEvent::Notify:
        note(effects_, 1, 76, .55f, .16f); note(effects_, 1, 83, .7f, .09f); break;
    }
}
void AudioMixer::nextTrack() {
    step_ = (step_ / 512 + 1) * 512;
    nextStep_ = static_cast<double>(sample_);
    music_ = {}; delayL_.fill(0); delayR_.fill(0);
    musicGain_ = 0;
}
void AudioMixer::sequence() {
    // Three 32-bar tunes: sidings funk, a mellow harbour shuffle and an upbeat mainline groove.
    // Sixteenth-note swing, seventh/ninth chords, syncopated bass, Rhodes, clav and brass/vibes.
    const int tune = static_cast<int>((step_ / 512) % 3);
    const int bar = static_cast<int>((step_ / 16) % 32), s = static_cast<int>(step_ % 16);
    constexpr int roots[3][8] = {{38, 43, 38, 45, 41, 46, 40, 45}, {41, 46, 43, 48, 41, 46, 40, 45}, {36, 41, 38, 43, 44, 41, 38, 43}};
    const int root = roots[tune][bar % 8];
    const bool major = tune == 1 && (bar % 8 == 0 || bar % 8 == 3 || bar % 8 == 4);
    const bool breakdown = bar >= 16 && bar < 20;
    const float beat = 60.0f / (tune == 0 ? 116.0f : tune == 1 ? 112.0f : 124.0f);
    const int third = major ? 4 : 3;
    if (!breakdown || s == 0 || s == 8) {
        if (s == 0 || s == 6 || s == 10 || (s == 14 && bar % 2)) note(music_, 3, 36, .20f, .34f);
        if (s == 4 || s == 12) { note(music_, 4, 50, .15f, .21f, -.12f); note(music_, 6, 70, .08f, .1f, .12f); }
        if (s % 2 == 0 || (bar % 4 == 3 && s > 11)) note(music_, 6, 70, s == 14 ? .16f : .045f, s % 4 == 2 ? .095f : .065f, .28f);
        if (bar % 8 == 7 && s >= 13) note(music_, 5, 48 - (s - 13) * 3, .18f, .18f, (s - 14) * .4f);
    }
    constexpr int bassPattern[16] = {0,-1,-1,12,-1,-1,7,-1,0,-1,10,-1,-1,7,12,-1};
    if (bassPattern[s] >= 0) note(music_, 0, root + bassPattern[s], beat * (s == 0 ? .7f : .36f), .24f);
    if (s == 15 && bar % 2 == 1 && !breakdown) note(music_, 0, roots[tune][(bar + 1) % 8] + 11, beat * .21f, .13f);
    if (s == 2 || s == 7 || s == 11 || (s == 14 && !breakdown)) {
        for (int interval : {0, third, 7, 10, 14}) note(music_, 1, root + 24 + interval, beat * (breakdown ? 2.8f : .85f), .055f, -.3f);
    }
    if (bar >= 4 && !breakdown && (s == 1 || s == 5 || s == 10 || s == 15))
        note(music_, 2, root + 24 + (s % 3 == 0 ? 7 : third), beat * .3f, .09f, .4f);
    constexpr int melody[3][16] = {{7,10,12,-1,14,12,10,7,3,7,10,-1,7,3,2,-1}, {12,14,16,19,16,-1,14,12,10,7,5,-1,7,10,14,-1}, {0,3,7,10,12,10,7,-1,14,12,10,7,3,-1,2,7}};
    if (bar < 2 && (s == 3 || s == 10)) note(music_, 7, root + 36 + (s == 3 ? 7 : 10), beat * 1.2f, .065f, .2f);
    if (bar >= 2 && !breakdown && (s == 0 || s == 3 || s == 6 || s == 10 || s == 14)) {
        const int index = (bar * 3 + (s * 5 / 16)) % 16, interval = melody[tune][index];
        if (interval >= 0) note(music_, tune == 1 ? 7 : 8, root + 24 + interval, beat * (s == 10 ? 1.15f : .65f), .095f, .08f);
    }
    nextStep_ += sampleRate * beat * (s % 2 == 0 ? .27 : .23);
    ++step_;
}
void AudioMixer::voices(std::array<Voice, 56>& bank, float& left, float& right) {
    for (auto& v : bank) {
        if (v.age >= v.duration) continue;
        const float age = v.age, remaining = v.duration - age;
        const float envelope = std::min(1.0f, age / .004f) * std::min(1.0f, remaining / .035f);
        float sound = 0;
        switch (v.instrument) {
        case 0: sound = (sine(v.phase) + .32f * sine(v.phase * 2) * std::exp(-age * 12) + .15f * sine(v.phase * 3) * std::exp(-age * 24)) * std::exp(-age * 4); break;
        case 1: sound = (sine(v.phase + sine(v.phase * 2) * .38 * std::exp(-age * 8)) + .10f * sine(v.phase * 1.003)) * std::exp(-age * 3.5f); break;
        case 2: sound = (sine(v.phase) + .45f * sine(v.phase * 3) + .18f * sine(v.phase * 5)) * std::exp(-age * 22); break;
        case 3: sound = sine((48 * age + 5 * (1 - std::exp(-age * 35)))) * std::exp(-age * 22); break;
        case 4: sound = (noise() * .65f + sine(v.phase) * .25f) * std::exp(-age * 25); break;
        case 5: sound = (noise() * .25f + sine(v.phase)) * std::exp(-age * 14); break;
        case 6: {
            const float white = noise(); v.filter += (white - v.filter) * .3f;
            sound = (white - v.filter) * std::exp(-age * 45); break;
        }
        case 7: sound = (sine(v.phase + .12 * sine(v.phase * 3)) + .12f * sine(v.phase * 4) * std::exp(-age * 14)) * std::exp(-age * 3) * (.88f + .12f * sine(age * 6)); break;
        case 8: sound = (sine(v.phase) + .32f * sine(v.phase * 2) + .19f * sine(v.phase * 3) + .08f * sine(v.phase * 4)) * std::min(1.0f, age / .025f); break;
        }
        stereo(sound * envelope * v.gain, v.pan, left, right);
        v.age += dt; v.phase += v.frequency / sampleRate * (v.instrument == 8 ? 1.0 + .003 * sine(age * 5.5) : 1.0);
        v.phase -= std::floor(v.phase);
    }
}
void AudioMixer::render(float* output, std::size_t frames, const AudioSettings& settings, const SoundScene& scene) {
    for (auto& e : engines_) e.target = 0;
    for (std::size_t i = 0; i < std::min(scene.count, scene.sources.size()); ++i) {
        const auto& source = scene.sources[i];
        auto slot = std::find_if(engines_.begin(), engines_.end(), [&](const Engine& e) { return e.id == source.id; });
        if (slot == engines_.end()) slot = std::min_element(engines_.begin(), engines_.end(), [](const Engine& a, const Engine& b) { return a.gain + a.target < b.gain + b.target; });
        if (slot->id != source.id) {
            *slot = {}; slot->id = source.id; slot->phase = (source.id % 97) / 97.0;
            slot->stopped = source.speed < .02f;
            slot->noiseSeed = source.id * 1664525u + 1013904223u;
        }
        slot->kind = source.kind; slot->target = scene.paused ? 0 : std::clamp(source.gain, 0.0f, 1.0f);
        if (source.speed < .01f) slot->stopped = true;
        if (!scene.paused && slot->stopped && source.speed > .04f) {
            if (slot->cooldown <= 0 && source.kind != SoundKind::Road && source.kind != SoundKind::RailWheels) { slot->horn = 1.2f; slot->cooldown = 20; }
            slot->stopped = false;
        }
        if (slot->speed > .08f && source.speed < .01f && (source.kind == SoundKind::Steam || source.kind == SoundKind::Diesel || source.kind == SoundKind::Electric)) slot->brake = .65f;
        slot->speed = std::clamp(source.speed, 0.0f, 1.0f); slot->pan = std::clamp(source.pan, -1.0f, 1.0f);
    }
    for (std::size_t f = 0; f < frames; ++f, ++sample_) {
        if (static_cast<double>(sample_) >= nextStep_) sequence();
        musicGain_ = approach(musicGain_, std::clamp(settings.music, 0.0f, 1.0f));
        vehicleGain_ = approach(vehicleGain_, std::clamp(settings.vehicles, 0.0f, 1.0f));
        ambienceGain_ = approach(ambienceGain_, std::clamp(settings.ambience, 0.0f, 1.0f));
        interfaceGain_ = approach(interfaceGain_, std::clamp(settings.interface, 0.0f, 1.0f));
        master_ = approach(master_, settings.enabled ? std::clamp(settings.master, 0.0f, 1.0f) : 0);
        float ml = 0, mr = 0, el = 0, er = 0, vl = 0, vr = 0;
        voices(music_, ml, mr); voices(effects_, el, er);
        // A quiet stereo slapback gives the small band some space without muddying world sounds.
        const std::size_t dl = (delayPos_ + delayL_.size() - 7200) % delayL_.size();
        const std::size_t dr = (delayPos_ + delayR_.size() - 10800) % delayR_.size();
        const float echoL = delayL_[dl], echoR = delayR_[dr];
        delayL_[delayPos_] = ml + echoR * .18f; delayR_[delayPos_] = mr + echoL * .18f;
        delayPos_ = (delayPos_ + 1) % delayL_.size();
        const float hiss = noise();
        filteredNoise_ += (hiss - filteredNoise_) * .018f;
        for (auto& e : engines_) {
            e.cooldown = std::max(0.0f, e.cooldown - dt);
            e.horn = std::max(0.0f, e.horn - dt);
            e.brake = std::max(0.0f, e.brake - dt);
            e.gain = approach(e.gain, e.target);
            if (e.gain < .00001f) continue;
            e.smoothSpeed = approach(e.smoothSpeed, e.speed);
            const float speed = e.smoothSpeed;
            e.noiseSeed ^= e.noiseSeed << 13; e.noiseSeed ^= e.noiseSeed >> 17; e.noiseSeed ^= e.noiseSeed << 5;
            const float white = static_cast<float>(e.noiseSeed & 0xffffu) / 32768.0f - 1.0f;
            e.lowNoise += (white - e.lowNoise) * .025f; e.midNoise += (white - e.midNoise) * .28f;
            const float band = e.midNoise - e.lowNoise;
            e.phase += (e.kind == SoundKind::Electric || e.kind == SoundKind::Tram ? 100 + speed * 700 : 28 + speed * 75) / sampleRate;
            e.phase -= std::floor(e.phase);
            e.wheel += (e.kind == SoundKind::Steam ? .5 + speed * 6 : .8 + speed * 12) / sampleRate; e.wheel -= std::floor(e.wheel);
            const float clack = speed > .02f && e.wheel < .035 ? white * (1.0f - static_cast<float>(e.wheel / .035)) : 0;
            float engine = 0;
            switch (e.kind) {
            case SoundKind::Steam: {
                const float phase = static_cast<float>(std::fmod(e.wheel * 2, 1.0));
                const float chuff = speed > .015f ? std::min(1.0f, phase * 90) * std::exp(-phase * 12) : 0;
                engine = (band * 1.7f + white * .18f) * chuff * (.5f + speed) + e.lowNoise * .4f + white * .015f; break;
            }
            case SoundKind::Diesel: engine = (sine(e.phase) + .35f * sine(e.phase * 2) + .14f * sine(e.phase * 3)) * (.22f + speed * .2f) + e.lowNoise * 1.2f; break;
            case SoundKind::Electric: engine = sine(e.phase) * (.025f + speed * .09f) + sine(e.phase * 2) * speed * .025f + band * speed * .06f; break;
            case SoundKind::Tram: engine = sine(e.phase) * (.025f + speed * .1f) + band * speed * .08f + clack * .28f; break;
            case SoundKind::Road: engine = (sine(e.phase) + .22f * sine(e.phase * 2)) * (.1f + speed * .18f) + band * speed * .22f + e.lowNoise * speed * .6f; break;
            case SoundKind::Ship: engine = (sine(e.phase * .5) + .25f * sine(e.phase)) * .28f + e.lowNoise * (1.1f + speed * .8f) * (.8f + .2f * sine(e.wheel * 4)) + band * speed * .16f; break;
            case SoundKind::RailWheels: {
                const float joint = static_cast<float>(e.wheel);
                const float second = std::fmod(joint + .82f, 1.0f);
                const float impacts = std::exp(-joint * 80) + .75f * std::exp(-second * 80);
                engine = speed * (e.lowNoise * 1.5f + band * .14f + (band * .8f + sine(e.phase * 2.7) * .24f) * impacts);
                break;
            }
            }
            engine += band * std::min(1.0f, e.brake / .08f) * e.brake * .4f;
            if (e.horn > 0) {
                const double age = 1.2 - e.horn, time = static_cast<double>(sample_) / sampleRate;
                const float envelope = static_cast<float>(std::min(1.0, age / .06)) * std::min(1.0f, e.horn / .2f);
                const double hz = e.kind == SoundKind::Steam ? 660 : e.kind == SoundKind::Ship ? 110 : e.kind == SoundKind::Tram ? 880 : 220;
                engine += (sine(time * hz) + .4f * sine(time * hz * 1.25)) * envelope * .3f;
            }
            stereo(engine * e.gain * .36f, e.pan, vl, vr);
        }
        const double seconds = static_cast<double>(sample_) / sampleRate;
        float ambient = filteredNoise_ * (.12f * scene.countryside + .24f * scene.water + .1f * scene.town);
        ambient += (sine(seconds * 47) * sine(seconds * .8) * .018f + filteredNoise_ * .12f) * scene.industry;
        ambient += (sine(seconds * 155 + sine(seconds * 2.1) * 3) * sine(seconds * .6) * .009f + filteredNoise_ * .08f) * scene.town;
        // Bird phrases by day, crickets by night; waves, town bustle and farm bleats nearby.
        const double bird = std::fmod(seconds, 8.7);
        if (bird < .55) ambient += sine(seconds * 1900 + sine(seconds * 8) * 14) * sine(bird * 5) * .035f * scene.countryside * (1 - scene.night);
        ambient += sine(seconds * 3400) * std::pow(std::max(0.0f, sine(seconds * 17)), 8.0f) * .025f * scene.countryside * scene.night;
        ambient += filteredNoise_ * (.65f + .35f * sine(seconds * .18)) * scene.water * .15f;
        ambient += (sine(seconds * 170 + sine(seconds * 5) * 3)) * std::max(0.0f, 1.0f - static_cast<float>(std::fmod(seconds, 13.0)) * 1.6f) * scene.farm * .05f;
        const float left = master_ * ((ml + echoL * .18f) * musicGain_ + el * interfaceGain_ + vl * vehicleGain_ + ambient * ambienceGain_);
        const float right = master_ * ((mr + echoR * .18f) * musicGain_ + er * interfaceGain_ + vr * vehicleGain_ + ambient * ambienceGain_);
        output[f * 2] = left / (1 + std::abs(left)); output[f * 2 + 1] = right / (1 + std::abs(right));
    }
}
}

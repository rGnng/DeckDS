#include "player.hpp"
#include "design_assets.inc"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <numeric>
#include <random>

namespace {
constexpr int sampleRate = 48000;
constexpr double tau = 2 * std::numbers::pi;

std::mt19937& randomSource()
{
    static std::mt19937 engine(std::random_device{}());
    return engine;
}

float note(double t, double pitch)
{
    return static_cast<float>(std::sin(tau * t * pitch));
}

float noise(double t)
{
    auto sample = static_cast<Uint32>(t * sampleRate);
    sample = (sample ^ 61U) ^ (sample >> 16);
    sample *= 9;
    sample ^= sample >> 4;
    sample *= 0x27d4eb2dU;
    sample ^= sample >> 15;
    return static_cast<float>(sample & 65535U) / 32768.f - 1.f;
}
}

Player::Player() : tracks(std::begin(demoTracks), std::end(demoTracks))
{
    for (size_t i = 0; i < tracks.size(); ++i) {
        stars.push_back(tracks[i].starred);
        if (i > 0 && i < 12)
            queue.push_back(static_cast<int>(i));
    }
    for (const auto& list : demoPlaylists) {
        Playlist playlist{std::string(list.name), {}};
        for (const auto id : list.ids) {
            const auto found = std::ranges::find(tracks, id, &Track::id);
            if (found != tracks.end())
                playlist.tracks.push_back(static_cast<int>(found - tracks.begin()));
        }
        playlists.push_back(std::move(playlist));
    }
}

Player::~Player()
{
    if (speakerStream)
        SDL_DestroyAudioStream(speakerStream);
    if (output)
        SDL_DestroyAudioStream(output);
}

const Track* Player::track() const
{
    return (current < 0 || current >= static_cast<int>(tracks.size())) ? nullptr : &tracks[current];
}
float Player::progress() const
{
    const auto* t = track();
    return (t && t->duration > 0) ? static_cast<float>(std::clamp(seconds / t->duration, 0.0, 1.0)) : 0.f;
}
int Player::librarySize() const { return largeLibrary ? static_cast<int>(tracks.size()) : defaultLibrarySize; }

void Player::update(double dt)
{
    if (playing && track()) {
        seconds += dt;
        if (seconds >= track()->duration) {
            if (repeat == 2)
                seek(0);
            else
                next();
        }
    }
    feedAudio();
}

void Player::start(int index)
{
    ++catalogRevision;
    current = index;
    seconds = 0;
    playing = true;
    resetAudio();
}

void Player::stop()
{
    current = -1;
    seconds = 0;
    playing = false;
    resetAudio(tones.empty());
}

void Player::playToggle()
{
    if (!track()) {
        if (!queue.empty()) {
            next();
        } else {
            std::vector<int> order(librarySize());
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), randomSource());
            playFrom(std::move(order));
        }
        return;
    }
    playing = !playing;
    resetAudio(playing && tones.empty());
}

void Player::playFrom(std::vector<int> order, int index)
{
    if (index < 0 || index >= static_cast<int>(order.size()))
        return;
    history.assign(order.begin(), order.begin() + index);
    queue.assign(order.begin() + index + 1, order.end());
    start(order[index]);
}

void Player::next()
{
    if (queue.empty() && repeat == 1) {
        queue = history;
        if (current >= 0)
            queue.push_back(current);
        history.clear();
        current = -1;
    }
    if (current >= 0)
        history.push_back(current);
    if (queue.empty()) {
        stop();
        return;
    }
    const int pick = shuffle ? std::uniform_int_distribution<int>(0, static_cast<int>(queue.size()) - 1)(randomSource()) : 0;
    const int index = queue[pick];
    queue.erase(queue.begin() + pick);
    start(index);
}

void Player::previous()
{
    if (history.empty()) {
        seek(0);
        return;
    }
    if (current >= 0)
        queue.insert(queue.begin(), current);
    const int index = history.back();
    history.pop_back();
    start(index);
}

void Player::makeNext(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(tracks.size()) || trackIndex == current)
        return;
    std::erase(queue, trackIndex);
    queue.insert(queue.begin(), trackIndex);
    ++catalogRevision;
}

void Player::append(const std::vector<int>& ids)
{
    for (const int index : ids)
        if (index >= 0 && index < static_cast<int>(tracks.size()) && index != current &&
            std::ranges::find(queue, index) == queue.end()) {
            queue.push_back(index);
            ++catalogRevision;
        }
}

void Player::seek(float fraction)
{
    if (const auto* t = track()) {
        seconds = std::clamp(fraction, 0.f, .998f) * t->duration;
        resetAudio(tones.empty());
    }
}

void Player::seekSeconds(float delta)
{
    if (const auto* t = track(); t && t->duration > 0)
        seek(static_cast<float>((seconds + delta) / t->duration));
}

void Player::adjustVolume(int delta)
{
    volume = std::clamp(volume + delta, 0, 100);
    mute = false;
}

void Player::toggleStar(int index)
{
    if (index >= 0 && index < static_cast<int>(stars.size())) {
        stars[index] = !stars[index];
        ++catalogRevision;
    }
}

void Player::resetAudio(bool flushStream)
{
    synthesisTime = seconds;
    if (output && flushStream)
        SDL_ClearAudioStream(output);
}

int Player::queuedFrames(SDL_AudioStream* stream) const
{
    if (!stream)
        return 0;
    const int bytes = SDL_GetAudioStreamQueued(stream);
    return bytes > 0 ? bytes / static_cast<int>(2 * sizeof(float)) : 0;
}

void Player::chime(std::string_view cue)
{
    if (!sfx || cue.empty())
        return;
    const Uint64 nowNs = SDL_GetTicksNS();
    SDL_AudioStream* fxTarget = sfxSpeaker ? speakerStream : output;
    const int curQueued = queuedFrames(fxTarget);
    const bool isUp = cue == "release" || cue.ends_with(":up") || cue == "star" || cue == "unstar";
    const bool isDown = cue == "press" || cue.ends_with(":down");
    int extraDelay = 0;
    if (isUp && lastDownNs > 0) {
        constexpr int minStepSamples = sampleRate * 52 / 1000; // 52 ms distinct two-step spacing
        const auto elapsedSamples = static_cast<Sint64>((nowNs - lastDownNs) * sampleRate / 1'000'000'000ULL);
        const Sint64 audibleGap = elapsedSamples + (curQueued - lastDownQueuedFrames);
        if (audibleGap < minStepSamples)
            extraDelay = static_cast<int>(minStepSamples - std::max<Sint64>(0, audibleGap));
    }
    if (isDown) {
        lastDownNs = nowNs;
        lastDownQueuedFrames = curQueued;
    }
    for (const auto& tone : sfxAssets)
        if (tone.cue == cue && tones.size() < 32)
            tones.push_back({&tone, static_cast<int>(tone.duration * sampleRate),
                             -static_cast<int>(tone.delay * sampleRate) - extraDelay});
    feedAudio();
}

void Player::chime(std::string_view cue, bool release)
{
    if (cue.empty())
        chime(release ? "release" : "press");
    else
        chime(std::string(cue) + (release ? ":up" : ":down"));
}

std::array<float, 2> Player::sample(double t) const
{
    if (!playing || !track())
        return {};

    const auto& song = *track();
    const double beat = t * song.bpm / 60.0;
    const double onset = beat - std::floor(beat);
    const double punch = std::exp(-onset * 5.6);
    const int progression[] = {0, 5, 3, 7};
    const int melody[] = {0, 7, 10, 12, 14, 7, 3, 10};
    const auto pitch = [](int midi) { return 440.0 * std::exp2((midi - 69) / 12.0); };
    const int chord = progression[static_cast<int>(beat / 4) % 4];
    const int lead = melody[static_cast<int>(beat * 2) % 8];
    const double leadTime = beat * 2 - std::floor(beat * 2);
    const auto bass = note(t, pitch(song.root + chord - 12)) * punch * 0.24;
    const auto chordLeft = (note(t, pitch(song.root + chord)) + note(t, pitch(song.root + chord + 3))) * 0.09 * punch;
    const auto chordRight = note(t, pitch(song.root + chord + 7)) * 0.16 * punch;
    const double leadPitch = pitch(song.root + lead + 12);
    const auto bell = (note(t, leadPitch) + 0.24 * note(t, leadPitch * 2)
                    + 0.12 * note(t, leadPitch * 3)) * std::exp(-leadTime * 6.0) * 0.16;
    const auto beatTick = note(t, 58 + 72 * std::exp(-onset * 9)) * std::exp(-onset * 19) * 0.19;
    const double shimmer = std::exp(-leadTime * 16.0) * 0.06;
    return {static_cast<float>(bass + chordLeft + bell + beatTick + noise(t) * shimmer),
            static_cast<float>(bass + chordRight + bell * 0.8 + beatTick + noise(t + .031) * shimmer)};
}

void Player::feedAudio()
{
    if (!playing && tones.empty())
        return;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, sampleRate};
    const bool separateSfx = sfxSpeaker && !tones.empty();
    if ((playing || (!sfxSpeaker && !tones.empty())) && !output && !audioUnavailable) {
        output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        audioUnavailable = !output;
        if (output) {
            synthesisTime = seconds;
            SDL_ResumeAudioStreamDevice(output);
        } else {
            SDL_Log("Demo output unavailable: %s", SDL_GetError());
        }
    }
    if (separateSfx && !speakerStream && !speakerUnavailable) {
        SDL_AudioDeviceID chosen = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
        int count = 0;
        if (SDL_AudioDeviceID* devs = SDL_GetAudioPlaybackDevices(&count)) {
            for (int i = 0; i < count; ++i) {
                if (const char* name = SDL_GetAudioDeviceName(devs[i])) {
                    const std::string_view sv(name);
                    if (sv.find("Speaker") != std::string_view::npos || sv.find("speaker") != std::string_view::npos) {
                        chosen = devs[i];
                        break;
                    }
                }
            }
            SDL_free(devs);
        }
        speakerStream = SDL_OpenAudioDeviceStream(chosen, &spec, nullptr, nullptr);
        speakerUnavailable = !speakerStream;
        if (speakerStream)
            SDL_ResumeAudioStreamDevice(speakerStream);
    }

    constexpr int frames = 240;
    constexpr int chunkBytes = frames * 2 * sizeof(float);
    const float sfxGain = sfxVolume == 0 ? 0.45f : sfxVolume == 2 ? 1.75f : 1.0f;

    auto renderToneFrame = [&]() -> float {
        float fx = 0;
        for (auto& tone : tones) {
            const int index = tone.age++;
            if (index < 0 || index >= tone.length)
                continue;
            const auto& sound = *tone.sound;
            const double elapsed = index / static_cast<double>(sampleRate);
            const double ramp = std::log(sound.end / sound.start) / sound.duration;
            const double phase = std::abs(ramp) < 1.e-8 ? sound.start * elapsed
                : sound.start * std::expm1(ramp * elapsed) / ramp;
            const double amplitude = elapsed < .006 ? std::lerp(.0001, sound.gain, elapsed / .006)
                : sound.gain * std::pow(.0001 / sound.gain, (elapsed - .006) / (sound.duration - .006));
            const double wave = sound.triangle ? (2 / std::numbers::pi) * std::asin(std::sin(phase * tau))
                : std::sin(phase * tau);
            fx += static_cast<float>(amplitude * wave);
        }
        return fx * sfxGain;
    };

    if (sfxSpeaker) {
        if (!speakerStream) {
            tones.clear();
        } else if (!tones.empty()) {
            int sfxQueued = SDL_GetAudioStreamQueued(speakerStream);
            while (sfxQueued >= 0 && sfxQueued < chunkBytes * 4 && !tones.empty()) {
                std::array<float, frames * 2> fxData{};
                for (int i = 0; i < frames; ++i) {
                    const float fx = std::clamp(renderToneFrame(), -1.f, 1.f);
                    fxData[i * 2] = fx;
                    fxData[i * 2 + 1] = fx;
                }
                std::erase_if(tones, [](const Tone& tone) { return tone.age >= tone.length; });
                if (!SDL_PutAudioStreamData(speakerStream, fxData.data(), chunkBytes))
                    break;
                sfxQueued += chunkBytes;
            }
        }
        if (!playing || !output)
            return;
        int queued = SDL_GetAudioStreamQueued(output);
        while (queued >= 0 && queued < chunkBytes * 4) {
            std::array<float, frames * 2> data{};
            const float gain = mute ? 0.f : (volume / 100.f) * 0.5f;
            for (int i = 0; i < frames; ++i) {
                const auto signal = sample(synthesisTime);
                data[i * 2] = std::clamp(signal[0] * gain, -1.f, 1.f);
                data[i * 2 + 1] = std::clamp(signal[1] * gain, -1.f, 1.f);
                synthesisTime += 1.0 / sampleRate;
            }
            if (!SDL_PutAudioStreamData(output, data.data(), chunkBytes))
                break;
            queued += chunkBytes;
        }
        return;
    }

    if (!output) {
        tones.clear();
        return;
    }
    int queued = SDL_GetAudioStreamQueued(output);
    while (queued >= 0 && queued < chunkBytes * 4) {
        std::array<float, frames * 2> data{};
        const float gain = mute ? 0.f : (volume / 100.f) * 0.5f;
        for (int i = 0; i < frames; ++i) {
            const auto signal = sample(synthesisTime);
            const float fx = renderToneFrame();
            data[i * 2] = std::clamp(signal[0] * gain + fx, -1.f, 1.f);
            data[i * 2 + 1] = std::clamp(signal[1] * gain + fx, -1.f, 1.f);
            if (playing)
                synthesisTime += 1.0 / sampleRate;
        }
        std::erase_if(tones, [](const Tone& tone) { return tone.age >= tone.length; });
        if (!SDL_PutAudioStreamData(output, data.data(), chunkBytes))
            break;
        queued += chunkBytes;
    }
}

std::string timecode(double seconds)
{
    const int whole = std::max(0, static_cast<int>(seconds));
    char out[24];
    std::snprintf(out, sizeof(out), "%02d:%02d", whole / 60, whole % 60);
    return out;
}

std::array<float, 160> waveform(const Track* track)
{
    std::array<float, 160> display{};
    if (!track)
        return display;
    double seed = track->seed;
    double v = seed * 13.7;
    for (size_t i = 0; i < display.size(); ++i) {
        const double pos = i / static_cast<double>(display.size());
        const double envelope = std::sin(pos * std::numbers::pi) * 0.72 + 0.28;
        const double wave = std::sin(i * 0.27 + seed) * 0.28 + std::cos(i * 0.61 + seed * 2.1) * 0.22
                          + std::sin(i * 1.37 + v) * 0.16;
        v = std::fmod(v * 1.13 + 0.41, 100.0);
        display[i] = static_cast<float>(std::clamp((0.46 + wave) * envelope, 0.12, 0.98));
    }
    return display;
}

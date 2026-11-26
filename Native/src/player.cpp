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
    if (output)
        SDL_DestroyAudioStream(output);
}

const Track* Player::track() const { return current < 0 ? nullptr : &tracks[current]; }
float Player::progress() const { return track() ? static_cast<float>(seconds / track()->duration) : 0; }
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
    resetAudio();
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
    resetAudio();
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
    if (trackIndex < 0 || trackIndex == current)
        return;
    std::erase(queue, trackIndex);
    queue.insert(queue.begin(), trackIndex);
    ++catalogRevision;
}

void Player::append(const std::vector<int>& ids)
{
    for (const int index : ids)
        if (index != current && std::ranges::find(queue, index) == queue.end()) {
            queue.push_back(index);
            ++catalogRevision;
        }
}

void Player::seek(float fraction)
{
    if (track()) {
        seconds = std::clamp(fraction, 0.f, 1.f) * track()->duration;
        resetAudio();
    }
}

void Player::seekSeconds(float delta)
{
    if (track())
        seek(static_cast<float>((seconds + delta) / track()->duration));
}

void Player::adjustVolume(int delta)
{
    volume = std::clamp(volume + delta, 0, 100);
    mute = false;
}

void Player::toggleStar(int index)
{
    if (index >= 0) {
        stars[index] = !stars[index];
        ++catalogRevision;
    }
}

void Player::resetAudio()
{
    synthesisTime = seconds;
    if (output)
        SDL_ClearAudioStream(output);
}

void Player::chime(std::string_view cue)
{
    if (!sfx)
        return;
    for (const auto& tone : sfxAssets)
        if (tone.cue == cue && tones.size() < 24)
            tones.push_back({&tone, static_cast<int>(tone.duration * sampleRate),
                             -static_cast<int>(tone.delay * sampleRate)});
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
    if (!output && !audioUnavailable) {
        const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, sampleRate};
        output = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        audioUnavailable = !output;
        if (output) {
            synthesisTime = seconds;
            SDL_ResumeAudioStreamDevice(output);
        } else {
            SDL_Log("Demo output unavailable: %s", SDL_GetError());
        }
    }
    if (!output) {
        tones.clear();
        return;
    }

    constexpr int frames = 480;
    constexpr int chunkBytes = frames * 2 * sizeof(float);
    int queued = SDL_GetAudioStreamQueued(output);
    while (queued >= 0 && queued < chunkBytes * 3) {
        std::array<float, frames * 2> data{};
        const float gain = mute ? 0.f : (volume / 100.f) * 0.5f;
        for (int i = 0; i < frames; ++i) {
            const auto signal = sample(synthesisTime);
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

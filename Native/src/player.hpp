#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

struct Track {
    std::string_view id, title, artist, album, format, rate, bitrate, added, art;
    int disc, number, year, duration;
    std::array<std::string_view, 3> colors;
    float seed;
    int bpm, root;
    bool starred;
};

struct PlaylistFixture {
    std::string_view name;
    std::vector<std::string_view> ids;
};
struct IconAsset { std::string_view name, svg; };
struct KeyAsset {
    bool symbols;
    int row, column, span;
    std::string_view label, text, action;
    bool function, done;
};
struct PixelAsset { std::string_view name; int size; std::string_view dots; };
struct SfxAsset { std::string_view cue; double start, end, duration, gain, delay; bool triangle; };

struct Playlist {
    std::string name;
    std::vector<int> tracks;
};

class Player {
public:
    Player();
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    std::vector<Track> tracks;
    std::vector<Playlist> playlists;
    std::vector<int> queue, history;
    std::vector<bool> stars;
    int current = 0;
    int volume = 76;
    int repeat = 0;
    int sfxVolume = 1; // 0 = Low, 1 = Mid, 2 = High
    bool sfxSpeaker = false; // false = Current Output, true = Built-in Speaker
    bool shuffle = false, playing = false, mute = false, sfx = true;
    bool largeLibrary = false;
    unsigned catalogRevision = 0;
    double seconds = 0;

    const Track* track() const;
    float progress() const;
    void update(double dt);
    void playToggle();
    void playFrom(std::vector<int> order, int index = 0);
    void next();
    void previous();
    void makeNext(int trackIndex);
    void append(const std::vector<int>& ids);
    void seek(float fraction);
    void seekSeconds(float delta);
    void adjustVolume(int delta);
    void toggleStar(int index);
    void chime(std::string_view cue);
    void chime(std::string_view cue, bool release);
    int librarySize() const;
    std::array<float, 2> sample(double time) const;

private:
    SDL_AudioStream* output = nullptr;
    SDL_AudioStream* speakerStream = nullptr;
    double synthesisTime = 0;
    struct Tone { const SfxAsset* sound; int length, age; };
    std::vector<Tone> tones;
    Uint64 lastDownNs = 0;
    int lastDownQueuedFrames = 0;
    bool audioUnavailable = false, speakerUnavailable = false;
    int queuedFrames(SDL_AudioStream* stream) const;
    void feedAudio();
    void start(int index);
    void stop();
    void resetAudio(bool flushStream = true);
};

std::string timecode(double seconds);
std::array<float, 160> waveform(const Track* track);

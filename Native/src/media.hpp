#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

struct MediaFile {
    std::string path, id, title, artist, album, albumArtist, format, rate, bitrate, modified, added, visualKey;
    int disc = 1, number = 0, year = 0, duration = 0, channels = 0, bitDepth = 0;
    uintmax_t bytes = 0;
    int64_t modificationStamp = 0;
    bool lossless = false, missing = false;
};

struct MediaLibrary {
    std::vector<MediaFile> files;
    std::vector<std::string> missingPaths;
    size_t skipped = 0, reused = 0, probed = 0, missing = 0;
};

enum class MusicScanMode { Quick, Full };

struct MediaScanProgress {
    std::atomic<size_t> checked{0}, total{0};
};

bool mediaSupported();
std::string mediaKey(std::string_view value);
std::optional<MediaLibrary> loadMusicIndex(const std::filesystem::path& directory, const std::filesystem::path& database);
MediaLibrary scanMusic(const std::filesystem::path& directory, MediaScanProgress& progress, std::stop_token stop,
                       const std::filesystem::path& database = {});
std::vector<std::string> removeMissingMusic(const std::filesystem::path& directory, std::span<const std::string> paths,
                                            std::stop_token stop, const std::filesystem::path& database);

using MediaWaveform = std::array<float, 160>;
struct WaveformSource { std::string_view key, path; };
std::vector<uint8_t> readEmbeddedArtwork(std::string_view path, size_t maxBytes, std::stop_token stop);
std::optional<std::filesystem::path> cachedArtwork(std::string_view key, std::string_view path);
std::optional<MediaWaveform> cachedWaveform(std::string_view key, std::string_view path);
void prefetchWaveforms(WaveformSource next, WaveformSource previous);

class MediaDecoder {
public:
    MediaDecoder(std::string_view path, double start, int outputRate, std::stop_token stop = {});
    ~MediaDecoder();
    bool valid() const;
    int sampleRate() const;
    int read(std::span<float> stereo);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

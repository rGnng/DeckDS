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
#include <unordered_map>
#include <vector>

struct MediaFile {
    std::string path, id, title, artist, album, albumArtist, format, rate, bitrate, modified, added, visualKey;
    int disc = 1, number = 0, year = 0, duration = 0, channels = 0, bitDepth = 0;
    uintmax_t bytes = 0;
    int64_t modificationStamp = 0;
    bool lossless = false, missing = false;
    std::string libraryId, albumId, artistId, albumArtistId;
    std::string identityArtist, identityAlbum, identityAlbumArtist;
    bool disabled = false;
};

struct MusicFolder {
    std::string id, root, originalRoot, identifier, idNamespace;
    std::string syncUrl{};
    bool enabled = true;
    bool operator==(const MusicFolder&) const = default;
};

struct MusicPath {
    std::string library, relative;
    bool operator==(const MusicPath&) const = default;
};

using MusicIdAliases = std::unordered_map<std::string, std::string>;

struct MediaLibrary {
    std::vector<MediaFile> files;
    std::vector<MusicPath> missingPaths;
    size_t skipped = 0, reused = 0, probed = 0, missing = 0;
    size_t unavailable = 0;
};

struct MusicLibrarySettings {
    std::string identifier, syncUrl;
    bool operator==(const MusicLibrarySettings&) const = default;
};

struct MusicRelocation {
    std::string library, destination;
    std::vector<MusicFolder> folders;
    std::vector<std::string> missing;
    std::optional<MusicLibrarySettings> settings;
    size_t matched = 0;
    bool operator==(const MusicRelocation&) const = default;
};

enum class MusicScanMode { Quick, Full };

struct MediaScanProgress {
    std::atomic<size_t> checked{0}, total{0};
};

bool mediaSupported();
std::string mediaKey(std::string_view value);
bool initializeMusicIndex(const std::filesystem::path& database, const std::filesystem::path& previousRoot,
                          std::string_view previousSyncUrl = {});
std::vector<MusicFolder> musicFolders(const std::filesystem::path& database);
MediaLibrary loadMusicIndex(const std::filesystem::path& database);
MusicIdAliases musicIdAliases(const std::filesystem::path& database);
void addMusicFolder(const std::filesystem::path& database, const std::filesystem::path& root, std::string_view identifier,
                    std::string_view previousSyncUrl = {}, std::string_view syncUrl = {});
void renameMusicFolder(const std::filesystem::path& database, std::string_view library, std::string_view identifier,
                       std::optional<std::string_view> syncUrl = std::nullopt);
void setMusicSyncUrl(const std::filesystem::path& database, std::string_view library, std::string_view url);
void setMusicFolderEnabled(const std::filesystem::path& database, std::string_view library, bool enabled);
void deleteMusicFolder(const std::filesystem::path& database, std::string_view library,
                       const std::filesystem::path& backup, std::stop_token stop);
MediaLibrary scanMusic(MediaScanProgress& progress, std::stop_token stop, const std::filesystem::path& database);
std::vector<std::string> removeMissingMusic(std::span<const MusicPath> paths, std::stop_token stop,
                                            const std::filesystem::path& database);
MusicRelocation previewMusicRelocation(const std::filesystem::path& database, std::string_view library,
                                       std::string_view destination, std::stop_token stop,
                                       const std::optional<MusicLibrarySettings>& settings = std::nullopt);
void relocateMusicLibrary(const std::filesystem::path& database, const MusicRelocation& plan,
                          const std::filesystem::path& backup, std::stop_token stop);
void rekeyMusicIndex(const std::filesystem::path& database, const std::filesystem::path& backup,
                     MediaScanProgress& progress, std::stop_token stop);

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

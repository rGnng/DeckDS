#pragma once

#include "media.hpp"
#include "wifi.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class WebSetting { Theme, Brightness, Clock, Sfx, Lid, Accent, SfxVolume, SfxOutput, Count };
using WebSettings = std::array<int, static_cast<size_t>(WebSetting::Count)>;

inline constexpr size_t webUrlLimit = 2048;
bool validOtaUrl(std::string_view url);
bool validLibrarySyncUrl(std::string_view url);

struct WebConfigState {
    struct Scan {
        size_t checked, total;
        bool full;
    };
    struct Playback {
        std::string trackId;
        std::string libraryId, albumId, artistId, albumArtistId;
        int64_t positionMs = 0, durationMs = 0;
        int volume = 0;
        size_t queued = 0;
        bool available = false, playing = false, muted = false;
        bool canPlay = false, canPrevious = false, canNext = false;
    };
    struct UrlSetting {
        std::string url, error;
        uint64_t revision = 0;
    };
    struct Wifi {
        std::vector<WifiNetwork> networks{};
        std::string message;
        bool supported = false, busy = false, connected = false, error = false;
        std::optional<WifiConnection> accessPoint{};
    };
    struct Bluetooth {
        struct Device { std::string id, name; bool paired, connected; };
        std::vector<Device> devices;
        std::string_view message;
        bool supported = false, busy = false, scanning = false, demo = false, error = false;
    };
    struct LibraryConfig {
        struct Preview {
            std::string destination;
            uint64_t token = 0;
            size_t matched = 0, missing = 0;
            std::vector<std::string> unmatched;
        };
        std::vector<MusicFolder> folders;
        std::optional<Preview> preview;
        std::optional<Scan> rekey;
        std::string message, error, backup;
        uint64_t revision = 0;
        bool supported = false, busy = false;
    };
    std::string folder, track, artist, message;
    size_t tracks = 0, missing = 0;
    uint64_t review = 0;
    bool canScan = false, removing = false;
    std::optional<Scan> scan;
    WebSettings settings{};
    std::array<std::string_view, 4> accents{};
    Playback playback{};
    UrlSetting ota{};
    Wifi wifi{};
    Bluetooth bluetooth{};
    LibraryConfig libraryConfig{};
};

struct WebDebugState {
    struct Display {
        std::string name;
        int width = 0, height = 0;
        float refreshHz = 0;
    };
    struct Window {
        std::string panel, renderer;
        int width = 0, height = 0;
    };
    struct Audio {
        std::string driver, codec, sourceRate;
        int decoderHz = 0, deviceHz = 0, deviceChannels = 0, deviceBufferFrames = 0;
        bool playing = false;
    };
    struct ControllerEvent {
        std::string button;
        uint64_t timeMs = 0;
        bool pressed = false;
    };
    struct Log {
        uint64_t timeMs;
        int category, priority;
        std::string message;
    };
    std::string version, sdlVersion, mode, platform, machine, kernel, model, videoDriver, powerState, libraryStatus;
    uint64_t uptimeMs = 0, peakRssBytes = 0;
    double frameMs = 0;
    std::optional<double> loadAverage;
    std::optional<int> batteryPercent;
    int logicalCpus = 0, ramMiB = 0, touchDevices = 0;
    size_t tracks = 0, queue = 0, missing = 0;
    bool wifiConnected = false, bluetoothConnected = false, lidClosed = false, displaysOff = false;
    std::vector<Display> displays;
    std::vector<Window> windows;
    Audio audio;
    std::vector<std::string> gamepads;
    std::optional<ControllerEvent> lastControllerEvent;
    std::optional<WebConfigState::Scan> scan;
    std::vector<Log> logs;
};

struct WebPlaybackCommand {
    enum class Action { Previous, Play, Pause, Next, Seek, Volume, Mute } action = Action::Play;
    int64_t value = 0;
    std::string trackId{};
};

struct WebConfigCommand {
    enum class Type {
        QuickScan, FullScan, KeepMissing, RemoveMissing, Setting, Playback, OtaUrl,
        WifiScan, WifiConnect, WifiConnectManual, BluetoothRefreshPaired, BluetoothScan, BluetoothConnect, BluetoothForget,
        LibraryAdd, LibraryRename, LibrarySyncUrl, LibrarySetEnabled, LibraryDelete, LibraryRekey, LibraryPreviewRelocate, LibraryApply
    } type;
    uint64_t review = 0;
    WebSetting setting = WebSetting::Theme;
    int value = 0;
    WebPlaybackCommand playback{};
    std::string url{};
    int theme = 0;
    std::string target{}, wifiPassword{};
    std::optional<MusicLibrarySettings> librarySettings{};
};

class WebConfig {
public:
    WebConfig();
    ~WebConfig();
    static bool supported();
    bool running() const;
    bool start(const std::filesystem::path& preferences, const WebConfigState& initial);
    void stop();
    std::string error() const;
    void publish(const WebConfigState& state);
    void publishDebug(const WebDebugState& state);
    void publishLibraryReport(uint64_t token, std::string report);
    std::vector<WebConfigCommand> takeCommands();

private:
    struct Server;
    std::unique_ptr<Server> server;
};

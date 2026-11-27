#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class WebSetting { Theme, Brightness, Clock, Sfx, Lid, Count };
using WebSettings = std::array<int, static_cast<size_t>(WebSetting::Count)>;

struct WebConfigState {
    struct Scan {
        size_t checked, total;
        bool full;
    };
    std::string folder, track, artist, message;
    size_t tracks = 0, missing = 0;
    uint64_t review = 0;
    bool canScan = false, removing = false;
    std::optional<Scan> scan;
    WebSettings settings{};
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

struct WebConfigCommand {
    enum class Type { QuickScan, FullScan, KeepMissing, RemoveMissing, Setting } type;
    uint64_t review = 0;
    WebSetting setting = WebSetting::Theme;
    int value = 0;
};

class WebConfig {
public:
    WebConfig();
    ~WebConfig();
    static bool supported();
    bool running() const;
    bool start();
    void stop();
    std::string address() const;
    std::string error() const;
    void publish(const WebConfigState& state);
    void publishDebug(const WebDebugState& state);
    std::vector<WebConfigCommand> takeCommands();

private:
    struct Server;
    std::unique_ptr<Server> server;
};

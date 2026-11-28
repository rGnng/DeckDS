#include "studio.hpp"
#include "wifi.hpp"

#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <future>
#include <iterator>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/resource.h>
#include <sys/utsname.h>
#endif
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace {
std::string textOrEmpty(const char* text)
{
    return text ? text : "";
}

class DebugLog {
public:
    explicit DebugLog(bool enabled) : enabled(enabled)
    {
        if (enabled) {
            SDL_GetLogOutputFunction(&previous, &userdata);
            SDL_SetLogOutputFunction(record, this);
        }
    }

    ~DebugLog()
    {
        if (enabled)
            SDL_SetLogOutputFunction(previous, userdata);
    }

    std::vector<WebDebugState::Log> recent()
    {
        const std::lock_guard lock(mutex);
        return {entries.begin(), entries.end()};
    }

private:
    static void SDLCALL record(void* context, int category, SDL_LogPriority priority, const char* message)
    {
        auto& log = *static_cast<DebugLog*>(context);
        {
            const std::lock_guard lock(log.mutex);
            constexpr size_t limit = 96;
            if (log.entries.size() == limit)
                log.entries.pop_front();
            log.entries.push_back({SDL_GetTicks(), category, priority,
                                   std::string(std::string_view(message).substr(0, 768))});
        }
        if (log.previous)
            log.previous(log.userdata, category, priority, message);
    }

    bool enabled;
    SDL_LogOutputFunction previous = nullptr;
    void* userdata = nullptr;
    std::mutex mutex;
    std::deque<WebDebugState::Log> entries;
};

void collectSystemDebug(WebDebugState& state)
{
    state.platform = textOrEmpty(SDL_GetPlatform());
    state.logicalCpus = SDL_GetNumLogicalCPUCores();
    state.ramMiB = SDL_GetSystemRAM();
#if defined(__APPLE__) || defined(__linux__)
    utsname host{};
    if (uname(&host) == 0) {
        state.machine = host.machine;
        state.kernel = host.release;
    }
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        state.peakRssBytes = usage.ru_maxrss;
    double load = 0;
    if (getloadavg(&load, 1) == 1)
        state.loadAverage = load;
#endif
#if defined(__APPLE__)
    char model[256]{};
    size_t length = sizeof(model);
    if (sysctlbyname("hw.model", model, &length, nullptr, 0) == 0)
        state.model = model;
#elif defined(__linux__)
    state.peakRssBytes *= 1024;
    std::ifstream model("/proc/device-tree/model");
    char name[256]{};
    model.getline(name, sizeof(name), '\0');
    state.model = name;
#endif
}

std::string_view debugPowerState(SDL_PowerState state)
{
    switch (state) {
    case SDL_POWERSTATE_ON_BATTERY: return "onBattery";
    case SDL_POWERSTATE_NO_BATTERY: return "noBattery";
    case SDL_POWERSTATE_CHARGING: return "charging";
    case SDL_POWERSTATE_CHARGED: return "charged";
    case SDL_POWERSTATE_ERROR: return "error";
    case SDL_POWERSTATE_UNKNOWN: return "unknown";
    }
    return "unknown";
}

class Window {
public:
    enum class Pane { Both, Upper, Lower };

    Window(const std::filesystem::path& fontPath, Pane pane, SDL_DisplayID display, bool fullscreen)
        : kind(pane)
    {
        SDL_Rect displayRect{};
        if (display)
            SDL_GetDisplayUsableBounds(display, &displayRect);
        const int width = pane == Pane::Both ? (displayRect.w > 0 ? std::clamp(displayRect.w - 48, 600, 1080) : 1024) : 768;
        const int height = pane == Pane::Both ? (displayRect.h > 0 ? std::clamp(displayRect.h - 44, 580, 980) : 940) : 576;
        const char* title = pane == Pane::Both ? "DeckDS — SDL3 preview" :
                            pane == Pane::Upper ? "DeckDS — Top" : "DeckDS — Bottom";
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, !fullscreen);
        if (display) {
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
            SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
        }
        window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!window)
            return;
        SDL_SetWindowMinimumSize(window, pane == Pane::Both ? 600 : 340, pane == Pane::Both ? 570 : 252);
        if (fullscreen && !SDL_SetWindowFullscreen(window, true))
            return;
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer)
            return;
        if (pane != Pane::Upper)
            SDL_SetRenderVSync(renderer, 1);
        draw = std::make_unique<Draw>(renderer, fontPath);
        if (kind != Pane::Lower) {
            top = surface();
            fade = surface();
            if (fade)
                SDL_SetTextureBlendMode(fade, SDL_BLENDMODE_BLEND);
        }
        if (kind != Pane::Upper)
            bottom = surface();
    }

    ~Window()
    {
        draw.reset();
        for (auto* texture : {top, bottom, fade})
            if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }

    bool ready() const
    {
        return renderer && (kind == Pane::Lower || (top && fade)) && (kind == Pane::Upper || bottom);
    }

    WebDebugState::Window debugState() const
    {
        WebDebugState::Window result;
        result.panel = kind == Pane::Both ? "both" : kind == Pane::Upper ? "upper" : "lower";
        result.renderer = textOrEmpty(SDL_GetRendererName(renderer));
        SDL_GetRenderOutputSize(renderer, &result.width, &result.height);
        return result;
    }

    SDL_Window* window = nullptr;
    Pane kind;
    Rect upper{}, lower{};
    Studio studio;
    bool pointerDown = false;

    void render(DeckUi& ui)
    {
        int width = 0, height = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(window, &width, &height);
        SDL_GetRenderOutputSize(renderer, &pixelWidth, &pixelHeight);
        if (width <= 0 || height <= 0)
            return;
        const float pixelRatio = static_cast<float>(pixelWidth) / width;
        if (top) {
            SDL_SetRenderTarget(renderer, top);
            SDL_SetRenderScale(renderer, 1.f, 1.f);
            ui.upper(*draw, fade);
        }
        if (bottom) {
            SDL_SetRenderTarget(renderer, bottom);
            SDL_SetRenderScale(renderer, 1.f, 1.f);
            ui.lower(*draw);
        }
        SDL_SetRenderTarget(renderer, nullptr);
        SDL_SetRenderScale(renderer, pixelRatio, static_cast<float>(pixelHeight) / height);
        draw->theme = Theme::Obsidian;
        draw->clear(color(0x0c0d11));
        if (kind == Pane::Both)
            studio.render(*draw, ui, window, top, bottom, static_cast<float>(width), static_cast<float>(height),
                          pixelRatio, static_cast<float>(pixelHeight) / height);
        else {
            const float scale = std::min(width / 1024.f, height / 768.f);
            const Rect placement{std::round((width - 1024 * scale) / 2), std::round((height - 768 * scale) / 2),
                                 1024 * scale, 768 * scale};
            if (kind == Pane::Lower) lower = placement;
            else upper = placement;
            auto* panel = bottom ? bottom : top;
            const float backingScale = scale * pixelRatio;
            const bool integerScale = std::abs(backingScale - std::round(backingScale)) < .01f && backingScale >= 1.f;
            SDL_SetTextureScaleMode(panel, integerScale ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
            draw->texture(panel, placement);
        }
        SDL_RenderPresent(renderer);
    }

private:
    SDL_Renderer* renderer = nullptr;
    SDL_Texture *top = nullptr, *bottom = nullptr, *fade = nullptr;
    std::unique_ptr<Draw> draw;

    SDL_Texture* surface()
    {
        auto* value = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1024, 768);
        if (value)
            SDL_SetTextureBlendMode(value, SDL_BLENDMODE_NONE);
        return value;
    }


};

SDL_Keycode controllerKey(SDL_Gamepad* pad, Uint8 button)
{
    if (button == SDL_GAMEPAD_BUTTON_SOUTH || button == SDL_GAMEPAD_BUTTON_EAST ||
        button == SDL_GAMEPAD_BUTTON_WEST || button == SDL_GAMEPAD_BUTTON_NORTH) {
        const auto label = pad ? SDL_GetGamepadButtonLabel(pad, static_cast<SDL_GamepadButton>(button))
                               : SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN;
        switch (label) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return SDLK_RETURN;
        case SDL_GAMEPAD_BUTTON_LABEL_B: return SDLK_B;
        case SDL_GAMEPAD_BUTTON_LABEL_X: return SDLK_X;
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return SDLK_Y;
        default: break;
        }
        // Fallback: RG DS Plus / Nintendo physical face layout (East=A, South=B, North=X, West=Y)
        switch (button) {
        case SDL_GAMEPAD_BUTTON_EAST: return SDLK_RETURN;
        case SDL_GAMEPAD_BUTTON_SOUTH: return SDLK_B;
        case SDL_GAMEPAD_BUTTON_NORTH: return SDLK_X;
        case SDL_GAMEPAD_BUTTON_WEST: return SDLK_Y;
        default: break;
        }
    }
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return SDLK_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return SDLK_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return SDLK_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return SDLK_RIGHT;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return SDLK_Q;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return SDLK_E;
    case SDL_GAMEPAD_BUTTON_START: return SDLK_SPACE;
    case SDL_GAMEPAD_BUTTON_BACK: return SDLK_S;
    case SDL_GAMEPAD_BUTTON_MISC1: return SDLK_G;
    case SDL_GAMEPAD_BUTTON_MISC2: return SDLK_H;
    default: return SDLK_UNKNOWN;
    }
}

std::filesystem::path previewPreferences()
{
    const std::unique_ptr<char, decltype(&SDL_free)> directory(SDL_GetPrefPath("DeckDS", "DeckDS"), SDL_free);
    if (!directory) {
        SDL_Log("Cannot find the settings directory: %s", SDL_GetError());
        return {};
    }
    return directory.get();
}

std::filesystem::path availableMusicPath(const std::filesystem::path& path)
{
    std::error_code error;
    const auto resolved = std::filesystem::canonical(path, error);
    return !error && std::filesystem::is_directory(resolved, error) ? resolved : std::filesystem::path{};
}

struct FolderChoice {
    std::filesystem::path path;
    std::string error;
};

struct FolderPicker {
    std::mutex mutex;
    std::optional<FolderChoice> choice;
};

class WebWifiSetup {
public:
    const WebConfigState::Wifi& state() const { return status; }

    void refreshLink(bool connected)
    {
        fallbackConnected = connected;
        if (!status.supported) {
            status.connected = connected;
        } else if (!status.busy && !linkTask.valid()) {
            linkTask = std::async(std::launch::async, activeWifi);
        }
    }

    void start(WebConfigCommand command)
    {
        if (!status.supported || status.busy)
            return;
        const bool scan = command.type == WebConfigCommand::Type::WifiScan;
        if (!scan) {
            WifiNetwork network;
            if (command.type == WebConfigCommand::Type::WifiConnectManual) {
                network.name = command.target;
                network.ssidBytes = std::move(command.target);
                network.security = command.wifiPassword.empty() ? WifiNetwork::Security::Open : WifiNetwork::Security::Personal;
                network.manual = true;
            } else {
                const auto selected = std::ranges::find(status.networks, command.target, &WifiNetwork::id);
                if (selected == status.networks.end() || selected->connected) {
                    status.message = "This network is no longer available to join. Scan again.";
                    status.error = true;
                    return;
                }
                network = *selected;
            }
            connecting = network;
            status.message = network.manual ? "Connecting to Wi-Fi…" : "Connecting to " + network.name + "…";
            task = std::async(std::launch::async,
                [network = std::move(network), password = std::move(command.wifiPassword)] { return connectWifi(network, password); });
        } else {
            status.message = "Scanning for Wi-Fi networks…";
            task = std::async(std::launch::async, scanWifi);
        }
        status.busy = true;
        status.error = false;
    }

    void update()
    {
        if (linkTask.valid() && linkTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto link = linkTask.get();
            if (!status.busy) {
                status.connected = link.has_value() || fallbackConnected;
                status.accessPoint = std::move(link);
                for (auto& network : status.networks) {
                    if (status.accessPoint && !status.accessPoint->id.empty())
                        network.connected = network.id == status.accessPoint->id;
                    else if (!status.connected)
                        network.connected = false;
                }
            }
        }
        if (!task.valid() || task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        auto result = task.get();
        if (!connecting) {
            status.networks = std::move(result.networks);
        } else if (!result.error) {
            status.connected = true;
            for (auto& network : status.networks) {
                network.connected = network.id == connecting->id;
            }
            status.accessPoint = WifiConnection{connecting->name, connecting->id,
                connecting->manual ? std::nullopt : std::optional(connecting->strength)};
        }
        connecting.reset();
        status.message = std::move(result.message);
        status.error = result.error;
        status.busy = false;
        if (!result.error)
            refreshLink(status.connected);
    }

private:
    WebConfigState::Wifi status{.message = wifiSetupSupported() ? "" : "Wi-Fi setup is unavailable.",
                                .supported = wifiSetupSupported()};
    std::optional<WifiNetwork> connecting;
    bool fallbackConnected = false;
    std::future<std::optional<WifiConnection>> linkTask;
    std::future<WifiResult> task;
};

void SDLCALL musicFolderChosen(void* data, const char* const* files, int)
{
    const std::unique_ptr<std::shared_ptr<FolderPicker>> picker(static_cast<std::shared_ptr<FolderPicker>*>(data));
    FolderChoice choice;
    if (!files)
        choice.error = SDL_GetError();
    else if (*files)
        choice.path = *files;
    const std::lock_guard lock((*picker)->mutex);
    (*picker)->choice = std::move(choice);
}

void loadClockSettings(const std::filesystem::path& path, DeckUi& app)
{
    std::ifstream settings(path);
    int hours = 0, offset = 0;
    if (!(settings >> hours >> offset) || (hours != 12 && hours != 24) || offset < 0 || offset >= 24 * 60)
        return;
    settings >> std::ws;
    if (settings.eof()) {
        app.clock24h = hours == 24;
        app.clockOffsetMinutes = offset;
    }
}

bool savePreviewSettings(const std::filesystem::path& path, std::string_view settings)
{
    auto temporary = path;
    temporary += "." + std::to_string(SDL_rand_bits()) + ".tmp";
    std::error_code error;
    if (SDL_SaveFile(temporary.string().c_str(), settings.data(), settings.size())) {
        std::filesystem::rename(temporary, path, error);
        if (!error)
            return true;
        SDL_Log("Cannot replace preview settings: %s", error.message().c_str());
    } else {
        SDL_Log("Cannot write preview settings: %s", SDL_GetError());
    }
    std::filesystem::remove(temporary, error);
    return false;
}

WebConfigState::UrlSetting loadWebUrl(const std::filesystem::path& path, bool (*valid)(std::string_view))
{
    WebConfigState::UrlSetting setting;
    if (std::ifstream input(path, std::ios::binary); input) {
        std::string url(webUrlLimit + 1, '\0');
        input.read(url.data(), url.size());
        url.resize(input.gcount());
        if (!input.bad() && valid(url))
            setting.url = std::move(url);
    }
    return setting;
}

void saveWebUrl(WebConfigState::UrlSetting& setting, const std::filesystem::path& path,
                const std::string& url, std::string_view error)
{
    ++setting.revision;
    setting.error.clear();
    if (url == setting.url)
        return;
    if (!path.empty() && savePreviewSettings(path, url))
        setting.url = url;
    else
        setting.error = error;
}

struct LibraryConfigResult {
    std::vector<MusicFolder> folders;
    std::optional<MusicRelocation> relocation{};
    std::optional<MediaLibrary> catalog{};
    MusicIdAliases aliases{};
    std::string backup, message;
    bool rescan = false, deleted = false;
};

int run(bool separate, bool kiosk, bool longMetadata, bool bluetoothDemo, std::filesystem::path musicPath)
{
    const char* base = SDL_GetBasePath();
    if (!base) {
        SDL_Log("Cannot find app resources: %s", SDL_GetError());
        return 1;
    }
    const auto fonts = std::filesystem::path(base) / "fonts";
    DebugLog debugLog(WebConfig::supported());
    DeckUi app;
    WebConfig web;
    WebWifiSetup webWifi;
    if (longMetadata)
        app.useLongMetadataFixture();
    const auto preferences = previewPreferences();
    const auto otaFile = preferences.empty() ? std::filesystem::path{} : preferences / "ota-url";
    const auto syncFile = preferences.empty() ? std::filesystem::path{} : preferences / "library-sync-url";
    auto ota = loadWebUrl(otaFile, validOtaUrl);
    const auto previousSyncUrl = loadWebUrl(syncFile, validLibrarySyncUrl).url;
    const auto previousFolderFile = preferences.empty() ? std::filesystem::path{} : preferences / "music-folder";
    const auto database = preferences.empty() ? std::filesystem::path{} : preferences / "library.sqlite";
    std::filesystem::path previousRoot;
    if (!previousFolderFile.empty()) {
        std::ifstream input(previousFolderFile, std::ios::binary);
        previousRoot = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    std::filesystem::path settingsPath;
    MusicIdAliases aliases;
    WebConfigState::LibraryConfig libraryConfig;
    libraryConfig.supported = !longMetadata && !bluetoothDemo && mediaSupported() && !database.empty();
    const auto setFolders = [&](std::vector<MusicFolder> folders) {
        libraryConfig.folders = std::move(folders);
        app.musicFolderSelected = !libraryConfig.folders.empty();
        app.enabledMusicFolders = std::ranges::any_of(libraryConfig.folders, &MusicFolder::enabled);
        musicPath = app.musicFolderSelected ? libraryConfig.folders.front().root : "";
        if (app.musicFolderSelected)
            settingsPath = preferences / "preview-libraries.conf";
    };
    const auto previewState = [&] {
        std::ostringstream settings;
        app.savePreview(settings);
        return std::move(settings).str();
    };
    const auto loadSettings = [&] {
        if (settingsPath.empty())
            return false;
        std::ifstream settings(settingsPath);
        if (!settings && !libraryConfig.folders.empty()) {
            const auto& origin = libraryConfig.folders.front().originalRoot;
            settings.open(preferences / ("preview-music-" + mediaKey(origin) + ".conf"));
        }
        const bool loaded = app.loadPreview(settings, aliases);
        if (!loaded && !longMetadata && !app.musicFolderSelected)
            loadClockSettings(settingsPath.parent_path() / "clock.conf", app);
        return loaded;
    };
    MediaScanProgress scanProgress, rekeyProgress;
    std::future<MediaLibrary> musicScan;
    std::future<std::vector<std::string>> missingCleanup;
    std::future<LibraryConfigResult> libraryJob;
    std::vector<MusicPath> pendingMissingPaths;
    std::optional<MusicRelocation> relocationPreview;
    bool rekeying = false;
    uint64_t nextPreviewToken = 0;
    std::string libraryMessage;
    std::jthread scanThread;
    const auto startMusicJob = [&scanThread](auto work) {
        using Result = std::invoke_result_t<decltype(work), std::stop_token>;
        std::packaged_task<Result(std::stop_token)> task(std::move(work));
        auto result = task.get_future();
        scanThread = std::jthread(std::move(task));
        return result;
    };
    bool libraryReady = true;
    const auto clearLibraryPreview = [&] {
        relocationPreview.reset();
        libraryConfig.preview.reset();
        web.publishLibraryReport(0, {});
    };
    const auto startMusicScan = [&](MusicScanMode mode = MusicScanMode::Quick) {
        if (!app.enabledMusicFolders || musicScan.valid() || missingCleanup.valid() || libraryJob.valid())
            return false;
        clearLibraryPreview();
        app.mediaStatus.clear();
        libraryMessage.clear();
        app.musicScan = DeckUi::MusicScan{libraryReady, mode};
        scanProgress.checked.store(0, std::memory_order_relaxed);
        scanProgress.total.store(0, std::memory_order_relaxed);
        musicScan = startMusicJob([&scanProgress, database](std::stop_token stop) {
            return scanMusic(scanProgress, stop, database);
        });
        return true;
    };
    if (libraryConfig.supported) {
        try {
            auto explicitRoot = musicPath;
            const bool initializedNamespaces = initializeMusicIndex(database, previousRoot, previousSyncUrl);
            if (!explicitRoot.empty())
                addMusicFolder(database, explicitRoot, {}, previousSyncUrl);
            setFolders(musicFolders(database));
            if (app.musicFolderSelected) {
                auto catalog = loadMusicIndex(database);
                libraryReady = !catalog.files.empty();
                app.useMediaLibrary(std::move(catalog));
                aliases = musicIdAliases(database);
                if (libraryReady && loadSettings() && initializedNamespaces && !savePreviewSettings(settingsPath, previewState()))
                    libraryConfig.error = "Library namespaces initialized. DeckDS could not save the playback settings.";
                startMusicScan();
            }
        } catch (const std::exception& error) {
            libraryConfig.error = error.what();
            libraryMessage = "Unable to open the music library.";
            SDL_Log("Unable to open the music library: %s", error.what());
        }
    }
    if (!app.musicFolderSelected && !bluetoothDemo && !preferences.empty()) {
        settingsPath = preferences / (longMetadata ? "preview-long-metadata.conf" : "preview.conf");
        loadSettings();
    }
    if (bluetoothDemo)
        app.useBluetoothFixture();
    auto savedSettings = previewState();
    const auto saveChangedSettings = [&] {
        auto current = previewState();
        if (current != savedSettings && savePreviewSettings(settingsPath, current))
            savedSettings = std::move(current);
    };
    const auto canManageLibrary = [&] {
        return libraryConfig.supported && !musicScan.valid() && !missingCleanup.valid() && !libraryJob.valid() &&
               !app.musicRemovalPending();
    };
    const auto startLibraryCommand = [&](const WebConfigCommand& command) {
        using Type = WebConfigCommand::Type;
        libraryConfig.error.clear();
        libraryConfig.message.clear();
        libraryConfig.backup.clear();
        const auto failure = [&](std::string message) {
            libraryConfig.error = std::move(message);
            ++libraryConfig.revision;
        };
        if (!canManageLibrary()) {
            failure("Wait for the current library operation to finish.");
            return;
        }
        const auto type = command.type;
        const auto target = command.target;
        const auto text = command.url;
        if (type == Type::LibraryApply || type == Type::LibraryRekey || type == Type::LibraryDelete) {
            const bool rekey = type == Type::LibraryRekey;
            const bool deleting = type == Type::LibraryDelete;
            if (type == Type::LibraryApply && (!relocationPreview || !libraryConfig.preview || libraryConfig.preview->token != command.review)) {
                failure("Open a current library preview before applying changes.");
                return;
            }
            if (deleting) {
                const auto folder = std::ranges::find(libraryConfig.folders, target, &MusicFolder::id);
                if (folder == libraryConfig.folders.end() || folder->enabled) {
                    failure("Disable the library before deleting it.");
                    return;
                }
            }
            if (rekey && libraryConfig.folders.empty()) {
                failure("Add a music folder first.");
                return;
            }
            const auto backup = preferences / "Library Backups" /
                (std::to_string(SDL_GetTicksNS()) + "-" + std::to_string(SDL_rand_bits()));
            auto relocation = type == Type::LibraryApply ? std::move(relocationPreview) : std::nullopt;
            clearLibraryPreview();
            app.player.playing = false;
            const auto snapshot = previewState();
            if (rekey) {
                rekeyProgress.checked.store(0, std::memory_order_relaxed);
                rekeyProgress.total.store(0, std::memory_order_relaxed);
            }
            libraryJob = startMusicJob([database, backup, snapshot, relocation = std::move(relocation),
                                        deleting, target, &rekeyProgress](std::stop_token stop) {
                std::filesystem::create_directories(backup);
                if (!savePreviewSettings(backup / "playback.conf", snapshot))
                    throw std::runtime_error("DeckDS could not back up playback settings.");
                if (relocation)
                    relocateMusicLibrary(database, *relocation, backup / "library.sqlite", stop);
                else if (deleting)
                    deleteMusicFolder(database, target, backup / "library.sqlite", stop);
                else
                    rekeyMusicIndex(database, backup / "library.sqlite", rekeyProgress, stop);
                LibraryConfigResult result;
                result.folders = musicFolders(database);
                result.catalog = loadMusicIndex(database);
                result.aliases = musicIdAliases(database);
                result.backup = backup.string();
                result.rescan = relocation.has_value();
                result.deleted = deleting;
                result.message = result.rescan ? "Library relocated." : deleting ? "Library deleted." : "ReKey complete.";
                return result;
            });
            rekeying = rekey;
            return;
        }
        clearLibraryPreview();
        libraryJob = startMusicJob([database, type, target, text, settings = command.librarySettings,
                                    enabled = command.value != 0, &previousSyncUrl](std::stop_token stop) {
            LibraryConfigResult result;
            const auto syncUrl = settings ? std::optional<std::string_view>(settings->syncUrl) : std::nullopt;
            if (type == Type::LibraryAdd) {
                addMusicFolder(database, target, text, previousSyncUrl, syncUrl.value_or(std::string_view{}));
                result.rescan = true;
                result.message = "Music folder added.";
            } else if (type == Type::LibraryRename) {
                renameMusicFolder(database, target, text, syncUrl);
                result.message = "Library settings saved.";
            } else if (type == Type::LibrarySyncUrl) {
                setMusicSyncUrl(database, target, text);
                result.message = text.empty() ? "Sync URL cleared." : "Sync URL saved.";
            } else if (type == Type::LibrarySetEnabled) {
                setMusicFolderEnabled(database, target, enabled);
                result.catalog = loadMusicIndex(database);
                result.aliases = musicIdAliases(database);
                result.rescan = enabled;
                result.message = enabled ? "Library enabled." : "Library disabled.";
            } else {
                result.relocation = previewMusicRelocation(database, target, text, stop, settings);
            }
            result.folders = musicFolders(database);
            return result;
        });
    };
    std::vector<std::unique_ptr<Window>> windows;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (separate) {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        if (kiosk && count < 2) {
            SDL_free(displays);
            SDL_Log("Two displays are required for kiosk mode");
            return 1;
        }
        windows.push_back(std::make_unique<Window>(fonts, Window::Pane::Upper, count > 0 ? displays[0] : primary, kiosk));
        windows.push_back(std::make_unique<Window>(fonts, Window::Pane::Lower, count > 1 ? displays[1] : primary, kiosk));
        SDL_free(displays);
        if (!kiosk && count < 2 && windows.front()->window && windows.back()->window) {
            int x = 0, y = 0;
            SDL_GetWindowPosition(windows.front()->window, &x, &y);
            SDL_SetWindowPosition(windows.front()->window, std::max(0, x - 184), std::max(0, y - 54));
            SDL_SetWindowPosition(windows.back()->window, std::max(0, x + 184), std::max(0, y + 54));
        }
    } else {
        windows.push_back(std::make_unique<Window>(fonts, Window::Pane::Both, primary, false));
    }
    for (const auto& view : windows)
        if (!view->ready()) {
            SDL_Log("Cannot initialize a DeckDS window: %s", SDL_GetError());
            return 1;
        }
    auto* textWindow = windows.back()->window;
    const auto picker = std::make_shared<FolderPicker>();
    bool choosingMusicFolder = false;
    const auto webState = [&] {
        WebConfigState state;
        state.folder = libraryConfig.folders.size() <= 1 ? musicPath.string()
            : std::to_string(libraryConfig.folders.size()) + " music folders";
        state.tracks = app.player.hasMediaLibrary() ? app.player.activeLibrarySize() : 0;
        state.message = libraryMessage;
        state.canScan = !choosingMusicFolder && !libraryJob.valid() && app.canScanMusic();
        state.review = app.missingMusicReview();
        state.missing = app.missingMusicFiles();
        state.removing = app.removingMusicFiles();
        state.settings = app.webSettings();
        const auto accents = accentPresets(app.theme);
        std::ranges::transform(accents, state.accents.begin(), &AccentPreset::name);
        state.playback = app.webPlayback();
        if (libraryJob.valid())
            state.playback.available = false;
        state.ota = ota;
        state.wifi = webWifi.state();
        state.bluetooth = app.webBluetooth();
        state.libraryConfig = libraryConfig;
        state.libraryConfig.busy = choosingMusicFolder || !canManageLibrary();
        if (rekeying)
            state.libraryConfig.rekey = WebConfigState::Scan{rekeyProgress.checked.load(std::memory_order_acquire),
                                                           rekeyProgress.total.load(std::memory_order_relaxed), false};
        if (const auto* track = app.player.track()) {
            state.track = track->title;
            state.artist = track->artist;
        }
        if (app.musicScan)
            state.scan = WebConfigState::Scan{app.musicScan->checked, app.musicScan->total,
                                            app.musicScan->mode == MusicScanMode::Full};
        return state;
    };
    if (WebConfig::supported()) {
        if (web.start(preferences, webState())) {
            app.refreshBluetoothDevices();
            SDL_Log("Web Config is listening on 0.0.0.0:8080");
        } else {
            SDL_Log("Web Config: %s", web.error().c_str());
        }
    }
    if (kiosk)
        SDL_HideCursor();

    const auto findWindow = [&](SDL_WindowID id) -> Window* {
        for (const auto& window : windows)
            if (SDL_GetWindowID(window->window) == id)
                return window.get();
        return nullptr;
    };

    std::map<SDL_JoystickID, SDL_Gamepad*> gamepads;
    const auto openPad = [&](SDL_JoystickID id) {
        if (!gamepads.contains(id))
            if (auto* pad = SDL_OpenGamepad(id)) {
                gamepads.emplace(id, pad);
                SDL_Log("Gamepad connected: %s", textOrEmpty(SDL_GetGamepadName(pad)).c_str());
            }
    };
    if (int count = 0; auto* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; ++i)
            openPad(ids[i]);
        SDL_free(ids);
    }

    bool running = true, enteringText = false;
    bool triggerLeft = false, triggerRight = false;
    Uint64 tick = SDL_GetTicksNS();
    Uint64 nextBatteryPoll = 0;
    Uint64 nextWifiPoll = 0;
    constexpr Uint64 saveInterval = 3'000'000'000ULL;
    Uint64 nextSettingsSave = tick + saveInterval;
    Uint64 nextWebPublish = 0;
    Uint64 nextDebugPublish = 0;
    double frameMs = 0;
    int batteryPercent = -1;
    SDL_PowerState powerState = SDL_POWERSTATE_UNKNOWN;
    std::optional<WebDebugState::ControllerEvent> lastControllerEvent;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                running = false;
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                app.cancelInput();
                if (auto* view = findWindow(event.window.windowID)) {
                    view->pointerDown = false;
                    view->studio.cancel(app);
                }
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                for (const auto& view : windows)
                    if (view->kind == Window::Pane::Both)
                        view->studio.keyVisual(app, event.key.key, event.type == SDL_EVENT_KEY_DOWN);
                app.key(event.key.key, event.type == SDL_EVENT_KEY_DOWN, event.key.repeat);
                break;
            case SDL_EVENT_TEXT_INPUT:
                app.text(event.text.text, true);
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                openPad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (const auto found = gamepads.find(event.gdevice.which); found != gamepads.end()) {
                    SDL_Log("Gamepad disconnected: %s", textOrEmpty(SDL_GetGamepadName(found->second)).c_str());
                    SDL_CloseGamepad(found->second);
                    gamepads.erase(found);
                }
                app.cancelInput();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const auto found = gamepads.find(event.gbutton.which);
                SDL_Gamepad* pad = found != gamepads.end() ? found->second : nullptr;
                const auto* button = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(event.gbutton.button));
                if (button)
                    lastControllerEvent = WebDebugState::ControllerEvent{button, SDL_GetTicks(),
                                                                         event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN};
                if (const auto key = controllerKey(pad, event.gbutton.button); key != SDLK_UNKNOWN)
                    app.key(key, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, false, true);
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX || event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
                    if (const auto found = gamepads.find(event.gaxis.which); found != gamepads.end()) {
                        app.analog(SDL_GetGamepadAxis(found->second, SDL_GAMEPAD_AXIS_LEFTX) / 32768.f,
                                   SDL_GetGamepadAxis(found->second, SDL_GAMEPAD_AXIS_LEFTY) / 32768.f);
                    }
                }
                if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    auto& sticky = event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? triggerLeft : triggerRight;
                    const bool down = sticky ? event.gaxis.value > 12000 : event.gaxis.value > 18000;
                    if (down != sticky)
                        app.key(event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? SDLK_LEFTBRACKET : SDLK_RIGHTBRACKET,
                                down, false, true);
                    sticky = down;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (auto* view = findWindow(event.wheel.windowID); view && view->kind != Window::Pane::Upper) {
                    float dx = event.wheel.x, dy = event.wheel.y;
                    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
                        dx = -dx;
                        dy = -dy;
                    }
                    if (view->kind == Window::Pane::Both)
                        view->studio.wheel(app, event.wheel.mouse_x, event.wheel.mouse_y, dx, dy, SDL_GetModState());
                    else if (inside(view->lower, event.wheel.mouse_x, event.wheel.mouse_y))
                        app.wheel(dy);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (auto* view = findWindow(event.button.windowID); view && view->kind != Window::Pane::Upper) {
                    const float x = event.button.x, y = event.button.y;
                    const bool down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                    if (view->kind == Window::Pane::Both) {
                        view->studio.mouseButton(app, event.button.button, down, x, y);
                    } else if (event.button.button == SDL_BUTTON_LEFT &&
                               view->lower.w > 0 && view->lower.h > 0 &&
                               (down ? inside(view->lower, x, y) : view->pointerDown)) {
                        app.pointer((x - view->lower.x) * 1024 / view->lower.w,
                                    (y - view->lower.y) * 768 / view->lower.h, down ? 0 : 2);
                        view->pointerDown = down;
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (auto* view = findWindow(event.motion.windowID); view) {
                    if (view->kind == Window::Pane::Both)
                        view->studio.mouseMotion(app, event.motion.x, event.motion.y);
                    else if (view->pointerDown && view->lower.w > 0 && view->lower.h > 0)
                        app.pointer((event.motion.x - view->lower.x) * 1024 / view->lower.w,
                                    (event.motion.y - view->lower.y) * 768 / view->lower.h, 1);
                }
                break;
            default:
                break;
            }
        }
        if (app.textInput() != enteringText) {
            enteringText = app.textInput();
            if (enteringText) SDL_StartTextInput(textWindow);
            else SDL_StopTextInput(textWindow);
        }
        const Uint64 frameTargetNs = app.displaysOff() ? 50'000'000ULL : 15'500'000ULL;
        Uint64 now = SDL_GetTicksNS();
        if (now - tick < frameTargetNs) {
            SDL_DelayNS(frameTargetNs - (now - tick));
            now = SDL_GetTicksNS();
        }
        const double step = (now - tick) / 1.e9;
        const double dt = std::min(step, .1);
        frameMs = frameMs ? std::lerp(frameMs, step * 1000, .08) : step * 1000;
        tick = now;
        if (now >= nextBatteryPoll) {
            powerState = SDL_GetPowerInfo(nullptr, &batteryPercent);
            app.batteryPercent = powerState == SDL_POWERSTATE_NO_BATTERY ? 100 : batteryPercent;
            nextBatteryPoll = now + 30'000'000'000ULL;
        }
        if (now >= nextWifiPoll) {
            app.wifiConnected = isWifiConnected();
            if (web.running())
                webWifi.refreshLink(app.wifiConnected);
            nextWifiPoll = now + 3'000'000'000ULL;
        }
        bool scanFromWeb = false;
        if (web.running()) {
            using Type = WebConfigCommand::Type;
            for (auto& command : web.takeCommands()) {
                switch (command.type) {
                case Type::QuickScan:
                case Type::FullScan:
                    if (!app.musicRescanRequested && !choosingMusicFolder && !libraryJob.valid() && app.canScanMusic()) {
                        scanFromWeb = true;
                        app.musicRescanRequested = command.type == Type::QuickScan ? MusicScanMode::Quick : MusicScanMode::Full;
                    }
                    break;
                case Type::KeepMissing:
                case Type::RemoveMissing:
                    if (app.missingMusicReview() == command.review)
                        app.answerMissingFiles(command.type == Type::RemoveMissing);
                    break;
                case Type::Setting:
                    app.setWebSetting(command.setting, command.value, command.theme);
                    break;
                case Type::Playback:
                    if (!libraryJob.valid())
                        app.applyWebPlayback(command.playback);
                    break;
                case Type::OtaUrl:
                    saveWebUrl(ota, otaFile, command.url, "DeckDS could not save the OTA URL.");
                    nextWebPublish = 0;
                    break;
                case Type::WifiScan:
                case Type::WifiConnect:
                case Type::WifiConnectManual:
                    webWifi.start(std::move(command));
                    nextWebPublish = 0;
                    break;
                case Type::BluetoothRefreshPaired:
                case Type::BluetoothScan:
                case Type::BluetoothConnect:
                case Type::BluetoothForget:
                    app.applyWebBluetooth(command.type, command.target);
                    nextWebPublish = 0;
                    break;
                case Type::LibraryAdd:
                case Type::LibraryRename:
                case Type::LibrarySyncUrl:
                case Type::LibrarySetEnabled:
                case Type::LibraryDelete:
                case Type::LibraryRekey:
                case Type::LibraryPreviewRelocate:
                case Type::LibraryApply:
                    if (!choosingMusicFolder)
                        startLibraryCommand(command);
                    else {
                        libraryConfig.error = "Close the folder picker first.";
                        ++libraryConfig.revision;
                    }
                    nextWebPublish = 0;
                    break;
                }
            }
            webWifi.update();
        }
        if (std::exchange(app.musicFolderRequested, false) && !choosingMusicFolder && canManageLibrary()) {
            choosingMusicFolder = true;
            const auto location = availableMusicPath(musicPath);
            const auto initialFolder = location.string();
            SDL_ShowOpenFolderDialog(musicFolderChosen, new std::shared_ptr<FolderPicker>(picker), textWindow,
                                     initialFolder.empty() ? nullptr : initialFolder.c_str(), false);
        }
        if (choosingMusicFolder) {
            std::optional<FolderChoice> choice;
            {
                const std::lock_guard lock(picker->mutex);
                choice = std::exchange(picker->choice, std::nullopt);
            }
            if (choice) {
                choosingMusicFolder = false;
                if (!choice->path.empty()) {
                    auto selected = availableMusicPath(choice->path);
                    if (!selected.empty()) {
                        if (libraryReady && !settingsPath.empty())
                            saveChangedSettings();
                        WebConfigCommand command{WebConfigCommand::Type::LibraryAdd};
                        command.target = selected.string();
                        startLibraryCommand(command);
                    } else {
                        choice->error = "The selected folder is unavailable.";
                    }
                }
                if (!choice->error.empty())
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Music Folder", choice->error.c_str(), textWindow);
            }
        }
        if (const auto mode = std::exchange(app.musicRescanRequested, std::nullopt);
            mode && !choosingMusicFolder && app.canScanMusic()) {
            if (startMusicScan(*mode)) {
                libraryConfig.error.clear();
                libraryConfig.message.clear();
                libraryConfig.backup.clear();
            } else if (!scanFromWeb) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Music Library", "Another library operation is running.", textWindow);
            }
        }
        if (const auto decision = std::exchange(app.musicMissingDecision, std::nullopt); decision) {
            auto paths = std::exchange(pendingMissingPaths, {});
            if (*decision) {
                missingCleanup = startMusicJob([database, paths = std::move(paths)](std::stop_token stop) {
                    return removeMissingMusic(paths, stop, database);
                });
            } else {
                libraryMessage = "Missing entries kept.";
            }
        }
        if (missingCleanup.valid() && missingCleanup.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                const auto removed = missingCleanup.get();
                if (!removed.empty()) {
                    app.removeMediaTracks(removed);
                    if (!settingsPath.empty())
                        saveChangedSettings();
                }
                SDL_Log("Removed %zu missing files from the music index", removed.size());
                libraryMessage = removed.empty() ? "The missing entries are back in the library."
                    : "Removed " + std::to_string(removed.size()) + (removed.size() == 1 ? " entry." : " entries.");
            } catch (const std::exception& error) {
                SDL_Log("Unable to remove missing files: %s", error.what());
                libraryMessage = "Unable to remove missing entries.";
                libraryConfig.error = libraryMessage;
                if (!web.running())
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Music Library", error.what(), textWindow);
            }
            app.dismissMissingFiles();
        }
        if (libraryJob.valid() && libraryJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            rekeying = false;
            ++libraryConfig.revision;
            nextWebPublish = 0;
            try {
                auto result = libraryJob.get();
                const bool firstLibrary = !app.musicFolderSelected && !result.folders.empty();
                setFolders(std::move(result.folders));
                if (result.relocation) {
                    relocationPreview = std::move(result.relocation);
                    const auto& plan = *relocationPreview;
                    WebConfigState::LibraryConfig::Preview preview;
                    preview.destination = plan.destination;
                    preview.token = ++nextPreviewToken;
                    preview.matched = plan.matched;
                    preview.missing = plan.missing.size();
                    const auto listed = std::min<size_t>(plan.missing.size(), 100);
                    preview.unmatched.assign(plan.missing.begin(), plan.missing.begin() + listed);
                    std::string report;
                    for (const auto& path : plan.missing)
                        report += path + '\n';
                    web.publishLibraryReport(preview.token, std::move(report));
                    libraryConfig.preview = std::move(preview);
                }
                if (result.catalog) {
                    const auto before = previewState();
                    aliases = std::move(result.aliases);
                    libraryReady = !result.catalog->files.empty();
                    app.useMediaLibrary(std::move(*result.catalog));
                    std::istringstream snapshot(before);
                    app.loadPreview(snapshot, aliases);
                    if ((libraryReady || result.deleted) && !savePreviewSettings(settingsPath, previewState()))
                        libraryConfig.error = "Library updated. DeckDS could not save the playback settings.";
                    else
                        savedSettings = previewState();
                } else if (firstLibrary) {
                    libraryReady = false;
                    app.useMediaLibrary({});
                }
                libraryConfig.message = std::move(result.message);
                libraryConfig.backup = std::move(result.backup);
                if (result.rescan)
                    startMusicScan();
            } catch (const std::exception& error) {
                libraryConfig.error = error.what();
                libraryMessage = "Library operation failed.";
                SDL_Log("Library operation failed: %s", error.what());
            }
        }
        if (musicScan.valid()) {
            if (!choosingMusicFolder && musicScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                app.musicScan.reset();
                try {
                    auto library = musicScan.get();
                    SDL_Log("Music library indexed %zu tracks (%zu probed, %zu cached, %zu skipped, %zu missing)",
                            library.files.size(), library.probed, library.reused, library.skipped, library.missing);
                    auto missing = std::move(library.missingPaths);
                    if (library.unavailable)
                        libraryMessage = std::to_string(library.unavailable) +
                            (library.unavailable == 1 ? " music folder unavailable." : " music folders unavailable.");
                    else
                        libraryMessage = library.files.empty() ? "No playable music files found." : "";
                    if (libraryReady) {
                        app.player.refreshMediaLibrary(std::move(library));
                    } else if (library.files.empty()) {
                        app.mediaStatus = "No playable music files found";
                    } else {
                        app.useMediaLibrary(std::move(library));
                        app.mediaStatus.clear();
                        loadSettings();
                        savedSettings = previewState();
                        libraryReady = true;
                    }
                    if (!missing.empty()) {
                        libraryMessage.clear();
                        pendingMissingPaths = std::move(missing);
                        app.askRemoveMissingFiles(pendingMissingPaths.size());
                    }
                } catch (const std::exception& error) {
                    SDL_Log("Music library scan failed: %s", error.what());
                    libraryMessage = "Unable to scan the music folder.";
                    libraryConfig.error = libraryMessage;
                    if (!libraryReady)
                        app.mediaStatus = "Unable to scan the music folder";
                }
            } else {
                app.musicScan->checked = scanProgress.checked.load(std::memory_order_acquire);
                app.musicScan->total = scanProgress.total.load(std::memory_order_relaxed);
            }
        }
        app.update(dt);
        if (libraryReady && !settingsPath.empty() && now >= nextSettingsSave) {
            saveChangedSettings();
            nextSettingsSave = now + saveInterval;
        }
        if (web.running() && now >= nextWebPublish) {
            web.publish(webState());
            nextWebPublish = now + 150'000'000ULL;
        }
        if (web.running() && now >= nextDebugPublish) {
            WebDebugState state;
            collectSystemDebug(state);
            state.version = textOrEmpty(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING));
            const int sdlVersion = SDL_GetVersion();
            state.sdlVersion = std::to_string(SDL_VERSIONNUM_MAJOR(sdlVersion)) + '.' +
                               std::to_string(SDL_VERSIONNUM_MINOR(sdlVersion)) + '.' +
                               std::to_string(SDL_VERSIONNUM_MICRO(sdlVersion));
            state.mode = kiosk ? "kiosk" : separate ? "panels" : "studio";
            state.uptimeMs = SDL_GetTicks();
            state.frameMs = frameMs;
            state.powerState = debugPowerState(powerState);
            if (batteryPercent >= 0)
                state.batteryPercent = batteryPercent;
            state.wifiConnected = app.wifiConnected;
            state.bluetoothConnected = app.bluetoothConnected();
            state.lidClosed = app.lid;
            state.displaysOff = app.displaysOff();
            state.videoDriver = textOrEmpty(SDL_GetCurrentVideoDriver());
            int displayCount = 0;
            if (auto* ids = SDL_GetDisplays(&displayCount)) {
                for (int i = 0; i < displayCount; ++i)
                    if (const auto* mode = SDL_GetCurrentDisplayMode(ids[i]))
                        state.displays.push_back({textOrEmpty(SDL_GetDisplayName(ids[i])), mode->w, mode->h, mode->refresh_rate});
                SDL_free(ids);
            }
            for (const auto& window : windows)
                state.windows.push_back(window->debugState());
            state.audio.driver = textOrEmpty(SDL_GetCurrentAudioDriver());
            state.audio.playing = app.player.playing;
            if (const auto* track = app.player.track()) {
                state.audio.codec = track->format;
                state.audio.sourceRate = track->rate;
            }
            const auto format = app.player.audioFormat();
            state.audio.decoderHz = format.decoderHz;
            state.audio.deviceHz = format.deviceHz;
            state.audio.deviceChannels = format.deviceChannels;
            state.audio.deviceBufferFrames = format.deviceBufferFrames;
            SDL_free(SDL_GetTouchDevices(&state.touchDevices));
            for (const auto& [id, pad] : gamepads) {
                (void)id;
                state.gamepads.push_back(textOrEmpty(SDL_GetGamepadName(pad)));
            }
            state.lastControllerEvent = lastControllerEvent;
            state.tracks = app.player.hasMediaLibrary() ? app.player.activeLibrarySize() : 0;
            state.queue = app.player.queue.size();
            state.missing = app.missingMusicFiles();
            state.libraryStatus = libraryMessage;
            if (app.musicScan)
                state.scan = WebConfigState::Scan{app.musicScan->checked, app.musicScan->total,
                                                app.musicScan->mode == MusicScanMode::Full};
            state.logs = debugLog.recent();
            web.publishDebug(state);
            nextDebugPublish = now + 1'000'000'000ULL;
        }
        for (const auto& window : windows) {
            if (window->kind == Window::Pane::Both)
                window->studio.update(app, dt);
            window->render(app);
        }
    }

    web.stop();
    if (libraryReady && !settingsPath.empty())
        saveChangedSettings();

    for (const auto& [id, gamepad] : gamepads) {
        (void)id;
        SDL_CloseGamepad(gamepad);
    }
    return 0;
}
int dumpViews(const std::filesystem::path& outDir, bool longMetadata, bool bluetoothDemo)
{
    const char* base = SDL_GetBasePath();
    if (!base)
        return 1;
    std::filesystem::create_directories(outDir);
    SDL_Window* win = SDL_CreateWindow("DeckDS Snapshot", 1024, 768, SDL_WINDOW_HIDDEN);
    if (!win)
        return 1;
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    if (!ren) {
        SDL_DestroyWindow(win);
        return 1;
    }
    SDL_Texture* topTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1024, 768);
    SDL_Texture* botTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1024, 768);
    SDL_Texture* fadeTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 1024, 768);
    if (fadeTex)
        SDL_SetTextureBlendMode(fadeTex, SDL_BLENDMODE_BLEND);

    {
        Draw draw(ren, std::filesystem::path(base) / "fonts");
        DeckUi ui;
        const char* names[] = {
            "00_deck_jp", "01_albums_minus1_play", "02_albums_search_osk", "03_songs_star_nodate",
            "04_songs_bottom_scroll", "05_order_songs", "06_artist_detail", "07_playlist_detail",
            "08_playlist_picker", "09_settings_accents_sfx", "10_vfd_amber_vu_jp", "11_ceramic_cobalt_info",
            "12_settings_clock_12h", "13_settings_lid_close", "14_audio_output_10_devices",
            "15_long_deck", "16_long_albums_grid", "17_long_albums_list", "18_long_album_detail",
            "19_long_artists", "20_long_artist_detail", "21_long_songs", "22_long_playlist_detail",
            "23_long_queue", "24_long_context", "25_long_info", "26_long_vfd_spectrum"
        };
        constexpr int bluetoothView = 14;
        constexpr int firstLongView = 15;
        if (longMetadata)
            ui.useLongMetadataFixture();
        const int start = bluetoothDemo ? bluetoothView : longMetadata ? firstLongView : 0;
        const int end = !bluetoothDemo && longMetadata ? static_cast<int>(std::size(names)) : firstLongView;
        for (int i = start; i < end; ++i) {
            ui.configureSnapshot(i);
            SDL_SetRenderTarget(ren, topTex);
            ui.upper(draw, fadeTex);
            if (SDL_Surface* sTop = SDL_RenderReadPixels(ren, nullptr)) {
                IMG_SavePNG(sTop, (outDir / (std::string(names[i]) + "_top.png")).string().c_str());
                SDL_DestroySurface(sTop);
            }
            SDL_SetRenderTarget(ren, botTex);
            ui.lower(draw);
            if (SDL_Surface* sBot = SDL_RenderReadPixels(ren, nullptr)) {
                IMG_SavePNG(sBot, (outDir / (std::string(names[i]) + "_bottom.png")).string().c_str());
                SDL_DestroySurface(sBot);
            }
        }
    }
    for (auto* t : {topTex, botTex, fadeTex})
        if (t) SDL_DestroyTexture(t);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return 0;
}
}

int main(int argc, char** argv)
{
    bool separate = false, kiosk = false, longMetadata = false, bluetoothDemo = false;
    std::filesystem::path dumpDir, musicPath;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--panels") separate = true;
        else if (arg == "--kiosk") kiosk = separate = true;
        else if (arg == "--long-metadata") longMetadata = true;
        else if (arg == "--bluetooth-demo") bluetoothDemo = true;
        else if (arg == "--web-config") continue; // Earlier previews required this flag.
        else if (arg == "--music-path" && i + 1 < argc) {
            musicPath = availableMusicPath(argv[++i]);
            if (musicPath.empty()) {
                std::cerr << "Music path must be a readable directory.\n";
                return 2;
            }
        }
        else if (arg == "--dump-views" && i + 1 < argc) dumpDir = argv[++i];
        else {
            std::cout << "Usage: DeckDS [--panels | --kiosk | --dump-views <dir>] "
                         "[--long-metadata | --bluetooth-demo | --music-path <dir>]\n";
            return arg == "--help" ? 0 : 2;
        }
    }
    if (!musicPath.empty() && (longMetadata || bluetoothDemo || !dumpDir.empty())) {
        std::cerr << "--music-path opens the live preview and cannot be combined with demo or snapshot options.\n";
        return 2;
    }
    if (!musicPath.empty() && !mediaSupported()) {
        std::cerr << "--music-path requires a build with FFmpeg and SQLite development libraries.\n";
        return 2;
    }

    SDL_SetAppMetadata("DeckDS", "0.1", "org.deckds.native");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
    const SDL_InitFlags flags = dumpDir.empty() ? (SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)
                                                : SDL_INIT_VIDEO;
    if (!SDL_Init(flags)) {
        SDL_Log("SDL initialization failed: %s", SDL_GetError());
        return 1;
    }
    if (!TTF_Init()) {
        SDL_Log("SDL_ttf initialization failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    const int result = dumpDir.empty() ? run(separate, kiosk, longMetadata, bluetoothDemo, musicPath)
                                       : dumpViews(dumpDir, longMetadata, bluetoothDemo);
    TTF_Quit();
    SDL_Quit();
    return result;
}

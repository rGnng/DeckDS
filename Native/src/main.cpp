#include "studio.hpp"
#include "wifi.hpp"

#include <SDL3/SDL_main.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <future>
#include <iterator>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {
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

int run(bool separate, bool kiosk, bool longMetadata, bool bluetoothDemo, std::filesystem::path musicPath)
{
    const char* base = SDL_GetBasePath();
    if (!base) {
        SDL_Log("Cannot find app resources: %s", SDL_GetError());
        return 1;
    }
    const auto fonts = std::filesystem::path(base) / "fonts";
    DeckUi app;
    if (longMetadata)
        app.useLongMetadataFixture();
    const auto preferences = previewPreferences();
    const auto folderFile = preferences.empty() ? std::filesystem::path{} : preferences / "music-folder";
    const auto database = preferences.empty() ? std::filesystem::path{} : preferences / "library.sqlite";
    const bool explicitMusicPath = !musicPath.empty();
    if (!explicitMusicPath && !longMetadata && !bluetoothDemo && mediaSupported() && !folderFile.empty()) {
        std::ifstream input(folderFile, std::ios::binary);
        musicPath = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    std::filesystem::path settingsPath;
    const auto loadSettings = [&] {
        if (settingsPath.empty())
            return;
        std::ifstream settings(settingsPath);
        if (!app.loadPreview(settings) && !longMetadata && musicPath.empty())
            loadClockSettings(settingsPath.parent_path() / "clock.conf", app);
    };
    std::atomic<size_t> scanned{0};
    std::future<MediaLibrary> musicScan;
    std::jthread scanThread;
    bool libraryReady = true;
    const auto startMusicScan = [&](std::filesystem::path path, bool remember) {
        if (scanThread.joinable()) {
            scanThread.request_stop();
            scanThread.join();
        }
        const auto resolved = availableMusicPath(path);
        const auto root = resolved.empty() ? std::move(path) : resolved;
        const bool keepCurrent = libraryReady && app.player.hasMediaLibrary() && musicPath == root;
        musicPath = root;
        musicScan = {};
        if (!keepCurrent) {
            libraryReady = false;
            settingsPath = preferences.empty() ? std::filesystem::path{}
                : preferences / ("preview-music-" + mediaKey(musicPath.string()) + ".conf");
            try {
                if (auto cached = loadMusicIndex(musicPath, database); cached && !cached->files.empty()) {
                    SDL_Log("Loaded %zu tracks from the music library index", cached->files.size());
                    app.useMediaLibrary(std::move(*cached));
                    libraryReady = true;
                    loadSettings();
                }
            } catch (const std::exception& error) {
                SDL_Log("Unable to read the music library index: %s", error.what());
            }
            if (!libraryReady)
                app.useMediaLibrary({});
        }
        app.mediaStatus.clear();
        if (resolved.empty()) {
            if (!libraryReady)
                app.mediaStatus = "Music folder is unavailable";
            return;
        }
        if (remember && !folderFile.empty())
            savePreviewSettings(folderFile, musicPath.string());
        if (!libraryReady)
            app.mediaStatus = "Scanning music…";
        scanned.store(0, std::memory_order_relaxed);
        std::promise<MediaLibrary> result;
        musicScan = result.get_future();
        scanThread = std::jthread([&scanned, musicPath, database, result = std::move(result)](std::stop_token stop) mutable {
            try {
                result.set_value(scanMusic(musicPath, scanned, stop, database));
            } catch (...) {
                result.set_exception(std::current_exception());
            }
        });
    };
    if (!musicPath.empty()) {
        startMusicScan(musicPath, explicitMusicPath);
    } else if (!bluetoothDemo && !preferences.empty()) {
        settingsPath = preferences / (longMetadata ? "preview-long-metadata.conf" : "preview.conf");
        loadSettings();
    }
    if (bluetoothDemo)
        app.useBluetoothFixture();
    const auto previewState = [&] {
        std::ostringstream settings;
        app.savePreview(settings);
        return std::move(settings).str();
    };
    auto savedSettings = previewState();
    const auto saveChangedSettings = [&] {
        auto current = previewState();
        if (current != savedSettings && savePreviewSettings(settingsPath, current))
            savedSettings = std::move(current);
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
            if (auto* pad = SDL_OpenGamepad(id))
                gamepads.emplace(id, pad);
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
                    SDL_CloseGamepad(found->second);
                    gamepads.erase(found);
                }
                app.cancelInput();
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                const auto found = gamepads.find(event.gbutton.which);
                SDL_Gamepad* pad = found != gamepads.end() ? found->second : nullptr;
                if (const auto key = controllerKey(pad, event.gbutton.button); key != SDLK_UNKNOWN)
                    app.key(key, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, false, true);
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
                    app.analog(event.gaxis.value / 32768.f);
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
        const double dt = std::min((now - tick) / 1.e9, .1);
        tick = now;
        if (now >= nextBatteryPoll) {
            int percent = -1;
            const auto powerState = SDL_GetPowerInfo(nullptr, &percent);
            app.batteryPercent = powerState == SDL_POWERSTATE_NO_BATTERY ? 100 : percent;
            nextBatteryPoll = now + 30'000'000'000ULL;
        }
        if (now >= nextWifiPoll) {
            app.wifiConnected = isWifiConnected();
            nextWifiPoll = now + 3'000'000'000ULL;
        }
        if (std::exchange(app.musicFolderRequested, false) && !choosingMusicFolder) {
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
                        startMusicScan(std::move(selected), true);
                        savedSettings = previewState();
                    } else {
                        choice->error = "The selected folder is unavailable.";
                    }
                }
                if (!choice->error.empty())
                    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Music Folder", choice->error.c_str(), textWindow);
            }
        }
        if (musicScan.valid()) {
            if (musicScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try {
                    auto library = musicScan.get();
                    SDL_Log("Music library indexed %zu tracks (%zu probed, %zu cached, %zu skipped, %zu missing)",
                            library.files.size(), library.probed, library.reused, library.skipped, library.missing);
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
                } catch (const std::exception& error) {
                    SDL_Log("Music library scan failed: %s", error.what());
                    if (!libraryReady)
                        app.mediaStatus = "Unable to scan the music folder";
                }
            } else if (!libraryReady) {
                app.mediaStatus = "Scanning music… " + std::to_string(scanned.load(std::memory_order_relaxed));
            }
        }
        app.mediaScanning = musicScan.valid();
        app.update(dt);
        if (libraryReady && !settingsPath.empty() && now >= nextSettingsSave) {
            saveChangedSettings();
            nextSettingsSave = now + saveInterval;
        }
        for (const auto& window : windows) {
            if (window->kind == Window::Pane::Both)
                window->studio.update(app, dt);
            window->render(app);
        }
    }

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

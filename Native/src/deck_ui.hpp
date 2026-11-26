#pragma once

#include "stage.hpp"

#include <optional>

enum class PreviewScale { Fit, Integer, Studio, PixelOne, PixelTwo, Count };

class DeckUi {
public:
    enum class Tab { Deck, Albums, Artists, Playlists, Songs, Queue };
    Theme theme = Theme::Obsidian;
    Player player;
    bool lid = false;
    bool showKeys = false;
    PreviewScale scaleMode = PreviewScale::Fit;
    bool textInput() const;
    bool displaysOff() const;
    std::string_view sleepMessage() const;
    float backlight() const;
    void update(double dt);
    void upper(Draw& draw, SDL_Texture* fadeLayer);
    void lower(Draw& draw);
    void key(SDL_Keycode key, bool down, bool repeated = false, bool controller = false);
    void text(std::string_view value, bool physicalKeyboard = false);
    void analog(float position);
    void toggleLibrary();
    void pointer(float x, float y, int phase);
    void wheel(float y);
    void cancelInput();

private:
    enum class Action {
        None, Tab, Settings, Context, Scrub, Previous, Play, Next, Star, Volume, Mute, Output,
        Stage, Shuffle, Repeat, Album, ClearSearch, Search, Grid, Order, NewPlaylist, ListPlay,
        ListShuffle, Row, RowMenu, PlayNext, Back, Rail, Option, Key, DetailQueue, Brightness
    };
    enum class Sheet { None, Settings, Track, Info, Order, Output, Playlist };
    enum class Input { None, Search, Playlist };
    struct Hit { Rect box; Action action; int data; };
    struct Row {
        std::string title, subtitle;
        std::vector<int> ids;
        int cover = -1;
        int track = -1;
        Tab source = Tab::Deck;
        int playlist = -1;
    };
    struct Option {
        std::string label, value;
        std::string_view icon;
    };

    static constexpr int brightnessLevels[] = {15, 30, 45, 60, 75, 90, 100};
    static constexpr Rect deckWave{38, 166, 948, 148};
    static constexpr Rect volumeTrack{112, 572, 800, 24};
    Tab tab = Tab::Deck;
    int stage = 0, outgoingStage = 0;
    float stageTime = 1, tabTime = 1;
    std::array<float, 6> tabWidths{265, 107, 107, 107, 107, 107};
    std::array<float, 6> tabStart = tabWidths;
    Meter meter;
    int brightness = 6, output = 0;
    bool pocket = true, albumsInGrid = true;
    bool screenOff = false, poweredOff = false, powerDown = false, powerLong = false;
    double powerTime = 0;
    float volumeTime = 0, seekPopup = 0;
    int sortField = 0;
    bool ascending = false;
    std::array<int, 6> sortFields{0, 0, 1, 0, 0, 0};
    std::array<bool, 6> sortDirections{false, false, true, false, false, false};
    Sheet sheet = Sheet::None;
    Input input = Input::None;
    bool symbols = false, utility = false, toolFocus = false, keypad = false;
    float utilityHeight = 0, keyboardHeight = 0;
    std::string query, playlistName, notice;
    float noticeTime = 0;
    int selection = 0, toolSelection = 2, keySelection = 0, sheetSelection = 0;
    int contextTrack = -1, contextPlaylist = -1;
    std::vector<Row> details;
    std::vector<Hit> hits;
    std::optional<Hit> pressed;
    Action lastAction = Action::None;
    int lastData = 0;
    float releaseTime = 0, pressTime = 0, pointerStartY = 0, pointerLastY = 0;
    std::string_view pressVoice;
    bool dragging = false, held = false;
    int seekDirection = 0;
    float seekHold = 0;
    float analogPosition = 0;
    Rect listBox{};
    float scroll = 0, scrollFrom = 0, scrollTarget = 0, scrollTime = 1, scrollLimit = 0;
    float railPopup = 0;
    int infoScroll = 0;
    mutable std::vector<Row> cachedRows;
    mutable unsigned cachedPlayerRevision = 0;
    mutable bool rowsValid = false;
    std::optional<Rect> hitClip;

    const std::vector<Row>& rows() const;
    std::vector<Row> buildRows() const;
    std::vector<Option> options() const;
    int gridColumns() const;
    float rowHeight() const;
    float rowGap() const;
    void tabs(Draw& d);
    void transport(Draw& d);
    void catalog(Draw& d);
    void keypadUi(Draw& d);
    void overlay(Draw& d);
    void button(Draw& d, Rect r, Action action, int value, std::string_view glyph = {},
                std::string_view label = {}, bool focus = false, bool hero = false,
                int iconSize = 32, std::optional<Color> background = std::nullopt,
                std::optional<Color> ink = std::nullopt, int vfdSize = 32, int corner = 14,
                bool marker = false);
    void hit(Rect r, Action action, int data = 0);
    std::string_view voiceFor(Action action) const;
    void activate(Action action, int data);
    void targetScroll(float target, bool animate = true);
    void keepSelectedVisible();
    void scrubRail(float y);
    void scrubPosition(Action action, float x);
    void openInput(Input field);
    void closeInput(bool confirm);
    void setTab(int index);
    void selectRow(int index);
    void openContext(int index, int playlist = -1);
    void openAlbum(int index);
    void openSheet(Sheet next);
    void cycleStage();
    void toggleLid();
    void power(bool down, bool repeated);
    void flash(std::string message);
    void back();
    void libraryKey(SDL_Keycode key);
    void sheetKey(SDL_Keycode key);
    void eraseCharacter();
};

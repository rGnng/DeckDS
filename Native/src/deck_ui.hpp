#pragma once

#include "bluetooth.hpp"
#include "stage.hpp"
#include "web_config.hpp"

#include <functional>
#include <iosfwd>
#include <optional>

enum class PreviewScale { Fit, Integer, Studio, PixelOne, PixelTwo, Count };

class DeckUi {
public:
    enum class Tab { Deck, Albums, Artists, Playlists, Songs, Queue };
    struct MusicScan {
        bool inBackground;
        MusicScanMode mode;
        size_t checked = 0, total = 0;
        Uint64 started = SDL_GetTicks();

        void draw(Draw& draw, Rect area, Size titleSize, Size percentSize) const;
    };
    Theme theme = Theme::Obsidian;
    std::array<int, 3> themeAccents{0, 0, 0};
    bool clock24h = true;
    int clockOffsetMinutes = 0;
    int batteryPercent = 100;
    bool wifiConnected = false;
    std::string mediaStatus;
    std::optional<MusicScan> musicScan;
    std::optional<MusicScanMode> musicRescanRequested;
    std::optional<bool> musicMissingDecision;
    bool musicFolderSelected = false, musicFolderRequested = false;
    Player player;
    bool lid = false;
    bool showKeys = false;
    PreviewScale scaleMode = PreviewScale::Fit;

    int currentAccent() const { return themeAccents[static_cast<int>(theme)]; }
    std::string clockString() const;
    bool textInput() const;
    bool displaysOff() const;
    bool bluetoothConnected() const { return !bluetooth.connectedDevice().empty(); }
    std::string_view sleepMessage() const;
    float backlight() const;
    void update(double dt);
    void upper(Draw& draw, SDL_Texture* fadeLayer);
    void lower(Draw& draw);
    void key(SDL_Keycode key, bool down, bool repeated = false, bool controller = false);
    void text(std::string_view value, bool physicalKeyboard = false);
    void analog(float x, float y);
    void toggleLibrary();
    void pointer(float x, float y, int phase);
    void wheel(float y);
    void cancelInput();
    bool loadPreview(std::istream& settings);
    void savePreview(std::ostream& settings) const;
    void useMediaLibrary(MediaLibrary library);
    bool musicRemovalPending() const { return missingFileCount != 0; }
    bool canScanMusic() const { return musicFolderSelected && !musicScan && !musicRemovalPending(); }
    size_t missingMusicFiles() const { return missingFileCount; }
    uint64_t missingMusicReview() const { return missingReview; }
    bool removingMusicFiles() const { return removingMissingFiles; }
    void askRemoveMissingFiles(size_t count);
    void answerMissingFiles(bool remove);
    void dismissMissingFiles();
    void removeMediaTracks(std::span<const std::string> ids);
    WebSettings webSettings() const;
    void setWebSetting(WebSetting setting, int value, int settingTheme);
    WebConfigState::Bluetooth webBluetooth() const;
    void refreshBluetoothDevices() { bluetooth.refreshPaired(); }
    void applyWebBluetooth(WebConfigCommand::Type action, std::string_view target);
    WebConfigState::Playback webPlayback() const;
    void applyWebPlayback(const WebPlaybackCommand& command);
    void useLongMetadataFixture();
    void useBluetoothFixture();
    void configureSnapshot(int preset);

private:
    enum class Action {
        None, Tab, Settings, Context, Scrub, Previous, Play, Next, Star, Volume, Mute, Output,
        Stage, Shuffle, Repeat, Album, ClearSearch, Search, Grid, Order, NewPlaylist, CreatePlaylist,
        ListPlay, ListShuffle, Row, RowMenu, PlayNext, Back, Rail, SettingsRail, OutputRail, Option, Key,
        DetailQueue, Brightness, BluetoothUnpair
    };
    enum class Sheet { None, Settings, Track, Info, Order, Output, Playlist, MissingFiles };
    enum class Input { None, Search, Playlist };
    enum class QueuePart { None, Played, Current, UpNext, EmptyUpNext };
    enum class SortField { Added, Name, Year, Artist, Album, AlbumCount, SongCount };
    struct SortSpec {
        SortField field;
        std::string_view label, hint, ascLabel, descLabel;
        bool alphaMode, defaultAsc;
    };
    struct Hit { Rect box; Action action; int data; };
    struct LayoutHeader { float y; std::string text; bool discDivider; };
    struct Row {
        std::string title, subtitle;
        std::vector<int> ids;
        int cover = -1;
        int track = -1;
        Tab source = Tab::Deck;
        int playlist = -1;
        int albumCount = 0;
        int year = 0;
        std::string added;
        Rect box{};
        bool albumTrack = false;
        QueuePart queuePart = QueuePart::None;
        int queueIndex = -1;
    };
    struct RowKey {
        explicit RowKey(const Row& row);
        bool matches(const Row& row) const;

        Tab source;
        QueuePart queuePart;
        int track, playlist;
        std::string title, subtitle;
    };
    struct CatalogPosition {
        float scroll = 0.f;
        int selection = 0;
        std::optional<RowKey> item;
    };
    struct Detail {
        Row row;
        CatalogPosition parent;
    };
    struct Option {
        std::string label, value;
        std::string_view icon;
        bool active = false, member = false;
        int id = 0;
    };

    static constexpr int brightnessLevels[] = {15, 30, 45, 60, 75, 90, 100};
    static constexpr Rect deckWave{38, 166, 948, 148};
    static constexpr Rect volumeTrack{112, 572, 800, 24};

    Tab tab = Tab::Deck;
    int stage = 0, outgoingStage = 0;
    float stageTime = 1, tabTime = 1;
    float queueLabelTime = 0.f;
    // The first phase moves the upper strip. The second phase reveals the lower waveform.
    float deckProgressTransition = 2.f;
    std::string waveformTrackKey;
    std::optional<MediaWaveform> deckWaveform;
    float waveformLoadProgress = 1.f;
    float stageCentering = 1.f;
    std::array<float, 6> tabWidths{260, 108, 108, 108, 108, 108};
    std::array<float, 6> tabStart = tabWidths;
    std::array<Rect, 6> tabRects{};
    bool tabSliding = false, tabSlideMoved = false, tabSlideCommitted = false;
    int slideTab = 0;
    float tabSlideDwell = 0;

    Meter meter;
    BluetoothPairing bluetooth;
    int brightness = 6, output = 0, lastNonZeroVolume = 76;
    bool pocket = true, albumsInGrid = true;
    bool screenOff = false, poweredOff = false, powerDown = false, powerLong = false;
    double powerTime = 0;
    float volumeTime = 0;
    SortField sortField = SortField::Added;
    bool ascending = false;
    std::array<SortField, 6> sortFields{
        SortField::Added, SortField::Added, SortField::Name, SortField::Added, SortField::Added, SortField::Added
    };
    std::array<bool, 6> sortDirections{false, false, true, false, false, false};

    Sheet sheet = Sheet::None;
    Sheet sheetBeforeMissingFiles = Sheet::None;
    size_t missingFileCount = 0;
    uint64_t missingReview = 0;
    int selectionBeforeMissingFiles = 0, libraryScanFocus = 0;
    bool removingMissingFiles = false;
    Input input = Input::None;
    bool symbols = false, utility = false, toolFocus = false, keypad = false;
    float utilityAnim = 0, searchAnim = 0, keyboardHeight = 0;
    std::array<float, 2> catBtnAnim{};
    std::array<float, 3> detailBtnAnim{};
    std::string query, playlistName;
    int selection = 0, toolSelection = 2, keySelection = 0, sheetSelection = 0, clockBtnFocus = 1;
    int marqueeRow = -1, touchSelectedTrack = -1;
    std::string marqueeTitle;
    float rowMarqueeTime = 0.f;
    int contextTrack = -1, contextPlaylist = -1, contextQueueIndex = -1;
    QueuePart contextQueuePart = QueuePart::None;
    std::string armedBluetoothDevice;
    std::vector<Detail> details;
    std::array<CatalogPosition, 6> rootPositions{};
    std::vector<Hit> hits;
    std::optional<Hit> pressed;
    Action lastAction = Action::None;
    int lastData = 0;
    float releaseTime = 0, pressTime = 0, keyPressTime = 0;
    float pointerStartX = 0, pointerStartY = 0, pointerLastX = 0, pointerLastY = 0;
    std::string_view pressVoice;
    struct HeldKey {
        std::string_view voice;
        std::function<void()> onRelease;
    };
    std::map<SDL_Keycode, HeldKey> heldKeys;
    std::map<SDL_Keycode, std::pair<Action, int>> heldKeyUi;
    bool dragging = false, held = false, listPointer = false, settingsPointer = false, sheetListPointer = false;
    int seekDirection = 0;
    float seekHold = 0;
    float analogX = 0, analogY = 0;
    Rect listBox{}, settingsBox{12, 192, 924, 560}, sheetListBox{};
    float scroll = 0, scrollFrom = 0, scrollTarget = 0, scrollTime = 1, scrollLimit = 0;
    float settingsScroll = 0, settingsScrollFrom = 0, settingsScrollTarget = 0, settingsScrollTime = 1, settingsLimit = 0;
    float railPopup = 0;
    float sheetListScroll = 0, sheetListLimit = 180;
    mutable std::vector<Row> cachedRows;
    mutable std::vector<LayoutHeader> cachedHeaders;
    mutable float cachedContentHeight = 0;
    mutable float cachedUpNextOffset = 0;
    mutable int cachedArtistAlbumCount = 0;
    mutable unsigned cachedPlayerRevision = 0;
    mutable bool rowsValid = false;
    std::optional<Rect> hitClip;

    std::vector<SortSpec> sortSpecsFor(Tab which) const;
    SortSpec activeSortSpec() const;
    const std::vector<Row>& rows() const;
    void rebuildCatalogCache() const;
    std::vector<Option> options() const;
    int gridColumns() const;
    bool isUiHeld(Action action, int data) const;
    float activePressAmount(Action action, int data, std::optional<Rect> checkBox = std::nullopt) const;
    void tabs(Draw& d);
    void transport(Draw& d);
    void catalog(Draw& d);
    void keypadUi(Draw& d);
    void overlay(Draw& d);
    void audioOutputUi(Draw& d);
    int audioOutputRows() const;
    const BluetoothPairing::Device* audioDevice(int row) const;
    void revealAudioOutputRow();
    void drawScrollbar(Draw& d, Rect rail, float pos, float limit, Action act,
                       const std::vector<std::string>& ticks);
    void button(Draw& d, Rect r, Action action, int value, std::string_view glyph = {},
                std::string_view label = {}, bool focus = false, bool hero = false,
                int iconSize = 32, std::optional<Color> background = std::nullopt,
                std::optional<Color> ink = std::nullopt, int vfdSize = 32, int corner = 14,
                bool marker = false, Size labelSize = Size::Sub, Align glyphAlign = Align::Left);
    void expandingButton(Draw& d, Rect r, Action action, int value, std::string_view glyph,
                         std::string_view label, float expandAnim, bool focus, bool primary = false,
                         int iconSize = 24);
    void hit(Rect r, Action action, int data = 0);
    std::string_view voiceFor(Action action, int data = 0) const;
    void activate(Action action, int data);
    void targetScroll(float target, bool animate = true);
    void targetSettingsScroll(float target, bool animate = true);
    void scrollWithStick(float seconds);
    float expectedCatalogHeight() const;
    float catalogScrollLimit(float viewHeight, float keyboardPad = 0) const;
    CatalogPosition catalogPosition() const;
    void rememberRootPosition();
    void restoreCatalogPosition(const CatalogPosition& position, bool revealItem = false);
    CatalogPosition defaultCatalogPosition() const;
    void keepSelectedVisible();
    void keepSettingsVisible();
    void scrubRail(float y);
    void scrubSettingsRail(float y);
    void scrubOutputRail(float y);
    void scrubPosition(Action action, float x);
    void setVolume(int value);
    void openInput(Input field);
    void closeInput(bool confirm);
    void setTab(int index);
    void settleTab();
    void selectRow(int index);
    void focusQueueCurrent();
    void followQueuePlaybackChange(int removedIndex, bool prepended = false);
    void editQueueEntry(int index, bool remove);
    void openContext(int index, int playlist = -1, QueuePart queuePart = QueuePart::None, int queueIndex = -1);
    void openAlbum(int index);
    void openSheet(Sheet next);
    void cycleStage();
    void toggleLid();
    void power(bool down, bool repeated);
    void back();
    void libraryKey(SDL_Keycode key);
    void sheetKey(SDL_Keycode key);
    void eraseCharacter();
};

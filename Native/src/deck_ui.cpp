#include "deck_ui.hpp"
#include "design_assets.inc"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numbers>
#include <numeric>
#include <random>

// Keep the manual clock controls for when they are needed again.
#define DECKDS_ENABLE_SET_CLOCK 0

namespace {
constexpr std::string_view tabNames[] = {"DECK", "ALBUMS", "ARTISTS", "PLAYLISTS", "SONGS", "QUEUE"};
constexpr std::string_view tabIcons[] = {"nav-play", "album", "artist", "playlists", "tracks", "nav-queue"};
constexpr std::string_view stageIcons[] = {"stage-cover", "stage-vu", "stage-spectrum"};
constexpr std::string_view themeNames[] = {"Obsidian", "Ceramic", "VFD"};
constexpr std::string_view outputIcons[] = {"headphones", "speaker", "bluetooth"};

enum TrackOptId {
    OptNone = 0,
    OptPlayNext,
    OptAddQueue,
    OptGoAlbum,
    OptGoArtist,
    OptStar,
    OptAddPlaylist,
    OptGetInfo,
    OptRemoveQueue,
    OptRemovePlaylist,
    OptShuffle,
    OptRepeat,
};
enum SettingsRow {
    BrightnessRow,
    TimeFormatRow,
#if DECKDS_ENABLE_SET_CLOCK
    SetClockRow,
#endif
    VisualThemeRow,
    AccentRow,
    SfxRow,
    SfxVolumeRow,
    SfxOutputRow,
    LidCloseRow,
    SettingsRowCount,
};
constexpr Rect fullPanel{0, 0, 1024, 768};
constexpr Rect railBox{944, 0, 64, 0};
const std::vector<std::string> dotRailTicks(5, ".");
constexpr float railThumbCenterInset = 34.f;
constexpr int keyColumns = 10;
constexpr Uint64 backHoldMs = 500;
constexpr float queueLabelHold = .500f;
constexpr float queueLabelFlip = .120f;
constexpr float stageFadeSeconds = .060f;
constexpr float progressPhaseSeconds = .080f;
constexpr float stageCenteringSeconds = .050f;
constexpr float catalogTop = 92.f;
constexpr float catalogBottom = 752.f;
constexpr float catalogToolbarHeight = 104.f;
constexpr float catalogInset = 4.f;
constexpr float catalogEndPadding = 16.f;
constexpr float listRowHeight = 116.f;
constexpr float listRowGap = 12.f;
constexpr float listTitleHeight = 44.f;
constexpr float listSubtitleHeight = 36.f;
constexpr float firstSettingsRowHeight = 152.f;
constexpr float settingsRowHeight = 136.f;
constexpr float settingsRowGap = 14.f;
constexpr float sheetOptionPitch = 92.f;
constexpr float audioOutputListHeight = 5 * sheetOptionPitch;
constexpr float trailingGlyphPadding = 12.f;
constexpr float trailingGlyphGap = 4.f;
constexpr float buttonPressShrink = .06f;
constexpr int bluetoothOutput = 2;
constexpr int audioHeaderSelection = -1;

float scrollRailFraction(Rect rail, float y)
{
    return (y - rail.y - railThumbCenterInset) / (rail.h - 2 * railThumbCenterInset);
}

constexpr float settingsRowOffset(int row)
{
    return row == BrightnessRow ? 0.f : firstSettingsRowHeight + settingsRowGap +
                                       (row - 1) * (settingsRowHeight + settingsRowGap);
}

constexpr float settingsContentHeight = settingsRowOffset(SettingsRowCount - 1) + settingsRowHeight;

float rowTextTop(Rect row, bool hasSubtitle)
{
    const float height = listTitleHeight + (hasSubtitle ? listSubtitleHeight : 0.f);
    return row.y + (row.h - height) / 2;
}

const KeyAsset* referenceKey(bool symbols, int position)
{
    for (const auto& key : keyAssets)
        if (key.symbols == symbols && key.row == position / keyColumns &&
            position % keyColumns >= key.column && position % keyColumns < key.column + key.span)
            return &key;
    return nullptr;
}

std::string twoDigits(int value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}

std::string_view stripLeadingArrow(std::string_view text)
{
    if (text.size() > 4 && (text.starts_with("↑ ") || text.starts_with("↓ ")))
        return text.substr(4);
    return text;
}

std::string normalize(std::string_view value)
{
    const std::pair<std::string_view, std::string_view> folds[] = {
        {"ō", "o"}, {"Ō", "o"}, {"ū", "u"}, {"Ū", "u"}, {"é", "e"}, {"è", "e"}, {"ü", "u"}
    };
    std::string text(value);
    for (const auto& [from, to] : folds)
        for (size_t pos = text.find(from); pos != std::string::npos; pos = text.find(from, pos + to.size()))
            text.replace(pos, from.size(), to);
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool matches(std::string_view text, std::string_view query)
{
    if (query.empty())
        return true;
    const auto normalized = normalize(text);
    if (normalized.find(query) != std::string::npos)
        return true;
    if (query.size() <= 2 || !std::ranges::all_of(query, [](unsigned char c) { return c < 128; }))
        return false;
    size_t next = 0;
    for (const char ch : normalized)
        if (ch == query[next] && ++next == query.size())
            return true;
    return false;
}

std::vector<int> randomOrder(std::vector<int> ids)
{
    static std::mt19937 rng(std::random_device{}());
    std::shuffle(ids.begin(), ids.end(), rng);
    return ids;
}

bool confirm(SDL_Keycode key)
{
    return key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_Z;
}

float expApproach(float current, float target, float dt, float duration)
{
    const float factor = 1.f - std::pow(.003f, dt / std::max(.01f, duration));
    const float next = std::lerp(current, target, std::clamp(factor, 0.f, 1.f));
    return std::abs(next - target) < .003f ? target : next;
}
}

bool DeckUi::textInput() const { return input != Input::None; }
bool DeckUi::displaysOff() const { return lid || screenOff || poweredOff; }
float DeckUi::backlight() const { return displaysOff() ? 0.f : .28f + .72f * brightnessLevels[brightness] / 100; }

std::string DeckUi::clockString() const
{
    const auto now = std::time(nullptr);
    int totalMin = 0;
    if (const auto* local = std::localtime(&now))
        totalMin = local->tm_hour * 60 + local->tm_min;
#if DECKDS_ENABLE_SET_CLOCK
    totalMin = ((totalMin + clockOffsetMinutes) % 1440 + 1440) % 1440;
#endif
    const int h24 = totalMin / 60;
    const int m = totalMin % 60;
    if (clock24h)
        return twoDigits(h24) + ":" + twoDigits(m);
    const bool pm = h24 >= 12;
    const int h12 = (h24 % 12 == 0) ? 12 : (h24 % 12);
    return std::to_string(h12) + ":" + twoDigits(m) + (pm ? " PM" : " AM");
}

std::string_view DeckUi::sleepMessage() const
{
    if (poweredOff) return "Powered off / press Power to start";
    if (lid) return "Lid closed / press Lid to reopen";
    if (screenOff) return "Screens off / press Power to wake";
    return {};
}

std::vector<DeckUi::SortSpec> DeckUi::sortSpecsFor(Tab which) const
{
    switch (which) {
    case Tab::Artists:
        return {
            {SortField::Name, "Name", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::AlbumCount, "Album Count", "#",
             "↑ Fewest First", "↓ Most First", false, false},
            {SortField::SongCount, "Song Count", "#",
             "↑ Fewest First", "↓ Most First", false, false},
        };
    case Tab::Songs:
        return {
            {SortField::Added, "Added Time", "#",
             "↑ Oldest First", "↓ Newest First", false, false},
            {SortField::Name, "Name", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Year, "Year", "Year",
             "↑ Oldest First", "↓ Newest First", false, false},
            {SortField::Artist, "Artist", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Album, "Album", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
        };
    default:
        return {
            {SortField::Added, "Added Time", "#",
             "↑ Oldest First", "↓ Newest First", false, false},
            {SortField::Name, "Name", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Year, "Year", "Year",
             "↑ Oldest First", "↓ Newest First", false, false},
            {SortField::Artist, "Artist", "A–Z", "↑ A → Z", "↓ Z → A", true, true},
        };
    }
}

DeckUi::SortSpec DeckUi::activeSortSpec() const
{
    const auto specs = sortSpecsFor(tab);
    for (const auto& s : specs)
        if (s.field == sortField)
            return s;
    return specs.front();
}

void DeckUi::update(double dt)
{
    const auto step = static_cast<float>(dt);
    if (powerDown && !poweredOff && (powerTime += dt) >= 3) {
        powerLong = poweredOff = true;
        screenOff = false;
        if (player.playing)
            player.playToggle();
    }
    if (tab == Tab::Queue)
        rows();
    if (const auto removedIndex = player.update(dt))
        followQueuePlaybackChange(*removedIndex);
    const auto bluetoothState = bluetooth.state();
    const auto deviceCount = bluetooth.devices().size();
    bluetooth.update(dt);
    if (bluetoothState != BluetoothPairing::State::Connected &&
        bluetooth.state() == BluetoothPairing::State::Connected)
        output = bluetoothOutput;
    if (output == bluetoothOutput && bluetooth.connectedDevice().empty())
        output = 0;
    if (sheet == Sheet::Output && bluetooth.devices().size() < deviceCount) {
        // Removing a device can shift the row captured by an input that is still held.
        const auto deviceAction = [](Action action, int row) {
            return row >= bluetoothOutput && (action == Action::Option || action == Action::BluetoothUnpair);
        };
        if (pressed && deviceAction(pressed->action, pressed->data))
            pressed.reset();
        std::erase_if(heldKeyUi, [&](const auto& entry) {
            if (!deviceAction(entry.second.first, entry.second.second))
                return false;
            heldKeys.erase(entry.first);
            return true;
        });
        sheetSelection = std::min(sheetSelection, audioOutputRows() - 1);
        if (keypad)
            revealAudioOutputRow();
    }
    if (!displaysOff())
        rowMarqueeTime += step;
    if (!displaysOff() && (stage != 0 || stageTime < 1))
        meter.update(player, dt);
    stageTime = std::min(1.f, stageTime + step / stageFadeSeconds);
    tabTime = std::min(1.f, tabTime + step / .22f);
    if (tab == Tab::Queue)
        queueLabelTime = std::min(queueLabelHold + queueLabelFlip, queueLabelTime + step);
    const int visualTab = tabSliding ? slideTab : static_cast<int>(tab);
    for (int i = 0; i < 6; ++i)
        tabWidths[i] = std::lerp(tabStart[i], i == visualTab ? 260.f : 108.f, smooth(tabTime));

    if (tabSliding && tabSlideMoved && !tabSlideCommitted) {
        tabSlideDwell += step;
        if (tabSlideDwell >= .18f) {
            tabSlideCommitted = true;
            setTab(slideTab);
            player.chime("deck", true);
        }
    }

    const float progressStep = step / progressPhaseSeconds;
    if (tab == Tab::Deck) {
        const float slideRemaining = std::max(0.f, 1.f - deckProgressTransition) * progressPhaseSeconds;
        deckProgressTransition = std::min(2.f, deckProgressTransition + progressStep);
        const float timeAfterSlide = std::max(0.f, step - slideRemaining);
        stageCentering = std::min(1.f, stageCentering + timeAfterSlide / stageCenteringSeconds);
    } else {
        deckProgressTransition = std::max(0.f, std::min(1.f, deckProgressTransition) - progressStep);
        stageCentering = std::max(0.f, stageCentering - step / stageCenteringSeconds);
    }

    if (scroll > 16.f && utility && !toolFocus && query.empty() && input == Input::None)
        utility = false;

    const bool utilVisible = (utility || !query.empty() || input == Input::Search) &&
                             tab != Tab::Deck && tab != Tab::Queue && details.empty();
    utilityAnim = expApproach(utilityAnim, utilVisible ? 1.f : 0.f, step, .22f);

    const bool fullSearch = !query.empty() || input == Input::Search;
    searchAnim = expApproach(searchAnim, fullSearch ? 1.f : 0.f, step, .22f);

    for (int i = 0; i < 2; ++i) {
        const bool expand = toolFocus && details.empty() && !fullSearch && tab != Tab::Playlists &&
                            toolSelection == (i + 1);
        catBtnAnim[i] = expApproach(catBtnAnim[i], expand ? 1.f : 0.f, step, .24f);
    }
    for (int i = 0; i < 3; ++i) {
        const bool expand = toolFocus && !details.empty() && toolSelection == i;
        detailBtnAnim[i] = expApproach(detailBtnAnim[i], expand ? 1.f : 0.f, step, .24f);
    }

    const float keyTarget = textInput() ? 280.f : 0.f;
    keyboardHeight = expApproach(keyboardHeight, keyTarget, step, .20f);
    if (std::abs(keyboardHeight - keyTarget) < .5f)
        keyboardHeight = keyTarget;

    if (scrollTime < 1) {
        scrollTime = std::min(1.f, scrollTime + step / .20f);
        scroll = std::lerp(scrollFrom, scrollTarget, smooth(scrollTime));
    }
    if (settingsScrollTime < 1) {
        settingsScrollTime = std::min(1.f, settingsScrollTime + step / .20f);
        settingsScroll = std::lerp(settingsScrollFrom, settingsScrollTarget, smooth(settingsScrollTime));
    }

    volumeTime = std::max(0.f, volumeTime - step);
    railPopup = std::max(0.f, railPopup - step);
    releaseTime = std::max(0.f, releaseTime - step);
    if (!heldKeyUi.empty())
        keyPressTime += step;
    else
        keyPressTime = 0;

    if (pressed) {
        pressTime += step;
        if ((pressed->action == Action::Previous || pressed->action == Action::Next) && pressTime >= .32f) {
            held = true;
            player.seekSeconds((pressed->action == Action::Previous ? -1 : 1) * step * 16);
        }
    }
    if (seekDirection != 0) {
        seekHold += step;
        if (seekHold >= .32f)
            player.seekSeconds(seekDirection * step * 16);
    }
    if (std::abs(analogPosition) > .25f && tab == Tab::Deck && sheet == Sheet::None && !displaysOff())
        player.seekSeconds(analogPosition * step * 20);
}

void DeckUi::upper(Draw& d, SDL_Texture* fadeLayer)
{
    d.theme = theme;
    d.accentIndex = currentAccent();
    if (displaysOff()) {
        d.clear(color(0x000000));
        return;
    }
    float title = stage != 0 ? 1.f : 0.f;
    if (stageTime < 1 && fadeLayer && stage != outgoingStage) {
        auto* target = SDL_GetRenderTarget(d.renderer);
        SDL_SetRenderTarget(d.renderer, fadeLayer);
        drawStage(d, player, meter, stage, stageCentering);
        SDL_SetRenderTarget(d.renderer, target);
        drawStage(d, player, meter, outgoingStage, stageCentering);
        const float fade = stageTime * stageTime * (3 - 2 * stageTime);
        d.texture(fadeLayer, fullPanel, fade);
        title = std::lerp(outgoingStage != 0 ? 1.f : 0.f, title, fade);
    } else {
        drawStage(d, player, meter, stage, stageCentering);
    }
    drawTopChrome(d, player, title, clockString(), batteryPercent, wifiConnected, !bluetooth.connectedDevice().empty(),
                  std::min(1.f, deckProgressTransition), stageCentering);
    d.rect(fullPanel, alpha(color(0x000000), 1 - backlight()));
}

void DeckUi::lower(Draw& d)
{
    d.theme = theme;
    d.accentIndex = currentAccent();
    hits.clear();
    if (displaysOff()) {
        d.clear(color(0x000000));
        return;
    }
    d.clear(d.p().bg);
    tabs(d);
    if (tab == Tab::Deck) {
        transport(d);
    } else {
        catalog(d);
        if (keyboardHeight > 1 && sheet == Sheet::None)
            keypadUi(d);
    }
    if (sheet != Sheet::None)
        overlay(d);
    d.rect(fullPanel, alpha(color(0x000000), 1 - backlight()));
}

void DeckUi::hit(Rect r, Action action, int data)
{
    if (hitClip) {
        SDL_FRect visible{};
        if (!SDL_GetRectIntersectionFloat(&*hitClip, &r, &visible))
            return;
        r = visible;
    }
    hits.push_back({r, action, data});
}

bool DeckUi::isUiHeld(Action action, int data) const
{
    for (const auto& [k, pair] : heldKeyUi) {
        (void)k;
        if (pair.first == action && pair.second == data)
            return true;
    }
    return false;
}

float DeckUi::activePressAmount(Action action, int data, std::optional<Rect> checkBox) const
{
    const bool ptrDown = pressed && pressed->action == action && pressed->data == data && !dragging &&
                         (!checkBox || inside(*checkBox, pressed->box.x + pressed->box.w / 2,
                                              pressed->box.y + pressed->box.h / 2));
    if (ptrDown)
        return smooth(pressTime / .09f);
    if (isUiHeld(action, data))
        return smooth(keyPressTime / .09f);
    if (lastAction == action && lastData == data && releaseTime > 0)
        return releaseTime / .11f;
    return 0.f;
}

void DeckUi::button(Draw& d, Rect r, Action action, int value, std::string_view glyph,
                    std::string_view text, bool focus, bool hero, int iconSize,
                    std::optional<Color> background, std::optional<Color> ink, int vfdSize, int corner, bool marker,
                    Size labelSize, Align glyphAlign)
{
    hit(r, action, value);
    const auto& p = d.p();
    const bool down = (pressed && pressed->action == action && pressed->data == value && !dragging &&
                       inside(r, pressed->box.x + pressed->box.w / 2, pressed->box.y + pressed->box.h / 2)) ||
                      isUiHeld(action, value);
    const float amount = activePressAmount(action, value, r);
    const float radius = d.vfd() ? 2 : corner;
    const float scale = 1 - buttonPressShrink * amount;
    const float dx = even((1 - scale) * r.w / 2);
    const float dy = even((1 - scale) * r.h / 2);
    r = {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
    Color fill = background.value_or(hero ? p.raised : p.card);
    if (down)
        fill = mix(fill, p.raised, .55f);
    if (focus || amount > .01f)
        d.strokeRound(inset(r, -4), radius + 4, p.soft, 4);
    if (fill.a || focus || down)
        d.frame(r, radius, fill.a ? fill : p.elevated, focus || down ? p.accent : p.strong, focus ? 4 : 2);
    const auto foreground = ink.value_or(hero ? p.accent : p.text);
    if (glyphAlign == Align::Right && !text.empty()) {
        const float iconX = r.x + r.w - trailingGlyphPadding - iconSize;
        const float margin = trailingGlyphPadding + iconSize + trailingGlyphGap;
        d.label(text, {r.x + margin, r.y, r.w - 2 * margin, r.h},
                labelSize, foreground, Face::Display, Align::Center, true, true);
        if (!glyph.empty())
            d.icon(glyph, {iconX, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
    } else if (!glyph.empty()) {
        if (text.empty()) {
            float x = r.x + even((r.w - iconSize) / 2);
            float y = r.y + even((r.h - iconSize) / 2);
            if (action == Action::Mute) {
                // Center the visible speaker and mute glyphs inside the volume button.
                x -= d.vfd() ? 1.f : 2.5f;
                y -= d.vfd() ? 0.f : 1.f;
            }
            d.icon(glyph, {x, y, static_cast<float>(iconSize), static_cast<float>(iconSize)}, foreground, vfdSize);
        } else if (r.w >= 120) {
            const float textW = std::min(d.textWidth(text, labelSize, Face::Display, true), r.w - iconSize - 26);
            const float x = r.x + even((r.w - iconSize - 8 - textW) / 2);
            d.icon(glyph, {x, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
            d.label(text, {x + iconSize + 8, r.y, textW, r.h},
                    labelSize, foreground, Face::Display, Align::Left, true);
        } else {
            const float x = r.x + even((r.w - iconSize) / 2);
            d.icon(glyph, {x, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
        }
    } else {
        d.label(text, inset(r, 4), labelSize, foreground, Face::Display, Align::Center, true);
    }
    if (hero)
        d.round({r.x + even((r.w - 56) / 2), r.y + r.h - 20, 56, 4}, 2, alpha(p.accent, .85f));
    if (marker)
        d.circle(r.x + even(r.w / 2), r.y + r.h - 16, 4, p.text);
}

void DeckUi::expandingButton(Draw& d, Rect r, Action action, int value, std::string_view glyph,
                             std::string_view text, float expandAnim, bool focus, bool primary, int iconSize)
{
    hit(r, action, value);
    const auto& p = d.p();
    const bool down = (pressed && pressed->action == action && pressed->data == value && !dragging) ||
                      isUiHeld(action, value);
    const float amount = activePressAmount(action, value, r);
    const float radius = d.vfd() ? 2.f : 14.f;
    const float dx = even(.06f * amount * r.w / 2);
    const float dy = even(.06f * amount * r.h / 2);
    r = {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};

    Color fill = primary ? p.accent : p.elevated;
    if (down)
        fill = primary ? mix(p.accent, p.bg, .16f) : p.raised;
    const Color stroke = focus ? (primary ? p.text : p.accent) : down ? p.accent : (primary ? p.accent : p.strong);
    const Color ink = primary ? p.accentInk : p.text;

    if (focus || amount > .01f)
        d.strokeRound(inset(r, -4), radius + 4, p.soft, 4);
    d.frame(r, radius, fill, stroke, focus ? 4 : 2);

    const float labelW = d.textWidth(text, Size::Sub, Face::Display, true);
    const float shownW = (8.f + labelW) * expandAnim;
    const float contentW = iconSize + shownW;
    const float ix = r.x + even((r.w - contentW) / 2);
    const float iy = r.y + even((r.h - iconSize) / 2);
    d.icon(glyph, {ix, iy, static_cast<float>(iconSize), static_cast<float>(iconSize)}, ink, 32);
    if (expandAnim > .12f) {
        const float textOpacity = std::clamp((expandAnim - .12f) / .88f, 0.f, 1.f);
        d.label(text, {ix + iconSize + 8, r.y, std::max(0.f, r.x + r.w - (ix + iconSize + 14)), r.h},
                Size::Sub, alpha(ink, textOpacity), Face::Display, Align::Left, true);
    }
}

void DeckUi::tabs(Draw& d)
{
    const auto& p = d.p();
    d.rect({0, 0, 1024, 76}, p.elevated);
    d.rect({0, 74, 1024, 2}, p.border);
    const int current = static_cast<int>(tab);
    const int visualTab = tabSliding ? slideTab : current;
    constexpr float iconSize = 32.f;
    constexpr float iconGap = 8.f;
    button(d, {16, 12, 64, 52}, Action::Tab, current - 1, {}, "L1", false, false, 32,
           current > 0 ? p.card : p.elevated, current > 0 ? p.dim : alpha(p.dim, .35f));
    button(d, {944, 12, 64, 52}, Action::Tab, current + 1, {}, "R1", false, false, 32,
           current < 5 ? p.card : p.elevated, current < 5 ? p.dim : alpha(p.dim, .35f));
    float x = 92;
    for (int i = 0; i < 6; ++i) {
        const float left = even(x);
        const float width = even(x + tabWidths[i]) - left;
        const Rect box{left, 10, width, 56};
        tabRects[i] = box;
        const bool isActive = i == visualTab;
        const bool isSlipFocus = tabSliding && i == slideTab;
        hit(box, Action::Tab, i);
        const float amount = activePressAmount(Action::Tab, i, box);
        const float dx = even(.05f * amount * box.w / 2);
        const float dy = even(.05f * amount * box.h / 2);
        const Rect vr{box.x + dx, box.y + dy, box.w - 2 * dx, box.h - 2 * dy};
        if (isActive) {
            const std::string_view tabLabel = tabNames[i];
            d.frame(vr, d.vfd() ? 2 : 14, isSlipFocus ? p.raised : p.card,
                    (isSlipFocus || d.vfd()) ? p.accent : p.strong);
            const float nameW = std::min(d.textWidth(tabLabel, Size::Sub, Face::Display, true), vr.w - 60);
            const bool showText = vr.w >= 170;
            const bool queueTab = i == static_cast<int>(Tab::Queue) && tab == Tab::Queue;
            const float flip = queueTab ? std::clamp((queueLabelTime - queueLabelHold) / queueLabelFlip, 0.f, 1.f) : 0.f;
            const bool showCount = flip >= .5f;
            std::string remaining;
            float countW = nameW;
            if (flip > 0.f) {
                remaining = std::to_string(player.queue.size());
                countW = std::min(d.textWidth(remaining, Size::Sub, Face::Display, true), vr.w - 60);
            }
            const float textW = std::lerp(nameW, countW, smooth(flip));
            const float ix = vr.x + even((vr.w - (showText ? iconSize + iconGap + textW : iconSize)) / 2);
            d.icon(tabIcons[i], {ix, vr.y + (vr.h - iconSize) / 2, iconSize, iconSize}, p.accent);
            if (showText) {
                const float fade = std::clamp((vr.w - 170.f) / 70.f, 0.f, 1.f);
                const float textX = ix + iconSize + iconGap;
                float y = vr.y;
                float verticalScale = 1.f;
                if (flip > 0.f && flip < 1.f) {
                    const float angle = flip * std::numbers::pi_v<float>;
                    // The old face rotates up and the new face rises from below.
                    y += (showCount ? 8.f : -8.f) * std::sin(angle);
                    verticalScale = std::abs(std::cos(angle));
                }
                const std::string_view label = showCount ? std::string_view(remaining) : tabLabel;
                d.label(label, {textX, y, (showCount ? countW : nameW) + 4, vr.h}, Size::Sub,
                        alpha(p.text, fade), Face::Display, Align::Left, true, false, 0, -1.f, verticalScale);
            }
        } else {
            if (amount > .01f)
                d.frame(vr, d.vfd() ? 2 : 14, p.elevated, p.accent);
            d.icon(tabIcons[i], {vr.x + even((vr.w - iconSize) / 2), vr.y + (vr.h - iconSize) / 2,
                               iconSize, iconSize}, p.dim);
        }
        x += tabWidths[i] + 8;
    }
}

void DeckUi::transport(Draw& d)
{
    const auto& p = d.p();
    const auto* track = player.track();

    d.frame({16, 92, 992, 240}, d.vfd() ? 2 : 16, p.card, p.border);
    const std::string elapsed = timecode(player.seconds);
    const std::string duration = timecode(track ? track->duration : 0);
    const std::string total = "/ " + duration;
    const float elapsedW = even(2 + std::max(d.textWidth(elapsed, Size::Body, Face::Mono, true),
                                             d.textWidth(duration, Size::Body, Face::Mono, true)));
    const float totalW = even(2 + d.textWidth(total, Size::Body, Face::Mono, true));
    const float totalX = 778 - totalW;
    const float elapsedX = totalX - 12 - elapsedW;
    constexpr float titleX = 38;
    if (track)
        d.label(track->title, {titleX, 108, elapsedX - titleX - 12, 48}, Size::Body, p.text,
                Face::Display, Align::Left, true, true);
    d.label(elapsed, {elapsedX, 108, elapsedW, 48}, Size::Body, p.accent, Face::Mono, Align::Right, true);
    d.label(total, {totalX, 108, totalW, 48}, Size::Body, p.muted, Face::Mono, Align::Right, true);
    const auto shortcutButton = [&](Rect r, Action act, int data, std::string_view glyph, std::string_view keycap,
                                    float keycapWidth, int iconSize, int vfdSize, Color fill, int corner) {
        hit(r, act, data);
        const bool down = (pressed && pressed->action == act && pressed->data == data && !dragging) ||
                          isUiHeld(act, data);
        const float amount = activePressAmount(act, data, r);
        const float dx = even(.06f * amount * r.w / 2);
        const float dy = even(.06f * amount * r.h / 2);
        r = {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
        const float radius = d.vfd() ? 2.f : static_cast<float>(corner);
        if (amount > .01f)
            d.strokeRound(inset(r, -4), radius + 4, p.soft, 4);
        d.frame(r, radius, down ? mix(fill, p.raised, .55f) : fill, down ? p.accent : p.strong);

        constexpr float gap = 8;
        constexpr float keycapHeight = 26;
        const float size = static_cast<float>(d.vfd() ? vfdSize : iconSize);
        const bool cornerKeycap = act == Action::Stage;
        const float contentWidth = cornerKeycap ? size : size + gap + keycapWidth;
        const float x = r.x + (r.w - contentWidth) / 2;
        d.icon(glyph, {x, r.y + (r.h - size) / 2, size, size}, p.text, vfdSize);
        const Rect keycapBox = cornerKeycap
            ? Rect{r.x + r.w - keycapWidth - gap, r.y + gap, keycapWidth, keycapHeight}
            : Rect{x + size + gap, r.y + (r.h - keycapHeight) / 2, keycapWidth, keycapHeight};
        d.icon(keycap, keycapBox, p.dim, 0);
    };
    shortcutButton({790, 108, 84, 48}, Action::Context, player.current, "more", "keycap-y", 26, 24, 32, p.elevated, 12);
    shortcutButton({886, 108, 100, 48}, Action::Settings, 0, "settings", "keycap-sel", 46, 24, 32, p.elevated, 12);
    drawWaveform(d, player, deckWave, smooth(deckProgressTransition - 1.f));
    hit(deckWave, Action::Scrub);

    button(d, {16, 348, 172, 172}, Action::Previous, 0, "prev", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);
    button(d, {204, 348, 428, 172}, Action::Play, 0, player.playing ? "pause" : "play",
           {}, false, true, 56, std::nullopt, std::nullopt, 64, 18);
    button(d, {648, 348, 172, 172}, Action::Next, 0, "next", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);
    button(d, {836, 348, 172, 172}, Action::Star, player.current,
           track && player.stars[player.current] ? "star-filled" : "star", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);

    d.frame({16, 536, 992, 94}, d.vfd() ? 2 : 16, p.card, p.border);
    const bool isMuted = player.mute || player.volume == 0;
    button(d, {36, 554, 58, 58}, Action::Mute, 0, isMuted ? "speaker-mute" : "speaker", {},
           false, false, 32, p.elevated);
    button(d, {930, 554, 58, 58}, Action::Output, 0, outputIcons[output], {},
           false, false, 32, p.elevated);
    d.frame(volumeTrack, d.vfd() ? 2 : 12, p.bg, isMuted ? alpha(p.strong, .5f) : p.strong);
    const int effVol = isMuted ? 0 : player.volume;
    const float thumb = volumeTrack.x + 22 + even((volumeTrack.w - 44) * effVol / 100.f);
    if (d.vfd()) {
        for (float x = volumeTrack.x + 4; x < volumeTrack.x + volumeTrack.w - 4; x += 8)
            d.rect({x, volumeTrack.y + 6, 6, 12}, x < thumb && !isMuted ? p.dim : p.wave);
    }
    d.frame({thumb - 22, 562, 44, 44}, d.vfd() ? 2 : 12, isMuted ? p.muted : p.dim, p.card);
    hit({volumeTrack.x, 550, volumeTrack.w, 68}, Action::Volume);
    const bool adjustingVolume = pressed && pressed->action == Action::Volume;
    if (adjustingVolume || volumeTime > 0) {
        const float opacity = adjustingVolume ? 1.f : std::min(1.f, volumeTime / .16f);
        const Rect bubble{thumb - 40, volumeTrack.y - 64 + even(8 * (1 - opacity)), 80, 52};
        d.frame(bubble, d.vfd() ? 2 : 14, alpha(p.elevated, opacity), alpha(p.strong, opacity));
        d.label(std::to_string(effVol), bubble, Size::Body, alpha(p.text, opacity), Face::Mono, Align::Center, true);
    }

    constexpr int quickPadIconSize = 48;
    shortcutButton({16, 646, 236, 104}, Action::Stage, 0, stageIcons[stage], "keycap-x", 26,
                   quickPadIconSize, quickPadIconSize, p.card, 14);
    button(d, {268, 646, 236, 104}, Action::Shuffle, 0, "shuffle", {}, false, false, quickPadIconSize,
           std::nullopt, player.shuffle ? p.text : p.muted, quickPadIconSize, 14, player.shuffle);
    button(d, {520, 646, 236, 104}, Action::Repeat, 0, player.repeat == 2 ? "repeat-one" : "repeat", {},
           false, false, quickPadIconSize, std::nullopt, player.repeat ? p.text : p.muted,
           quickPadIconSize, 14, player.repeat != 0);
    button(d, {772, 646, 236, 104}, Action::Album, player.current, "goto-album", {},
           false, false, quickPadIconSize, std::nullopt, p.dim, quickPadIconSize);
}

const std::vector<DeckUi::Row>& DeckUi::rows() const
{
    if (!rowsValid || cachedPlayerRevision != player.catalogRevision) {
        rebuildCatalogCache();
        cachedPlayerRevision = player.catalogRevision;
        rowsValid = true;
    }
    return cachedRows;
}

void DeckUi::rebuildCatalogCache() const
{
    cachedRows.clear();
    cachedHeaders.clear();
    cachedContentHeight = 0;
    cachedUpNextOffset = 0;
    cachedArtistAlbumCount = 0;

    const auto addTrack = [&](int index, int playlist = -1, bool albumMode = false) {
        const auto& t = player.tracks[index];
        Row r;
        r.title = std::string(t.title);
        r.subtitle = std::string(t.artist) + " — " + std::string(t.album);
        r.ids = {index};
        r.cover = index;
        r.track = index;
        r.source = Tab::Songs;
        r.playlist = playlist;
        r.year = t.year;
        r.added = std::string(t.added);
        r.albumTrack = albumMode;
        cachedRows.push_back(std::move(r));
    };
    const auto addGroup = [&](int index, Tab type) {
        const auto& t = player.tracks[index];
        const std::string_view name = type == Tab::Albums ? t.album : t.artist;
        auto match = std::ranges::find(cachedRows, name, &Row::title);
        if (match == cachedRows.end()) {
            Row r;
            r.title = std::string(name);
            r.subtitle = type == Tab::Albums ? std::string(t.artist) : "";
            r.ids = {index};
            r.cover = index;
            r.track = -1;
            r.source = type;
            r.albumCount = 1;
            r.year = t.year;
            r.added = std::string(t.added);
            cachedRows.push_back(std::move(r));
        } else {
            if (type == Tab::Artists) {
                const bool seenAlbum = std::ranges::any_of(match->ids, [&](int prevIdx) {
                    return player.tracks[prevIdx].album == t.album;
                });
                if (!seenAlbum)
                    ++match->albumCount;
            }
            match->ids.push_back(index);
            if (t.added > match->added)
                match->added = std::string(t.added);
        }
    };

    if (!details.empty()) {
        const auto& detail = details.back().row;
        if (detail.source == Tab::Artists) {
            for (const int index : detail.ids)
                addGroup(index, Tab::Albums);
            cachedArtistAlbumCount = static_cast<int>(cachedRows.size());
            const int maxSongs = std::min<int>(60, static_cast<int>(detail.ids.size()));
            for (int i = 0; i < maxSongs; ++i)
                addTrack(detail.ids[i], -1, false);
        } else if (detail.source == Tab::Albums) {
            std::vector<int> sortedIds = detail.ids;
            std::stable_sort(sortedIds.begin(), sortedIds.end(), [&](int a, int b) {
                const auto& ta = player.tracks[a];
                const auto& tb = player.tracks[b];
                if (ta.disc != tb.disc) return ta.disc < tb.disc;
                return ta.number < tb.number;
            });
            for (const int index : sortedIds)
                addTrack(index, -1, true);
        } else {
            for (const int index : detail.ids)
                addTrack(index, detail.playlist, false);
        }
    } else if (tab == Tab::Queue) {
        float y = 0.f;
        const auto header = [&](std::string_view name) {
            cachedHeaders.push_back({y, std::string(name), false});
            y += 34.f;
        };
        const auto queueRow = [&](int track, QueuePart part, int index = -1) {
            addTrack(track);
            auto& row = cachedRows.back();
            row.queuePart = part;
            row.queueIndex = index;
            row.box = {16.f, y, 916.f, listRowHeight};
            y += listRowHeight + listRowGap;
        };
        if (!player.played.empty()) {
            header("PLAYED");
            for (const int track : player.played)
                queueRow(track, QueuePart::Played);
        }
        if (player.track())
            queueRow(player.current, QueuePart::Current);
        cachedUpNextOffset = y;
        header("UP NEXT");
        for (int i = 0; i < static_cast<int>(player.queue.size()); ++i)
            queueRow(player.queue[i], QueuePart::UpNext, i);
        if (player.queue.empty()) {
            Row& empty = cachedRows.emplace_back();
            empty.queuePart = QueuePart::EmptyUpNext;
            empty.box = {16.f, y, 916.f, listRowHeight};
            y += listRowHeight + listRowGap;
        }
        cachedContentHeight = std::max(0.f, y - listRowGap);
        return;
    } else if (tab == Tab::Playlists) {
        Row starred;
        starred.title = std::string("Starred");
        starred.source = Tab::Playlists;
        starred.playlist = -1;
        for (int i = 0; i < player.librarySize(); ++i)
            if (player.stars[i])
                starred.ids.push_back(i);
        if (!starred.ids.empty())
            starred.cover = starred.ids.front();
        cachedRows.push_back(std::move(starred));
        for (size_t i = 0; i < player.playlists.size(); ++i) {
            const auto& list = player.playlists[i];
            Row r;
            r.title = list.name;
            r.ids = list.tracks;
            r.cover = list.tracks.empty() ? -1 : list.tracks.front();
            r.source = Tab::Playlists;
            r.playlist = static_cast<int>(i);
            cachedRows.push_back(std::move(r));
        }
    } else if (tab != Tab::Deck) {
        for (int i = 0; i < player.librarySize(); ++i) {
            if (tab == Tab::Songs)
                addTrack(i);
            else
                addGroup(i, tab);
        }
    }

    if (details.empty() && tab != Tab::Queue && tab != Tab::Playlists && tab != Tab::Deck) {
        std::stable_sort(cachedRows.begin(), cachedRows.end(), [&](const Row& a, const Row& b) {
            int cmp = 0;
            const auto& ta = player.tracks[std::max(0, a.cover)];
            const auto& tb = player.tracks[std::max(0, b.cover)];
            switch (sortField) {
            case SortField::Added:
                cmp = a.added.compare(b.added);
                break;
            case SortField::Year:
                cmp = (a.year != b.year) ? (a.year < b.year ? -1 : 1) : 0;
                break;
            case SortField::Artist:
                cmp = normalize(ta.artist).compare(normalize(tb.artist));
                break;
            case SortField::Album:
                cmp = normalize(ta.album).compare(normalize(tb.album));
                if (cmp == 0)
                    cmp = (ta.disc != tb.disc) ? (ta.disc < tb.disc ? -1 : 1)
                        : (ta.number != tb.number ? (ta.number < tb.number ? -1 : 1) : 0);
                break;
            case SortField::AlbumCount:
                cmp = (a.albumCount != b.albumCount) ? (a.albumCount < b.albumCount ? -1 : 1) : 0;
                break;
            case SortField::SongCount:
                cmp = (a.ids.size() != b.ids.size()) ? (a.ids.size() < b.ids.size() ? -1 : 1) : 0;
                break;
            case SortField::Name:
                break;
            }
            if (cmp == 0)
                cmp = normalize(a.title).compare(normalize(b.title));
            return ascending ? (cmp < 0) : (cmp > 0);
        });
    }

    if (details.empty() && !query.empty()) {
        const auto folded = normalize(query);
        std::erase_if(cachedRows, [&](const Row& row) {
            if (matches(row.title + " " + row.subtitle, folded))
                return false;
            return std::ranges::none_of(row.ids, [&](int i) {
                const auto& track = player.tracks[i];
                return matches(std::string(track.title) + " " + std::string(track.artist) + " " +
                               std::string(track.album), folded);
            });
        });
    }

    // Compute exact per-item layout coordinates (supports 3-col grid, 1-col lists,
    // Album Detail multi-disc dividers, and Artist Detail 2-col Albums + 1-col Songs!)
    if (cachedRows.empty())
        return;

    if (!details.empty()) {
        const auto& detail = details.back().row;
        if (detail.source == Tab::Artists) {
            float curY = 0;
            if (cachedArtistAlbumCount > 0) {
                cachedHeaders.push_back({curY, std::string("ALBUMS (") +
                                         std::to_string(cachedArtistAlbumCount) + ")", false});
                curY += 34;
                for (int i = 0; i < cachedArtistAlbumCount; ++i) {
                    const float x = 16.f + (i % 2) * (450.f + 16.f);
                    const float y = curY + (i / 2) * (160.f + 16.f);
                    cachedRows[i].box = {x, y, 450.f, 160.f};
                }
                const int albRows = (cachedArtistAlbumCount + 1) / 2;
                curY += albRows * 176.f - 16.f + 18.f;
            }
            const int songCount = static_cast<int>(cachedRows.size()) - cachedArtistAlbumCount;
            if (songCount > 0) {
                cachedHeaders.push_back({curY, std::string("SONGS (") +
                                         std::to_string(detail.ids.size()) + ")", false});
                curY += 34;
                for (int i = cachedArtistAlbumCount; i < static_cast<int>(cachedRows.size()); ++i) {
                    cachedRows[i].box = {16.f, curY, 916.f, listRowHeight};
                    curY += listRowHeight + listRowGap;
                }
                curY -= listRowGap;
            }
            cachedContentHeight = curY;
            return;
        }
        if (detail.source == Tab::Albums) {
            bool multiDisc = false;
            const int firstDisc = player.tracks[cachedRows.front().track].disc;
            for (const auto& r : cachedRows)
                if (player.tracks[r.track].disc != firstDisc)
                    multiDisc = true;
            float curY = 0;
            int lastDisc = -1;
            for (auto& r : cachedRows) {
                const int dNo = player.tracks[r.track].disc;
                if (multiDisc && dNo != lastDisc) {
                    lastDisc = dNo;
                    if (curY > 0) curY += 6;
                    cachedHeaders.push_back({curY, "DISC " + std::to_string(dNo), true});
                    curY += 36;
                }
                r.box = {16.f, curY, 916.f, 92.f};
                curY += 92.f + listRowGap;
            }
            cachedContentHeight = std::max(0.f, curY - listRowGap);
            return;
        }
        // Playlist detail uses the same rows as the Songs tab.
        float curY = 0;
        for (auto& r : cachedRows) {
            r.box = {16.f, curY, 916.f, listRowHeight};
            curY += listRowHeight + listRowGap;
        }
        cachedContentHeight = std::max(0.f, curY - listRowGap);
        return;
    }

    const int cols = gridColumns();
    const float rowH = cols == 3 ? 368.f : listRowHeight;
    const float gap = cols == 3 ? 14.f : listRowGap;
    const float cellW = cols == 3 ? 296.f : 916.f;
    for (int i = 0; i < static_cast<int>(cachedRows.size()); ++i) {
        cachedRows[i].box = {16.f + (i % cols) * (cellW + gap), (i / cols) * (rowH + gap), cellW, rowH};
    }
    const int totalRows = (static_cast<int>(cachedRows.size()) + cols - 1) / cols;
    cachedContentHeight = std::max(0.f, totalRows * (rowH + gap) - gap);
}

int DeckUi::gridColumns() const
{
    if (!details.empty())
        return details.back().row.source == Tab::Artists ? 2 : 1;
    return tab == Tab::Albums && albumsInGrid ? 3 : 1;
}

void DeckUi::drawScrollbar(Draw& d, Rect rail, float pos, float limit, Action act,
                           const std::vector<std::string>& ticks)
{
    const auto& p = d.p();
    if (rail.h <= 104) {
        d.frame(rail, d.vfd() ? 2 : 12, p.elevated, p.border);
        return;
    }
    const float ratio = limit > 0 ? std::clamp(pos / limit, 0.f, 1.f) : 0.f;
    const bool draggingThis = pressed && pressed->action == act;
    d.frame(rail, d.vfd() ? 2 : 16, draggingThis ? p.elevated : p.card, draggingThis ? p.accent : p.strong);
    const float thumbTravel = rail.h - 2 * railThumbCenterInset;
    const Rect thumb{rail.x + 8, rail.y + railThumbCenterInset - 22 + even(thumbTravel * ratio), 48, 44};
    d.frame(thumb, d.vfd() ? 2 : 10, mix(p.card, p.accent, limit > 0 ? .20f : .07f),
            limit > 0 ? p.accent : mix(p.strong, p.accent, .35f));
    for (size_t i = 0; i < ticks.size(); ++i) {
        const float fraction = static_cast<float>(i) / std::max<size_t>(1, ticks.size() - 1);
        if (ticks[i] == ".") {
            const float y = rail.y + railThumbCenterInset + even(thumbTravel * fraction);
            d.circle(rail.x + rail.w / 2, y, 3, p.dim);
        } else {
            const float y = rail.y + 10 + even((rail.h - 40) * fraction);
            d.label(ticks[i], {rail.x, y, rail.w, 24}, Size::Caption, p.dim, Face::Mono, Align::Center, true);
        }
    }
    hit(rail, act);
}

void DeckUi::catalog(Draw& d)
{
    const auto& p = d.p();
    const auto& entries = rows();
    const bool root = details.empty();
    const bool isQueue = tab == Tab::Queue;
    const bool searchable = root && !isQueue;
    const float top = catalogTop + (root ? (searchable ? even(utilityAnim * catalogToolbarHeight) : 0.f)
                                         : catalogToolbarHeight);
    const float maxBottom = catalogBottom;
    listBox = {12.f, top, 924.f, std::max(0.f, maxBottom - top)};
    const float oskPad = (input == Input::Search && sheet == Sheet::None && keyboardHeight > 1.f)
                       ? (keyboardHeight + 16.f) : 0.f;
    scrollLimit = catalogScrollLimit(listBox.h, oskPad);
    scroll = std::clamp(scroll, 0.f, scrollLimit);
    scrollTarget = std::clamp(scrollTarget, 0.f, scrollLimit);
    selection = std::clamp(selection, 0, std::max(0, static_cast<int>(entries.size()) - 1));
    const float yPos = even(scroll);
    const bool clearView = input == Input::None && sheet == Sheet::None && !showKeys;
    const Row* highlighted = nullptr;
    if (clearView && !entries.empty()) {
        const auto& row = entries[selection];
        const float y = top + catalogInset + row.box.y - yPos;
        const bool focused = keypad ? !toolFocus : row.track >= 0 && row.track == touchSelectedTrack;
        if (focused && row.queuePart != QueuePart::EmptyUpNext && y + row.box.h > top && y < maxBottom)
            highlighted = &row;
    }
    if (!highlighted || marqueeRow != selection || marqueeTitle != highlighted->title) {
        marqueeRow = highlighted ? selection : -1;
        marqueeTitle = highlighted ? highlighted->title : std::string{};
        rowMarqueeTime = 0.f;
    }
    if (!root) {
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 68, 68}, Action::Back, 0, "back", {}, false, false, 32, p.elevated);
        const auto& detail = details.back().row;

        // Right-aligned animated Shuffle / Play / Queue action buttons
        const std::string_view labels[3] = {"Shuffle", "Play", "Queue"};
        const std::string_view icons[3] = {"shuffle", "play", "queue-add"};
        const Action actions[3] = {Action::ListShuffle, Action::ListPlay, Action::DetailQueue};
        float rightEdge = 994.f;
        std::array<Rect, 3> btnBoxes{};
        for (int k = 2; k >= 0; --k) {
            const float lblW = d.textWidth(labels[k], Size::Sub, Face::Display, true);
            const float w = 64.f + even((8.f + lblW + 16.f) * detailBtnAnim[k]);
            rightEdge -= w;
            btnBoxes[k] = {rightEdge, 104.f, w, 64.f};
            rightEdge -= 10.f;
        }
        for (int k = 0; k < 3; ++k)
            expandingButton(d, btnBoxes[k], actions[k], 0, icons[k], labels[k],
                            detailBtnAnim[k], toolFocus && toolSelection == k,
                            k == 1 && detail.source == Tab::Playlists, 32);

        float titleX = 114.f;
        std::string headerTitle = detail.playlist < 0 && detail.source == Tab::Playlists
                                ? std::string("Starred") : detail.title;
        std::string headerSub = detail.subtitle;
        if (detail.source == Tab::Albums && detail.cover >= 0) {
            d.frame({110, 97, 78, 78}, d.vfd() ? 2 : 12, p.card, p.strong);
            d.art(&player.tracks[detail.cover], {112, 99, 74, 74}, 10);
            titleX = 202.f;
            const auto& ct = player.tracks[detail.cover];
            headerSub = std::string(ct.artist) + " (" + std::to_string(ct.year) + ") — " +
                        std::to_string(detail.ids.size()) +
                        (detail.ids.size() == 1 ? " track" : " tracks");
        } else if (detail.source == Tab::Playlists) {
            const Rect tile{110, 99, 74, 74};
            d.frame(tile, d.vfd() ? 2 : 12, p.elevated, p.strong);
            d.icon(detail.playlist < 0 ? "star-filled" : "playlists",
                   {tile.x + 21, tile.y + 21, 32, 32}, p.accent);
            titleX = 200.f;
            int totalSec = 0;
            for (const int id : detail.ids)
                totalSec += player.tracks[id].duration;
            headerSub = std::to_string(detail.ids.size()) +
                        (detail.ids.size() == 1 ? " song — " : " songs — ") + timecode(totalSec);
        } else if (detail.source == Tab::Artists) {
            headerSub = std::to_string(cachedArtistAlbumCount) +
                        (cachedArtistAlbumCount == 1 ? " Album, " : " Albums, ") +
                        std::to_string(detail.ids.size()) + (detail.ids.size() == 1 ? " Song" : " Songs");
        }
        const float maxTextW = std::max(0.f, btnBoxes[0].x - titleX - 16.f);
        d.label(headerTitle, {titleX, 102, maxTextW, 38}, Size::Body, p.text,
                Face::Display, Align::Left, true, true);
        d.label(headerSub, {titleX, 144, maxTextW, 26}, Size::Caption, p.dim,
                Face::Mono, Align::Left, false, true, 600);
    } else if (searchable && utilityAnim > .01f) {
        const bool fullSearch = !query.empty() || input == Input::Search;
        const float searchWidth = even(std::lerp(320.f, 960.f, searchAnim));
        const float reveal = even(utilityAnim * 96.f);
        const float barY = 92.f - (88.f - std::min(88.f, reveal));
        const float y = barY + 14.f;
        d.clip({8, 86, 1008, reveal + 10});
        hitClip = Rect{0, 92, 1024, std::min(88.f, reveal)};
        d.frame({16, barY, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);

        const Rect search{32, y, searchWidth, 60};
        const bool searchFocus = input == Input::Search || (toolFocus && toolSelection == 0);
        if (searchFocus)
            d.strokeRound(inset(search, -4), d.vfd() ? 6 : 18, p.soft, 4);
        d.frame(search, d.vfd() ? 2 : 14, p.elevated, searchFocus ? p.accent : p.strong, searchFocus ? 4 : 2);
        d.icon("search", {48, y + 18, 24, 24}, p.dim);
        const std::string_view hint = tab == Tab::Songs ? "Search songs…"
                                    : tab == Tab::Albums ? "Search albums…"
                                    : tab == Tab::Artists ? "Search artists…"
                                    : "Search playlists…";
        const float closeSpace = searchAnim > .6f ? 52.f : 0.f;
        const auto countText = std::to_string(entries.size());
        const float badgeW = std::max(54.f, even(d.textWidth(countText, Size::Sub, Face::Mono, true) + 22.f));
        const Rect badge{search.x + search.w - 10.f - closeSpace - badgeW, y + 12, badgeW, 36};
        d.label(query.empty() ? hint : std::string_view(query), {84, y, std::max(0.f, badge.x - 94), 60},
                Size::Sub, query.empty() ? p.muted : p.text, Face::Display, Align::Left, false, true, 600);
        d.frame(badge, d.vfd() ? 2 : 8, p.card, p.border);
        d.label(countText, badge, Size::Sub, p.dim, Face::Mono, Align::Center, true);
        hit(search, Action::Search);

        if (searchAnim > .65f) {
            button(d, {search.x + search.w - 52, y + 9, 42, 42}, Action::ClearSearch, 0, {}, "×",
                   toolFocus && fullSearch && toolSelection == 1, false, 24, p.card, p.text, 24, 10);
        }
        if (searchAnim < .35f) {
            float rightX = 992.f;
            if (tab == Tab::Playlists) {
                const Rect newPl{rightX - 216.f, y, 216.f, 60.f};
                button(d, newPl, Action::NewPlaylist, 0, "playlist-add", "New Playlist",
                       toolFocus && toolSelection == 1, false, 24, p.elevated, p.text);
            } else {
                const auto spec = activeSortSpec();
                const std::string sortText = std::string(spec.label) + (ascending ? " ↑" : " ↓");
                const float sortW = std::clamp(even(40.f + 24.f + 10.f + d.textWidth(sortText, Size::Sub, Face::Display, true)),
                                               160.f, 260.f);
                rightX -= sortW;
                button(d, {rightX, y, sortW, 60}, Action::Order, 0, "sort", sortText,
                       toolFocus && toolSelection == (tab == Tab::Albums ? 5 : 3), false, 24, p.elevated, p.text);
                rightX -= 14.f;

                if (tab == Tab::Albums) {
                    const Rect group{rightX - 122.f, y, 122.f, 60.f};
                    d.frame(group, d.vfd() ? 2 : 14, p.elevated, p.strong);
                    const Color activeSeg = d.vfd() ? p.border :
                                            theme == Theme::Ceramic ? color(0xd4cdc0) : color(0x323745);
                    const auto segBtn = [&](Rect sb, int dir, std::string_view ic, bool activeMode, bool focused) {
                        hit(sb, Action::Grid, dir);
                        const float amt = activePressAmount(Action::Grid, dir, sb);
                        if (amt > .01f) sb = inset(sb, 2);
                        if (focused) d.strokeRound(inset(sb, -4), d.vfd() ? 4 : 14, p.soft, 4);
                        if (activeMode || focused)
                            d.frame(sb, d.vfd() ? 2 : 10, activeMode ? activeSeg : p.elevated,
                                    focused ? p.accent : p.strong, focused ? 4 : 2);
                        d.icon(ic, {sb.x + even((sb.w - 24) / 2), sb.y + even((sb.h - 24) / 2), 24, 24},
                               activeMode || focused ? p.text : p.muted, 32);
                    };
                    segBtn({group.x + 4, y + 6, 54, 48}, 1, "view-grid", albumsInGrid, toolFocus && toolSelection == 3);
                    segBtn({group.x + 64, y + 6, 54, 48}, -1, "view-list", !albumsInGrid, toolFocus && toolSelection == 4);
                    rightX = group.x - 14.f;
                }

                const std::string_view playLbl = "Play";
                const std::string_view shufLbl = "Shuffle";
                const float playLblW = d.textWidth(playLbl, Size::Sub, Face::Display, true);
                const float shufLblW = d.textWidth(shufLbl, Size::Sub, Face::Display, true);
                const float playW = 60.f + even((8.f + playLblW + 14.f) * catBtnAnim[1]);
                const float shufW = 60.f + even((8.f + shufLblW + 14.f) * catBtnAnim[0]);
                rightX -= playW;
                expandingButton(d, {rightX, y, playW, 60}, Action::ListPlay, 0, "play", playLbl,
                                catBtnAnim[1], toolFocus && toolSelection == 2, false, 24);
                rightX -= (8.f + shufW);
                expandingButton(d, {rightX, y, shufW, 60}, Action::ListShuffle, 0, "shuffle", shufLbl,
                                catBtnAnim[0], toolFocus && toolSelection == 1, false, 24);
            }
        }
        d.unclip();
        hitClip.reset();
    }

    d.clip(listBox);
    hitClip = listBox;

    for (const auto& hdr : cachedHeaders) {
        const float hy = top + catalogInset + hdr.y - yPos;
        if (hy + 32.f <= top || hy >= maxBottom)
            continue;
        if (hdr.discDivider) {
            const float tw = even(d.textWidth(hdr.text, Size::Caption, Face::Mono, true));
            d.label(hdr.text, {22, hy + 4, tw + 8, 26}, Size::Caption, p.accent, Face::Mono, Align::Left, true);
            d.rect({22 + tw + 16, hy + 16, std::max(0.f, 926.f - (22 + tw + 16)), 2}, p.border);
        } else {
            d.label(hdr.text, {20, hy + 4, 500, 26}, Size::Caption, p.muted, Face::Mono, Align::Left, true);
        }
    }

    if (entries.empty()) {
        const std::string_view emptyMsg = !query.empty() ? "No matching tracks"
            : (!root && details.back().row.source == Tab::Playlists && details.back().row.playlist < 0)
                ? "No starred songs yet. Star any track to see it here."
            : "Playlist is empty. Press Y on any track to add.";
        d.label(emptyMsg, {16, top, 914, listBox.h - oskPad}, Size::Body, p.muted, Face::Display, Align::Center);
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& row = entries[i];
        const Rect cell{row.box.x, top + catalogInset + row.box.y - yPos, row.box.w, row.box.h};
        if (cell.y + cell.h <= top || cell.y >= maxBottom)
            continue;
        if (row.queuePart == QueuePart::EmptyUpNext) {
            d.label("Up Next is empty", cell, Size::Body, p.muted,
                    Face::Display, Align::Center);
            continue;
        }
        const bool focus = keypad && selection == i && !toolFocus && input == Input::None && sheet == Sheet::None;
        const float marqueeTime = i == marqueeRow ? rowMarqueeTime : -1.f;
        const auto labelTitle = [&](Rect box) {
            d.label(row.title, box, Size::Title, p.text, Face::Display, Align::Left, true, true, 0, marqueeTime);
        };
        const bool down = (pressed && pressed->action == Action::Row && pressed->data == i && !dragging) ||
                          isUiHeld(Action::Row, i);
        const float amount = activePressAmount(Action::Row, i);
        const Rect interactive{cell.x, std::max(cell.y, top), cell.w,
                               std::min(cell.y + cell.h, maxBottom) - std::max(cell.y, top)};
        hit(interactive, Action::Row, i);
        Rect visual = cell;
        if (amount > .01f)
            visual = inset(visual, even(amount * 4.f));
        if (focus)
            d.strokeRound(inset(visual, -4), d.vfd() ? 6 : 18, p.soft, 4);
        const auto fill = down ? mix(p.card, p.accent, .18f) : p.card;
        const bool gridCard = cell.w < 600.f;
        d.frame(visual, d.vfd() ? 2 : gridCard ? 18 : 16, fill,
                focus || down ? p.accent : p.border, focus ? 4 : 2);
        const auto* cover = row.cover < 0 ? nullptr : &player.tracks[row.cover];

        if (gridCard) {
            const bool threeCol = cell.w < 350.f;
            const float artSize = threeCol ? 256.f : 124.f;
            const auto count = std::to_string(row.ids.size());
            const float badgeW = even(d.textWidth(count, Size::Body, Face::Mono, true) + 20);
            if (threeCol) {
                const Rect artwork{cell.x + 20, cell.y + 14, artSize, artSize};
                d.art(cover, artwork, 12);
                const float textX = cell.x + 14;
                const float titleY = artwork.y + artwork.h + 8;
                const float subtitleY = titleY + listTitleHeight;
                const Rect badge{cell.x + cell.w - 14 - badgeW, subtitleY, badgeW, listSubtitleHeight};
                labelTitle({textX, titleY, cell.w - 28, listTitleHeight});
                d.label(row.subtitle, {textX, subtitleY, badge.x - textX - 12, listSubtitleHeight},
                        Size::Body, p.dim, Face::Display, Align::Left, false, true);
                d.round(badge, d.vfd() ? 2 : 8, p.elevated);
                d.label(count, badge, Size::Body, p.dim, Face::Mono, Align::Center, true);
            } else {
                d.art(cover, {cell.x + 18, cell.y + 18, artSize, artSize}, 12);
                const float textX = cell.x + 158;
                const float textW = cell.w - 176;
                const float titleY = cell.y + 18;
                const float subtitleY = titleY + listTitleHeight;
                labelTitle({textX, titleY, textW, listTitleHeight});
                d.label(row.subtitle, {textX, subtitleY, textW, listSubtitleHeight},
                        Size::Body, p.dim, Face::Display, Align::Left, false, true);
                const Rect badge{textX, subtitleY + listSubtitleHeight + 8, badgeW, listSubtitleHeight};
                d.round(badge, d.vfd() ? 2 : 8, p.elevated);
                d.label(count, badge, Size::Body, p.dim, Face::Mono, Align::Center, true);
            }
        } else if (row.track < 0) {
            const bool artist = row.source == Tab::Artists;
            const bool playlist = row.source == Tab::Playlists;
            const float textX = cell.x + (artist ? 24 : playlist ? 110 : 124);
            if (playlist) {
                const Rect tile{cell.x + 22, cell.y + (cell.h - 72) / 2, 72, 72};
                d.frame(tile, d.vfd() ? 2 : 14, p.elevated, p.strong);
                d.icon(row.playlist < 0 ? "star-filled" : "tracks",
                       {tile.x + 20, tile.y + 20, 32, 32}, p.accent, 32);
            } else if (!artist) {
                d.art(cover, {cell.x + 18, cell.y + (cell.h - 88) / 2, 88, 88}, 12);
            }
            float textRight = cell.x + cell.w - 22;
            if (!artist) {
                const auto count = std::to_string(row.ids.size());
                const float badgeW = even(d.textWidth(count, Size::Body, Face::Mono, true) + 28);
                const Rect badge{textRight - badgeW, cell.y + (cell.h - listSubtitleHeight) / 2,
                                 badgeW, listSubtitleHeight};
                d.round(badge, d.vfd() ? 2 : 10, p.elevated);
                d.label(count, badge, Size::Body, p.dim, Face::Mono, Align::Center, true);
                textRight = badge.x - 12;
            }
            const float textW = textRight - textX;
            const float titleY = rowTextTop(cell, !playlist);
            labelTitle({textX, titleY, textW, listTitleHeight});
            if (!playlist) {
                std::string subtitle = row.subtitle;
                if (artist) {
                    subtitle = std::to_string(row.albumCount) + (row.albumCount == 1 ? " Album, " : " Albums, ") +
                               std::to_string(row.ids.size()) + (row.ids.size() == 1 ? " Song" : " Songs");
                }
                d.label(subtitle, {textX, titleY + listTitleHeight, textW, listSubtitleHeight},
                        Size::Body, p.dim, Face::Display, Align::Left, false, true);
            }
        } else {
            const auto& track = player.tracks[row.track];
            const bool nowPlaying = isQueue ? row.queuePart == QueuePart::Current : row.track == player.current;
            const bool albumMode = row.albumTrack;
            const float art = albumMode ? 56.f : 76.f;
            const float artX = cell.x + 20.f;
            const float artY = cell.y + even((cell.h - art) / 2);
            if (albumMode) {
                d.frame({artX, artY, art, art}, d.vfd() ? 2 : 12,
                        nowPlaying ? p.accent : p.elevated, nowPlaying ? p.accent : p.strong);
                if (nowPlaying)
                    d.icon("play", {artX + 14, artY + 14, 28, 28}, p.accentInk, 32);
                else
                    d.label(twoDigits(track.number), {artX, artY, art, art},
                            Size::Body, p.dim, Face::Mono, Align::Center, true);
            } else {
                d.art(cover, {artX, artY, art, art}, 12);
                if (nowPlaying) {
                    d.round({artX + 44, artY + 44, 28, 28}, d.vfd() ? 2 : 8, p.accent);
                    d.icon("play", {artX + 50, artY + 50, 16, 16}, p.accentInk, 16);
                }
            }
            const float titleX = artX + art + 16.f;
            const float menuX = cell.x + cell.w - (isQueue ? 90.f : 88.f);
            const bool hasPlayNext = row.queuePart == QueuePart::UpNext;
            const float firstActionX = menuX - (hasPlayNext ? 74.f : 0.f);
            float textRight = firstActionX;
            if (!isQueue) {
                const std::string duration = timecode(track.duration);
                const float durationW = std::max(94.f, even(d.textWidth(duration, Size::Body, Face::Mono, false) + 4));
                textRight = menuX - 16.f - durationW;
                d.label(duration, {textRight, cell.y, durationW, cell.h}, Size::Body,
                        p.dim, Face::Mono, Align::Right, false, false, 600);
            }
            const bool starred = player.stars[row.track];
            constexpr float starSize = 32.f;
            constexpr float starGap = 8.f;
            const float starReserve = starred ? starSize + starGap : 0.f;
            const float textW = textRight - titleX - 16.f;
            const float availTitleW = std::max(0.f, textW - starReserve);
            const float titleY = rowTextTop(cell, !albumMode);
            labelTitle({titleX, titleY, availTitleW, listTitleHeight});
            if (starred) {
                const float naturalW = d.textWidth(row.title, Size::Title, Face::Display, true);
                const float starX = titleX + even(std::min(naturalW, availTitleW)) + starGap;
                const float starY = titleY + (listTitleHeight - starSize) / 2;
                d.icon("star-filled", {starX, starY, starSize, starSize}, p.text);
            }
            if (!albumMode) {
                d.label(row.subtitle, {titleX, titleY + listTitleHeight, textW, listSubtitleHeight},
                        Size::Body, p.dim, Face::Display, Align::Left, false, true);
            }
            if (isQueue) {
                const float y = cell.y + (cell.h - 64) / 2;
                if (hasPlayNext)
                    button(d, {firstActionX, y, 64, 64}, Action::PlayNext, row.queueIndex, "play-next",
                           {}, false, false, 32, p.elevated);
                button(d, {menuX, y, 64, 64}, Action::RowMenu, i, "more",
                       {}, false, false, 32, p.elevated);
            } else {
                const float y = cell.y + even((cell.h - 68) / 2);
                button(d, {menuX, y, 68, 68}, Action::RowMenu, i, "more",
                       {}, false, false, 32, p.elevated);
            }
        }
    }
    d.unclip();
    hitClip.reset();

    const Rect rail{railBox.x, top, railBox.w, listBox.h};
    std::vector<std::string> ticks;
    const bool emptyUpNext = !entries.empty() && entries.back().queuePart == QueuePart::EmptyUpNext;
    const int count = static_cast<int>(entries.size()) - (emptyUpNext ? 1 : 0);
    const bool showNumbers = count > 0 && (!emptyUpNext || yPos < cachedUpNextOffset);
    const auto itemNumber = [&](int index) {
        if (!isQueue)
            return index + 1;
        const auto& row = entries[index];
        if (row.queuePart == QueuePart::Played)
            return index - static_cast<int>(player.played.size());
        return row.queuePart == QueuePart::Current ? 0 : row.queueIndex + 1;
    };
    if (root && !isQueue && tab != Tab::Playlists) {
        const auto spec = activeSortSpec();
        if (spec.alphaMode && count > 0) {
            for (const char letter : std::string_view(ascending ? "ADGJMPSVZ" : "ZVSPMJGDA"))
                ticks.emplace_back(1, letter);
        } else if (spec.field == SortField::Year && count > 0) {
            const int indices[5] = {0, count / 4, count / 2, (count * 3) / 4, count - 1};
            for (int k = 0; k < 5; ++k) {
                const std::string yr = std::to_string(entries[indices[k]].year);
                ticks.push_back((k == 0 || k == 4 || yr.size() < 4) ? yr : yr.substr(2));
            }
        }
    }
    if (ticks.empty() && showNumbers) {
        const auto mark = [&](int index) {
            const int number = itemNumber(index);
            const int magnitude = std::abs(number);
            if (magnitude < 1000)
                return std::to_string(number);
            const int tenths = (magnitude + 50) / 100;
            return (number < 0 ? "-" : "") + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "k";
        };
        ticks.push_back(mark(0));
        if (count >= 10)
            for (int quarter = 1; quarter < 4; ++quarter)
                ticks.push_back(mark(static_cast<int>(std::lround(count * quarter / 4.f)) - 1));
        if (count > 1)
            ticks.push_back(mark(count - 1));
    }
    drawScrollbar(d, rail, scroll, scrollLimit, Action::Rail, ticks);

    if (railPopup > 0 && showNumbers && scrollLimit > 0) {
        int nearIdx = 0;
        for (int i = 0; i < count; ++i) {
            if (entries[i].box.y + entries[i].box.h * .5f >= scroll) {
                nearIdx = i;
                break;
            }
            nearIdx = i;
        }
        std::string hero = std::to_string(itemNumber(nearIdx));
        if (root && !isQueue && tab != Tab::Playlists) {
            const auto spec = activeSortSpec();
            if (spec.field == SortField::Year) {
                hero = std::to_string(entries[nearIdx].year);
            } else if (spec.alphaMode && !entries[nearIdx].title.empty()) {
                const unsigned char c0 = static_cast<unsigned char>(entries[nearIdx].title[0]);
                hero = c0 < 128 ? std::string(1, static_cast<char>(std::toupper(c0))) : "#";
            }
        }
        const float y = top + even((listBox.h - 116) / 2);
        const Rect popup{652, y, 276, 116};
        d.round(popup, d.vfd() ? 2 : 22, p.accent);
        d.label(hero, {popup.x, y + 12, popup.w, 56}, Size::Display,
                p.accentInk, Face::Mono, Align::Center, true);
        d.label(entries[nearIdx].title, {popup.x + 16, y + 72, popup.w - 32, 32}, Size::Sub, p.accentInk,
                Face::Display, Align::Center, true, true);
    }
}

void DeckUi::keypadUi(Draw& d)
{
    const auto& p = d.p();
    const auto hitsBefore = hits.size();
    const float y = 752 - even(keyboardHeight);
    d.frame({16, y, 992, 280}, d.vfd() ? 2 : 16, p.elevated, p.strong);
    const auto* selected = referenceKey(symbols, keySelection);
    for (const auto& key : keyAssets) {
        if (key.symbols != symbols)
            continue;
        Rect box{26.f + 98 * key.column, y + 12 + 66 * key.row, 98.f * key.span - 8, 58};
        const int code = key.row * keyColumns + key.column + key.span / 2;
        hit(box, Action::Key, code);
        const bool focus = &key == selected && textInput() && keypad;
        const float amount = activePressAmount(Action::Key, code, box);
        const bool down = (pressed && pressed->action == Action::Key && pressed->data == code) ||
                          isUiHeld(Action::Key, code);
        if (amount > .01f)
            box = inset(box, even(amount * 4));
        if (focus || amount > .01f)
            d.strokeRound(inset(box, -4), d.vfd() ? 6 : 14, p.soft, 4);
        d.frame(box, d.vfd() ? 2 : 10, down ? p.raised : key.done ? p.raised : key.function ? p.bg : p.card,
                focus || down ? p.accent : key.function ? p.strong : p.border, focus ? 4 : 2);
        const std::string_view caption = key.action == "bksp" ? "DEL"
                                       : key.action == "clear" ? "Clear"
                                       : key.action == "space" ? "Space"
                                       : key.action == "done" ? "Done"
                                       : key.label;
        d.label(caption, box, key.function ? Size::Sub : Size::Body,
                key.done ? p.accent : key.function ? p.dim : p.text, Face::Mono, Align::Center, true);
    }
    if (!textInput())
        hits.resize(hitsBefore);
}

std::vector<DeckUi::Option> DeckUi::options() const
{
    switch (sheet) {
    case Sheet::Track: {
        if (contextTrack < 0)
            return {};
        std::vector<Option> items;
        if (tab == Tab::Deck) {
            items.push_back({std::string("Shuffle"),
                             std::string(player.shuffle ? "On" : "Off"),
                             "shuffle", false, player.shuffle, OptShuffle});
            items.push_back({std::string("Repeat"),
                             std::string(player.repeat == 2 ? "One"
                                       : player.repeat == 1 ? "All" : "Off"),
                             player.repeat == 2 ? "repeat-one" : "repeat", false, player.repeat != 0, OptRepeat});
        } else if (contextQueuePart != QueuePart::Current) {
            items.push_back({std::string("Play Next"), "", "play-next", false, false, OptPlayNext});
            if (tab != Tab::Queue)
                items.push_back({std::string("Add to Queue"), "", "queue-add", false, false, OptAddQueue});
        }
        items.push_back({std::string("Go to Album"), "", "goto-album", false, false, OptGoAlbum});
        items.push_back({std::string("Go to Artist"), "", "artist", false, false, OptGoArtist});
        items.push_back({std::string(player.stars[contextTrack]
                                     ? "Unstar" : "Star"),
                         "", player.stars[contextTrack] ? "star-filled" : "star", false, false, OptStar});
        items.push_back({std::string("Add to Playlist"), "", "playlist-add", false, false, OptAddPlaylist});
        items.push_back({std::string("Get Info"), "", "info", false, false, OptGetInfo});
        if (contextQueuePart == QueuePart::UpNext)
            items.push_back({std::string("Remove from Queue"), "", "trash", false, false, OptRemoveQueue});
        else if (contextPlaylist >= 0)
            items.push_back({std::string("Remove from Playlist"), "", "trash", false, false, OptRemovePlaylist});
        return items;
    }
    case Sheet::Order: {
        std::vector<Option> items;
        const auto specs = sortSpecsFor(tab);
        const auto active = activeSortSpec();
        for (const auto& s : specs)
            items.push_back({std::string(s.label), std::string(s.hint), {}, s.field == sortField, false});
        if (active.alphaMode) {
            items.push_back({std::string(active.ascLabel), "", {}, ascending, false});
            items.push_back({std::string(active.descLabel), "", {}, !ascending, false});
        } else {
            items.push_back({std::string(active.descLabel), "", {}, !ascending, false});
            items.push_back({std::string(active.ascLabel), "", {}, ascending, false});
        }
        return items;
    }
    case Sheet::Playlist: {
        std::vector<Option> items;
        const bool inStar = contextTrack >= 0 && player.stars[contextTrack];
        const std::string addedLbl("Added");
        const std::string addLbl("+ Add");
        items.push_back({std::string("Starred"), inStar ? addedLbl : addLbl, "star-filled", false, inStar});
        for (const auto& list : player.playlists) {
            const bool inPl = contextTrack >= 0 &&
                std::ranges::find(list.tracks, contextTrack) != list.tracks.end();
            items.push_back({list.name + " (" + std::to_string(list.tracks.size()) + ")",
                             inPl ? addedLbl : addLbl, "playlists", false, inPl});
        }
        return items;
    }
    default: return {};
    }
}

int DeckUi::audioOutputRows() const
{
    return bluetoothOutput + static_cast<int>(bluetooth.devices().size());
}

const BluetoothPairing::Device* DeckUi::audioDevice(int row) const
{
    const int index = row - bluetoothOutput;
    const auto& devices = bluetooth.devices();
    return index >= 0 && index < static_cast<int>(devices.size()) ? &devices[index] : nullptr;
}

void DeckUi::revealAudioOutputRow()
{
    const int top = static_cast<int>(std::max(0, sheetSelection) * sheetOptionPitch);
    const int bottom = top + static_cast<int>(sheetOptionPitch);
    const int limit = std::max(0, static_cast<int>(audioOutputRows() * sheetOptionPitch - audioOutputListHeight));
    sheetListScroll = std::clamp(sheetListScroll, std::max(0, bottom - static_cast<int>(audioOutputListHeight)),
                                std::min(top, limit));
}

void DeckUi::audioOutputUi(Draw& d)
{
    using State = BluetoothPairing::State;
    const auto& p = d.p();
    const auto state = bluetooth.state();
    const bool scanning = state == State::Searching;
    const bool busy = bluetooth.busy();
    const auto scanLabel = scanning ? "Scan…"
                       : state == State::Unavailable ? "Scan failed"
                       : "Scan";

    const int count = audioOutputRows();
    const float contentHeight = count * sheetOptionPitch;
    const float visibleHeight = std::min(contentHeight, audioOutputListHeight);
    const float height = 116.f + visibleHeight;
    const float top = 76.f + even((692.f - height) / 2.f);
    const float listTop = top + 106.f;
    constexpr int unpairHintSize = 26;
    const auto unpairLabel = "Unpair";
    const auto unpairConfirm = "Unpair?";
    const float longestUnpairLabel = std::max(d.textWidth(unpairLabel, Size::Sub, Face::Display, true),
                                              d.textWidth(unpairConfirm, Size::Sub, Face::Display, true));
    const float unpairMargin = trailingGlyphPadding + unpairHintSize + trailingGlyphGap;
    const float unpairWidth = std::max(170.f, even((longestUnpairLabel + 2 * unpairMargin) /
                                                  (1.f - buttonPressShrink)) + 2.f);
    d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);
    d.label("Audio Output", {88, top + 22, 552, 68},
            Size::Title, p.text, Face::Display, Align::Left, true);
    button(d, {660, top + 22, 200, 68}, Action::Option, audioHeaderSelection, "search", scanLabel,
           keypad && sheetSelection == audioHeaderSelection, false, 32,
           p.elevated, scanning || busy ? p.muted : p.text, 32, 14, false, Size::Body);
    button(d, {872, top + 22, 64, 68}, Action::Back, 0, "close", {}, false, false, 32, p.elevated);

    sheetListLimit = static_cast<int>(contentHeight - visibleHeight);
    sheetListScroll = std::clamp(sheetListScroll, 0, sheetListLimit);
    constexpr float listRight = 932.f;
    const Rect rail{listRight - railBox.w, listTop, railBox.w, visibleHeight};
    const float rowRight = sheetListLimit > 0 ? rail.x - 12.f : listRight;
    const float actionRight = rowRight - 14.f;
    sheetListBox = {86, listTop, rowRight + 6.f - 86.f, visibleHeight};
    d.clip(sheetListBox);
    hitClip = sheetListBox;
    for (int row = 0; row < count; ++row) {
        const float y = listTop + 4.f + row * sheetOptionPitch - sheetListScroll;
        if (y + 82.f <= listTop || y >= listTop + visibleHeight)
            continue;
        const auto* device = audioDevice(row);
        const auto deviceState = device ? bluetooth.stateFor(*device) : State::Idle;
        const bool active = row < bluetoothOutput ? output == row
            : device && output == bluetoothOutput && bluetooth.isConnected(*device);
        const bool selected = sheetSelection == row;
        const bool focus = keypad && selected;
        const bool armed = device && device->id == armedBluetoothDevice;
        const bool down = (pressed && pressed->action == Action::Option && pressed->data == row && !dragging)
                          || isUiHeld(Action::Option, row);
        const Rect box{92, y, rowRight - 92.f, 78};
        if (focus)
            d.strokeRound(inset(box, -4), d.vfd() ? 6 : 20, p.soft, 4);
        d.frame(box, d.vfd() ? 2 : 16, down ? p.raised : p.elevated,
                focus || down ? p.accent : p.strong, focus ? 4 : 2);
        const float cy = y + box.h / 2;
        if (row < bluetoothOutput || (device && device->paired)) {
            d.circle(124, cy, 12, active ? p.accent : p.muted);
            d.circle(124, cy, 9, down ? p.raised : p.elevated);
            if (active)
                d.circle(124, cy, 5, p.accent);
        }
        const auto label = device ? std::string_view(device->name)
                         : row == 0 ? "Headphones" : "Speakers";
        d.icon(outputIcons[std::min(row, bluetoothOutput)], {154, cy - 16, 32, 32}, active ? p.accent : p.dim, 32);
        const float actionWidth = device && device->paired ? unpairWidth : 170.f;
        const auto failure = deviceState == State::Failed ? "Connection failed"
                           : deviceState == State::UnpairFailed ? "Unpair failed"
                           : std::string_view{};
        const float failureWidth = failure.empty() ? 0.f : even(d.textWidth(failure, Size::Sub, Face::Display)) + 16.f;
        const float labelRight = device ? actionRight - actionWidth - failureWidth - 16.f : rowRight - 26.f;
        d.label(label, {202, y, labelRight - 202.f, box.h}, Size::Body,
                p.text, Face::Display, Align::Left, true, true);
        if (!failure.empty())
            d.label(failure, {actionRight - actionWidth - failureWidth, y, failureWidth - 8.f, box.h},
                    Size::Sub, p.dim, Face::Display, Align::Right);
        if (!busy)
            hit(box, Action::Option, row);
        if (deviceState == State::Pairing || deviceState == State::Unpairing) {
            const auto progress = deviceState == State::Pairing ? "Connecting…"
                                                               : "Unpairing…";
            d.label(progress, {actionRight - actionWidth, y, actionWidth, box.h},
                    Size::Sub, p.dim, Face::Display, Align::Center, true);
        } else if (device && device->paired) {
            button(d, {actionRight - unpairWidth, y + 10, unpairWidth, 58}, Action::BluetoothUnpair, row,
                   selected ? "keycap-y" : "", armed ? unpairConfirm : unpairLabel, false, false, unpairHintSize,
                   armed ? p.danger : p.card, armed ? p.accentInk : p.dim, 0, 14, false, Size::Sub, Align::Right);
        } else if (device) {
            button(d, {actionRight - actionWidth, y + 10, actionWidth, 58}, Action::Option, row, {}, "Pair",
                   false, false, 32, p.card, busy ? p.muted : p.text);
        }
    }
    hitClip.reset();
    d.unclip();
    if (sheetListLimit > 0)
        drawScrollbar(d, rail, sheetListScroll, sheetListLimit, Action::OutputRail, dotRailTicks);
}

void DeckUi::overlay(Draw& d)
{
    const auto& p = d.p();
    hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& h) { return h.box.y >= 76; }), hits.end());

    if (sheet == Sheet::Settings) {
        d.rect({0, 76, 1024, 692}, p.bg);
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 68, 68}, Action::Back, 0, "back", {}, false, false, 32, p.elevated);
        const std::string_view settingsHeading = "Deck Settings";
        const auto titleW = d.textWidth(settingsHeading, Size::Title, Face::Display, true);
        d.label(settingsHeading, {114, 92, titleW + 8, 88}, Size::Title, p.text, Face::Display, Align::Left, true);

        settingsBox = {12, 192, 924, 560};
        settingsLimit = std::max(0.f, 4.f + settingsContentHeight + 16.f - settingsBox.h);
        settingsScroll = std::clamp(settingsScroll, 0.f, settingsLimit);
        settingsScrollTarget = std::clamp(settingsScrollTarget, 0.f, settingsLimit);
        const float sy = 196.f - even(settingsScroll);

        d.clip(settingsBox);
        hitClip = settingsBox;

        const auto card = [&](Rect box, std::string_view heading, int row, std::string_view rightInfo = {}) {
            const bool focus = keypad && sheetSelection == row;
            if (focus)
                d.strokeRound(inset(box, -4), d.vfd() ? 6 : 20, p.soft, 4);
            d.frame(box, d.vfd() ? 2 : 16, p.card, focus ? p.accent : p.border, focus ? 4 : 2);
            const float infoW = rightInfo.empty() ? 0 : even(d.textWidth(rightInfo, Size::Body, Face::Mono, true) + 2);
            const float infoRight = box.x + box.w - 22;
            d.label(heading, {box.x + 22, box.y + 14, box.w - 60 - infoW, 30},
                    Size::Body, p.dim, Face::Mono, Align::Left, true);
            if (!rightInfo.empty())
                d.label(rightInfo, {infoRight - infoW, box.y + 14, infoW, 30},
                        Size::Body, p.accent, Face::Mono, Align::Right, true);
        };
        const auto segment = [&](Rect box, int option, std::string_view label, bool selected,
                                 std::optional<Color> swatch = std::nullopt, bool subFocused = false) {
            hit(box, Action::Option, option);
            const float amt = activePressAmount(Action::Option, option, box);
            if (amt > .01f)
                box = inset(box, even(amt * 3));
            if (subFocused)
                d.strokeRound(inset(box, -3), d.vfd() ? 4 : 16, p.soft, 3);
            const Color stroke = subFocused ? (selected ? p.text : p.accent) : (selected ? p.accent : p.strong);
            d.frame(box, d.vfd() ? 2 : 14, selected ? p.accent : p.elevated, stroke, subFocused ? 4 : 2);
            const Color ink = selected ? p.accentInk : p.text;
            if (swatch) {
                const float tw = d.textWidth(label, Size::Body, Face::Mono, true);
                const float dotX = box.x + even((box.w - (20 + 10 + tw)) / 2) + 10;
                const float dotY = box.y + even(box.h / 2);
                d.circle(dotX, dotY, 10, selected ? p.accentInk : p.strong);
                d.circle(dotX, dotY, 8, *swatch);
                d.label(label, {dotX + 18, box.y, tw + 8, box.h}, Size::Body, ink, Face::Mono, Align::Left, true);
            } else {
                d.label(label, box, Size::Body, ink, Face::Mono, Align::Center, true);
            }
        };

        const Rect bBox{16, sy, 916, firstSettingsRowHeight};
        card(bBox, "SCREEN BRIGHTNESS", BrightnessRow);
        d.frame({38, sy + 58, 872, 24}, d.vfd() ? 2 : 12, p.bg, p.strong);
        for (int i = 0; i < 7; ++i) {
            const float x = 58 + even(i * 832 / 6.f);
            d.round({x - 2, sy + 92, 4, 12}, 2, p.strong);
            d.label(std::to_string(brightnessLevels[i]) + "%", {x - 30, sy + 110, 60, 26},
                    Size::Sub, p.muted, Face::Mono, Align::Center, true);
        }
        const float thumb = 58 + even(brightness * 832 / 6.f);
        d.frame({thumb - 22, sy + 48, 44, 44}, d.vfd() ? 2 : 12, p.accent, p.card);
        hit({38, sy + 44, 872, 54}, Action::Brightness);

        float cy = sy + settingsRowOffset(TimeFormatRow);
        card({16, cy, 916, settingsRowHeight}, "TIME FORMAT", TimeFormatRow);
        segment({38, cy + 54, 430, 64}, 50, "12-Hour (12h)", !clock24h);
        segment({480, cy + 54, 430, 64}, 51, "24-Hour (24h)", clock24h);

#if DECKDS_ENABLE_SET_CLOCK
        cy = sy + settingsRowOffset(SetClockRow);
        const std::string clockStatus = clockString() + "  " +
            std::string(clockOffsetMinutes == 0 ? "(AUTO)" : "(MANUAL)");
        card({16, cy, 916, settingsRowHeight}, "SET CLOCK", SetClockRow, clockStatus);
        const std::string_view clkLabels[5] = {
            "-1 hr", "+1 hr",
            "-1 min", "+1 min", "Sync"
        };
        float clkX = 38.f;
        for (int i = 0; i < 5; ++i) {
            const float bw = (i == 4) ? 152.f : 168.f;
            const bool subFoc = keypad && sheetSelection == SetClockRow && clockBtnFocus == i;
            segment({clkX, cy + 54, bw, 64}, 60 + i, clkLabels[i], i == 4 && clockOffsetMinutes == 0,
                    std::nullopt, subFoc);
            clkX += bw + 12.f;
        }
#endif

        cy = sy + settingsRowOffset(VisualThemeRow);
        card({16, cy, 916, settingsRowHeight}, "VISUAL THEME", VisualThemeRow);
        for (int i = 0; i < 3; ++i)
            segment({38.f + i * 294.f, cy + 54, 284, 64}, i,
                    themeNames[i], static_cast<int>(theme) == i);

        cy = sy + settingsRowOffset(AccentRow);
        card({16, cy, 916, settingsRowHeight}, "ACCENT COLOR", AccentRow);
        const auto presets = accentPresets(theme);
        const int curAcc = currentAccent();
        for (int i = 0; i < 4; ++i)
            segment({38.f + i * 220.f, cy + 54, 210, 64}, 10 + i,
                    presets[i].name, curAcc == i, presets[i].swatch);

        cy = sy + settingsRowOffset(SfxRow);
        card({16, cy, 916, settingsRowHeight}, "UI SOUND FX", SfxRow);
        segment({38, cy + 54, 430, 64}, 5, "SFX On", player.sfx);
        segment({480, cy + 54, 430, 64}, 6, "Mute", !player.sfx);

        cy = sy + settingsRowOffset(SfxVolumeRow);
        card({16, cy, 916, settingsRowHeight}, "SOUND FX VOLUME", SfxVolumeRow);
        const std::string_view volLabels[3] = {"Low", "Mid", "High"};
        for (int i = 0; i < 3; ++i)
            segment({38.f + i * 294.f, cy + 54, 284, 64}, 20 + i, volLabels[i], player.sfxVolume == i);

        cy = sy + settingsRowOffset(SfxOutputRow);
        card({16, cy, 916, settingsRowHeight}, "SOUND FX PLAY THROUGH", SfxOutputRow);
        segment({38, cy + 54, 430, 64}, 30, "Current Output", !player.sfxSpeaker);
        segment({480, cy + 54, 430, 64}, 31, "Built-in Speaker", player.sfxSpeaker);

        cy = sy + settingsRowOffset(LidCloseRow);
        card({16, cy, 916, settingsRowHeight}, "LID CLOSE (HALL)", LidCloseRow);
        segment({38, cy + 54, 430, 64}, 3, "Pocket Audio", pocket);
        segment({480, cy + 54, 430, 64}, 4, "Pause", !pocket);

        d.unclip();
        hitClip.reset();

        const Rect sRail{railBox.x, 192, railBox.w, 560};
        drawScrollbar(d, sRail, settingsScroll, settingsLimit, Action::SettingsRail, dotRailTicks);
        return;
    }

    d.rect({0, 76, 1024, 692}, color(0x08090c, 214));
    if (sheet == Sheet::Output) {
        audioOutputUi(d);
        return;
    }
    const Track* track = (contextTrack >= 0 && contextTrack < static_cast<int>(player.tracks.size()))
                       ? &player.tracks[contextTrack] : nullptr;
    const auto items = options();
    const auto closeSheet = [&](float top) {
        button(d, {872, top + 26, 64, 64}, Action::Back, 0, "close", {}, false, false, 32, p.elevated);
    };

    if (sheet == Sheet::Order) {
        const auto specs = sortSpecsFor(tab);
        const auto active = activeSortSpec();
        const int fieldRows = (static_cast<int>(specs.size()) + 1) / 2;
        const float height = 114.f + 28.f + fieldRows * 88.f + 34.f + 88.f + 16.f;
        const float top = 76.f + even((692.f - height) / 2);
        d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);

        const std::string_view heading = tab == Tab::Songs ? "Order Songs"
                                       : tab == Tab::Artists ? "Order Artists"
                                       : "Order Albums";
        const std::string sub = std::string(active.label) + " (" +
            std::string(stripLeadingArrow(ascending ? active.ascLabel : active.descLabel)) + ")";
        d.label(heading, {88, top + 22, 770, 36}, Size::Body, p.text, Face::Display, Align::Left, true);
        d.label(sub, {88, top + 60, 770, 28}, Size::Sub, p.dim, Face::Mono, Align::Left);
        closeSheet(top);

        d.label("ORDER BY", {92, top + 104, 400, 24}, Size::Caption, p.muted, Face::Mono, Align::Left, true);
        const float w = 414.f;
        float y = top + 134.f;
        for (int i = 0; i < static_cast<int>(specs.size()); ++i) {
            Rect r{92.f + (i % 2) * (w + 12.f), y + (i / 2) * 88.f, w, 76.f};
            hit(r, Action::Option, i);
            const bool focus = keypad && sheetSelection == i;
            const bool sel = items[i].active;
            const float amt = activePressAmount(Action::Option, i, r);
            if (amt > .01f) r = inset(r, even(amt * 4));
            if (focus) d.strokeRound(inset(r, -4), d.vfd() ? 6 : 20, p.soft, 4);
            d.frame(r, d.vfd() ? 2 : 16, sel ? p.accent : p.elevated,
                    focus ? (sel ? p.text : p.accent) : (sel ? p.accent : p.strong), focus ? 4 : 2);
            const Color ink = sel ? p.accentInk : p.text;
            d.label(items[i].label, {r.x + 22, r.y, r.w - 110, r.h}, Size::Body, ink, Face::Display, Align::Left, true);
            d.label(items[i].value, {r.x + r.w - 90, r.y, 68, r.h}, Size::Caption,
                    alpha(ink, .74f), Face::Mono, Align::Right, true);
        }

        const float dirY = y + fieldRows * 88.f + 6.f;
        d.label("DIRECTION", {92, dirY, 400, 24}, Size::Caption, p.muted, Face::Mono, Align::Left, true);
        for (int j = 0; j < 2; ++j) {
            const int idx = static_cast<int>(specs.size()) + j;
            Rect r{92.f + j * (w + 12.f), dirY + 30.f, w, 76.f};
            hit(r, Action::Option, idx);
            const bool focus = keypad && sheetSelection == idx;
            const bool sel = items[idx].active;
            const float amt = activePressAmount(Action::Option, idx, r);
            if (amt > .01f) r = inset(r, even(amt * 4));
            if (focus) d.strokeRound(inset(r, -4), d.vfd() ? 6 : 20, p.soft, 4);
            d.frame(r, d.vfd() ? 2 : 16, sel ? p.accent : p.elevated,
                    focus ? (sel ? p.text : p.accent) : (sel ? p.accent : p.strong), focus ? 4 : 2);
            d.label(items[idx].label, r, Size::Body, sel ? p.accentInk : p.text, Face::Display, Align::Center, true);
        }
        return;
    }

    if (sheet == Sheet::Playlist) {
        const bool typing = input == Input::Playlist;
        const int maxVisibleRows = typing ? 2 : 4;
        const int shownRows = std::min(maxVisibleRows, static_cast<int>(items.size()));
        const float height = typing ? 378.f : (116.f + shownRows * 86.f + 88.f);
        const float top = typing ? 84.f : 76.f + even((692.f - height) / 2);
        d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);
        d.label(track ? "Add to Playlist" : "Playlists",
                {88, top + 20, 770, 36}, Size::Body, p.text, Face::Display, Align::Left, true);
        const std::string sub = track ? std::string(track->title) + " — " + std::string(track->artist)
                                      : std::string("Create a new playlist");
        d.label(sub, {88, top + 58, 770, 26}, Size::Sub, p.dim, Face::Mono, Align::Left, false, true);
        closeSheet(top);

        const float listTop = top + 98.f;
        const float listH = shownRows * 86.f;
        d.clip({84, listTop - 4, 856, listH + 8});
        hitClip = Rect{84, listTop - 4, 856, listH + 8};
        const int startIdx = std::clamp(sheetSelection - shownRows + 1, 0,
                                        std::max(0, static_cast<int>(items.size()) - shownRows));
        for (int i = startIdx; i < std::min(static_cast<int>(items.size()), startIdx + shownRows); ++i) {
            Rect r{92, listTop + (i - startIdx) * 86.f, 840, 76};
            hit(r, Action::Option, i);
            const auto& opt = items[i];
            const bool focus = keypad && !typing && sheetSelection == i;
            const float amt = activePressAmount(Action::Option, i, r);
            if (amt > .01f) r = inset(r, even(amt * 4));
            if (focus) d.strokeRound(inset(r, -4), d.vfd() ? 6 : 18, p.soft, 4);
            d.frame(r, d.vfd() ? 2 : 14, opt.member ? mix(p.elevated, p.accent, .18f) : p.elevated,
                    focus || opt.member ? p.accent : p.strong, focus ? 4 : 2);
            const Color ink = opt.member ? p.accent : p.text;
            d.icon(opt.icon, {r.x + 20, r.y + 22, 32, 32}, opt.member ? p.accent : p.dim, 32);
            d.label(opt.label, {r.x + 66, r.y, r.w - 200, r.h}, Size::Body, ink, Face::Display, Align::Left, true, true);
            if (track)
                d.label(opt.value, {r.x + r.w - 130, r.y, 108, r.h}, Size::Sub,
                        opt.member ? p.accent : p.dim, Face::Mono, Align::Right, true);
        }
        d.unclip();
        hitClip.reset();

        const float inputY = top + height - 82.f;
        const Rect inputBox{92, inputY, 692, 62};
        const bool inputFocus = typing || (keypad && sheetSelection == static_cast<int>(items.size()));
        if (inputFocus)
            d.strokeRound(inset(inputBox, -4), d.vfd() ? 6 : 18, p.soft, 4);
        d.frame(inputBox, d.vfd() ? 2 : 14, p.bg, inputFocus ? p.accent : p.strong, inputFocus ? 4 : 2);
        d.label(playlistName.empty() ? "New playlist name…" : std::string_view(playlistName),
                {inputBox.x + 18, inputBox.y, inputBox.w - 36, inputBox.h},
                Size::Sub, playlistName.empty() ? p.muted : p.text, Face::Display, Align::Left, false, true, 600);
        hit(inputBox, Action::NewPlaylist, 1);
        button(d, {796, inputY, 136, 62}, Action::CreatePlaylist, 0, {}, "Create",
               keypad && !typing && sheetSelection == static_cast<int>(items.size()) + 1, false, 24,
               p.accent, p.accentInk, 24, 14);

        if (typing)
            keypadUi(d);
        return;
    }

    const int columns = sheet == Sheet::Track ? 2 : 1;
    const int count = (static_cast<int>(items.size()) + columns - 1) / columns;
    const float height = sheet == Sheet::Info ? 612.f
        : std::min(620.f, (sheet == Sheet::Track ? 124.f : 118.f) + count * sheetOptionPitch);
    const float top = 76.f + even((692.f - height) / 2);
    d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);

    const bool subline = sheet == Sheet::Track && track;
    const bool info = sheet == Sheet::Info && track;
    const std::string_view title = subline ? track->title : "Get Info";
    const float headingX = info ? 180.f : 88.f;
    const float headingH = subline ? 36.f : info ? 76.f : 60.f;
    if (info)
        d.art(track, {88, top + 24, 76, 76}, 12);
    d.label(title, {headingX, top + 24, 852 - headingX, headingH},
            Size::Body, p.text, Face::Display, Align::Left, true, true);
    if (subline) {
        const std::string sub = std::string(track->artist) + " — " + std::string(track->album);
        d.label(sub, {headingX, top + 62, 852 - headingX, 28}, Size::Sub,
                p.dim, Face::Mono, Align::Left, false, true);
    }
    closeSheet(top);

    if (info) {
        struct InfoRow { std::string_view label; std::string value; bool full; bool mono; bool wrap = false; };
        int kbps = 1411;
        std::string digits;
        for (const char ch : track->bitrate)
            if (ch >= '0' && ch <= '9') digits.push_back(ch);
        if (!digits.empty()) kbps = std::max(128, std::stoi(digits));
        char sizeBuf[32];
        std::snprintf(sizeBuf, sizeof(sizeBuf), "%.1f MB", (kbps * 1000.0 / 8.0) * track->duration / (1024.0 * 1024.0));
        const auto ext = std::string(track->format.substr(0, track->format.find(' ')));
        std::string lowerExt = ext;
        for (char& c : lowerExt) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        const std::string depth = track->format.find("24") != std::string_view::npos ? "24-bit"
                                : track->format.find("16") != std::string_view::npos ? "16-bit"
                                : std::string("Lossy");
        const std::string codecKind = lowerExt == "mp3" ? std::string("Lossy")
                                                        : std::string("Lossless");
        const std::string filePath = "/mnt/sd2/Music/" + std::string(track->artist) + "/" +
            std::to_string(track->year) + " - " + std::string(track->album) + "/" +
            twoDigits(track->number) + " - " + std::string(track->title) + "." + lowerExt;

        const InfoRow specs[] = {
            {"TITLE", std::string(track->title), true, false, true},
            {"ARTIST", std::string(track->artist), true, false, true},
            {"ALBUM", std::string(track->album), true, false, true},
            {"YEAR", std::to_string(track->year), false, true},
            {"TRACK / DISC",
             "Disc " + std::to_string(track->disc) + " / Track " + twoDigits(track->number), false, true},
            {"DURATION", timecode(track->duration), false, true},
            {"FORMAT", std::string(track->format) + " (" + codecKind + ")", false, true},
            {"SAMPLE RATE", std::string(track->rate) + "  " + depth, false, true},
            {"BITRATE", std::string(track->bitrate), false, true},
            {"CHANNELS", std::string("Stereo (2.0)"), false, true},
            {"FILE SIZE", sizeBuf, false, true},
            {"ADDED DATE", std::string(track->added), false, true},
            {"REPLAYGAIN", "-5.4 dB (Track) / -4.8 dB (Album)", true, true},
            {"SIGNAL PATH",
             std::string("ALSA Direct (Bit-Perfect Hardware Output)"), true, false},
            {"FILE PATH", filePath, true, true},
        };

        constexpr float lineHeight = 30.f;
        constexpr float fullWidth = 848.f;
        struct InfoCard {
            Rect box;
            const InfoRow* spec;
            std::vector<std::string> lines;
        };
        std::vector<InfoCard> placed;
        float curY = 0;
        int col = 0;
        for (const auto& sp : specs) {
            if (sp.full) {
                if (col == 1) { curY += 90.f; col = 0; }
                auto lines = sp.wrap ? d.wrapText(sp.value, fullWidth - 36.f, Size::Sub, Face::Display, true)
                                     : std::vector<std::string>{};
                const float extraHeight = std::max<std::size_t>(1, lines.size()) - 1;
                const float h = 80.f + extraHeight * lineHeight;
                placed.push_back({{88.f, curY, fullWidth, h}, &sp, std::move(lines)});
                curY += h + 10.f;
            } else {
                placed.push_back({{88.f + col * 430.f, curY, 418.f, 80.f}, &sp, {}});
                if (++col == 2) { curY += 90.f; col = 0; }
            }
        }
        if (col == 1) curY += 90.f;
        sheetListBox = {86, top + 114, 852, 472};
        sheetListLimit = std::max(0, static_cast<int>(std::ceil(curY - sheetListBox.h)));
        sheetListScroll = std::clamp(sheetListScroll, 0, sheetListLimit);

        d.clip(sheetListBox);
        const float viewBottom = sheetListBox.y + sheetListBox.h;
        for (const auto& [box, sp, lines] : placed) {
            const Rect r{box.x, top + 116.f + box.y - sheetListScroll, box.w, box.h};
            if (r.y + r.h <= sheetListBox.y || r.y >= viewBottom)
                continue;
            d.frame(r, d.vfd() ? 2 : 12, p.elevated, p.border);
            d.label(sp->label, {r.x + 18, r.y + 10, r.w - 36, 24}, Size::Caption,
                    p.muted, Face::Mono, Align::Left, true);
            Rect value{r.x + 18, r.y + 38, r.w - 36, lineHeight};
            if (lines.empty()) {
                d.label(sp->value, value, Size::Sub, p.text, sp->mono ? Face::Mono : Face::Display,
                        Align::Left, true, true);
            } else {
                for (const auto& line : lines) {
                    if (value.y >= viewBottom)
                        break;
                    if (value.y + value.h > sheetListBox.y)
                        d.label(line, value, Size::Sub, p.text, Face::Display, Align::Left, true);
                    value.y += lineHeight;
                }
            }
        }
        d.unclip();
    } else {
        const float w = columns == 2 ? 414.f : 840.f;
        const float baseY = top + (sheet == Sheet::Track ? 110.f : 104.f);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const float y = baseY + (i / columns) * sheetOptionPitch;
            if (y + 78.f > top + height - 16.f)
                break;
            Rect r{92.f + (i % columns) * (w + 12.f), y, w, 78.f};
            const auto& option = items[i];
            const bool focus = keypad && sheetSelection == i;
            const bool down = (pressed && pressed->action == Action::Option && pressed->data == i) ||
                              isUiHeld(Action::Option, i);
            const float amt = activePressAmount(Action::Option, i, r);
            if (amt > .01f)
                r = inset(r, even(amt * 4));
            const bool danger = option.icon == "trash";
            if (focus)
                d.strokeRound(inset(r, -4), d.vfd() ? 6 : 20, p.soft, 4);
            const Color fill = down ? p.raised : danger ? mix(p.elevated, p.danger, .14f) : p.elevated;
            const Color stroke = focus || down ? p.accent : danger ? p.danger : p.strong;
            const Color ink = danger ? p.danger : p.text;
            d.frame(r, d.vfd() ? 2 : 16, fill, stroke, focus ? 4 : 2);
            d.icon(option.icon, {r.x + 22, r.y + (r.h - 32) / 2, 32, 32}, ink, 32);
            const float valueW = even(d.textWidth(option.value, Size::Sub, Face::Mono, true));
            const float labelW = r.w - 96 - valueW - (valueW > 0 ? 16.f : 0.f);
            d.label(option.label, {r.x + 70, r.y, labelW, r.h}, Size::Body,
                    ink, Face::Display, Align::Left, true, true);
            d.label(option.value, {r.x + r.w - 24 - valueW, r.y, valueW + 4, r.h}, Size::Sub,
                    option.member ? p.text : p.muted, Face::Mono, Align::Right, true);
            hit(r, Action::Option, i);
        }
    }
}

void DeckUi::targetScroll(float target, bool animate)
{
    scrollFrom = scroll;
    scrollTarget = std::clamp(target, 0.f, scrollLimit);
    scrollTime = animate ? 0 : 1;
    if (!animate)
        scroll = scrollTarget;
}

void DeckUi::targetSettingsScroll(float target, bool animate)
{
    settingsScrollFrom = settingsScroll;
    settingsScrollTarget = std::clamp(target, 0.f, settingsLimit);
    settingsScrollTime = animate ? 0 : 1;
    if (!animate)
        settingsScroll = settingsScrollTarget;
}

float DeckUi::expectedCatalogHeight() const
{
    const bool root = details.empty();
    const bool searchable = root && tab != Tab::Queue;
    const bool utilOpen = searchable && (utility || !query.empty() || input == Input::Search);
    return catalogBottom - catalogTop - (root && !utilOpen ? 0.f : catalogToolbarHeight);
}

float DeckUi::catalogScrollLimit(float viewHeight, float keyboardPad) const
{
    // Keep Up Next at the top even when the queue is short or empty.
    return std::max(cachedUpNextOffset, catalogInset + cachedContentHeight + catalogEndPadding + keyboardPad - viewHeight);
}

DeckUi::RowKey::RowKey(const Row& row)
    : source(row.source), queuePart(row.queuePart), track(row.track), playlist(row.playlist), title(row.title)
{
}

bool DeckUi::RowKey::matches(const Row& row) const
{
    if (row.source != source)
        return false;
    if (source == Tab::Songs)
        return row.queuePart == queuePart && (queuePart == QueuePart::Current || row.track == track);
    if (source == Tab::Playlists)
        return row.playlist == playlist;
    return row.title == title;
}

DeckUi::CatalogPosition DeckUi::catalogPosition() const
{
    const auto& entries = rows();
    CatalogPosition position{scrollTarget, selection, std::nullopt};
    if (selection >= 0 && selection < static_cast<int>(entries.size()))
        position.item.emplace(entries[selection]);
    return position;
}

void DeckUi::rememberRootPosition()
{
    // Search clears on a tab change. Keep the position in the unfiltered catalog.
    if (tab != Tab::Deck && tab != Tab::Queue && details.empty() && query.empty() && input != Input::Search)
        rootPositions[static_cast<int>(tab)] = catalogPosition();
}

void DeckUi::restoreCatalogPosition(const CatalogPosition& position, bool revealItem)
{
    const auto& entries = rows();
    selection = std::clamp(position.selection, 0, std::max(0, static_cast<int>(entries.size()) - 1));
    const float height = expectedCatalogHeight();
    scrollLimit = catalogScrollLimit(height);
    targetScroll(position.scroll, false);
    if (!position.item || entries.empty())
        return;

    const auto& key = *position.item;
    if (!key.matches(entries[selection])) {
        const auto found = std::ranges::find_if(entries, [&](const Row& row) { return key.matches(row); });
        if (found == entries.end())
            return;
        selection = static_cast<int>(found - entries.begin());
    }
    if (!revealItem)
        return;

    keypad = true;
    const Rect box = entries[selection].box;
    const float top = catalogInset + box.y;
    const float bottom = top + box.h;
    const float visibleTop = even(scroll);
    if (top < visibleTop)
        targetScroll(top);
    else if (bottom > visibleTop + height)
        targetScroll(bottom - height);
}

DeckUi::CatalogPosition DeckUi::defaultCatalogPosition() const
{
    CatalogPosition position;
    if (tab != Tab::Queue)
        return position;

    const auto& entries = rows();
    const auto target = std::ranges::find_if(entries, [](const Row& row) {
        return row.queuePart == QueuePart::UpNext || row.queuePart == QueuePart::EmptyUpNext;
    });
    position.selection = static_cast<int>(target - entries.begin());
    position.scroll = cachedUpNextOffset;
    return position;
}

void DeckUi::keepSelectedVisible()
{
    const auto& entries = rows();
    if (selection < 0 || selection >= static_cast<int>(entries.size()))
        return;
    const float expectedListH = expectedCatalogHeight();
    const float oskPad = (input == Input::Search && sheet == Sheet::None) ? 296.f : 0.f;
    scrollLimit = catalogScrollLimit(expectedListH, oskPad);

    const auto& row = entries[selection];
    const bool firstUpNext = row.queuePart == QueuePart::UpNext && row.queueIndex == 0;
    if (row.queuePart == QueuePart::EmptyUpNext || firstUpNext) {
        if (scrollTarget != cachedUpNextOffset)
            targetScroll(cachedUpNextOffset);
        return;
    }
    const Rect box = row.box;
    const float visible = std::max(60.f, expectedListH - oskPad);
    const float firstOffset = row.queuePart == QueuePart::UpNext ? cachedUpNextOffset : 0.f;
    float target = std::max(scrollTarget, firstOffset);
    if (box.y < target)
        target = std::max(firstOffset, box.y - 4.f);
    else if (box.y + box.h + 16.f > target + visible)
        target = box.y + box.h + 16.f - visible;
    if (target != scrollTarget)
        targetScroll(target);
}

void DeckUi::keepSettingsVisible()
{
    const int row = std::clamp(sheetSelection, 0, SettingsRowCount - 1);
    const float topY = settingsRowOffset(row);
    const float botY = topY + (row == BrightnessRow ? firstSettingsRowHeight : settingsRowHeight) + 16.f;
    const float viewH = settingsBox.h;
    settingsLimit = std::max(0.f, 4.f + settingsContentHeight + 16.f - viewH);
    if (topY < settingsScrollTarget)
        targetSettingsScroll(std::max(0.f, topY - 4.f));
    else if (botY > settingsScrollTarget + viewH)
        targetSettingsScroll(botY - viewH);
}

void DeckUi::scrubRail(float y)
{
    if (listBox.h <= 68)
        return;
    targetScroll(scrollRailFraction(listBox, y) * scrollLimit, false);
    railPopup = .75f;
}

void DeckUi::scrubSettingsRail(float y)
{
    if (settingsBox.h <= 68)
        return;
    targetSettingsScroll(scrollRailFraction(settingsBox, y) * settingsLimit, false);
}

void DeckUi::scrubOutputRail(float y)
{
    if (sheet != Sheet::Output || sheetListLimit <= 0)
        return;
    const float fraction = std::clamp(scrollRailFraction(sheetListBox, y), 0.f, 1.f);
    sheetListScroll = static_cast<int>(std::lround(fraction * sheetListLimit));
}

void DeckUi::scrubPosition(Action which, float x)
{
    if (which == Action::Brightness) {
        activate(Action::Brightness, static_cast<int>(std::lround((x - 58) * 6 / 832)));
    } else if (which == Action::Scrub) {
        player.seek((x - deckWave.x) / deckWave.w);
    } else {
        const float fraction = std::clamp((x - volumeTrack.x - 22) / (volumeTrack.w - 44), 0.f, 1.f);
        player.volume = static_cast<int>(std::round(fraction * 100));
        if (player.volume > 0)
            lastNonZeroVolume = player.volume;
        player.mute = player.volume == 0;
        volumeTime = .9f;
    }
}

void DeckUi::cycleStage()
{
    outgoingStage = stage;
    stage = (stage + 1) % 3;
    stageTime = 0;
}

void DeckUi::toggleLid()
{
    lid = !lid;
    if (lid) {
        if (!pocket && player.playing)
            player.playToggle();
        cancelInput();
    }
}

void DeckUi::power(bool down, bool repeated)
{
    if (repeated)
        return;
    if (down) {
        powerDown = true;
        powerLong = false;
        powerTime = 0;
    } else if (powerDown) {
        const bool longPress = powerLong;
        cancelInput();
        if (!longPress) {
            if (poweredOff) poweredOff = false;
            else screenOff = !screenOff;
        }
    }
}

void DeckUi::setTab(int index)
{
    const int old = static_cast<int>(tab);
    if (index < 0 || index > 5) {
        player.chime("bump");
        return;
    }
    rememberRootPosition();
    marqueeRow = touchSelectedTrack = -1;
    if (index != old) {
        sortFields[old] = sortField;
        sortDirections[old] = ascending;
        sortField = sortFields[index];
        ascending = sortDirections[index];
        tab = static_cast<Tab>(index);
        tabStart = tabWidths;
        tabTime = 0;
    }
    details.clear();
    query.clear();
    input = Input::None;
    utility = toolFocus = false;
    utilityAnim = searchAnim = 0;
    openSheet(Sheet::None);
    rowsValid = false;
    keypad = tab != Tab::Deck;
    if (tab == Tab::Deck) {
        selection = 0;
        targetScroll(0, false);
    } else if (tab == Tab::Queue) {
        queueLabelTime = 0.f;
        restoreCatalogPosition(defaultCatalogPosition());
    } else {
        restoreCatalogPosition(rootPositions[index]);
    }
}

void DeckUi::selectRow(int index)
{
    const auto& entries = rows();
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return;
    const auto& row = entries[index];
    if (row.queuePart == QueuePart::EmptyUpNext)
        return;
    marqueeRow = -1;
    touchSelectedTrack = keypad ? -1 : row.track;
    if (row.queuePart != QueuePart::None) {
        if (row.queuePart == QueuePart::Current) {
            player.playToggle();
        } else {
            if (row.queuePart == QueuePart::Played)
                player.playNow(row.track);
            else
                player.playQueued(row.queueIndex);
            focusQueueCurrent();
        }
        return;
    }
    if (row.track >= 0) {
        std::vector<int> order;
        int playIdx = 0;
        for (const auto& entry : entries) {
            if (entry.track >= 0) {
                if (&entry == &row)
                    playIdx = static_cast<int>(order.size());
                order.push_back(entry.track);
            }
        }
        player.playFrom(std::move(order), playIdx);
    } else {
        rememberRootPosition();
        details.push_back({row, catalogPosition()});
        utility = toolFocus = false;
        selection = 0;
        targetScroll(0, false);
    }
    rowsValid = false;
}

void DeckUi::focusQueueCurrent()
{
    const auto& entries = rows();
    const auto current = std::ranges::find(entries, QueuePart::Current, &Row::queuePart);
    selection = current == entries.end() ? 0 : static_cast<int>(current - entries.begin());
    keepSelectedVisible();
}

void DeckUi::followQueuePlaybackChange(int removedIndex, bool prepended)
{
    if (tab != Tab::Queue)
        return;

    // A press started before the queue changed carries an old row index.
    const auto queueRowAction = [](Action action) {
        return action == Action::Row || action == Action::RowMenu || action == Action::PlayNext;
    };
    if (pressed && queueRowAction(pressed->action))
        pressed.reset();
    std::erase_if(heldKeyUi, [&](const auto& key) {
        if (!queueRowAction(key.second.first))
            return false;
        heldKeys.erase(key.first);
        return true;
    });
    if (queueRowAction(lastAction))
        releaseTime = 0;

    if (contextQueueIndex >= 0) {
        if (prepended)
            ++contextQueueIndex;
        else if (contextQueueIndex == removedIndex || player.queue.empty())
            contextQueueIndex = -1;
        else if (removedIndex >= 0 && contextQueueIndex > removedIndex)
            --contextQueueIndex;
    }
    if (selection < 0 || selection >= static_cast<int>(cachedRows.size()))
        return;

    // Read the old cursor and viewport before rebuilding the queue rows.
    const auto& previous = cachedRows[selection];
    const bool inPlayed = previous.queuePart == QueuePart::Played;
    if (inPlayed) {
        if (selection >= static_cast<int>(player.played.size())) {
            restoreCatalogPosition(defaultCatalogPosition());
            return;
        }
        if (scrollTarget < cachedUpNextOffset)
            return;
    }

    const int previousIndex = previous.queueIndex;
    const float previousUpNextOffset = cachedUpNextOffset;
    const float height = expectedCatalogHeight();
    const bool cursorVisible = keypad && previous.box.y + previous.box.h > scrollTarget &&
                               previous.box.y < scrollTarget + height;
    const auto position = defaultCatalogPosition();
    if (!inPlayed) {
        selection = position.selection;
        if (previousIndex >= 0 && prepended)
            selection += previousIndex + 1;
        else if (previousIndex > 0 && removedIndex >= 0 && removedIndex != previousIndex)
            selection += previousIndex - (removedIndex < previousIndex ? 1 : 0);
    }

    const bool wasAboveUpNext = scrollTarget < previousUpNextOffset;
    if (wasAboveUpNext && !cursorVisible)
        return;

    scrollLimit = catalogScrollLimit(height);
    if (cursorVisible && (wasAboveUpNext || selection == position.selection)) {
        targetScroll(position.scroll, false);
    } else {
        const float shift = position.scroll - previousUpNextOffset;
        scroll = std::clamp(scroll + shift, 0.f, scrollLimit);
        scrollFrom = std::clamp(scrollFrom + shift, 0.f, scrollLimit);
        scrollTarget = std::clamp(scrollTarget + shift, 0.f, scrollLimit);
    }
    if (cursorVisible)
        keepSelectedVisible();
}

void DeckUi::editQueueEntry(int index, bool remove)
{
    if (index < 0 || index >= static_cast<int>(player.queue.size()))
        return;
    auto begin = player.queue.begin();
    if (!remove) {
        if (index > 0) {
            std::rotate(begin, begin + index, begin + index + 1);
            ++player.catalogRevision;
        }
        restoreCatalogPosition(defaultCatalogPosition());
        return;
    }

    const auto& entries = rows();
    const int selected = selection >= 0 && selection < static_cast<int>(entries.size())
        ? entries[selection].queueIndex : -1;
    player.queue.erase(begin + index);
    if (selected > index || (selected == index && index > 0 && index == static_cast<int>(player.queue.size())))
        --selection;
    ++player.catalogRevision;
    if (selected >= 0 && keypad)
        keepSelectedVisible();
}

void DeckUi::openContext(int index, int playlist, QueuePart queuePart, int queueIndex)
{
    if (index >= 0 && index < static_cast<int>(player.tracks.size())) {
        contextTrack = index;
        contextPlaylist = playlist;
        contextQueuePart = queuePart;
        contextQueueIndex = queueIndex;
        openSheet(Sheet::Track);
    }
}

void DeckUi::openAlbum(int index)
{
    if (index < 0 || index >= static_cast<int>(player.tracks.size()))
        return;
    const auto& current = player.tracks[index];
    setTab(static_cast<int>(Tab::Albums));
    const auto& collection = rows();
    const auto found = std::ranges::find(collection, current.album, &Row::title);
    if (found != collection.end())
        selectRow(static_cast<int>(found - collection.begin()));
}

void DeckUi::openSheet(Sheet next)
{
    if (sheet == Sheet::Output && next != Sheet::Output && !bluetooth.busy())
        bluetooth.cancel();
    armedBluetoothDevice.clear();
    sheet = next;
    sheetListScroll = 0;
    if (next == Sheet::Output) {
        sheetListLimit = 0;
        bluetooth.refreshPaired();
        sheetSelection = output;
        if (output == bluetoothOutput) {
            const auto& devices = bluetooth.devices();
            const auto active = std::ranges::find_if(devices, [&](const auto& device) {
                return bluetooth.isConnected(device);
            });
            if (active != devices.end())
                sheetSelection += static_cast<int>(active - devices.begin());
        }
        revealAudioOutputRow();
    } else if (next == Sheet::Order) {
        const auto specs = sortSpecsFor(tab);
        sheetSelection = 0;
        for (size_t i = 0; i < specs.size(); ++i)
            if (specs[i].field == sortField)
                sheetSelection = static_cast<int>(i);
    } else {
        sheetSelection = 0;
    }
}

void DeckUi::openInput(Input field)
{
    if (field == Input::Search)
        rememberRootPosition();
    input = field;
    symbols = false;
    keySelection = 0;
    if (field == Input::Search) {
        utility = true;
        toolFocus = false;
    } else if (field == Input::Playlist) {
        if (sheet != Sheet::Playlist) {
            playlistName.clear();
            openSheet(Sheet::Playlist);
        }
    }
}

void DeckUi::closeInput(bool doConfirm)
{
    if (input == Input::Playlist) {
        if (doConfirm && playlistName.find_first_not_of(" \t") != std::string::npos) {
            Playlist value{playlistName, {}};
            if (contextTrack >= 0)
                value.tracks.push_back(contextTrack);
            player.playlists.push_back(std::move(value));
            ++player.catalogRevision;
            playlistName.clear();
            if (contextTrack < 0)
                sheet = Sheet::None;
        }
        input = Input::None;
        rowsValid = false;
        return;
    }
    input = Input::None;
    rowsValid = false;
    targetScroll(0, false);
    selection = 0;
}

void DeckUi::back()
{
    rowsValid = false;
    if (input != Input::None) {
        closeInput(false);
    } else if (sheet == Sheet::Info || (sheet == Sheet::Playlist && contextTrack >= 0)) {
        openSheet(Sheet::Track);
    } else if (sheet != Sheet::None) {
        openSheet(Sheet::None);
    } else if (!details.empty()) {
        auto detail = std::move(details.back());
        details.pop_back();
        marqueeRow = touchSelectedTrack = -1;
        toolFocus = false;
        if (details.empty())
            utilityAnim = searchAnim = query.empty() ? 0.f : 1.f;
        const bool revealAlbum = detail.row.source == Tab::Albums;
        if (revealAlbum)
            detail.parent.item.emplace(detail.row);
        restoreCatalogPosition(detail.parent, revealAlbum);
    } else if (!query.empty()) {
        query.clear();
        toolFocus = false;
        selection = 0;
        targetScroll(0, false);
    } else if (utility || toolFocus) {
        utility = toolFocus = false;
    } else if (tab != Tab::Deck) {
        const auto position = defaultCatalogPosition();
        if (selection != position.selection || even(scroll) != position.scroll || even(scrollTarget) != position.scroll) {
            keypad = true;
            marqueeRow = touchSelectedTrack = -1;
            railPopup = 0.f;
            restoreCatalogPosition(position);
        }
    } else {
        keypad = false;
    }
}

void DeckUi::activate(Action action, int data)
{
    lastAction = action;
    lastData = data;
    releaseTime = .11f;
    switch (action) {
    case Action::Tab: setTab(data); return;
    case Action::Settings: openSheet(Sheet::Settings); break;
    case Action::Context: openContext(data); break;
    case Action::Previous: {
        if (tab == Tab::Queue)
            rows();
        const auto revision = player.catalogRevision;
        const auto queueSize = player.queue.size();
        player.previous();
        if (player.catalogRevision != revision)
            followQueuePlaybackChange(-1, player.queue.size() > queueSize);
        break;
    }
    case Action::Play:
        if (tab == Tab::Queue)
            rows();
        if (const auto removedIndex = player.playToggle())
            followQueuePlaybackChange(*removedIndex);
        break;
    case Action::Next:
        if (tab == Tab::Queue)
            rows();
        followQueuePlaybackChange(player.next());
        break;
    case Action::Star:
        if (data >= 0) {
            player.toggleStar(data);
            player.chime(player.stars[data] ? "star" : "unstar");
        }
        break;
    case Action::Mute:
        if (player.mute || player.volume == 0) {
            player.mute = false;
            if (player.volume == 0)
                player.volume = lastNonZeroVolume > 0 ? lastNonZeroVolume : 76;
        } else {
            lastNonZeroVolume = player.volume;
            player.mute = true;
        }
        volumeTime = .9f;
        break;
    case Action::Output: openSheet(Sheet::Output); break;
    case Action::BluetoothUnpair: {
        const auto* device = audioDevice(data);
        if (sheet != Sheet::Output || bluetooth.busy() || !device || !device->paired)
            break;
        sheetSelection = data;
        if (keypad)
            revealAudioOutputRow();
        if (armedBluetoothDevice == device->id) {
            armedBluetoothDevice.clear();
            bluetooth.unpair(data - bluetoothOutput);
        } else {
            armedBluetoothDevice = device->id;
        }
        break;
    }
    case Action::Stage: cycleStage(); break;
    case Action::Shuffle:
        player.toggleShuffle();
        break;
    case Action::Repeat:
        player.repeat = (player.repeat + 1) % 3;
        break;
    case Action::Album: openAlbum(data); break;
    case Action::Search: openInput(Input::Search); break;
    case Action::ClearSearch:
        query.clear();
        if (input == Input::Search)
            closeInput(false);
        toolFocus = false;
        selection = 0;
        targetScroll(0, false);
        break;
    case Action::Grid:
        albumsInGrid = data == 0 ? !albumsInGrid : data > 0;
        selection = 0;
        targetScroll(0, false);
        break;
    case Action::Order: openSheet(Sheet::Order); break;
    case Action::NewPlaylist:
        if (data == 0) {
            contextTrack = player.current;
            playlistName.clear();
            openSheet(Sheet::Playlist);
        } else {
            openInput(Input::Playlist);
        }
        break;
    case Action::CreatePlaylist:
        if (playlistName.find_first_not_of(" \t") != std::string::npos) {
            Playlist value{playlistName, {}};
            if (contextTrack >= 0)
                value.tracks.push_back(contextTrack);
            player.playlists.push_back(std::move(value));
            ++player.catalogRevision;
            playlistName.clear();
            input = Input::None;
            player.chime("star");
        } else {
            openInput(Input::Playlist);
        }
        break;
    case Action::Back: back(); return;
    case Action::Brightness:
        if (std::clamp(data, 0, 6) != brightness) {
            brightness = std::clamp(data, 0, 6);
            player.chime("tick");
        }
        break;
    case Action::Row: selection = data; selectRow(data); return;
    case Action::PlayNext:
        editQueueEntry(data, false);
        break;
    case Action::RowMenu: {
        const auto entries = rows();
        if (data >= 0 && data < static_cast<int>(entries.size()))
            openContext(entries[data].track, entries[data].playlist, entries[data].queuePart, entries[data].queueIndex);
        break;
    }
    case Action::ListPlay:
    case Action::ListShuffle:
    case Action::DetailQueue: {
        std::vector<int> ids;
        if (!details.empty()) {
            ids = details.back().row.ids;
        } else {
            for (const auto& row : rows())
                for (const int index : row.ids)
                    if (std::ranges::find(ids, index) == ids.end())
                        ids.push_back(index);
        }
        if (ids.empty())
            break;
        if (action == Action::DetailQueue) {
            player.append(ids);
        } else {
            if (action == Action::ListShuffle)
                ids = randomOrder(std::move(ids));
            player.playFrom(std::move(ids));
        }
        break;
    }
    case Action::Key: {
        const auto* key = referenceKey(symbols, data);
        if (!key)
            break;
        keySelection = key->row * keyColumns + key->column + key->span / 2;
        if (key->action == "mode") {
            symbols = !symbols;
        } else if (key->action == "bksp") {
            eraseCharacter();
        } else if (key->action == "clear") {
            (input == Input::Playlist ? playlistName : query).clear();
            selection = 0;
            targetScroll(0, false);
        } else if (key->action == "done") {
            closeInput(true);
        } else {
            text(key->action == "space" ? " " : key->text);
        }
        break;
    }
    case Action::Option:
        switch (sheet) {
        case Sheet::Settings:
            if (data >= 0 && data < 3) {
                theme = static_cast<Theme>(data);
            } else if (data >= 10 && data < 14) {
                themeAccents[static_cast<int>(theme)] = data - 10;
            } else if (data == 3 || data == 4) {
                pocket = data == 3;
            } else if (data == 5 || data == 6) {
                player.sfx = data == 5;
            } else if (data >= 20 && data < 23) {
                player.sfxVolume = data - 20;
            } else if (data == 30 || data == 31) {
                player.sfxSpeaker = (data == 31);
            } else if (data == 50 || data == 51) {
                clock24h = (data == 51);
#if DECKDS_ENABLE_SET_CLOCK
            } else if (data >= 60 && data <= 64) {
                clockBtnFocus = data - 60;
                if (data == 60) clockOffsetMinutes = ((clockOffsetMinutes - 60) % 1440 + 1440) % 1440;
                else if (data == 61) clockOffsetMinutes = (clockOffsetMinutes + 60) % 1440;
                else if (data == 62) clockOffsetMinutes = ((clockOffsetMinutes - 1) % 1440 + 1440) % 1440;
                else if (data == 63) clockOffsetMinutes = (clockOffsetMinutes + 1) % 1440;
                else clockOffsetMinutes = 0;
#endif
            }
            break;
        case Sheet::Output: {
            if (bluetooth.busy() || data < audioHeaderSelection || data >= audioOutputRows())
                break;
            armedBluetoothDevice.clear();
            sheetSelection = data;
            if (data == audioHeaderSelection) {
                if (bluetooth.state() != BluetoothPairing::State::Searching)
                    bluetooth.scan();
            } else if (data < bluetoothOutput) {
                output = data;
            } else if (const auto* device = audioDevice(data);
                       device && (output != bluetoothOutput || !bluetooth.isConnected(*device))) {
                bluetooth.pair(data - bluetoothOutput);
            }
            break;
        }
        case Sheet::Order: {
            const auto specs = sortSpecsFor(tab);
            const int nFields = static_cast<int>(specs.size());
            if (data >= 0 && data < nFields) {
                const bool wasSame = (sortField == specs[data].field);
                sortField = specs[data].field;
                if (!wasSame)
                    ascending = specs[data].defaultAsc;
            } else if (data == nFields || data == nFields + 1) {
                const auto active = activeSortSpec();
                ascending = active.alphaMode ? (data == nFields) : (data == nFields + 1);
            }
            sortFields[static_cast<int>(tab)] = sortField;
            sortDirections[static_cast<int>(tab)] = ascending;
            selection = 0;
            targetScroll(0, false);
            break;
        }
        case Sheet::Playlist:
            if (contextTrack < 0)
                break;
            if (data == 0) {
                player.toggleStar(contextTrack);
                player.chime(player.stars[contextTrack] ? "star" : "unstar");
            } else if (data - 1 < static_cast<int>(player.playlists.size())) {
                auto& list = player.playlists[data - 1].tracks;
                const auto found = std::ranges::find(list, contextTrack);
                if (found != list.end())
                    list.erase(found);
                else
                    list.push_back(contextTrack);
                ++player.catalogRevision;
            }
            break;
        case Sheet::Track: {
            const auto items = options();
            if (data < 0 || data >= static_cast<int>(items.size()))
                break;
            const int optId = items[data].id;
            const int index = contextTrack;
            if (optId == OptPlayNext) {
                if (contextQueuePart == QueuePart::UpNext)
                    editQueueEntry(contextQueueIndex, false);
                else {
                    player.makeNext(index);
                    if (tab == Tab::Queue && !player.queue.empty() && player.queue.front() == index)
                        restoreCatalogPosition(defaultCatalogPosition());
                }
            } else if (optId == OptGoAlbum) {
                openAlbum(index);
            } else if (optId == OptGoArtist) {
                const auto name = player.tracks[index].artist;
                setTab(static_cast<int>(Tab::Artists));
                const auto artists = rows();
                const auto found = std::ranges::find(artists, name, &Row::title);
                if (found != artists.end()) selectRow(static_cast<int>(found - artists.begin()));
            } else if (optId == OptGetInfo) {
                openSheet(Sheet::Info);
                break;
            } else if (optId == OptAddPlaylist) {
                openSheet(Sheet::Playlist);
                break;
            } else if (optId == OptStar) {
                player.toggleStar(index);
                player.chime(player.stars[index] ? "star" : "unstar");
            } else if (optId == OptAddQueue) {
                player.append({index});
            } else if (optId == OptRemoveQueue) {
                editQueueEntry(contextQueueIndex, true);
            } else if (optId == OptRemovePlaylist) {
                if (contextPlaylist >= 0 && contextPlaylist < static_cast<int>(player.playlists.size())) {
                    std::erase(player.playlists[contextPlaylist].tracks, index);
                    ++player.catalogRevision;
                }
                if (!details.empty())
                    std::erase(details.back().row.ids, index);
            } else if (optId == OptShuffle) {
                player.toggleShuffle();
                break;
            } else if (optId == OptRepeat) {
                player.repeat = (player.repeat + 1) % 3;
                break;
            }
            sheet = Sheet::None;
            break;
        }
        default: break;
        }
        break;
    default: break;
    }
    rowsValid = false;
}

void DeckUi::eraseCharacter()
{
    auto& field = input == Input::Search ? query : playlistName;
    if (field.empty())
        return;
    size_t start = field.size() - 1;
    while (start > 0 && (static_cast<unsigned char>(field[start]) & 0xc0) == 0x80)
        --start;
    field.erase(start);
    rowsValid = false;
    selection = 0;
    targetScroll(0, false);
}

void DeckUi::text(std::string_view value, bool physicalKeyboard)
{
    if (!textInput() || (physicalKeyboard && value == " " && heldKeys.contains(SDLK_SPACE)))
        return;
    if (physicalKeyboard)
        keypad = false;
    auto& field = input == Input::Search ? query : playlistName;
    const size_t maxLen = input == Input::Playlist ? 28 : 156;
    if (field.size() + value.size() <= maxLen)
        field.append(value);
    rowsValid = false;
    selection = 0;
    targetScroll(0, false);
}

void DeckUi::analog(float position)
{
    analogPosition = std::clamp(position, -1.f, 1.f);
}

void DeckUi::toggleLibrary()
{
    player.largeLibrary = !player.largeLibrary;
    rowsValid = false;
    for (auto& detail : details)
        std::erase_if(detail.row.ids, [&](int i) { return i >= player.librarySize(); });
    if (!details.empty() && details.back().row.ids.empty()) {
        const auto parent = std::move(details.front().parent);
        details.clear();
        restoreCatalogPosition(parent);
        return;
    }
    selection = 0;
    targetScroll(0, false);
}

void DeckUi::pointer(float x, float y, int phase)
{
    if (displaysOff() || showKeys)
        return;
    if (phase == 0) {
        pressed.reset();
        dragging = held = false;
        tabSliding = tabSlideMoved = tabSlideCommitted = false;
        listPointer = sheet == Sheet::None && tab != Tab::Deck && !textInput() && inside(listBox, x, y);
        settingsPointer = sheet == Sheet::Settings && inside(settingsBox, x, y);
        sheetListPointer = (sheet == Sheet::Info || sheet == Sheet::Output) && inside(sheetListBox, x, y);
        pressTime = 0;
        pointerStartX = pointerLastX = x;
        pointerStartY = pointerLastY = y;
        keypad = false;

        const auto match = std::find_if(hits.rbegin(), hits.rend(), [=](const Hit& hit) {
            return inside(hit.box, x, y);
        });
        if (match != hits.rend()) {
            pressed = *match;
            pressVoice = voiceFor(match->action, match->data);
            if (match->action == Action::Tab && match->data >= 0 && match->data < 6 && y < 76) {
                tabSliding = true;
                slideTab = match->data;
                tabSlideDwell = 0;
                if (slideTab != static_cast<int>(tab)) {
                    tabStart = tabWidths;
                    tabTime = 0;
                }
                player.chime("deck", false);
            } else if (match->action == Action::Scrub || match->action == Action::Volume || match->action == Action::Brightness) {
                scrubPosition(match->action, x);
            } else if (match->action == Action::Rail) {
                scrubRail(y);
            } else if (match->action == Action::SettingsRail) {
                scrubSettingsRail(y);
            } else if (match->action == Action::OutputRail) {
                scrubOutputRail(y);
            } else if (match->action == Action::Tab && (match->data < 0 || match->data >= 6)) {
                player.chime("bump");
            } else {
                player.chime(pressVoice, false);
            }
        } else if (y < 76 && x >= 88 && x < 936) {
            // Started in the 8px gap between tabs: still enter slide-select!
            tabSliding = true;
            slideTab = std::clamp(static_cast<int>((x - 92) / 140), 0, 5);
            tabSlideDwell = 0;
            player.chime("deck", false);
        }
        return;
    }

    if (phase == 1) {
        if (tabSliding) {
            if (std::abs(x - pointerStartX) > 8.f)
                tabSlideMoved = true;
            int hovered = slideTab;
            if (x < tabRects[0].x) {
                hovered = 0;
            } else if (x >= tabRects[5].x + tabRects[5].w) {
                hovered = 5;
            } else {
                for (int i = 0; i < 6; ++i) {
                    const float left = tabRects[i].x - 4.f;
                    const float right = tabRects[i].x + tabRects[i].w + 4.f;
                    if (x >= left && x < right) {
                        hovered = i;
                        break;
                    }
                }
            }
            if (hovered != slideTab) {
                slideTab = hovered;
                tabStart = tabWidths;
                tabTime = 0;
                tabSlideDwell = 0;
                tabSlideCommitted = false;
                player.chime("tick");
            } else if (std::hypot(x - pointerLastX, y - pointerLastY) > 2.f) {
                tabSlideDwell = 0;
            }
            pointerLastX = x;
            pointerLastY = y;
            return;
        }

        if (pressed && (pressed->action == Action::Volume || pressed->action == Action::Scrub || pressed->action == Action::Brightness)) {
            scrubPosition(pressed->action, x);
        } else if (pressed && pressed->action == Action::Rail) {
            scrubRail(y);
        } else if (pressed && pressed->action == Action::SettingsRail) {
            scrubSettingsRail(y);
        } else if (pressed && pressed->action == Action::OutputRail) {
            scrubOutputRail(y);
        } else if (sheet == Sheet::Settings && settingsPointer &&
                   (dragging || std::abs(y - pointerStartY) > 12)) {
            dragging = true;
            targetSettingsScroll(settingsScroll + (pointerLastY - y), false);
        } else if ((sheet == Sheet::Info || sheet == Sheet::Output) && sheetListPointer &&
                   (dragging || std::abs(y - pointerStartY) > 12)) {
            dragging = true;
            const int delta = static_cast<int>(std::lround(pointerLastY - y));
            sheetListScroll = std::clamp(sheetListScroll + delta, 0, sheetListLimit);
        } else if (((pressed && pressed->action == Action::Row) || (!pressed && listPointer)) &&
                   (dragging || std::abs(y - pointerStartY) > 12)) {
            dragging = true;
            const float dy = y - pointerLastY;
            if (scroll <= 0 && dy > 0 && details.empty() && tab != Tab::Queue) {
                utility = true;
            } else if (scroll <= 0 && dy < 0 && utility && query.empty()) {
                utility = toolFocus = false;
            } else {
                targetScroll(scroll - dy, false);
            }
        }
        pointerLastX = x;
        pointerLastY = y;
        return;
    }

    listPointer = settingsPointer = sheetListPointer = false;
    if (tabSliding) {
        tabSliding = false;
        pressed.reset();
        if (!tabSlideCommitted) {
            if (y < 140.f) {
                setTab(slideTab);
                player.chime("deck", true);
            } else {
                tabStart = tabWidths;
                tabTime = 0;
            }
        }
        return;
    }

    if (!pressed)
        return;
    const auto item = *pressed;
    pressed.reset();
    if (item.action == Action::Volume)
        volumeTime = .7f;
    lastAction = item.action;
    lastData = item.data;
    releaseTime = .11f;
    if (!dragging && !held && inside(item.box, x, y) &&
        item.action != Action::Scrub && item.action != Action::Volume &&
        item.action != Action::Rail && item.action != Action::SettingsRail && item.action != Action::OutputRail &&
        item.action != Action::Brightness) {
        if (item.action == Action::Tab && (item.data < 0 || item.data >= 6))
            return;
        const bool isStarOption = item.action == Action::Option &&
            ((sheet == Sheet::Track && item.data >= 0 && item.data < static_cast<int>(options().size()) &&
              options()[item.data].id == OptStar) ||
             (sheet == Sheet::Playlist && item.data == 0));
        activate(item.action, item.data);
        if (item.action != Action::Star && item.action != Action::CreatePlaylist && !isStarOption)
            player.chime(pressVoice, true);
    }
}

std::string_view DeckUi::voiceFor(Action action, int data) const
{
    switch (action) {
    case Action::Tab: return "deck";
    case Action::Play: return player.playing ? "pause" : "play";
    case Action::Stage: return "stage";
    case Action::Back:
    case Action::ClearSearch: return "back";
    case Action::PlayNext:
    case Action::DetailQueue: return "add";
    case Action::Row:
    case Action::Album: return "confirm";
    case Action::ListPlay:
    case Action::ListShuffle: return "play";
    case Action::Key:
        if (const auto* k = referenceKey(symbols, data)) {
            if (k->action == "done") return "confirm";
            if (k->action == "bksp" || k->action == "clear") return "back";
        }
        return {};
    case Action::Option:
        if (sheet == Sheet::Settings)
            return {};
        if (sheet == Sheet::Playlist) {
            if (data > 0 && data - 1 < static_cast<int>(player.playlists.size()) && contextTrack >= 0) {
                const auto& trks = player.playlists[data - 1].tracks;
                return std::ranges::find(trks, contextTrack) != trks.end() ? "back" : "add";
            }
            return {};
        }
        if (sheet == Sheet::Track) {
            const auto items = options();
            if (data >= 0 && data < static_cast<int>(items.size())) {
                const int optId = items[data].id;
                if (items[data].icon == "trash") return "back";
                if (optId == OptPlayNext || optId == OptAddQueue) return "add";
                if (optId == OptShuffle || optId == OptRepeat || optId == OptStar) return {};
            }
        }
        return "confirm";
    default: return {};
    }
}

void DeckUi::wheel(float direction)
{
    if (sheet == Sheet::Info || sheet == Sheet::Output) {
        sheetListScroll = std::clamp(sheetListScroll - static_cast<int>(direction * 96), 0, sheetListLimit);
        return;
    }
    if (sheet == Sheet::Settings) {
        targetSettingsScroll(settingsScrollTarget - direction * 96);
        return;
    }
    if (sheet != Sheet::None || tab == Tab::Deck || textInput())
        return;
    if (tab == Tab::Queue && direction != 0)
        keypad = false;
    if (direction > 0 && scroll <= 0 && details.empty() && tab != Tab::Queue) {
        utility = true;
    } else if (direction < 0 && utility && query.empty()) {
        utility = toolFocus = false;
    } else {
        targetScroll(scrollTarget - direction * 96);
    }
}

void DeckUi::cancelInput()
{
    armedBluetoothDevice.clear();
    pressed.reset();
    heldKeys.clear();
    heldKeyUi.clear();
    tabSliding = tabSlideMoved = tabSlideCommitted = false;
    seekDirection = 0;
    analogPosition = 0;
    powerDown = false;
    dragging = false;
    listPointer = settingsPointer = sheetListPointer = false;
}

void DeckUi::libraryKey(SDL_Keycode value)
{
    const auto& entries = rows();
    const bool detail = !details.empty();
    const bool rootUtility = !detail && tab != Tab::Queue;
    const bool fullSearch = rootUtility && (!query.empty() || input == Input::Search);
    if (toolFocus) {
        const int upper = detail ? 2 : fullSearch ? 1 : tab == Tab::Albums ? 5 : tab == Tab::Playlists ? 1 : 3;
        toolSelection = std::clamp(toolSelection, 0, upper);
        if (value == SDLK_LEFT || value == SDLK_RIGHT) {
            const int next = std::clamp(toolSelection + (value == SDLK_LEFT ? -1 : 1), 0, upper);
            if (next == toolSelection) player.chime("bump");
            toolSelection = next;
        } else if (value == SDLK_DOWN) {
            toolFocus = false;
            if (query.empty()) utility = false;
        } else if (value == SDLK_UP) {
            player.chime("bump");
        } else if (confirm(value)) {
            if (detail) {
                const Action actions[] = {Action::ListShuffle, Action::ListPlay, Action::DetailQueue};
                activate(actions[toolSelection], 0);
            } else if (fullSearch) {
                activate(toolSelection == 0 ? Action::Search : Action::ClearSearch, 0);
            } else if (tab == Tab::Playlists) {
                activate(toolSelection == 0 ? Action::Search : Action::NewPlaylist, 0);
            } else if (tab == Tab::Albums) {
                const Action actions[] = {Action::Search, Action::ListShuffle, Action::ListPlay,
                                           Action::Grid, Action::Grid, Action::Order};
                activate(actions[toolSelection], toolSelection == 3 ? 1 : toolSelection == 4 ? -1 : 0);
            } else {
                const Action actions[] = {Action::Search, Action::ListShuffle, Action::ListPlay, Action::Order};
                activate(actions[toolSelection], 0);
            }
        }
        return;
    }

    const int count = static_cast<int>(entries.size());
    if (confirm(value)) {
        if (count > 0)
            selectRow(selection);
        else
            player.chime("bump");
        return;
    }

    // Artist Detail mixed 2-col Albums + 1-col Songs navigation!
    if (detail && details.back().row.source == Tab::Artists) {
        const int albCount = cachedArtistAlbumCount;
        int next = selection;
        if (value == SDLK_UP) {
            if (selection < albCount) {
                if (selection >= 2) next = selection - 2;
                else {
                    toolFocus = true;
                    toolSelection = 1;
                    targetScroll(0);
                    return;
                }
            } else if (selection == albCount) {
                if (albCount > 0) next = albCount - 1;
                else {
                    toolFocus = true;
                    toolSelection = 1;
                    targetScroll(0);
                    return;
                }
            } else {
                next = selection - 1;
            }
        } else if (value == SDLK_DOWN) {
            if (selection < albCount) {
                if (selection + 2 < albCount) next = selection + 2;
                else if (selection / 2 < (albCount - 1) / 2) next = albCount - 1;
                else if (count > albCount) next = albCount;
            } else {
                next = std::min(count - 1, selection + 1);
            }
        } else if (value == SDLK_LEFT) {
            if (selection < albCount) {
                if (selection % 2 == 1) next = selection - 1;
            } else {
                next = std::max(albCount, selection - 4);
            }
        } else if (value == SDLK_RIGHT) {
            if (selection < albCount) {
                if (selection % 2 == 0 && selection + 1 < albCount) next = selection + 1;
            } else {
                next = std::min(count - 1, selection + 4);
            }
        }
        if ((value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) && next == selection)
            player.chime("bump");
        selection = std::clamp(next, 0, std::max(0, count - 1));
        keepSelectedVisible();
        return;
    }

    const int columns = gridColumns();
    if (value == SDLK_UP && (count == 0 || selection < columns) && (detail || rootUtility)) {
        toolFocus = true;
        toolSelection = detail ? 1 : fullSearch ? 0 : tab == Tab::Playlists ? 0 : 2;
        utility = rootUtility;
        if (detail) targetScroll(0);
        return;
    }
    const float nomRowH = entries.empty() ? listRowHeight + listRowGap : entries[0].box.h + listRowGap;
    const int page = std::max(4, static_cast<int>(listBox.h / nomRowH));
    int next = selection;
    if (value == SDLK_UP) next -= columns;
    if (value == SDLK_DOWN) next += columns;
    if (value == SDLK_LEFT) next -= columns == 1 ? page : 1;
    if (value == SDLK_RIGHT) next += columns == 1 ? page : 1;
    const int clamped = std::clamp(next, 0, std::max(0, count - 1));
    if ((value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) && clamped == selection)
        player.chime("bump");
    selection = clamped;
    if (rootUtility && utility && query.empty() && selection >= columns)
        utility = false;
    keepSelectedVisible();
}

void DeckUi::sheetKey(SDL_Keycode value)
{
    if (sheet == Sheet::Info) {
        if (value == SDLK_UP || value == SDLK_DOWN) {
            const int next = std::clamp(sheetListScroll + (value == SDLK_UP ? -140 : 140), 0, sheetListLimit);
            if (next == sheetListScroll) player.chime("bump");
            sheetListScroll = next;
        }
        if (confirm(value))
            back();
        return;
    }
    if (sheet == Sheet::Settings) {
        if (value == SDLK_UP || value == SDLK_DOWN) {
            const int next = std::clamp(sheetSelection + (value == SDLK_UP ? -1 : 1), 0, SettingsRowCount - 1);
            if (next == sheetSelection) player.chime("bump");
            sheetSelection = next;
            keepSettingsVisible();
            return;
        }
        if (value == SDLK_LEFT || value == SDLK_RIGHT || confirm(value)) {
            const int delta = value == SDLK_LEFT ? -1 : 1;
            if (sheetSelection == BrightnessRow && !confirm(value)) {
                const int prev = brightness;
                activate(Action::Brightness, brightness + delta);
                if (brightness == prev) player.chime("bump");
            } else if (sheetSelection == TimeFormatRow) {
                activate(Action::Option, confirm(value) ? (clock24h ? 50 : 51) : (delta < 0 ? 50 : 51));
#if DECKDS_ENABLE_SET_CLOCK
            } else if (sheetSelection == SetClockRow) {
                if (confirm(value)) {
                    activate(Action::Option, 60 + std::clamp(clockBtnFocus, 0, 4));
                } else {
                    const int next = std::clamp(clockBtnFocus + delta, 0, 4);
                    if (next == clockBtnFocus) player.chime("bump");
                    clockBtnFocus = next;
                }
#endif
            } else if (sheetSelection == VisualThemeRow) {
                const int next = (static_cast<int>(theme) + delta + 3) % 3;
                activate(Action::Option, next);
            } else if (sheetSelection == AccentRow) {
                const int cur = currentAccent();
                activate(Action::Option, 10 + (cur + delta + 4) % 4);
            } else if (sheetSelection == SfxRow) {
                activate(Action::Option, confirm(value) ? (player.sfx ? 6 : 5) : (delta < 0 ? 5 : 6));
            } else if (sheetSelection == SfxVolumeRow) {
                const int next = confirm(value) ? ((player.sfxVolume + 1) % 3)
                                                : std::clamp(player.sfxVolume + delta, 0, 2);
                activate(Action::Option, 20 + next);
            } else if (sheetSelection == SfxOutputRow) {
                activate(Action::Option, confirm(value) ? (player.sfxSpeaker ? 30 : 31) : (delta < 0 ? 30 : 31));
            } else if (sheetSelection == LidCloseRow) {
                activate(Action::Option, confirm(value) ? (pocket ? 4 : 3) : (delta < 0 ? 3 : 4));
            }
        }
        return;
    }
    if (sheet == Sheet::Playlist) {
        const int listCount = static_cast<int>(options().size());
        const int maxIdx = listCount + 1; // listCount = input box, listCount + 1 = Create btn
        if (confirm(value)) {
            if (sheetSelection < listCount) activate(Action::Option, sheetSelection);
            else if (sheetSelection == listCount) activate(Action::NewPlaylist, 1);
            else activate(Action::CreatePlaylist, 0);
            return;
        }
        int next = sheetSelection;
        if (value == SDLK_UP) next = sheetSelection >= listCount ? listCount - 1 : sheetSelection - 1;
        if (value == SDLK_DOWN) next = sheetSelection < listCount ? sheetSelection + 1 : maxIdx;
        if (value == SDLK_LEFT && sheetSelection == maxIdx) next = listCount;
        if (value == SDLK_RIGHT && sheetSelection == listCount) next = maxIdx;
        const int clamped = std::clamp(next, 0, maxIdx);
        if ((value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) && clamped == sheetSelection)
            player.chime("bump");
        sheetSelection = clamped;
        return;
    }

    const bool audioSheet = sheet == Sheet::Output;
    const int count = audioSheet ? audioOutputRows() : static_cast<int>(options().size());
    const int columns = sheet == Sheet::Track || sheet == Sheet::Order ? 2 : 1;
    if (confirm(value) && (count > 0 || (audioSheet && sheetSelection == audioHeaderSelection))) {
        activate(Action::Option, sheetSelection);
        return;
    }
    int next = sheetSelection;
    if (value == SDLK_UP) next -= columns;
    if (value == SDLK_DOWN) next += columns;
    if (value == SDLK_LEFT && columns == 2) --next;
    if (value == SDLK_RIGHT && columns == 2) ++next;
    const int first = audioSheet ? audioHeaderSelection : 0;
    const int clamped = std::clamp(next, first, std::max(0, count - 1));
    if ((value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) && clamped == sheetSelection)
        player.chime("bump");
    if (audioSheet && clamped != sheetSelection)
        armedBluetoothDevice.clear();
    sheetSelection = clamped;
    if (audioSheet)
        revealAudioOutputRow();
}

void DeckUi::key(SDL_Keycode value, bool down, bool repeated, bool controller)
{
    const SDL_Keycode rawKey = value;
    if (!controller && !textInput()) {
        switch (value) {
        case SDLK_C: value = SDLK_X; break;
        case SDLK_X:
        case SDLK_BACKSPACE: value = SDLK_B; break;
        case SDLK_V: value = SDLK_Y; break;
        case SDLK_1: value = SDLK_LEFTBRACKET; break;
        case SDLK_2: value = SDLK_RIGHTBRACKET; break;
        case SDLK_D: value = SDLK_G; break;
        default: break;
        }
    }
    if (value == SDLK_POWER || (value == SDLK_P && !textInput())) {
        if (!repeated)
            player.chime("", !down);
        power(down, repeated);
        return;
    }
    if (!down) {
        if (const auto uiIt = heldKeyUi.find(rawKey); uiIt != heldKeyUi.end()) {
            lastAction = uiIt->second.first;
            lastData = uiIt->second.second;
            releaseTime = .11f;
            heldKeyUi.erase(uiIt);
        }
        const auto heldIt = heldKeys.find(rawKey);
        if ((value == SDLK_LEFT && seekDirection == -1) || (value == SDLK_RIGHT && seekDirection == 1)) {
            if (heldIt != heldKeys.end())
                heldKeys.erase(heldIt);
            if (seekHold < .32f) {
                if (seekDirection < 0)
                    activate(Action::Previous, 0);
                else
                    activate(Action::Next, 0);
                player.chime("", true);
            }
            seekDirection = 0;
            return;
        }
        if (heldIt != heldKeys.end()) {
            auto key = std::move(heldIt->second);
            heldKeys.erase(heldIt);
            if (key.voice != "NONE")
                player.chime(key.voice, true);
            if (key.onRelease)
                key.onRelease();
        }
        return;
    }

    const auto beginTwoStep = [&](std::string_view voice, Action uiAct = Action::None, int uiData = 0,
                                  std::function<void()> onRelease = {}) {
        if (!repeated) {
            if (voice != "NONE")
                player.chime(voice, false);
            heldKeys[rawKey] = {voice, std::move(onRelease)};
            if (uiAct != Action::None)
                heldKeyUi[rawKey] = {uiAct, uiData};
        }
    };
    const auto holdAction = [&](std::string_view voice, Action action, int data = 0) {
        beginTwoStep(voice, action, data, [this, action, data] { activate(action, data); });
    };
    const auto holdBack = [&](Action uiAction) {
        beginTwoStep("back", uiAction, 0, [this, from = tab, editing = input, started = SDL_GetTicks()] {
            if (tab != from || input != editing)
                return;
            if (from != Tab::Deck && SDL_GetTicks() - started >= backHoldMs)
                activate(Action::Tab, static_cast<int>(Tab::Deck));
            else
                activate(Action::Back, 0);
        });
    };

    if (value == SDLK_F1 && !repeated) {
        beginTwoStep({}, Action::None, 0, [this] { showKeys = !showKeys; });
        return;
    }
    if (value == SDLK_F2 && !repeated) {
        beginTwoStep({}, Action::None, 0, [this] {
            scaleMode = static_cast<PreviewScale>((static_cast<int>(scaleMode) + 1) %
                                                 static_cast<int>(PreviewScale::Count));
        });
        return;
    }
    if (showKeys) {
        if (value == SDLK_ESCAPE && !repeated)
            beginTwoStep("back", Action::None, 0, [this] { showKeys = false; });
        return;
    }
    if (value == SDLK_L && !repeated && (controller || !textInput())) {
        beginTwoStep({}, Action::None, 0, [this] { toggleLid(); });
        return;
    }
    if (poweredOff)
        return;
    if (value == SDLK_VOLUMEUP || value == SDLK_VOLUMEDOWN) {
        if (repeated) player.chime("tick");
        else beginTwoStep({});
        player.adjustVolume(value == SDLK_VOLUMEUP ? 5 : -5);
        if (player.volume > 0) lastNonZeroVolume = player.volume;
        volumeTime = .9f;
        return;
    }
    if (displaysOff() && value != SDLK_SPACE && value != SDLK_LEFTBRACKET && value != SDLK_RIGHTBRACKET)
        return;
    if (displaysOff()) {
        if (!repeated) {
            if (value == SDLK_SPACE) {
                holdAction(voiceFor(Action::Play), Action::Play);
            } else if (value == SDLK_LEFTBRACKET) {
                holdAction({}, Action::Previous);
            } else {
                holdAction({}, Action::Next);
            }
        }
        return;
    }
    if (textInput()) {
        if (value == SDLK_SPACE && !repeated) {
            const auto* done = std::ranges::find_if(keyAssets, [this](const KeyAsset& key) {
                return key.symbols == symbols && key.action == "done";
            });
            if (done != std::end(keyAssets)) {
                const int code = done->row * keyColumns + done->column + done->span / 2;
                beginTwoStep("confirm", Action::Key, code, [this, field = input, code] {
                    if (input == field) activate(Action::Key, code);
                });
            }
        } else if (value == SDLK_ESCAPE || (controller && value == SDLK_B)) {
            holdBack(Action::ClearSearch);
        } else if (value == SDLK_BACKSPACE || (controller && value == SDLK_Y)) {
            int bkspCode = 9;
            for (const auto& k : keyAssets)
                if (k.symbols == symbols && k.action == "bksp")
                    bkspCode = k.row * keyColumns + k.column + k.span / 2;
            beginTwoStep("back", Action::Key, bkspCode, [this, field = input] {
                if (input == field) eraseCharacter();
            });
        } else if (value == SDLK_RETURN || value == SDLK_KP_ENTER) {
            if (!repeated) {
                if (controller || keypad) {
                    const auto* k = referenceKey(symbols, keySelection);
                    const int code = k ? (k->row * keyColumns + k->column + k->span / 2) : keySelection;
                    beginTwoStep(voiceFor(Action::Key, code), Action::Key, code,
                                 [this, code, field = input, wasSymbols = symbols] {
                        if (input == field && symbols == wasSymbols) activate(Action::Key, code);
                    });
                } else {
                    beginTwoStep("confirm", Action::None, 0, [this, field = input] {
                        if (input == field) closeInput(true);
                    });
                }
            }
        } else if (value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) {
            keypad = true;
            const auto* key = referenceKey(symbols, keySelection);
            if (key) {
                const int row = keySelection / keyColumns;
                int nextSel = keySelection;
                if (value == SDLK_UP || value == SDLK_DOWN) {
                    const int nextRow = std::clamp(row + (value == SDLK_UP ? -1 : 1), 0, 3);
                    nextSel = keySelection % keyColumns + nextRow * keyColumns;
                } else {
                    const int nextCol = value == SDLK_LEFT ? key->column - 1 : key->column + key->span;
                    nextSel = row * keyColumns + std::clamp(nextCol, 0, keyColumns - 1);
                }
                if (nextSel == keySelection) {
                    player.chime("bump");
                    beginTwoStep("NONE");
                } else if (repeated) {
                    player.chime("tick");
                } else {
                    beginTwoStep({});
                }
                keySelection = nextSel;
            }
        }
        return;
    }
    if (value == SDLK_SPACE && !repeated) {
        holdAction(voiceFor(Action::Play), Action::Play);
        return;
    }
    if (value == SDLK_LEFTBRACKET && !repeated) { holdAction({}, Action::Previous); return; }
    if (value == SDLK_RIGHTBRACKET && !repeated) { holdAction({}, Action::Next); return; }
    if (value == SDLK_H && !repeated) {
        beginTwoStep("confirm", Action::Album, player.current, [this, index = player.current] {
            details.empty() ? openAlbum(index) : setTab(0);
        });
        return;
    }
    if (value == SDLK_G && !repeated) {
        player.chime("", false);
        holdAction("NONE", Action::Star, player.current);
        return;
    }
    if (value == SDLK_ESCAPE || value == SDLK_B) {
        if (sheet == Sheet::Output && !armedBluetoothDevice.empty()) {
            beginTwoStep("back", Action::None, 0, [this] { armedBluetoothDevice.clear(); });
        } else {
            holdBack(Action::Back);
        }
        return;
    }
    if (sheet != Sheet::None) {
        keypad = true;
        if (sheet == Sheet::Output && value == SDLK_Y) {
            const auto* device = audioDevice(sheetSelection);
            if (!repeated && !bluetooth.busy() && device && device->paired) {
                beginTwoStep("confirm", Action::BluetoothUnpair, sheetSelection,
                             [this, row = sheetSelection, id = device->id] {
                    const auto* current = audioDevice(row);
                    if (sheet == Sheet::Output && sheetSelection == row && current && current->id == id)
                        activate(Action::BluetoothUnpair, row);
                });
            }
            return;
        }
        const bool isConfirm = confirm(value);
        bool clockRow = false;
#if DECKDS_ENABLE_SET_CLOCK
        clockRow = sheet == Sheet::Settings && sheetSelection == SetClockRow;
#endif
        const bool adjustsSetting = sheet == Sheet::Settings && !clockRow &&
                                    (value == SDLK_LEFT || value == SDLK_RIGHT);
        if (isConfirm || adjustsSetting) {
            if (repeated)
                return;
            const int target = clockRow ? 60 + clockBtnFocus : sheetSelection;
            const bool isStar = isConfirm && ((sheet == Sheet::Track && target >= 0 &&
                target < static_cast<int>(options().size()) && options()[target].id == OptStar) ||
                (sheet == Sheet::Playlist && target == 0));
            const auto voice = sheet == Sheet::Info ? std::string_view("back") : voiceFor(Action::Option, target);
            if (isStar)
                player.chime("", false);
            beginTwoStep(isStar ? "NONE" : voice, isConfirm ? Action::Option : Action::None, target,
                         [this, value, shown = sheet, row = sheetSelection, clockRow, clockFocus = clockBtnFocus] {
                if (sheet == shown && sheetSelection == row && (!clockRow || clockBtnFocus == clockFocus))
                    sheetKey(value);
            });
        } else {
            if (repeated) player.chime("tick");
            else beginTwoStep({});
            sheetKey(value);
        }
        return;
    }
    if (value == SDLK_Q || value == SDLK_E) {
        if (!repeated) {
            const int cur = static_cast<int>(tab);
            const int index = cur + (value == SDLK_Q ? -1 : 1);
            holdAction(index >= 0 && index < 6 ? "deck" : "NONE", Action::Tab, index);
        }
        return;
    }
    if (value == SDLK_X && !repeated) {
        holdAction("stage", Action::Stage);
        return;
    }
    if (value == SDLK_S || value == SDLK_TAB) {
        if (tab == Tab::Deck) holdAction({}, Action::Settings);
        else if (tab == Tab::Playlists) holdAction({}, Action::NewPlaylist);
        else if (tab != Tab::Queue) holdAction({}, Action::Order);
        return;
    }
    if (value == SDLK_Y && !repeated) {
        if (tab == Tab::Deck) holdAction({}, Action::Context, player.current);
        else holdAction({}, Action::RowMenu, selection);
        return;
    }
    if ((value == SDLK_SLASH || value == SDLK_F) && tab != Tab::Deck && tab != Tab::Queue && details.empty()) {
        holdAction({}, Action::Search);
        return;
    }
    if (tab != Tab::Deck) {
        keypad = true;
        if (!repeated || !confirm(value)) {
            if (confirm(value)) {
                std::string_view v = "confirm";
                Action uiAct = Action::Row;
                int uiData = selection;
                if (toolFocus) {
                    const bool detail = !details.empty();
                    const bool fullSearch = !detail && (!query.empty() || input == Input::Search);
                    if (detail) {
                        const Action acts[3] = {Action::ListShuffle, Action::ListPlay, Action::DetailQueue};
                        uiAct = acts[std::clamp(toolSelection, 0, 2)];
                        uiData = 0;
                        v = toolSelection == 2 ? "add" : "play";
                    } else if (fullSearch) {
                        uiAct = toolSelection == 0 ? Action::Search : Action::ClearSearch;
                        uiData = 0;
                        v = toolSelection == 0 ? std::string_view{} : "back";
                    } else if (tab == Tab::Playlists) {
                        uiAct = toolSelection == 0 ? Action::Search : Action::NewPlaylist;
                        uiData = 0;
                        v = {};
                    } else if (tab == Tab::Albums) {
                        const Action acts[6] = {Action::Search, Action::ListShuffle, Action::ListPlay,
                                                Action::Grid, Action::Grid, Action::Order};
                        const int s = std::clamp(toolSelection, 0, 5);
                        uiAct = acts[s];
                        uiData = s == 3 ? 1 : s == 4 ? -1 : 0;
                        v = (s == 1 || s == 2) ? "play" : std::string_view{};
                    } else {
                        const Action acts[4] = {Action::Search, Action::ListShuffle, Action::ListPlay, Action::Order};
                        const int s = std::clamp(toolSelection, 0, 3);
                        uiAct = acts[s];
                        uiData = 0;
                        v = (s == 1 || s == 2) ? "play" : std::string_view{};
                    }
                }
                const auto& entries = rows();
                if (!toolFocus && (selection < 0 || selection >= static_cast<int>(entries.size()) ||
                                   entries[selection].queuePart == QueuePart::EmptyUpNext))
                    beginTwoStep("NONE");
                else
                    beginTwoStep(v, uiAct, uiData,
                                 [this, value, from = tab, depth = details.size(), row = selection,
                                  toolbar = toolFocus, tool = toolSelection] {
                        if (sheet == Sheet::None && tab == from && details.size() == depth && toolFocus == toolbar &&
                            (toolbar ? toolSelection == tool : selection == row))
                            libraryKey(value);
                    });
            } else {
                if (repeated) player.chime("tick");
                else beginTwoStep({});
                libraryKey(value);
            }
        }
        return;
    }
    if (value == SDLK_UP || value == SDLK_DOWN) {
        if (repeated) player.chime("tick");
        else beginTwoStep({});
        player.adjustVolume(value == SDLK_UP ? 5 : -5);
        if (player.volume > 0) lastNonZeroVolume = player.volume;
        volumeTime = .9f;
    } else if ((value == SDLK_LEFT || value == SDLK_RIGHT) && !repeated) {
        beginTwoStep({}, value == SDLK_LEFT ? Action::Previous : Action::Next, 0);
        seekDirection = value == SDLK_LEFT ? -1 : 1;
        seekHold = 0;
    } else if (confirm(value) && !repeated) {
        holdAction(voiceFor(Action::Play), Action::Play);
    }
}

void DeckUi::useLongMetadataFixture()
{
    auto& track = player.tracks.front();
    track.title = "真夜中の電波 — The Last Train Home Through the Sleeping City, "
                  "Across the River and Past the Streetlights (Unabridged Late Night Recording)";
    track.artist = "A Midnight Collective 夜の音楽隊 with the Full Metropolitan Chamber Ensemble, "
                   "the Station Choir and Guest Musicians from the Riverside Sessions";
    track.album = "深夜放送の記録 — Notes from the Midnight Radio Sessions, Volume Three: "
                  "The Complete Broadcast Recordings with Rehearsals and Alternate Takes";
    track.added = "2099-01-01";
    player.stars.front() = true;
    player.played = {2, 3};
    player.history = player.played;
    std::erase(player.queue, 2);
    std::erase(player.queue, 3);
    rowsValid = false;
}

void DeckUi::useBluetoothFixture()
{
    bluetooth.useDemoDevices();
    openSheet(Sheet::Output);
    keypad = true;
}

void DeckUi::configureSnapshot(int preset)
{
    player.sfx = false;
    cancelInput();
    keyboardHeight = 0.f;
    utilityAnim = 0.f;
    searchAnim = 0.f;
    catBtnAnim = {};
    detailBtnAnim = {};
    settingsScroll = settingsScrollTarget = 0.f;
    settingsScrollTime = 1.f;
    player.playing = false;
    player.seconds = 0.0;
    theme = Theme::Obsidian;
    clock24h = true;
    clockOffsetMinutes = 0;
    wifiConnected = true;
    clockBtnFocus = 1;
    themeAccents = {0, 0, 0};
    stage = 0;
    setTab(0);
    rootPositions = {};
    for (int i = 0; i < 6; ++i)
        tabWidths[i] = (i == 0 ? 260.f : 108.f);

    switch (preset) {
    case 0: // DECK + Japanese track on Top Cover!
        player.current = 9; // 真夜中の真空管 — 高橋コウジ — 深夜蛍光管セッション
        player.stars[9] = true;
        break;
    case 1: // ALBUMS + -1 Utility Bar revealed with Play focused (expanded label!)
        setTab(1);
        utility = true;
        toolFocus = true;
        toolSelection = 2; // Play
        utilityAnim = 1.f;
        catBtnAnim[1] = 1.f;
        break;
    case 2: // ALBUMS + Search expanded 100% + OSK open
        setTab(1);
        openInput(Input::Search);
        keypad = true;
        utilityAnim = 1.f;
        searchAnim = 1.f;
        keyboardHeight = 280.f;
        break;
    case 3: // SONGS list (starred icon hugging title + Artist — Album subtitle, NO date + Japanese rows!)
        setTab(4);
        player.stars[9] = true;
        selection = 1;
        keypad = true;
        break;
    case 4: // SONGS scrolled all the way to the bottom (verify zero bottom clipping!)
        setTab(4);
        keypad = true;
        rows();
        selection = static_cast<int>(cachedRows.size()) - 1;
        keepSelectedVisible();
        scroll = scrollTarget;
        scrollTime = 1.f;
        break;
    case 5: // ORDER SONGS sheet (ORDER BY + DIRECTION sections + active accent pills!)
        setTab(4);
        openSheet(Sheet::Order);
        keypad = true;
        break;
    case 6: // ARTIST DETAIL (2-col Albums + 1-col Songs + Detail Header with Play expanded!)
        setTab(2);
        rows();
        selectRow(0);
        toolFocus = true;
        toolSelection = 1;
        detailBtnAnim[1] = 1.f;
        break;
    case 7: // PLAYLIST DETAIL (full 116px cover rows + badge header)
        setTab(3);
        rows();
        selectRow(1);
        keypad = true;
        break;
    case 8: // ADD TO PLAYLIST sheet + inline New Playlist input
        setTab(4);
        contextTrack = 0;
        openSheet(Sheet::Playlist);
        keypad = true;
        break;
    case 9: // DECK SETTINGS (Accent Color swatches + SFX Volume + SFX Output)
        setTab(0);
        openSheet(Sheet::Settings);
        keypad = true;
        sheetSelection = AccentRow;
        settingsScroll = settingsScrollTarget = settingsRowOffset(AccentRow) - 96.f;
        settingsScrollTime = 1.f;
        break;
    case 10: // VFD Amber Phosphor Accent + VU Meters + Japanese Track!
        theme = Theme::Vfd;
        wifiConnected = false;
        themeAccents[2] = 1; // Amber VFD!
        player.current = 9;
        player.playing = true;
        player.seconds = 42.0;
        stage = 1;
        meter.update(player, .1);
        setTab(4);
        break;
    case 11: // Ceramic Cobalt Accent + Get Info Sheet
        theme = Theme::Ceramic;
        themeAccents[1] = 1; // Cobalt!
        contextTrack = 0;
        openSheet(Sheet::Info);
        break;
    case 12: // DECK SETTINGS (Time Format, 12h AM/PM in top bar)
        clock24h = false;
#if DECKDS_ENABLE_SET_CLOCK
        clockOffsetMinutes = 90;
#endif
        setTab(0);
        openSheet(Sheet::Settings);
        keypad = true;
        sheetSelection = TimeFormatRow;
        break;
    case 13: // DECK SETTINGS (Lid Close at the bottom)
        setTab(0);
        openSheet(Sheet::Settings);
        keypad = true;
        sheetSelection = LidCloseRow;
        keepSettingsVisible();
        settingsScroll = settingsScrollTarget;
        settingsScrollTime = 1.f;
        break;
    case 14:
        useBluetoothFixture();
        break;
    case 15: // Long metadata on Cover and Deck
        player.seconds = 105;
        break;
    case 16:
    case 17:
        setTab(1);
        albumsInGrid = preset == 16;
        break;
    case 18:
        openAlbum(0);
        break;
    case 19:
    case 20: {
        setTab(2);
        const auto artists = rows();
        const auto found = std::ranges::find(artists, player.tracks.front().artist, &Row::title);
        if (found != artists.end()) {
            const int index = static_cast<int>(found - artists.begin());
            if (preset == 20) {
                selectRow(index);
            } else {
                selection = index;
                keepSelectedVisible();
                scroll = scrollTarget;
            }
        }
        break;
    }
    case 21:
        setTab(4);
        break;
    case 22:
        setTab(3);
        selectRow(0); // Starred playlist
        break;
    case 23:
        setTab(5);
        break;
    case 24:
        openContext(0);
        break;
    case 25:
        contextTrack = 0;
        openSheet(Sheet::Info);
        break;
    case 26:
        theme = Theme::Vfd;
        stage = 2;
        player.seconds = 105;
        break;
    default:
        break;
    }
    player.sfx = true;
    settleTab();
}

void DeckUi::settleTab()
{
    tabTime = 1.f;
    deckProgressTransition = tab == Tab::Deck ? 2.f : 0.f;
    stageCentering = tab == Tab::Deck ? 1.f : 0.f;
    for (int i = 0; i < 6; ++i)
        tabWidths[i] = (i == static_cast<int>(tab) ? 260.f : 108.f);
    tabStart = tabWidths;
    rowsValid = false;
}

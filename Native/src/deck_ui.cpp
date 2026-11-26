#include "deck_ui.hpp"
#include "design_assets.inc"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <numeric>
#include <random>

namespace {
constexpr std::string_view tabNames[] = {"DECK", "ALBUMS", "ARTISTS", "PLAYLISTS", "SONGS", "QUEUE"};
constexpr std::string_view tabIcons[] = {"nav-play", "album", "artist", "playlists", "tracks", "nav-queue"};
constexpr std::string_view stageIcons[] = {"stage-cover", "stage-vu", "stage-spectrum"};
constexpr std::string_view themeNames[] = {"Obsidian", "Ceramic", "VFD"};
constexpr std::string_view outputIcons[] = {"headphones", "speaker", "bluetooth"};
constexpr Rect fullPanel{0, 0, 1024, 768};
constexpr Rect railBox{944, 0, 64, 0};
constexpr int keyColumns = 10;
const KeyAsset* referenceKey(bool symbols, int position)
{
    for (const auto& key : keyAssets)
        if (key.symbols == symbols && key.row == position / keyColumns &&
            position % keyColumns >= key.column && position % keyColumns < key.column + key.span)
            return &key;
    return nullptr;
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
}

bool DeckUi::textInput() const { return input != Input::None; }
bool DeckUi::displaysOff() const { return lid || screenOff || poweredOff; }
float DeckUi::backlight() const { return displaysOff() ? 0.f : .28f + .72f * brightnessLevels[brightness] / 100; }

std::string_view DeckUi::sleepMessage() const
{
    if (poweredOff) return "Powered off / press Power to start";
    if (lid) return "Lid closed / press Lid to reopen";
    if (screenOff) return "Screens off / press Power to wake";
    return {};
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
    player.update(dt);
    if (!displaysOff() && (stage != 0 || stageTime < 1))
        meter.update(player, dt);
    stageTime = std::min(1.f, stageTime + step / .20f);
    tabTime = std::min(1.f, tabTime + step / .22f);
    for (int i = 0; i < 6; ++i)
        tabWidths[i] = std::lerp(tabStart[i], i == static_cast<int>(tab) ? 265.f : 107.f, smooth(tabTime));
    utilityHeight = std::lerp(utilityHeight, utility && tab != Tab::Deck ? 76.f : 0.f, std::min(1.f, step * 23));
    keyboardHeight = std::lerp(keyboardHeight, textInput() ? 280.f : 0.f, std::min(1.f, step * 25));
    if (scrollTime < 1) {
        scrollTime = std::min(1.f, scrollTime + step / .20f);
        scroll = std::lerp(scrollFrom, scrollTarget, smooth(scrollTime));
    }
    volumeTime = std::max(0.f, volumeTime - step);
    seekPopup = std::max(0.f, seekPopup - step);
    railPopup = std::max(0.f, railPopup - step);
    noticeTime = std::max(0.f, noticeTime - step);
    releaseTime = std::max(0.f, releaseTime - step);
    if (pressed) {
        pressTime += step;
        if ((pressed->action == Action::Previous || pressed->action == Action::Next) && pressTime >= .32f) {
            held = true;
            player.seekSeconds((pressed->action == Action::Previous ? -1 : 1) * step * 16);
            seekPopup = .55f;
        }
    }
    if (seekDirection != 0) {
        seekHold += step;
        if (seekHold >= .32f) {
            player.seekSeconds(seekDirection * step * 16);
            seekPopup = .55f;
        }
    }
    if (std::abs(analogPosition) > .25f && tab == Tab::Deck && sheet == Sheet::None && !displaysOff()) {
        player.seekSeconds(analogPosition * step * 20);
        seekPopup = .55f;
    }
}

void DeckUi::upper(Draw& d, SDL_Texture* fadeLayer)
{
    d.theme = theme;
    if (displaysOff()) {
        d.clear(color(0x000000));
        return;
    }
    float title = stage != 0 ? 1.f : 0.f;
    if (stageTime < 1 && fadeLayer && stage != outgoingStage) {
        auto* target = SDL_GetRenderTarget(d.renderer);
        SDL_SetRenderTarget(d.renderer, fadeLayer);
        drawStage(d, player, meter, stage);
        SDL_SetRenderTarget(d.renderer, target);
        drawStage(d, player, meter, outgoingStage);
        const float fade = stageTime * stageTime * (3 - 2 * stageTime);
        d.texture(fadeLayer, fullPanel, fade);
        title = std::lerp(outgoingStage != 0 ? 1.f : 0.f, title, fade);
    } else {
        drawStage(d, player, meter, stage);
    }
    drawTopChrome(d, player, title);
    d.rect(fullPanel, alpha(color(0x000000), 1 - backlight()));
}

void DeckUi::lower(Draw& d)
{
    d.theme = theme;
    hits.clear();
    if (displaysOff()) {
        d.clear(color(0x000000));
        return;
    }
    d.clear(d.p().bg);
    tabs(d);
    if (tab == Tab::Deck) {
        transport(d);
        d.phosphor(fullPanel);
    } else {
        catalog(d);
        if (keyboardHeight > 1 && sheet == Sheet::None)
            keypadUi(d);
    }
    if (sheet != Sheet::None)
        overlay(d);
    if (noticeTime > 0) {
        const float opacity = std::min(1.f, noticeTime * 6);
        d.round({262, 654, 500, 56}, 12, alpha(d.p().elevated, opacity));
        d.label(notice, {278, 654, 468, 56}, Size::Sub, alpha(d.p().text, opacity), Face::Display, Align::Center);
    }
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

void DeckUi::button(Draw& d, Rect r, Action action, int value, std::string_view glyph,
                    std::string_view text, bool focus, bool hero, int iconSize,
                    std::optional<Color> background, std::optional<Color> ink, int vfdSize, int corner, bool marker)
{
    hit(r, action, value);
    const auto& p = d.p();
    const bool down = pressed && pressed->action == action && pressed->data == value && !dragging &&
                      inside(r, pressed->box.x + pressed->box.w / 2, pressed->box.y + pressed->box.h / 2);
    const bool released = lastAction == action && lastData == value;
    const float amount = down ? smooth(pressTime / .09f) : released ? releaseTime / .09f : 0;
    const float radius = d.vfd() ? 2 : corner;
    const float scale = 1 - .06f * amount;
    const float dx = even((1 - scale) * r.w / 2);
    const float dy = even((1 - scale) * r.h / 2);
    r = {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
    Color fill = background.value_or(hero ? p.raised : p.card);
    if (down)
        fill = mix(fill, p.raised, .55f);
    if (focus || amount > .01f)
        d.frame(inset(r, -4), radius + 4, p.bg, p.soft);
    if (fill.a || focus || down)
        d.frame(r, radius, fill.a ? fill : p.elevated, focus || down ? p.accent : p.strong, focus ? 4 : 2);
    const auto foreground = ink.value_or(hero ? p.accent : p.text);
    if (!glyph.empty()) {
        if (text.empty()) {
            const float x = r.x + even((r.w - iconSize) / 2);
            const float y = r.y + even((r.h - iconSize) / 2);
            d.icon(glyph, {x, y, static_cast<float>(iconSize), static_cast<float>(iconSize)}, foreground, vfdSize);
        } else if (r.w >= 132) {
            const float textW = std::min(d.textWidth(text, Size::Sub, Face::Display, true), r.w - iconSize - 28);
            const float x = r.x + even((r.w - iconSize - 8 - textW) / 2);
            d.icon(glyph, {x, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
            d.label(text, {x + iconSize + 8, r.y, textW, r.h},
                    Size::Sub, p.text, Face::Display, Align::Left, true);
        } else {
            const float x = r.x + even((r.w - iconSize) / 2);
            d.icon(glyph, {x, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
        }
    } else {
        d.label(text, inset(r, 4), Size::Sub, foreground, Face::Display, Align::Center, true);
    }
    if (hero)
        d.round({r.x + even((r.w - 56) / 2), r.y + r.h - 20, 56, 4}, 2, alpha(p.accent, .85f));
    if (marker)
        d.circle(r.x + even(r.w / 2), r.y + r.h - 16, 4, p.text);
}

void DeckUi::tabs(Draw& d)
{
    const auto& p = d.p();
    d.rect({0, 0, 1024, 76}, p.elevated);
    d.rect({0, 74, 1024, 2}, p.border);
    const int current = static_cast<int>(tab);
    button(d, {16, 12, 64, 52}, Action::Tab, current - 1, {}, "L1", false, false, 32,
           current > 0 ? p.card : p.elevated, current > 0 ? p.dim : alpha(p.dim, .35f));
    button(d, {944, 12, 64, 52}, Action::Tab, current + 1, {}, "R1", false, false, 32,
           current < 5 ? p.card : p.elevated, current < 5 ? p.dim : alpha(p.dim, .35f));
    float x = 92;
    for (int i = 0; i < 6; ++i) {
        const float left = even(x);
        const float width = even(x + tabWidths[i]) - left;
        button(d, {left, 10, width, 56}, Action::Tab, i, tabIcons[i], i == current ? tabNames[i] : "",
               false, false, 34, i == current ? p.card : color(0, 0), i == current ? p.accent : p.dim);
        x += tabWidths[i] + 8;
    }
}

void DeckUi::transport(Draw& d)
{
    const auto& p = d.p();
    const auto* track = player.track();

    d.frame({16, 92, 992, 240}, d.vfd() ? 2 : 16, p.card, p.border);
    if (track)
        d.label(track->title, {38, 108, 524, 48}, Size::Body, p.text, Face::Display, Align::Left, true, true);
    d.label(timecode(player.seconds), {634, 108, 80, 48}, Size::Body, p.accent, Face::Mono, Align::Left, true);
    d.label("/ " + timecode(track ? track->duration : 0), {730, 108, 112, 48}, Size::Body, p.muted, Face::Mono, Align::Left, true);
    button(d, {858, 108, 56, 48}, Action::Context, player.current, "more", {}, false, false, 28, p.elevated);
    button(d, {930, 108, 56, 48}, Action::Settings, 0, "settings", {}, false, false, 28, p.elevated);
    for (const auto& [r, caption] : {std::pair(Rect{890, 100, 28, 24}, "Y"), std::pair(Rect{940, 100, 52, 24}, "SEL")}) {
        d.frame(r, d.vfd() ? 2 : 6, p.bg, p.strong);
        d.label(caption, r, Size::Caption, p.dim, Face::Mono, Align::Center);
    }
    drawWaveform(d, player, deckWave);
    hit(deckWave, Action::Scrub);
    if (seekPopup > 0 && track) {
        const float x = std::clamp(deckWave.x + deckWave.w * player.progress() - 78, 38.f, 830.f);
        d.frame({even(x), 190, 156, 58}, 12, p.elevated, p.strong);
        d.label(timecode(player.seconds), {even(x), 190, 156, 58}, Size::Title, p.text, Face::Mono, Align::Center);
    }

    button(d, {16, 348, 172, 172}, Action::Previous, 0, "prev", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);
    button(d, {204, 348, 428, 172}, Action::Play, 0, track ? player.playing ? "pause" : "play" : "shuffle",
           {}, false, true, 56, std::nullopt, std::nullopt, 64, 18);
    button(d, {648, 348, 172, 172}, Action::Next, 0, "next", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);
    button(d, {836, 348, 172, 172}, Action::Star, player.current,
           track && player.stars[player.current] ? "star-filled" : "star", {}, false, false, 48,
           std::nullopt, std::nullopt, 48, 18);

    d.frame({16, 536, 992, 94}, d.vfd() ? 2 : 16, p.card, p.border);
    button(d, {36, 554, 58, 58}, Action::Mute, 0, player.mute ? "speaker-mute" : "speaker", {},
           false, false, 28, p.elevated, player.mute ? p.danger : p.text);
    button(d, {930, 554, 58, 58}, Action::Output, 0, outputIcons[output], {},
           false, false, 28, p.elevated);
    d.frame(volumeTrack, d.vfd() ? 2 : 12, p.bg, p.strong);
    const float thumb = volumeTrack.x + 22 + even((volumeTrack.w - 44) * player.volume / 100);
    if (d.vfd()) {
        for (float x = volumeTrack.x + 4; x < volumeTrack.x + volumeTrack.w - 4; x += 8)
            d.rect({x, volumeTrack.y + 6, 6, 12}, x < thumb && !player.mute ? p.accent : p.wave);
    }
    d.frame({thumb - 22, 562, 44, 44}, d.vfd() ? 2 : 12, player.mute ? p.muted : p.accent, p.card);
    hit({volumeTrack.x, 550, volumeTrack.w, 68}, Action::Volume);
    if (volumeTime > 0) {
        const float opacity = std::min(1.f, volumeTime / .16f);
        const Rect bubble{std::clamp(thumb - 40, 108.f, 834.f), 504 + even(8 * (1 - opacity)), 80, 52};
        d.round(bubble, d.vfd() ? 2 : 14, alpha(p.accent, opacity));
        d.label(std::to_string(player.mute ? 0 : player.volume), bubble, Size::Body, alpha(p.accentInk, opacity),
                Face::Mono, Align::Center, true);
    }

    button(d, {16, 646, 236, 104}, Action::Stage, 0, stageIcons[stage], {}, false, false, 34,
           std::nullopt, p.text, 48);
    const Rect stageHint{214, 654, 28, 26};
    d.frame(stageHint, d.vfd() ? 2 : 6, p.bg, p.strong);
    d.label("X", stageHint, Size::Caption, p.dim, Face::Mono, Align::Center, true);
    button(d, {268, 646, 236, 104}, Action::Shuffle, 0, "shuffle", {}, false, false, 34,
           std::nullopt, player.shuffle ? p.text : p.muted, 48, 14, player.shuffle);
    button(d, {520, 646, 236, 104}, Action::Repeat, 0, player.repeat == 2 ? "repeat-one" : "repeat", {},
           false, false, 34, std::nullopt, player.repeat ? p.text : p.muted, 48, 14, player.repeat != 0);
    button(d, {772, 646, 236, 104}, Action::Album, player.current, "goto-album", {},
           false, false, 34, std::nullopt, p.dim, 48);
}

const std::vector<DeckUi::Row>& DeckUi::rows() const
{
    if (!rowsValid || cachedPlayerRevision != player.catalogRevision) {
        cachedRows = buildRows();
        cachedPlayerRevision = player.catalogRevision;
        rowsValid = true;
    }
    return cachedRows;
}

std::vector<DeckUi::Row> DeckUi::buildRows() const
{
    std::vector<Row> result;
    const auto addTrack = [&](int index, int playlist = -1) {
        const auto& t = player.tracks[index];
        result.push_back({std::string(t.title), std::string(t.artist), {index}, index, index, Tab::Songs, playlist});
    };
    const auto addGroup = [&](int index, Tab type) {
        const auto& t = player.tracks[index];
        const std::string_view name = type == Tab::Albums ? t.album : t.artist;
        auto match = std::ranges::find(result, name, &Row::title);
        if (match == result.end())
            result.push_back({std::string(name), type == Tab::Albums ? std::string(t.artist) : "",
                              {index}, index, -1, type});
        else
            match->ids.push_back(index);
    };

    if (!details.empty()) {
        const auto& detail = details.back();
        for (const int index : detail.ids) {
            if (detail.source == Tab::Artists)
                addGroup(index, Tab::Albums);
            else
                addTrack(index, detail.playlist);
        }
    } else if (tab == Tab::Queue) {
        for (const int index : player.queue)
            addTrack(index);
    } else if (tab == Tab::Playlists) {
        Row starred{"Starred", "", {}, -1, -1, Tab::Playlists, -1};
        for (int i = 0; i < player.librarySize(); ++i)
            if (player.stars[i])
                starred.ids.push_back(static_cast<int>(i));
        if (!starred.ids.empty())
            starred.cover = starred.ids.front();
        result.push_back(std::move(starred));
        for (size_t i = 0; i < player.playlists.size(); ++i) {
            const auto& list = player.playlists[i];
            result.push_back({list.name, "", list.tracks, list.tracks.empty() ? -1 : list.tracks.front(),
                              -1, Tab::Playlists, static_cast<int>(i)});
        }
    } else if (tab != Tab::Deck) {
        for (int i = 0; i < player.librarySize(); ++i) {
            if (tab == Tab::Songs)
                addTrack(static_cast<int>(i));
            else
                addGroup(static_cast<int>(i), tab);
        }
    }

    if (details.empty() && tab != Tab::Queue && tab != Tab::Playlists) {
        std::stable_sort(result.begin(), result.end(), [&](const Row& a, const Row& b) {
            const auto& ta = player.tracks[a.cover];
            const auto& tb = player.tracks[b.cover];
            auto key = [&](const Row& row, const Track& track) -> std::string {
                switch (sortField) {
                case 1: return normalize(row.title);
                case 2: return normalize(track.artist);
                case 3: return std::to_string(track.year);
                default: return std::string(track.added);
                }
            };
            const auto aKey = key(a, ta), bKey = key(b, tb);
            return ascending ? aKey < bKey : aKey > bKey;
        });
    }

    if (details.empty() && !query.empty()) {
        const auto folded = normalize(query);
        std::erase_if(result, [&](const Row& row) {
            if (matches(row.title + " " + row.subtitle, folded))
                return false;
            return std::ranges::none_of(row.ids, [&](int i) {
                const auto& track = player.tracks[i];
                return matches(std::string(track.title) + " " + std::string(track.artist) + " " +
                               std::string(track.album), folded);
            });
        });
    }
    return result;
}

int DeckUi::gridColumns() const
{
    if (!details.empty())
        return details.back().source == Tab::Artists ? 2 : 1;
    return tab == Tab::Albums && albumsInGrid ? 3 : 1;
}

float DeckUi::rowHeight() const
{
    if (gridColumns() != 1)
        return 368;
    if (!details.empty())
        return 92;
    switch (tab) {
    case Tab::Queue: return 108;
    case Tab::Artists: return 96;
    case Tab::Playlists: return 112;
    case Tab::Songs: return 116;
    default: return 118;
    }
}

float DeckUi::rowGap() const
{
    const int columns = gridColumns();
    return columns == 3 ? 14 : columns == 2 ? 16 : 12;
}

void DeckUi::catalog(Draw& d)
{
    const auto& p = d.p();
    const auto& entries = rows();
    const bool root = details.empty();
    const bool isQueue = tab == Tab::Queue;
    const bool searchable = root && !isQueue;
    const float top = root ? 92 + (searchable ? even(utilityHeight * 104 / 76) : 0) : 196;
    const float maxBottom = 750;
    const float rowH = rowHeight();
    const int columns = gridColumns();
    const int rowCount = (static_cast<int>(entries.size()) + columns - 1) / columns;
    listBox = {12, top, 924, std::max(0.f, maxBottom - top)};
    scrollLimit = std::max(0.f, rowCount * (rowH + rowGap()) - rowGap() + 8 - listBox.h +
                           (input == Input::Search ? keyboardHeight : 0));
    scroll = std::clamp(scroll, 0.f, scrollLimit);
    scrollTarget = std::clamp(scrollTarget, 0.f, scrollLimit);
    selection = std::clamp(selection, 0, std::max(0, static_cast<int>(entries.size()) - 1));

    if (!root) {
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 66, 68}, Action::Back, 0, "back", {}, false, false, 28, p.elevated);
        const auto& detail = details.back();
        const bool album = detail.source == Tab::Albums;
        float titleX = 112;
        if (album && detail.cover >= 0) {
            d.art(&player.tracks[detail.cover], {112, 102, 68, 68}, 10);
            titleX = 196;
        }
        d.label(detail.title, {titleX, 102, 668 - titleX, 42}, Size::Body, p.text, Face::Display, Align::Left, true, true);
        d.label(detail.subtitle, {titleX, 145, 668 - titleX, 26}, Size::Sub, p.dim, Face::Display, Align::Left, false, true);
        button(d, {774, 102, 66, 68}, Action::ListShuffle, 0, "shuffle",
               {}, toolFocus && toolSelection == 0);
        button(d, {852, 102, 66, 68}, Action::ListPlay, 0, "play",
               {}, toolFocus && toolSelection == 1, true);
        button(d, {930, 102, 64, 68}, Action::DetailQueue, 0, "queue-add",
               {}, toolFocus && toolSelection == 2);
    } else if (searchable && utilityHeight > 1) {
        const bool fullSearch = !query.empty() || input == Input::Search;
        const float searchWidth = fullSearch ? 960 : 320;
        const float reveal = even(utilityHeight * 88 / 76);
        const float y = 106 - (88 - reveal);
        d.clip({0, 92, 1024, reveal});
        hitClip = Rect{0, 92, 1024, reveal};
        d.frame({16, y - 14, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        const Rect search{32, y, searchWidth, 60};
        const bool searchFocus = input == Input::Search || (toolFocus && toolSelection == 0);
        if (searchFocus)
            d.frame(inset(search, -4), 18, p.card, p.soft);
        d.frame(search, d.vfd() ? 2 : 14, p.elevated, searchFocus ? p.accent : p.strong, searchFocus ? 4 : 2);
        d.icon("search", {48, y + 18, 24, 24}, p.dim);
        const std::string hint = tab == Tab::Songs ? "Search songs…" : tab == Tab::Albums ? "Search albums…" :
                                 tab == Tab::Artists ? "Search artists…" : "Search playlists…";
        d.label(query.empty() ? hint : query, {84, y, searchWidth - 170, 60},
                Size::Sub, query.empty() ? p.muted : p.text, Face::Display, Align::Left, false, true, 600);
        const Rect badge{search.x + search.w - (fullSearch ? 120 : 68), y + 12, 60, 36};
        d.frame(badge, 8, p.card, p.border);
        d.label(std::to_string(entries.size()), badge, Size::Caption, p.dim, Face::Mono, Align::Center, true);
        hit(search, Action::Search);
        if (fullSearch) {
            button(d, {940, y + 8, 42, 42}, Action::ClearSearch, 0, {}, "✕");
        } else {
            if (tab != Tab::Playlists) {
                button(d, {tab == Tab::Albums ? 410.f : 638.f, y, 62, 60},
                       Action::ListShuffle, 0, "shuffle", {}, toolFocus && toolSelection == 1);
                button(d, {tab == Tab::Albums ? 484.f : 712.f, y, 62, 60},
                       Action::ListPlay, 0, "play", {}, toolFocus && toolSelection == 2);
            }
            if (tab == Tab::Albums) {
                const auto neutral = d.vfd() ? color(0x133540) :
                                     theme == Theme::Ceramic ? color(0xd4cdc0) : color(0x323745);
                button(d, {558, y, 74, 60}, Action::Grid, 1, "view-grid",
                       {}, toolFocus && toolSelection == 3, false, 28, albumsInGrid ? neutral : p.elevated);
                button(d, {640, y, 74, 60}, Action::Grid, -1, "view-list",
                       {}, toolFocus && toolSelection == 4, false, 28, albumsInGrid ? p.elevated : neutral);
            }
            if (tab == Tab::Playlists)
                button(d, {784, y, 208, 60}, Action::NewPlaylist, 0, "playlist-add", "New Playlist",
                       toolFocus && toolSelection == 1);
            else {
                const std::string_view names[] = {"Added Time", "Name", "Artist", "Year"};
                button(d, {784, y, 208, 60}, Action::Order, 0, "sort",
                       std::string(names[sortField]) + (ascending ? " ↑" : " ↓"), toolFocus && toolSelection == (tab == Tab::Albums ? 5 : 3));
            }
        }
        d.unclip();
        hitClip.reset();
    }

    d.clip(listBox);
    hitClip = listBox;
    const float yPos = even(scroll);
    const float cellW = columns == 3 ? 296 : columns == 2 ? 450 : 916;
    if (entries.empty())
        d.label(query.empty() ? isQueue ? "Up Next is empty" : "No tracks here yet" : "No matching tracks",
                {16, top, 914, listBox.h}, Size::Body, p.muted, Face::Display, Align::Center);
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& row = entries[i];
        const Rect cell{16 + (i % columns) * (cellW + rowGap()), top + 4 + (i / columns) * (rowH + rowGap()) - yPos, cellW, rowH};
        if (cell.y + cell.h <= top || cell.y >= maxBottom)
            continue;
        const bool focus = keypad && selection == i && !toolFocus && input == Input::None && sheet == Sheet::None;
        const bool down = pressed && pressed->action == Action::Row && pressed->data == i && !dragging;
        const Rect interactive{cell.x, std::max(cell.y, top), cell.w,
                               std::min(cell.y + cell.h, maxBottom) - std::max(cell.y, top)};
        hit(interactive, Action::Row, i);
        Rect visual = cell;
        if (down)
            visual = inset(visual, even(smooth(pressTime / .09f) * 4));
        if (focus)
            d.frame(inset(visual, -4), d.vfd() ? 2 : 18, p.bg, p.soft);
        const bool active = row.track >= 0 && row.track == player.current;
        const auto fill = down ? p.raised : active && !isQueue ? mix(p.bg, p.accent, .18f) : p.card;
        d.frame(visual, d.vfd() ? 2 : columns > 1 ? 18 : 16, fill,
                focus || down || (active && !isQueue) ? p.accent : p.border, focus ? 4 : 2);
        const auto* cover = row.cover < 0 ? nullptr : &player.tracks[row.cover];

        if (columns > 1) {
            const float artSize = columns == 3 ? 256 : 124;
            const auto count = std::to_string(row.ids.size());
            const float badgeW = even(d.textWidth(count, Size::Caption, Face::Mono, true) + 20);
            if (columns == 3) {
                d.art(cover, {cell.x + 20, cell.y + 14, artSize, artSize}, 12);
                d.label(row.title, {cell.x + 14, cell.y + 280, 268, 32},
                        Size::Body, p.text, Face::Display, Align::Left, true, true);
                d.label(row.subtitle, {cell.x + 14, cell.y + 316, 254 - badgeW, 28},
                        Size::Sub, p.dim, Face::Display, Align::Left, false, true);
                const Rect badge{cell.x + cell.w - 14 - badgeW, cell.y + 316, badgeW, 28};
                d.round(badge, d.vfd() ? 2 : 8, p.soft);
                d.label(count, badge, Size::Caption, p.accent, Face::Mono, Align::Center, true);
            } else {
                d.art(cover, {cell.x + 18, cell.y + 18, artSize, artSize}, 12);
                d.label(row.title, {cell.x + 158, cell.y + 30, cell.w - 176, 34},
                        Size::Body, p.text, Face::Display, Align::Left, true, true);
                d.label(row.subtitle, {cell.x + 158, cell.y + 70, cell.w - 176, 28}, Size::Sub, p.dim);
                const Rect badge{cell.x + 158, cell.y + 108, badgeW, 26};
                d.round(badge, 8, p.soft);
                d.label(count, badge, Size::Caption, p.accent, Face::Mono, Align::Center, true);
            }
        } else if (row.track < 0) {
            const bool artist = row.source == Tab::Artists;
            const bool playlist = row.source == Tab::Playlists;
            const float textX = cell.x + (artist ? 24 : playlist ? 110 : 124);
            if (playlist) {
                const Rect tile{cell.x + 22, cell.y + 20, 72, 72};
                d.frame(tile, d.vfd() ? 2 : 14, p.elevated, p.strong);
                d.icon(row.playlist < 0 ? "star-filled" : "tracks",
                       {tile.x + 20, tile.y + 20, 32, 32}, p.accent);
            } else if (!artist) {
                d.art(cover, {cell.x + 18, cell.y + 16, 88, 88}, 12);
            }
            d.label(row.title, {textX, cell.y + (artist ? 16.f : playlist ? 38.f : 22.f),
                    cell.x + cell.w - textX - 100, 34},
                    Size::Body, p.text, Face::Display, Align::Left, true, true);
            std::string subtitle = row.subtitle;
            if (artist) {
                std::vector<std::string_view> albums;
                for (const int index : row.ids)
                    if (std::ranges::find(albums, player.tracks[index].album) == albums.end())
                        albums.push_back(player.tracks[index].album);
                subtitle = std::to_string(albums.size()) + (albums.size() == 1 ? " Album, " : " Albums, ") +
                           std::to_string(row.ids.size()) + (row.ids.size() == 1 ? " Song" : " Songs");
            }
            d.label(subtitle, {textX, cell.y + (artist ? 54.f : 62.f), cell.w - 180, 28}, Size::Sub, p.dim);
            if (!artist) {
                const auto count = std::to_string(row.ids.size());
                if (playlist) {
                    d.label(count, {cell.x + cell.w - 108, cell.y, 86, cell.h}, Size::Sub,
                            p.dim, Face::Mono, Align::Right, true);
                } else {
                    const float badgeW = even(d.textWidth(count, Size::Sub, Face::Mono, true) + 28);
                    const Rect badge{cell.x + cell.w - 22 - badgeW, cell.y + 42, badgeW, 36};
                    d.round(badge, 10, p.soft);
                    d.label(count, badge, Size::Sub, p.accent, Face::Mono, Align::Center, true);
                }
            }
        } else {
            const auto& track = player.tracks[row.track];
            const bool detailTrack = !root;
            const float art = detailTrack ? 56 : 76;
            const float artX = cell.x + 20;
            const float artY = cell.y + even((cell.h - art) / 2);
            if (detailTrack) {
                d.frame({artX, artY, art, art}, d.vfd() ? 2 : 12,
                        active ? p.accent : p.elevated, active ? p.accent : p.strong);
                if (active)
                    d.icon("play", {artX + 14, artY + 14, 28, 28}, p.accentInk);
                else
                    d.label(std::to_string(track.number), {artX, artY, art, art},
                            Size::Body, p.dim, Face::Mono, Align::Center, true);
            } else if (!isQueue) {
                d.art(cover, {artX, artY, art, art}, 12);
                if (active) {
                    d.round({artX + 44, artY + 44, 28, 28}, d.vfd() ? 2 : 8, p.accent);
                    d.icon("play", {artX + 50, artY + 50, 16, 16}, p.accentInk, 16);
                }
            }
            const float titleX = isQueue ? cell.x + 22 : artX + art + 16;
            const float durationX = cell.x + cell.w - (isQueue ? 266 : 198);
            const std::string title = row.title + (player.stars[row.track] ? " ★" : "");
            d.label(title, {titleX, cell.y + (detailTrack ? 0.f : isQueue ? 20.f : 26.f),
                    durationX - titleX - 16, detailTrack ? cell.h : 34.f},
                    Size::Body, p.text, Face::Display, Align::Left, true, true);
            if (!detailTrack) {
                const std::string subtitle = isQueue ? row.subtitle + " — " + std::string(track.album)
                    : sortField == 0 ? std::string(track.added) + " " + row.subtitle : row.subtitle;
                d.label(subtitle, {titleX, cell.y + (isQueue ? 58.f : 64.f), durationX - titleX - 16, 28},
                        Size::Sub, p.dim, Face::Display, Align::Left, false, true);
            }
            d.label(timecode(track.duration), {durationX, cell.y, isQueue ? 90.f : 94.f, cell.h}, Size::Sub,
                    p.dim, Face::Mono, Align::Right, false, false, 600);
            if (isQueue) {
                const float y = cell.y + 22;
                button(d, {cell.x + cell.w - 164, y, 64, 64}, Action::PlayNext, row.track, "play-next",
                       {}, false, false, 28, p.elevated);
                button(d, {cell.x + cell.w - 90, y, 64, 64}, Action::RowMenu, i, "more",
                       {}, false, false, 28, p.elevated);
            } else {
                const float y = cell.y + even((cell.h - 68) / 2);
                button(d, {cell.x + cell.w - 88, y, 68, 68}, Action::RowMenu, i, "more",
                       {}, false, false, 28, p.elevated);
            }
        }
    }
    d.unclip();
    hitClip.reset();

    const Rect rail{railBox.x, top, railBox.w, listBox.h};
    d.frame(rail, d.vfd() ? 2 : 12, p.elevated, p.border);
    if (rail.h > 104) {
        const auto count = static_cast<int>(entries.size());
        const float position = scrollLimit > 0 ? scroll / scrollLimit : 0;
        d.frame(rail, d.vfd() ? 2 : 16, p.card, p.strong);
        const Rect thumb{rail.x + 8, rail.y + 12 + even((rail.h - 68) * position), 48, 44};
        d.frame(thumb, d.vfd() ? 2 : 10, mix(p.card, p.accent, scrollLimit > 0 ? .18f : .07f),
                scrollLimit > 0 ? p.accent : mix(p.strong, p.accent, .35f));
        std::vector<std::string> ticks;
        if (root && !isQueue && (sortField == 1 || sortField == 2)) {
            for (const char letter : std::string_view(ascending ? "ADGJMPSVZ" : "ZVSPMJGDA"))
                ticks.emplace_back(1, letter);
        } else if (count > 0) {
            const auto mark = [&](int index) {
                return index >= 1000 ? std::to_string(index / 1000) + "." + std::to_string((index + 50) / 100 % 10) + "k"
                                     : std::to_string(index);
            };
            ticks.emplace_back("1");
            if (count >= 10)
                for (int quarter = 1; quarter < 4; ++quarter)
                    ticks.push_back(mark(static_cast<int>(std::lround(count * quarter / 4.f))));
            if (count > 1)
                ticks.push_back(mark(count));
        }
        for (size_t i = 0; i < ticks.size(); ++i) {
            const float y = rail.y + 10 + even((rail.h - 40) * i / std::max<size_t>(1, ticks.size() - 1));
            d.label(ticks[i], {rail.x, y, rail.w, 24}, Size::Caption, p.dim, Face::Mono, Align::Center, true);
        }
        hit(rail, Action::Rail);
    }
    if (d.vfd()) {
        d.phosphor({0, 0, 1024, top});
        d.phosphor(rail);
    }
    if (railPopup > 0 && !entries.empty() && scrollLimit > 0) {
        const int row = std::clamp(static_cast<int>(std::round(scroll / (rowH + rowGap()))) * columns,
                                   0, static_cast<int>(entries.size()) - 1);
        const float y = top + even((listBox.h - 116) / 2);
        const Rect popup{688, y, 236, 116};
        d.round(popup, d.vfd() ? 2 : 22, p.accent);
        d.label(std::to_string(row + 1), {popup.x, y + 12, popup.w, 58}, Size::Display,
                p.accentInk, Face::Mono, Align::Center, true);
        d.label(entries[row].title, {popup.x + 12, y + 72, popup.w - 24, 32}, Size::Sub, p.accentInk,
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
        const bool focus = &key == selected && textInput();
        if (pressed && pressed->action == Action::Key && pressed->data == code)
            box = inset(box, even(smooth(pressTime / .09f) * 4));
        if (focus)
            d.frame(inset(box, -4), 14, p.elevated, p.soft);
        d.frame(box, d.vfd() ? 2 : 10, key.done ? p.raised : key.function ? p.bg : p.card,
                focus ? p.accent : key.function ? p.strong : p.border, focus ? 4 : 2);
        d.label(key.label, box, key.function ? Size::Sub : Size::Body,
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
            items.push_back({"Shuffle", player.shuffle ? "On" : "Off", "shuffle"});
            items.push_back({"Repeat", player.repeat == 2 ? "One" : player.repeat == 1 ? "All" : "Off",
                             player.repeat == 2 ? "repeat-one" : "repeat"});
        } else {
            items.push_back({"Play Next", "", "play-next"});
            if (tab != Tab::Queue)
                items.push_back({"Add to Queue", "", "queue-add"});
        }
        items.push_back({"Go to Album", "", "goto-album"});
        items.push_back({"Go to Artist", "", "artist"});
        items.push_back({player.stars[contextTrack] ? "Unstar" : "Star", "",
                         player.stars[contextTrack] ? "star-filled" : "star"});
        items.push_back({"Add to Playlist", "", "playlist-add"});
        items.push_back({"Get Info", "", "info"});
        if (tab == Tab::Queue) items.push_back({"Remove from Queue", "", "trash"});
        else if (contextPlaylist >= 0) items.push_back({"Remove from Playlist", "", "trash"});
        return items;
    }
    case Sheet::Output:
        return {{"3.5mm Headphone DAC", output == 0 ? "Selected" : "", "headphones"},
                {"Built-in Stereo Speakers", output == 1 ? "Selected" : "", "speaker"},
                {"Bluetooth 4.2 Audio", output == 2 ? "Selected" : "", "bluetooth"}};
    case Sheet::Order:
        return {{"Added Time", sortField == 0 ? "Selected" : "", "sort"},
                {"Title", sortField == 1 ? "Selected" : "", "tracks"},
                {"Artist", sortField == 2 ? "Selected" : "", "artist"},
                {"Year", sortField == 3 ? "Selected" : "", "album"},
                {sortField == 1 || sortField == 2 ? "A → Z" : "Oldest First", ascending ? "Selected" : "", "sort"},
                {sortField == 1 || sortField == 2 ? "Z → A" : "Newest First", ascending ? "" : "Selected", "sort"}};
    case Sheet::Playlist: {
        std::vector<Option> items;
        for (const auto& list : player.playlists)
            items.push_back({list.name, std::to_string(list.tracks.size()), "playlists"});
        items.push_back({"New Playlist", "", "playlist-add"});
        return items;
    }
    default: return {};
    }
}

void DeckUi::overlay(Draw& d)
{
    const auto& p = d.p();
    hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& h) { return h.box.y >= 76; }), hits.end());
    if (sheet == Sheet::Settings) {
        d.rect({0, 76, 1024, 692}, p.bg);
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 66, 68}, Action::Back, 0, "back", {}, false, false, 28, p.elevated);
        const auto titleW = d.textWidth("Deck Settings", Size::Title, Face::Display, true);
        d.label("Deck Settings", {112, 92, titleW + 4, 88}, Size::Title, p.text, Face::Display, Align::Left, true);
        const Rect device{130 + even(titleW), 118, 96, 36};
        d.frame(device, d.vfd() ? 2 : 8, p.elevated, p.border);
        d.label("RG DS+", device, Size::Caption, p.dim, Face::Mono, Align::Center, true);

        const auto card = [&](Rect box, int row) {
            const bool focus = sheetSelection == row;
            if (focus)
                d.frame(inset(box, -4), 20, p.bg, p.soft);
            d.frame(box, d.vfd() ? 2 : 16, p.card, focus ? p.accent : p.border, focus ? 4 : 2);
        };
        card({16, 196, 992, 158}, 0);
        d.label("SCREEN BRIGHTNESS", {40, 214, 812, 34}, Size::Sub, p.muted, Face::Mono, Align::Left, true);
        d.label(std::to_string(brightnessLevels[brightness]) + "%", {860, 214, 124, 34},
                Size::Body, p.accent, Face::Mono, Align::Right, true);
        d.frame({42, 260, 944, 24}, d.vfd() ? 2 : 12, p.bg, p.strong);
        for (int i = 0; i < 7; ++i) {
            const float x = 62 + even(i * 902 / 6.f);
            d.round({x - 2, 294, 4, 12}, 2, p.strong);
            d.label(std::to_string(brightnessLevels[i]) + "%", {x - 30, 314, 60, 26},
                    Size::Caption, p.muted, Face::Mono, Align::Center, true);
        }
        const float thumb = 64 + even(brightness * 900 / 6.f);
        d.frame({thumb - 22, 250, 44, 44}, d.vfd() ? 2 : 12, p.accent, p.card);
        hit({42, 248, 944, 52}, Action::Brightness);

        const auto setting = [&](Rect box, std::string_view label, int row) {
            card(box, row);
            d.label(label, {box.x + 22, box.y + 16, box.w - 44, 34},
                    Size::Sub, p.muted, Face::Mono, Align::Left, true);
        };
        const auto segment = [&](Rect box, int option, std::string_view label, bool selected) {
            hit(box, Action::Option, option);
            const bool down = pressed && pressed->action == Action::Option && pressed->data == option;
            if (down)
                box = inset(box, 2);
            d.frame(box, d.vfd() ? 2 : 14, selected ? p.accent : p.elevated, selected ? p.accent : p.strong);
            d.label(label, box, Size::Sub, selected ? p.accentInk : p.text, Face::Mono, Align::Center, true);
        };
        setting({16, 370, 992, 182}, "VISUAL THEME", 1);
        for (int i = 0; i < 3; ++i) {
            const float x = 38 + even(i * 319.33f);
            segment({x, 464, 310, 68}, i, themeNames[i], static_cast<int>(theme) == i);
        }
        setting({16, 568, 488, 182}, "LID CLOSE (HALL)", 2);
        segment({38, 662, 218, 68}, 3, "Pocket Audio", pocket);
        segment({266, 662, 216, 68}, 4, "Pause", !pocket);
        setting({520, 568, 488, 182}, "UI SOUND FX", 3);
        segment({542, 662, 218, 68}, 5, "Nintendo SFX", player.sfx);
        segment({770, 662, 216, 68}, 6, "Mute", !player.sfx);
        d.phosphor({0, 76, 1024, 692});
        return;
    }

    d.rect({0, 76, 1024, 692}, color(0x08090c, 214));
    if (input == Input::Playlist) {
        d.frame({16, 332, 992, 122}, d.vfd() ? 2 : 14, p.elevated, p.strong);
        d.label("New Playlist", {38, 342, 880, 42}, Size::Title, p.text, Face::Display, Align::Left, true);
        d.label(playlistName.empty() ? "Playlist name" : playlistName, {38, 394, 912, 44}, Size::Body,
                playlistName.empty() ? p.muted : p.text);
        keypadUi(d);
        return;
    }

    const auto items = options();
    const int columns = sheet == Sheet::Track || sheet == Sheet::Order ? 2 : 1;
    const int count = (static_cast<int>(items.size()) + columns - 1) / columns;
    const float height = sheet == Sheet::Info ? 610 : std::min(620.f, (sheet == Sheet::Track ? 124.f : 118.f) + count * 92.f);
    const float top = 76 + even((692 - height) / 2);
    d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);
    const Track* track = contextTrack >= 0 ? &player.tracks[contextTrack] : nullptr;
    const std::string_view title = (sheet == Sheet::Track || sheet == Sheet::Info) && track ? track->title
        : sheet == Sheet::Info ? "Get Info" : sheet == Sheet::Order ? "Order By"
        : sheet == Sheet::Playlist ? "Add to Playlist" : "Audio Output";
    const float headingX = sheet == Sheet::Info && track ? 180 : 88;
    const bool subline = (sheet == Sheet::Track || sheet == Sheet::Info || sheet == Sheet::Playlist) && track;
    if (sheet == Sheet::Info && track)
        d.art(track, {88, top + 24, 76, 76}, 12);
    d.label(title, {headingX, top + 24, 852 - headingX, subline ? 34.f : 60.f},
            Size::Body, p.text, Face::Display, Align::Left, true, true);
    if (subline) {
        const std::string sub = sheet == Sheet::Track ? std::string(track->artist) + " — " + std::string(track->album)
            : std::string(sheet == Sheet::Info ? track->artist : track->title);
        d.label(sub, {headingX, top + 62, 852 - headingX, 28}, Size::Sub,
                p.dim, Face::Mono, Align::Left, false, true);
    }
    button(d, {876, top + 28, 60, 60}, Action::Back, 0, {}, "✕", {}, false, 28, p.elevated);

    if (sheet == Sheet::Info && track) {
        const std::pair<std::string_view, std::string> specs[] = {
            {"Track", std::string(track->title)}, {"Album", std::string(track->album)},
            {"Artist", std::string(track->artist)}, {"Year", std::to_string(track->year)},
            {"Disc / Track", std::to_string(track->disc) + " / " + std::to_string(track->number)},
            {"Duration", timecode(track->duration)},
            {"Format", std::string(track->format)}, {"Sample Rate", std::string(track->rate)},
            {"Bitrate", std::string(track->bitrate)}, {"Channels", "2"}, {"Added", std::string(track->added)},
            {"Signal Path", "SDL demo source → system output"}, {"Media", "Demo library"}
        };
        d.clip({88, top + 116, 848, 464});
        for (int i = 0; i < static_cast<int>(std::size(specs)); ++i) {
            const float x = 88.f + (i % 2) * 430;
            const float y = top + 116 + (i / 2) * 92 - infoScroll;
            d.frame({x, y, 418, 80}, 12, p.elevated, p.border);
            d.label(specs[i].first, {x + 18, y + 10, 382, 24}, Size::Caption,
                    p.muted, Face::Mono, Align::Left, true);
            d.label(specs[i].second, {x + 18, y + 36, 382, 30}, Size::Sub,
                    p.text, Face::Display, Align::Left, true, true);
        }
        d.unclip();
    } else {
        const float w = columns == 2 ? 414 : 840;
        const float baseY = top + (sheet == Sheet::Track ? 110 : 104);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const float y = baseY + (i / columns) * 92;
            if (y + 78 > top + height - 16)
                break;
            Rect r{92 + (i % columns) * (w + 12), y, w, 78};
            const auto& option = items[i];
            const bool focus = sheetSelection == i;
            const bool down = pressed && pressed->action == Action::Option && pressed->data == i;
            if (down)
                r = inset(r, even(smooth(pressTime / .09f) * 4));
            const bool danger = option.icon == "trash";
            if (focus)
                d.frame(inset(r, -4), 20, p.card, p.soft);
            d.frame(r, d.vfd() ? 2 : 16, down ? p.raised : danger ? mix(p.elevated, p.danger, .14f) : p.elevated,
                    focus || down ? p.accent : danger ? p.danger : p.strong, focus ? 4 : 2);
            d.icon(option.icon, {r.x + 22, r.y + (r.h - 32) / 2, 32, 32}, danger ? p.danger : p.text);
            const float valueW = even(d.textWidth(option.value, Size::Sub, Face::Mono, true));
            d.label(option.label, {r.x + 70, r.y, r.w - 92 - valueW, r.h}, Size::Body,
                    danger ? p.danger : p.text, Face::Display, Align::Left, true, true);
            d.label(option.value, {r.x + r.w - 24 - valueW, r.y, valueW, r.h}, Size::Sub,
                    option.value == "Off" ? p.muted : p.text, Face::Mono, Align::Right, true);
            hit(r, Action::Option, i);
        }
    }
    d.phosphor({62, top, 900, height});
}

void DeckUi::targetScroll(float target, bool animate)
{
    scrollFrom = scroll;
    scrollTarget = std::clamp(target, 0.f, scrollLimit);
    scrollTime = animate ? 0 : 1;
    if (!animate)
        scroll = scrollTarget;
}

void DeckUi::keepSelectedVisible()
{
    const auto h = rowHeight();
    const float start = (selection / gridColumns()) * (h + rowGap());
    const float visible = std::max(1.f, listBox.h - keyboardHeight);
    if (start < scrollTarget)
        targetScroll(start);
    else if (start + h > scrollTarget + visible)
        targetScroll(start + h - visible);
}

void DeckUi::scrubRail(float y)
{
    if (listBox.h <= 44)
        return;
    const float fraction = (y - listBox.y - 22) / (listBox.h - 44);
    targetScroll(fraction * scrollLimit, false);
    railPopup = .75f;
}

void DeckUi::scrubPosition(Action which, float x)
{
    if (which == Action::Brightness) {
        activate(Action::Brightness, static_cast<int>(std::lround((x - 64) * 6 / 900)));
    } else if (which == Action::Scrub) {
        player.seek((x - deckWave.x) / deckWave.w);
        seekPopup = .65f;
    } else {
        const float fraction = std::clamp((x - volumeTrack.x - 22) / (volumeTrack.w - 44), 0.f, 1.f);
        player.volume = static_cast<int>(std::round(fraction * 100));
        player.mute = false;
        volumeTime = .9f;
    }
}

void DeckUi::flash(std::string message)
{
    notice = std::move(message);
    noticeTime = 1.25f;
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
    utilityHeight = 0;
    sheet = Sheet::None;
    rowsValid = false;
    keypad = tab != Tab::Deck;
    selection = 0;
    targetScroll(0, false);
}

void DeckUi::selectRow(int index)
{
    const auto entries = rows();
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return;
    const auto& row = entries[index];
    if (row.track >= 0) {
        std::vector<int> order;
        if (tab == Tab::Queue && player.current >= 0) {
            order = player.history;
            order.push_back(player.current);
        }
        const int offset = static_cast<int>(order.size());
        for (const auto& entry : entries)
            order.push_back(entry.track);
        player.playFrom(std::move(order), index + offset);
        if (tab == Tab::Queue)
            selection = 0;
    } else {
        details.push_back(row);
        utility = toolFocus = false;
        selection = 0;
        targetScroll(0, false);
    }
    rowsValid = false;
}

void DeckUi::openContext(int index, int playlist)
{
    if (index >= 0) {
        contextTrack = index;
        contextPlaylist = playlist;
        openSheet(Sheet::Track);
    }
}

void DeckUi::openAlbum(int index)
{
    if (index < 0)
        return;
    const auto& current = player.tracks[index];
    setTab(static_cast<int>(Tab::Albums));
    const auto collection = rows();
    const auto found = std::ranges::find(collection, current.album, &Row::title);
    if (found != collection.end())
        selectRow(static_cast<int>(found - collection.begin()));
}

void DeckUi::openSheet(Sheet next)
{
    sheet = next;
    sheetSelection = 0;
    infoScroll = 0;
}

void DeckUi::openInput(Input field)
{
    input = field;
    symbols = false;
    keySelection = 0;
    if (field == Input::Search) {
        utility = true;
        toolFocus = false;
    } else if (field == Input::Playlist) {
        playlistName.clear();
        openSheet(Sheet::Playlist);
    }
}

void DeckUi::closeInput(bool doConfirm)
{
    if (input == Input::Playlist) {
        if (doConfirm) {
            if (playlistName.find_first_not_of(" \t") == std::string::npos)
                return;
            Playlist value{playlistName, {}};
            if (contextTrack >= 0)
                value.tracks.push_back(contextTrack);
            player.playlists.push_back(std::move(value));
            flash("Playlist created");
        }
        sheet = Sheet::None;
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
    } else if (sheet == Sheet::Info || sheet == Sheet::Playlist) {
        openSheet(contextTrack >= 0 ? Sheet::Track : Sheet::None);
    } else if (sheet != Sheet::None) {
        sheet = Sheet::None;
    } else if (!details.empty()) {
        details.pop_back();
        selection = 0;
        toolFocus = false;
        targetScroll(0, false);
    } else if (!query.empty()) {
        query.clear();
        selection = 0;
        targetScroll(0, false);
    } else if (utility) {
        utility = toolFocus = false;
    } else if (tab != Tab::Deck) {
        setTab(0);
    } else {
        keypad = false;
    }
}

void DeckUi::activate(Action action, int data)
{
    lastAction = action;
    lastData = data;
    releaseTime = .09f;
    switch (action) {
    case Action::Tab: setTab(data); break;
    case Action::Settings: openSheet(Sheet::Settings); break;
    case Action::Context: openContext(data); break;
    case Action::Previous: player.previous(); break;
    case Action::Play: player.playToggle(); break;
    case Action::Next: player.next(); break;
    case Action::Star:
        if (data >= 0) {
            player.toggleStar(data);
            player.chime(player.stars[data] ? "star" : "unstar");
        }
        break;
    case Action::Mute: player.mute = !player.mute; volumeTime = .9f; break;
    case Action::Output: openSheet(Sheet::Output); break;
    case Action::Stage: cycleStage(); break;
    case Action::Shuffle: player.shuffle = !player.shuffle; break;
    case Action::Repeat: player.repeat = (player.repeat + 1) % 3; break;
    case Action::Album: openAlbum(data); break;
    case Action::Search: openInput(Input::Search); break;
    case Action::ClearSearch: query.clear(); selection = 0; targetScroll(0, false); break;
    case Action::Grid:
        albumsInGrid = data == 0 ? !albumsInGrid : data > 0;
        selection = 0;
        targetScroll(0, false);
        break;
    case Action::Order: openSheet(Sheet::Order); break;
    case Action::NewPlaylist: contextTrack = -1; openInput(Input::Playlist); break;
    case Action::Back: back(); break;
    case Action::Brightness:
        if (std::clamp(data, 0, 6) != brightness) {
            brightness = std::clamp(data, 0, 6);
            player.chime("tick");
        }
        break;
    case Action::Row: selection = data; selectRow(data); break;
    case Action::PlayNext: player.makeNext(data); break;
    case Action::RowMenu: {
        const auto entries = rows();
        if (data >= 0 && data < static_cast<int>(entries.size()))
            openContext(entries[data].track, entries[data].playlist);
        break;
    }
    case Action::ListPlay:
    case Action::ListShuffle:
    case Action::DetailQueue: {
        std::vector<int> ids;
        if (!details.empty()) {
            ids = details.back().ids;
        } else {
            for (const auto& row : rows())
                for (const int index : row.ids)
                    if (std::ranges::find(ids, index) == ids.end())
                        ids.push_back(index);
        }
        if (action == Action::DetailQueue) {
            player.append(ids);
            flash("Added to Up Next");
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
            player.chime("tick");
        } else if (key->action == "bksp") {
            eraseCharacter();
            player.chime("back");
        } else if (key->action == "clear") {
            (input == Input::Playlist ? playlistName : query).clear();
            selection = 0;
            targetScroll(0, false);
            player.chime("back");
        } else if (key->action == "done") {
            closeInput(true);
            player.chime("confirm");
        } else {
            text(key->action == "space" ? " " : key->text);
            player.chime("tick");
        }
        break;
    }
    case Action::Option:
        switch (sheet) {
        case Sheet::Settings:
            if (data < 3) theme = static_cast<Theme>(data);
            else if (data == 3 || data == 4) pocket = data == 3;
            else if (data == 5 || data == 6) player.sfx = data == 5;
            break;
        case Sheet::Output:
            output = data;
            sheet = Sheet::None;
            break;
        case Sheet::Order:
            if (data < 4) {
                sortField = data;
                ascending = data == 1 || data == 2;
            } else {
                ascending = data == 4;
                sheet = Sheet::None;
            }
            selection = 0;
            targetScroll(0, false);
            break;
        case Sheet::Playlist:
            if (data == static_cast<int>(player.playlists.size())) {
                openInput(Input::Playlist);
            } else if (contextTrack >= 0) {
                auto& list = player.playlists[data].tracks;
                if (std::ranges::find(list, contextTrack) == list.end())
                    list.push_back(contextTrack);
                flash("Added to Playlist");
                sheet = Sheet::None;
            }
            break;
        case Sheet::Track: {
            const auto items = options();
            if (data >= static_cast<int>(items.size()))
                break;
            const auto& label = items[data].label;
            const int index = contextTrack;
            if (label == "Play Next") player.makeNext(index);
            else if (label == "Go to Album") openAlbum(index);
            else if (label == "Go to Artist") {
                const auto name = player.tracks[index].artist;
                setTab(static_cast<int>(Tab::Artists));
                const auto artists = rows();
                const auto found = std::ranges::find(artists, name, &Row::title);
                if (found != artists.end()) selectRow(static_cast<int>(found - artists.begin()));
            } else if (label == "Get Info") {
                openSheet(Sheet::Info);
                break;
            } else if (label == "Add to Playlist") {
                openSheet(Sheet::Playlist);
                break;
            } else if (label == "Star" || label == "Unstar") {
                player.toggleStar(index);
                player.chime(player.stars[index] ? "star" : "unstar");
            }
            else if (label == "Add to Queue") player.append({index});
            else if (label == "Remove from Queue") std::erase(player.queue, index);
            else if (label == "Remove from Playlist") {
                std::erase(player.playlists[contextPlaylist].tracks, index);
                if (!details.empty())
                    std::erase(details.back().ids, index);
            } else if (label == "Shuffle") {
                player.shuffle = !player.shuffle;
                break;
            } else if (label == "Repeat") {
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
    if (!textInput())
        return;
    if (physicalKeyboard)
        keypad = false;
    auto& field = input == Input::Search ? query : playlistName;
    if (field.size() + value.size() <= 156)
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
    for (auto& detail : details)
        std::erase_if(detail.ids, [&](int i) { return i >= player.librarySize(); });
    if (!details.empty() && details.back().ids.empty())
        details.clear();
    rowsValid = false;
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
        pressTime = 0;
        pointerStartY = pointerLastY = y;
        keypad = false;
        const auto match = std::find_if(hits.rbegin(), hits.rend(), [=](const Hit& hit) {
            return inside(hit.box, x, y);
        });
        if (match != hits.rend()) {
            pressed = *match;
            pressVoice = voiceFor(match->action);
            if (match->action == Action::Scrub || match->action == Action::Volume || match->action == Action::Brightness)
                scrubPosition(match->action, x);
            else if (match->action == Action::Rail)
                scrubRail(y);
            else if (match->action != Action::Previous && match->action != Action::Next &&
                     (match->action != Action::Tab || (match->data >= 0 && match->data < 6)))
                player.chime(pressVoice, false);
        }
        return;
    }

    if (phase == 1) {
        if (pressed) {
            if (pressed->action == Action::Volume || pressed->action == Action::Scrub || pressed->action == Action::Brightness)
                scrubPosition(pressed->action, x);
            else if (pressed->action == Action::Rail)
                scrubRail(y);
            else if (pressed->action == Action::Row && (dragging || std::abs(y - pointerStartY) > 12)) {
                dragging = true;
                if (scroll <= 0 && y > pointerLastY && details.empty() && tab != Tab::Queue)
                    utility = true;
                targetScroll(scroll + pointerLastY - y, false);
            }
        }
        pointerLastY = y;
        return;
    }

    if (!pressed)
        return;
    const auto item = *pressed;
    pressed.reset();
    lastAction = item.action;
    lastData = item.data;
    releaseTime = .09f;
    if (!dragging && !held && inside(item.box, x, y) &&
        item.action != Action::Scrub && item.action != Action::Volume && item.action != Action::Rail && item.action != Action::Brightness) {
        activate(item.action, item.data);
        if (item.action == Action::Previous || item.action == Action::Next)
            player.chime("tick");
        else if (item.action != Action::Star && item.action != Action::Key && (item.action != Action::Tab || (item.data >= 0 && item.data < 6)))
            player.chime(pressVoice, true);
    }
}

std::string_view DeckUi::voiceFor(Action action) const
{
    switch (action) {
    case Action::Tab: return "deck";
    case Action::Play: return player.playing ? "pause" : "play";
    case Action::Stage: return "stage";
    case Action::Back: return "back";
    case Action::PlayNext:
    case Action::DetailQueue: return "add";
    case Action::Row: return "confirm";
    case Action::ListPlay:
    case Action::ListShuffle: return "play";
    default: return {};
    }
}

void DeckUi::wheel(float direction)
{
    if (sheet == Sheet::Info) {
        infoScroll = std::clamp(infoScroll - static_cast<int>(direction * 86), 0, 180);
        return;
    }
    if (sheet != Sheet::None || tab == Tab::Deck || textInput())
        return;
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
    pressed.reset();
    seekDirection = 0;
    analogPosition = 0;
    powerDown = false;
    dragging = false;
}

void DeckUi::libraryKey(SDL_Keycode value)
{
    const auto entries = rows();
    const bool detail = !details.empty();
    const bool rootUtility = !detail && tab != Tab::Queue;
    if (toolFocus) {
        const auto upper = detail ? 2 : tab == Tab::Albums ? 5 : tab == Tab::Playlists ? 1 : 3;
        if (value == SDLK_LEFT || value == SDLK_RIGHT) {
            toolSelection = std::clamp(toolSelection + (value == SDLK_LEFT ? -1 : 1), 0, upper);
        } else if (value == SDLK_DOWN) {
            toolFocus = false;
            if (query.empty()) utility = false;
        } else if (confirm(value)) {
            if (detail) {
                const Action actions[] = {Action::ListShuffle, Action::ListPlay, Action::DetailQueue};
                activate(actions[toolSelection], 0);
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
    const int columns = gridColumns();
    const int count = static_cast<int>(entries.size());
    if (confirm(value)) {
        selectRow(selection);
        player.chime("confirm");
        return;
    }
    if (value == SDLK_UP && selection < columns && (detail || rootUtility)) {
        toolFocus = true;
        toolSelection = detail ? 1 : tab == Tab::Playlists ? 0 : 2;
        utility = rootUtility;
        return;
    }
    const int page = std::max(1, static_cast<int>(listBox.h / (rowHeight() + rowGap())));
    int next = selection;
    if (value == SDLK_UP) next -= columns;
    if (value == SDLK_DOWN) next += columns;
    if (value == SDLK_LEFT) next -= columns == 1 ? page : 1;
    if (value == SDLK_RIGHT) next += columns == 1 ? page : 1;
    selection = std::clamp(next, 0, std::max(0, count - 1));
    keepSelectedVisible();
}

void DeckUi::sheetKey(SDL_Keycode value)
{
    if (sheet == Sheet::Info) {
        if (value == SDLK_UP || value == SDLK_DOWN)
            infoScroll = std::clamp(infoScroll + (value == SDLK_UP ? -120 : 120), 0, 180);
        if (confirm(value))
            back();
        return;
    }
    if (sheet == Sheet::Settings) {
        if (value == SDLK_UP || value == SDLK_DOWN)
            sheetSelection = std::clamp(sheetSelection + (value == SDLK_UP ? -1 : 1), 0, 3);
        if (value == SDLK_LEFT || value == SDLK_RIGHT) {
            const int delta = value == SDLK_LEFT ? -1 : 1;
            if (sheetSelection == 0) activate(Action::Brightness, brightness + delta);
            if (sheetSelection == 1) theme = static_cast<Theme>(std::clamp(static_cast<int>(theme) + delta, 0, 2));
            if (sheetSelection == 2) activate(Action::Option, delta < 0 ? 3 : 4);
            if (sheetSelection == 3) activate(Action::Option, delta < 0 ? 5 : 6);
        }
        if (confirm(value) && sheetSelection >= 2)
            activate(Action::Option, sheetSelection == 2 ? (pocket ? 4 : 3) : (player.sfx ? 6 : 5));
        return;
    }
    const int count = static_cast<int>(options().size());
    const int columns = sheet == Sheet::Track || sheet == Sheet::Order ? 2 : 1;
    if (confirm(value) && count > 0) {
        activate(Action::Option, sheetSelection);
        return;
    }
    if (value == SDLK_UP) sheetSelection -= columns;
    if (value == SDLK_DOWN) sheetSelection += columns;
    if (value == SDLK_LEFT) --sheetSelection;
    if (value == SDLK_RIGHT) ++sheetSelection;
    sheetSelection = std::clamp(sheetSelection, 0, std::max(0, count - 1));
}

void DeckUi::key(SDL_Keycode value, bool down, bool repeated, bool controller)
{
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
        power(down, repeated);
        return;
    }
    if (!down) {
        if ((value == SDLK_LEFT && seekDirection == -1) || (value == SDLK_RIGHT && seekDirection == 1)) {
            if (seekHold < .32f) {
                seekDirection < 0 ? player.previous() : player.next();
                player.chime("tick");
            }
            seekDirection = 0;
        }
        return;
    }
    if (value == SDLK_F1 && !repeated) {
        showKeys = !showKeys;
        return;
    }
    if (value == SDLK_F2 && !repeated) {
        scaleMode = static_cast<PreviewScale>((static_cast<int>(scaleMode) + 1) %
                                             static_cast<int>(PreviewScale::Count));
        return;
    }
    if (showKeys) {
        if (value == SDLK_ESCAPE) showKeys = false;
        return;
    }
    if (value == SDLK_L && !repeated && (controller || !textInput())) {
        toggleLid();
        return;
    }
    if (poweredOff)
        return;
    if (value == SDLK_VOLUMEUP || value == SDLK_VOLUMEDOWN) {
        player.adjustVolume(value == SDLK_VOLUMEUP ? 5 : -5);
        volumeTime = .9f;
        return;
    }
    if (displaysOff() && value != SDLK_SPACE && value != SDLK_LEFTBRACKET && value != SDLK_RIGHTBRACKET)
        return;
    if (displaysOff()) {
        if (!repeated) {
            if (value == SDLK_SPACE) activate(Action::Play, 0);
            else if (value == SDLK_LEFTBRACKET) player.previous();
            else player.next();
        }
        return;
    }
    if (textInput()) {
        if (value == SDLK_ESCAPE || (controller && value == SDLK_B)) closeInput(false);
        else if (value == SDLK_BACKSPACE || (controller && value == SDLK_Y)) eraseCharacter();
        else if (value == SDLK_RETURN || value == SDLK_KP_ENTER) {
            if (controller) activate(Action::Key, keySelection);
            else closeInput(true);
        } else if (value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) {
            keypad = true;
            const auto* key = referenceKey(symbols, keySelection);
            if (key) {
                const int row = keySelection / keyColumns;
                if (value == SDLK_UP || value == SDLK_DOWN) {
                    const int nextRow = std::clamp(row + (value == SDLK_UP ? -1 : 1), 0, 3);
                    keySelection = keySelection % keyColumns + nextRow * keyColumns;
                } else {
                    const int nextCol = value == SDLK_LEFT ? key->column - 1 : key->column + key->span;
                    keySelection = row * keyColumns + std::clamp(nextCol, 0, keyColumns - 1);
                }
                player.chime("tick");
            }
        }
        return;
    }
    if (value == SDLK_SPACE && !repeated) {
        const auto cue = voiceFor(Action::Play);
        activate(Action::Play, 0);
        player.chime(cue);
        return;
    }
    if (value == SDLK_LEFTBRACKET && !repeated) { player.previous(); return; }
    if (value == SDLK_RIGHTBRACKET && !repeated) { player.next(); return; }
    if (value == SDLK_H && !repeated) { details.empty() ? openAlbum(player.current) : setTab(0); return; }
    if (value == SDLK_G && !repeated) { activate(Action::Star, player.current); return; }
    if (value == SDLK_ESCAPE || value == SDLK_B) {
        if (!repeated) {
            back();
            player.chime("back");
        }
        return;
    }
    if (sheet != Sheet::None) {
        keypad = true;
        if (!repeated || !confirm(value)) sheetKey(value);
        return;
    }
    if (value == SDLK_Q || value == SDLK_E) {
        if (!repeated) {
            const int index = static_cast<int>(tab) + (value == SDLK_Q ? -1 : 1);
            setTab(index);
            if (index >= 0 && index < 6)
                player.chime("deck");
        }
        return;
    }
    if (value == SDLK_X && !repeated) { cycleStage(); player.chime("stage"); return; }
    if (value == SDLK_S || value == SDLK_TAB) {
        if (!repeated) {
            if (tab == Tab::Deck) openSheet(Sheet::Settings);
            else if (tab == Tab::Playlists) activate(Action::NewPlaylist, 0);
            else if (tab != Tab::Queue) openSheet(Sheet::Order);
        }
        return;
    }
    if (value == SDLK_Y && !repeated) {
        if (tab == Tab::Deck) openContext(player.current);
        else activate(Action::RowMenu, selection);
        return;
    }
    if ((value == SDLK_SLASH || value == SDLK_F) && tab != Tab::Deck && tab != Tab::Queue && details.empty()) {
        if (!repeated) openInput(Input::Search);
        return;
    }
    if (tab != Tab::Deck) {
        keypad = true;
        if (!repeated || !confirm(value)) libraryKey(value);
        return;
    }
    if (value == SDLK_UP || value == SDLK_DOWN) {
        player.adjustVolume(value == SDLK_UP ? 5 : -5);
        volumeTime = .9f;
        player.chime("tick");
    } else if ((value == SDLK_LEFT || value == SDLK_RIGHT) && !repeated) {
        seekDirection = value == SDLK_LEFT ? -1 : 1;
        seekHold = 0;
    } else if (confirm(value) && !repeated) {
        const auto cue = voiceFor(Action::Play);
        activate(Action::Play, 0);
        player.chime(cue);
    }
}

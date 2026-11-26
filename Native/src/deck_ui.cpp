#include "deck_ui.hpp"
#include "design_assets.inc"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numeric>
#include <random>

namespace {
constexpr std::string_view tabNamesEn[] = {"DECK", "ALBUMS", "ARTISTS", "PLAYLISTS", "SONGS", "QUEUE"};
constexpr std::string_view tabNamesJa[] = {"デッキ", "アルバム", "アーティスト", "プレイリスト", "曲", "キュー"};
constexpr std::string_view tabIcons[] = {"nav-play", "album", "artist", "playlists", "tracks", "nav-queue"};
constexpr std::string_view stageIcons[] = {"stage-cover", "stage-vu", "stage-spectrum"};
constexpr std::string_view themeNamesEn[] = {"Obsidian", "Ceramic", "VFD"};
constexpr std::string_view themeNamesJa[] = {"オブシディアン", "セラミック", "VFD"};
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
    totalMin = ((totalMin + clockOffsetMinutes) % 1440 + 1440) % 1440;
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
    if (poweredOff) return tr("Powered off / press Power to start", "電源オフ / Powerボタンで起動");
    if (lid) return tr("Lid closed / press Lid to reopen", "フタ閉鎖中 / Lidボタンで開く");
    if (screenOff) return tr("Screens off / press Power to wake", "画面オフ / Powerボタンで復帰");
    return {};
}

std::vector<DeckUi::SortSpec> DeckUi::sortSpecsFor(Tab which) const
{
    switch (which) {
    case Tab::Artists:
        return {
            {SortField::Name, tr("Name", "名前"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::AlbumCount, tr("Album Count", "アルバム数"), "#",
             tr("↑ Fewest First", "↑ 少ない順"), tr("↓ Most First", "↓ 多い順"), false, false},
            {SortField::SongCount, tr("Song Count", "曲数"), "#",
             tr("↑ Fewest First", "↑ 少ない順"), tr("↓ Most First", "↓ 多い順"), false, false},
        };
    case Tab::Songs:
        return {
            {SortField::Added, tr("Added Time", "追加日時"), "#",
             tr("↑ Oldest First", "↑ 古い順"), tr("↓ Newest First", "↓ 新しい順"), false, false},
            {SortField::Name, tr("Name", "名前"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Year, tr("Year", "年"), tr("Year", "年"),
             tr("↑ Oldest First", "↑ 古い順"), tr("↓ Newest First", "↓ 新しい順"), false, false},
            {SortField::Artist, tr("Artist", "アーティスト"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Album, tr("Album", "アルバム"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
        };
    default:
        return {
            {SortField::Added, tr("Added Time", "追加日時"), "#",
             tr("↑ Oldest First", "↑ 古い順"), tr("↓ Newest First", "↓ 新しい順"), false, false},
            {SortField::Name, tr("Name", "名前"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
            {SortField::Year, tr("Year", "年"), tr("Year", "年"),
             tr("↑ Oldest First", "↑ 古い順"), tr("↓ Newest First", "↓ 新しい順"), false, false},
            {SortField::Artist, tr("Artist", "アーティスト"), "A–Z", "↑ A → Z", "↓ Z → A", true, true},
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
    player.update(dt);
    if (!displaysOff() && (stage != 0 || stageTime < 1))
        meter.update(player, dt);
    stageTime = std::min(1.f, stageTime + step / .20f);
    tabTime = std::min(1.f, tabTime + step / .22f);
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
    seekPopup = std::max(0.f, seekPopup - step);
    railPopup = std::max(0.f, railPopup - step);
    noticeTime = std::max(0.f, noticeTime - step);
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
    d.accentIndex = currentAccent();
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
    drawTopChrome(d, player, title, stage == 0, clockString());
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
        d.frame({252, 654, 520, 56}, d.vfd() ? 2 : 14, alpha(d.p().elevated, opacity), alpha(d.p().accent, opacity));
        d.label(notice, {268, 654, 488, 56}, Size::Sub, alpha(d.p().text, opacity), Face::Display, Align::Center, true);
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
                    std::optional<Color> background, std::optional<Color> ink, int vfdSize, int corner, bool marker)
{
    hit(r, action, value);
    const auto& p = d.p();
    const bool down = (pressed && pressed->action == action && pressed->data == value && !dragging &&
                       inside(r, pressed->box.x + pressed->box.w / 2, pressed->box.y + pressed->box.h / 2)) ||
                      isUiHeld(action, value);
    const float amount = activePressAmount(action, value, r);
    const float radius = d.vfd() ? 2 : corner;
    const float scale = 1 - .06f * amount;
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
    if (!glyph.empty()) {
        if (text.empty()) {
            const float x = r.x + even((r.w - iconSize) / 2);
            const float y = r.y + even((r.h - iconSize) / 2);
            d.icon(glyph, {x, y, static_cast<float>(iconSize), static_cast<float>(iconSize)}, foreground, vfdSize);
        } else if (r.w >= 120) {
            const float textW = std::min(d.textWidth(text, Size::Sub, Face::Display, true), r.w - iconSize - 26);
            const float x = r.x + even((r.w - iconSize - 8 - textW) / 2);
            d.icon(glyph, {x, r.y + even((r.h - iconSize) / 2), static_cast<float>(iconSize),
                           static_cast<float>(iconSize)}, foreground, vfdSize);
            d.label(text, {x + iconSize + 8, r.y, textW, r.h},
                    Size::Sub, foreground, Face::Display, Align::Left, true);
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
            const std::string_view tabLabel = jp() ? tabNamesJa[i] : tabNamesEn[i];
            d.frame(vr, d.vfd() ? 2 : 14, isSlipFocus ? p.raised : p.card,
                    (isSlipFocus || d.vfd()) ? p.accent : p.strong);
            const float textW = std::min(d.textWidth(tabLabel, Size::Sub, Face::Display, true), vr.w - 60);
            const float showText = vr.w >= 170;
            const float ix = vr.x + even((vr.w - (showText ? 34 + 8 + textW : 34.f)) / 2);
            d.icon(tabIcons[i], {ix, vr.y + even((vr.h - 34) / 2), 34, 34}, p.accent, 32);
            if (showText) {
                const float fade = std::clamp((vr.w - 170.f) / 70.f, 0.f, 1.f);
                d.label(tabLabel, {ix + 42, vr.y, textW + 4, vr.h}, Size::Sub, alpha(p.text, fade),
                        Face::Display, Align::Left, true);
            }
        } else {
            if (amount > .01f)
                d.frame(vr, d.vfd() ? 2 : 14, p.elevated, p.accent);
            d.icon(tabIcons[i], {vr.x + even((vr.w - 34) / 2), vr.y + even((vr.h - 34) / 2), 34, 34}, p.dim, 32);
        }
        x += tabWidths[i] + 8;
    }
}

void DeckUi::transport(Draw& d)
{
    const auto& p = d.p();
    const auto* track = player.track();

    d.frame({16, 92, 992, 240}, d.vfd() ? 2 : 16, p.card, p.border);
    if (track)
        d.label(track->title, {38, 108, 532, 48}, Size::Body, p.text, Face::Display, Align::Left, true, true);
    d.label(timecode(player.seconds), {580, 108, 78, 48}, Size::Body, p.accent, Face::Mono, Align::Right, true);
    d.label("/ " + timecode(track ? track->duration : 0), {670, 108, 108, 48}, Size::Body, p.muted, Face::Mono, Align::Left, true);
    const auto headerPill = [&](Rect r, Action act, int data, std::string_view glyph, std::string_view keycap, float capW) {
        hit(r, act, data);
        const bool down = (pressed && pressed->action == act && pressed->data == data && !dragging) ||
                          isUiHeld(act, data);
        const float amount = activePressAmount(act, data, r);
        const float dx = even(.06f * amount * r.w / 2);
        const float dy = even(.06f * amount * r.h / 2);
        r = {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
        if (amount > .01f)
            d.strokeRound(inset(r, -4), d.vfd() ? 6 : 16, p.soft, 4);
        d.frame(r, d.vfd() ? 2 : 12, down ? p.raised : p.elevated, down ? p.accent : p.strong);
        d.icon(glyph, {r.x + 12, r.y + even((r.h - 24) / 2), 24, 24}, p.text, 32);
        const Rect cap{r.x + r.w - capW - 10, r.y + even((r.h - 24) / 2), capW, 24};
        d.frame(cap, d.vfd() ? 2 : 6, p.bg, p.border);
        d.label(keycap, cap, Size::Caption, p.dim, Face::Mono, Align::Center, true);
    };
    headerPill({790, 108, 84, 48}, Action::Context, player.current, "more", "Y", 26);
    headerPill({886, 108, 100, 48}, Action::Settings, 0, "settings", "SEL", 44);
    drawWaveform(d, player, deckWave);
    hit(deckWave, Action::Scrub);
    if (seekPopup > 0 && track) {
        const float x = std::clamp(deckWave.x + deckWave.w * player.progress() - 78, 38.f, 830.f);
        d.frame({even(x), 190, 156, 58}, d.vfd() ? 2 : 12, p.elevated, p.strong);
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
    const bool isMuted = player.mute || player.volume == 0;
    button(d, {36, 554, 58, 58}, Action::Mute, 0, isMuted ? "speaker-mute" : "speaker", {},
           false, false, 28, isMuted ? mix(p.elevated, p.danger, .16f) : p.elevated,
           isMuted ? p.danger : p.text);
    button(d, {930, 554, 58, 58}, Action::Output, 0, outputIcons[output], {},
           false, false, 28, p.elevated);
    d.frame(volumeTrack, d.vfd() ? 2 : 12, p.bg, isMuted ? alpha(p.strong, .5f) : p.strong);
    const int effVol = isMuted ? 0 : player.volume;
    const float thumb = volumeTrack.x + 22 + even((volumeTrack.w - 44) * effVol / 100.f);
    if (d.vfd()) {
        for (float x = volumeTrack.x + 4; x < volumeTrack.x + volumeTrack.w - 4; x += 8)
            d.rect({x, volumeTrack.y + 6, 6, 12}, x < thumb && !isMuted ? p.accent : p.wave);
    }
    d.frame({thumb - 22, 562, 44, 44}, d.vfd() ? 2 : 12, isMuted ? p.muted : p.accent, p.card);
    hit({volumeTrack.x, 550, volumeTrack.w, 68}, Action::Volume);
    if (volumeTime > 0) {
        const float opacity = std::min(1.f, volumeTime / .16f);
        const Rect bubble{std::clamp(thumb - 40, 108.f, 834.f), 504 + even(8 * (1 - opacity)), 80, 52};
        if (isMuted) {
            d.frame(bubble, d.vfd() ? 2 : 14, alpha(p.card, opacity), alpha(p.danger, opacity));
            d.label("0", bubble, Size::Body, alpha(p.danger, opacity), Face::Mono, Align::Center, true);
        } else {
            d.round(bubble, d.vfd() ? 2 : 14, alpha(p.accent, opacity));
            d.label(std::to_string(effVol), bubble, Size::Body, alpha(p.accentInk, opacity),
                    Face::Mono, Align::Center, true);
        }
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
        const auto& detail = details.back();
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
        for (const int index : player.queue)
            addTrack(index);
    } else if (tab == Tab::Playlists) {
        Row starred;
        starred.title = std::string(tr("Starred", "お気に入り"));
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
        const auto& detail = details.back();
        if (detail.source == Tab::Artists) {
            float curY = 0;
            if (cachedArtistAlbumCount > 0) {
                cachedHeaders.push_back({curY, std::string(tr("ALBUMS (", "アルバム (")) +
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
                cachedHeaders.push_back({curY, std::string(tr("SONGS (", "曲 (")) +
                                         std::to_string(detail.ids.size()) + ")", false});
                curY += 34;
                for (int i = cachedArtistAlbumCount; i < static_cast<int>(cachedRows.size()); ++i) {
                    cachedRows[i].box = {16.f, curY, 916.f, 116.f};
                    curY += 116.f + 12.f;
                }
                curY -= 12.f;
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
                curY += 92.f + 12.f;
            }
            cachedContentHeight = std::max(0.f, curY - 12.f);
            return;
        }
        // Playlist Detail: full 116px track rows with album art & Artist — Album subtitle
        float curY = 0;
        for (auto& r : cachedRows) {
            r.box = {16.f, curY, 916.f, 116.f};
            curY += 116.f + 12.f;
        }
        cachedContentHeight = std::max(0.f, curY - 12.f);
        return;
    }

    const int cols = gridColumns();
    const float rowH = cols == 3 ? 368.f : tab == Tab::Queue ? 108.f : tab == Tab::Artists ? 96.f
                     : tab == Tab::Playlists ? 112.f : tab == Tab::Songs ? 116.f : 118.f;
    const float gap = cols == 3 ? 14.f : 12.f;
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
        return details.back().source == Tab::Artists ? 2 : 1;
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
    const Rect thumb{rail.x + 8, rail.y + 12 + even((rail.h - 68) * ratio), 48, 44};
    d.frame(thumb, d.vfd() ? 2 : 10, mix(p.card, p.accent, limit > 0 ? .20f : .07f),
            limit > 0 ? p.accent : mix(p.strong, p.accent, .35f));
    for (size_t i = 0; i < ticks.size(); ++i) {
        const float y = rail.y + 10 + even((rail.h - 40) * i / std::max<size_t>(1, ticks.size() - 1));
        d.label(ticks[i], {rail.x, y, rail.w, 24}, Size::Caption, p.dim, Face::Mono, Align::Center, true);
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
    const float top = root ? 92.f + (searchable ? even(utilityAnim * 104.f) : 0.f) : 196.f;
    const float maxBottom = 752.f;
    listBox = {12.f, top, 924.f, std::max(0.f, maxBottom - top)};
    const float oskPad = (input == Input::Search && sheet == Sheet::None && keyboardHeight > 1.f)
                       ? (keyboardHeight + 16.f) : 0.f;
    scrollLimit = std::max(0.f, 4.f + cachedContentHeight + 16.f + oskPad - listBox.h);
    scroll = std::clamp(scroll, 0.f, scrollLimit);
    scrollTarget = std::clamp(scrollTarget, 0.f, scrollLimit);
    selection = std::clamp(selection, 0, std::max(0, static_cast<int>(entries.size()) - 1));

    if (!root) {
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 68, 68}, Action::Back, 0, "back", {}, false, false, 28, p.elevated);
        const auto& detail = details.back();

        // Right-aligned animated Shuffle / Play / Queue action buttons
        const std::string_view labels[3] = {tr("Shuffle", "シャッフル"), tr("Play", "再生"), tr("Queue", "キュー追加")};
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
                            detailBtnAnim[k], toolFocus && toolSelection == k, k == 1, 28);

        float titleX = 114.f;
        std::string headerTitle = detail.playlist < 0 && detail.source == Tab::Playlists
                                ? std::string(tr("Starred", "お気に入り")) : detail.title;
        std::string headerSub = detail.subtitle;
        if (detail.source == Tab::Albums && detail.cover >= 0) {
            d.frame({110, 97, 78, 78}, d.vfd() ? 2 : 12, p.card, p.strong);
            d.art(&player.tracks[detail.cover], {112, 99, 74, 74}, 10);
            titleX = 202.f;
            const auto& ct = player.tracks[detail.cover];
            headerSub = std::string(ct.artist) + " (" + std::to_string(ct.year) + ") — " +
                        std::to_string(detail.ids.size()) +
                        (jp() ? " 曲" : (detail.ids.size() == 1 ? " track" : " tracks"));
        } else if (detail.source == Tab::Playlists) {
            const Rect tile{110, 99, 74, 74};
            d.frame(tile, d.vfd() ? 2 : 12, p.elevated, p.strong);
            d.icon(detail.playlist < 0 ? "star-filled" : "playlists",
                   {tile.x + 20, tile.y + 20, 34, 34}, p.accent, 32);
            titleX = 200.f;
            int totalSec = 0;
            for (const int id : detail.ids)
                totalSec += player.tracks[id].duration;
            headerSub = std::to_string(detail.ids.size()) +
                        (jp() ? " 曲 — " : (detail.ids.size() == 1 ? " song — " : " songs — ")) + timecode(totalSec);
        } else if (detail.source == Tab::Artists) {
            headerSub = jp()
                ? (std::to_string(cachedArtistAlbumCount) + " アルバム / " + std::to_string(detail.ids.size()) + " 曲")
                : (std::to_string(cachedArtistAlbumCount) +
                   (cachedArtistAlbumCount == 1 ? " Album, " : " Albums, ") +
                   std::to_string(detail.ids.size()) + (detail.ids.size() == 1 ? " Song" : " Songs"));
        }
        const float maxTextW = std::max(0.f, btnBoxes[0].x - titleX - 16.f);
        d.label(headerTitle, {titleX, 102, maxTextW, 38}, Size::Body, p.text, Face::Display, Align::Left, true, true);
        d.label(headerSub, {titleX, 144, maxTextW, 26}, Size::Caption, p.dim, Face::Mono, Align::Left, false, true, 600);
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
        const std::string_view hint = tab == Tab::Songs ? tr("Search songs…", "曲を検索...")
                                    : tab == Tab::Albums ? tr("Search albums…", "アルバムを検索...")
                                    : tab == Tab::Artists ? tr("Search artists…", "アーティストを検索...")
                                    : tr("Search playlists…", "プレイリストを検索...");
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
                button(d, newPl, Action::NewPlaylist, 0, "playlist-add", tr("New Playlist", "新規作成"),
                       toolFocus && toolSelection == 1, false, 24, p.elevated, p.text);
            } else {
                const auto spec = activeSortSpec();
                const std::string sortText = std::string(spec.label) + (ascending ? " ↑" : " ↓");
                const float sortW = std::clamp(even(40.f + 22.f + 10.f + d.textWidth(sortText, Size::Sub, Face::Display, true)),
                                               160.f, 260.f);
                rightX -= sortW;
                button(d, {rightX, y, sortW, 60}, Action::Order, 0, "sort", sortText,
                       toolFocus && toolSelection == (tab == Tab::Albums ? 5 : 3), false, 22, p.elevated, p.text);
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

                const std::string_view playLbl = tr("Play", "再生");
                const std::string_view shufLbl = tr("Shuffle", "シャッフル");
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
    const float yPos = even(scroll);

    for (const auto& hdr : cachedHeaders) {
        const float hy = top + 4.f + hdr.y - yPos;
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
        const std::string_view emptyMsg = !query.empty() ? tr("No matching tracks", "一致する曲がありません")
            : isQueue ? tr("Up Next is empty", "再生待ちの曲はありません")
            : (!root && details.back().source == Tab::Playlists && details.back().playlist < 0)
                ? tr("No starred songs yet. Star any track to see it here.", "お気に入りの曲はまだありません")
            : tr("Playlist is empty. Press Y on any track to add.", "プレイリストは空です (Yボタンで曲を追加)");
        d.label(emptyMsg, {16, top, 914, listBox.h - oskPad}, Size::Body, p.muted, Face::Display, Align::Center);
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& row = entries[i];
        const Rect cell{row.box.x, top + 4.f + row.box.y - yPos, row.box.w, row.box.h};
        if (cell.y + cell.h <= top || cell.y >= maxBottom)
            continue;
        const bool focus = keypad && selection == i && !toolFocus && input == Input::None && sheet == Sheet::None;
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
        const bool active = row.track >= 0 && row.track == player.current;
        const auto fill = down ? mix(p.card, p.accent, .18f) : active && !isQueue ? mix(p.bg, p.accent, .18f) : p.card;
        const bool gridCard = cell.w < 600.f;
        d.frame(visual, d.vfd() ? 2 : gridCard ? 18 : 16, fill,
                focus || down || (active && !isQueue) ? p.accent : p.border, focus ? 4 : 2);
        const auto* cover = row.cover < 0 ? nullptr : &player.tracks[row.cover];

        if (gridCard) {
            const bool threeCol = cell.w < 350.f;
            const float artSize = threeCol ? 256.f : 124.f;
            const auto count = std::to_string(row.ids.size());
            const float badgeW = even(d.textWidth(count, Size::Caption, Face::Mono, true) + 20);
            if (threeCol) {
                d.art(cover, {cell.x + 20, cell.y + 14, artSize, artSize}, 12);
                d.label(row.title, {cell.x + 14, cell.y + 280, 268, 34},
                        Size::Body, p.text, Face::Display, Align::Left, true, true);
                d.label(row.subtitle, {cell.x + 14, cell.y + 318, 254 - badgeW, 28},
                        Size::Sub, p.dim, Face::Display, Align::Left, false, true);
                const Rect badge{cell.x + cell.w - 14 - badgeW, cell.y + 318, badgeW, 28};
                d.round(badge, d.vfd() ? 2 : 8, p.soft);
                d.label(count, badge, Size::Caption, p.accent, Face::Mono, Align::Center, true);
            } else {
                d.art(cover, {cell.x + 18, cell.y + 18, artSize, artSize}, 12);
                d.label(row.title, {cell.x + 158, cell.y + 24, cell.w - 176, 34},
                        Size::Body, p.text, Face::Display, Align::Left, true, true);
                d.label(row.subtitle, {cell.x + 158, cell.y + 64, cell.w - 176, 28}, Size::Sub, p.dim,
                        Face::Display, Align::Left, false, true);
                const Rect badge{cell.x + 158, cell.y + 104, badgeW, 28};
                d.round(badge, d.vfd() ? 2 : 8, p.soft);
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
                       {tile.x + 20, tile.y + 20, 32, 32}, p.accent, 32);
            } else if (!artist) {
                d.art(cover, {cell.x + 18, cell.y + 16, 88, 88}, 12);
            }
            d.label(row.title, {textX, cell.y + (artist ? 16.f : playlist ? 38.f : 22.f),
                    cell.x + cell.w - textX - 108, 36},
                    Size::Body, p.text, Face::Display, Align::Left, true, true);
            if (!playlist) {
                std::string subtitle = row.subtitle;
                if (artist) {
                    subtitle = jp()
                        ? (std::to_string(row.albumCount) + " アルバム / " + std::to_string(row.ids.size()) + " 曲")
                        : (std::to_string(row.albumCount) + (row.albumCount == 1 ? " Album, " : " Albums, ") +
                           std::to_string(row.ids.size()) + (row.ids.size() == 1 ? " Song" : " Songs"));
                }
                d.label(subtitle, {textX, cell.y + (artist ? 54.f : 64.f), cell.w - 190, 28},
                        Size::Sub, p.dim, Face::Display, Align::Left, false, true);
            }
            if (!artist) {
                const auto count = std::to_string(row.ids.size());
                if (playlist) {
                    d.label(count, {cell.x + cell.w - 108, cell.y, 86, cell.h}, Size::Sub,
                            p.dim, Face::Mono, Align::Right, true);
                } else {
                    const float badgeW = even(d.textWidth(count, Size::Sub, Face::Mono, true) + 28);
                    const Rect badge{cell.x + cell.w - 22 - badgeW, cell.y + 42, badgeW, 36};
                    d.round(badge, d.vfd() ? 2 : 10, p.soft);
                    d.label(count, badge, Size::Sub, p.accent, Face::Mono, Align::Center, true);
                }
            }
        } else {
            const auto& track = player.tracks[row.track];
            const bool albumMode = row.albumTrack;
            const float art = albumMode ? 56.f : 76.f;
            const float artX = cell.x + 20.f;
            const float artY = cell.y + even((cell.h - art) / 2);
            if (albumMode) {
                d.frame({artX, artY, art, art}, d.vfd() ? 2 : 12,
                        active ? p.accent : p.elevated, active ? p.accent : p.strong);
                if (active)
                    d.icon("play", {artX + 14, artY + 14, 28, 28}, p.accentInk, 32);
                else
                    d.label(twoDigits(track.number), {artX, artY, art, art},
                            Size::Body, p.dim, Face::Mono, Align::Center, true);
            } else if (!isQueue) {
                d.art(cover, {artX, artY, art, art}, 12);
                if (active) {
                    d.round({artX + 44, artY + 44, 28, 28}, d.vfd() ? 2 : 8, p.accent);
                    d.icon("play", {artX + 50, artY + 50, 16, 16}, p.accentInk, 16);
                }
            }
            const float titleX = isQueue ? cell.x + 22.f : artX + art + 16.f;
            const float durationX = cell.x + cell.w - (isQueue ? 266.f : 198.f);
            const bool starred = player.stars[row.track];
            const float starReserve = starred ? 28.f : 0.f;
            const float availTitleW = std::max(0.f, durationX - titleX - 16.f - starReserve);
            const float titleY = cell.y + (albumMode ? even((cell.h - 36.f) / 2) : isQueue ? 20.f : 24.f);
            d.label(row.title, {titleX, titleY, availTitleW, 36.f},
                    Size::Body, p.text, Face::Display, Align::Left, true, true);
            if (starred) {
                const float naturalW = d.textWidth(row.title, Size::Body, Face::Display, true);
                const float starDim = d.vfd() ? 16.f : 18.f;
                const float starX = titleX + even(std::min(naturalW, availTitleW)) + 8.f;
                const float starY = titleY + even((36.f - starDim) / 2);
                d.icon("star-filled", {starX, starY, starDim, starDim}, p.text, 16);
            }
            if (!albumMode) {
                d.label(row.subtitle, {titleX, cell.y + (isQueue ? 58.f : 64.f), durationX - titleX - 16.f, 28.f},
                        Size::Sub, p.dim, Face::Display, Align::Left, false, true);
            }
            d.label(timecode(track.duration), {durationX, cell.y, isQueue ? 90.f : 94.f, cell.h}, Size::Sub,
                    p.dim, Face::Mono, Align::Right, false, false, 600);
            if (isQueue) {
                const float y = cell.y + 22.f;
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
    std::vector<std::string> ticks;
    const int count = static_cast<int>(entries.size());
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
    if (ticks.empty() && count > 0) {
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
    drawScrollbar(d, rail, scroll, scrollLimit, Action::Rail, ticks);

    if (d.vfd()) {
        d.phosphor({0, 0, 1024, top});
        d.phosphor(rail);
        const bool hasCovers = root ? (tab == Tab::Albums || tab == Tab::Songs)
                                    : (details.back().source != Tab::Albums);
        if (!hasCovers)
            d.phosphor(listBox);
    }

    if (railPopup > 0 && !entries.empty() && scrollLimit > 0) {
        int nearIdx = 0;
        for (int i = 0; i < count; ++i) {
            if (entries[i].box.y + entries[i].box.h * .5f >= scroll) {
                nearIdx = i;
                break;
            }
            nearIdx = i;
        }
        std::string hero = std::to_string(nearIdx + 1);
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
        const std::string_view caption = key.action == "bksp" ? tr("DEL", "削除")
                                       : key.action == "clear" ? tr("Clear", "消去")
                                       : key.action == "space" ? tr("Space", "空白")
                                       : key.action == "done" ? tr("Done", "決定")
                                       : key.label;
        d.label(caption, box, key.function ? Size::Sub : Size::Body,
                key.done ? p.accent : key.function ? p.dim : p.text, Face::Mono, Align::Center, true);
    }
    d.phosphor({16, y, 992, 280});
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
            items.push_back({std::string(tr("Shuffle", "シャッフル")),
                             std::string(player.shuffle ? tr("On", "オン") : tr("Off", "オフ")),
                             "shuffle", false, player.shuffle, OptShuffle});
            items.push_back({std::string(tr("Repeat", "リピート")),
                             std::string(player.repeat == 2 ? tr("One", "1曲")
                                       : player.repeat == 1 ? tr("All", "全曲") : tr("Off", "オフ")),
                             player.repeat == 2 ? "repeat-one" : "repeat", false, player.repeat != 0, OptRepeat});
        } else {
            items.push_back({std::string(tr("Play Next", "次に再生")), "", "play-next", false, false, OptPlayNext});
            if (tab != Tab::Queue)
                items.push_back({std::string(tr("Add to Queue", "キューに追加")), "", "queue-add", false, false, OptAddQueue});
        }
        items.push_back({std::string(tr("Go to Album", "アルバムへ移動")), "", "goto-album", false, false, OptGoAlbum});
        items.push_back({std::string(tr("Go to Artist", "アーティストへ移動")), "", "artist", false, false, OptGoArtist});
        items.push_back({std::string(player.stars[contextTrack]
                                     ? tr("Unstar", "お気に入り解除") : tr("Star", "お気に入り登録")),
                         "", player.stars[contextTrack] ? "star-filled" : "star", false, false, OptStar});
        items.push_back({std::string(tr("Add to Playlist", "プレイリストに追加")), "", "playlist-add", false, false, OptAddPlaylist});
        items.push_back({std::string(tr("Get Info", "詳細情報")), "", "info", false, false, OptGetInfo});
        if (tab == Tab::Queue)
            items.push_back({std::string(tr("Remove from Queue", "キューから削除")), "", "trash", false, false, OptRemoveQueue});
        else if (contextPlaylist >= 0)
            items.push_back({std::string(tr("Remove from Playlist", "プレイリストから削除")), "", "trash", false, false, OptRemovePlaylist});
        return items;
    }
    case Sheet::Output: {
        const std::string act(tr("Active", "使用中"));
        return {{std::string(tr("3.5mm Headphone DAC", "3.5mm ヘッドホン DAC")), output == 0 ? act : "", "headphones", false, output == 0},
                {std::string(tr("Built-in Stereo Speakers", "内蔵ステレオスピーカー")), output == 1 ? act : "", "speaker", false, output == 1},
                {std::string(tr("Bluetooth 4.2 Audio", "Bluetooth 4.2 オーディオ")), output == 2 ? act : "", "bluetooth", false, output == 2}};
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
        const std::string addedLbl(tr("Added", "追加済"));
        const std::string addLbl(tr("+ Add", "+ 追加"));
        items.push_back({std::string(tr("Starred", "お気に入り")), inStar ? addedLbl : addLbl, "star-filled", false, inStar});
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

void DeckUi::overlay(Draw& d)
{
    const auto& p = d.p();
    hits.erase(std::remove_if(hits.begin(), hits.end(), [](const Hit& h) { return h.box.y >= 76; }), hits.end());

    if (sheet == Sheet::Settings) {
        d.rect({0, 76, 1024, 692}, p.bg);
        d.frame({16, 92, 992, 88}, d.vfd() ? 2 : 16, p.card, p.strong);
        button(d, {30, 102, 68, 68}, Action::Back, 0, "back", {}, false, false, 28, p.elevated);
        const std::string_view settingsHeading = tr("Deck Settings", "本体設定");
        const auto titleW = d.textWidth(settingsHeading, Size::Title, Face::Display, true);
        d.label(settingsHeading, {114, 92, titleW + 8, 88}, Size::Title, p.text, Face::Display, Align::Left, true);
        const Rect device{132 + even(titleW), 118, 96, 36};
        d.frame(device, d.vfd() ? 2 : 8, p.elevated, p.border);
        d.label("RG DS+", device, Size::Caption, p.dim, Face::Mono, Align::Center, true);

        settingsBox = {12, 192, 924, 560};
        constexpr float totalH = 152.f + 9 * 136.f + 9 * 14.f;
        settingsLimit = std::max(0.f, 4.f + totalH + 16.f - settingsBox.h);
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
            d.label(heading, {box.x + 22, box.y + 14, box.w - 200, 30},
                    Size::Sub, p.muted, Face::Mono, Align::Left, true);
            if (!rightInfo.empty())
                d.label(rightInfo, {box.x + box.w - 200, box.y + 14, 178, 30},
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
                const float tw = d.textWidth(label, Size::Sub, Face::Mono, true);
                const float dotX = box.x + even((box.w - (20 + 10 + tw)) / 2) + 10;
                const float dotY = box.y + even(box.h / 2);
                d.circle(dotX, dotY, 10, selected ? p.accentInk : p.strong);
                d.circle(dotX, dotY, 8, *swatch);
                d.label(label, {dotX + 18, box.y, tw + 8, box.h}, Size::Sub, ink, Face::Mono, Align::Left, true);
            } else {
                d.label(label, box, Size::Sub, ink, Face::Mono, Align::Center, true);
            }
        };

        // Row 0: Screen Brightness (152px)
        const Rect bBox{16, sy, 916, 152};
        const std::string brightPct = std::to_string(brightnessLevels[brightness]) + "%";
        card(bBox, tr("SCREEN BRIGHTNESS", "画面の明るさ"), 0, brightPct);
        d.frame({38, sy + 58, 872, 24}, d.vfd() ? 2 : 12, p.bg, p.strong);
        for (int i = 0; i < 7; ++i) {
            const float x = 58 + even(i * 832 / 6.f);
            d.round({x - 2, sy + 92, 4, 12}, 2, p.strong);
            d.label(std::to_string(brightnessLevels[i]) + "%", {x - 30, sy + 110, 60, 26},
                    Size::Caption, p.muted, Face::Mono, Align::Center, true);
        }
        const float thumb = 58 + even(brightness * 832 / 6.f);
        d.frame({thumb - 22, sy + 48, 44, 44}, d.vfd() ? 2 : 12, p.accent, p.card);
        hit({38, sy + 44, 872, 54}, Action::Brightness);

        // Row 1: UI Language (English / 日本語)
        float cy = sy + 152 + 14;
        card({16, cy, 916, 136}, tr("LANGUAGE", "表示言語"), 1);
        segment({38, cy + 54, 430, 64}, 40, "English", language == Language::English);
        segment({480, cy + 54, 430, 64}, 41, "日本語", language == Language::Japanese);

        // Row 2: Clock Format (12h / 24h)
        const std::string liveClock = clockString();
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("TIME FORMAT", "時刻表示"), 2, liveClock);
        segment({38, cy + 54, 430, 64}, 50, tr("12-Hour (12h)", "12時間制 (AM/PM)"), !clock24h);
        segment({480, cy + 54, 430, 64}, 51, tr("24-Hour (24h)", "24時間制 (24h)"), clock24h);

        // Row 3: Set Clock (-1h, +1h, -1m, +1m, Sync)
        cy += 136 + 14;
        const std::string clockStatus = liveClock + "  " +
            std::string(clockOffsetMinutes == 0 ? tr("(AUTO)", "(自動)") : tr("(MANUAL)", "(手動)"));
        card({16, cy, 916, 136}, tr("SET CLOCK", "時刻設定"), 3, clockStatus);
        const std::string_view clkLabels[5] = {
            tr("-1 hr", "-1 時間"), tr("+1 hr", "+1 時間"),
            tr("-1 min", "-1 分"), tr("+1 min", "+1 分"), tr("Sync", "自動同期")
        };
        float clkX = 38.f;
        for (int i = 0; i < 5; ++i) {
            const float bw = (i == 4) ? 152.f : 168.f;
            const bool subFoc = keypad && sheetSelection == 3 && clockBtnFocus == i;
            segment({clkX, cy + 54, bw, 64}, 60 + i, clkLabels[i], i == 4 && clockOffsetMinutes == 0,
                    std::nullopt, subFoc);
            clkX += bw + 12.f;
        }

        // Row 4: Visual Theme
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("VISUAL THEME", "テーマ"), 4);
        for (int i = 0; i < 3; ++i)
            segment({38.f + i * 294.f, cy + 54, 284, 64}, i,
                    jp() ? themeNamesJa[i] : themeNamesEn[i], static_cast<int>(theme) == i);

        // Row 5: Per-Theme Accent Color
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("ACCENT COLOR", "アクセントカラー"), 5);
        const auto presets = accentPresets(theme);
        const int curAcc = currentAccent();
        for (int i = 0; i < 4; ++i)
            segment({38.f + i * 220.f, cy + 54, 210, 64}, 10 + i,
                    jp() ? presets[i].jaName : presets[i].name, curAcc == i, presets[i].swatch);

        // Row 6: UI Sound FX
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("UI SOUND FX", "UI 効果音"), 6);
        segment({38, cy + 54, 430, 64}, 5, tr("Nintendo SFX", "効果音 オン"), player.sfx);
        segment({480, cy + 54, 430, 64}, 6, tr("Mute", "ミュート"), !player.sfx);

        // Row 7: Sound FX Volume (Low / Mid / High)
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("SOUND FX VOLUME", "効果音の音量"), 7);
        const std::string_view volLabels[3] = {tr("Low", "小"), tr("Mid", "中"), tr("High", "大")};
        for (int i = 0; i < 3; ++i)
            segment({38.f + i * 294.f, cy + 54, 284, 64}, 20 + i, volLabels[i], player.sfxVolume == i);

        // Row 8: Sound FX Output (Current Output / Speaker)
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("SOUND FX PLAY THROUGH", "効果音の出力先"), 8);
        segment({38, cy + 54, 430, 64}, 30, tr("Current Output", "現在の出力先"), !player.sfxSpeaker);
        segment({480, cy + 54, 430, 64}, 31, tr("Built-in Speaker", "内蔵スピーカー"), player.sfxSpeaker);

        // Row 9: Lid Close (Hall)
        cy += 136 + 14;
        card({16, cy, 916, 136}, tr("LID CLOSE (HALL)", "フタを閉じた時"), 9);
        segment({38, cy + 54, 430, 64}, 3, tr("Pocket Audio", "再生を継続"), pocket);
        segment({480, cy + 54, 430, 64}, 4, tr("Pause", "一時停止"), !pocket);

        d.unclip();
        hitClip.reset();

        const Rect sRail{railBox.x, 192, railBox.w, 560};
        drawScrollbar(d, sRail, settingsScroll, settingsLimit, Action::SettingsRail, {"1", "5", "10"});
        d.phosphor({0, 76, 1024, 692});
        return;
    }

    d.rect({0, 76, 1024, 692}, color(0x08090c, 214));
    const Track* track = (contextTrack >= 0 && contextTrack < static_cast<int>(player.tracks.size()))
                       ? &player.tracks[contextTrack] : nullptr;
    const auto items = options();

    if (sheet == Sheet::Order) {
        const auto specs = sortSpecsFor(tab);
        const auto active = activeSortSpec();
        const int fieldRows = (static_cast<int>(specs.size()) + 1) / 2;
        const float height = 114.f + 28.f + fieldRows * 88.f + 34.f + 88.f + 16.f;
        const float top = 76.f + even((692.f - height) / 2);
        d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);

        const std::string_view heading = tab == Tab::Songs ? tr("Order Songs", "曲の並び替え")
                                       : tab == Tab::Artists ? tr("Order Artists", "アーティストの並び替え")
                                       : tr("Order Albums", "アルバムの並び替え");
        const std::string sub = std::string(active.label) + " (" +
            std::string(stripLeadingArrow(ascending ? active.ascLabel : active.descLabel)) + ")";
        d.label(heading, {88, top + 22, 770, 36}, Size::Body, p.text, Face::Display, Align::Left, true);
        d.label(sub, {88, top + 60, 770, 28}, Size::Sub, p.dim, Face::Mono, Align::Left);
        button(d, {876, top + 24, 60, 60}, Action::Back, 0, {}, "×", false, false, 28, p.elevated);

        d.label(tr("ORDER BY", "並び替え項目"), {92, top + 104, 400, 24}, Size::Caption, p.muted, Face::Mono, Align::Left, true);
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
        d.label(tr("DIRECTION", "順序"), {92, dirY, 400, 24}, Size::Caption, p.muted, Face::Mono, Align::Left, true);
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
        d.phosphor({62, top, 900, height});
        return;
    }

    if (sheet == Sheet::Playlist) {
        const bool typing = input == Input::Playlist;
        const int maxVisibleRows = typing ? 2 : 4;
        const int shownRows = std::min(maxVisibleRows, static_cast<int>(items.size()));
        const float height = typing ? 378.f : (116.f + shownRows * 86.f + 88.f);
        const float top = typing ? 84.f : 76.f + even((692.f - height) / 2);
        d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);
        d.label(track ? tr("Add to Playlist", "プレイリストに追加") : tr("Playlists", "プレイリスト"),
                {88, top + 20, 770, 36}, Size::Body, p.text, Face::Display, Align::Left, true);
        const std::string sub = track ? std::string(track->title) + " — " + std::string(track->artist)
                                      : std::string(tr("Create a new playlist", "新しいプレイリストを作成"));
        d.label(sub, {88, top + 58, 770, 26}, Size::Sub, p.dim, Face::Mono, Align::Left, false, true);
        button(d, {876, top + 22, 60, 60}, Action::Back, 0, {}, "×", false, false, 28, p.elevated);

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
        d.label(playlistName.empty() ? tr("New playlist name…", "新しいプレイリスト名...") : std::string_view(playlistName),
                {inputBox.x + 18, inputBox.y, inputBox.w - 36, inputBox.h},
                Size::Sub, playlistName.empty() ? p.muted : p.text, Face::Display, Align::Left, false, true, 600);
        hit(inputBox, Action::NewPlaylist, 1);
        button(d, {796, inputY, 136, 62}, Action::CreatePlaylist, 0, {}, tr("Create", "作成"),
               keypad && !typing && sheetSelection == static_cast<int>(items.size()) + 1, false, 24,
               p.accent, p.accentInk, 24, 14);

        d.phosphor({62, top, 900, height});
        if (typing)
            keypadUi(d);
        return;
    }

    const int columns = sheet == Sheet::Track ? 2 : 1;
    const int count = (static_cast<int>(items.size()) + columns - 1) / columns;
    const float height = sheet == Sheet::Info ? 612.f : std::min(620.f, (sheet == Sheet::Track ? 124.f : 118.f) + count * 92.f);
    const float top = 76.f + even((692.f - height) / 2);
    d.frame({62, top, 900, height}, d.vfd() ? 2 : 22, p.card, p.strong);

    const std::string_view title = (sheet == Sheet::Track || sheet == Sheet::Info) && track ? track->title
        : sheet == Sheet::Info ? tr("Get Info", "詳細情報") : tr("Audio Output", "オーディオ出力");
    const float headingX = sheet == Sheet::Info && track ? 180.f : 88.f;
    const bool subline = (sheet == Sheet::Track || sheet == Sheet::Info) && track;
    if (sheet == Sheet::Info && track)
        d.art(track, {88, top + 24, 76, 76}, 12);
    d.label(title, {headingX, top + 24, 852 - headingX, subline ? 36.f : 60.f},
            Size::Body, p.text, Face::Display, Align::Left, true, true);
    if (subline) {
        const std::string sub = sheet == Sheet::Track ? std::string(track->artist) + " — " + std::string(track->album)
            : std::string(track->artist);
        d.label(sub, {headingX, top + 62, 852 - headingX, 28}, Size::Sub,
                p.dim, Face::Mono, Align::Left, false, true);
    }
    button(d, {876, top + 28, 60, 60}, Action::Back, 0, {}, "×", false, false, 28, p.elevated);

    if (sheet == Sheet::Info && track) {
        struct InfoRow { std::string_view label; std::string value; bool full; bool mono; };
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
                                : std::string(tr("Lossy", "非可逆"));
        const std::string codecKind = lowerExt == "mp3" ? std::string(tr("Lossy", "非可逆"))
                                                        : std::string(tr("Lossless", "ロスレス"));
        const std::string filePath = "/mnt/sd2/Music/" + std::string(track->artist) + "/" +
            std::to_string(track->year) + " - " + std::string(track->album) + "/" +
            twoDigits(track->number) + " - " + std::string(track->title) + "." + lowerExt;

        const InfoRow specs[] = {
            {tr("ALBUM", "アルバム"), std::string(track->album), true, false},
            {tr("ARTIST", "アーティスト"), std::string(track->artist), false, false},
            {tr("YEAR", "年"), std::to_string(track->year), false, true},
            {tr("TRACK / DISC", "トラック / ディスク"),
             "Disc " + std::to_string(track->disc) + " / Track " + twoDigits(track->number), false, true},
            {tr("DURATION", "長さ"), timecode(track->duration), false, true},
            {tr("FORMAT", "フォーマット"), std::string(track->format) + " (" + codecKind + ")", false, true},
            {tr("SAMPLE RATE", "サンプルレート"), std::string(track->rate) + "  " + depth, false, true},
            {tr("BITRATE", "ビットレート"), std::string(track->bitrate), false, true},
            {tr("CHANNELS", "チャンネル"), std::string(tr("Stereo (2.0)", "ステレオ (2.0)")), false, true},
            {tr("FILE SIZE", "ファイルサイズ"), sizeBuf, false, true},
            {tr("ADDED DATE", "追加日"), std::string(track->added), false, true},
            {tr("REPLAYGAIN", "リプレイゲイン"), "-5.4 dB (Track) / -4.8 dB (Album)", true, true},
            {tr("SIGNAL PATH", "シグナルパス"),
             std::string(tr("ALSA Direct (Bit-Perfect Hardware Output)", "ALSA ダイレクト (ビットパーフェクト出力)")), true, false},
            {tr("FILE PATH", "ファイルパス"), filePath, true, true},
        };

        float curY = 0;
        int col = 0;
        std::vector<std::pair<Rect, const InfoRow*>> placed;
        for (const auto& sp : specs) {
            if (sp.full) {
                if (col == 1) { curY += 90.f; col = 0; }
                placed.push_back({{88.f, curY, 848.f, 80.f}, &sp});
                curY += 90.f;
            } else {
                placed.push_back({{88.f + col * 430.f, curY, 418.f, 80.f}, &sp});
                if (++col == 2) { curY += 90.f; col = 0; }
            }
        }
        if (col == 1) curY += 90.f;
        const float viewportH = 472.f;
        infoMaxScroll = std::max(0, static_cast<int>(std::ceil(curY - viewportH)));
        infoScroll = std::clamp(infoScroll, 0, infoMaxScroll);

        d.clip({86, top + 114, 852, viewportH});
        for (const auto& [box, sp] : placed) {
            const Rect r{box.x, top + 116.f + box.y - infoScroll, box.w, box.h};
            if (r.y + r.h < top + 114.f || r.y > top + 114.f + viewportH)
                continue;
            d.frame(r, d.vfd() ? 2 : 12, p.elevated, p.border);
            d.label(sp->label, {r.x + 18, r.y + 10, r.w - 36, 24}, Size::Caption,
                    p.muted, Face::Mono, Align::Left, true);
            d.label(sp->value, {r.x + 18, r.y + 38, r.w - 36, 30}, Size::Sub,
                    p.text, sp->mono ? Face::Mono : Face::Display, Align::Left, true, true);
        }
        d.unclip();
    } else {
        const float w = columns == 2 ? 414.f : 840.f;
        const float baseY = top + (sheet == Sheet::Track ? 110.f : 104.f);
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const float y = baseY + (i / columns) * 92.f;
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
            const bool member = sheet == Sheet::Output && option.member;
            if (focus)
                d.strokeRound(inset(r, -4), d.vfd() ? 6 : 20, p.soft, 4);
            const Color fill = down ? p.raised : danger ? mix(p.elevated, p.danger, .14f)
                             : member ? mix(p.elevated, p.accent, .18f) : p.elevated;
            const Color stroke = focus || down || member ? p.accent : danger ? p.danger : p.strong;
            const Color ink = danger ? p.danger : member ? p.accent : p.text;
            d.frame(r, d.vfd() ? 2 : 16, fill, stroke, focus ? 4 : 2);
            d.icon(option.icon, {r.x + 22, r.y + (r.h - 32) / 2, 32, 32}, ink, 32);
            const float valueW = even(d.textWidth(option.value, Size::Sub, Face::Mono, true));
            d.label(option.label, {r.x + 70, r.y, r.w - 96 - valueW, r.h}, Size::Body,
                    ink, Face::Display, Align::Left, true, true);
            d.label(option.value, {r.x + r.w - 24 - valueW, r.y, valueW + 4, r.h}, Size::Sub,
                    member ? p.accent : option.member ? p.text : p.muted, Face::Mono, Align::Right, true);
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

void DeckUi::targetSettingsScroll(float target, bool animate)
{
    settingsScrollFrom = settingsScroll;
    settingsScrollTarget = std::clamp(target, 0.f, settingsLimit);
    settingsScrollTime = animate ? 0 : 1;
    if (!animate)
        settingsScroll = settingsScrollTarget;
}

void DeckUi::keepSelectedVisible()
{
    const auto& entries = rows();
    if (selection < 0 || selection >= static_cast<int>(entries.size()))
        return;
    const bool root = details.empty();
    const bool searchable = root && tab != Tab::Queue;
    const bool utilOpen = searchable && (utility || !query.empty() || input == Input::Search);
    const float expectedTop = root ? 92.f + (utilOpen ? 104.f : 0.f) : 196.f;
    const float expectedListH = std::max(1.f, 752.f - expectedTop);
    const float oskPad = (input == Input::Search && sheet == Sheet::None) ? 296.f : 0.f;
    scrollLimit = std::max(0.f, 4.f + cachedContentHeight + 16.f + oskPad - expectedListH);

    const Rect box = entries[selection].box;
    const float visible = std::max(60.f, expectedListH - oskPad);
    if (box.y < scrollTarget)
        targetScroll(std::max(0.f, box.y - 4.f));
    else if (box.y + box.h + 16.f > scrollTarget + visible)
        targetScroll(box.y + box.h + 16.f - visible);
}

void DeckUi::keepSettingsVisible()
{
    constexpr float offsets[10] = {0.f, 166.f, 316.f, 466.f, 616.f, 766.f, 916.f, 1066.f, 1216.f, 1366.f};
    constexpr float heights[10] = {152.f, 136.f, 136.f, 136.f, 136.f, 136.f, 136.f, 136.f, 136.f, 136.f};
    const int row = std::clamp(sheetSelection, 0, 9);
    const float topY = offsets[row];
    const float botY = topY + heights[row] + 16.f;
    const float viewH = 560.f;
    settingsLimit = std::max(0.f, 4.f + 1502.f + 16.f - viewH);
    if (topY < settingsScrollTarget)
        targetSettingsScroll(std::max(0.f, topY - 4.f));
    else if (botY > settingsScrollTarget + viewH)
        targetSettingsScroll(botY - viewH);
}

void DeckUi::scrubRail(float y)
{
    if (listBox.h <= 68)
        return;
    const float fraction = (y - listBox.y - 34) / (listBox.h - 68);
    targetScroll(fraction * scrollLimit, false);
    railPopup = .75f;
}

void DeckUi::scrubSettingsRail(float y)
{
    if (settingsBox.h <= 68)
        return;
    const float fraction = (y - settingsBox.y - 34) / (settingsBox.h - 68);
    targetSettingsScroll(fraction * settingsLimit, false);
}

void DeckUi::scrubPosition(Action which, float x)
{
    if (which == Action::Brightness) {
        activate(Action::Brightness, static_cast<int>(std::lround((x - 58) * 6 / 832)));
    } else if (which == Action::Scrub) {
        player.seek((x - deckWave.x) / deckWave.w);
        seekPopup = .65f;
    } else {
        const float fraction = std::clamp((x - volumeTrack.x - 22) / (volumeTrack.w - 44), 0.f, 1.f);
        player.volume = static_cast<int>(std::round(fraction * 100));
        if (player.volume > 0)
            lastNonZeroVolume = player.volume;
        player.mute = player.volume == 0;
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
    if (index == old && sheet == Sheet::None && !details.empty()) {
        details.clear();
        utility = toolFocus = false;
        rowsValid = false;
        selection = 0;
        targetScroll(0, false);
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
    utilityAnim = searchAnim = 0;
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
            for (const auto& entry : entries)
                order.push_back(entry.track);
            player.playFrom(std::move(order), index + static_cast<int>(player.history.size()) + 1);
            selection = 0;
        } else {
            int playIdx = 0;
            for (const auto& entry : entries) {
                if (entry.track >= 0) {
                    if (&entry == &row)
                        playIdx = static_cast<int>(order.size());
                    order.push_back(entry.track);
                }
            }
            player.playFrom(std::move(order), playIdx);
        }
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
    if (index >= 0 && index < static_cast<int>(player.tracks.size())) {
        contextTrack = index;
        contextPlaylist = playlist;
        openSheet(Sheet::Track);
    }
}

void DeckUi::openAlbum(int index)
{
    if (index < 0 || index >= static_cast<int>(player.tracks.size()))
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
    infoScroll = 0;
    if (next == Sheet::Output) {
        sheetSelection = output;
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
            flash(std::string(tr("Playlist created", "プレイリストを作成しました")));
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
        sheet = Sheet::None;
    } else if (!details.empty()) {
        details.pop_back();
        selection = 0;
        toolFocus = false;
        targetScroll(0, false);
    } else if (!query.empty()) {
        query.clear();
        toolFocus = false;
        selection = 0;
        targetScroll(0, false);
    } else if (utility || toolFocus) {
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
    releaseTime = .11f;
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
    case Action::Stage: cycleStage(); break;
    case Action::Shuffle:
        player.shuffle = !player.shuffle;
        flash(std::string(player.shuffle ? tr("Shuffle ON", "シャッフル: オン") : tr("Shuffle OFF", "シャッフル: オフ")));
        break;
    case Action::Repeat:
        player.repeat = (player.repeat + 1) % 3;
        flash(std::string(player.repeat == 2 ? tr("Repeat: ONE", "リピート: 1曲")
                        : player.repeat == 1 ? tr("Repeat: ALL", "リピート: 全曲")
                        : tr("Repeat: OFF", "リピート: オフ")));
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
        flash(std::string(albumsInGrid ? tr("Albums: Grid View", "アルバム: グリッド表示")
                                       : tr("Albums: List View", "アルバム: リスト表示")));
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
            flash(std::string(tr("Created Playlist", "プレイリストを作成しました")));
            player.chime("star");
        } else {
            openInput(Input::Playlist);
        }
        break;
    case Action::Back: back(); break;
    case Action::Brightness:
        if (std::clamp(data, 0, 6) != brightness) {
            brightness = std::clamp(data, 0, 6);
            player.chime("tick");
            flash(std::string(tr("Brightness: ", "画面の明るさ: ")) + std::to_string(brightnessLevels[brightness]) + "%");
        }
        break;
    case Action::Row: selection = data; selectRow(data); break;
    case Action::PlayNext:
        player.makeNext(data);
        flash(std::string(tr("Playing Next", "次に再生します")));
        break;
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
        if (ids.empty())
            break;
        if (action == Action::DetailQueue) {
            player.append(ids);
            flash(std::string(tr("Added to Up Next", "再生待ちに追加しました")));
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
                const auto preset = accentPresets(theme)[data - 10];
                flash(std::string(tr("Accent: ", "アクセント: ")) + std::string(jp() ? preset.jaName : preset.name));
            } else if (data == 3 || data == 4) {
                pocket = data == 3;
                flash(std::string(pocket ? tr("Lid Close: POCKET AUDIO", "フタ閉鎖時: 再生を継続")
                                         : tr("Lid Close: PAUSE", "フタ閉鎖時: 一時停止")));
            } else if (data == 5 || data == 6) {
                player.sfx = data == 5;
                flash(std::string(player.sfx ? tr("UI Sound FX: ON", "UI 効果音: オン")
                                             : tr("UI Sound FX: MUTED", "UI 効果音: ミュート")));
            } else if (data >= 20 && data < 23) {
                player.sfxVolume = data - 20;
                const std::string_view names[3] = {tr("LOW", "小"), tr("MID", "中"), tr("HIGH", "大")};
                flash(std::string(tr("SFX Volume: ", "効果音の音量: ")) + std::string(names[player.sfxVolume]));
            } else if (data == 30 || data == 31) {
                player.sfxSpeaker = (data == 31);
                flash(std::string(player.sfxSpeaker ? tr("SFX Output: SPEAKER", "効果音出力: 内蔵スピーカー")
                                                    : tr("SFX Output: CURRENT", "効果音出力: 現在の出力先")));
            } else if (data == 40 || data == 41) {
                language = (data == 41) ? Language::Japanese : Language::English;
                flash(std::string(tr("Language: English", "表示言語: 日本語")));
            } else if (data == 50 || data == 51) {
                clock24h = (data == 51);
                flash(std::string(clock24h ? tr("Clock: 24-Hour", "時刻表示: 24時間制")
                                           : tr("Clock: 12-Hour", "時刻表示: 12時間制")) + " (" + clockString() + ")");
            } else if (data >= 60 && data <= 64) {
                clockBtnFocus = data - 60;
                if (data == 60) clockOffsetMinutes = ((clockOffsetMinutes - 60) % 1440 + 1440) % 1440;
                else if (data == 61) clockOffsetMinutes = (clockOffsetMinutes + 60) % 1440;
                else if (data == 62) clockOffsetMinutes = ((clockOffsetMinutes - 1) % 1440 + 1440) % 1440;
                else if (data == 63) clockOffsetMinutes = (clockOffsetMinutes + 1) % 1440;
                else clockOffsetMinutes = 0;
                flash(std::string(data == 64 ? tr("Clock synced: ", "時刻を自動同期: ")
                                             : tr("Clock set: ", "時刻を設定: ")) + clockString());
            }
            break;
        case Sheet::Output:
            output = std::clamp(data, 0, 2);
            sheet = Sheet::None;
            flash(std::string(output == 0 ? tr("Output: 3.5mm", "出力: 3.5mm ヘッドホン")
                            : output == 1 ? tr("Output: Speakers", "出力: 内蔵スピーカー")
                            : tr("Output: BT 4.2", "出力: Bluetooth 4.2")));
            break;
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
                flash(std::string(player.stars[contextTrack]
                                  ? tr("Added to Starred", "お気に入りに追加しました")
                                  : tr("Removed from Starred", "お気に入りから解除しました")));
            } else if (data - 1 < static_cast<int>(player.playlists.size())) {
                auto& list = player.playlists[data - 1].tracks;
                const auto found = std::ranges::find(list, contextTrack);
                if (found != list.end()) {
                    list.erase(found);
                    flash(jp() ? (player.playlists[data - 1].name + " から削除しました")
                               : ("Removed from " + player.playlists[data - 1].name));
                } else {
                    list.push_back(contextTrack);
                    flash(jp() ? (player.playlists[data - 1].name + " に追加しました")
                               : ("Added to " + player.playlists[data - 1].name));
                }
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
                player.makeNext(index);
                flash(std::string(tr("Playing Next", "次に再生します")));
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
                flash(std::string(tr("Added to Up Next", "再生待ちに追加しました")));
            } else if (optId == OptRemoveQueue) {
                std::erase(player.queue, index);
                ++player.catalogRevision;
            } else if (optId == OptRemovePlaylist) {
                if (contextPlaylist >= 0 && contextPlaylist < static_cast<int>(player.playlists.size())) {
                    std::erase(player.playlists[contextPlaylist].tracks, index);
                    ++player.catalogRevision;
                }
                if (!details.empty())
                    std::erase(details.back().ids, index);
            } else if (optId == OptShuffle) {
                player.shuffle = !player.shuffle;
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
    if (!textInput())
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
        tabSliding = tabSlideMoved = tabSlideCommitted = false;
        listPointer = sheet == Sheet::None && tab != Tab::Deck && !textInput() && inside(listBox, x, y);
        settingsPointer = sheet == Sheet::Settings && inside(settingsBox, x, y);
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
        } else if (sheet == Sheet::Settings && settingsPointer &&
                   (dragging || std::abs(y - pointerStartY) > 12)) {
            dragging = true;
            targetSettingsScroll(settingsScroll + (pointerLastY - y), false);
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

    listPointer = settingsPointer = false;
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
    lastAction = item.action;
    lastData = item.data;
    releaseTime = .11f;
    if (!dragging && !held && inside(item.box, x, y) &&
        item.action != Action::Scrub && item.action != Action::Volume &&
        item.action != Action::Rail && item.action != Action::SettingsRail && item.action != Action::Brightness) {
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
    if (sheet == Sheet::Info) {
        infoScroll = std::clamp(infoScroll - static_cast<int>(direction * 96), 0, infoMaxScroll);
        return;
    }
    if (sheet == Sheet::Settings) {
        targetSettingsScroll(settingsScrollTarget - direction * 96);
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
    heldKeyVoices.clear();
    heldKeyUi.clear();
    tabSliding = tabSlideMoved = tabSlideCommitted = false;
    seekDirection = 0;
    analogPosition = 0;
    powerDown = false;
    dragging = false;
    listPointer = settingsPointer = false;
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
    if (detail && details.back().source == Tab::Artists) {
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
    const float nomRowH = !entries.empty() ? (entries[0].box.h + 12.f) : 120.f;
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
            const int next = std::clamp(infoScroll + (value == SDLK_UP ? -140 : 140), 0, infoMaxScroll);
            if (next == infoScroll) player.chime("bump");
            infoScroll = next;
        }
        if (confirm(value))
            back();
        return;
    }
    if (sheet == Sheet::Settings) {
        if (value == SDLK_UP || value == SDLK_DOWN) {
            const int next = std::clamp(sheetSelection + (value == SDLK_UP ? -1 : 1), 0, 9);
            if (next == sheetSelection) player.chime("bump");
            sheetSelection = next;
            keepSettingsVisible();
            return;
        }
        if (value == SDLK_LEFT || value == SDLK_RIGHT || confirm(value)) {
            const int delta = value == SDLK_LEFT ? -1 : 1;
            if (sheetSelection == 0 && !confirm(value)) {
                const int prev = brightness;
                activate(Action::Brightness, brightness + delta);
                if (brightness == prev) player.chime("bump");
            } else if (sheetSelection == 1) {
                activate(Action::Option, confirm(value) ? (jp() ? 40 : 41) : (delta < 0 ? 40 : 41));
            } else if (sheetSelection == 2) {
                activate(Action::Option, confirm(value) ? (clock24h ? 50 : 51) : (delta < 0 ? 50 : 51));
            } else if (sheetSelection == 3) {
                if (confirm(value)) {
                    activate(Action::Option, 60 + std::clamp(clockBtnFocus, 0, 4));
                } else {
                    const int next = std::clamp(clockBtnFocus + delta, 0, 4);
                    if (next == clockBtnFocus) player.chime("bump");
                    clockBtnFocus = next;
                }
            } else if (sheetSelection == 4) {
                const int next = (static_cast<int>(theme) + delta + 3) % 3;
                activate(Action::Option, next);
            } else if (sheetSelection == 5) {
                const int cur = currentAccent();
                activate(Action::Option, 10 + (cur + delta + 4) % 4);
            } else if (sheetSelection == 6) {
                activate(Action::Option, confirm(value) ? (player.sfx ? 6 : 5) : (delta < 0 ? 5 : 6));
            } else if (sheetSelection == 7) {
                const int next = confirm(value) ? ((player.sfxVolume + 1) % 3)
                                                : std::clamp(player.sfxVolume + delta, 0, 2);
                activate(Action::Option, 20 + next);
            } else if (sheetSelection == 8) {
                activate(Action::Option, confirm(value) ? (player.sfxSpeaker ? 30 : 31) : (delta < 0 ? 30 : 31));
            } else if (sheetSelection == 9) {
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

    const int count = static_cast<int>(options().size());
    const int columns = sheet == Sheet::Track || sheet == Sheet::Order ? 2 : 1;
    if (confirm(value) && count > 0) {
        activate(Action::Option, sheetSelection);
        return;
    }
    int next = sheetSelection;
    if (value == SDLK_UP) next -= columns;
    if (value == SDLK_DOWN) next += columns;
    if (value == SDLK_LEFT && columns == 2) --next;
    if (value == SDLK_RIGHT && columns == 2) ++next;
    const int clamped = std::clamp(next, 0, std::max(0, count - 1));
    if ((value == SDLK_UP || value == SDLK_DOWN || value == SDLK_LEFT || value == SDLK_RIGHT) && clamped == sheetSelection)
        player.chime("bump");
    sheetSelection = clamped;
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
        const auto heldIt = heldKeyVoices.find(rawKey);
        if ((value == SDLK_LEFT && seekDirection == -1) || (value == SDLK_RIGHT && seekDirection == 1)) {
            if (heldIt != heldKeyVoices.end())
                heldKeyVoices.erase(heldIt);
            if (seekHold < .32f) {
                seekDirection < 0 ? player.previous() : player.next();
                player.chime("", true);
            }
            seekDirection = 0;
            return;
        }
        if (heldIt != heldKeyVoices.end()) {
            const auto voice = heldIt->second;
            heldKeyVoices.erase(heldIt);
            if (voice != "NONE")
                player.chime(voice, true);
        }
        return;
    }

    const auto beginTwoStep = [&](std::string_view voice, Action uiAct = Action::None, int uiData = 0) {
        if (!repeated) {
            player.chime(voice, false);
            heldKeyVoices[rawKey] = voice;
            if (uiAct != Action::None)
                heldKeyUi[rawKey] = {uiAct, uiData};
        }
    };
    const auto suppressRelease = [&](Action uiAct = Action::None, int uiData = 0) {
        if (!repeated) {
            heldKeyVoices[rawKey] = "NONE";
            if (uiAct != Action::None)
                heldKeyUi[rawKey] = {uiAct, uiData};
        }
    };

    if (value == SDLK_F1 && !repeated) {
        beginTwoStep({});
        showKeys = !showKeys;
        return;
    }
    if (value == SDLK_F2 && !repeated) {
        beginTwoStep({});
        scaleMode = static_cast<PreviewScale>((static_cast<int>(scaleMode) + 1) %
                                             static_cast<int>(PreviewScale::Count));
        return;
    }
    if (showKeys) {
        if (value == SDLK_ESCAPE && !repeated) {
            beginTwoStep("back");
            showKeys = false;
        }
        return;
    }
    if (value == SDLK_L && !repeated && (controller || !textInput())) {
        beginTwoStep({});
        toggleLid();
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
                beginTwoStep(voiceFor(Action::Play));
                activate(Action::Play, 0);
            } else if (value == SDLK_LEFTBRACKET) {
                beginTwoStep({});
                player.previous();
            } else {
                beginTwoStep({});
                player.next();
            }
        }
        return;
    }
    if (textInput()) {
        if (value == SDLK_ESCAPE || (controller && value == SDLK_B)) {
            if (!repeated) {
                beginTwoStep("back", Action::ClearSearch, 0);
                closeInput(false);
            }
        } else if (value == SDLK_BACKSPACE || (controller && value == SDLK_Y)) {
            int bkspCode = 9;
            for (const auto& k : keyAssets)
                if (k.symbols == symbols && k.action == "bksp")
                    bkspCode = k.row * keyColumns + k.column + k.span / 2;
            if (repeated) player.chime("tick");
            else beginTwoStep("back", Action::Key, bkspCode);
            eraseCharacter();
        } else if (value == SDLK_RETURN || value == SDLK_KP_ENTER) {
            if (!repeated) {
                if (controller || keypad) {
                    const auto* k = referenceKey(symbols, keySelection);
                    const int code = k ? (k->row * keyColumns + k->column + k->span / 2) : keySelection;
                    beginTwoStep(voiceFor(Action::Key, code), Action::Key, code);
                    activate(Action::Key, code);
                } else {
                    beginTwoStep("confirm");
                    closeInput(true);
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
                    suppressRelease();
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
        beginTwoStep(voiceFor(Action::Play), Action::Play, 0);
        activate(Action::Play, 0);
        return;
    }
    if (value == SDLK_LEFTBRACKET && !repeated) { beginTwoStep({}, Action::Previous, 0); player.previous(); return; }
    if (value == SDLK_RIGHTBRACKET && !repeated) { beginTwoStep({}, Action::Next, 0); player.next(); return; }
    if (value == SDLK_H && !repeated) {
        beginTwoStep("confirm", Action::Album, player.current);
        details.empty() ? openAlbum(player.current) : setTab(0);
        return;
    }
    if (value == SDLK_G && !repeated) {
        player.chime("", false);
        suppressRelease(Action::Star, player.current);
        activate(Action::Star, player.current);
        return;
    }
    if (value == SDLK_ESCAPE || value == SDLK_B) {
        if (!repeated) {
            beginTwoStep("back", Action::Back, 0);
            back();
        }
        return;
    }
    if (sheet != Sheet::None) {
        keypad = true;
        const bool allowClockRepeat = (sheet == Sheet::Settings && sheetSelection == 3 && clockBtnFocus < 4);
        if (!repeated || !confirm(value) || allowClockRepeat) {
            if (confirm(value)) {
                const int optTarget = (sheet == Sheet::Settings && sheetSelection == 3)
                                    ? (60 + std::clamp(clockBtnFocus, 0, 4)) : sheetSelection;
                const auto v = sheet == Sheet::Info ? std::string_view("back") : voiceFor(Action::Option, optTarget);
                const bool isStar = (sheet == Sheet::Track && sheetSelection >= 0 &&
                    sheetSelection < static_cast<int>(options().size()) &&
                    options()[sheetSelection].id == OptStar) ||
                    (sheet == Sheet::Playlist && sheetSelection == 0);
                if (repeated) {
                    player.chime("tick");
                } else if (isStar) {
                    player.chime("", false);
                    suppressRelease(Action::Option, optTarget);
                } else {
                    beginTwoStep(v, Action::Option, optTarget);
                }
            } else if (repeated) {
                player.chime("tick");
            } else {
                beginTwoStep({});
            }
            sheetKey(value);
        }
        return;
    }
    if (value == SDLK_Q || value == SDLK_E) {
        if (!repeated) {
            const int cur = static_cast<int>(tab);
            const int index = cur + (value == SDLK_Q ? -1 : 1);
            if (index >= 0 && index < 6)
                beginTwoStep("deck", Action::Tab, index);
            else
                suppressRelease(Action::Tab, index);
            setTab(index);
        }
        return;
    }
    if (value == SDLK_X && !repeated) {
        beginTwoStep("stage", Action::Stage, 0);
        cycleStage();
        return;
    }
    if (value == SDLK_S || value == SDLK_TAB) {
        if (!repeated) {
            const Action uiAct = tab == Tab::Deck ? Action::Settings
                               : tab == Tab::Playlists ? Action::NewPlaylist : Action::Order;
            beginTwoStep({}, uiAct, 0);
            if (tab == Tab::Deck) openSheet(Sheet::Settings);
            else if (tab == Tab::Playlists) activate(Action::NewPlaylist, 0);
            else if (tab != Tab::Queue) openSheet(Sheet::Order);
        }
        return;
    }
    if (value == SDLK_Y && !repeated) {
        if (tab == Tab::Deck) {
            beginTwoStep({}, Action::Context, player.current);
            openContext(player.current);
        } else {
            beginTwoStep({}, Action::RowMenu, selection);
            activate(Action::RowMenu, selection);
        }
        return;
    }
    if ((value == SDLK_SLASH || value == SDLK_F) && tab != Tab::Deck && tab != Tab::Queue && details.empty()) {
        if (!repeated) {
            beginTwoStep({}, Action::Search, 0);
            openInput(Input::Search);
        }
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
                if (!toolFocus && rows().empty())
                    suppressRelease();
                else
                    beginTwoStep(v, uiAct, uiData);
            } else if (repeated) {
                player.chime("tick");
            } else {
                beginTwoStep({});
            }
            libraryKey(value);
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
        beginTwoStep(voiceFor(Action::Play), Action::Play, 0);
        activate(Action::Play, 0);
    }
}

void DeckUi::configureSnapshot(int preset)
{
    player.sfx = false;
    cancelInput();
    noticeTime = 0;
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
    language = Language::English;
    clock24h = true;
    clockOffsetMinutes = 0;
    clockBtnFocus = 1;
    themeAccents = {0, 0, 0};
    stage = 0;
    setTab(0);
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
        sheetSelection = 5;
        settingsScroll = settingsScrollTarget = 670.f;
        settingsScrollTime = 1.f;
        break;
    case 10: // VFD Amber Phosphor Accent + VU Meters + Japanese Track!
        theme = Theme::Vfd;
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
    case 12: // DECK SETTINGS (Language + 12h/24h Clock + Set Clock, 12h AM/PM in Top Bar!)
        clock24h = false;
        clockOffsetMinutes = 90;
        setTab(0);
        openSheet(Sheet::Settings);
        keypad = true;
        sheetSelection = 3;
        clockBtnFocus = 1;
        settingsScroll = settingsScrollTarget = 160.f;
        settingsScrollTime = 1.f;
        break;
    case 13: // JAPANESE UI MODE (Settings in 日本語 + 12時間制 + Set Clock!)
        language = Language::Japanese;
        clock24h = false;
        setTab(0);
        openSheet(Sheet::Settings);
        keypad = true;
        sheetSelection = 1;
        settingsScroll = settingsScrollTarget = 160.f;
        settingsScrollTime = 1.f;
        break;
    case 14: // JAPANESE UI MODE (Albums -1 Utility Bar + Order Sheet / Japanese Tabs!)
        language = Language::Japanese;
        setTab(4);
        openSheet(Sheet::Order);
        keypad = true;
        break;
    default:
        break;
    }
    player.sfx = true;
    tabTime = 1.f;
    for (int i = 0; i < 6; ++i)
        tabWidths[i] = (i == static_cast<int>(tab) ? 260.f : 108.f);
    rowsValid = false;
}

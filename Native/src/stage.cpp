#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
constexpr float pi = std::numbers::pi_v<float>;
constexpr Rect topArt{24, 118, 544, 544};

void arc(Draw& d, float cx, float cy, float r, float begin, float end, Color tint, float thickness)
{
    const int pieces = std::clamp(static_cast<int>(std::ceil(std::abs(end - begin) * r / 6)), 1, 96);
    for (int i = 0; i < pieces; ++i) {
        const float a = std::lerp(begin, end, static_cast<float>(i) / pieces);
        const float b = std::lerp(begin, end, static_cast<float>(i + 1) / pieces);
        d.line(cx + std::cos(a) * r, cy + std::sin(a) * r,
               cx + std::cos(b) * r, cy + std::sin(b) * r, tint, thickness);
    }
}

void vu(Draw& d, float x, float level, float held, std::string_view channel)
{
    const auto& p = d.p();
    const bool ceramic = d.theme == Theme::Ceramic;
    const Rect r{x, 88, 478, 540};
    const float cx = x + r.w / 2;
    const float cy = r.y + r.h - 34;
    constexpr float start = -.70f * pi, end = -.30f * pi, redT = .72f;
    constexpr float red = std::lerp(start, end, redT);
    constexpr std::pair<float, std::string_view> marks[] = {
        {0, "-20"}, {.22f, "-10"}, {.36f, "-7"}, {.55f, "-3"}, {redT, "0"}, {1, "+3"},
    };
    const bool overload = level > .84f;
    const Color ink = ceramic ? color(0x2c2720) : color(0xf5e4c8);
    const Color soft = ceramic ? alpha(ink, .55f) : color(0xffe4b9, 140);

    const auto tick = [&](float angle, float radius, float from, float to, Color c, float width) {
        const float dx = std::cos(angle), dy = std::sin(angle);
        d.line(cx + dx * (radius + from), cy + dy * (radius + from),
               cx + dx * (radius + to), cy + dy * (radius + to), c, width);
    };
    const auto markLabels = [&](float radius, Color normal) {
        for (const auto& [position, text] : marks) {
            const float angle = std::lerp(start, end, position);
            const float xx = cx + std::cos(angle) * (radius - 44);
            const float yy = cy + std::sin(angle) * (radius - 44);
            d.label(text, {xx - 36, yy - 15, 72, 30}, Size::MeterMark,
                    position >= redT ? p.danger : normal, Face::Mono, Align::Center, true);
        }
    };
    const auto legend = [&](float offset, float center, std::string_view text, Size size, Color c, Face type, int weight) {
        d.label(text, {offset - 76, r.y + center - 24, 152, 48}, size, c, type, Align::Center,
                false, false, weight);
    };

    if (d.vfd()) {
        constexpr float radius = 324;
        d.frame(r, 2, p.vuBase, p.strong);
        d.clip(inset(r, 2));
        for (int gx = 8; gx < r.w - 8; gx += 8)
            for (int gy = 8; gy < r.h - 8; gy += 8)
                d.rect({x + gx, r.y + gy, 4, 4}, mix(p.vuBase, p.wave, .45f));

        constexpr int segments = 24;
        const int lit = static_cast<int>(std::lround(level * segments));
        const int peak = held > .04f ? std::min(segments - 1, static_cast<int>(held * segments)) : -1;
        for (int i = 0; i < segments; ++i) {
            const bool hot = i / static_cast<float>(segments) >= redT;
            const auto tint = i == peak && i >= lit ? (hot ? p.danger : p.text) :
                              i < lit ? (hot ? p.danger : p.accent) :
                              (hot ? alpha(p.danger, .16f) : alpha(p.accent, .12f));
            arc(d, cx, cy, radius, std::lerp(start, end, i / 24.f), std::lerp(start, end, (i + .7f) / 24.f),
                tint, 24);
        }
        markLabels(radius, p.text);
        d.label(channel, {x + 24, r.y + 12, 140, 48}, Size::Sub, p.dim, Face::Mono);
        const Rect peakBox{x + r.w - 88, r.y + 22, 64, 28};
        d.rect(peakBox, overload ? p.danger : alpha(p.danger, .18f));
        d.label("PEAK", peakBox, Size::Caption, overload ? p.vuBase : alpha(p.danger, .45f), Face::Mono, Align::Center);
        legend(cx, 360, "VU", Size::Title, p.text, Face::Display, 400);
        legend(cx, 394, "dB", Size::Caption, p.dim, Face::Mono, 400);
        d.unclip();
        return;
    }

    constexpr float radius = 336;
    d.vuDial(r);
    d.clip(inset(r, 2));
    arc(d, cx, cy, radius, start, red, ceramic ? color(0x3a342a) : color(0xffe4b9, 199), 5);
    arc(d, cx, cy, radius, red, end, p.danger, 8);
    for (int i = 1; i < 20; ++i) {
        const float position = i / 20.f;
        tick(std::lerp(start, end, position), radius, -10, 5, position >= redT ? p.danger : soft, 2);
    }
    for (const auto& [position, text] : marks) {
        (void)text;
        tick(std::lerp(start, end, position), radius, -16, 12, position >= redT ? p.danger : ink, 3);
    }
    markLabels(radius, ink);
    d.label(channel, {x + 28, r.y + 12, 140, 48}, Size::Caption, soft, Face::Mono, Align::Left, true);
    if (overload)
        d.glow(x + r.w - 34, r.y + 36, 26, alpha(p.danger, .7f), alpha(p.danger, .25f));
    d.circle(x + r.w - 34, r.y + 36, 10, overload ? p.danger : ceramic ? color(0xcdc4b2) : color(0x2a2220));
    legend(cx, 344, "VU", Size::Title, ceramic ? ink : color(0xffe0b4, 230), Face::Display, 700);
    legend(cx, 380, "dB", Size::Caption, soft, Face::Mono, 600);

    const float angle = std::lerp(start, end, level);
    const float xx = cx + std::cos(angle) * (radius + 8);
    const float yy = cy + std::sin(angle) * (radius + 8);
    // The needle and its shadow end at the same lower edge as the pivot shield.
    d.clip({x + 2, r.y + 2, r.w - 4, cy - r.y - 2});
    d.line(cx + 4, cy + 4, xx + 4, yy + 4, color(0x000000, 71), 6);
    d.line(cx, cy, xx, yy, p.accent, 5);
    d.circle(xx, yy, 2.5f, p.accent);
    d.circle(cx, cy, 40, ceramic ? color(0x2c2720) : color(0x18191d));
    arc(d, cx, cy, 40, pi, 2 * pi, color(0xffffff, ceramic ? 46 : 31), 2);
    d.unclip();
}

void spectrum(Draw& d, const Meter& meter)
{
    const auto& p = d.p();
    const bool ceramic = d.theme == Theme::Ceramic;
    constexpr float oscY = 64 + 112;
    constexpr float oscScale = 112;
    constexpr float baseline = 768 - 138;
    constexpr float maxH = 344;
    d.clip({24, 72, 976, 208});
    d.rect({24, oscY, 976, 2}, p.border);
    if (d.vfd()) {
        for (int x = 24; x < 1000; x += 8) {
            const int index = (x - 24) * 255 / 976;
            const float y = std::clamp(std::round((oscY + meter.wave[index] * oscScale) / 8) * 8, 72.f, 272.f);
            d.rect({static_cast<float>(x), oscY, 6, 6}, mix(p.bg, p.wave, .65f));
            d.rect({static_cast<float>(x), y, 6, 6}, p.text);
        }
    } else {
        float px = 24, py = std::clamp(oscY + meter.wave[0] * oscScale, 74.f, 278.f);
        for (size_t i = 1; i < meter.wave.size(); ++i) {
            const float x = 24 + 976 * i / static_cast<float>(meter.wave.size() - 1);
            const float y = std::clamp(oscY + meter.wave[i] * oscScale, 74.f, 278.f);
            d.line(px, py, x, y, alpha(p.text, .78f), 2);
            px = x;
            py = y;
        }
    }
    d.unclip();
    if (!d.vfd())
        d.rect({24, baseline, 976, 2}, p.border);

    for (int i = 0; i < 28; ++i) {
        const float start = even(24 + i * 976 / 28.f) + 4;
        const float width = even(24 + (i + 1) * 976 / 28.f) - start - 4;
        if (d.vfd()) {
            const int lit = std::max(1, static_cast<int>(std::round(meter.band[i] * 22)));
            const int peak = std::min(21, static_cast<int>(meter.bandPeak[i] * 22));
            for (int segment = 0; segment < 22; ++segment) {
                const auto ink = segment == peak && lit > 1 ? p.text
                    : segment < lit ? (segment >= 18 ? p.danger : p.accent)
                    : segment >= 18 ? alpha(p.danger, .12f) : mix(p.bg, p.wave, .68f);
                d.rect({start, baseline - (segment + 1) * 16, width, 12}, ink);
            }
        } else {
            const float bar = std::max(8.f, even(meter.band[i] * maxH));
            const Color dark = ceramic ? color(0xc8c0b2) : color(0x222630);
            const Color mid = ceramic ? color(0x8b857a) : color(0x414859);
            const Color high = ceramic ? color(0x3f424a) : color(0x8a92a4);
            const float stops[] = {0.f, .28f * maxH, .68f * maxH, maxH};
            const Color palette[] = {dark, mid, high, p.accent};
            for (int s = 0; s < 3; ++s) {
                if (bar <= stops[s])
                    break;
                const float lo = stops[s];
                const float hi = std::min(bar, stops[s + 1]);
                const float span = stops[s + 1] - stops[s];
                const Color topColor = mix(palette[s], palette[s + 1], (hi - lo) / span);
                d.gradient({start, baseline - hi, width, hi - lo}, topColor, palette[s]);
            }
            d.rect({start, baseline - std::max(8.f, even(meter.bandPeak[i] * maxH)) - 8, width, 4},
                   meter.bandPeak[i] > .62f ? p.accent : p.dim);
        }
    }
}
}

void Meter::update(const Player& player, double dt)
{
    std::array<float, 2> levels{};
    for (size_t i = 0; i < wave.size(); ++i) {
        const double time = std::max(0.0, player.seconds + (static_cast<double>(i) - 256) / 8192);
        const auto sample = player.sample(time);
        wave[i] = (sample[0] + sample[1]) * .5f;
        for (int channel = 0; channel < 2; ++channel)
            levels[channel] += sample[channel] * sample[channel];
    }
    for (int channel = 0; channel < 2; ++channel) {
        const float rms = std::sqrt(levels[channel] / wave.size());
        const float db = rms > .0001f ? std::max(-60.f, 20 * std::log10(rms * 2.6f)) : -60.f;
        const float level = std::clamp((db + 48) / 48, 0.f, 1.f);
        const float response = 1 - std::pow(level > needle[channel] ? .65f : .9f, static_cast<float>(dt * 60));
        needle[channel] = std::lerp(needle[channel], level, response);
        peak[channel] = std::max(needle[channel], peak[channel] - static_cast<float>(.48 * dt));
    }

    static const auto analysis = [] {
        std::array<std::array<SDL_FPoint, 256>, 28> coefficients{};
        for (int i = 0; i < 28; ++i) {
            const float bin = 2 + std::pow(i / 27.f, 1.55f) * 62;
            for (int j = 0; j < 256; ++j) {
                const float radians = 2 * pi * bin * j / 256;
                const float window = .5f - .5f * std::cos(2 * pi * j / 255);
                coefficients[i][j] = {std::cos(radians) * window, std::sin(radians) * window};
            }
        }
        return coefficients;
    }();
    for (size_t i = 0; i < band.size(); ++i) {
        float x = 0, y = 0;
        for (size_t j = 0; j < wave.size(); ++j) {
            x += wave[j] * analysis[i][j].x;
            y += wave[j] * analysis[i][j].y;
        }
        const float rms = std::hypot(x, y) / 64.f;
        const float decibels = 20 * std::log10(std::max(rms, 1.e-5f));
        const float magnitude = std::clamp((decibels + 80) / 61, 0.f, 1.f);
        band[i] = std::lerp(band[i], magnitude, 1 - std::pow(.78f, static_cast<float>(dt * 60)));
        bandPeak[i] = std::max(band[i], bandPeak[i] - static_cast<float>(.48 * dt));
    }
}

void drawStage(Draw& d, const Player& player, const Meter& meter, int stage, float stageCentering)
{
    const auto& p = d.p();
    d.clear(p.bg);
    if (stage == 0) {
        constexpr float centeredCoverY = (64.f + 768.f - topArt.h) / 2;
        const float offsetY = even((centeredCoverY - topArt.y) * stageCentering);
        if (const auto* track = player.track()) {
            if (!d.vfd())
                d.glow(302, 382 + offsetY, 730, alpha(color(track->colors[0]), .20f), alpha(color(track->colors[1]), .18f));
            d.label(track->title, {604, 310 + offsetY, 396, 54}, Size::Display, p.text, Face::Display, Align::Left, true, true, 700);
            d.label(track->artist, {604, 382 + offsetY, 396, 38}, Size::Title, p.dim, Face::Display, Align::Left, false, true, 600);
            d.label(track->album, {604, 435 + offsetY, 396, 38}, Size::Title, p.muted, Face::Display, Align::Left, false, true, 500);
        }
        d.art(player.track(), {topArt.x, topArt.y + offsetY, topArt.w, topArt.h}, d.vfd() ? 2.f : 24.f);
    } else if (stage == 1) {
        vu(d, 24, meter.needle[0], meter.peak[0], "LEFT");
        vu(d, 522, meter.needle[1], meter.peak[1], "RIGHT");
    } else {
        spectrum(d, meter);
    }
}

void drawTopChrome(Draw& d, const Player& player, float dockTitle, std::string_view clockText,
                   int batteryPercent, bool wifiConnected, bool bluetoothConnected, float progressSlide, float stageCentering)
{
    const auto& p = d.p();
    const auto* track = player.track();
    d.rect({0, 0, 1024, 64}, p.elevated);
    d.rect({0, 62, 1024, 2}, p.border);
    constexpr float iconDim = 32.f;
    constexpr float iconY = 16.f;
    constexpr Size statusSize = Size::Body;
    constexpr int statusWeight = 600;
    constexpr float formatX = 68.f;
    constexpr float gap = 18;
    const auto statusWidth = [&](std::string_view text) {
        return even(d.textWidth(text, statusSize, Face::Mono, false) + 4);
    };
    d.icon(track ? player.playing ? "play" : "pause" : "stop", {24, iconY, iconDim, iconDim}, p.dim);

    const float clockW = std::max(68.f, statusWidth(clockText));
    const float clockX = 1000.f - clockW;
    const float batX = clockX - gap - iconDim;
    const float wifiX = batX - gap - iconDim;
    const float bluetoothX = wifiX - gap - iconDim;
    const float starX = bluetoothX - gap - iconDim;
    if (track) {
        const bool starred = player.stars[player.current];
        const float formatRight = (starred ? starX : bluetoothX) - gap;
        const auto codec = track->format.substr(0, track->format.find(' '));
        d.label(codec, {formatX, 0, formatRight - formatX, 62},
                statusSize, p.dim, Face::Mono, Align::Left, false, true, statusWeight);
        if (starred)
            d.icon("star-filled", {starX, iconY, iconDim, iconDim}, p.dim);
    }
    d.icon("bluetooth", {bluetoothX, iconY, iconDim, iconDim}, bluetoothConnected ? p.dim : alpha(p.dim, .32f), 0);
    d.icon(wifiConnected ? "wifi" : "wifi-off", {wifiX, iconY, iconDim, iconDim},
           wifiConnected ? p.dim : p.muted, 0);
    d.icon("battery", {batX, iconY, iconDim, iconDim}, p.dim, 0);
    const int batterySteps = (std::clamp(batteryPercent, 0, 100) + 5) / 10;
    // The fill spans x=4–24 and y=11–21 within the 32 px battery icon.
    if (batterySteps > 0)
        d.rect({batX + 4, iconY + 11, 2.f * batterySteps, 10}, p.dim);

    d.label(clockText, {clockX, 0, clockW, 62}, statusSize, p.dim, Face::Mono, Align::Right, false, false, statusWeight);
    if (track && dockTitle > 0) {
        constexpr float meterAreaBottom = 632;
        constexpr float titleHeight = 48;
        constexpr float centeredTitleY = (meterAreaBottom + 768 - titleHeight) / 2;
        const float titleY = even(std::lerp(656.f, centeredTitleY, stageCentering));
        d.label(track->title, {24, titleY, 976, titleHeight}, Size::Title, alpha(p.text, dockTitle),
                Face::Display, Align::Center, true, true);
    }

    if (progressSlide >= 1.f)
        return;

    const std::string elapsed = timecode(player.seconds);
    const std::string duration = timecode(track ? track->duration : 0);
    const float timeWidth = std::max(66.f, even(2 + std::max(d.textWidth(elapsed, Size::Sub, Face::Mono, true),
                                                             d.textWidth(duration, Size::Sub, Face::Mono, true))));
    constexpr float dockY = 710;
    const float y = dockY + even((768 - dockY) * progressSlide);
    const Rect rail{44 + timeWidth, y + 12, 936 - 2 * timeWidth, 12};
    d.label(elapsed, {24, y, timeWidth, 34}, Size::Sub, p.text, Face::Mono, Align::Left, false, false, 600);
    d.label(duration, {1000 - timeWidth, y, timeWidth, 34}, Size::Sub, p.dim, Face::Mono, Align::Right, false, false, 600);
    d.frame(rail, d.vfd() ? 2 : 6, p.card, p.strong);
    const float filled = even((rail.w - 4) * player.progress());
    if (d.vfd()) {
        for (float x = 0; x < rail.w - 4; x += 8)
            d.rect({rail.x + 2 + x, rail.y + 2, 6, 8}, x < filled ? p.dim : p.wave);
    } else {
        d.round({rail.x + 2, rail.y + 2, filled, 8}, 4, p.dim);
    }
}

void drawWaveform(Draw& d, const Player& player, Rect area, float reveal)
{
    const auto& p = d.p();
    d.frame(area, d.vfd() ? 2 : 10, p.bg, p.border);
    if (reveal <= 0.f)
        return;

    const Rect r = inset(area, 2);
    const float center = r.y + r.h / 2;
    const auto* track = player.track();
    const auto wave = waveform(track);
    const float played = r.w * player.progress();
    const float progressX = r.x + played;
    constexpr float playheadWidth = 5.f;
    const float playhead = std::clamp(progressX - playheadWidth / 2, r.x, r.x + r.w - playheadWidth);
    const auto paintPlayed = [&](const auto& paint) {
        if (played < 1.f)
            return;
        d.clip({r.x, r.y, played, r.h});
        paint(p.dim, progressX);
        d.unclip();
    };
    if (d.vfd()) {
        const int columns = static_cast<int>(r.w / 8), rows = static_cast<int>(r.h / 8);
        const int firstRow = std::min((rows - 1) / 2, static_cast<int>(std::ceil((1 - reveal) * rows / 2)));
        const float middle = (rows - 1) / 2.f;
        const Color background = mix(p.bg, p.wave, .45f);
        for (int row = firstRow; row < rows - firstRow; ++row)
            for (int column = 0; column < columns; ++column)
                d.rect({r.x + column * 8 + 1, r.y + row * 8 + 1, 6, 6}, background);
        if (track) {
            const auto paint = [&](Color ink, float right) {
                for (int column = 0; column < columns; ++column) {
                    const float x = r.x + column * 8 + 1;
                    if (x >= right)
                        break;
                    const int source = column * static_cast<int>(wave.size()) / columns;
                    const float span = std::max(1.f, std::round(wave[source] * rows * .46f));
                    for (int row = firstRow; row < rows - firstRow; ++row)
                        if (std::abs(row - middle) <= span)
                            d.rect({x, r.y + row * 8 + 1, 6, 6}, ink);
                }
            };
            paint(mix(p.wave, p.muted, .45f), r.x + r.w);
            paintPlayed(paint);
            for (int row = firstRow; row < rows - firstRow; ++row) {
                d.rect({playhead, r.y + row * 8 + 1, playheadWidth, 6}, p.text);
            }
        }
        return;
    }
    if (!track) {
        d.rect({r.x, center - 1, r.w, 2}, p.border);
        return;
    }

    constexpr int bars = 80;
    const float pitch = r.w / bars;
    const auto paint = [&](Color ink, float right) {
        for (int i = 0; i < bars; ++i) {
            const float x = r.x + 2 + i * pitch;
            if (x >= right)
                break;
            const float amplitude = std::max(12.f, wave[i * 2] * (r.h - 20)) * reveal;
            d.round({x, center - amplitude / 2, std::max(5.f, pitch - 4), amplitude}, 3, ink);
        }
    };
    paint(p.wave, r.x + r.w);
    paintPlayed(paint);
    const float playheadH = (r.h - 8) * reveal;
    d.rect({playhead, center - playheadH / 2, playheadWidth, playheadH}, p.text);
}

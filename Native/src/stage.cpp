#include "stage.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace {
constexpr float pi = std::numbers::pi_v<float>;
constexpr Rect topArt{24, 118, 544, 544};
constexpr float vuRedPosition = .72f;
constexpr size_t spectrumCapacity = 16384;
using SpectrumBands = decltype(Meter::band);
using FftRoots = std::array<std::complex<float>, spectrumCapacity / 2 + 1>;
struct VuMark {
    float position, db;
    std::string_view label;
};
constexpr std::array vuMarks{
    VuMark{0, -20, "-20"}, VuMark{.22f, -10, "-10"}, VuMark{.36f, -7, "-7"},
    VuMark{.55f, -3, "-3"}, VuMark{vuRedPosition, 0, "0"}, VuMark{1, 3, "+3"},
};

float vuPosition(float db)
{
    if (db <= vuMarks.front().db)
        return vuMarks.front().position;
    for (size_t i = 1; i < vuMarks.size(); ++i) {
        const auto& low = vuMarks[i - 1];
        const auto& high = vuMarks[i];
        if (db <= high.db)
            return std::lerp(low.position, high.position, (db - low.db) / (high.db - low.db));
    }
    return vuMarks.back().position;
}

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
    constexpr float start = -.70f * pi, end = -.30f * pi;
    constexpr float red = std::lerp(start, end, vuRedPosition);
    const bool overload = level > .84f;
    const Color ink = ceramic ? color(0x2c2720) : color(0xf5e4c8);
    const Color soft = ceramic ? alpha(ink, .55f) : color(0xffe4b9, 140);

    const auto tick = [&](float angle, float radius, float from, float to, Color c, float width) {
        const float dx = std::cos(angle), dy = std::sin(angle);
        d.line(cx + dx * (radius + from), cy + dy * (radius + from),
               cx + dx * (radius + to), cy + dy * (radius + to), c, width);
    };
    const auto markLabels = [&](float radius, Color normal) {
        for (const auto& mark : vuMarks) {
            const float angle = std::lerp(start, end, mark.position);
            const float xx = cx + std::cos(angle) * (radius - 44);
            const float yy = cy + std::sin(angle) * (radius - 44);
            d.label(mark.label, {xx - 36, yy - 15, 72, 30}, Size::MeterMark,
                    mark.position >= vuRedPosition ? p.danger : normal, Face::Mono, Align::Center, true);
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
            const bool hot = i / static_cast<float>(segments) >= vuRedPosition;
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
        tick(std::lerp(start, end, position), radius, -10, 5, position >= vuRedPosition ? p.danger : soft, 2);
    }
    for (const auto& mark : vuMarks) {
        const Color tint = mark.position >= vuRedPosition ? p.danger : ink;
        tick(std::lerp(start, end, mark.position), radius, -16, 12, tint, 3);
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

void fft(std::span<std::complex<float>> bins, const FftRoots& roots)
{
    for (size_t i = 1, reversed = 0; i < bins.size(); ++i) {
        size_t bit = bins.size() / 2;
        while (reversed & bit) {
            reversed ^= bit;
            bit /= 2;
        }
        reversed ^= bit;
        if (i < reversed)
            std::swap(bins[i], bins[reversed]);
    }

    for (size_t length = 2; length <= bins.size(); length *= 2) {
        const size_t half = length / 2;
        const size_t stride = spectrumCapacity / length;
        for (size_t block = 0; block < bins.size(); block += length) {
            for (size_t j = 0; j < half; ++j) {
                const auto even = bins[block + j];
                const auto odd = bins[block + j + half] * roots[j * stride];
                bins[block + j] = even + odd;
                bins[block + j + half] = even - odd;
            }
        }
    }
}

SpectrumBands measureSpectrum(MediaPlayback::SpectrumInput input)
{
    SpectrumBands power{};
    const size_t length = input.samples.size();
    if (length < 4)
        return power;

    static const auto roots = [] {
        FftRoots values{};
        for (size_t i = 0; i < values.size(); ++i) {
            const float angle = -2 * pi * i / spectrumCapacity;
            values[i] = {std::cos(angle), std::sin(angle)};
        }
        return values;
    }();
    static const auto edges = [] {
        constexpr float low = 40.f, high = 16000.f;
        std::array<float, SpectrumBands{}.size() + 1> frequencies{};
        for (size_t i = 0; i < frequencies.size(); ++i)
            frequencies[i] = low * std::pow(high / low, static_cast<float>(i) / (frequencies.size() - 1));
        return frequencies;
    }();
    std::array<std::complex<float>, spectrumCapacity> storage;
    const auto bins = std::span(storage).first(length);
    const size_t stride = spectrumCapacity / length;
    for (size_t i = 0; i < length; ++i) {
        const float window = .5f * (1.f - roots[std::min(i, length - i) * stride].real());
        const auto [left, right] = input.samples[i];
        bins[i] = std::complex<float>{std::isfinite(left) ? left : 0.f, std::isfinite(right) ? right : 0.f} * window;
    }
    fft(bins, roots);

    // Packing L and R into the real and imaginary parts lets the mirrored bins
    // recover their average power. The periodic Hann window has energy 3N/8.
    const float scale = .5f / (.375f * length * length);
    const float binWidth = static_cast<float>(input.rate) / length;
    const int nyquist = static_cast<int>(length / 2);
    for (size_t band = 0; band < power.size(); ++band) {
        const float low = edges[band] / binWidth;
        const float high = std::min(edges[band + 1] / binWidth, static_cast<float>(nyquist));
        const int first = std::max(1, static_cast<int>(std::floor(low + .5f)));
        const int last = std::min(nyquist, static_cast<int>(std::ceil(high + .5f)) - 1);
        for (int bin = first; bin <= last; ++bin) {
            const float overlap = std::max(0.f, std::min(high, bin + .5f) - std::max(low, bin - .5f));
            power[band] += overlap * (std::norm(bins[bin]) + std::norm(bins[length - bin]));
        }
        power[band] *= scale;
    }
    return power;
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

void Meter::update(const Player& player, double dt, bool analyzeSpectrum)
{
    const auto* track = player.track();
    const bool local = track && !track->path.empty();
    const float powerResponse = static_cast<float>(-std::expm1(-dt / .10));
    if (!local)
        mediaPower = {};
    std::array<float, 2> levels{};
    for (size_t i = 0; i < wave.size(); ++i) {
        const double time = std::max(0.0, player.seconds + (static_cast<double>(i) - 256) / 8192);
        const auto sample = player.sample(time);
        wave[i] = (sample[0] + sample[1]) * .5f;
        for (int channel = 0; channel < 2; ++channel)
            levels[channel] += sample[channel] * sample[channel];
    }
    for (int channel = 0; channel < 2; ++channel) {
        const float power = levels[channel] / wave.size();
        if (local) {
            // Leave playback headroom with 0 VU at -6 dBFS RMS.
            constexpr float referenceDbfs = -6.f;
            mediaPower[channel] = std::lerp(mediaPower[channel], power, powerResponse);
            const float dbfs = 10 * std::log10(std::max(mediaPower[channel], 1.e-8f));
            needle[channel] = vuPosition(dbfs - referenceDbfs);
        } else {
            const float rms = std::sqrt(power);
            const float db = rms > .0001f ? std::max(-60.f, 20 * std::log10(rms * 2.6f)) : -60.f;
            const float level = std::clamp((db + 48) / 48, 0.f, 1.f);
            const float response = 1 - std::pow(level > needle[channel] ? .65f : .9f, static_cast<float>(dt * 60));
            needle[channel] = std::lerp(needle[channel], level, response);
        }
        peak[channel] = std::max(needle[channel], peak[channel] - static_cast<float>(.48 * dt));
    }

    if (!analyzeSpectrum)
        return;

    std::array<MediaPlayback::Sample, spectrumCapacity> samples;
    const auto power = measureSpectrum(player.readSpectrum(samples));
    const float attack = static_cast<float>(-std::expm1(-dt / .025));
    const float release = static_cast<float>(-std::expm1(-dt / .12));
    for (size_t i = 0; i < band.size(); ++i) {
        constexpr float floorDbfs = -60.f;
        const float dbfs = 10 * std::log10(std::max(power[i], 1.e-12f));
        const float level = std::clamp((dbfs - floorDbfs) / -floorDbfs, 0.f, 1.f);
        band[i] = std::lerp(band[i], level, level > band[i] ? attack : release);
        bandPeak[i] = std::max(band[i], bandPeak[i] - static_cast<float>(.90 * dt));
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
            if (!d.vfd() && track->path.empty())
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
                   int batteryPercent, bool wifiConnected, bool bluetoothConnected, std::optional<float> syncPhase,
                   float progressSlide, float stageCentering)
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
    const float syncX = bluetoothX - gap - iconDim;
    const float rightIcons = syncPhase ? syncX : bluetoothX;
    const float starX = rightIcons - gap - iconDim;
    if (track) {
        const bool starred = player.stars[player.current];
        const float formatRight = (starred ? starX : rightIcons) - gap;
        const auto codec = track->format.substr(0, track->format.find(' '));
        d.label(codec, {formatX, 0, formatRight - formatX, 62},
                statusSize, p.dim, Face::Mono, Align::Left, false, true, statusWeight);
        if (starred)
            d.icon("star-filled", {starX, iconY, iconDim, iconDim}, p.dim);
    }
    if (syncPhase)
        d.icon("arrow-repeat", {syncX, iconY, iconDim, iconDim}, p.dim, 0, *syncPhase * 360.f);
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

void drawWaveform(Draw& d, const Player& player, const std::optional<MediaWaveform>& decoded,
                  Rect area, float reveal, float loadReveal)
{
    const auto& p = d.p();
    d.frame(area, d.vfd() ? 2 : 10, p.bg, p.border);
    if (reveal <= 0.f)
        return;

    const Rect r = inset(area, 2);
    const float center = r.y + r.h / 2;
    const auto* track = player.track();
    const bool local = track && !track->path.empty();
    const bool flat = local && (!decoded || std::ranges::none_of(*decoded, [](float level) { return level > 0.f; }));
    const float barsReveal = flat ? 0.f : std::min(reveal, loadReveal);
    const float played = r.w * player.progress();
    const float progressX = r.x + played;
    constexpr float playheadWidth = 5.f;
    const float playhead = std::clamp(progressX - playheadWidth / 2, r.x, r.x + r.w - playheadWidth);
    const auto paintBaseline = [&] {
        const float opacity = flat ? 1.f : 1.f - loadReveal;
        if (opacity <= 0.f)
            return;
        const float h = 6.f * reveal;
        d.round({r.x, center - h / 2, r.w, h}, 2, alpha(p.border, opacity));
        if (played >= 1.f)
            d.round({r.x, center - h / 2, played, h}, 2, alpha(p.dim, opacity));
    };
    const auto wave = local ? decoded.value_or(MediaWaveform{}) : waveform(track);
    const auto displayLevel = [local](float level) {
        if (!local)
            return level;
        // Preserve quiet passages and gradually lift louder passages into the available headroom.
        constexpr float quietLevel = .12f, fullLiftLevel = .55f;
        const float t = std::clamp((level - quietLevel) / (fullLiftLevel - quietLevel), 0.f, 1.f);
        const float lift = t * t * (3.f - 2.f * t);
        return std::lerp(level, 1.f, .65f * lift);
    };
    const auto paintPlayed = [&](const auto& paint) {
        if (played < 1.f)
            return;
        d.clip({r.x, r.y, played, r.h});
        paint(p.dim, progressX);
        d.unclip();
    };
    if (d.vfd()) {
        const int columns = static_cast<int>(r.w / 8), rows = static_cast<int>(r.h / 8);
        const auto firstVisibleRow = [rows](float fraction) {
            return std::min((rows - 1) / 2, static_cast<int>(std::ceil((1 - fraction) * rows / 2)));
        };
        const int firstRow = firstVisibleRow(reveal);
        const int firstBarRow = firstVisibleRow(barsReveal);
        const float middle = (rows - 1) / 2.f;
        const Color background = mix(p.bg, p.wave, .45f);
        for (int row = firstRow; row < rows - firstRow; ++row)
            for (int column = 0; column < columns; ++column)
                d.rect({r.x + column * 8 + 1, r.y + row * 8 + 1, 6, 6}, background);
        if (track) {
            paintBaseline();
            const auto paint = [&](Color ink, float right) {
                for (int column = 0; column < columns; ++column) {
                    const float x = r.x + column * 8 + 1;
                    if (x >= right)
                        break;
                    const int source = column * static_cast<int>(wave.size()) / columns;
                    const float span = std::max(1.f, std::round(displayLevel(wave[source]) * rows * .46f));
                    for (int row = firstBarRow; row < rows - firstBarRow; ++row)
                        if (std::abs(row - middle) <= span)
                            d.rect({x, r.y + row * 8 + 1, 6, 6}, ink);
                }
            };
            if (barsReveal > 0.f) {
                paint(mix(p.wave, p.muted, .45f), r.x + r.w);
                paintPlayed(paint);
            }
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
            const float a = wave[i * 2], b = wave[i * 2 + 1];
            const float level = local ? std::sqrt((a * a + b * b) * .5f) : a;
            const float amplitude = std::max(local ? 4.f : 12.f, displayLevel(level) * (r.h - 20)) * barsReveal;
            d.round({x, center - amplitude / 2, std::max(5.f, pitch - 4), amplitude}, 3, ink);
        }
    };
    paintBaseline();
    if (barsReveal > 0.f) {
        paint(p.wave, r.x + r.w);
        paintPlayed(paint);
    }
    const float playheadH = (r.h - 8) * reveal;
    d.rect({playhead, center - playheadH / 2, playheadWidth, playheadH}, p.text);
}

#include "draw.hpp"
#include "design_assets.inc"

#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

Color color(unsigned hex, Uint8 a)
{
    return {static_cast<Uint8>((hex >> 16) & 255), static_cast<Uint8>((hex >> 8) & 255),
            static_cast<Uint8>(hex & 255), a};
}

Color color(std::string_view hex)
{
    unsigned value = 0;
    std::from_chars(hex.data() + 1, hex.data() + hex.size(), value, 16);
    return color(value);
}

Color alpha(Color c, float opacity)
{
    c.a = static_cast<Uint8>(std::clamp(opacity, 0.f, 1.f) * c.a);
    return c;
}

Color mix(Color lhs, Color rhs, float weight)
{
    const auto lerp = [weight](Uint8 a, Uint8 b) { return static_cast<Uint8>(std::lerp(a, b, weight)); };
    return {lerp(lhs.r, rhs.r), lerp(lhs.g, rhs.g), lerp(lhs.b, rhs.b), lerp(lhs.a, rhs.a)};
}

Rect inset(Rect r, float size) { return {r.x + size, r.y + size, r.w - size * 2, r.h - size * 2}; }
bool inside(Rect r, float x, float y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
float even(float v) { return std::round(v / 2) * 2; }
float smooth(float t) { return 1 - std::pow(1 - std::clamp(t, 0.f, 1.f), 3.f); }

std::array<AccentPreset, 4> accentPresets(Theme theme)
{
    switch (theme) {
    case Theme::Obsidian:
        return {{{ "Amber", "アンバー", color(0xff7a1a) }, { "Cyan", "シアン", color(0x38bdf8) },
                 { "Mint", "ミント", color(0x2dd4a0) }, { "Coral", "コーラル", color(0xff5c7c) }}};
    case Theme::Ceramic:
        return {{{ "Vermilion", "朱色", color(0xe6481e) }, { "Cobalt", "コバルト", color(0x2563eb) },
                 { "Forest", "フォレスト", color(0x15803d) }, { "Plum", "プラム", color(0x7e22ce) }}};
    case Theme::Vfd:
        return {{{ "Aqua", "アクア", color(0x38f2c6) }, { "Amber", "アンバー", color(0xffb020) },
                 { "Green", "グリーン", color(0x39e868) }, { "Ice", "アイス", color(0x5ce1ff) }}};
    }
    return {};
}

const Palette& colors(Theme theme, int accent)
{
    const int idx = std::clamp(accent, 0, 3);
    static const Palette obsidian[4] = {
        {color(0x0c0d10), color(0x15171d), color(0x1c1f27), color(0x252934), color(0x2c313d),
         color(0x414859), color(0xf5f2ea), color(0xaeb4c2), color(0x767d8e), color(0xff7a1a),
         color(0xff7a1a, 46), color(0x120b05), color(0xff4757), color(0x171410), color(0x382a1a), color(0x343948)},
        {color(0x0c0d10), color(0x15171d), color(0x1c1f27), color(0x252934), color(0x2c313d),
         color(0x414859), color(0xf5f2ea), color(0xaeb4c2), color(0x767d8e), color(0x38bdf8),
         color(0x38bdf8, 46), color(0x06131c), color(0xff4757), color(0x171410), color(0x382a1a), color(0x343948)},
        {color(0x0c0d10), color(0x15171d), color(0x1c1f27), color(0x252934), color(0x2c313d),
         color(0x414859), color(0xf5f2ea), color(0xaeb4c2), color(0x767d8e), color(0x2dd4a0),
         color(0x2dd4a0, 46), color(0x051610), color(0xff4757), color(0x171410), color(0x382a1a), color(0x343948)},
        {color(0x0c0d10), color(0x15171d), color(0x1c1f27), color(0x252934), color(0x2c313d),
         color(0x414859), color(0xf5f2ea), color(0xaeb4c2), color(0x767d8e), color(0xff5c7c),
         color(0xff5c7c, 46), color(0x1a060b), color(0xff4757), color(0x171410), color(0x382a1a), color(0x343948)},
    };
    static const Palette ceramic[4] = {
        {color(0xeae5dc), color(0xdfd9cd), color(0xf7f4ed), color(0xffffff), color(0xccc4b5),
         color(0xaba291), color(0x16171b), color(0x494c55), color(0x73767f), color(0xe6481e),
         color(0xe6481e, 36), color(0xffffff), color(0xd92d3a), color(0xf4eedf), color(0xfff9ea), color(0xc3bbb0)},
        {color(0xeae5dc), color(0xdfd9cd), color(0xf7f4ed), color(0xffffff), color(0xccc4b5),
         color(0xaba291), color(0x16171b), color(0x494c55), color(0x73767f), color(0x2563eb),
         color(0x2563eb, 36), color(0xffffff), color(0xd92d3a), color(0xf4eedf), color(0xfff9ea), color(0xc3bbb0)},
        {color(0xeae5dc), color(0xdfd9cd), color(0xf7f4ed), color(0xffffff), color(0xccc4b5),
         color(0xaba291), color(0x16171b), color(0x494c55), color(0x73767f), color(0x15803d),
         color(0x15803d, 36), color(0xffffff), color(0xd92d3a), color(0xf4eedf), color(0xfff9ea), color(0xc3bbb0)},
        {color(0xeae5dc), color(0xdfd9cd), color(0xf7f4ed), color(0xffffff), color(0xccc4b5),
         color(0xaba291), color(0x16171b), color(0x494c55), color(0x73767f), color(0x7e22ce),
         color(0x7e22ce, 36), color(0xffffff), color(0xd92d3a), color(0xf4eedf), color(0xfff9ea), color(0xc3bbb0)},
    };
    static const Palette vfd[4] = {
        {color(0x03080a), color(0x071115), color(0x09171c), color(0x0e242b), color(0x133540),
         color(0x1f5563), color(0x72ffe0), color(0x36b8a2), color(0x1e6b62), color(0x38f2c6),
         color(0x38f2c6, 41), color(0x03120f), color(0xff4a5e), color(0x040e12), color(0x38f2c6), color(0x0f2c33)},
        {color(0x090602), color(0x120c04), color(0x181005), color(0x241808), color(0x3d290a),
         color(0x664512), color(0xffd97d), color(0xcc8e28), color(0x7a5314), color(0xffb020),
         color(0xffb020, 44), color(0x170e02), color(0xff4a5e), color(0x0f0a03), color(0xffb020), color(0x2e1f08)},
        {color(0x030905), color(0x061209), color(0x08180d), color(0x0d2614), color(0x113b1e),
         color(0x1d6132), color(0x8affaa), color(0x32b85b), color(0x1b6934), color(0x39e868),
         color(0x39e868, 41), color(0x031408), color(0xff4a5e), color(0x041007), color(0x39e868), color(0x0d2e17)},
        {color(0x03080b), color(0x061016), color(0x08161e), color(0x0d2330), color(0x123542),
         color(0x1e576b), color(0xb3f2ff), color(0x43a8c7), color(0x226075), color(0x5ce1ff),
         color(0x5ce1ff, 41), color(0x031117), color(0xff4a5e), color(0x040d13), color(0x5ce1ff), color(0x0e2a36)},
    };
    if (theme == Theme::Ceramic) return ceramic[idx];
    if (theme == Theme::Vfd) return vfd[idx];
    return obsidian[idx];
}

Draw::Draw(SDL_Renderer* target, std::filesystem::path directory)
    : renderer(target), fontPath(std::move(directory))
{
    const auto limit = SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),
                                             SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 0);
    if (limit > 0 && limit < maxLabelTextureWidth)
        maxLabelTextureWidth = static_cast<int>(limit);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

Draw::~Draw()
{
    for (const auto& entry : artCache)
        SDL_DestroyTexture(entry.second);
    for (auto* texture : vuDials)
        if (texture)
            SDL_DestroyTexture(texture);
    for (auto [key, texture] : icons)
        SDL_DestroyTexture(texture);
    for (auto [key, value] : fonts)
        if (value)
            TTF_CloseFont(value);
    for (auto [key, value] : fallbackFonts)
        if (value)
            TTF_CloseFont(value);
}

void Draw::paint(Color c) { SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a); }
void Draw::clear(Color c) { paint(c); SDL_RenderClear(renderer); }

void Draw::rect(Rect r, Color c)
{
    if (r.w > 0 && r.h > 0 && c.a) {
        paint(c);
        SDL_RenderFillRect(renderer, &r);
    }
}

namespace {
SDL_FColor vectorColor(Color c) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f}; }
constexpr float pi = std::numbers::pi_v<float>;

void fan(SDL_Renderer* renderer, SDL_FPoint center, const std::vector<SDL_FPoint>& edge, Color c)
{
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(edge.size() + 1);
    indices.reserve(edge.size() * 3);
    vertices.push_back({center, vectorColor(c), {}});
    for (size_t i = 0; i < edge.size(); ++i) {
        vertices.push_back({edge[i], vectorColor(c), {}});
        indices.insert(indices.end(), {0, static_cast<int>(i + 1), static_cast<int>((i + 1) % edge.size() + 1)});
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}
}

void Draw::round(Rect r, float radius, Color c)
{
    if (r.w <= 0 || r.h <= 0 || !c.a)
        return;
    if (radius <= 2) {
        rect(r, c);
        return;
    }
    radius = std::min({r.w / 2, r.h / 2, radius});
    const SDL_FPoint centers[] = {{r.x + r.w - radius, r.y + radius}, {r.x + r.w - radius, r.y + r.h - radius},
                                  {r.x + radius, r.y + r.h - radius}, {r.x + radius, r.y + radius}};
    std::vector<SDL_FPoint> points;
    points.reserve(28);
    for (int corner = 0; corner < 4; ++corner)
        for (int i = 0; i <= 6; ++i) {
            const float angle = (corner - 1 + i / 6.f) * pi / 2;
            points.push_back({centers[corner].x + std::cos(angle) * radius,
                              centers[corner].y + std::sin(angle) * radius});
        }
    fan(renderer, {r.x + r.w / 2, r.y + r.h / 2}, points, c);
}

void Draw::strokeRound(Rect r, float radius, Color c, float edge)
{
    if (r.w <= 0 || r.h <= 0 || edge <= 0 || !c.a)
        return;
    if (edge * 2 >= r.w || edge * 2 >= r.h) {
        round(r, radius, c);
        return;
    }
    if (radius <= 2) {
        rect({r.x, r.y, r.w, edge}, c);
        rect({r.x, r.y + r.h - edge, r.w, edge}, c);
        rect({r.x, r.y + edge, edge, r.h - edge * 2}, c);
        rect({r.x + r.w - edge, r.y + edge, edge, r.h - edge * 2}, c);
        return;
    }
    const float outerR = std::min({r.w / 2, r.h / 2, radius});
    const Rect inner = inset(r, edge);
    const float innerR = std::min({inner.w / 2, inner.h / 2, std::max(0.f, outerR - edge)});
    const SDL_FPoint outerCenters[] = {
        {r.x + r.w - outerR, r.y + outerR}, {r.x + r.w - outerR, r.y + r.h - outerR},
        {r.x + outerR, r.y + r.h - outerR}, {r.x + outerR, r.y + outerR},
    };
    const SDL_FPoint innerCenters[] = {
        {inner.x + inner.w - innerR, inner.y + innerR}, {inner.x + inner.w - innerR, inner.y + inner.h - innerR},
        {inner.x + innerR, inner.y + inner.h - innerR}, {inner.x + innerR, inner.y + innerR},
    };
    const auto tint = vectorColor(c);
    constexpr int segs = 6;
    constexpr int ringPts = 4 * (segs + 1);
    SDL_Vertex vertices[ringPts * 2];
    int indices[ringPts * 6];
    int idx = 0;
    for (int corner = 0; corner < 4; ++corner) {
        for (int i = 0; i <= segs; ++i) {
            const float angle = (corner - 1 + i / static_cast<float>(segs)) * pi / 2;
            const float ca = std::cos(angle), sa = std::sin(angle);
            vertices[2 * idx] = {{outerCenters[corner].x + ca * outerR, outerCenters[corner].y + sa * outerR}, tint, {}};
            vertices[2 * idx + 1] = {{innerCenters[corner].x + ca * innerR, innerCenters[corner].y + sa * innerR}, tint, {}};
            const int next = (idx + 1) % ringPts;
            const int a = 2 * idx, b = 2 * next;
            indices[6 * idx + 0] = a;
            indices[6 * idx + 1] = a + 1;
            indices[6 * idx + 2] = b;
            indices[6 * idx + 3] = a + 1;
            indices[6 * idx + 4] = b + 1;
            indices[6 * idx + 5] = b;
            ++idx;
        }
    }
    SDL_RenderGeometry(renderer, nullptr, vertices, ringPts * 2, indices, ringPts * 6);
}

void Draw::frame(Rect r, float radius, Color fill, Color stroke, float edge)
{
    if (fill.a)
        round(inset(r, edge), std::max(0.f, radius - edge), fill);
    if (stroke.a && edge > 0)
        strokeRound(r, radius, stroke, edge);
}

void Draw::line(float x, float y, float x2, float y2, Color c, float width)
{
    const float len = std::hypot(x2 - x, y2 - y);
    if (len == 0)
        return;
    const float dx = (y - y2) / len * width / 2;
    const float dy = (x2 - x) / len * width / 2;
    const auto tint = vectorColor(c);
    const SDL_Vertex vertices[] = {{{x + dx, y + dy}, tint, {}}, {{x - dx, y - dy}, tint, {}},
                                   {{x2 - dx, y2 - dy}, tint, {}}, {{x2 + dx, y2 + dy}, tint, {}}};
    const int triangles[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, triangles, 6);
}

void Draw::circle(float x, float y, float radius, Color c)
{
    if (radius <= 0)
        return;
    const int sides = std::clamp(static_cast<int>(radius), 14, 92);
    std::vector<SDL_FPoint> points;
    points.reserve(sides);
    for (int i = 0; i < sides; ++i) {
        const float angle = i * 2 * pi / sides;
        points.push_back({x + std::cos(angle) * radius, y + std::sin(angle) * radius});
    }
    fan(renderer, {x, y}, points, c);
}

void Draw::ring(float x, float y, float radius, float width, Color c)
{
    const auto tint = vectorColor(c);
    SDL_Vertex vertices[194];
    int indices[576];
    for (int i = 0; i <= 96; ++i) {
        const float angle = i * 2 * pi / 96;
        const SDL_FPoint normal{std::cos(angle), std::sin(angle)};
        const float outer = radius + width / 2, inner = std::max(0.f, radius - width / 2);
        vertices[2 * i] = {{x + normal.x * outer, y + normal.y * outer}, tint, {}};
        vertices[2 * i + 1] = {{x + normal.x * inner, y + normal.y * inner}, tint, {}};
        if (i < 96) {
            const int a = 2 * i;
            const int tri[] = {a, a + 1, a + 2, a + 1, a + 3, a + 2};
            std::copy(std::begin(tri), std::end(tri), indices + 6 * i);
        }
    }
    SDL_RenderGeometry(renderer, nullptr, vertices, 194, indices, 576);
}

void Draw::glow(float x, float y, float radius, Color lit, Color outer)
{
    constexpr int sides = 72;
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.push_back({{x, y}, vectorColor(lit), {}});
    for (float fraction : {.48f, 1.f}) {
        const auto c = vectorColor(fraction < 1 ? outer : alpha(outer, 0));
        for (int i = 0; i < sides; ++i) {
            const float angle = i * 2 * pi / sides;
            vertices.push_back({{x + std::cos(angle) * radius * fraction,
                                 y + std::sin(angle) * radius * fraction}, c, {}});
        }
    }
    for (int i = 0; i < sides; ++i) {
        const int a = 1 + i, b = 1 + (i + 1) % sides;
        indices.insert(indices.end(), {0, a, b, a, a + sides, b, b, a + sides, b + sides});
    }
    SDL_RenderGeometry(renderer, nullptr, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void Draw::gradient(Rect r, Color a, Color b, bool horizontal)
{
    const SDL_FColor x = vectorColor(a), y = vectorColor(b);
    const SDL_Vertex vertices[] = {{{r.x, r.y}, x, {}}, {{r.x + r.w, r.y}, horizontal ? y : x, {}},
                                   {{r.x + r.w, r.y + r.h}, y, {}}, {{r.x, r.y + r.h}, horizontal ? x : y, {}}};
    const int triangles[] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, triangles, 6);
}

void Draw::clip(Rect r)
{
    const SDL_Rect area{static_cast<int>(r.x), static_cast<int>(r.y),
                        std::max(0, static_cast<int>(r.w)), std::max(0, static_cast<int>(r.h))};
    SDL_SetRenderClipRect(renderer, &area);
}
void Draw::unclip() { SDL_SetRenderClipRect(renderer, nullptr); }

TTF_Font* Draw::font(Face type, Size size, int weight)
{
    const auto key = std::tuple(theme, type, size, vfd() ? 400 : weight);
    if (const auto found = fonts.find(key); found != fonts.end())
        return found->second;

    constexpr int standard[] = {48, 32, 26, 21, 18, 15, 20};
    constexpr int pixel[] = {48, 32, 24, 20, 16, 16, 20};
    const int index = static_cast<int>(size);
    const char* name = weight >= 700 ? "SpaceGrotesk-Bold.ttf" : weight >= 600 ? "SpaceGrotesk-SemiBold.ttf"
        : weight >= 500 ? "SpaceGrotesk-Medium.ttf" : "SpaceGrotesk-Regular.ttf";
    if (vfd()) {
        if (size == Size::Display || size == Size::Body)
            name = "PixelMplus12-Regular.ttf";
        else if (size == Size::Sub || size == Size::MeterMark)
            name = "PixelMplus10-Regular.ttf";
        else
            name = "DotGothic16-Regular.ttf";
    } else if (type == Face::Mono) {
        name = weight >= 700 ? "IBMPlexMono-Bold.ttf" : weight >= 600 ? "IBMPlexMono-SemiBold.ttf" : "IBMPlexMono-Medium.ttf";
    }
    auto* value = TTF_OpenFont((fontPath / name).string().c_str(), vfd() ? pixel[index] : standard[index]);
    if (!value) {
        SDL_Log("Cannot load font %s: %s", name, SDL_GetError());
    } else {
        const bool isVfd = vfd();
        if (isVfd)
            TTF_SetFontHinting(value, TTF_HINTING_MONO);
        const bool needFallback = !isVfd || std::string_view(name) != "DotGothic16-Regular.ttf";
        if (needFallback) {
            const auto fbKey = std::tuple(isVfd, size);
            auto alternate = fallbackFonts.find(fbKey);
            if (alternate == fallbackFonts.end()) {
                const char* fallback = isVfd ? "DotGothic16-Regular.ttf" : "MPLUS1p-Regular.ttf";
                auto* fb = TTF_OpenFont((fontPath / fallback).string().c_str(), isVfd ? pixel[index] : standard[index]);
                if (fb)
                    TTF_SetFontHinting(fb, isVfd ? TTF_HINTING_MONO : TTF_HINTING_NONE);
                else
                    SDL_Log("Cannot load fallback font %s: %s", fallback, SDL_GetError());
                alternate = fallbackFonts.emplace(fbKey, fb).first;
            }
            if (alternate->second)
                TTF_AddFallbackFont(value, alternate->second);
        }
    }
    fonts.emplace(key, value);
    return value;
}

namespace {
std::string vfdSanitize(std::string_view src)
{
    std::string out;
    out.reserve(src.size());
    for (size_t i = 0; i < src.size();) {
        const unsigned char c0 = static_cast<unsigned char>(src[i]);
        if (c0 == 0xE2 && i + 2 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            const unsigned char c2 = static_cast<unsigned char>(src[i + 2]);
            if ((c1 == 0x80 && (c2 == 0x94 || c2 == 0x93)) || (c1 == 0x88 && c2 == 0x92)) {
                out.push_back('-');
                i += 3;
                continue;
            }
            if (c1 == 0x80 && c2 == 0xA6) {
                out.append("...");
                i += 3;
                continue;
            }
        } else if (c0 == 0xC5 && i + 1 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            if (c1 == 0x8D) { out.push_back('o'); i += 2; continue; }
            if (c1 == 0x8C) { out.push_back('O'); i += 2; continue; }
            if (c1 == 0xAB) { out.push_back('u'); i += 2; continue; }
            if (c1 == 0xAA) { out.push_back('U'); i += 2; continue; }
        } else if (c0 == 0xC3 && i + 1 < src.size()) {
            const unsigned char c1 = static_cast<unsigned char>(src[i + 1]);
            char repl = 0;
            if (c1 == 0xA9 || c1 == 0xA8 || c1 == 0xAA || c1 == 0xAB) repl = 'e';
            else if (c1 == 0xA1 || c1 == 0xA0 || c1 == 0xA4) repl = 'a';
            else if (c1 == 0xB6 || c1 == 0xB3 || c1 == 0xB2) repl = 'o';
            else if (c1 == 0xBC || c1 == 0xBA || c1 == 0xB9) repl = 'u';
            else if (c1 == 0xAD || c1 == 0xAC || c1 == 0xAF) repl = 'i';
            else if (c1 == 0xB1) repl = 'n';
            else if (c1 == 0xA7) repl = 'c';
            if (repl) {
                out.push_back(repl);
                i += 2;
                continue;
            }
        }
        out.push_back(src[i++]);
    }
    return out;
}
} // namespace

float Draw::textWidth(std::string_view text, Size size, Face type, bool strong)
{
    int width = 0;
    if (auto* face = font(type, size, strong ? 700 : 400)) {
        if (vfd()) {
            const std::string clean = vfdSanitize(text);
            TTF_GetStringSize(face, clean.data(), clean.size(), &width, nullptr);
        } else {
            TTF_GetStringSize(face, text.data(), text.size(), &width, nullptr);
        }
    }
    return static_cast<float>(width);
}

bool Draw::appendTextTile(TextImage& image, TTF_Font* face, std::string_view remaining)
{
    if (!face)
        return false;
    // Leave room for glyph overhang when the measured UTF-8 prefix is reshaped.
    const int maxWidth = std::max(1, maxLabelTextureWidth - TTF_GetFontHeight(face));
    size_t length = 0;
    if (!TTF_MeasureString(face, remaining.data(), remaining.size(), maxWidth, nullptr, &length) || !length)
        return false;
    if (length < remaining.size()) {
        // Use a nearby word boundary where possible so kerning is preserved within words.
        const size_t space = remaining.rfind(' ', length - 1);
        if (space != std::string_view::npos && length - space <= 32 && space > 0)
            length = space + 1;
    }
    auto* surface = vfd() ? TTF_RenderText_Solid(face, remaining.data(), length, color(0xffffff))
                          : TTF_RenderText_Blended(face, remaining.data(), length, color(0xffffff));
    if (!surface)
        return false;
    auto* texture = SDL_CreateTextureFromSurface(renderer, surface);
    const float w = static_cast<float>(surface->w), h = static_cast<float>(surface->h);
    SDL_DestroySurface(surface);
    if (!texture)
        return false;
    SDL_SetTextureScaleMode(texture, vfd() ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    image.tiles.push_back({{texture, SDL_DestroyTexture}, image.width, w, h});
    image.width += w;
    image.bytes += length;
    return true;
}

void Draw::label(std::string_view text, Rect r, Size size, Color ink, Face type, Align align, bool strong, bool fade,
                 int weight, float marqueeTime)
{
    if (text.empty() || r.w <= 0 || r.h <= 0)
        return;
    if (!weight)
        weight = strong ? 700 : 400;
    std::string renderedText = vfd() ? vfdSanitize(text) : std::string(text);
    auto key = std::tuple(theme, type, size, vfd() ? 400 : weight, renderedText);
    auto found = labels.find(key);
    if (found == labels.end()) {
        TextImage image;
        if (!appendTextTile(image, font(type, size, weight), renderedText))
            return;
        image.lastUsed = ++labelTick;
        if (labels.size() >= 768) {
            std::vector<std::pair<Uint64, decltype(labels)::iterator>> entries;
            entries.reserve(labels.size());
            for (auto it = labels.begin(); it != labels.end(); ++it)
                entries.emplace_back(it->second.lastUsed, it);
            const size_t evict = 256;
            std::nth_element(entries.begin(), entries.begin() + evict, entries.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            for (size_t i = 0; i < evict; ++i)
                labels.erase(entries[i].second);
        }
        found = labels.emplace(std::move(key), std::move(image)).first;
    } else {
        found->second.lastUsed = ++labelTick;
    }

    auto& image = found->second;
    constexpr float startPause = .65f, endPause = 1.1f, speed = 96.f;
    if (marqueeTime >= startPause && image.bytes < renderedText.size()) {
        auto* face = font(type, size, weight);
        while (image.bytes < renderedText.size()) {
            if (!appendTextTile(image, face, std::string_view(renderedText).substr(image.bytes)))
                break;
        }
    }

    const float w = std::min(image.width, r.w);
    const float overflow = image.width - w;
    float offset = 0.f;
    if (marqueeTime >= startPause && overflow > 0.f) {
        const float phase = std::fmod(marqueeTime, startPause + overflow / speed + endPause);
        offset = std::clamp((phase - startPause) * speed, 0.f, overflow);
        if (vfd())
            offset = std::min(even(offset), overflow);
    }
    const float x = align == Align::Center ? r.x + even((r.w - w) / 2)
                  : align == Align::Right ? r.x + r.w - w : r.x;
    const float leading = fade && offset > 0.f ? std::min(48.f, w / 2) : 0.f;
    const float trailing = fade && (offset < overflow || image.bytes < renderedText.size())
                         ? std::max(leading, w - 48.f) : w;

    for (const auto& tile : image.tiles) {
        const float tileLeft = tile.x - offset, tileRight = tileLeft + tile.w;
        if (tileRight <= 0.f || tileLeft >= w)
            continue;
        auto* texture = tile.texture.get();
        SDL_SetTextureColorMod(texture, ink.r, ink.g, ink.b);
        const float y = r.y + even((r.h - tile.h) / 2);
        const auto part = [&](float from, float to, Uint8 opacity) {
            const float left = std::max(from, tileLeft), right = std::min(to, tileRight);
            if (right <= left)
                return;
            const Rect source{left - tileLeft, 0, right - left, tile.h};
            const Rect target{x + left, y, right - left, tile.h};
            SDL_SetTextureAlphaMod(texture, opacity);
            SDL_RenderTexture(renderer, texture, &source, &target);
        };
        for (float i = 0.f; i < leading; i += 2.f)
            part(i, std::min(i + 2.f, leading), static_cast<Uint8>(ink.a * i / leading));
        part(leading, trailing, ink.a);
        for (float i = trailing; i < w; i += 2.f)
            part(i, std::min(i + 2.f, w), static_cast<Uint8>(ink.a * (w - i) / 48.f));
        SDL_SetTextureAlphaMod(texture, 255);
    }
}

SDL_Texture* Draw::iconTexture(std::string_view name, int pixels)
{
    const auto key = std::tuple(std::string(name), pixels);
    if (const auto found = icons.find(key); found != icons.end())
        return found->second;
    SDL_Surface* surface = nullptr;
    if (pixels) {
        const auto findPixel = [name](int grid) -> const PixelAsset* {
            for (const auto& asset : pixelAssets)
                if (asset.name == name && asset.size == grid)
                    return &asset;
            return nullptr;
        };
        const PixelAsset* found = findPixel(pixels / 2);
        if (!found)
            found = findPixel(16);
        if (!found)
            found = findPixel(24);
        if (!found)
            found = findPixel(8);
        if (found) {
            const int dim = found->size * 2;
            surface = SDL_CreateSurface(dim, dim, SDL_PIXELFORMAT_RGBA32);
            if (surface) {
                SDL_ClearSurface(surface, 0, 0, 0, 0);
                for (int row = 0; row < found->size; ++row)
                    for (int col = 0; col < found->size; ++col)
                        if (found->dots[row * found->size + col] == '#') {
                            const SDL_Rect dot{col * 2, row * 2, 2, 2};
                            SDL_FillSurfaceRect(surface, &dot, SDL_MapSurfaceRGBA(surface, 255, 255, 255, 255));
                        }
            }
        }
    }
    if (!surface) {
        const auto found = std::ranges::find(svgAssets, name, &IconAsset::name);
        if (found == std::end(svgAssets))
            return nullptr;
        if (auto* io = SDL_IOFromConstMem(found->svg.data(), found->svg.size())) {
            surface = IMG_LoadSizedSVG_IO(io, 96, 96);
            SDL_CloseIO(io);
        }
    }
    if (!surface)
        return nullptr;
    auto* value = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (value) {
        SDL_SetTextureScaleMode(value, pixels ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
        icons.emplace(key, value);
    }
    return value;
}

void Draw::icon(std::string_view name, Rect r, Color tint, int pixelSize)
{
    const int pixels = vfd() ? pixelSize : 0;
    auto* value = iconTexture(name, pixels);
    if (!value)
        return;
    if (pixels) {
        r.x += even((r.w - pixels) / 2);
        r.y += even((r.h - pixels) / 2);
        r.w = r.h = static_cast<float>(pixels);
    }
    SDL_SetTextureColorMod(value, tint.r, tint.g, tint.b);
    texture(value, r, tint.a / 255.f);
}

void Draw::art(const Track* track, Rect r, float corner)
{
    if (corner <= 0) {
        artwork(track, r);
        return;
    }
    const bool hero = r.w == 544;
    const int side = hero ? 544 : 256;
    const auto key = track ? std::tuple(track->art, track->colors, Theme::Obsidian, 0, side)
        : std::tuple(std::string_view(), std::array<std::string_view, 3>{}, theme, accentIndex, side);
    auto found = artCache.find(key);
    if (found == artCache.end()) {
        auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, side, side);
        if (!texture) {
            artwork(track, r);
            return;
        }
        auto* prev = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, texture);
        artwork(track, {0, 0, static_cast<float>(side), static_cast<float>(side)});
        SDL_SetRenderTarget(renderer, prev);
        found = artCache.emplace(key, texture).first;
    }
    if (hero) {
        const float borderCorner = vfd() ? 2.f : corner;
        strokeRound(r, borderCorner, p().strong, 2);
        r = inset(r, 2);
        corner = std::max(0.f, borderCorner - 2);
    } else if (vfd()) {
        corner = 2.f;
    }
    if (corner <= 2.f)
        SDL_RenderTexture(renderer, found->second, nullptr, &r);
    else
        roundedTexture(found->second, r, corner);
}

void Draw::roundedTexture(SDL_Texture* value, Rect r, float corner)
{
    const SDL_FPoint corners[] = {{r.w - corner, corner}, {r.w - corner, r.h - corner},
                                  {corner, r.h - corner}, {corner, corner}};
    std::vector<SDL_Vertex> vertices{{{r.x + r.w / 2, r.y + r.h / 2}, {1, 1, 1, 1}, {.5f, .5f}}};
    std::vector<int> indices;
    for (int index = 0; index < 4; ++index) {
        for (int i = 0; i <= 12; ++i) {
            const float angle = (index - 1 + i / 12.f) * pi / 2;
            const float x = corners[index].x + std::cos(angle) * corner;
            const float y = corners[index].y + std::sin(angle) * corner;
            vertices.push_back({{r.x + x, r.y + y}, {1, 1, 1, 1}, {x / r.w, y / r.h}});
        }
    }
    for (int i = 1; i < static_cast<int>(vertices.size()); ++i)
        indices.insert(indices.end(), {0, i, i + 1 < static_cast<int>(vertices.size()) ? i + 1 : 1});
    SDL_RenderGeometry(renderer, value, vertices.data(), static_cast<int>(vertices.size()),
                       indices.data(), static_cast<int>(indices.size()));
}

void Draw::vuDial(Rect area)
{
    const bool ceramic = theme == Theme::Ceramic;
    auto*& dial = vuDials[ceramic ? 1 : 0];
    if (!dial) {
        constexpr int width = 478, height = 540;
        if (auto* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32)) {
            const Color center = ceramic ? color(0xfffdf7) : p().vuGlow;
            const Color base = p().vuBase;
            const Color edge = ceramic ? color(0xddd4c2) : color(0x0e0c0a);
            const float stop = ceramic ? .55f : .42f;
            constexpr float startRadius = 16, rise = width * .72f - startRadius;
            constexpr float startY = height * .92f, travel = height * .52f - startY;
            constexpr float a = travel * travel - rise * rise;
            auto* pixels = static_cast<Uint8*>(surface->pixels);
            for (int y = 0; y < height; ++y) {
                const float py = y + .5f - startY;
                const float b = -2 * (py * travel + startRadius * rise);
                for (int x = 0; x < width; ++x) {
                    const float px = x + .5f - width / 2.f;
                    const float c = px * px + py * py - startRadius * startRadius;
                    const float t = c < 0 ? 0.f :
                        std::clamp((-b - std::sqrt(std::max(0.f, b * b - 4 * a * c))) / (2 * a), 0.f, 1.f);
                    const Color ink = t < stop ? mix(center, base, t / stop)
                                               : mix(base, edge, (t - stop) / (1 - stop));
                    auto* target = pixels + y * surface->pitch + x * 4;
                    target[0] = ink.r;
                    target[1] = ink.g;
                    target[2] = ink.b;
                    target[3] = 255;
                }
            }
            dial = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_DestroySurface(surface);
        }
    }
    round(area, 16, p().strong);
    if (dial)
        roundedTexture(dial, inset(area, 2), 14);
}

void Draw::artwork(const Track* track, Rect r)
{
    const auto x = [&](float pos) { return r.x + r.w * pos; };
    const auto y = [&](float pos) { return r.y + r.h * pos; };
    if (!track) {
        rect(r, p().card);
        circle(x(.5f), y(.5f), r.w * .24f, p().border);
        circle(x(.5f), y(.5f), r.w * .233f, p().card);
        circle(x(.5f), y(.5f), r.w * .062f, p().border);
        circle(x(.5f), y(.5f), r.w * .055f, p().card);
        return;
    }
    const auto a = color(track->colors[0]), b = color(track->colors[1]), c = color(track->colors[2]);
    gradient(r, b, color(0x08090c), true);
    if (track->art == "bauhaus-amber") {
        circle(x(.56f), y(.48f), r.w * .34f, a);
        rect({x(.12f), y(.62f), r.w * .44f, r.h * .24f}, c);
        ring(x(.56f), y(.48f), r.w * .18f, r.w * .048f, b);
        rect({x(.12f), y(.14f), r.w * .14f, r.h * .34f}, b);
    } else if (track->art == "vinyl-sage") {
        for (int groove = 0; groove < 9; ++groove) {
            const float radius = r.w * (.44f - groove * .036f);
            ring(x(.5f), y(.5f), radius, std::max(1.f, r.w * .005f), alpha(color(0xffffff), .14f));
        }
        circle(x(.5f), y(.5f), r.w * .18f, a);
        circle(x(.5f), y(.5f), r.w * .045f, b);
    } else if (track->art == "vfd-grid") {
        for (int i = 1; i < 10; ++i) {
            rect({x(i * .1f), r.y, 2, r.h}, alpha(a, .24f));
            rect({r.x, y(i * .1f), r.w, 2}, alpha(a, .24f));
        }
        for (int i = 0; i < 7; ++i) {
            const float h = r.h * (.2f + std::abs(std::sin(i * .9f)) * .5f);
            rect({x(.14f + i * .108f), y(.84f) - h, r.w * .072f, h}, a);
        }
        rect({x(.14f), y(.15f), r.w * .72f, r.h * .038f}, c);
    } else {
        rect(r, b);
        for (int ix = 0; ix < 6; ++ix)
            for (int iy = 0; iy < 6; ++iy)
                circle(x(.18f + ix * .064f), y(.18f + iy * .064f), r.w * .015f, c);
        circle(x(.68f), y(.66f), r.w * .21f, a);
        rect({x(.14f), y(.78f), r.w * .34f, r.h * .045f}, c);
    }
}

void Draw::texture(SDL_Texture* value, Rect target, float opacity)
{
    SDL_SetTextureAlphaMod(value, alpha(color(0xffffff), opacity).a);
    SDL_RenderTexture(renderer, value, nullptr, &target);
    SDL_SetTextureAlphaMod(value, 255);
}

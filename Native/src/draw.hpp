#pragma once

#include "player.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <filesystem>
#include <map>
#include <memory>
#include <tuple>

using Rect = SDL_FRect;
using Color = SDL_Color;

Color color(unsigned hex, Uint8 alpha = 255);
Color color(std::string_view hex);
Color alpha(Color c, float opacity);
Color mix(Color lhs, Color rhs, float weight);
Rect inset(Rect rect, float size);
bool inside(Rect rect, float x, float y);
float even(float value);
float smooth(float progress);

enum class Theme { Obsidian, Ceramic, Vfd };
enum class Face { Display, Mono };
enum class Size { Display, Title, Body, Sub, Caption, Desktop, MeterMark };
enum class Align { Left, Center, Right };

struct Palette {
    Color bg, elevated, card, raised, border, strong, text, dim, muted, accent, soft,
          accentInk, danger, vuBase, vuGlow, wave;
};
struct AccentPreset {
    std::string_view name;
    std::string_view jaName;
    Color swatch;
};
const Palette& colors(Theme theme, int accent = 0);
std::array<AccentPreset, 4> accentPresets(Theme theme);

class Draw {
public:
    Draw(SDL_Renderer* renderer, std::filesystem::path fonts);
    ~Draw();
    Draw(const Draw&) = delete;
    Draw& operator=(const Draw&) = delete;

    SDL_Renderer* renderer;
    Theme theme = Theme::Obsidian;
    int accentIndex = 0;
    const Palette& p() const { return colors(theme, accentIndex); }
    bool vfd() const { return theme == Theme::Vfd; }

    void clear(Color c);
    void rect(Rect r, Color c);
    void round(Rect r, float radius, Color c);
    void strokeRound(Rect r, float radius, Color c, float edge = 2);
    void frame(Rect r, float radius, Color fill, Color stroke, float edge = 2);
    void line(float x1, float y1, float x2, float y2, Color c, float width = 2);
    void circle(float x, float y, float radius, Color c);
    void ring(float x, float y, float radius, float width, Color c);
    void glow(float x, float y, float radius, Color lit, Color outer);
    void gradient(Rect r, Color a, Color b, bool horizontal = false);
    void clip(Rect r);
    void unclip();
    void label(std::string_view text, Rect r, Size size, Color ink, Face type = Face::Display,
               Align align = Align::Left, bool strong = false, bool fadeOverflow = false, int weight = 0,
               float marqueeTime = -1.f);
    float textWidth(std::string_view text, Size size, Face type = Face::Display, bool strong = false);
    // A zero pixel size uses the SVG at the requested rectangle in every theme.
    void icon(std::string_view name, Rect target, Color tint, int pixelSize = 32);
    void art(const Track* track, Rect target, float corner = 0);
    void vuDial(Rect area);
    void texture(SDL_Texture* texture, Rect target, float opacity = 1);

private:
    struct TextTile {
        std::unique_ptr<SDL_Texture, decltype(&SDL_DestroyTexture)> texture;
        float x, w, h;
    };
    struct TextImage {
        std::vector<TextTile> tiles;
        float width = 0;
        size_t bytes = 0;
        Uint64 lastUsed = 0;
    };
    std::filesystem::path fontPath;
    int maxLabelTextureWidth = 4096;
    std::map<std::tuple<Theme, Face, Size, int>, TTF_Font*> fonts;
    std::map<std::tuple<bool, Size>, TTF_Font*> fallbackFonts;
    std::map<std::tuple<Theme, Face, Size, int, std::string>, TextImage> labels;
    std::map<std::tuple<std::string, int>, SDL_Texture*> icons;
    Uint64 labelTick = 0;
    void paint(Color c);
    TTF_Font* font(Face type, Size size, int weight);
    bool appendTextTile(TextImage& image, TTF_Font* font, std::string_view remaining);
    void artwork(const Track* track, Rect target);
    void roundedTexture(SDL_Texture* texture, Rect area, float corner);
    std::map<std::tuple<std::string_view, std::array<std::string_view, 3>, Theme, int, int>, SDL_Texture*> artCache;
    std::array<SDL_Texture*, 2> vuDials{};
    SDL_Texture* iconTexture(std::string_view name, int pixels);
};

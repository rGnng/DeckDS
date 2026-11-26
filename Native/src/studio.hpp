#pragma once

#include "deck_ui.hpp"

#include <set>

class Studio {
public:
    void render(Draw& draw, DeckUi& ui, SDL_Window* window, SDL_Texture* top, SDL_Texture* bottom,
                float width, float height, float pixelX, float pixelY);
    void mouseButton(DeckUi& ui, Uint8 button, bool down, float x, float y);
    void mouseMotion(DeckUi& ui, float x, float y);
    void wheel(DeckUi& ui, float x, float y, float dx, float dy, SDL_Keymod mods);
    void keyVisual(const DeckUi& ui, SDL_Keycode key, bool down);
    void update(DeckUi& ui, double dt);
    void cancel(DeckUi& ui);

private:
    enum class Kind { Scale, Theme, Key, Stick, Help, Library };
    struct Control { Rect box; Kind kind; int code; };
    enum class Drag { None, Lower, Pan, Control };
    std::vector<Control> controls;
    std::optional<Control> active, releasedCtl;
    std::set<int> heldKeyCodes;
    float releaseFlash = 0;
    Rect workspace{}, sidebar{}, topBox{}, bottomBox{};
    Drag drag = Drag::None;
    float offsetX = 0, offsetY = 0, maxX = 0, maxY = 0, sidebarY = 0;
    float lastX = 0, lastY = 0, stickX = 0, stickY = 0;
    double heldTime = 0, repeatTime = 0;
    PreviewScale lastScale = PreviewScale::Count;

    void drawSidebar(Draw& d, const DeckUi& ui, std::string_view scaleDetail);
    void drawHelp(Draw& d, float width, float height);
    void control(Draw& d, Rect r, Kind kind, int key, std::string_view label,
                 bool selected = false, int radius = 8);
    void moveStick(DeckUi& ui, float x, float y);
    void paneInput(DeckUi& ui, float x, float y, int phase);
};

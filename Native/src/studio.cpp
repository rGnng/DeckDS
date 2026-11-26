#include "studio.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace {
constexpr float railWidth = 304, sidebarContent = 758;
constexpr float paneWidth = 1024, paneHeight = 768;
constexpr float studioPixelsWide = 5120;
constexpr float handheldWidthMM = 87.4f;
constexpr float studioPPI = 218;
constexpr float inchMM = 25.4f;

float studioPanelPoints(SDL_Window* window)
{
    // The reference mode is 5120 physical pixels across 2560 desktop points.
    // A named Studio Display can use a different desktop size when macOS scales it.
    const float panePixels = handheldWidthMM * studioPPI / inchMM;
    const auto display = SDL_GetDisplayForWindow(window);
    const char* name = SDL_GetDisplayName(display);
    SDL_Rect bounds{};
    if (name && std::string_view(name).find("Studio Display") != std::string_view::npos
        && SDL_GetDisplayBounds(display, &bounds)) {
        const auto orientation = SDL_GetCurrentDisplayOrientation(display);
        const bool portrait = orientation == SDL_ORIENTATION_PORTRAIT || orientation == SDL_ORIENTATION_PORTRAIT_FLIPPED;
        return panePixels * (portrait ? bounds.h : bounds.w) / studioPixelsWide;
    }
    return panePixels / 2;
}

float snap(float logical, float density)
{
    return std::round(logical * density) / density;
}

bool direction(SDL_Keycode key)
{
    return key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN ||
           key == SDLK_VOLUMEUP || key == SDLK_VOLUMEDOWN;
}

bool handlesHold(SDL_Keycode key)
{
    return direction(key) || key == SDLK_POWER;
}
}

void Studio::render(Draw& d, DeckUi& ui, SDL_Window* window, SDL_Texture* upper, SDL_Texture* lower,
                    float w, float h, float pixelX, float pixelY)
{
    controls.clear();
    if (lastScale != ui.scaleMode) {
        lastScale = ui.scaleMode;
        offsetX = offsetY = 0;
    }
    d.gradient({0, 0, w, h}, color(0x24262e), color(0x101115));
    workspace = {14, 55, w - railWidth - 42, h - 98};
    sidebar = {w - railWidth - 14, 55, railWidth, std::min(h - 70, sidebarContent)};
    const float fit = std::min({(workspace.w - 16) / 1056, (workspace.h - 10) / 1594, 1.5f});
    float scaleX = fit, scaleY = fit;
    bool nearest = false;
    std::string dimensions;
    switch (ui.scaleMode) {
    case PreviewScale::Fit:
        dimensions = "1024 × 768 source px per panel";
        break;
    case PreviewScale::Integer: {
        const float capacity = fit * std::min(pixelX, pixelY);
        const float factor = std::max(1.f, std::floor(capacity));
        scaleX = factor / pixelX;
        scaleY = factor / pixelY;
        nearest = true;
        dimensions = std::to_string(static_cast<int>(factor)) + "× nearest pixel scale";
        break;
    }
    case PreviewScale::Studio:
        scaleX = scaleY = studioPanelPoints(window) / paneWidth;
        dimensions = "87.4 × 65.5 mm on Studio Display";
        break;
    case PreviewScale::PixelOne:
    case PreviewScale::PixelTwo: {
        const int factor = ui.scaleMode == PreviewScale::PixelOne ? 1 : 2;
        scaleX = factor / pixelX;
        scaleY = factor / pixelY;
        nearest = true;
        dimensions = std::to_string(1024 * factor) + " × " + std::to_string(768 * factor) + " px per panel";
        break;
    }
    case PreviewScale::Count:
        break;
    }
    const float screenW = paneWidth * scaleX, screenH = paneHeight * scaleY;
    const float edgeX = 16 * scaleX, edgeY = 16 * scaleY, hinge = 26 * scaleY;
    const bool sideBySide = ui.scaleMode == PreviewScale::PixelTwo &&
                            screenH * 2 + edgeY * 2 + hinge > workspace.h &&
                            screenW * 2 + 3 * edgeX <= workspace.w && screenH + 2 * edgeY <= workspace.h;
    const float artW = sideBySide ? screenW * 2 + 3 * edgeX : screenW + 2 * edgeX;
    const float artH = sideBySide ? screenH + 2 * edgeY : screenH * 2 + 2 * edgeY + hinge;
    maxX = std::max(0.f, artW - workspace.w);
    maxY = std::max(0.f, artH - workspace.h);
    offsetX = std::clamp(offsetX, 0.f, maxX);
    offsetY = std::clamp(offsetY, 0.f, maxY);
    const float x = workspace.x + std::max(0.f, (workspace.w - artW) / 2) - offsetX;
    const float y = workspace.y + std::max(0.f, (workspace.h - artH) / 2) - offsetY;
    const auto aligned = [&](float x0, float y0) {
        return Rect{nearest ? snap(x0, pixelX) : x0, nearest ? snap(y0, pixelY) : y0, screenW, screenH};
    };
    topBox = aligned(x + edgeX, y + edgeY);
    bottomBox = sideBySide ? aligned(x + screenW + 2 * edgeX, y + edgeY)
                           : aligned(x + edgeX, y + edgeY + screenH + hinge);

    d.clip(workspace);
    d.frame({x, y, artW, artH}, std::min(28.f, 28 * scaleX), color(0x08090c), color(0x373943), 2);
    if (!sideBySide) {
        const float barY = topBox.y + topBox.h + hinge / 2;
        d.round({topBox.x, barY - 3 * scaleY, topBox.w, 6 * scaleY}, 3, color(0x242731));
        d.round({topBox.x + (topBox.w - 180 * scaleX) / 2, barY - scaleY, 180 * scaleX, 2 * scaleY}, 2, color(0x414653));
    }
    SDL_SetTextureScaleMode(upper, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    SDL_SetTextureScaleMode(lower, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
    d.texture(upper, topBox);
    d.texture(lower, bottomBox);
    d.unclip();

    const auto& p = d.p();
    const auto accent = colors(ui.theme).accent;
    d.circle(25, 26, 4, accent);
    d.label("DECK DS", {40, 8, 180, 36}, Size::Sub, p.text, Face::Display, Align::Left, true);
    control(d, {w - 86, 5, 72, 44}, Kind::Help, 0, "Keys", ui.showKeys);
    control(d, {w - 242, 5, 148, 44}, Kind::Library, 0,
            ui.player.largeLibrary ? "2,000 Albums" : "100 Albums", ui.player.largeLibrary);

    const std::string_view caption = ui.displaysOff() ? ui.sleepMessage() :
        maxX > 0 || maxY > 0 ? "Right-drag or Option-scroll to pan" : "RG DS PLUS / 2 × 1024 × 768";
    d.label(caption, {workspace.x, h - 38, workspace.w, 28}, Size::Caption, p.dim, Face::Display, Align::Center);
    drawSidebar(d, ui, dimensions);
    if (ui.showKeys)
        drawHelp(d, w, h);
}

void Studio::control(Draw& d, Rect r, Kind kind, int code, std::string_view label, bool selected, int radius)
{
    controls.push_back({r, kind, code});
    const auto& p = d.p();
    const bool down = active && active->kind == kind && active->code == code;
    const auto normal = kind == Kind::Key ? p.elevated : p.card;
    const auto stroke = down ? p.accent : selected ? p.dim : p.strong;
    if (down)
        r = inset(r, 2);
    d.frame(r, radius, selected || down ? p.raised : normal, stroke);
    d.label(label, inset(r, 2), Size::Desktop, selected || down ? p.text : p.dim, Face::Display, Align::Center, true);
}

void Studio::drawSidebar(Draw& d, const DeckUi& ui, std::string_view scaleDetail)
{
    const auto& p = d.p();
    const float overflow = std::max(0.f, sidebarContent - sidebar.h);
    sidebarY = std::clamp(sidebarY, 0.f, overflow);
    const float x = sidebar.x + 16;
    const float y = sidebar.y - sidebarY;
    d.frame(sidebar, 12, p.bg, p.border);
    d.clip(inset(sidebar, 2));
    const auto heading = [&](std::string_view name, float row) {
        d.label(name, {x, y + row, 272, 25}, Size::Sub, p.text, Face::Display, Align::Left, true);
    };
    const auto scale = [&](float x0, float y0, float width, PreviewScale mode, std::string_view name) {
        control(d, {x + x0, y + y0, width, 44}, Kind::Scale, static_cast<int>(mode), name, ui.scaleMode == mode);
    };

    heading("Display", 14);
    scale(0, 49, 132, PreviewScale::Fit, "Fit");
    scale(140, 49, 132, PreviewScale::Integer, "Integer");
    scale(0, 101, 132, PreviewScale::PixelOne, "1× Pixels");
    scale(140, 101, 132, PreviewScale::PixelTwo, "2× Pixels");
    scale(0, 153, 272, PreviewScale::Studio, "Studio Display 5K / 1:1");
    d.label(scaleDetail, {x, y + 203, 272, 26}, Size::Desktop, p.dim, Face::Display, Align::Center);

    heading("Theme", 238);
    const std::string_view themes[] = {"Obsidian", "Ceramic", "VFD"};
    for (int i = 0; i < 3; ++i)
        control(d, {x + i * 92, y + 270, 88, 44}, Kind::Theme, i, themes[i], static_cast<int>(ui.theme) == i);

    d.rect({x, y + 329, 272, 2}, p.border);
    heading("RG DS Plus Controls", 342);
    const auto button = [&](float x0, float y0, float width, std::string_view label, SDL_Keycode key, int radius = 8) {
        control(d, {x + x0, y + y0, width, 44}, Kind::Key, static_cast<int>(key), label, false, radius);
    };
    button(0, 376, 62, "L2", SDLK_LEFTBRACKET);
    button(70, 376, 62, "L1", SDLK_Q);
    button(140, 376, 62, "R1", SDLK_E);
    button(210, 376, 62, "R2", SDLK_RIGHTBRACKET);

    d.round({x + 44, y + 436, 44, 132}, 10, p.elevated);
    d.round({x, y + 480, 132, 44}, 10, p.elevated);
    button(44, 436, 44, "↑", SDLK_UP);
    button(0, 480, 44, "←", SDLK_LEFT);
    button(88, 480, 44, "→", SDLK_RIGHT);
    button(44, 524, 44, "↓", SDLK_DOWN);
    button(184, 436, 44, "X", SDLK_X, 22);
    button(140, 480, 44, "Y", SDLK_Y, 22);
    button(228, 480, 44, "A", SDLK_RETURN, 22);
    button(184, 524, 44, "B", SDLK_B, 22);

    const Rect stick{x + 22, y + 585, 88, 88};
    controls.push_back({stick, Kind::Stick, 0});
    d.frame(stick, 44, color(0x08090b), p.border);
    d.circle(stick.x + 44 + stickX * 20, stick.y + 44 + stickY * 20, 26, p.strong);
    d.circle(stick.x + 44 + stickX * 20, stick.y + 44 + stickY * 20, 23, p.raised);
    button(126, 583, 68, "Start", SDLK_SPACE);
    button(202, 583, 70, "Select", SDLK_S);
    button(126, 635, 68, "Star", SDLK_G);
    button(202, 635, 70, "Now", SDLK_H);

    button(0, 698, 64, "Vol −", SDLK_VOLUMEDOWN);
    button(70, 698, 64, "Vol +", SDLK_VOLUMEUP);
    button(140, 698, 64, "Power", SDLK_POWER);
    button(210, 698, 62, ui.lid ? "Open" : "Lid", SDLK_L);
    d.unclip();
    if (overflow > 0) {
        const float h = std::max(24.f, (sidebar.h - 10) * sidebar.h / sidebarContent);
        d.round({sidebar.x + sidebar.w - 7, sidebar.y + 5 + (sidebar.h - 10 - h) * sidebarY / overflow, 4, h},
                2, p.strong);
    }
}

void Studio::drawHelp(Draw& d, float width, float height)
{
    const auto& p = d.p();
    d.rect({0, 52, width, height - 52}, color(0x000000, 182));
    const float w = std::min(520.f, width - 28);
    const Rect modal{std::round((width - w) / 2), std::round((height - 436) / 2), w, 436};
    d.frame(modal, 14, p.elevated, p.strong);
    d.label("Keyboard controls", {modal.x + 20, modal.y + 12, w - 40, 48}, Size::Title, p.text, Face::Display, Align::Left, true);
    const std::pair<std::string_view, std::string_view> shortcuts[] = {
        {"Q / E", "L1 / R1 tabs"}, {"1 / 2", "L2 / R2 previous and next"},
        {"Arrows", "D-pad / hold Left or Right to seek"}, {"Enter / Z", "A / select"}, {"X / Backspace", "B / back"},
        {"Space / Tab", "Start / Select"}, {"C / V", "X / Y"}, {"D / H", "Star / Now"},
        {"P / L", "Power / Lid"}, {"/", "Search library"}, {"F2", "Cycle display modes"}
    };
    for (size_t i = 0; i < std::size(shortcuts); ++i) {
        const float rowY = modal.y + 69 + 31 * i;
        d.label(shortcuts[i].first, {modal.x + 20, rowY, 130, 28}, Size::Caption, p.accent, Face::Mono);
        d.label(shortcuts[i].second, {modal.x + 156, rowY, w - 174, 28}, Size::Caption, p.text);
    }
}

void Studio::moveStick(DeckUi& ui, float x, float y)
{
    const auto r = active->box;
    stickX = (x - r.x - r.w / 2) / 32;
    stickY = (y - r.y - r.h / 2) / 32;
    const float length = std::max(1.f, std::hypot(stickX, stickY));
    stickX /= length;
    stickY /= length;
    ui.analog(stickX);
}

void Studio::paneInput(DeckUi& ui, float x, float y, int phase)
{
    ui.pointer((x - bottomBox.x) * paneWidth / bottomBox.w, (y - bottomBox.y) * paneHeight / bottomBox.h, phase);
}

void Studio::mouseButton(DeckUi& ui, Uint8 button, bool down, float x, float y)
{
    const bool panButton = button == SDL_BUTTON_RIGHT || button == SDL_BUTTON_MIDDLE;
    if (down && drag != Drag::None)
        return;
    if (panButton) {
        if (down && !ui.showKeys && inside(workspace, x, y)) {
            drag = Drag::Pan;
            lastX = x;
            lastY = y;
        } else if (!down && drag == Drag::Pan) {
            drag = Drag::None;
        }
        return;
    }
    if (button != SDL_BUTTON_LEFT || drag == Drag::Pan)
        return;
    if (down) {
        const auto match = std::ranges::find_if(controls, [&](const Control& ctl) {
            return inside(ctl.box, x, y) && (ctl.kind == Kind::Help ||
                   (!ui.showKeys && (ctl.kind == Kind::Library || inside(sidebar, x, y))));
        });
        if (match != controls.end()) {
            active = *match;
            drag = Drag::Control;
            heldTime = 0;
            repeatTime = .36;
            if (match->kind == Kind::Key && handlesHold(static_cast<SDL_Keycode>(match->code)))
                ui.key(static_cast<SDL_Keycode>(match->code), true, false, true);
            else if (match->kind == Kind::Stick)
                moveStick(ui, x, y);
        } else if (ui.showKeys) {
            ui.showKeys = false;
        } else if (inside(workspace, x, y) && inside(bottomBox, x, y)) {
            drag = Drag::Lower;
            paneInput(ui, x, y, 0);
        }
        return;
    }

    if (drag == Drag::Lower) {
        paneInput(ui, x, y, 2);
    } else if (active && drag == Drag::Control) {
        if (active->kind == Kind::Key) {
            const auto key = static_cast<SDL_Keycode>(active->code);
            if (handlesHold(key)) ui.key(key, false, false, true);
            else if (inside(active->box, x, y)) ui.key(key, true, false, true);
        } else if (active->kind == Kind::Stick) {
            stickX = stickY = 0;
            ui.analog(0);
        } else if (inside(active->box, x, y)) {
            if (active->kind == Kind::Scale) ui.scaleMode = static_cast<PreviewScale>(active->code);
            if (active->kind == Kind::Theme) ui.theme = static_cast<Theme>(active->code);
            if (active->kind == Kind::Help) ui.showKeys = !ui.showKeys;
            if (active->kind == Kind::Library) ui.toggleLibrary();
        }
        active.reset();
    }
    drag = Drag::None;
}

void Studio::mouseMotion(DeckUi& ui, float x, float y)
{
    if (drag == Drag::Pan) {
        offsetX = std::clamp(offsetX + lastX - x, 0.f, maxX);
        offsetY = std::clamp(offsetY + lastY - y, 0.f, maxY);
        lastX = x;
        lastY = y;
    } else if (drag == Drag::Lower) {
        paneInput(ui, x, y, 1);
    } else if (active && active->kind == Kind::Stick) {
        moveStick(ui, x, y);
    }
}

void Studio::wheel(DeckUi& ui, float x, float y, float dx, float dy, SDL_Keymod mods)
{
    if (ui.showKeys)
        return;
    if (inside(sidebar, x, y)) {
        sidebarY = std::clamp(sidebarY - dy * 38, 0.f, std::max(0.f, sidebarContent - sidebar.h));
    } else if (inside(workspace, x, y)) {
        if (!(mods & SDL_KMOD_ALT) && inside(bottomBox, x, y)) {
            ui.wheel(dy);
        } else {
            if ((mods & SDL_KMOD_SHIFT) && dx == 0) {
                dx = dy;
                dy = 0;
            }
            offsetX = std::clamp(offsetX - dx * 52, 0.f, maxX);
            offsetY = std::clamp(offsetY - dy * 52, 0.f, maxY);
        }
    }
}

void Studio::update(DeckUi& ui, double dt)
{
    if (!active || active->kind != Kind::Key || !direction(static_cast<SDL_Keycode>(active->code)))
        return;
    heldTime += dt;
    if (heldTime >= repeatTime) {
        ui.key(static_cast<SDL_Keycode>(active->code), true, true, true);
        repeatTime += .075;
    }
}

void Studio::cancel(DeckUi& ui)
{
    active.reset();
    drag = Drag::None;
    stickX = stickY = 0;
    ui.cancelInput();
}

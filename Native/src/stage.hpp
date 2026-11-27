#pragma once

#include "draw.hpp"

struct Meter {
    std::array<float, 2> needle{}, peak{};
    std::array<float, 28> band{}, bandPeak{};
    std::array<float, 256> wave{};
    void update(const Player& player, double dt);
};

void drawStage(Draw& draw, const Player& player, const Meter& meter, int stage, float stageCentering);
void drawTopChrome(Draw& draw, const Player& player, float dockTitle, std::string_view clockText,
                   int batteryPercent, bool wifiConnected, bool bluetoothConnected, float progressSlide, float stageCentering);
void drawWaveform(Draw& draw, const Player& player, Rect area, float reveal);

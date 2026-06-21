#pragma once

#include "engine/ui/UIRenderer.hpp"
#include "engine/ui/UIVertex.hpp"

// ─────────────────────────────────────────────
//  Crosshair
//
//  Draws a simple Minecraft-style + crosshair
//  at the center of the screen.
//
//  Usage:
//    Crosshair::Draw(ui, screenW, screenH);
// ─────────────────────────────────────────────

class Crosshair {
public:
    static void Draw(UIRenderer& ui, int screenW, int screenH) {
        static constexpr float THICKNESS = 2.f; // pixels
        static constexpr float ARM = 8.f; // pixels from center to tip

        const float cx = static_cast<float>(screenW) * 0.5f;
        const float cy = static_cast<float>(screenH) * 0.5f;

        const uint32_t color = UIVertex::PackColor(1.f, 1.f, 1.f, 0.85f);

        // Horizontal bar
        ui.DrawQuad(cx - ARM - THICKNESS * 0.5f, cy - THICKNESS * 0.5f,
            ARM * 2.f + THICKNESS, THICKNESS, color);

        // Vertical bar
        ui.DrawQuad(cx - THICKNESS * 0.5f, cy - ARM - THICKNESS * 0.5f,
            THICKNESS, ARM * 2.f + THICKNESS, color);
    }
};
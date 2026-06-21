#pragma once
 
#include <string>
 
class UIRenderer;
class Camera;
class ChunkManager;
class Timer;
class InputManager;
 
// ─────────────────────────────────────────────
//  DebugOverlay
//
//  Minecraft F3-style debug overlay.
//  Toggle with F3.  Rendered entirely through
//  UIRenderer — no extra Vulkan objects needed.
//
//  Left column  — player & world info
//  Right column — renderer & chunk info
//
//  Usage:
//    overlay.HandleInput(window);   // call each frame (key toggle)
//    overlay.Draw(ui, camera, chunks, timer, screenW, screenH);
// ─────────────────────────────────────────────

struct GLFWwindow;

class DebugOverlay {
public:
    DebugOverlay() = default;

    // Edge-detect F3 press each frame
    void HandleInput(const InputManager& input);

    // Draw the overlay (no-op when hidden)
    void Draw(UIRenderer& ui, const Camera& camera, const ChunkManager& chunks, const Timer& timer, int screenW, int screenH);

    bool IsVisible() const { return m_visible; };

private:
    bool m_visible = false;

    // FPS smoothing
    float m_fpsAccum = 0.f;
    int m_fpsFrames = 0;
    float m_smoothFps = 0.f;

    // helpers
    static std::string Vec3Str(float x, float y, float z, int decimals = 2);
    static std::string FormatFloat(float v, int decimals);
};
#include "engine/ui/DebugOverlay.hpp"
#include "engine/core/Logger.hpp"
#include "engine/platform/InputManager.hpp"
#include "engine/ui/UIRenderer.hpp"
#include "engine/ui/UIVertex.hpp"
#include "game/world/Camera.hpp"
#include "game/world/ChunkManager.hpp"
#include "engine/core/Timer.hpp"
 
#include <GLFW/glfw3.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <string>
 
///
/// Helpers
///

std::string DebugOverlay::FormatFloat(float v, int decimals) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimals) << v;
    return ss.str();
}

std::string DebugOverlay::Vec3Str(float x, float y, float z, int decimals) {
    return FormatFloat(x, decimals) + " / "
        + FormatFloat(y, decimals) + " / "
        + FormatFloat(z, decimals);
}

///
/// Input
///

void DebugOverlay::HandleInput(const InputManager& input) {
    if (input.WasActionPressed(InputAction::OpenDebugMenu))
        m_visible = !m_visible;
}

///
/// Draw
///

void DebugOverlay::Draw(UIRenderer& ui, const Camera& camera, const ChunkManager& chunks,
    const Timer& timer, int screenW, int screenH)
{
    // FPS smoothing - average over ~20 frames
    m_fpsAccum += timer.GetDeltaTime();
    m_fpsFrames += 1;
    if (m_fpsFrames >= 20) {
        m_smoothFps = static_cast<float>(m_fpsFrames) / m_fpsAccum;
        m_fpsAccum = 0.f;
        m_fpsFrames = 0;
    }
 
    if (!m_visible) return;

    // Layout constants
    static constexpr int SCALE = 2; // text scale (2 = 16px tall)
    static constexpr float PAD = 4.f; // outer padding
    static constexpr float LINE_H = FONT_CHAR_H * SCALE + 2.f; // 18 px per line
    static constexpr float PANEL_ALPHA = 0.55f;
    static constexpr float COL_W = 220.f;
 
    // Gather data
    const glm::vec3 pos   = camera.GetPosition();
    const glm::vec3 front = camera.GetFront();

    // Chunk coordinate the player is standing in
    const int chunkX = static_cast<int>(std::floor(pos.x)) >> 5; // CHUNK_SIZE(32)
    const int chunkY = static_cast<int>(std::floor(pos.y)) >> 5;
    const int chunkZ = static_cast<int>(std::floor(pos.z)) >> 5;
 
    // Block coord within chunk
    const int blockX = static_cast<int>(std::floor(pos.x)) & 31;
    const int blockZ = static_cast<int>(std::floor(pos.z)) & 31;
 
    // Loaded chunk count
    const int chunkCount = static_cast<int>(chunks.GetChunks().size());
 
    // Count chunks that have a mesh (renderable)
    int meshedChunks = 0;
    for (const auto& [coord, chunk] : chunks.GetChunks())
        if (chunk->HasMesh()) ++meshedChunks;
    
    const float fps = m_smoothFps;
    const float ms = fps > 0.f ? 1000.f / fps : 0.f;
 
    // Facing direction label (Minecraft-style)
    auto facingStr = [](glm::vec3 f) -> const char* {
        const float ax = std::abs(f.x);
        const float az = std::abs(f.z);
        if (ax > az) return f.x > 0.f ? "East  (+X)" : "West  (-X)";
        else         return f.z > 0.f ? "South (+Z)" : "North (-Z)";
    };

    // Build line lists
 
    // Left column
    const std::string leftLines[] = {
        "--- Voxel Engine ---",
        "",
        "FPS:    " + FormatFloat(fps, 0) + " (" + FormatFloat(ms, 2) + " ms)",
        "Time:   " + FormatFloat(timer.GetElapsedTime(), 1) + "s",
        "",
        "--- Player ---",
        "XYZ:    " + Vec3Str(pos.x, pos.y, pos.z, 2),
        "Chunk:  " + std::to_string(chunkX) + " / "
                   + std::to_string(chunkY) + " / "
                   + std::to_string(chunkZ),
        "Block:  " + std::to_string(blockX) + " / "
                   + std::to_string(static_cast<int>(std::floor(pos.y))) + " / "
                   + std::to_string(blockZ),
        "Facing: " + std::string(facingStr(front)),
        "Dir:    " + Vec3Str(front.x, front.y, front.z, 3),
    };
    static constexpr int LEFT_COUNT = static_cast<int>(std::size(leftLines));

    // Right column
    const std::string rightLines[] = {
        "--- Renderer ---",
        "",
        "Chunks loaded:  " + std::to_string(chunkCount),
        "Chunks meshed:  " + std::to_string(meshedChunks),
        "",
        "--- Controls ---",
        "F3      Toggle debug",
        "WASD            Move",
        "Space         Fly up",
        "L-Shift     Fly down",
        "LMB     Break block",
        "RMB     Place block",
        "Esc            Quit",
    };
    static constexpr int RIGHT_COUNT = static_cast<int>(std::size(rightLines));

    // Draw background panels
 
    const uint32_t bgColor = UIVertex::PackColor(0.f, 0.f, 0.f, PANEL_ALPHA);
 
    // Left panel
    size_t longestLeftLine = 0;
    for (const auto& line : leftLines)
        longestLeftLine = std::max(longestLeftLine, line.size());
    const float leftPanelW = std::max(COL_W,
        static_cast<float>(longestLeftLine * FONT_CHAR_W * SCALE) + 8.f);
    const float leftPanelH = LEFT_COUNT * LINE_H + PAD * 2.f;
    ui.DrawQuad(PAD, PAD, leftPanelW, leftPanelH, bgColor);
 
    // Right panel (right-aligned to screen edge)
    // Width is derived from the longest line so right-aligned text
    // never overflows a fixed-width panel, with a sensible minimum.
    size_t longestRightLine = 0;
    for (const auto& line : rightLines)
        longestRightLine = std::max(longestRightLine, line.size());
    const float rightPanelW = std::max(COL_W,
        static_cast<float>(longestRightLine * FONT_CHAR_W * SCALE) + 8.f);
    const float rightPanelH = RIGHT_COUNT * LINE_H + PAD * 2.f;
    const float rightPanelX = static_cast<float>(screenW) - rightPanelW - PAD;
    ui.DrawQuad(rightPanelX, PAD, rightPanelW, rightPanelH, bgColor);

    // Draw text
 
    const uint32_t headerColor  = UIVertex::PackColor(1.00f, 1.00f, 0.40f);  // yellow headers
    const uint32_t normalColor  = UIVertex::PackColor(1.00f, 1.00f, 1.00f);  // white
    const uint32_t sectionColor = UIVertex::PackColor(0.50f, 1.00f, 0.50f);  // green section titles
 
    auto lineColor = [&](const std::string& s, int lineIndex) -> uint32_t {
        if (lineIndex == 0) return headerColor;
        if (s.find("---") != std::string::npos) return sectionColor;
        return normalColor;
    };

    
    // Left column text
    for (int i = 0; i < LEFT_COUNT; ++i) {
        const float tx = PAD + 4.f;
        const float ty = PAD + 2.f + i * LINE_H;
        ui.DrawTextShadowed(leftLines[i], tx, ty, lineColor(leftLines[i], i), SCALE);
    }
 
    // Right column text — right-aligned: each line's text ends flush
    // against the panel's right inner edge, rather than starting at
    // the panel's left edge like the left column does.
    const float rightInnerEdge = rightPanelX + rightPanelW - 4.f;
    for (int i = 0; i < RIGHT_COUNT; ++i) {
        const float textW = static_cast<float>(rightLines[i].size() * FONT_CHAR_W * SCALE);
        const float tx = rightInnerEdge - textW;
        const float ty = PAD + 2.f + i * LINE_H;
        ui.DrawTextShadowed(rightLines[i], tx, ty, lineColor(rightLines[i], i), SCALE);
    }
}
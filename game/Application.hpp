#pragma once

#include "engine/core/Timer.hpp"
#include "engine/platform/InputManager.hpp"
#include "engine/platform/Window.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/TextureAtlas.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/ui/DebugOverlay.hpp"
#include "game/world/Camera.hpp"
#include "game/world/ChunkManager.hpp"
#include <memory>

class Application {
public:
    bool Initialize();
    void Run();
    void Shutdown();

private:
    void Update();
    void Render();
    void HandleBlockInteraction();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkan;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<TextureAtlas>  m_atlas;
    std::unique_ptr<ChunkManager>  m_chunkManager;
    std::unique_ptr<Camera>        m_camera;
    std::unique_ptr<InputManager>  m_input;

    Timer m_timer;
    DebugOverlay m_debugOverlay;
    bool m_running = false;

    // Edge-detection: only act on the press, not every frame held
    // bool m_leftWasDown = false;
    // bool m_rightWasDown = false;

    // How many chunks in XZ to keep loaded around the camera
    static constexpr int VIEW_RADIUS = 6;
};
#pragma once

#include "Timer.hpp"
#include "engine/platform/Window.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include <memory>

class Application {
public:
    bool Initialize();

    void Run();

    void Shutdown();

private:
    void Update();
    void Render();

private:
    std::unique_ptr<Window> m_window;
    std::unique_ptr<VulkanContext> m_vulkan;
    std::unique_ptr<Renderer> m_renderer;

    Timer m_timer;

    bool m_running = false;
};
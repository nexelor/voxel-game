#include "Application.hpp"

#include "Assert.hpp"
#include "Logger.hpp"

bool Application::Initialize() {
    Logger::Log(LogLevel::Info, "Core", "Initializing application");

    m_window = std::make_unique<Window>(1280, 720, "Voxel Game");

    VG_ASSERT(m_window, "Window creation failed");

    m_vulkan = std::make_unique<VulkanContext>(m_window.get());
    m_vulkan->Init();

    m_renderer = std::make_unique<Renderer>(m_vulkan.get(), m_window.get());
    m_renderer->Init();

    m_running = true;

    Logger::Log(LogLevel::Info, "Core", "Initialization complete");

    return true;
}

void Application::Run() {
    Logger::Log(LogLevel::Info, "Core", "Entering main loop");

    while (m_running && !m_window->ShoudClose()) {
        m_window->PollEvent();

        m_timer.Update();

        Update();
        Render();
    }
}

void Application::Shutdown() {
    Logger::Log(LogLevel::Info, "Core", "Shutting down");

    m_renderer.reset();
    m_vulkan.reset();
    m_window.reset();
}

void Application::Update() {}
void Application::Render() {
    m_renderer->DrawFrame();
}
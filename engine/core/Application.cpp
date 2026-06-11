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

    // Atlas (1×1 white — unlocks drawing)
    m_atlas = std::make_unique<TextureAtlas>(m_vulkan.get(), m_renderer->GetCommandPool());
    m_atlas->CreateSolid(255, 255, 255);
    m_atlas->WriteDescriptorSet(m_renderer->GetTextureDescSet());
    m_renderer->BindTextureAtlas(m_renderer->GetTextureDescSet());

    // World
    m_chunkManager = std::make_unique<ChunkManager>(m_vulkan.get(), m_renderer.get());
    m_chunkManager->Init();
    m_chunkManager->FlushDirtyMeshes(
        m_renderer->GetCommandPool(),
        m_renderer->GetTransferQueue()
    );
    m_renderer->SetChunkManager(m_chunkManager.get());

    // Camera
    m_camera = std::make_unique<Camera>(m_window->GetNativeWindow());

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

    // Destroy in reverse construction order.
    // ChunkManager holds VkBuffers → must die before device.
    m_chunkManager.reset();
    m_atlas.reset();
    m_camera.reset();
    m_renderer.reset();
    m_vulkan.reset();
    m_window.reset();
}

void Application::Update() {
    m_camera->Update(m_timer.GetDeltaTime());
 
    const float aspect =
        static_cast<float>(m_window->GetWidth()) /
        static_cast<float>(m_window->GetHeight());
 
    m_renderer->UpdateCamera(m_camera->GetUBO(aspect));
}

void Application::Render() {
    m_renderer->DrawFrame();
}
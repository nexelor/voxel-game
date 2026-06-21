#include "game/Application.hpp"

#include "content/blocks/Blocks.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/InputAction.hpp"
#include "engine/core/Logger.hpp"
#include "engine/ui/Crosshair.hpp"
#include "engine/ui/UIRenderer.hpp"
#include "game/registry/BlockRegistry.hpp"
#include <GLFW/glfw3.h>

bool Application::Initialize() {
    Logger::Log(LogLevel::Info, "Core", "Initializing application");

    m_window = std::make_unique<Window>(1280, 720, "Voxel Game");

    VG_ASSERT(m_window, "Window creation failed");

    m_input = std::make_unique<InputManager>(m_window->GetNativeWindow());

    m_vulkan = std::make_unique<VulkanContext>(m_window.get());
    m_vulkan->Init();

    m_renderer = std::make_unique<Renderer>(m_vulkan.get(), m_window.get());
    m_renderer->Init();

    // Atlas (1×1 white — unlocks drawing)
    m_atlas = std::make_unique<TextureAtlas>(m_vulkan.get(), m_renderer->GetCommandPool());
    m_atlas->CreateSolid(255, 255, 255);
    m_atlas->WriteDescriptorSet(m_renderer->GetTextureDescSet());
    m_renderer->BindTextureAtlas(m_renderer->GetTextureDescSet());

    RegisterAllBlocks(BlockRegistry::Get());

    // World
    m_chunkManager = std::make_unique<ChunkManager>(m_vulkan.get(), m_renderer.get());
    m_chunkManager->Init(m_renderer->GetCommandPool(), m_renderer->GetTransferQueue());
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

    // Block until every in-flight command buffer has finished executing
    // before destroying ANY GPU resource (chunk buffers, atlas, UI buffers).
    // Without this, ChunkManager's destructor below can call DestroyBuffers()
    // on vertex/index buffers that the last submitted command buffer is
    // still using for vkCmdDrawIndexed, which the Vulkan spec forbids and
    // the validation layer (correctly) flags.
    if (m_vulkan) {
        vkDeviceWaitIdle(m_vulkan->GetDevice());
    }

    m_chunkManager.reset();
    m_atlas.reset();
    m_camera.reset();
    m_renderer.reset();
    m_vulkan.reset();
    m_window.reset();
}

void Application::Update() {
    const float dt = m_timer.GetDeltaTime();

    m_input->Update();

    m_camera->Update(dt, *m_input);

    // Push camera matrices into the renderer for this frame
    const float aspect =
        static_cast<float>(m_window->GetWidth()) /
        static_cast<float>(m_window->GetHeight());
    m_renderer->UpdateCamera(m_camera->GetUBO(aspect));

    // Block interaction (break / place)
    HandleBlockInteraction();

    // F3 toggle
    m_debugOverlay.HandleInput(*m_input);

    // Stream-in new chunks and rebuild dirty meshes.
    // The GPU wait inside FlushDirty is fine here since the whole
    // chunk system is still single-threaded.
    m_chunkManager->Update(m_camera->GetPosition(), VIEW_RADIUS,
        m_renderer->GetCommandPool(), m_renderer->GetTransferQueue());
}

// ─────────────────────────────────────────────
//  Block interaction
// ─────────────────────────────────────────────
//
//  Edge detection (act once per click, not every
//  frame the button is held) is handled inside
//  InputManager::WasActionPressed — no manual
//  "was it down last frame" bookkeeping needed
//  here anymore.
//
//  BreakBlock  — remove the targeted block
//  PlaceBlock  — place a stone block on the face
//                the ray hit from
//
//  Bindings live in KeyBindings::Defaults();
//  rebind via m_input->GetBindings().Rebind(...).
// ─────────────────────────────────────────────

void Application::HandleBlockInteraction() {
    const bool breakPressed = m_input->WasActionPressed(InputAction::BreakBlock);
    const bool placePressed = m_input->WasActionPressed(InputAction::PlaceBlock);

    if (!breakPressed && !placePressed) return;
 
    // Cast a ray from the camera eye along the view direction
    const RaycastResult hit = m_chunkManager->Raycast(
        m_camera->GetPosition(),
        m_camera->GetFront(),
        /*maxDistance=*/10.f);
 
    if (!hit.hit) return;

    if (breakPressed) {
        // Break: replace the hit block with air
        m_chunkManager->SetBlock(hit.blockPos, BlockType::Air);
        Logger::Log(LogLevel::Info, "World",
            "Broke block at (" +
            std::to_string(hit.blockPos.x) + "," +
            std::to_string(hit.blockPos.y) + "," +
            std::to_string(hit.blockPos.z) + ")");
    }
 
    if (placePressed) {
        // Place: one block in from the hit face
        const glm::ivec3 placePos = hit.blockPos + hit.faceNormal;
 
        // Don't place inside the player (simple AABB: skip if within 1 block of eye)
        const glm::ivec3 eyeBlock {
            static_cast<int>(std::floor(m_camera->GetPosition().x)),
            static_cast<int>(std::floor(m_camera->GetPosition().y)),
            static_cast<int>(std::floor(m_camera->GetPosition().z))
        };
        const glm::ivec3 d = placePos - eyeBlock;
        if (d.x == 0 && (d.y == 0 || d.y == -1) && d.z == 0) return;
 
        m_chunkManager->SetBlock(placePos, BlockType::Stone);
        Logger::Log(LogLevel::Info, "World",
            "Placed block at (" +
            std::to_string(placePos.x) + "," +
            std::to_string(placePos.y) + "," +
            std::to_string(placePos.z) + ")");
    }
}

void Application::Render() {
    const int w = m_window->GetWidth();
    const int h = m_window->GetHeight();

    // UI batch for this frame
    UIRenderer& ui = m_renderer->GetUI();
    ui.BeginFrame(static_cast<float>(w), static_cast<float>(h));

    // Crosshair - always visible
    Crosshair::Draw(ui, w, h);

    // Debug overlay - F3 toggled
    m_debugOverlay.Draw(ui, *m_camera, *m_chunkManager, m_timer, w, h);
    
    // DrawFrame records the command buffer (which calls ui.EndFrame internally)
    m_renderer->DrawFrame();
}
#include "game/Application.hpp"

#include "content/blocks/Blocks.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/InputAction.hpp"
#include "engine/core/Logger.hpp"
#include "engine/ui/Crosshair.hpp"
#include "engine/ui/UIRenderer.hpp"
#include "game/events/GameEvents.hpp"
#include "game/registry/BlockRegistry.hpp"
#include <GLFW/glfw3.h>

// Root directory the TextureAtlas scans for block textures, relative to
// the working directory the game is launched from (same convention as
// "shaders/" for SPIR-V — see tools/compile_shaders.sh).
static constexpr const char* TEXTURE_ROOT_DIR = "content/assets/textures"; 

bool Application::Initialize() {
    Logger::Log(LogLevel::Info, "Core", "Initializing application");

    m_window = std::make_unique<Window>(1280, 720, "Voxel Game");

    VG_ASSERT(m_window, "Window creation failed");

    m_input = std::make_unique<InputManager>(m_window->GetNativeWindow());

    m_vulkan = std::make_unique<VulkanContext>(m_window.get());
    m_vulkan->Init();

    m_renderer = std::make_unique<Renderer>(m_vulkan.get(), m_window.get());
    m_renderer->Init();

    // ── Content registration ──
    // Must happen before the atlas is built: CollectRequiredTextures()
    // below reads straight from whatever blocks were just registered.
    RegisterAllBlocks(BlockRegistry::Get());

    // ── Texture atlas ──
    // Scans content/assets/textures/<namespace>/<name>.png for every
    // TextureID referenced by a registered block, packs them into one
    // atlas, and uploads it. Adding a new block+texture later requires
    // no changes here — CollectRequiredTextures() picks it up automatically.
    m_atlas = std::make_unique<TextureAtlas>(m_vulkan.get(), m_renderer->GetCommandPool());

    const auto requiredTextures = BlockRegistry::Get().CollectRequiredTextures();
    m_atlas->Build(TEXTURE_ROOT_DIR, requiredTextures);
 
    m_atlas->WriteDescriptorSet(m_renderer->GetTextureDescSet());
    m_renderer->BindTextureAtlas(m_renderer->GetTextureDescSet());
    
    // Now that the atlas grid is known, resolve every block's per-face
    // TextureIDs into concrete tile coordinates. This is cached on each
    // BlockDef so the mesher does a flat array lookup per face — no
    // string/hash work happens during chunk meshing.
    BlockRegistry::Get().ResolveAtlasCoords(
        [this](const TextureID& id) { return m_atlas->GetTileCoord(id); });

    // World
    m_chunkManager = std::make_unique<ChunkManager>(m_vulkan.get(), m_renderer.get());
    // Must be set before Init() — the mesher bakes UVs using these
    // dimensions when it builds the very first chunk meshes below.
    m_chunkManager->SetAtlasGridSize(m_atlas->GetGridCols(), m_atlas->GetGridRows());
    m_chunkManager->Init(m_renderer->GetCommandPool(), m_renderer->GetTransferQueue());
    m_renderer->SetChunkManager(m_chunkManager.get());

    // Camera
    m_camera = std::make_unique<Camera>(m_window->GetNativeWindow());

    RegisterEventListeners();

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

    EventBus::Get().Unsubscribe<BlockBreakEvent>(m_blockBreakListenerID);
    EventBus::Get().Unsubscribe<BlockPlaceEvent>(m_blockPlaceListenerID);
    
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
        // Read the block BEFORE it's overwritten — listeners (and the
        // event itself) need to know what was actually broken.
        const BlockType broken = m_chunkManager->GetBlock(hit.blockPos);

        // Break: replace the hit block with air
        m_chunkManager->SetBlock(hit.blockPos, BlockType::Air);

        EventBus::Get().Publish(BlockBreakEvent{ hit.blockPos, broken });
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

        constexpr BlockType placedType = BlockType::Stone;
        m_chunkManager->SetBlock(placePos, placedType);

        EventBus::Get().Publish(BlockPlaceEvent{ placePos, placedType });
    }
}

// ─────────────────────────────────────────────
//  Event listeners
//
//  The one place reactive systems get wired onto
//  world events. Add new Subscribe<...>() calls
//  here as new systems need to react — input/world
//  code never needs to change.
// ─────────────────────────────────────────────

void Application::RegisterEventListeners() {
    m_blockBreakListenerID = EventBus::Get().Subscribe<BlockBreakEvent>(
        [](const BlockBreakEvent& e) {
            Logger::Log(LogLevel::Info, "World",
                "Broke block at (" +
                std::to_string(e.position.x) + "," +
                std::to_string(e.position.y) + "," +
                std::to_string(e.position.z) + ")");
        });

    m_blockPlaceListenerID = EventBus::Get().Subscribe<BlockPlaceEvent>(
        [](const BlockPlaceEvent& e) {
            Logger::Log(LogLevel::Info, "World",
                "Placed block at (" +
                std::to_string(e.position.x) + "," +
                std::to_string(e.position.y) + "," +
                std::to_string(e.position.z) + ")");
        });
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
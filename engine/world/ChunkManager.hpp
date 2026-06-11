#pragma once

#include "Chunk.hpp"

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanContext;
class Renderer;

// ─────────────────────────────────────────────
//  ChunkManager
//
//  Owns all loaded chunks.  For now it just
//  creates a single test chunk full of blocks
//  so you can see geometry on screen.
//
//  The draw loop is driven by Renderer calling
//  DrawChunks() from inside RecordCommandBuffer.
//  Since the renderer owns the command buffer we
//  pass the pipeline layout so the manager can
//  push constants itself.
// ─────────────────────────────────────────────

class ChunkManager {
public:
    ChunkManager(VulkanContext* context, Renderer* renderer);
    ~ChunkManager();

    // Build and upload the test chunk
    void Init();

    // Upload all dirty chunk meshes to GPU
    void FlushDirtyMeshes(VkCommandPool pool, VkQueue queue);

    // Iterate visible chunks for the renderer
    const std::vector<std::unique_ptr<Chunk>>& GetChunks() const { return m_chunks; }

private:
    VulkanContext* m_context = nullptr;
    Renderer*      m_renderer = nullptr;

    std::vector<std::unique_ptr<Chunk>> m_chunks;
};
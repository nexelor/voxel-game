#include "ChunkManager.hpp"
#include "engine/renderer/Renderer.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/core/Logger.hpp"
#include "engine/world/ChunkMesh.hpp"

ChunkManager::ChunkManager(VulkanContext* context, Renderer* renderer)
    : m_context(context), m_renderer(renderer) {}

ChunkManager::~ChunkManager() {
    VkDevice device = m_context->GetDevice();
    for (auto& chunk : m_chunks)
        chunk->DestroyBuffers(device);
}

// ─────────────────────────────────────────────
//  Init — build a simple test world
//
//  Generates one 32×32×32 chunk with:
//    y 0..15  → Stone
//    y 14     → Dirt (layer under surface)
//    y 15     → Grass (surface)
//    y 16+    → Air
//
//  You'll see the top of a flat terrain slab
//  floating in world space around y = 0-15.
// ─────────────────────────────────────────────

void ChunkManager::Init() {
    auto chunk = std::make_unique<Chunk>();
    chunk->m_chunkCoord = { 0, 0, 0 };

    // Fill blocks
    for (int z = 0; z < CHUNK_SIZE; z++) {
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int y = 0; y < CHUNK_SIZE; y++) {
                BlockType type = BlockType::Air;

                if (y < 13)       type = BlockType::Stone;
                else if (y == 13) type = BlockType::Dirt;
                else if (y == 14) type = BlockType::Dirt;
                else if (y == 15) type = BlockType::Grass;

                chunk->SetBlock(x, y, z, type);
            }
        }
    }

    m_chunks.push_back(std::move(chunk));

    Logger::Log(LogLevel::Info, "World", "Test chunk generated");
}

// ─────────────────────────────────────────────
//  FlushDirtyMeshes
// ─────────────────────────────────────────────

void ChunkManager::FlushDirtyMeshes(VkCommandPool pool, VkQueue queue) {
    for (auto& chunk : m_chunks) {
        if (!chunk->m_dirty) continue;

        std::vector<VoxelVertex> verts;
        std::vector<uint32_t>    indices;
        ChunkMesher::Mesh(*chunk, verts, indices);

        chunk->UploadMesh(
            m_context->GetDevice(),
            m_context->GetPhysicalDevice(),
            pool, queue,
            verts, indices
        );

        Logger::Log(LogLevel::Info, "World",
            "Chunk mesh uploaded: " +
            std::to_string(verts.size()) + " verts, " +
            std::to_string(indices.size() / 3) + " tris");
    }
}
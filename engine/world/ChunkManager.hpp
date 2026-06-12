#pragma once

#include "Chunk.hpp"
#include "ChunkMesh.hpp"

#include <memory>
#include <vulkan/vulkan.h>

class VulkanContext;
class Renderer;

// ─────────────────────────────────────────────
//  glm::ivec3 hash  (needed for the chunk map)
// ─────────────────────────────────────────────
 
struct IVec3Hash
{
    std::size_t operator()(const glm::ivec3& v) const noexcept
    {
        // Combine three ints with a Fibonacci-mixing constant so
        // axis-aligned runs of coords don't cluster in the same buckets.
        std::size_t h = 0;
        h ^= std::hash<int>{}(v.x) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(v.y) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(v.z) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

// ─────────────────────────────────────────────
//  RaycastResult
// ─────────────────────────────────────────────
 
struct RaycastResult
{
    bool hit = false;
    glm::ivec3 blockPos {};   // world-space coords of the hit block
    glm::ivec3 faceNormal {};   // outward normal of the entered face
    float distance = 0.f;
};

// ─────────────────────────────────────────────
//  ChunkManager
//
//  Owns every loaded chunk in a hash map.
//  All coordinates that cross this interface
//  are WORLD-SPACE block coordinates (ivec3)
//  unless explicitly noted as chunk coords.
//
//  Typical per-frame call order:
//    1. Update(cameraPos, radius, pool, queue)
//         → loads new chunks, unloads distant ones,
//           flushes all dirty meshes
//    2. GetChunks() → Renderer iterates and draws
//
//  Block interaction:
//    hit = Raycast(eye, forward, maxDist)
//    if (hit) SetBlock(hit.blockPos, Air)           // break
//    if (hit) SetBlock(hit.blockPos+hit.faceNormal, Stone) // place
//    // dirty flag is set automatically; next Update() uploads mesh
// ─────────────────────────────────────────────

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

    // Load initial world and upload all meshes.
    void Init(VkCommandPool pool, VkQueue queue);

    // Load/unload chunks around camera, then flush dirty meshes.
    // viewRadiusXZ is in chunk units.  Call once per frame.
    void Update(glm::vec3 cameraWorldPos, int viewRadiusXZ, VkCommandPool pool, VkQueue queue);

    // Block access (world-space)
    BlockType GetBlock(glm::ivec3 worldPos) const;
    void SetBlock(glm::ivec3 worldPos, BlockType type);

    // Ray cast
    RaycastResult Raycast(glm::vec3 origin, glm::vec3 dir, float maxDistance = 10.f) const;
    
    // Renderer access
    using ChunkMap = std::unordered_map<glm::ivec3, std::unique_ptr<Chunk>, IVec3Hash>;
    const ChunkMap& GetChunks() const { return m_chunks; }

    // Exposed so Application can call after block edits between frames
    ChunkNeighbors GatherNeighbors(glm::ivec3 chunkCoord) const;

private:
    // Coord conversions
    static glm::ivec3 WorldToChunkCoord(glm::ivec3 worldPos);
    static glm::ivec3 WorldToLocalPos(glm::ivec3 worldPos);
 
    Chunk* GetChunk(glm::ivec3 chunkCoord);
    const Chunk* GetChunk(glm::ivec3 chunkCoord) const;

    void GenerateChunk(Chunk& chunk, glm::ivec3 chunkCoord) const;
    void RebuildMesh (Chunk& chunk, VkCommandPool pool, VkQueue queue);
    void FlushDirty (VkCommandPool pool, VkQueue queue);

    VulkanContext* m_context = nullptr;
    Renderer* m_renderer = nullptr;

    ChunkMap m_chunks;
};
#pragma once
 
#include "Block.hpp"
#include "engine/renderer/VoxelVertex.hpp"
 
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// ─────────────────────────────────────────────
//  Chunk
//
//  A 32×32×32 region of blocks.  The chunk owns
//  its vertex and index GPU buffers and knows
//  how to upload mesh data into them.
//
//  Coordinate convention (same as Minecraft):
//      X = east (+) / west (−)
//      Y = up   (+) / down (−)
//      Z = south(+) / north(−)
//
//  Chunk-space origin is the block at (0,0,0).
//  World-space position is chunkCoord * SIZE.
// ─────────────────────────────────────────────

static constexpr int CHUNK_SIZE = 32;

class Chunk {
public:
    // Lifecycle
    Chunk() = default;
    ~Chunk() = default;

    // Block access
    BlockType GetBlock(int x, int y, int z) const;
    void SetBlock(int x, int y, int z, BlockType type);

    // Return true if (x,y,z) is inside [0, CHUNK_SIZE]
    static bool InBounds(int x, int y, int z);

    // GPU buffers
    // Call after meshing. device/physDevice needed for memory allocation.
    void UploadMesh(VkDevice device, VkPhysicalDevice physDevice, VkCommandPool pool, VkQueue queue,
        const std::vector<VoxelVertex>& vertices, const std::vector<uint32_t>& indices);

    void DestroyBuffers(VkDevice device);

    // Draw data
    VkBuffer GetVertexBuffer() const { return m_vertexBuffer; }
    VkBuffer GetIndexBuffer() const { return m_indexBuffer; }
    uint32_t GetIndexCount() const { return m_indexCount; }
    bool HasMesh() const { return m_indexCount > 0 && m_vertexBuffer != VK_NULL_HANDLE; }

    // World position
    glm::mat4 GetModelMatrix() const;

    glm::ivec3 m_chunkCoord { 0, 0, 0 }; // chunk-grid coordinate
    bool m_dirty { true }; // mesh needs rebuild

private:
    static uint32_t FindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags props);

    static void CreateBuffer(VkDevice device, VkPhysicalDevice physDevice, VkDeviceSize size, VkBufferUsageFlags usage,
        VkMemoryPropertyFlags props, VkBuffer& outBuf, VkDeviceMemory& outMem);
    
    static void CopyBuffer(VkDevice device, VkCommandPool pool, VkQueue queue, VkBuffer src, VkBuffer dst, VkDeviceSize size);

    // Block data
    // Flat array, index = x + CHUNK_SIZE*(y + CHUNK_SIZE*z)
    std::array<BlockType, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> m_blocks{};

    // GPU resources
    VkBuffer       m_vertexBuffer { VK_NULL_HANDLE };
    VkDeviceMemory m_vertexMemory { VK_NULL_HANDLE };
    VkBuffer       m_indexBuffer  { VK_NULL_HANDLE };
    VkDeviceMemory m_indexMemory  { VK_NULL_HANDLE };
    uint32_t       m_indexCount   { 0 };
};
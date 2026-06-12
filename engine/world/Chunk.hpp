#pragma once
 
#include "Block.hpp"
#include "engine/renderer/VoxelVertex.hpp"
 
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// ─────────────────────────────────────────────
//  Dimensions
//
//  The chunk is always stored as a full 32×32×32
//  block array.  For meshing we subdivide the Y
//  axis into 4 slabs of 8 blocks each:
//
//    slab 0 : y  0 ..  7
//    slab 1 : y  8 .. 15
//    slab 2 : y 16 .. 23
//    slab 3 : y 24 .. 31
//
//  When a block edit touches y=N we mark only
//  slab (N/SLAB_HEIGHT) dirty — and the slab
//  above/below if N is on the boundary — so the
//  mesher re-scans at most 2×8 = 16 Y-layers
//  instead of 32.
// ─────────────────────────────────────────────

inline constexpr int CHUNK_SIZE = 32;

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

    void ClearAllDirty() { m_dirty = false; }
    bool AnyDirty() const { return m_dirty; }

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
    static int Index(int x, int y, int z) {
        return x + CHUNK_SIZE * (y + CHUNK_SIZE * z);
    }
 
    std::array<BlockType, CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE> m_blocks {};
    
    // GPU resources
    VkBuffer       m_vertexBuffer { VK_NULL_HANDLE };
    VkDeviceMemory m_vertexMemory { VK_NULL_HANDLE };
    VkBuffer       m_indexBuffer  { VK_NULL_HANDLE };
    VkDeviceMemory m_indexMemory  { VK_NULL_HANDLE };
    uint32_t       m_indexCount   { 0 };

    // Internal buffer helpers
    static uint32_t FindMemoryType(VkPhysicalDevice, uint32_t filter, VkMemoryPropertyFlags);
    static void CreateBuffer(VkDevice, VkPhysicalDevice, VkDeviceSize, VkBufferUsageFlags, VkMemoryPropertyFlags, VkBuffer&, VkDeviceMemory&);
    static void CopyBuffer(VkDevice, VkCommandPool, VkQueue, VkBuffer src, VkBuffer dst, VkDeviceSize);
};
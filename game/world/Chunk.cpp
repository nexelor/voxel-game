#include "game/world/Chunk.hpp"
 
#include <glm/gtc/matrix_transform.hpp>
#include <stdexcept>
#include <cstring>

bool Chunk::InBounds(int x, int y, int z) {
    return x >= 0 && x < CHUNK_SIZE
        && y >= 0 && y < CHUNK_SIZE
        && z >= 0 && z < CHUNK_SIZE;
}
 
BlockType Chunk::GetBlock(int x, int y, int z) const {
    if (!InBounds(x, y, z)) return BlockType::Air;
    return m_blocks[Index(x, y, z)];
}
 
void Chunk::SetBlock(int x, int y, int z, BlockType type) {
    if (!InBounds(x, y, z)) return;
    m_blocks[Index(x, y, z)] = type;
    m_dirty = true;
}

///
/// World
///

glm::mat4 Chunk::GetModelMatrix() const {
    return glm::translate(glm::mat4(1.0f), glm::vec3(m_chunkCoord) * static_cast<float>(CHUNK_SIZE));
}

///
/// GPU buffer helpers
///

uint32_t Chunk::FindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
 
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("Chunk: failed to find suitable memory type");
}

void Chunk::CreateBuffer(VkDevice device, VkPhysicalDevice physDevice, VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& outBuf, VkDeviceMemory& outMem)
{
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
 
    if (vkCreateBuffer(device, &info, nullptr, &outBuf) != VK_SUCCESS)
        throw std::runtime_error("Chunk: failed to create buffer");
 
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(device, outBuf, &reqs);
 
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = reqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(physDevice, reqs.memoryTypeBits, props);
 
    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMem) != VK_SUCCESS)
        throw std::runtime_error("Chunk: failed to allocate buffer memory");
 
    vkBindBufferMemory(device, outBuf, outMem, 0);
}

void Chunk::CopyBuffer(VkDevice device, VkCommandPool pool, VkQueue queue, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    // One-shot command buffer for the transfer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;
 
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);
 
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
 
    VkBufferCopy region{ 0, 0, size };
    vkCmdCopyBuffer(cmd, src, dst, 1, &region);
 
    vkEndCommandBuffer(cmd);
 
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
 
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}

///
/// Mesh upload
///

void Chunk::UploadMesh(VkDevice device, VkPhysicalDevice physDevice, VkCommandPool pool, VkQueue queue,
    const std::vector<VoxelVertex>& vertices, const std::vector<uint32_t>&    indices)
{
    // Destroy old buffers first
    DestroyBuffers(device);
 
    if (vertices.empty() || indices.empty()) {
        m_indexCount = 0;
        return;
    }

    auto uploadBuffer = [&](VkDeviceSize size, const void* data,
        VkBufferUsageFlags dstUsage, VkBuffer& outBuf, VkDeviceMemory& outMem)
    {
        VkBuffer staging;
        VkDeviceMemory stagingMem;
        CreateBuffer(device, physDevice, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingMem);
 
        void* ptr;
        vkMapMemory(device, stagingMem, 0, size, 0, &ptr);
        memcpy(ptr, data, static_cast<size_t>(size));
        vkUnmapMemory(device, stagingMem);
 
        CreateBuffer(device, physDevice, size, dstUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, outBuf, outMem);
 
        CopyBuffer(device, pool, queue, staging, outBuf, size);
        vkDestroyBuffer(device, staging, nullptr);
        vkFreeMemory(device, stagingMem, nullptr);
    };

    uploadBuffer(sizeof(VoxelVertex) * vertices.size(), vertices.data(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, m_vertexBuffer, m_vertexMemory);
 
    uploadBuffer(sizeof(uint32_t) * indices.size(), indices.data(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, m_indexBuffer, m_indexMemory);
 
    m_indexCount = static_cast<uint32_t>(indices.size());
}

void Chunk::DestroyBuffers(VkDevice device) {
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        vkFreeMemory(device, m_vertexMemory, nullptr);
        m_vertexBuffer = VK_NULL_HANDLE;
        m_vertexMemory = VK_NULL_HANDLE;
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        vkFreeMemory(device, m_indexMemory, nullptr);
        m_indexBuffer = VK_NULL_HANDLE;
        m_indexMemory = VK_NULL_HANDLE;
    }
    m_indexCount = 0;
}
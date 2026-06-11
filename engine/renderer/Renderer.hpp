#pragma once

#include "engine/renderer/GraphicsPipeline.hpp"
#include "engine/renderer/Shader.hpp"
#include "engine/renderer/VoxelVertex.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "Swapchain.hpp"

#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class Window;

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

struct CameraUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 cameraPos;
    float _pad = 0.0f;
};

struct FrameUBO {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;
};

class Renderer {
public:
    Renderer(VulkanContext* context, Window* window);
    ~Renderer();

    void Init();
    void Cleanup();

    void DrawFrame();

    void UpdateCamera(const CameraUBO& camera);

private:
    // Persistent (survive swapchain recreation)
    void CreateCommandPool();
    void CreateDescriptorSetLayouts();
    void CreateDescriptorPool();
    void CreateCameraUBOs();
    void CreateSyncObjects();
 
    void DestroyDescriptorSetLayouts();
    void DestroyCameraUBOs();
    void DestroySyncObjects();

    // Descriptor sets are allocated from the pool; they don't need
    // explicit destruction if the pool is destroyed with FREE_DESCRIPTOR_SET_BIT.
    void AllocateDescriptorSets();
    void UpdateDescriptorSets();   // writes VkBuffer / VkImageView into each set
 
    // Swapchain-dependent (recreated on resize)
    void CreateSwapchainResources();
    void CleanupSwapchain();
    void RecreateSwapchain();
 
    void CreateRenderPass();
    void CreateDepthResources();
    void CreateFramebuffers();
    void CreateCommandBuffers();
    void CreateVoxelPipeline();

    // Per-frame recording
    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
 
    // Helpers
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
        VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;

private:
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;

    // Swapchain
    std::unique_ptr<Swapchain> m_swapchain;
    
    // Render pass & framebuffers
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> m_framebuffers;

    // Depth buffer
    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthImageMemory = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    // Command pool / buffers
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;

    // Sync primitives
    std::vector<VkSemaphore> m_imageAvailableSemaphores; // one per swapchain image
    uint32_t m_currentFrame = 0;

    // Pipeline
    GraphicsPipeline m_voxelPipeline;

    // Descriptor infrastructure
    VkDescriptorSetLayout m_globalSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool   = VK_NULL_HANDLE;

    // One descriptor set per frame-in-flight for set 0 (camera UBO)
    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_globalDescSets{};

    // Set 1 (atlas texture) is shared across frames — texture doesn't change per-frame.
    // Set to VK_NULL_HANDLE until TextureAtlas is implemented; drawing is skipped then.
    VkDescriptorSet m_textureDescSet = VK_NULL_HANDLE;

    // Camera uniform buffers
    // One per frame-in-flight; persistently mapped.
    std::array<FrameUBO, MAX_FRAMES_IN_FLIGHT> m_cameraUBOs;
};
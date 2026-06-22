#pragma once

#include "engine/renderer/GraphicsPipeline.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/renderer/Swapchain.hpp"
#include "engine/ui/UIRenderer.hpp"

#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

class ChunkManager;
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

    UIRenderer& GetUI() { return *m_ui; }

    VkCommandPool GetCommandPool() const { return m_commandPool; }
    VkQueue GetTransferQueue() const { return m_context->GetGraphicsQueue(); }
    VkDescriptorSet GetTextureDescSet() const { return m_textureDescSet; }

    void BindTextureAtlas(VkDescriptorSet atlasSet) { m_textureDescSet = atlasSet; }

    void SetChunkManager(ChunkManager* cm) { m_chunkManager = cm; }
    void SetSelectedBlock(glm::ivec3 pos, bool hasSelection) {
        m_selectedBlockPos = pos;
        m_hasSelection = hasSelection;
    }

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

    void AllocateDescriptorSets();
    void UpdateDescriptorSets();
 
    // Swapchain-dependent (recreated on resize)
    void CreateSwapchainResources();
    void CleanupSwapchain();
    void RecreateSwapchain();
 
    void CreateRenderPass();
    void CreateDepthResources();
    void CreateFramebuffers();
    void CreateCommandBuffers();
    void CreateVoxelPipeline();
    void CreateSelectionPipeline();
    void CreateSelectionBuffers();
    void DestroySelectionBuffers();

    // Per-frame recording
    void RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
 
    // Helpers
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
        VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;

private:
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;
    ChunkManager* m_chunkManager = nullptr;
    
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
    GraphicsPipeline m_translucentPipeline;
    GraphicsPipeline m_selectionPipeline;

    // Selection highlight geometry (static unit cube wireframe)
    VkBuffer       m_selectionVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_selectionVertexMemory = VK_NULL_HANDLE;
    VkBuffer       m_selectionIndexBuffer  = VK_NULL_HANDLE;
    VkDeviceMemory m_selectionIndexMemory  = VK_NULL_HANDLE;
    uint32_t       m_selectionIndexCount   = 0;

    bool       m_hasSelection      = false;
    glm::ivec3 m_selectedBlockPos  {};

    // Descriptor infrastructure
    VkDescriptorSetLayout m_globalSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool      m_descriptorPool   = VK_NULL_HANDLE;

    std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_globalDescSets{};
    VkDescriptorSet m_textureDescSet = VK_NULL_HANDLE;

    // Camera uniform buffers
    std::array<FrameUBO, MAX_FRAMES_IN_FLIGHT> m_cameraUBOs;

    CameraUBO m_lastCameraUBO{};
    
    // UI renderer
    std::unique_ptr<UIRenderer> m_ui;
};
#pragma once

#include "engine/renderer/VulkanContext.hpp"
#include "Swapchain.hpp"

#include <array>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

class Window;

static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence         inFlightFence           = VK_NULL_HANDLE;
};

class Renderer {
public:
    Renderer(VulkanContext* context, Window* window);
    ~Renderer();

    void Init();
    void Cleanup();

    void DrawFrame();

private:
    // Swapchain-dependent resources
    void CreateRenderPass();
    void CreateFramebuffers();
    void CreateCommandBuffers();
    void CreateSwapchainResources();
    void CleanupSwapchain();
    void RecreateSwapchain();

    // Persistent resources (survive swapchain recreation)
    void CreateCommandPool();
    void CreateSyncObjects();
    void DestroySyncObjects();

    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

private:
    VulkanContext* m_context = nullptr;
    Window*        m_window  = nullptr;

    std::unique_ptr<Swapchain> m_swapchain;

    VkRenderPass                 m_renderPass   = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>   m_framebuffers;

    VkCommandPool                          m_commandPool = VK_NULL_HANDLE;
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    std::vector<VkSemaphore> m_imageAvailableSemaphores; // one per swapchain image
    uint32_t m_currentFrame = 0;
};
#include "Renderer.hpp"

#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "engine/platform/Window.hpp"
#include "engine/core/Logger.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

Renderer::Renderer(VulkanContext* context, Window* window)
    : m_context(context), m_window(window) {}

Renderer::~Renderer() { Cleanup(); }

// ─────────────────────────────────────────────
//  Init / Cleanup
// ─────────────────────────────────────────────

void Renderer::Init() {
    CreateCommandPool();
    CreateSwapchainResources();
    CreateSyncObjects();

    Logger::Log(LogLevel::Info, "Renderer", "Renderer initialized");
}

void Renderer::Cleanup() {
    VkDevice device = m_context->GetDevice();
    vkDeviceWaitIdle(device);

    DestroySyncObjects();
    CleanupSwapchain();
    vkDestroyCommandPool(device, m_commandPool, nullptr);
}

// ─────────────────────────────────────────────
//  Persistent resources
// ─────────────────────────────────────────────

void Renderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context->GetGraphicsFamily();

    if (vkCreateCommandPool(m_context->GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

void Renderer::CreateSyncObjects() {
    VkDevice device = m_context->GetDevice();

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // Per-frame: renderFinished + fence
    for (auto& frame : m_frames) {
        if (vkCreateSemaphore(device, &semInfo, nullptr, &frame.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence    (device, &fenceInfo, nullptr, &frame.inFlightFence)         != VK_SUCCESS)
            throw std::runtime_error("Failed to create per-frame sync objects");
    }

    // Per-swapchain-image: imageAvailable
    const size_t acquirePoolSize = std::max(
        (size_t)MAX_FRAMES_IN_FLIGHT,
        m_swapchain->GetImageViews().size()
    );
    m_imageAvailableSemaphores.resize(acquirePoolSize);
    for (auto& sem : m_imageAvailableSemaphores) {
        if (vkCreateSemaphore(device, &semInfo, nullptr, &sem) != VK_SUCCESS)
            throw std::runtime_error("Failed to create image available semaphore");
    }
}

void Renderer::DestroySyncObjects() {
    VkDevice device = m_context->GetDevice();

    for (auto& frame : m_frames) {
        vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr);
        vkDestroyFence    (device, frame.inFlightFence,           nullptr);
        frame.renderFinishedSemaphore = VK_NULL_HANDLE;
        frame.inFlightFence           = VK_NULL_HANDLE;
    }

    for (auto& sem : m_imageAvailableSemaphores)
        vkDestroySemaphore(device, sem, nullptr);
    m_imageAvailableSemaphores.clear();
}

// ─────────────────────────────────────────────
//  Swapchain-dependent resources
// ─────────────────────────────────────────────

void Renderer::CreateSwapchainResources() {
    m_swapchain = std::make_unique<Swapchain>(m_context, m_window);
    m_swapchain->Init();

    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandBuffers();
}

void Renderer::CleanupSwapchain() {
    VkDevice device = m_context->GetDevice();

    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();

    // Free command buffers back to pool (pool itself survives)
    for (auto& frame : m_frames) {
        if (frame.commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, m_commandPool, 1, &frame.commandBuffer);
            frame.commandBuffer = VK_NULL_HANDLE;
        }
    }

    vkDestroyRenderPass(device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;

    m_swapchain.reset();
}

void Renderer::RecreateSwapchain() {
    // Handle minimization — wait until window has a real size
    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window->GetNativeWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_context->GetDevice());

    CleanupSwapchain();
    CreateSwapchainResources();

    m_window->ResetResizeFlag();

    Logger::Log(LogLevel::Info, "Renderer", "Swapchain recreated");
}

// ─────────────────────────────────────────────
//  Render pass
// ─────────────────────────────────────────────

void Renderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format         = m_swapchain->GetImageFormat();
    colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &colorRef;

    // Subpass dependency: ensure image layout transition happens after
    // the swapchain finishes reading the image
    VkSubpassDependency dependency{};
    dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass    = 0;
    dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments    = &colorAttachment;
    renderPassInfo.subpassCount    = 1;
    renderPassInfo.pSubpasses      = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies   = &dependency;

    if (vkCreateRenderPass(m_context->GetDevice(), &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

// ─────────────────────────────────────────────
//  Framebuffers
// ─────────────────────────────────────────────

void Renderer::CreateFramebuffers() {
    const auto& imageViews = m_swapchain->GetImageViews();
    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass      = m_renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments    = attachments;
        fbInfo.width           = m_swapchain->GetExtent().width;
        fbInfo.height          = m_swapchain->GetExtent().height;
        fbInfo.layers          = 1;

        if (vkCreateFramebuffer(m_context->GetDevice(), &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

// ─────────────────────────────────────────────
//  Command buffers
// ─────────────────────────────────────────────

void Renderer::CreateCommandBuffers() {
    // One command buffer per frame-in-flight, NOT per swapchain image
    for (auto& frame : m_frames) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_context->GetDevice(), &allocInfo, &frame.commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffer");
    }
}

void Renderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer");

    VkClearValue clearColor = {{{0.1f, 0.2f, 0.35f, 1.0f}}};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass        = m_renderPass;
    rpInfo.framebuffer       = m_framebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapchain->GetExtent();
    rpInfo.clearValueCount   = 1;
    rpInfo.pClearValues      = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
    // ── draw calls will go here ──
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}

// ─────────────────────────────────────────────
//  Draw frame
// ─────────────────────────────────────────────

void Renderer::DrawFrame() {
    VkDevice   device = m_context->GetDevice();
    FrameData& frame  = m_frames[m_currentFrame];

    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    // Use a per-image semaphore — we don't know imageIndex yet,
    // so we use a "pending" slot: the one currentFrame would have used last time.
    // Simplest correct approach: use a dedicated "acquire semaphore" pool indexed by currentFrame,
    // and track which imageIndex it ended up signaling.
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(
        device,
        m_swapchain->GetSwapchain(),
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], // indexed by frame slot, pool size >= swapchain images
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    vkResetFences(device, 1, &frame.inFlightFence);

    vkResetCommandBuffer(frame.commandBuffer, 0);
    RecordCommandBuffer(frame.commandBuffer, imageIndex);

    VkSemaphore          waitSemaphores[]   = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[]       = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore          signalSemaphores[] = { frame.renderFinishedSemaphore };

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(m_context->GetGraphicsQueue(), 1, &submitInfo, frame.inFlightFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command");

    VkSwapchainKHR   swapchains[] = { m_swapchain->GetSwapchain() };
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = signalSemaphores;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = swapchains;
    presentInfo.pImageIndices      = &imageIndex;

    result = vkQueuePresentKHR(m_context->GetPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window->WasResized())
        RecreateSwapchain();
    else if (result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swapchain image");

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}
#include "Renderer.hpp"

#include "Swapchain.hpp"
#include "VulkanContext.hpp"
#include "engine/platform/Window.hpp"

#include <GLFW/glfw3.h>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

Renderer::Renderer(VulkanContext* context, Window* window) : m_context(context), m_window(window) {}
Renderer::~Renderer() { Cleanup(); }

void Renderer::Init() {
    CreateCommandPool();
    CreateSwapchainResources();
    CreateSyncObjects();
}

void Renderer::Cleanup() {
    VkDevice device = m_context->GetDevice();

    vkDeviceWaitIdle(device);

    CleanupSwapchain();

    vkDestroyFence(device, m_inFlightFence, nullptr);
    vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);
    vkDestroySemaphore(device, m_imageAvailableSemaphore, nullptr);
    vkDestroyCommandPool(device, m_commandPool, nullptr);
}

void Renderer::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain->GetImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(m_context->GetDevice(), &renderPassInfo,
        nullptr, &m_renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass");
    }
}

void Renderer::CreateFramebuffers() {
    const auto& imageViews = m_swapchain->GetImageViews();
    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[] = { imageViews[i] };
        
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchain->GetExtent().width;
        framebufferInfo.height = m_swapchain->GetExtent().height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(m_context->GetDevice(), &framebufferInfo,
            nullptr, &m_framebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void Renderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context->GetGraphicsFamily();

    if (vkCreateCommandPool(m_context->GetDevice(), &poolInfo,
        nullptr, &m_commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }
}

void Renderer::CreateCommandBuffers() {
    m_commandBuffers.resize(m_framebuffers.size());

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(m_commandBuffers.size());

    if (vkAllocateCommandBuffers(m_context->GetDevice(), &allocInfo,
        m_commandBuffers.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

void Renderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkClearValue clearColor = {{{0.1f, 0.2f, 0.35f, 1.0f}}};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_framebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->GetExtent();
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);
}

void Renderer::CreateSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    // Start signaled so first frame works
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(m_context->GetDevice(), &semaphoreInfo,
        nullptr, &m_imageAvailableSemaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image semaphore");
    }

    if (vkCreateSemaphore(m_context->GetDevice(), &semaphoreInfo,
        nullptr, &m_renderFinishedSemaphore) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render semaphore");
    }

    if (vkCreateFence(m_context->GetDevice(), &fenceInfo, nullptr,
        &m_inFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create fence");
    }
}

void Renderer::DrawFrame() {
    VkDevice device = m_context->GetDevice();

    vkWaitForFences(device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);

    vkResetFences(device, 1, &m_inFlightFence);

    uint32_t imageIndex;

    VkResult result = vkAcquireNextImageKHR(device, m_swapchain->GetSwapchain(), UINT64_MAX,
        m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }

    vkResetCommandBuffer(m_commandBuffers[ imageIndex ], 0);

    RecordCommandBuffer(m_commandBuffers[ imageIndex ], imageIndex);

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphore };

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[ imageIndex ];

    VkSemaphore signalSemaphores[] = { m_renderFinishedSemaphore };

    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_context->GetGraphicsQueue(), 1,
        &submitInfo, m_inFlightFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command");
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = { m_swapchain->GetSwapchain() };
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->GetPresentQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window->WasResized()) {
        RecreateSwapchain();
    }
}

void Renderer::CreateSwapchainResources() {
    m_swapchain = std::make_unique<Swapchain>(m_context, m_window);
    m_swapchain->Init();

    CreateRenderPass();
    CreateFramebuffers();
    CreateCommandBuffers();
}

void Renderer::CleanupSwapchain() {
    VkDevice device = m_context->GetDevice();

    vkFreeCommandBuffers(device, m_commandPool,
        static_cast<uint32_t>(m_commandBuffers.size()), m_commandBuffers.data());

    m_commandBuffers.clear();

    for (auto framebuffer : m_framebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }

    m_framebuffers.clear();

    vkDestroyRenderPass(device, m_renderPass, nullptr);

    m_swapchain.reset();
}

void Renderer::RecreateSwapchain() {
    int width = 0;
    int height = 0;

    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window->GetNativeWindow(), &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_context->GetDevice());

    CleanupSwapchain();

    CreateSwapchainResources();

    m_window->ResetResizeFlag();
}
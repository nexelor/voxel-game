#include "engine/renderer/Renderer.hpp"

#include "engine/renderer/Swapchain.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/platform/Window.hpp"
#include "engine/core/Logger.hpp"
#include "engine/renderer/Shader.hpp"
#include "engine/renderer/VoxelVertex.hpp"
#include "game/world/ChunkManager.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

Renderer::Renderer(VulkanContext* context, Window* window)
    : m_context(context), m_window(window) {}

Renderer::~Renderer() { Cleanup(); }

void Renderer::Init() {
    CreateCommandPool();
    CreateDescriptorSetLayouts();
    CreateDescriptorPool();
    CreateCameraUBOs();

    CreateSwapchainResources();
    
    AllocateDescriptorSets();
    UpdateDescriptorSets();

    m_ui = std::make_unique<UIRenderer>(m_context, m_renderPass, m_commandPool);
    m_ui->Init();

    CreateSyncObjects();

    Logger::Log(LogLevel::Info, "Renderer", "Renderer initialized");
}

void Renderer::Cleanup() {
    VkDevice device = m_context->GetDevice();
    vkDeviceWaitIdle(device);

    if (m_ui) {
        m_ui->Cleanup();
        m_ui.reset();
    }

    DestroySyncObjects();
    CleanupSwapchain();
    if (m_descriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);

    DestroyCameraUBOs();
    DestroyDescriptorSetLayouts();

    vkDestroyCommandPool(device, m_commandPool, nullptr);
}

void Renderer::CreateCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context->GetGraphicsFamily();

    if (vkCreateCommandPool(m_context->GetDevice(), &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create command pool");
}

void Renderer::CreateDescriptorSetLayouts() {
    VkDevice device = m_context->GetDevice();
 
    // Set 0 — CameraUBO (vertex + fragment)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
 
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings = &b;
 
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_globalSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create global descriptor set layout");
    }
 
    // Set 1 — block atlas sampler (fragment only)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
 
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings = &b;
 
        if (vkCreateDescriptorSetLayout(device, &info, nullptr, &m_textureSetLayout) != VK_SUCCESS)
            throw std::runtime_error("Failed to create texture descriptor set layout");
    }
}

void Renderer::DestroyDescriptorSetLayouts() {
    VkDevice device = m_context->GetDevice();
    vkDestroyDescriptorSetLayout(device, m_globalSetLayout,  nullptr);
    vkDestroyDescriptorSetLayout(device, m_textureSetLayout, nullptr);
    m_globalSetLayout  = VK_NULL_HANDLE;
    m_textureSetLayout = VK_NULL_HANDLE;
}

void Renderer::CreateDescriptorPool() {
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1; // one atlas for all frames
 
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_FRAMES_IN_FLIGHT + 1;
 
    if (vkCreateDescriptorPool(m_context->GetDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        throw std::runtime_error("Failed to create descriptor pool");
}
 
void Renderer::CreateCameraUBOs() {
    VkDeviceSize bufSize = sizeof(CameraUBO);
 
    for (auto& ubo : m_cameraUBOs) {
        CreateBuffer(bufSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, ubo.buffer, ubo.memory);
        if (vkMapMemory(m_context->GetDevice(), ubo.memory, 0, bufSize, 0, &ubo.mappedPtr) != VK_SUCCESS)
            throw std::runtime_error("Failed to map camero UBO memory");
    }
}

void Renderer::DestroyCameraUBOs() {
    VkDevice device = m_context->GetDevice();
    for (auto& ubo : m_cameraUBOs) {
        if (ubo.mappedPtr) {
            vkUnmapMemory(device, ubo.memory);
            ubo.mappedPtr = nullptr;
        }
        vkDestroyBuffer(device, ubo.buffer, nullptr);
        vkFreeMemory(device, ubo.memory, nullptr);
        ubo.buffer = VK_NULL_HANDLE;
        ubo.memory = VK_NULL_HANDLE;
    }
}

void Renderer::AllocateDescriptorSets() {
    // ── Set 0 (global / camera) — one per frame ──
    {
        std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT> layouts;
        layouts.fill(m_globalSetLayout);
 
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descriptorPool;
        ai.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
        ai.pSetLayouts = layouts.data();
 
        if (vkAllocateDescriptorSets(m_context->GetDevice(), &ai, m_globalDescSets.data()) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate global descriptor sets");
    }
 
    // Set 1 — single shared atlas set
    {
        VkDescriptorSetAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool = m_descriptorPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_textureSetLayout;
 
        if (vkAllocateDescriptorSets(m_context->GetDevice(), &ai, &m_textureDescSet) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate texture descriptor set");
    }
}

void Renderer::UpdateDescriptorSets() {
    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorBufferInfo bufInfo{};
        bufInfo.buffer = m_cameraUBOs[i].buffer;
        bufInfo.offset = 0;
        bufInfo.range  = sizeof(CameraUBO);
 
        VkWriteDescriptorSet write{};
        write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet          = m_globalDescSets[i];
        write.dstBinding      = 0;
        write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo     = &bufInfo;
 
        vkUpdateDescriptorSets(m_context->GetDevice(), 1, &write, 0, nullptr);
    }
}

void Renderer::CreateSyncObjects() {
    VkDevice device = m_context->GetDevice();

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : m_frames) {
        if (vkCreateSemaphore(device, &semInfo, nullptr, &frame.renderFinishedSemaphore) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to create per-frame sync objects");
    }

    const size_t acquirePoolSize = std::max(
        static_cast<size_t>(MAX_FRAMES_IN_FLIGHT),
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
        vkDestroyFence(device, frame.inFlightFence, nullptr);
        frame.renderFinishedSemaphore = VK_NULL_HANDLE;
        frame.inFlightFence = VK_NULL_HANDLE;
    }

    for (auto& sem : m_imageAvailableSemaphores)
        vkDestroySemaphore(device, sem, nullptr);
    m_imageAvailableSemaphores.clear();
}

void Renderer::CreateSwapchainResources() {
    m_swapchain = std::make_unique<Swapchain>(m_context, m_window);
    m_swapchain->Init();

    CreateRenderPass();
    CreateDepthResources();
    CreateFramebuffers();
    CreateCommandBuffers();
    CreateVoxelPipeline();
}

void Renderer::CleanupSwapchain() {
    VkDevice device = m_context->GetDevice();

    m_voxelPipeline.Destroy(device);
    m_translucentPipeline.Destroy(device);

    vkDestroyImageView(device, m_depthImageView, nullptr);
    vkDestroyImage(device, m_depthImage, nullptr);
    vkFreeMemory(device, m_depthImageMemory, nullptr);
    m_depthImageView = VK_NULL_HANDLE;
    m_depthImage = VK_NULL_HANDLE;
    m_depthImageMemory = VK_NULL_HANDLE;

    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(device, fb, nullptr);
    m_framebuffers.clear();

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
    int w = 0, h = 0;
    while (w == 0 || h == 0) {
        glfwGetFramebufferSize(m_window->GetNativeWindow(), &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_context->GetDevice());

    if (m_ui) m_ui->Cleanup();

    CleanupSwapchain();
    CreateSwapchainResources();
    m_window->ResetResizeFlag();

    if (m_ui) {
        m_ui = std::make_unique<UIRenderer>(m_context, m_renderPass, m_commandPool);
        m_ui->Init();
    }

    Logger::Log(LogLevel::Info, "Renderer", "Swapchain recreated");
}

void Renderer::CreateRenderPass() {
    // Colour attachment
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapchain->GetImageFormat();
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = VK_FORMAT_D32_SFLOAT;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    if (vkCreateRenderPass(m_context->GetDevice(), &rpInfo, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

void Renderer::CreateDepthResources() {
    VkDevice device = m_context->GetDevice();
    VkExtent2D extent = m_swapchain->GetExtent();

    // Image
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent = { extent.width, extent.height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_D32_SFLOAT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
 
    if (vkCreateImage(device, &imgInfo, nullptr, &m_depthImage) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image");
 
    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(device, m_depthImage, &memReqs);
 
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &m_depthImageMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate depth image memory");
 
    vkBindImageMemory(device, m_depthImage, m_depthImageMemory, 0);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
 
    if (vkCreateImageView(device, &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS)
        throw std::runtime_error("Failed to create depth image view");
}

void Renderer::CreateFramebuffers() {
    const auto& imageViews = m_swapchain->GetImageViews();
    m_framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        std::array<VkImageView, 2> attachments = { imageViews[i], m_depthImageView };

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_renderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = m_swapchain->GetExtent().width;
        fbInfo.height = m_swapchain->GetExtent().height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_context->GetDevice(), &fbInfo, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }
}

void Renderer::CreateCommandBuffers() {
    for (auto& frame : m_frames) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_context->GetDevice(), &allocInfo, &frame.commandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate command buffer");
    }
}

void Renderer::CreateVoxelPipeline() {
    VkDevice device = m_context->GetDevice();

    auto binding = VoxelVertex::BindingDescription();
    auto attributes = VoxelVertex::AttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attributes.begin(), attributes.end());
    
    {
        Shader vert(device, "shaders/voxel.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        Shader frag(device, "shaders/voxel.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
    
        m_voxelPipeline = GraphicsPipeline::Builder(device, m_renderPass)
            .AddShaderStage(vert.StageInfo())
            .AddShaderStage(frag.StageInfo())
            .SetVertexInput(binding, attrVec)
            .SetDepthTest(true)
            .SetCullMode(VK_CULL_MODE_BACK_BIT)
            .AddPushConstantRange<VoxelPushConstants>(
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
            )
            .AddDescriptorSetLayout(m_globalSetLayout)   // set 0: camera
            .AddDescriptorSetLayout(m_textureSetLayout)  // set 1: atlas
            .Build();
    }

    // Translucent pass: same shaders and vertex layout — only the
    // fixed-function state differs. Depth WRITE off so translucent
    // geometry never blocks what's drawn behind it; depth TEST stays
    // on so it still respects already-drawn opaque geometry (you
    // can't see glass through a wall). Drawn in a second pass after
    // every solid chunk — see RecordCommandBuffer.
    {
        Shader vert(device, "shaders/voxel.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        Shader frag(device, "shaders/voxel.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

        m_translucentPipeline = GraphicsPipeline::Builder(device, m_renderPass)
            .AddShaderStage(vert.StageInfo())
            .AddShaderStage(frag.StageInfo())
            .SetVertexInput(binding, attrVec)
            .SetDepthTest(true, false)           // test ON, write OFF
            .SetCullMode(VK_CULL_MODE_BACK_BIT)
            .SetBlending(true)
            .AddPushConstantRange<VoxelPushConstants>(
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .AddDescriptorSetLayout(m_globalSetLayout)
            .AddDescriptorSetLayout(m_textureSetLayout)
            .Build();
    }
}

void Renderer::RecordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS)
        throw std::runtime_error("Failed to begin command buffer");

    // Two clear values - must match attachment order in CreateRenderPass()
    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color        = {{0.1f, 0.2f, 0.35f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpInfo.renderPass = m_renderPass;
    rpInfo.framebuffer = m_framebuffers[imageIndex];
    rpInfo.renderArea.offset = {0, 0};
    rpInfo.renderArea.extent = m_swapchain->GetExtent();
    rpInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    rpInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
 
    // Set dynamic viewport & scissor (required because we use VK_DYNAMIC_STATE_*)
    VkExtent2D extent = m_swapchain->GetExtent();

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
 
    VkRect2D scissor{ {0,0}, extent };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_voxelPipeline.GetPipeline());

    // Bind set 0 (camera UBO for this frame)
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_voxelPipeline.GetLayout(), 0,
        1, &m_globalDescSets[m_currentFrame], 0, nullptr);

    // Bind set 1 (texture atlas) — only when it has been written by TextureAtlas.
    // Until then we skip any draw calls to avoid a validation error.
    if (m_textureDescSet != VK_NULL_HANDLE && m_chunkManager != nullptr) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_voxelPipeline.GetLayout(),
            1, 1, &m_textureDescSet, 0, nullptr);
 
        for (const auto& [coord, chunk] : m_chunkManager->GetChunks()) {
            if (!chunk->HasMesh()) continue;

            VoxelPushConstants pc{};
            pc.model = chunk->GetModelMatrix();
            pc.atlasRows = 1.0f;
            pc.atlasCols = 1.0f;

            vkCmdPushConstants(cmd, m_voxelPipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(VoxelPushConstants), &pc);
            
            VkBuffer vb[] = { chunk->GetVertexBuffer() };
            VkDeviceSize off[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vb, off);
            vkCmdBindIndexBuffer(cmd, chunk->GetIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cmd, chunk->GetIndexCount(), 1, 0, 0, 0);
        }
    }

    // Translucent pass — same camera/atlas bindings, different
    // pipeline. Chunks are sorted back-to-front by distance from the
    // camera so overlapping translucent geometry (e.g. looking
    // through two stacked glass panes) blends in the right order.
    // This is a per-CHUNK sort, not per-face — good enough until you
    // need correctly sorted translucency *within* a single chunk.
    if (m_textureDescSet != VK_NULL_HANDLE && m_chunkManager != nullptr) {
        std::vector<Chunk*> translucentChunks;
        for (const auto& [coord, chunk] : m_chunkManager->GetChunks())
            if (chunk->HasTranslucentMesh())
                translucentChunks.push_back(chunk.get());

        const glm::vec3 camPos = m_lastCameraUBO.cameraPos;
        auto sqDist = [&](const Chunk* c) {
            const glm::vec3 center = glm::vec3(c->GetModelMatrix()[3]);
            const glm::vec3 diff = center - camPos;
            return glm::dot(diff, diff);
        };
        std::sort(translucentChunks.begin(), translucentChunks.end(),
            [&](const Chunk* a, const Chunk* b) { return sqDist(a) > sqDist(b); }); // far -> near

        if (!translucentChunks.empty()) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_translucentPipeline.GetPipeline());

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_translucentPipeline.GetLayout(), 0,
                1, &m_globalDescSets[m_currentFrame], 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_translucentPipeline.GetLayout(), 1,
                1, &m_textureDescSet, 0, nullptr);

            for (Chunk* chunk : translucentChunks) {
                VoxelPushConstants pc{};
                pc.model = chunk->GetModelMatrix();
                pc.atlasRows = 1.0f;
                pc.atlasCols = 1.0f;

                vkCmdPushConstants(cmd, m_translucentPipeline.GetLayout(),
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(VoxelPushConstants), &pc);

                VkBuffer vb[] = { chunk->GetTranslucentVertexBuffer() };
                VkDeviceSize off[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vb, off);
                vkCmdBindIndexBuffer(cmd, chunk->GetTranslucentIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, chunk->GetTranslucentIndexCount(), 1, 0, 0, 0);
            }
        }
    }

    if (m_ui)
        m_ui->EndFrame(cmd);

    vkCmdEndRenderPass(cmd);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
        throw std::runtime_error("Failed to record command buffer");
}

void Renderer::DrawFrame() {
    VkDevice device = m_context->GetDevice();
    FrameData& frame = m_frames[m_currentFrame];

    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, m_swapchain->GetSwapchain(), UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        RecreateSwapchain();
        return;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("Failed to acquire swapchain image");

    vkResetFences(device, 1, &frame.inFlightFence);

    vkResetCommandBuffer(frame.commandBuffer, 0);
    RecordCommandBuffer(frame.commandBuffer, imageIndex);

    VkSemaphore waitSemaphores[] = { m_imageAvailableSemaphores[m_currentFrame] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSemaphore signalSemaphores[] = { frame.renderFinishedSemaphore };

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = waitSemaphores;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &frame.commandBuffer;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(m_context->GetGraphicsQueue(), 1, &si, frame.inFlightFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to submit draw command");

    VkSwapchainKHR swapchains[] = { m_swapchain->GetSwapchain() };
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = signalSemaphores;
    pi.swapchainCount = 1;
    pi.pSwapchains = swapchains;
    pi.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_context->GetPresentQueue(), &pi);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_window->WasResized())
        RecreateSwapchain();
    else if (result != VK_SUCCESS)
        throw std::runtime_error("Failed to present swapchain image");

    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Renderer::UpdateCamera(const CameraUBO& camera) {
    memcpy(m_cameraUBOs[m_currentFrame].mappedPtr, &camera, sizeof(CameraUBO));
    m_lastCameraUBO = camera;
}

uint32_t Renderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_context->GetPhysicalDevice(), &memProps);
 
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
 
    throw std::runtime_error("Failed to find suitable memory type");
}

void Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags props, VkBuffer& outBuffer, VkDeviceMemory& outMemory) const
{
    VkDevice device = m_context->GetDevice();
 
    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
 
    if (vkCreateBuffer(device, &bi, nullptr, &outBuffer) != VK_SUCCESS)
        throw std::runtime_error("Failed to create buffer");
 
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReqs);
 
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = memReqs.size;
    ai.memoryTypeIndex = FindMemoryType(memReqs.memoryTypeBits, props);
 
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
        throw std::runtime_error("Failed to allocate buffer memory");
 
    vkBindBufferMemory(device, outBuffer, outMemory, 0);
}
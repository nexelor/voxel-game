#include "engine/ui/UIRenderer.hpp"
#include "engine/ui/BitmapFont.hpp"
#include "engine/renderer/VulkanContext.hpp"
#include "engine/renderer/Shader.hpp"
#include "engine/core/Logger.hpp"
 
#include <cstring>
#include <stdexcept>
#include <array>
 
///
///  Lifecycle
///

UIRenderer::UIRenderer(VulkanContext* context, VkRenderPass renderPass, VkCommandPool pool)
    : m_context(context), m_renderPass(renderPass), m_pool(pool) {}
 
UIRenderer::~UIRenderer() { Cleanup(); }
 
void UIRenderer::Init() {
    CreateFontTexture();
    CreateFontDescriptorLayout();
    CreateFontDescriptorSet();
    CreatePipeline();
    m_initialized = true;
    Logger::Log(LogLevel::Info, "UI", "UIRenderer initialized");
}

void UIRenderer::Cleanup() {
    if (!m_initialized) return;
    VkDevice device = m_context->GetDevice();
 
    m_pipeline.Destroy(device);
 
    if (m_vertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_vertexBuffer, nullptr);
        vkFreeMemory(device, m_vertexMemory, nullptr);
    }
    if (m_indexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, m_indexBuffer, nullptr);
        vkFreeMemory(device, m_indexMemory, nullptr);
    }
 
    vkDestroyDescriptorPool(device, m_descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_descLayout, nullptr);
 
    vkDestroySampler(device, m_fontSampler, nullptr);
    vkDestroyImageView(device, m_fontView, nullptr);
    vkDestroyImage(device, m_fontImage, nullptr);
    vkFreeMemory(device, m_fontMemory, nullptr);
 
    m_initialized = false;
}

// ─────────────────────────────────────────────
//  Font texture
//
//  Layout: [white_tile_8px][glyph_0 .. glyph_94]
//  White tile = first 8 columns, solid 0xFF.
//  Glyphs use R8_UNORM; 0xFF = opaque, 0x00 = transparent.
// ─────────────────────────────────────────────

void UIRenderer::CreateFontTexture() {
    VkDevice device = m_context->GetDevice();
    VkPhysicalDevice physDev = m_context->GetPhysicalDevice();
    VkQueue queue = m_context->GetGraphicsQueue();
 
    // Build the CPU-side atlas pixels
    std::vector<uint8_t> pixels(ATLAS_W * ATLAS_H, 0);
 
    // White tile (columns 0..FONT_CHAR_W-1)
    for (int py = 0; py < FONT_CHAR_H; ++py)
        for (int px = 0; px < FONT_CHAR_W; ++px)
            pixels[py * ATLAS_W + px] = 0xFF;
 
    // Glyphs (columns FONT_CHAR_W onwards, one tile per glyph)
    for (int g = 0; g < FONT_COUNT; ++g) {
        const uint8_t* glyph = FONT_BITMAP[g];
        const int baseX = FONT_CHAR_W + g * FONT_CHAR_W;
        for (int row = 0; row < FONT_CHAR_H; ++row) {
            for (int col = 0; col < FONT_CHAR_W; ++col) {
                const bool set = (glyph[row] >> (7 - col)) & 1;
                pixels[row * ATLAS_W + baseX + col] = set ? 0xFF : 0x00;
            }
        }
    }

    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(pixels.size());
 
    // Staging buffer
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = dataSize;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &stagingBuf) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to create staging buffer");
        // vkCreateBuffer(device, &info, nullptr, &stagingBuf);
 
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, stagingBuf, &reqs);
 
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = reqs.size;
        ai.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &ai, nullptr, &stagingMem) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to allocate staging memory");
        if (vkBindBufferMemory(device, stagingBuf, stagingMem, 0) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to bind staging memory");
        // vkAllocateMemory(device, &ai, nullptr, &stagingMem);
        // vkBindBufferMemory(device, stagingBuf, stagingMem, 0);
 
        void* ptr;
        if (vkMapMemory(device, stagingMem, 0, dataSize, 0, &ptr) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to map staging memory");
        // vkMapMemory(device, stagingMem, 0, dataSize, 0, &ptr);
        memcpy(ptr, pixels.data(), static_cast<size_t>(dataSize));
        vkUnmapMemory(device, stagingMem);
    }

    // VkImage (R8_UNORM)
    {
        // Verify the format actually supports sampled-optimal-tiling on this device.
        // Some drivers reject single-channel formats for this combo, which would
        // otherwise fail silently and leave m_fontImage producing validation
        // errors on every frame with no obvious cause.
        VkFormatProperties fmtProps;
        vkGetPhysicalDeviceFormatProperties(physDev, VK_FORMAT_R8_UNORM, &fmtProps);
        if (!(fmtProps.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            throw std::runtime_error(
                "UIRenderer: VK_FORMAT_R8_UNORM does not support "
                "VK_IMAGE_TILING_OPTIMAL + SAMPLED on this device");
        }

        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8_UNORM;
        info.extent = { static_cast<uint32_t>(ATLAS_W), static_cast<uint32_t>(ATLAS_H), 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &info, nullptr, &m_fontImage) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: vkCreateImage failed for font atlas");
        // vkCreateImage(device, &info, nullptr, &m_fontImage);
 
        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(device, m_fontImage, &reqs);
 
        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = reqs.size;
        ai.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &ai, nullptr, &m_fontMemory) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to allocate font image memory");
        if (vkBindImageMemory(device, m_fontImage, m_fontMemory, 0) != VK_SUCCESS)
            throw std::runtime_error("UIRenderer: failed to bind font image memory");
        // vkAllocateMemory(device, &ai, nullptr, &m_fontMemory);
        // vkBindImageMemory(device, m_fontImage, m_fontMemory, 0);
        Logger::Log(LogLevel::Info, "UI",
            "Font image created: " + std::to_string(ATLAS_W) + "x" + std::to_string(ATLAS_H));
    }

    // One-shot command: transition → copy → transition
    {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &ai, &cmd);
 
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);
 
        auto barrier = [&](VkImageLayout oldL, VkImageLayout newL, VkAccessFlags src,
            VkAccessFlags dst, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = oldL;
            b.newLayout = newL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = m_fontImage;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = src;
            b.dstAccessMask = dst;
            vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        barrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        
        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { static_cast<uint32_t>(ATLAS_W), static_cast<uint32_t>(ATLAS_H), 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuf, m_fontImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
 
        barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
 
        vkEndCommandBuffer(cmd);
 
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, m_pool, 1, &cmd);
    }

    // Cleanup staging
    vkDestroyBuffer(device, stagingBuf, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
 
    // Image view
    {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = m_fontImage;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8_UNORM;
        info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(device, &info, nullptr, &m_fontView);
    }

    // Sampler — nearest for crisp pixels
    {
        VkSamplerCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = VK_FILTER_NEAREST;
        info.minFilter = VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.maxAnisotropy = 1.0f;
        vkCreateSampler(device, &info, nullptr, &m_fontSampler);
    }
}

void UIRenderer::CreateFontDescriptorLayout() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
 
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = 1;
    info.pBindings = &b;
    vkCreateDescriptorSetLayout(m_context->GetDevice(), &info, nullptr, &m_descLayout);
}

void UIRenderer::CreateFontDescriptorSet() {
    VkDevice device = m_context->GetDevice();
 
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;
 
    VkDescriptorPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.poolSizeCount = 1;
    info.pPoolSizes = &ps;
    info.maxSets = 1;
    vkCreateDescriptorPool(device, &info, nullptr, &m_descPool);
 
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_descLayout;
    vkAllocateDescriptorSets(device, &ai, &m_descSet);
 
    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = m_fontSampler;
    imgInfo.imageView = m_fontView;
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void UIRenderer::CreatePipeline() {
    VkDevice device = m_context->GetDevice();
 
    Shader vert(device, "shaders/ui.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
    Shader frag(device, "shaders/ui.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
 
    auto binding = UIVertex::BindingDescription();
    auto attributes = UIVertex::AttributeDescriptions();
    std::vector<VkVertexInputAttributeDescription> attrVec(attributes.begin(), attributes.end());
 
    m_pipeline = GraphicsPipeline::Builder(device, m_renderPass)
        .AddShaderStage(vert.StageInfo())
        .AddShaderStage(frag.StageInfo())
        .SetVertexInput(binding, attrVec)
        .SetDepthTest(false, false)
        .SetCullMode(VK_CULL_MODE_NONE)
        .SetBlending(true)
        .AddPushConstantRange<UIPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
        .AddDescriptorSetLayout(m_descLayout)
        .Build();
}

///
/// Dynamic buffer management
///
 
uint32_t UIRenderer::FindMemoryType(VkPhysicalDevice physDev, uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    throw std::runtime_error("UIRenderer: no suitable memory type");
}

void UIRenderer::CreateOrResizeDynamicBuffers(size_t vertCount, size_t idxCount) {
    VkDevice device     = m_context->GetDevice();
    VkPhysicalDevice pd = m_context->GetPhysicalDevice();
 
    auto makeBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer& buf, VkDeviceMemory& mem, size_t& cap)
    {
        if (size <= cap) return;
 
        if (buf != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buf, nullptr);
            vkFreeMemory(device, mem, nullptr);
        }
 
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = size;
        info.usage       = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(device, &info, nullptr, &buf);
 
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, buf, &reqs);
 
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = reqs.size;
        ai.memoryTypeIndex = FindMemoryType(pd, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkAllocateMemory(device, &ai, nullptr, &mem);
        vkBindBufferMemory(device, buf, mem, 0);
 
        cap = static_cast<size_t>(size);
    };

    if (vertCount > 0)
        makeBuffer(vertCount * sizeof(UIVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            m_vertexBuffer, m_vertexMemory, m_vertexCapacity);
    if (idxCount > 0)
        makeBuffer(idxCount * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            m_indexBuffer, m_indexMemory, m_indexCapacity);
}

///
/// Per-frame API
///
 
void UIRenderer::BeginFrame(float screenW, float screenH) {
    m_screenW = screenW;
    m_screenH = screenH;
    m_vertices.clear();
    m_indices.clear();
}

void UIRenderer::EndFrame(VkCommandBuffer cmd) {
    if (m_vertices.empty()) return;
 
    CreateOrResizeDynamicBuffers(m_vertices.size(), m_indices.size());
 
    VkDevice device = m_context->GetDevice();
 
    // Upload vertices
    {
        void* ptr;
        vkMapMemory(device, m_vertexMemory, 0, m_vertices.size() * sizeof(UIVertex), 0, &ptr);
        memcpy(ptr, m_vertices.data(), m_vertices.size() * sizeof(UIVertex));
        vkUnmapMemory(device, m_vertexMemory);
    }
 
    // Upload indices
    {
        void* ptr;
        vkMapMemory(device, m_indexMemory, 0, m_indices.size() * sizeof(uint32_t), 0, &ptr);
        memcpy(ptr, m_indices.data(), m_indices.size() * sizeof(uint32_t));
        vkUnmapMemory(device, m_indexMemory);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetPipeline());
 
    UIPushConstants pc{ m_screenW, m_screenH };
    vkCmdPushConstants(cmd, m_pipeline.GetLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(UIPushConstants), &pc);
 
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline.GetLayout(), 0, 1, &m_descSet, 0, nullptr);
 
    VkBuffer     vb[]  = { m_vertexBuffer };
    VkDeviceSize off[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, vb, off);
    vkCmdBindIndexBuffer(cmd, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);
 
    vkCmdDrawIndexed(cmd, static_cast<uint32_t>(m_indices.size()), 1, 0, 0, 0);
}

///
///  Draw calls
///

void UIRenderer::DrawQuad(float x, float y, float w, float h, uint32_t color) {
    // UV centres on the white tile (first FONT_CHAR_W columns)
    const float u0 = WHITE_U + WHITE_DU * 0.5f;
    const float v0 = 0.5f;
 
    const uint32_t base = static_cast<uint32_t>(m_vertices.size());
 
    m_vertices.push_back({ {x, y}, {u0, v0}, color });
    m_vertices.push_back({ {x + w, y}, {u0, v0}, color });
    m_vertices.push_back({ {x + w, y + h}, {u0, v0}, color });
    m_vertices.push_back({ {x, y + h}, {u0, v0}, color });
 
    m_indices.push_back(base + 0); m_indices.push_back(base + 1); m_indices.push_back(base + 2);
    m_indices.push_back(base + 0); m_indices.push_back(base + 2); m_indices.push_back(base + 3);
}

void UIRenderer::DrawText(const std::string& text, float x, float y, uint32_t color, int scale) {
    const float glyphW = static_cast<float>(FONT_CHAR_W * scale);
    const float glyphH = static_cast<float>(FONT_CHAR_H * scale);
 
    // UV width of one glyph tile
    const float tileU = static_cast<float>(FONT_CHAR_W) / ATLAS_W;
 
    float cx = x;

    for (char c : text) {
        if (c == '\n') {
            cx  = x;
            y  += glyphH;
            continue;
        }
 
        const int glyphIdx = static_cast<int>(static_cast<unsigned char>(c)) - FONT_FIRST;
        const int clampedIdx = (glyphIdx < 0 || glyphIdx >= FONT_COUNT) ? 31 : glyphIdx;
 
        // +1 because column 0 is the white tile
        const float u0 = static_cast<float>(FONT_CHAR_W + clampedIdx * FONT_CHAR_W) / ATLAS_W;
        const float u1 = u0 + tileU;
        const float v0 = 0.0f;
        const float v1 = 1.0f;
 
        const uint32_t base = static_cast<uint32_t>(m_vertices.size());
 
        m_vertices.push_back({ {cx, y}, {u0, v0}, color });
        m_vertices.push_back({ {cx + glyphW, y}, {u1, v0}, color });
        m_vertices.push_back({ {cx + glyphW, y + glyphH}, {u1, v1}, color });
        m_vertices.push_back({ {cx, y + glyphH}, {u0, v1}, color });
 
        m_indices.push_back(base + 0); m_indices.push_back(base + 1); m_indices.push_back(base + 2);
        m_indices.push_back(base + 0); m_indices.push_back(base + 2); m_indices.push_back(base + 3);
 
        cx += glyphW;
    }
}

void UIRenderer::DrawTextShadowed(const std::string& text, float x, float y, uint32_t color, int scale) {
    // Draw shadow offset by (scale, scale) pixels in dark grey
    const uint32_t shadow = UIVertex::PackColor(0.15f, 0.15f, 0.15f, 0.9f);
    DrawText(text, x + scale, y + scale, shadow, scale);
    DrawText(text, x, y, color, scale);
}
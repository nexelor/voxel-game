#include "TextureAtlas.hpp"
#include "VulkanContext.hpp"
#include "engine/core/Logger.hpp"
 
#define STB_IMAGE_IMPLEMENTATION
// Only decoding PNGs we ship ourselves — disable the format readers we
// don't need to keep compile time and binary size down.
#define STBI_ONLY_PNG
#include "stb_image.h"
 
#include <cmath>
#include <cstring>
#include <stdexcept>
 
TextureAtlas::TextureAtlas(VulkanContext* context, VkCommandPool pool)
    : m_context(context), m_pool(pool) {}
 
TextureAtlas::~TextureAtlas() {
    VkDevice device = m_context->GetDevice();
    if (m_sampler != VK_NULL_HANDLE) vkDestroySampler(device, m_sampler, nullptr);
    if (m_imageView != VK_NULL_HANDLE) vkDestroyImageView(device, m_imageView, nullptr);
    if (m_image != VK_NULL_HANDLE) vkDestroyImage(device, m_image, nullptr);
    if (m_memory != VK_NULL_HANDLE) vkFreeMemory(device, m_memory, nullptr);
}

///
/// Helpers
///

uint32_t TextureAtlas::FindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("TextureAtlas: no suitable memory type");
}
 
uint32_t TextureAtlas::NextPowerOfTwo(uint32_t v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

std::vector<uint8_t> TextureAtlas::MakeMissingTexture(uint32_t tileSize) {
    std::vector<uint8_t> pixels(static_cast<size_t>(tileSize) * tileSize * 4);

    // 2x2 checkerboard pattern scaled to the tile, like Minecraft's own
    // missing-texture placeholder: magenta / black, fully opaque.
    const uint32_t half = std::max(1u, tileSize / 2);

    for (uint32_t y = 0; y < tileSize; ++y) {
        for (uint32_t x = 0; x < tileSize; ++x) {
            const bool magentaQuadrant = ((x / half) + (y / half)) % 2 == 0;
            uint8_t* px = &pixels[(static_cast<size_t>(y) * tileSize + x) * 4];
            if (magentaQuadrant) {
                px[0] = 255; px[1] = 0; px[2] = 255; px[3] = 255; // magenta
            } else {
                px[0] = 0; px[1] = 0; px[2] = 0; px[3] = 255;     // black
            }
        }
    }

    return pixels;
}

///
/// Loading
///
 
std::vector<TextureAtlas::DecodedImage> TextureAtlas::LoadAll(
    const std::string& textureRootDir, const std::vector<TextureID>& required, uint32_t& outTileSize) const
{
    std::vector<DecodedImage> images;
    images.reserve(required.size());
 
    outTileSize = 0;

    // Fallback tile size used only if EVERY texture fails to load (so we
    // still have something to lock the atlas grid to). Matches Minecraft's
    // classic 16x16 convention.
    constexpr uint32_t kFallbackTileSize = 16;

    for (const TextureID& id : required) {
        const std::string path = textureRootDir + "/" + id.RelativePath();

        int width = 0, height = 0, channels = 0;
        stbi_uc* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
 
        bool ok = (data != nullptr);
        std::string failReason;
        
        if (ok && width != height) {
            failReason = "not square (" + std::to_string(width) + "x" + std::to_string(height) + ")";
            stbi_image_free(data);
            data = nullptr;
            ok = false;
        }

        if (ok && outTileSize != 0 && static_cast<uint32_t>(width) != outTileSize) {
            failReason = "size " + std::to_string(width) + "x" + std::to_string(width) +
                " doesn't match locked atlas tile size " + std::to_string(outTileSize) +
                "x" + std::to_string(outTileSize);
            stbi_image_free(data);
            data = nullptr;
            ok = false;
        }

        if (!ok) {
            if (failReason.empty()) {
                failReason = stbi_failure_reason() ? stbi_failure_reason() : "unknown error";
            }
            Logger::Log(LogLevel::Warning, "Renderer",
                "TextureAtlas: \"" + path + "\" for " + id.ToString() +
                " failed to load (" + failReason + ") — substituting missing-texture checkerboard");

            const uint32_t tileSize = (outTileSize != 0) ? outTileSize : kFallbackTileSize;
            if (outTileSize == 0) outTileSize = tileSize;

            DecodedImage img;
            img.id = id;
            img.pixels = MakeMissingTexture(tileSize);
            images.push_back(std::move(img));
            continue;
        }

        if (outTileSize == 0) {
            outTileSize = static_cast<uint32_t>(width);
        }

        DecodedImage img;
        img.id = id;
        img.pixels.assign(data, data + (static_cast<size_t>(width) * height * 4));
        stbi_image_free(data);
 
        images.push_back(std::move(img));
    }

    return images;
}

///
/// Build — load, pack, upload
///
 
void TextureAtlas::Build(const std::string& textureRootDir, const std::vector<TextureID>& required) {
    if (required.empty()) {
        throw std::runtime_error("TextureAtlas::Build: no textures requested — nothing to pack");
    }

    uint32_t tileSize = 0;
    std::vector<DecodedImage> images = LoadAll(textureRootDir, required, tileSize);
 
    m_tileSize = tileSize;
 
    // Pack into the smallest square grid that fits every tile, rounded up
    // to a power of two so mip generation (later) and GPU alignment stay
    // simple — same rationale as Minecraft's own atlas sizing.
    const uint32_t tileCount = static_cast<uint32_t>(images.size());
    const uint32_t gridSide = NextPowerOfTwo(static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(tileCount)))));
    m_gridCols = gridSide;
    m_gridRows = gridSide;

    const uint32_t atlasW = m_gridCols * m_tileSize;
    const uint32_t atlasH = m_gridRows * m_tileSize;
 
    Logger::Log(LogLevel::Info, "Renderer",
        "TextureAtlas: packing " + std::to_string(tileCount) + " tile(s) of " +
        std::to_string(m_tileSize) + "x" + std::to_string(m_tileSize) +
        " into a " + std::to_string(m_gridCols) + "x" + std::to_string(m_gridRows) +
        " grid (" + std::to_string(atlasW) + "x" + std::to_string(atlasH) + " px)");
 
    // CPU-side composite buffer for the whole atlas, RGBA8.
    std::vector<uint8_t> atlasPixels(static_cast<size_t>(atlasW) * atlasH * 4, 0);
 
    m_tileCoords.clear();
    m_tileCoords.reserve(images.size());

    for (uint32_t i = 0; i < tileCount; ++i) {
        const uint32_t col = i % m_gridCols;
        const uint32_t row = i / m_gridCols;
        m_tileCoords[images[i].id] = { col, row };
 
        const uint32_t originX = col * m_tileSize;
        const uint32_t originY = row * m_tileSize;
 
        // Blit this tile's rows into the composite buffer.
        for (uint32_t y = 0; y < m_tileSize; ++y) {
            const uint8_t* srcRow = &images[i].pixels[static_cast<size_t>(y) * m_tileSize * 4];
            uint8_t* dstRow = &atlasPixels[(static_cast<size_t>(originY + y) * atlasW + originX) * 4];
            std::memcpy(dstRow, srcRow, static_cast<size_t>(m_tileSize) * 4);
        }
    }

    // ── Upload to GPU (staging buffer -> device-local image) ──
    VkDevice device = m_context->GetDevice();
    VkPhysicalDevice physDev = m_context->GetPhysicalDevice();
    VkQueue queue = m_context->GetGraphicsQueue();
 
    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(atlasPixels.size());
 
    VkBuffer stagingBuf;
    VkDeviceMemory stagingMem;
    {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = dataSize;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &stagingBuf) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create staging buffer");
 
        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, stagingBuf, &reqs);
 
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = reqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMem) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to allocate staging memory");
        vkBindBufferMemory(device, stagingBuf, stagingMem, 0);
 
        void* ptr;
        vkMapMemory(device, stagingMem, 0, dataSize, 0, &ptr);
        std::memcpy(ptr, atlasPixels.data(), static_cast<size_t>(dataSize));
        vkUnmapMemory(device, stagingMem);
    }
    
    // Destroy any previous atlas image before creating the new one —
    // Build() is safe to call again later (e.g. a future texture-pack
    // reload feature) without leaking the old GPU resources.
    if (m_imageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }

    if (m_image != VK_NULL_HANDLE) { vkDestroyImage(device, m_image, nullptr); m_image = VK_NULL_HANDLE; }
    if (m_memory != VK_NULL_HANDLE) { vkFreeMemory(device, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
 
    // VkImage (RGBA8 sRGB — matches CreateSolid's previous format choice)
    {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_SRGB;
        info.extent = { atlasW, atlasH, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &info, nullptr, &m_image) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create atlas image");
 
        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(device, m_image, &reqs);
 
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = reqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to allocate atlas image memory");
        vkBindImageMemory(device, m_image, m_memory, 0);
    }

    // One-shot command: transition -> copy -> transition (same pattern as
    // the original CreateSolid()).
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);
 
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        // UNDEFINED -> TRANSFER_DST
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { atlasW, atlasH, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuf, m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
 
        // TRANSFER_DST -> SHADER_READ_ONLY
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_image;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(cmd);
 
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
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
        info.image = m_image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_SRGB;
        info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &info, nullptr, &m_imageView) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create atlas image view");
    }
 
    if (m_sampler == VK_NULL_HANDLE) {
        CreateSampler();
    }
 
    Logger::Log(LogLevel::Info, "Renderer", "TextureAtlas: build complete");
}

void TextureAtlas::CreateSampler() {
    VkSamplerCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // NEAREST keeps pixels crisp, like Minecraft
    info.magFilter = VK_FILTER_NEAREST;
    info.minFilter = VK_FILTER_NEAREST;
    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.anisotropyEnable = VK_FALSE;
    info.maxAnisotropy = 1.0f;
    info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    info.unnormalizedCoordinates = VK_FALSE;
    info.compareEnable = VK_FALSE;
    info.compareOp = VK_COMPARE_OP_ALWAYS;
    info.minLod = 0.0f;
    info.maxLod = 0.0f;
    if (vkCreateSampler(m_context->GetDevice(), &info, nullptr, &m_sampler) != VK_SUCCESS)
        throw std::runtime_error("TextureAtlas: failed to create sampler");
}

///
/// Lookups / descriptor write
///
 
std::pair<uint32_t, uint32_t> TextureAtlas::GetTileCoord(const TextureID& id) const {
    auto it = m_tileCoords.find(id);
    if (it == m_tileCoords.end()) {
        Logger::Log(LogLevel::Warning, "Renderer",
            "TextureAtlas: GetTileCoord() called with unregistered TextureID \"" +
            id.ToString() + "\" — did you forget to add it to CollectRequiredTextures()? "
            "Returning tile (0,0).");
        return { 0, 0 };
    }
    return it->second;
}

void TextureAtlas::WriteDescriptorSet(VkDescriptorSet set) const {
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_sampler;
    imageInfo.imageView = m_imageView;
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
 
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
 
    vkUpdateDescriptorSets(m_context->GetDevice(), 1, &write, 0, nullptr);
}
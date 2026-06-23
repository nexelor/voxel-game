#include "TextureAtlas.hpp"
#include "VulkanContext.hpp"
#include "engine/core/Logger.hpp"
#include "engine/renderer/TextureID.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>

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
/// Cache - hash, load, save
///

uint64_t TextureAtlas::ComputeInputHash(const std::string& textureRootDir, const std::vector<TextureID>& required) const {
    std::vector<const TextureID*> sorted;
    sorted.reserve(required.size());
    for (const auto& id : required) sorted.push_back(&id);
    std::sort(sorted.begin(), sorted.end(), [](const TextureID* a, const TextureID* b) {
        return a->ToString() < b->ToString();
    });

    constexpr uint64_t kOffset = 14695981039346656037ULL;
    constexpr uint64_t kPrime  = 1099511628211ULL;
    uint64_t hash = kOffset;
    auto feed = [&](const void* data, size_t len) {
        const auto* b = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ++i) { hash ^= b[i]; hash *= kPrime; }
    };

    for (const TextureID* id : sorted) {
        const std::string s = id->ToString();
        feed(s.data(), s.size());

        const std::string path = textureRootDir + "/" + id->RelativePath();
        std::ifstream f(path, std::ios::binary);
        if (f) {
            const std::vector<char> bytes(std::istreambuf_iterator<char>(f), {});
            feed(bytes.data(), bytes.size());
        }
    }
    return hash;
}

// Cache file format:
//   [8]  magic "VATLAS\x01\x00"
//   [8]  hash uint64
//   [4]  gridCols, [4] gridRows, [4] tileSize, [4] tileCount
//   per tile: [2] nsLen, [nsLen], [2] nameLen, [nameLen], [4] col, [4] row
//   raw RGBA pixels (gridCols*tileSize * gridRows*tileSize * 4 bytes)
bool TextureAtlas::TryLoadCache(const std::string& cachePath, uint64_t expectedHash,
    std::vector<uint8_t>& outPixels)
{
    std::ifstream f(cachePath, std::ios::binary);
    if (!f) return false;

    auto readU16 = [&]{ uint16_t v=0; f.read(reinterpret_cast<char*>(&v), 2); return v; };
    auto readU32 = [&]{ uint32_t v=0; f.read(reinterpret_cast<char*>(&v), 4); return v; };
    auto readU64 = [&]{ uint64_t v=0; f.read(reinterpret_cast<char*>(&v), 8); return v; };
    auto readStr = [&](uint16_t len) -> std::string {
        std::string s(len, '\0'); f.read(s.data(), len); return s;
    };

    char magic[8];
    f.read(magic, 8);
    if (std::memcmp(magic, "VATLAS\x01\x00", 8) != 0) {
        Logger::Log(LogLevel::Warning, "Renderer", "TextureAtlas: cache has wrong magic, ignoring");
        return false;
    }

    if (readU64() != expectedHash) {
        Logger::Log(LogLevel::Info, "Renderer", "TextureAtlas: cache outdated, rebuilding");
        return false;
    }

    const uint32_t gridCols  = readU32();
    const uint32_t gridRows  = readU32();
    const uint32_t tileSize  = readU32();
    const uint32_t tileCount = readU32();
    if (!f) return false;

    m_tileCoords.clear();
    m_tileCoords.reserve(tileCount);
    for (uint32_t i = 0; i < tileCount; ++i) {
        const std::string ns   = readStr(readU16());
        const std::string name = readStr(readU16());
        const uint32_t col = readU32();
        const uint32_t row = readU32();
        if (!f) return false;
        m_tileCoords[TextureID(ns, name)] = { col, row };
    }

    const size_t pixBytes = static_cast<size_t>(gridCols) * gridRows * tileSize * tileSize * 4;
    outPixels.resize(pixBytes);
    f.read(reinterpret_cast<char*>(outPixels.data()), static_cast<std::streamsize>(pixBytes));
    if (!f || static_cast<size_t>(f.gcount()) != pixBytes) {
        Logger::Log(LogLevel::Warning, "Renderer", "TextureAtlas: cache truncated, rebuilding");
        m_tileCoords.clear();
        return false;
    }

    m_gridCols = gridCols;
    m_gridRows = gridRows;
    m_tileSize = tileSize;
    Logger::Log(LogLevel::Info, "Renderer", "TextureAtlas: loaded from cache");
    return true;
}

void TextureAtlas::TrySaveCache(const std::string& cachePath, uint64_t hash,
    const std::vector<uint8_t>& pixels) const
{
    try { std::filesystem::create_directories(std::filesystem::path(cachePath).parent_path()); }
    catch (...) {
        Logger::Log(LogLevel::Warning, "Renderer", "TextureAtlas: could not create cache dir");
        return;
    }

    std::ofstream f(cachePath, std::ios::binary);
    if (!f) {
        Logger::Log(LogLevel::Warning, "Renderer", "TextureAtlas: could not write cache to " + cachePath);
        return;
    }

    auto writeU16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    auto writeU32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU64 = [&](uint64_t v) { f.write(reinterpret_cast<const char*>(&v), 8); };
    auto writeStr = [&](const std::string& s) {
        writeU16(static_cast<uint16_t>(s.size()));
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
    };

    f.write("VATLAS\x01\x00", 8);
    writeU64(hash);
    writeU32(m_gridCols);
    writeU32(m_gridRows);
    writeU32(m_tileSize);
    writeU32(static_cast<uint32_t>(m_tileCoords.size()));

    for (const auto& [id, coord] : m_tileCoords) {
        writeStr(id.ns);
        writeStr(id.name);
        writeU32(coord.first);
        writeU32(coord.second);
    }

    f.write(reinterpret_cast<const char*>(pixels.data()),
            static_cast<std::streamsize>(pixels.size()));

    Logger::Log(LogLevel::Info, "Renderer", "TextureAtlas: cache saved to " + cachePath);
}

///
/// UploadPixels — CPU buffer → GPU device-local VkImage
///

void TextureAtlas::UploadPixels(const std::vector<uint8_t>& pixels, uint32_t atlasW, uint32_t atlasH) {
    VkDevice        device  = m_context->GetDevice();
    VkPhysicalDevice physDev = m_context->GetPhysicalDevice();

    VkQueue          queue   = m_context->GetGraphicsQueue();

    const VkDeviceSize dataSize = static_cast<VkDeviceSize>(pixels.size());

    VkBuffer       stagingBuf;
    VkDeviceMemory stagingMem;
    {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = dataSize;
        info.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &info, nullptr, &stagingBuf) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create staging buffer");

        VkMemoryRequirements reqs;
        vkGetBufferMemoryRequirements(device, stagingBuf, &reqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = reqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMem) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to allocate staging memory");
        vkBindBufferMemory(device, stagingBuf, stagingMem, 0);

        void* ptr;
        vkMapMemory(device, stagingMem, 0, dataSize, 0, &ptr);
        std::memcpy(ptr, pixels.data(), static_cast<size_t>(dataSize));
        vkUnmapMemory(device, stagingMem);
    }

    // Destroy any previous atlas GPU resources before (re)creating them.
    if (m_imageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
    if (m_image     != VK_NULL_HANDLE) { vkDestroyImage    (device, m_image,     nullptr); m_image     = VK_NULL_HANDLE; }
    if (m_memory    != VK_NULL_HANDLE) { vkFreeMemory      (device, m_memory,    nullptr); m_memory    = VK_NULL_HANDLE; }

    {
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_R8G8B8A8_SRGB;
        info.extent        = { atlasW, atlasH, 1 };
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device, &info, nullptr, &m_image) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create atlas image");

        VkMemoryRequirements reqs;
        vkGetImageMemoryRequirements(device, m_image, &reqs);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = reqs.size;
        allocInfo.memoryTypeIndex = FindMemoryType(physDev, reqs.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_memory) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to allocate staging memory");
        vkBindImageMemory(device, m_image, m_memory, 0);
    }

    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = m_pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        VkCommandBuffer cmd;
        vkAllocateCommandBuffers(device, &allocInfo, &cmd);

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &begin);

        {
            VkImageMemoryBarrier barrier{};
            barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = m_image;
            barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask       = 0;
            barrier.dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent      = { atlasW, atlasH, 1 };
        vkCmdCopyBufferToImage(cmd, stagingBuf, m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        {
            VkImageMemoryBarrier barrier{};
            barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image               = m_image;
            barrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
        vkFreeCommandBuffers(device, m_pool, 1, &cmd);
    }

    vkDestroyBuffer(device, stagingBuf, nullptr);
    vkFreeMemory   (device, stagingMem, nullptr);

    {
        VkImageViewCreateInfo info{};
        info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image            = m_image;
        info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        info.format           = VK_FORMAT_R8G8B8A8_SRGB;
        info.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &info, nullptr, &m_imageView) != VK_SUCCESS)
            throw std::runtime_error("TextureAtlas: failed to create atlas image view");
    }
}

///
/// Build — hash check → cache or full load → GPU upload
///

void TextureAtlas::Build(const std::string& textureRootDir, const std::vector<TextureID>& required) {
    if (required.empty())
        throw std::runtime_error("TextureAtlas::Build: no textures requested — nothing to pack");

    static constexpr const char* kCachePath = "cache/atlas.bin";

    const uint64_t hash = ComputeInputHash(textureRootDir, required);

    std::vector<uint8_t> atlasPixels;

    if (!TryLoadCache(kCachePath, hash, atlasPixels)) {
        // ── Full load + pack ──────────────────────────────────────────────
        uint32_t tileSize = 0;
        std::vector<DecodedImage> images = LoadAll(textureRootDir, required, tileSize);

        m_tileSize = tileSize;

        const uint32_t tileCount = static_cast<uint32_t>(images.size());
        const uint32_t gridSide  = NextPowerOfTwo(
            static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(tileCount)))));
        m_gridCols = gridSide;
        m_gridRows = gridSide;

        const uint32_t atlasW = m_gridCols * m_tileSize;
        const uint32_t atlasH = m_gridRows * m_tileSize;

        Logger::Log(LogLevel::Info, "Renderer",
            "TextureAtlas: packing " + std::to_string(tileCount) + " tile(s) of " +
            std::to_string(m_tileSize) + "x" + std::to_string(m_tileSize) +
            " into a " + std::to_string(m_gridCols) + "x" + std::to_string(m_gridRows) +
            " grid (" + std::to_string(atlasW) + "x" + std::to_string(atlasH) + " px)");

        atlasPixels.assign(static_cast<size_t>(atlasW) * atlasH * 4, 0);
        m_tileCoords.clear();
        m_tileCoords.reserve(images.size());

        for (uint32_t i = 0; i < tileCount; ++i) {
            const uint32_t col = i % m_gridCols;
            const uint32_t row = i / m_gridCols;
            m_tileCoords[images[i].id] = { col, row };

            const uint32_t originX = col * m_tileSize;
            const uint32_t originY = row * m_tileSize;
            for (uint32_t y = 0; y < m_tileSize; ++y) {
                const uint8_t* srcRow = &images[i].pixels[static_cast<size_t>(y) * m_tileSize * 4];
                uint8_t*       dstRow = &atlasPixels[(static_cast<size_t>(originY + y) * atlasW + originX) * 4];
                std::memcpy(dstRow, srcRow, static_cast<size_t>(m_tileSize) * 4);
            }
        }

        TrySaveCache(kCachePath, hash, atlasPixels);
    }

    const uint32_t atlasW = m_gridCols * m_tileSize;
    const uint32_t atlasH = m_gridRows * m_tileSize;

    UploadPixels(atlasPixels, atlasW, atlasH);

    if (m_sampler == VK_NULL_HANDLE)
        CreateSampler();

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
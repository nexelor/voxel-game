#pragma once

#include "engine/renderer/TextureID.hpp"
 
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class VulkanContext;

// ─────────────────────────────────────────────
//  TextureAtlas
//
//  Owns a single VkImage packed with every block
//  texture the game needs, built fresh at startup
//  from PNG files on disk.
//
//  Directory layout expected (see content/blocks/Blocks.hpp
//  for how IDs map to paths):
//
//      content/assets/textures/<namespace>/<name>.png
//
//  e.g. TextureID("minecraft:stone") -> content/assets/textures/minecraft/stone.png
//
//  Usage (see Application::Initialize):
//
//      TextureAtlas atlas(context, pool);
//      atlas.Build("content/assets/textures", BlockRegistry::Get().CollectRequiredTextures());
//      atlas.WriteDescriptorSet(set);
//
//      BlockRegistry::Get().ResolveAtlasCoords(
//          [&](const TextureID& id) { return atlas.GetTileCoord(id); });
//
//  All textures MUST be the same pixel size (e.g. all 16x16).
//  This matches Minecraft's own atlas convention and keeps the
//  packing trivial: a uniform grid, no rectangle-packing needed.
//  Build() throws if it finds a mismatched size — better to fail
//  loudly at startup than silently misrender.
//
//  Adding more textures later requires NO code changes here —
//  just drop the PNG in the right namespace folder and reference
//  its TextureID from a BlockDef (or anything else that wants a
//  tile in this atlas).
// ─────────────────────────────────────────────

class TextureAtlas {
public:
    TextureAtlas(VulkanContext* context, VkCommandPool pool);
    ~TextureAtlas();

    TextureAtlas(const TextureAtlas&) = delete;
    TextureAtlas& operator=(const TextureAtlas&) = delete;

    // Loads every TextureID in `required` from `textureRootDir`, packs them
    // into a single square atlas, and uploads it as one VkImage.
    // Throws std::runtime_error on any missing file, decode failure, or
    // tile-size mismatch (logged with enough detail to find the bad file).
    void Build(const std::string& textureRootDir, const std::vector<TextureID>& required);
    
    // Write the image into an already-allocated descriptor set (set 1).
    void WriteDescriptorSet(VkDescriptorSet set) const;
 
    bool IsReady() const { return m_imageView != VK_NULL_HANDLE; }
 
    // Atlas grid dimensions, in tiles. Needed by the mesher/shader to
    // convert a tile coordinate into normalized UV sub-rectangles.
    uint32_t GetGridCols() const { return m_gridCols; }
    uint32_t GetGridRows() const { return m_gridRows; }
 
    // Tile coordinate (col, row) for a previously-Build()-loaded texture.
    // Returns {0, 0} and logs a warning if the ID was never loaded —
    // callers get a visibly-wrong (but non-crashing) tile rather than UB,
    // which is enough to spot a typo'd TextureID immediately in-game.
    std::pair<uint32_t, uint32_t> GetTileCoord(const TextureID& id) const;

private:
    struct DecodedImage {
        TextureID id;
        std::vector<uint8_t> pixels; // RGBA8, tileSize*tileSize*4 bytes
    };

    std::vector<DecodedImage> LoadAll(const std::string& textureRootDir,
        const std::vector<TextureID>& required, uint32_t& outTileSize) const;

    // Cache helpers — cache/atlas.bin stores packed pixels + metadata keyed by
    // an FNV-1a hash of sorted input PNG file contents.
    uint64_t ComputeInputHash(const std::string& textureRootDir, const std::vector<TextureID>& required) const;
    bool TryLoadCache(const std::string& cachePath, uint64_t expectedHash, std::vector<uint8_t>& outPixels);
    void TrySaveCache(const std::string& cachePath, uint64_t hash, const std::vector<uint8_t>& pixels) const;

    // Stages atlasPixels CPU buffer to a new device-local VkImage.
    void UploadPixels(const std::vector<uint8_t>& pixels, uint32_t atlasWm, uint32_t atlasH);

    void CreateSampler();

    static uint32_t FindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags props);
    static uint32_t NextPowerOfTwo(uint32_t v);

    // Generates a Minecraft-style magenta/black checkerboard, RGBA8,
    // tileSize*tileSize*4 bytes — used in place of any texture that's
    // missing, fails to decode, or mismatches the atlas's locked tile size.
    static std::vector<uint8_t> MakeMissingTexture(uint32_t tileSize);

    VulkanContext* m_context = nullptr;
    VkCommandPool m_pool = VK_NULL_HANDLE;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    uint32_t m_tileSize = 0;   // pixels per tile edge (e.g. 16)
    uint32_t m_gridCols = 0;   // tiles per row
    uint32_t m_gridRows = 0;   // tile rows

    std::unordered_map<TextureID, std::pair<uint32_t, uint32_t>, TextureIDHash> m_tileCoords;
};
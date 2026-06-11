#pragma once

#include <vulkan/vulkan.h>

class VulkanContext;

// ─────────────────────────────────────────────
//  TextureAtlas
//
//  Owns a single VkImage (the block atlas) and
//  the associated sampler / image view.
//
//  Two construction modes:
//   1. LoadFromFile(path)  — load a real PNG/BMP
//      (requires a future image-loading backend;
//       currently a placeholder that produces a
//       1×1 white pixel so you can see geometry).
//   2. CreateSolid(r,g,b) — 1×1 colour; handy
//      for debugging faces.
//
//  After construction call WriteDescriptorSet()
//  to bind the atlas into set 1 of the pipeline.
// ─────────────────────────────────────────────

class TextureAtlas {
public:
    TextureAtlas(VulkanContext* context, VkCommandPool pool);
    ~TextureAtlas();

    // Create a tiny solid-colour atlas so the pipeline can draw
    // without needing a real image on disk.
    void CreateSolid(uint8_t r = 255, uint8_t g = 255, uint8_t b = 255);

    // Write the image into an already-allocated descriptor set (set 1).
    void WriteDescriptorSet(VkDescriptorSet set) const;

    bool IsReady() const { return m_imageView != VK_NULL_HANDLE; }

private:
    void CreateSampler();

    static uint32_t FindMemoryType(VkPhysicalDevice physDevice, uint32_t typeFilter, VkMemoryPropertyFlags props);

    VulkanContext* m_context = nullptr;
    VkCommandPool m_pool = VK_NULL_HANDLE;

    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};
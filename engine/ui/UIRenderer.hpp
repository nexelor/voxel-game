#pragma once
 
#include "engine/ui/BitmapFont.hpp"
#include "engine/ui/UIVertex.hpp"
#include "engine/renderer/GraphicsPipeline.hpp"
 
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
 
class VulkanContext;
 
// ─────────────────────────────────────────────
//  UIRenderer
//
//  Batches 2D draw calls (text, filled quads,
//  outlined quads) into a single dynamic vertex
//  buffer and draws them with a dedicated
//  pipeline at the end of each frame.
//
//  Coordinate system:
//    Origin (0,0) = top-left of the window.
//    X grows right, Y grows down.
//    Units = screen pixels.
//
//  Usage per frame:
//    1. renderer.BeginFrame(screenW, screenH)
//    2. renderer.DrawText(...)  / DrawQuad(...)
//    3. renderer.EndFrame(cmd)
//
//  The font texture is a 128×FONT_CHAR_H image
//  with every ASCII glyph laid out in one row.
//  A 1×1 white pixel occupies uv (0,0)–(1/128,1)
//  so solid quads need no special atlas tile.
// ─────────────────────────────────────────────

class UIRenderer {
public:
    UIRenderer(VulkanContext* context, VkRenderPass renderPass, VkCommandPool pool);
    ~UIRenderer();

    // Call once after construction to build the font texture + pipeline.
    void Init();
    void Cleanup();

    // Per-frame API

    // Reset the CPU-side vertex/index lists.
    // Pass the current framebuffer dimensions so text is positioned correctly.
    void BeginFrame(float sreenW, float screenH);

    // Flush all buffered geometry into the command buffer.
    // Must be called inside an active render pass.
    void EndFrame(VkCommandBuffer cmd);

    // Draw Call

    // Draw a filled rectangle.
    // color is packed RGBA8 (use UIVertex::PackColor).
    void DrawQuad(float x, float y, float w, float h, uint32_t color);

    // Draw a single line of text at pixel position (x, y).
    // scale: 1 = 8px, 2 = 16px, etc.
    void DrawText(const std::string& text, float x, float y, uint32_t color, int scale = 2);
 
    // Convenience: white text
    void DrawText(const std::string& text, float x, float y, int scale = 2) {
        DrawText(text, x, y, UIVertex::PackColor(1.f, 1.f, 1.f), scale);
    }
 
    // Draw text with a dark drop-shadow (classic MC style).
    void DrawTextShadowed(const std::string& text, float x, float y, uint32_t color = UIVertex::PackColor(1.f, 1.f, 1.f), int scale = 2);

private:
    // Vulkan setup
    void CreateFontTexture();
    void CreateFontDescriptorLayout();
    void CreateFontDescriptorSet();
    void CreatePipeline();
    void CreateOrResizeDynamicBuffers(size_t vertCount, size_t idxCount);

    static uint32_t FindMemoryType(VkPhysicalDevice, uint32_t filter, VkMemoryPropertyFlags);

    VulkanContext* m_context = nullptr;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkCommandPool m_pool = VK_NULL_HANDLE;
 
    // Font atlas texture (128 × FONT_CHAR_H, R8_UNORM)
    VkImage m_fontImage = VK_NULL_HANDLE;
    VkDeviceMemory m_fontMemory = VK_NULL_HANDLE;
    VkImageView m_fontView = VK_NULL_HANDLE;
    VkSampler m_fontSampler = VK_NULL_HANDLE;
 
    // Descriptor set for the font texture
    VkDescriptorSetLayout m_descLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet = VK_NULL_HANDLE;

    // Font atlas dimensions
    // 128 glyphs across + 1 white pixel column on the far left
    static constexpr int ATLAS_COLS = FONT_COUNT;   // 95 glyphs
    static constexpr int ATLAS_W = ATLAS_COLS * FONT_CHAR_W + FONT_CHAR_W; // +8 for white tile
    static constexpr int ATLAS_H = FONT_CHAR_H;
 
    // UV of the solid-white 1×1 tile (leftmost glyph column)
    static constexpr float WHITE_U = 0.0f;
    static constexpr float WHITE_V = 0.0f;
    static constexpr float WHITE_DU = static_cast<float>(FONT_CHAR_W) / ATLAS_W;
    static constexpr float WHITE_DV = 1.0f;

    // Pipeline
    GraphicsPipeline m_pipeline;

    // Dynamic vertex + index buffers (grown as needed)
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    size_t m_vertexCapacity = 0;
    
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    size_t m_indexCapacity = 0;

    // CPU-side batch
    std::vector<UIVertex> m_vertices;
    std::vector<uint32_t> m_indices;

    float m_screenW = 1280.f;
    float m_screenH = 720.f;

    bool m_initialized = false;
};
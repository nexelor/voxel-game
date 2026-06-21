#pragma once
 
#include <vulkan/vulkan.h>
#include <array>
#include <glm/glm.hpp>
 
// ─────────────────────────────────────────────
//  UIVertex  — 20 bytes
//
//  position : screen-space pixels, origin top-left.
//             The UI pipeline converts to NDC via
//             a push-constant viewport size.
//  uv       : [0,1]² into the font texture atlas.
//  color    : RGBA8 packed as uint32 (R in LSB).
//
//  All UI geometry (text, solid quads) shares
//  this layout.  Solid quads use uv=(0,0) and
//  rely on a 1×1 white pixel in the font texture.
// ─────────────────────────────────────────────

struct UIVertex {
    // total: 20 bytes
    glm::vec2 position; // 8 bytes - location 0
    glm::vec2 uv; // 8 bytes - location 1
    uint32_t color; // 4 bytes - location 2 packed RGBA
    
    static VkVertexInputBindingDescription BindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding = 0;
        desc.stride = sizeof(UIVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 3> AttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attrs{};

        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[0].offset = offsetof(UIVertex, position);

        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[1].offset = offsetof(UIVertex, uv);

        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32_UINT;
        attrs[2].offset = offsetof(UIVertex, color);

        return attrs;
    }

    // Convenience: pack four [0,1] floats into the uint32 color field.
    static uint32_t PackColor(float r, float g, float b, float a = 1.0f) {
        const auto rb = static_cast<uint32_t>(r* 255.f) & 0xFF;
        const auto gb = static_cast<uint32_t>(g* 255.f) & 0xFF;
        const auto bb = static_cast<uint32_t>(b* 255.f) & 0xFF;
        const auto ab = static_cast<uint32_t>(a* 255.f) & 0xFF;
        return rb | (gb << 8) | (bb << 16) | (ab << 24);
    }
    
    static uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
        return static_cast<uint32_t>(r) | static_cast<uint32_t>(g << 8) |
            static_cast<uint32_t>(b << 16) | static_cast<uint32_t>(a << 24);
    }
};

// ─────────────────────────────────────────────
//  UIPushConstants  (8 bytes, well within the
//  guaranteed 128-byte Vulkan minimum)
// ─────────────────────────────────────────────
 
struct UIPushConstants {
    float screenWidth;
    float screenHeight;
};
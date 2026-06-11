#pragma once
 
#include <vulkan/vulkan.h>
#include <array>
#include <glm/glm.hpp>
 
// ─────────────────────────────────────────────
//  VoxelVertex  — 28 bytes, std140-friendly
//
//  faceIndex and ao are sent as full uint32
//  (VK_FORMAT_R32_UINT) because R8_UINT is not
//  guaranteed to have VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT.
//  The 8 bytes of "waste" vs the packed version
//  are negligible — vertex buffers are already
//  large and cache-line aligned.
// ─────────────────────────────────────────────

struct VoxelVertex {
    glm::vec3 position;   // 12 bytes  location 0
    glm::vec2 uv;         //  8 bytes  location 1
    uint32_t  faceIndex;  //  4 bytes  location 2  (0=+X 1=-X 2=+Y 3=-Y 4=+Z 5=-Z)
    uint32_t  ao;         //  4 bytes  location 3  (0=dark .. 3=bright)
                          // total: 28 bytes

    static VkVertexInputBindingDescription BindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding   = 0;
        desc.stride    = sizeof(VoxelVertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 4> AttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 4> attrs{};

        attrs[0].binding = 0;
        attrs[0].location = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;   // vec3
        attrs[0].offset = offsetof(VoxelVertex, position);

        attrs[1].binding = 0;
        attrs[1].location = 1;
        attrs[1].format = VK_FORMAT_R32G32_SFLOAT;      // vec2
        attrs[1].offset = offsetof(VoxelVertex, uv);

        attrs[2].binding = 0;
        attrs[2].location = 2;
        attrs[2].format = VK_FORMAT_R32_UINT;            // uint32 — universally supported
        attrs[2].offset = offsetof(VoxelVertex, faceIndex);

        attrs[3].binding = 0;
        attrs[3].location = 3;
        attrs[3].format = VK_FORMAT_R32_UINT;            // uint32
        attrs[3].offset = offsetof(VoxelVertex, ao);

        return attrs;
    }
};

// ─────────────────────────────────────────────
//  VoxelPushConstants
//
//  Per-draw data pushed directly without a descriptor set.
//  Cheap for per-chunk model matrices + atlas metadata.
//
//  Vulkan guarantees at least 128 bytes of push constant space.
//  This struct is 80 bytes - safe everywhere.
// ─────────────────────────────────────────────

struct VoxelPushConstants {
    glm::mat4 model;        // 64 bytes - chunk-to-world transform
    float     atlasRows;    // 4  bytes - number of tile rows in texture atlas
    float     atlasCols;    // 4  bytes - number of tile columns in texture atlas
    float     _pad[2];      // 8  bytes  padding to 80 bytes total
};

static_assert(sizeof(VoxelPushConstants) <= 128, "VoxelPushConstants exceeds guaranteed Vulkan push constant space");
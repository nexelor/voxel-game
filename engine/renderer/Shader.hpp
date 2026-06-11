#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>
 
// ─────────────────────────────────────────────
//  Shader stage wrapper
//
//  Loads a pre-compiled SPIR-V file from disk,
//  owns the VkShaderModule lifetime, and exposes
//  a ready-to-use VkPipelineShaderStageCreateInfo.
//
//  Usage:
//      Shader vert(device, "shaders/voxel.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
//      Shader frag(device, "shaders/voxel.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
//      auto stages = { vert.StageInfo(), frag.StageInfo() };
// ─────────────────────────────────────────────

class Shader {
public:
    Shader(VkDevice device, const std::string& spirvPath, VkShaderStageFlagBits stage);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    VkPipelineShaderStageCreateInfo StageInfo() const;
    VkShaderStageFlagBits Stage() const { return m_stage; }
    bool IsValid() const { return m_module != VK_NULL_HANDLE; }

private:
    static std::vector<char> ReadSPIRV(const std::string& path);

    VkDevice m_device = VK_NULL_HANDLE;
    VkShaderModule m_module = VK_NULL_HANDLE;
    VkShaderStageFlagBits m_stage;
};
#pragma once
 
#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <vulkan/vulkan_core.h>
 
// ─────────────────────────────────────────────
//  GraphicsPipeline
//
//  Builder-pattern wrapper around VkPipeline.
//  Designed so different pipeline types (voxel,
//  water, UI, skybox) each get their own
//  PipelineBuilder call chain, without sharing
//  mutable state.
//
//  Usage:
//      auto pipeline = GraphicsPipeline::Builder(device, renderPass)
//          .SetShaders(vertStage, fragStage)
//          .SetVertexInput(binding, attributes)
//          .SetDepthTest(true)
//          .SetCullMode(VK_CULL_MODE_BACK_BIT)
//          .AddPushConstantRange<VoxelPushConstants>(VK_SHADER_STAGE_VERTEX_BIT)
//          .AddDescriptorSetLayout(globalLayout)
//          .Build();
// ─────────────────────────────────────────────

class GraphicsPipeline {
public:
    VkPipeline GetPipeline() const { return m_pipeline; };
    VkPipelineLayout GetLayout() const { return m_layout; };
    bool IsValid() const { return m_pipeline != VK_NULL_HANDLE; };

    void Destroy(VkDevice device);

private:
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;

public:
    class Builder {
    public:
        Builder(VkDevice device, VkRenderPass renderPass, uint32_t subpass = 0);

        // Shader stages (call once per stage)
        Builder& AddShaderStage(VkPipelineShaderStageCreateInfo stage);

        // Vertex input
        Builder& SetVertexInput(VkVertexInputBindingDescription binding, const std::vector<VkVertexInputAttributeDescription>& attributes);

        // Depth / stencil
        Builder& SetDepthTest(bool enableTest, bool enableWrite = true, VkCompareOp compareOp = VK_COMPARE_OP_LESS);

        // Rasterisation
        Builder& SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE);
        Builder& SetPolygonMode(VkPolygonMode mode);
        Builder& SetLineWidth(float width);

        // Blending
        Builder& SetBlending(bool enable);

        // Push constants
        template<typename T>
        Builder& AddPushConstantRange(VkShaderStageFlags stages, uint32_t offset = 0) {
            VkPushConstantRange range{};
            range.stageFlags = stages;
            range.offset = offset;
            range.size = sizeof(T);
            m_pushConstantRanges.push_back(range);
            return *this;
        }

        // Descriptor set layouts (in set index order: 0, 1, 2...)
        Builder& AddDescriptorSetLayout(VkDescriptorSetLayout layout);

        // Dynamic state
        Builder& AddDynamicState(VkDynamicState state);

        // Topology
        Builder& SetTopology(VkPrimitiveTopology topology);

        // Build
        GraphicsPipeline Build() const;

    private:
        VkDevice m_device;
        VkRenderPass m_renderPass;
        uint32_t m_subpass;

        std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;
        std::optional<VkVertexInputBindingDescription> m_bindingDesc;
        std::vector<VkVertexInputAttributeDescription> m_attributeDescs;
        std::vector<VkPushConstantRange> m_pushConstantRanges;
        std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
        std::vector<VkDynamicState> m_dynamicStates;

        // State with sensible defaults
        bool m_depthTestEnable = true;
        bool m_depthWriteEnable = true;
        VkCompareOp m_depthCompareOp = VK_COMPARE_OP_LESS;

        VkCullModeFlags m_cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace m_frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        VkPolygonMode m_polyMode = VK_POLYGON_MODE_FILL;
        float m_lineWidth = 1.0f;

        bool m_blendEnable = false;

        VkPrimitiveTopology m_topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    };
};
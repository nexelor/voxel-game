#include "GraphicsPipeline.hpp"
#include "engine/core/Logger.hpp"
 
#include <stdexcept>
#include <vulkan/vulkan_core.h>

void GraphicsPipeline::Destroy(VkDevice device) {
    if (m_layout   != VK_NULL_HANDLE) vkDestroyPipelineLayout(device, m_layout,   nullptr);
    if (m_pipeline != VK_NULL_HANDLE) vkDestroyPipeline      (device, m_pipeline, nullptr);
    m_layout   = VK_NULL_HANDLE;
    m_pipeline = VK_NULL_HANDLE;
}

GraphicsPipeline::Builder::Builder(VkDevice device, VkRenderPass renderPass, uint32_t subpass)
    : m_device(device), m_renderPass(renderPass), m_subpass(subpass)
{
    // Always add viewport and scissor as dynamic — means we don't have to
    // recreate the pipeline when the window is resized.
    m_dynamicStates.push_back(VK_DYNAMIC_STATE_VIEWPORT);
    m_dynamicStates.push_back(VK_DYNAMIC_STATE_SCISSOR);
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::AddShaderStage(VkPipelineShaderStageCreateInfo stage) {
    m_shaderStages.push_back(stage);
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetVertexInput(
    VkVertexInputBindingDescription binding,
    const std::vector<VkVertexInputAttributeDescription>& attributes)
{
    m_bindingDesc    = binding;
    m_attributeDescs = attributes;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetDepthTest(
    bool enableTest, bool enableWrite, VkCompareOp compareOp)
{
    m_depthTestEnable  = enableTest;
    m_depthWriteEnable = enableWrite;
    m_depthCompareOp   = compareOp;
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetCullMode(
    VkCullModeFlags cullMode, VkFrontFace frontFace)
{
    m_cullMode   = cullMode;
    m_frontFace  = frontFace;
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetPolygonMode(VkPolygonMode mode) {
    m_polyMode = mode;
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetLineWidth(float width) {
    m_lineWidth = width;
    return *this;
}

GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetBlending(bool enable) {
    m_blendEnable = enable;
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::AddDescriptorSetLayout(
    VkDescriptorSetLayout layout)
{
    m_descriptorSetLayouts.push_back(layout);
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::AddDynamicState(VkDynamicState state) {
    m_dynamicStates.push_back(state);
    return *this;
}
 
GraphicsPipeline::Builder& GraphicsPipeline::Builder::SetTopology(VkPrimitiveTopology topology) {
    m_topology = topology;
    return *this;
}

GraphicsPipeline GraphicsPipeline::Builder::Build() const {
    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
 
    if (m_bindingDesc.has_value()) {
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &m_bindingDesc.value();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(m_attributeDescs.size());
        vertexInputInfo.pVertexAttributeDescriptions = m_attributeDescs.data();
    }

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = m_topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport / scissor (dynamic, so counts matter but values don't)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterisation
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = m_polyMode;
    rasterizer.lineWidth = m_lineWidth;
    rasterizer.cullMode = m_cullMode;
    rasterizer.frontFace = m_frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling (off - add MSAA later if needed)
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth / stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = m_depthTestEnable  ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = m_depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = m_depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Colour blend attachment
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    if (m_blendEnable) {
        // Standard alpha blending: out = src.a * src + (1 - src.a) * dst
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    } else {
        colorBlendAttachment.blendEnable = VK_FALSE;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(m_dynamicStates.size());
    dynamicState.pDynamicStates = m_dynamicStates.data();

    // Pipeline layout
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(m_descriptorSetLayouts.size());
    
    layoutInfo.pSetLayouts = m_descriptorSetLayouts.empty() ? nullptr : m_descriptorSetLayouts.data();
    
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_pushConstantRanges.size());
    
    layoutInfo.pPushConstantRanges    = m_pushConstantRanges.empty() ? nullptr : m_pushConstantRanges.data();
 
    GraphicsPipeline result;

    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &result.m_layout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create pipeline layout");

    // Pipeline itself
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(m_shaderStages.size());
    pipelineInfo.pStages = m_shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = result.m_layout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = m_subpass;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1,
        &pipelineInfo, nullptr, &result.m_pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(m_device, result.m_layout, nullptr);
        result.m_layout = VK_NULL_HANDLE;
        throw std::runtime_error("Failed to create graphics pipeline");
    }
 
    Logger::Log(LogLevel::Info, "Renderer", "Graphics pipeline created");
    return result;
}
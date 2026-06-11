#include "Shader.hpp"
#include "engine/core/Logger.hpp"
 
#include <fstream>
#include <stdexcept>

Shader::Shader(VkDevice device, const std::string& spirvPath, VkShaderStageFlagBits stage)
    : m_device(device), m_stage(stage)
{
    auto code = ReadSPIRV(spirvPath);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());
 
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &m_module) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shader module: " + spirvPath);
 
    Logger::Log(LogLevel::Info, "Shader", "Loaded: " + spirvPath);
}

Shader::~Shader() {
    if (m_module != VK_NULL_HANDLE)
        vkDestroyShaderModule(m_device, m_module, nullptr);
}
 
Shader::Shader(Shader&& other) noexcept
    : m_device(other.m_device), m_module(other.m_module), m_stage(other.m_stage)
{
    other.m_module = VK_NULL_HANDLE;
}
 
Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) {
        if (m_module != VK_NULL_HANDLE)
            vkDestroyShaderModule(m_device, m_module, nullptr);
        m_device       = other.m_device;
        m_module       = other.m_module;
        m_stage        = other.m_stage;
        other.m_module = VK_NULL_HANDLE;
    }
    return *this;
}

VkPipelineShaderStageCreateInfo Shader::StageInfo() const {
    VkPipelineShaderStageCreateInfo info{};
    info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage  = m_stage;
    info.module = m_module;
    info.pName  = "main";
    return info;
}

std::vector<char> Shader::ReadSPIRV(const std::string& path) {
    // ate = start at end so tellg() gives file size directly
    std::ifstream file(path, std::ios::binary | std::ios::ate);
 
    if (!file.is_open())
        throw std::runtime_error("Cannot open SPIR-V file: " + path);
 
    const std::streamsize size = file.tellg();
    if (size <= 0 || size % 4 != 0)
        throw std::runtime_error("SPIR-V file is empty or misaligned: " + path);
 
    std::vector<char> buffer(static_cast<size_t>(size));
    file.seekg(0);
    file.read(buffer.data(), size);
 
    return buffer;
}
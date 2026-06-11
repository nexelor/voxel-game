#pragma once

#include "engine/platform/Window.hpp"

#include <optional>
#include <vulkan/vulkan.h>
#include <vector>

struct QueueFamilyIndices
{
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool IsComplete() const
    {
        return
            graphicsFamily.has_value() &&
            presentFamily.has_value();
    }
};

class VulkanContext {
public:
    VulkanContext(Window* window);
    ~VulkanContext();

    void Init();
    void Cleanup();

    VkDevice GetDevice() const;
    VkPhysicalDevice GetPhysicalDevice() const;
    VkSurfaceKHR GetSurface() const;

    VkQueue GetGraphicsQueue() const;
    VkQueue GetPresentQueue() const;
    uint32_t GetGraphicsFamily() const;

private:
    void CreateInstance();
    void SetupDebugMessenger();
    void CreateSurface();
    void PickPhysicalDevice();
    void CreateLogicalDevice();

    bool CheckValidationLayerSupport();
    std::vector<const char*> GetRequiredExtensions();

    QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);

    bool IsDeviceSuitable(VkPhysicalDevice device);
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

private:
    Window* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;

    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;

    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    const std::vector<const char*> m_validationLayers = { "VK_LAYER_KHRONOS_validation" };
    const std::vector<const char*> m_deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    QueueFamilyIndices m_queueFamilies;
};
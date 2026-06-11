#pragma once

#include <vulkan/vulkan.h>
#include <vector>

class VulkanContext;
class Window;

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    
    std::vector<VkSurfaceFormatKHR> formats;

    std::vector<VkPresentModeKHR> presentModes;
};

class Swapchain {
public:
    Swapchain(VulkanContext* context, Window* window);
    ~Swapchain();

    void Init();
    void Cleanup();

    const std::vector<VkImageView>& GetImageViews() const;
    VkFormat GetImageFormat() const;
    VkExtent2D GetExtent() const;
    VkSwapchainKHR GetSwapchain() const;

private:
    SwapchainSupportDetails QuerySwapchainSupport();

    VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats);

    VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes);

    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities);

    void CreateSwapchain();
    void CreateImageViews();

private:
    VulkanContext* m_context = nullptr;
    Window* m_window = nullptr;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;

    VkFormat m_imageFormat;
    VkExtent2D m_extent;
};
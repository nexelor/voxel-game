#include "Swapchain.hpp"

#include "VulkanContext.hpp"
#include "engine/platform/Window.hpp"
#include "Renderer.hpp"

#include <algorithm>
#include <stdexcept>

Swapchain::Swapchain(VulkanContext* context, Window* window) : m_context(context), m_window(window) {}
Swapchain::~Swapchain() { Cleanup(); }

void Swapchain::Init() {
    CreateSwapchain();
    CreateImageViews();
}

void Swapchain::Cleanup() {
    for (auto imageView : m_imageViews) {
        vkDestroyImageView(m_context->GetDevice(), imageView, nullptr);
    }

    vkDestroySwapchainKHR(m_context->GetDevice(), m_swapchain, nullptr);
}

SwapchainSupportDetails Swapchain::QuerySwapchainSupport() {
    SwapchainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context->GetPhysicalDevice(),
        m_context->GetSurface(), &details.capabilities);

    uint32_t formatCount;

    vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->GetPhysicalDevice(),
        m_context->GetSurface(), &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);

        vkGetPhysicalDeviceSurfaceFormatsKHR(m_context->GetPhysicalDevice(),
            m_context->GetSurface(), &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;

    vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->GetPhysicalDevice(),
        m_context->GetSurface(), &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);

        vkGetPhysicalDeviceSurfacePresentModesKHR(m_context->GetPhysicalDevice(),
            m_context->GetSurface(), &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR Swapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB
            && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    return formats[0];
}

VkPresentModeKHR Swapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) {
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D Swapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(m_window->GetWidth()),
        static_cast<uint32_t>(m_window->GetHeight())
    };

    actualExtent.width = std::clamp(actualExtent.width,
        capabilities.minImageExtent.width, capabilities.maxImageExtent.width);

    actualExtent.height = std::clamp(actualExtent.height,
        capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

    return actualExtent;
}

void Swapchain::CreateSwapchain() {
    auto support = QuerySwapchainSupport();
    auto surfaceFormat = ChooseSurfaceFormat(support.formats);
    auto presentMode = ChoosePresentMode(support.presentModes);
    auto extent = ChooseExtent(support.capabilities);

    uint32_t imageCount = MAX_FRAMES_IN_FLIGHT;
    
    if (support.capabilities.minImageCount > imageCount)
        imageCount = support.capabilities.minImageCount;
    
    if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_context->GetSurface();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_context->GetDevice(),
        &createInfo, nullptr, &m_swapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain");
    }

    vkGetSwapchainImagesKHR(m_context->GetDevice(), m_swapchain, &imageCount, nullptr);

    m_images.resize(imageCount);

    vkGetSwapchainImagesKHR(m_context->GetDevice(), m_swapchain, &imageCount, m_images.data());

    m_imageFormat = surfaceFormat.format;

    m_extent = extent;
}

void Swapchain::CreateImageViews() {
    m_imageViews.resize(m_images.size());

    for (size_t i = 0; i < m_images.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_images[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_imageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_context->GetDevice(), &createInfo,
            nullptr, &m_imageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image view");
        }
    }
}

const std::vector<VkImageView>& Swapchain::GetImageViews() const { return m_imageViews; }
VkFormat Swapchain::GetImageFormat() const { return m_imageFormat; }
VkExtent2D Swapchain::GetExtent() const { return m_extent; }
VkSwapchainKHR Swapchain::GetSwapchain() const { return m_swapchain; }
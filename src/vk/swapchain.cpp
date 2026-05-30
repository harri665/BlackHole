#include "vk/swapchain.h"
#include <algorithm>
#include <stdexcept>

namespace bh2::vk {

void Swapchain::init(VkPhysicalDevice physical, VkDevice device, VkSurfaceKHR surface,
                      uint32_t width, uint32_t height, uint32_t graphics_family) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

    uint32_t fmt_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &fmt_count, formats.data());

    VkSurfaceFormatKHR chosen_fmt = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen_fmt = f;
            break;
        }
    }
    format_ = chosen_fmt.format;

    extent_.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent_.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);

    uint32_t image_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) image_count = std::min(image_count, caps.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface;
    ci.minImageCount = image_count;
    ci.imageFormat = format_;
    ci.imageColorSpace = chosen_fmt.colorSpace;
    ci.imageExtent = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    uint32_t actual_count;
    vkGetSwapchainImagesKHR(device, swapchain_, &actual_count, nullptr);
    images_.resize(actual_count);
    vkGetSwapchainImagesKHR(device, swapchain_, &actual_count, images_.data());

    views_.resize(actual_count);
    for (uint32_t i = 0; i < actual_count; i++) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = images_[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = format_;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &vci, nullptr, &views_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create swapchain image view");
        }
    }
}

void Swapchain::destroy(VkDevice device) {
    for (auto v : views_) vkDestroyImageView(device, v, nullptr);
    views_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

} // namespace bh2::vk

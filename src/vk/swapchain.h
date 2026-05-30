#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace bh2::vk {

class Swapchain {
public:
    void init(VkPhysicalDevice physical, VkDevice device, VkSurfaceKHR surface,
              uint32_t width, uint32_t height, uint32_t graphics_family);
    void destroy(VkDevice device);

    VkSwapchainKHR handle() const { return swapchain_; }
    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    const std::vector<VkImageView>& image_views() const { return views_; }
    uint32_t image_count() const { return static_cast<uint32_t>(views_.size()); }

private:
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_;
    VkExtent2D extent_;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
};

} // namespace bh2::vk

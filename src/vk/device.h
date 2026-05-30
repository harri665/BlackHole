#pragma once
#include <vulkan/vulkan.h>
#include <optional>

namespace bh2::vk {

struct QueueFamilies {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> compute;
    bool complete() const { return graphics.has_value() && compute.has_value(); }
};

class Device {
public:
    void init(VkInstance instance, VkSurfaceKHR surface);
    void destroy();

    VkPhysicalDevice physical() const { return physical_; }
    VkDevice logical() const { return device_; }
    VkQueue graphics_queue() const { return graphics_queue_; }
    VkQueue compute_queue() const { return compute_queue_; }
    const QueueFamilies& families() const { return families_; }

    VkPhysicalDeviceProperties properties() const;

private:
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    VkQueue compute_queue_ = VK_NULL_HANDLE;
    QueueFamilies families_;

    void pick_physical_device(VkInstance instance, VkSurfaceKHR surface);
    QueueFamilies find_queue_families(VkPhysicalDevice dev, VkSurfaceKHR surface);
};

} // namespace bh2::vk

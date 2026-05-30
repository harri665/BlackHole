#pragma once
#include <vulkan/vulkan.h>
#include "vk/allocator.h"

namespace bh2::vk {

class ManagedImage {
public:
    void init_storage_2d(Allocator& alloc, VkDevice device,
                         uint32_t width, uint32_t height, VkFormat format);
    void init_sampled_2d(Allocator& alloc, VkDevice device,
                         uint32_t width, uint32_t height, VkFormat format,
                         const void* pixels, VkDeviceSize pixel_size,
                         VkQueue queue, uint32_t queue_family);
    void destroy(Allocator& alloc, VkDevice device);

    VkImage image() const { return alloc_image_.image; }
    VkImageView view() const { return view_; }
    VkSampler sampler() const { return sampler_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void transition_layout(VkDevice device, VkQueue queue, uint32_t queue_family,
                           VkImageLayout old_layout, VkImageLayout new_layout);

    void read_pixels(Allocator& alloc, VkDevice device, VkQueue queue, uint32_t queue_family,
                     void* dst, VkDeviceSize size);

private:
    Allocator::Image alloc_image_{};
    VkImageView view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    uint32_t width_ = 0, height_ = 0;

    VkCommandBuffer begin_single_command(VkDevice device, uint32_t queue_family);
    void end_single_command(VkDevice device, VkQueue queue, uint32_t queue_family,
                            VkCommandBuffer cmd);
    VkCommandPool tmp_pool_ = VK_NULL_HANDLE;
};

} // namespace bh2::vk

#pragma once
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace bh2::vk {

class Allocator {
public:
    void init(VkInstance instance, VkPhysicalDevice physical, VkDevice device);
    void destroy();

    VmaAllocator handle() const { return allocator_; }

    struct Buffer {
        VkBuffer buffer;
        VmaAllocation allocation;
        VkDeviceSize size;
    };

    struct Image {
        VkImage image;
        VmaAllocation allocation;
    };

    Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                         VmaMemoryUsage memory_usage);
    void destroy_buffer(Buffer& buf);

    Image create_image(const VkImageCreateInfo& ci, VmaMemoryUsage memory_usage);
    void destroy_image(Image& img);

    void* map(VmaAllocation alloc);
    void unmap(VmaAllocation alloc);

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};

} // namespace bh2::vk

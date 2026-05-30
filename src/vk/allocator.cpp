#define VMA_IMPLEMENTATION
#include "vk/allocator.h"
#include <stdexcept>

namespace bh2::vk {

void Allocator::init(VkInstance instance, VkPhysicalDevice physical, VkDevice device) {
    VmaAllocatorCreateInfo ci{};
    ci.instance = instance;
    ci.physicalDevice = physical;
    ci.device = device;
    ci.vulkanApiVersion = VK_API_VERSION_1_2;
    if (vmaCreateAllocator(&ci, &allocator_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create VMA allocator");
    }
}

void Allocator::destroy() {
    if (allocator_) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
}

Allocator::Buffer Allocator::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                            VmaMemoryUsage memory_usage) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = memory_usage;

    Buffer buf{};
    buf.size = size;
    if (vmaCreateBuffer(allocator_, &bci, &aci, &buf.buffer, &buf.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer");
    }
    return buf;
}

void Allocator::destroy_buffer(Buffer& buf) {
    if (buf.buffer) {
        vmaDestroyBuffer(allocator_, buf.buffer, buf.allocation);
        buf.buffer = VK_NULL_HANDLE;
    }
}

Allocator::Image Allocator::create_image(const VkImageCreateInfo& ci, VmaMemoryUsage memory_usage) {
    VmaAllocationCreateInfo aci{};
    aci.usage = memory_usage;

    Image img{};
    if (vmaCreateImage(allocator_, &ci, &aci, &img.image, &img.allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image");
    }
    return img;
}

void Allocator::destroy_image(Image& img) {
    if (img.image) {
        vmaDestroyImage(allocator_, img.image, img.allocation);
        img.image = VK_NULL_HANDLE;
    }
}

void* Allocator::map(VmaAllocation alloc) {
    void* data;
    vmaMapMemory(allocator_, alloc, &data);
    return data;
}

void Allocator::unmap(VmaAllocation alloc) {
    vmaUnmapMemory(allocator_, alloc);
}

} // namespace bh2::vk

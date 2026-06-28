// Buffer / Image — thin VMA-backed RAII wrappers plus upload helpers.
#pragma once

#include "Context.h"

namespace vk {

class Buffer
{
public:
    Buffer() = default;
    Buffer(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
           bool hostVisible);
    ~Buffer() { destroy(); }

    Buffer(Buffer&& o) noexcept { *this = std::move(o); }
    Buffer& operator=(Buffer&& o) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    VkBuffer handle() const { return m_buffer; }
    void* mapped() const { return m_mapped; }
    VkDeviceSize size() const { return m_size; }

private:
    void destroy();
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    void* m_mapped = nullptr;
    VkDeviceSize m_size = 0;
};

class Image
{
public:
    Image() = default;
    // 1D when extent.height == extent.depth == 1 and depth1D is true;
    // 3D when extent.depth > 1; otherwise 2D.
    Image(const Context& ctx, VkImageType type, VkExtent3D extent, VkFormat format,
          VkImageUsageFlags usage);
    ~Image() { destroy(); }

    Image(Image&& o) noexcept { *this = std::move(o); }
    Image& operator=(Image&& o) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }
    VkExtent3D extent() const { return m_extent; }
    VkFormat format() const { return m_format; }
    bool valid() const { return m_image != VK_NULL_HANDLE; }

    // Blocking staged upload; leaves the image in SHADER_READ_ONLY_OPTIMAL.
    void upload(const Context& ctx, const void* data, VkDeviceSize bytes);
    // Blocking layout transition (full subresource range).
    void transition(const Context& ctx, VkImageLayout from, VkImageLayout to);

private:
    void destroy();
    VkDevice m_device = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkExtent3D m_extent{};
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};

// Records a full-range image layout barrier into cmd.
void cmdImageBarrier(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);

} // namespace vk

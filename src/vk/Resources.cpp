#include "Resources.h"

#include <cstring>
#include <utility>

namespace vk {

// --------------------------------------------------------------------- Buffer

Buffer::Buffer(const Context& ctx, VkDeviceSize size, VkBufferUsageFlags usage,
               bool hostVisible)
    : m_allocator(ctx.allocator()), m_size(size)
{
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible)
        ai.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                   VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo info{};
    VK_CHECK(vmaCreateBuffer(m_allocator, &ci, &ai, &m_buffer, &m_allocation, &info));
    m_mapped = info.pMappedData;
}

Buffer& Buffer::operator=(Buffer&& o) noexcept
{
    if (this != &o)
    {
        destroy();
        m_allocator = std::exchange(o.m_allocator, VK_NULL_HANDLE);
        m_buffer = std::exchange(o.m_buffer, VK_NULL_HANDLE);
        m_allocation = std::exchange(o.m_allocation, VK_NULL_HANDLE);
        m_mapped = std::exchange(o.m_mapped, nullptr);
        m_size = std::exchange(o.m_size, 0);
    }
    return *this;
}

void Buffer::destroy()
{
    if (m_buffer)
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    m_buffer = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------- Image

Image::Image(const Context& ctx, VkImageType type, VkExtent3D extent, VkFormat format,
             VkImageUsageFlags usage)
    : m_device(ctx.device()), m_allocator(ctx.allocator()),
      m_extent(extent), m_format(format)
{
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = type;
    ci.format = format;
    ci.extent = extent;
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_AUTO;
    VK_CHECK(vmaCreateImage(m_allocator, &ci, &ai, &m_image, &m_allocation, nullptr));

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = m_image;
    vi.viewType = type == VK_IMAGE_TYPE_1D ? VK_IMAGE_VIEW_TYPE_1D
                : type == VK_IMAGE_TYPE_3D ? VK_IMAGE_VIEW_TYPE_3D
                                           : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(m_device, &vi, nullptr, &m_view));
}

Image& Image::operator=(Image&& o) noexcept
{
    if (this != &o)
    {
        destroy();
        m_device = std::exchange(o.m_device, VK_NULL_HANDLE);
        m_allocator = std::exchange(o.m_allocator, VK_NULL_HANDLE);
        m_image = std::exchange(o.m_image, VK_NULL_HANDLE);
        m_allocation = std::exchange(o.m_allocation, VK_NULL_HANDLE);
        m_view = std::exchange(o.m_view, VK_NULL_HANDLE);
        m_extent = o.m_extent;
        m_format = o.m_format;
    }
    return *this;
}

void Image::destroy()
{
    if (m_view)
        vkDestroyImageView(m_device, m_view, nullptr);
    if (m_image)
        vmaDestroyImage(m_allocator, m_image, m_allocation);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
}

void Image::upload(const Context& ctx, const void* data, VkDeviceSize bytes)
{
    Buffer staging(ctx, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped(), data, bytes);

    ctx.oneShot([&](VkCommandBuffer cmd) {
        cmdImageBarrier(cmd, m_image,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        0, VK_ACCESS_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = m_extent;
        vkCmdCopyBufferToImage(cmd, staging.handle(), m_image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        cmdImageBarrier(cmd, m_image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
}

void Image::transition(const Context& ctx, VkImageLayout from, VkImageLayout to)
{
    ctx.oneShot([&](VkCommandBuffer cmd) {
        cmdImageBarrier(cmd, m_image, from, to,
                        VK_ACCESS_MEMORY_WRITE_BIT,
                        VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    });
}

void cmdImageBarrier(VkCommandBuffer cmd, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

} // namespace vk

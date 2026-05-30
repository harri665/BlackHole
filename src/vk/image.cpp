#include "vk/image.h"
#include <stdexcept>
#include <cstring>

namespace bh2::vk {

VkCommandBuffer ManagedImage::begin_single_command(VkDevice device, uint32_t queue_family) {
    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.queueFamilyIndex = queue_family;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCreateCommandPool(device, &pool_ci, nullptr, &tmp_pool_);

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = tmp_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void ManagedImage::end_single_command(VkDevice device, VkQueue queue, uint32_t,
                                       VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkDestroyCommandPool(device, tmp_pool_, nullptr);
    tmp_pool_ = VK_NULL_HANDLE;
}

void ManagedImage::init_storage_2d(Allocator& alloc, VkDevice device,
                                    uint32_t w, uint32_t h, VkFormat format) {
    width_ = w;
    height_ = h;

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
              | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    alloc_image_ = alloc.create_image(ici, VMA_MEMORY_USAGE_GPU_ONLY);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = alloc_image_.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &vci, nullptr, &view_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create storage image view");
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &sci, nullptr, &sampler_);
}

void ManagedImage::init_sampled_2d(Allocator& alloc, VkDevice device,
                                    uint32_t w, uint32_t h, VkFormat format,
                                    const void* pixels, VkDeviceSize pixel_size,
                                    VkQueue queue, uint32_t queue_family) {
    width_ = w;
    height_ = h;

    auto staging = alloc.create_buffer(pixel_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void* mapped = alloc.map(staging.allocation);
    memcpy(mapped, pixels, pixel_size);
    alloc.unmap(staging.allocation);

    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    alloc_image_ = alloc.create_image(ici, VMA_MEMORY_USAGE_GPU_ONLY);

    auto cmd = begin_single_command(device, queue_family);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = alloc_image_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, alloc_image_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    end_single_command(device, queue, queue_family, cmd);
    alloc.destroy_buffer(staging);

    VkImageViewCreateInfo vci{};
    vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image = alloc_image_.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &vci, nullptr, &view_);

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &sci, nullptr, &sampler_);
}

void ManagedImage::destroy(Allocator& alloc, VkDevice device) {
    if (sampler_) { vkDestroySampler(device, sampler_, nullptr); sampler_ = VK_NULL_HANDLE; }
    if (view_) { vkDestroyImageView(device, view_, nullptr); view_ = VK_NULL_HANDLE; }
    alloc.destroy_image(alloc_image_);
}

void ManagedImage::transition_layout(VkDevice device, VkQueue queue, uint32_t queue_family,
                                      VkImageLayout old_layout, VkImageLayout new_layout) {
    auto cmd = begin_single_command(device, queue_family);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.image = alloc_image_.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    end_single_command(device, queue, queue_family, cmd);
}

void ManagedImage::read_pixels(Allocator& alloc, VkDevice device,
                                VkQueue queue, uint32_t queue_family,
                                void* dst, VkDeviceSize size) {
    auto staging = alloc.create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                        VMA_MEMORY_USAGE_CPU_ONLY);

    auto cmd = begin_single_command(device, queue_family);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, alloc_image_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging.buffer, 1, &region);

    end_single_command(device, queue, queue_family, cmd);

    void* mapped = alloc.map(staging.allocation);
    memcpy(dst, mapped, size);
    alloc.unmap(staging.allocation);
    alloc.destroy_buffer(staging);
}

} // namespace bh2::vk

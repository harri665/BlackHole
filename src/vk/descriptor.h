#pragma once
#include <vulkan/vulkan.h>
#include <vector>

namespace bh2::vk {

class DescriptorManager {
public:
    void init(VkDevice device);
    void destroy(VkDevice device);

    VkDescriptorSetLayout compute_layout() const { return compute_layout_; }
    VkDescriptorSetLayout tonemap_layout() const { return tonemap_layout_; }

    VkDescriptorSet allocate_compute_set(VkDevice device);
    VkDescriptorSet allocate_tonemap_set(VkDevice device);

    // Update compute descriptor set bindings
    void update_compute_set(VkDevice device, VkDescriptorSet set,
                            VkImageView hdr_image_view,
                            VkBuffer ubo_buffer, VkDeviceSize ubo_size,
                            VkImageView skybox_view, VkSampler skybox_sampler,
                            VkBuffer density_grid, VkDeviceSize density_size,
                            VkBuffer temp_grid, VkDeviceSize temp_size);

    void update_tonemap_set(VkDevice device, VkDescriptorSet set,
                            VkImageView hdr_view, VkSampler sampler);

private:
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout compute_layout_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout tonemap_layout_ = VK_NULL_HANDLE;
};

} // namespace bh2::vk

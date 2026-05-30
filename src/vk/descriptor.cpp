#include "vk/descriptor.h"
#include <stdexcept>

namespace bh2::vk {

void DescriptorManager::init(VkDevice device) {
    // Pool
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
    };
    VkDescriptorPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_ci.poolSizeCount = 4;
    pool_ci.pPoolSizes = pool_sizes;
    pool_ci.maxSets = 8;
    if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &pool_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }

    // Compute layout: binding 0=storage image, 1=UBO, 2=skybox sampler, 3=density SSBO, 4=temp SSBO
    VkDescriptorSetLayoutBinding bindings[5]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_ci.bindingCount = 5;
    layout_ci.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &compute_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor set layout");
    }

    // Tonemap layout: binding 0 = sampled HDR image
    VkDescriptorSetLayoutBinding tonemap_binding{};
    tonemap_binding.binding = 0;
    tonemap_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    tonemap_binding.descriptorCount = 1;
    tonemap_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo tonemap_layout_ci{};
    tonemap_layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    tonemap_layout_ci.bindingCount = 1;
    tonemap_layout_ci.pBindings = &tonemap_binding;
    if (vkCreateDescriptorSetLayout(device, &tonemap_layout_ci, nullptr, &tonemap_layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create tonemap descriptor set layout");
    }
}

void DescriptorManager::destroy(VkDevice device) {
    if (compute_layout_) { vkDestroyDescriptorSetLayout(device, compute_layout_, nullptr); compute_layout_ = VK_NULL_HANDLE; }
    if (tonemap_layout_) { vkDestroyDescriptorSetLayout(device, tonemap_layout_, nullptr); tonemap_layout_ = VK_NULL_HANDLE; }
    if (pool_) { vkDestroyDescriptorPool(device, pool_, nullptr); pool_ = VK_NULL_HANDLE; }
}

VkDescriptorSet DescriptorManager::allocate_compute_set(VkDevice device) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &compute_layout_;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate compute descriptor set");
    }
    return set;
}

VkDescriptorSet DescriptorManager::allocate_tonemap_set(VkDevice device) {
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &tonemap_layout_;
    VkDescriptorSet set;
    if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate tonemap descriptor set");
    }
    return set;
}

void DescriptorManager::update_compute_set(VkDevice device, VkDescriptorSet set,
                                            VkImageView hdr_image_view,
                                            VkBuffer ubo_buffer, VkDeviceSize ubo_size,
                                            VkImageView skybox_view, VkSampler skybox_sampler,
                                            VkBuffer density_grid, VkDeviceSize density_size,
                                            VkBuffer temp_grid, VkDeviceSize temp_size) {
    VkDescriptorImageInfo img_info{};
    img_info.imageView = hdr_image_view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo ubo_info{};
    ubo_info.buffer = ubo_buffer;
    ubo_info.offset = 0;
    ubo_info.range = ubo_size;

    VkDescriptorImageInfo sky_info{};
    sky_info.imageView = skybox_view;
    sky_info.sampler = skybox_sampler;
    sky_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo density_info{};
    density_info.buffer = density_grid;
    density_info.offset = 0;
    density_info.range = density_size;

    VkDescriptorBufferInfo temp_info{};
    temp_info.buffer = temp_grid;
    temp_info.offset = 0;
    temp_info.range = temp_size;

    VkWriteDescriptorSet writes[5]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = set;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &img_info;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &ubo_info;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = set;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &sky_info;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = set;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].pBufferInfo = &density_info;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = set;
    writes[4].dstBinding = 4;
    writes[4].descriptorCount = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo = &temp_info;

    vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
}

void DescriptorManager::update_tonemap_set(VkDevice device, VkDescriptorSet set,
                                            VkImageView hdr_view, VkSampler sampler) {
    VkDescriptorImageInfo img_info{};
    img_info.imageView = hdr_view;
    img_info.sampler = sampler;
    img_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &img_info;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

} // namespace bh2::vk

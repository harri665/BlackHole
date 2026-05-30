#pragma once
#include <vulkan/vulkan.h>
#include <string>

namespace bh2::vk {

class ComputePipeline {
public:
    void init(VkDevice device, const std::string& spirv_path,
              VkDescriptorSetLayout desc_layout, uint32_t push_constant_size = 0);
    void destroy(VkDevice device);

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderModule shader_ = VK_NULL_HANDLE;
};

class GraphicsPipeline {
public:
    void init(VkDevice device,
              const std::string& vert_spirv, const std::string& frag_spirv,
              VkDescriptorSetLayout desc_layout, VkRenderPass render_pass,
              uint32_t push_constant_size = 0);
    void destroy(VkDevice device);

    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }

private:
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkShaderModule vert_shader_ = VK_NULL_HANDLE;
    VkShaderModule frag_shader_ = VK_NULL_HANDLE;
};

} // namespace bh2::vk

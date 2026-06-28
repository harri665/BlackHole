// Pipeline — SPIR-V loading, descriptor set layout helpers, and wrappers for
// the compute (tracer) and graphics (post-process) pipelines.
#pragma once

#include "Context.h"

#include <string>

namespace vk {

VkShaderModule loadShaderModule(VkDevice device, const std::string& spvPath);

struct BindingDesc
{
    uint32_t binding;
    VkDescriptorType type;
    VkShaderStageFlags stages;
};

class ComputePipeline
{
public:
    ComputePipeline(const Context& ctx, const std::string& spvPath,
                    const std::vector<BindingDesc>& bindings,
                    uint32_t pushConstantSize = 0);
    ~ComputePipeline();

    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }
    VkDescriptorSetLayout setLayout() const { return m_setLayout; }

private:
    VkDevice m_device;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

// Fullscreen-triangle graphics pipeline (no vertex input, no depth).
class GraphicsPipeline
{
public:
    GraphicsPipeline(const Context& ctx, const std::string& vertSpv,
                     const std::string& fragSpv, VkRenderPass renderPass,
                     const std::vector<BindingDesc>& bindings,
                     uint32_t pushConstantSize = 0);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }
    VkDescriptorSetLayout setLayout() const { return m_setLayout; }

private:
    VkDevice m_device;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace vk

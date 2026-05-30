#include "vk/compute_pipeline.h"
#include <fstream>
#include <stdexcept>
#include <vector>

namespace bh2::vk {

static std::vector<char> read_spirv(const std::string& path) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("Failed to open shader: " + path);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(size);
    file.seekg(0);
    file.read(buffer.data(), size);
    return buffer;
}

static VkShaderModule create_shader_module(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    return module;
}

void ComputePipeline::init(VkDevice device, const std::string& spirv_path,
                            VkDescriptorSetLayout desc_layout, uint32_t push_constant_size) {
    auto code = read_spirv(spirv_path);
    shader_ = create_shader_module(device, code);

    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &desc_layout;

    VkPushConstantRange push_range{};
    if (push_constant_size > 0) {
        push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        push_range.offset = 0;
        push_range.size = push_constant_size;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
    }

    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout");
    }

    VkComputePipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module = shader_;
    ci.stage.pName = "main";
    ci.layout = layout_;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline");
    }
}

void ComputePipeline::destroy(VkDevice device) {
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_) { vkDestroyPipelineLayout(device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    if (shader_) { vkDestroyShaderModule(device, shader_, nullptr); shader_ = VK_NULL_HANDLE; }
}

void GraphicsPipeline::init(VkDevice device,
                             const std::string& vert_spirv, const std::string& frag_spirv,
                             VkDescriptorSetLayout desc_layout, VkRenderPass render_pass,
                             uint32_t push_constant_size) {
    auto vert_code = read_spirv(vert_spirv);
    auto frag_code = read_spirv(frag_spirv);
    vert_shader_ = create_shader_module(device, vert_code);
    frag_shader_ = create_shader_module(device, frag_code);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert_shader_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag_shader_;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_asm{};
    input_asm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;

    VkPipelineMultisampleStateCreateInfo msaa{};
    msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_attach{};
    blend_attach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
                                | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_attach;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &desc_layout;

    VkPushConstantRange push_range{};
    if (push_constant_size > 0) {
        push_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = push_constant_size;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
    }

    if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &layout_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline layout");
    }

    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertex_input;
    ci.pInputAssemblyState = &input_asm;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &msaa;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dyn;
    ci.layout = layout_;
    ci.renderPass = render_pass;
    ci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &pipeline_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }
}

void GraphicsPipeline::destroy(VkDevice device) {
    if (pipeline_) { vkDestroyPipeline(device, pipeline_, nullptr); pipeline_ = VK_NULL_HANDLE; }
    if (layout_) { vkDestroyPipelineLayout(device, layout_, nullptr); layout_ = VK_NULL_HANDLE; }
    if (vert_shader_) { vkDestroyShaderModule(device, vert_shader_, nullptr); vert_shader_ = VK_NULL_HANDLE; }
    if (frag_shader_) { vkDestroyShaderModule(device, frag_shader_, nullptr); frag_shader_ = VK_NULL_HANDLE; }
}

} // namespace bh2::vk

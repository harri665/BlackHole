#include "Pipeline.h"

#include <cstdio>
#include <fstream>

namespace vk {

VkShaderModule loadShaderModule(VkDevice device, const std::string& spvPath)
{
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open shader: " + spvPath);
    size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), size);

    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &module));
    return module;
}

static VkDescriptorSetLayout makeSetLayout(VkDevice device,
                                           const std::vector<BindingDesc>& bindings)
{
    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    for (const auto& b : bindings)
        vkBindings.push_back({b.binding, b.type, 1, b.stages, nullptr});

    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = static_cast<uint32_t>(vkBindings.size());
    ci.pBindings = vkBindings.data();
    VkDescriptorSetLayout layout;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout));
    return layout;
}

static VkPipelineLayout makePipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout,
                                           uint32_t pushSize, VkShaderStageFlags pushStages)
{
    VkPushConstantRange range{pushStages, 0, pushSize};
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &setLayout;
    if (pushSize > 0)
    {
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &range;
    }
    VkPipelineLayout layout;
    VK_CHECK(vkCreatePipelineLayout(device, &ci, nullptr, &layout));
    return layout;
}

// ------------------------------------------------------------ ComputePipeline

ComputePipeline::ComputePipeline(const Context& ctx, const std::string& spvPath,
                                 const std::vector<BindingDesc>& bindings,
                                 uint32_t pushConstantSize)
    : m_device(ctx.device())
{
    m_setLayout = makeSetLayout(m_device, bindings);
    m_layout = makePipelineLayout(m_device, m_setLayout, pushConstantSize,
                                  VK_SHADER_STAGE_COMPUTE_BIT);

    VkShaderModule module = loadShaderModule(m_device, spvPath);

    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr};
    ci.layout = m_layout;
    VK_CHECK(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline));

    vkDestroyShaderModule(m_device, module, nullptr);
}

ComputePipeline::~ComputePipeline()
{
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
}

// ----------------------------------------------------------- GraphicsPipeline

GraphicsPipeline::GraphicsPipeline(const Context& ctx, const std::string& vertSpv,
                                   const std::string& fragSpv, VkRenderPass renderPass,
                                   const std::vector<BindingDesc>& bindings,
                                   uint32_t pushConstantSize)
    : m_device(ctx.device())
{
    m_setLayout = makeSetLayout(m_device, bindings);
    m_layout = makePipelineLayout(m_device, m_setLayout, pushConstantSize,
                                  VK_SHADER_STAGE_FRAGMENT_BIT);

    VkShaderModule vert = loadShaderModule(m_device, vertSpv);
    VkShaderModule frag = loadShaderModule(m_device, fragSpv);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamics;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vertexInput;
    ci.pInputAssemblyState = &inputAssembly;
    ci.pViewportState = &viewport;
    ci.pRasterizationState = &raster;
    ci.pMultisampleState = &multisample;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dynamic;
    ci.layout = m_layout;
    ci.renderPass = renderPass;
    ci.subpass = 0;
    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline));

    vkDestroyShaderModule(m_device, vert, nullptr);
    vkDestroyShaderModule(m_device, frag, nullptr);
}

GraphicsPipeline::~GraphicsPipeline()
{
    vkDestroyPipeline(m_device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    vkDestroyDescriptorSetLayout(m_device, m_setLayout, nullptr);
}

} // namespace vk

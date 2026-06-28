#include "Swapchain.h"

#include <algorithm>

namespace vk {

Swapchain::Swapchain(const Context& ctx, uint32_t width, uint32_t height)
    : m_ctx(ctx)
{
    create(width, height);

    // Render pass: single color attachment, cleared, ends PRESENT_SRC.
    VkAttachmentDescription color{};
    color.format = m_format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(m_ctx.device(), &rpci, nullptr, &m_renderPass));

    // framebuffers need the render pass, so they are built here, not in create()
    recreate(width, height);
}

Swapchain::~Swapchain()
{
    cleanup();
    if (m_swapchain)
        vkDestroySwapchainKHR(m_ctx.device(), m_swapchain, nullptr);
    if (m_renderPass)
        vkDestroyRenderPass(m_ctx.device(), m_renderPass, nullptr);
}

void Swapchain::create(uint32_t width, uint32_t height)
{
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        m_ctx.physicalDevice(), m_ctx.surface(), &caps));

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_ctx.surface(),
                                         &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_ctx.physicalDevice(), m_ctx.surface(),
                                         &formatCount, formats.data());

    // We do sRGB encoding manually in post.frag, so pick a UNORM format.
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            chosen = f;
            break;
        }
    m_format = chosen.format;

    if (caps.currentExtent.width != UINT32_MAX)
        m_extent = caps.currentExtent;
    else
        m_extent = {std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
                    std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainKHR oldSwapchain = m_swapchain;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = m_ctx.surface();
    ci.minImageCount = imageCount;
    ci.imageFormat = chosen.format;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = m_extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = oldSwapchain;
    VK_CHECK(vkCreateSwapchainKHR(m_ctx.device(), &ci, nullptr, &m_swapchain));

    if (oldSwapchain)
        vkDestroySwapchainKHR(m_ctx.device(), oldSwapchain, nullptr);

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(m_ctx.device(), m_swapchain, &count, m_images.data());
}

void Swapchain::recreate(uint32_t width, uint32_t height)
{
    m_ctx.waitIdle();
    cleanup();
    create(width, height);

    m_views.resize(m_images.size());
    m_framebuffers.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i)
    {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = m_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(m_ctx.device(), &vi, nullptr, &m_views[i]));

        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass = m_renderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &m_views[i];
        fb.width = m_extent.width;
        fb.height = m_extent.height;
        fb.layers = 1;
        VK_CHECK(vkCreateFramebuffer(m_ctx.device(), &fb, nullptr, &m_framebuffers[i]));
    }
}

void Swapchain::cleanup()
{
    for (auto fb : m_framebuffers)
        vkDestroyFramebuffer(m_ctx.device(), fb, nullptr);
    for (auto v : m_views)
        vkDestroyImageView(m_ctx.device(), v, nullptr);
    m_framebuffers.clear();
    m_views.clear();
}

} // namespace vk

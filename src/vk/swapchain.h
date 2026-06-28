// Swapchain — surface swapchain, image views, a single-attachment render pass
// and framebuffers for the post-process + UI pass.
#pragma once

#include "Context.h"

namespace vk {

class Swapchain
{
public:
    Swapchain(const Context& ctx, uint32_t width, uint32_t height);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void recreate(uint32_t width, uint32_t height);

    VkSwapchainKHR handle() const { return m_swapchain; }
    VkRenderPass renderPass() const { return m_renderPass; }
    VkFramebuffer framebuffer(uint32_t i) const { return m_framebuffers[i]; }
    VkExtent2D extent() const { return m_extent; }
    VkFormat format() const { return m_format; }
    uint32_t imageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    void create(uint32_t width, uint32_t height);
    void cleanup();

    const Context& m_ctx;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    std::vector<VkFramebuffer> m_framebuffers;
    VkExtent2D m_extent{};
    VkFormat m_format = VK_FORMAT_UNDEFINED;
};

} // namespace vk

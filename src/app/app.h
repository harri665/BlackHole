#pragma once
#include "app/config.h"
#include "app/camera.h"
#include "vk/instance.h"
#include "vk/device.h"
#include "vk/swapchain.h"
#include "vk/allocator.h"
#include "vk/compute_pipeline.h"
#include "vk/descriptor.h"
#include "vk/image.h"

struct GLFWwindow;

namespace bh2 {

class App {
public:
    void init(RenderConfig& cfg);
    void run();
    void destroy();

private:
    RenderConfig* cfg_ = nullptr;
    Camera camera_;

    // Vulkan
    vk::Instance instance_;
    vk::Device device_;
    vk::Swapchain swapchain_;
    vk::Allocator allocator_;
    vk::ComputePipeline compute_pipeline_;
    vk::GraphicsPipeline tonemap_pipeline_;
    vk::DescriptorManager descriptors_;
    vk::ManagedImage hdr_image_;
    vk::ManagedImage skybox_image_;

    VkDescriptorSet compute_set_ = VK_NULL_HANDLE;
    VkDescriptorSet tonemap_set_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmd_buffers_;
    std::vector<VkSemaphore> image_available_;  // one per swapchain image
    std::vector<VkSemaphore> render_finished_;  // one per swapchain image
    VkFence in_flight_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    vk::Allocator::Buffer ubo_buffer_{};
    vk::Allocator::Buffer density_buffer_{};
    vk::Allocator::Buffer temp_buffer_{};

    GLFWwindow* window_ = nullptr;
    int sample_index_ = 0;
    bool camera_dirty_ = true;

    void init_window();
    void init_vulkan();
    void create_render_pass();
    void create_framebuffers();
    void create_command_pool();
    void create_sync();
    void load_resources();
    void update_ubo(int sample_index);
    void record_frame(uint32_t image_index);
    void draw_frame();
    void rebuild_swapchain();

    static void glfw_scroll_callback(GLFWwindow* window, double xoff, double yoff);
};

} // namespace bh2

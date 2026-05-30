#include "app/app.h"
#include "io/hdr_loader.h"
#include "io/vdb_loader.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <filesystem>

namespace bh2 {

void App::glfw_scroll_callback(GLFWwindow* window, double, double yoff) {
    auto* app = static_cast<App*>(glfwGetWindowUserPointer(window));
    app->camera_.zoom(static_cast<float>(-yoff) * 2.0f);
    app->camera_dirty_ = true;
}

void App::init(RenderConfig& cfg) {
    cfg_ = &cfg;
    camera_.update_from_config(cfg);
    init_window();
    init_vulkan();
    load_resources();
}

void App::init_window() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(cfg_->width, cfg_->height, "BlackHole2", nullptr, nullptr);
    glfwSetWindowUserPointer(window_, this);
    glfwSetScrollCallback(window_, glfw_scroll_callback);
}

void App::init_vulkan() {
    vk::InstanceCreateInfo ici;
    ici.enable_validation = true;
    instance_.init(ici);

    if (glfwCreateWindowSurface(instance_.handle(), window_, nullptr, &surface_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface");
    }

    device_.init(instance_.handle(), surface_);
    allocator_.init(instance_.handle(), device_.physical(), device_.logical());

    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    swapchain_.init(device_.physical(), device_.logical(), surface_,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                    device_.families().graphics.value());

    descriptors_.init(device_.logical());

    // HDR accumulation image
    hdr_image_.init_storage_2d(allocator_, device_.logical(),
                                cfg_->width, cfg_->height, VK_FORMAT_R32G32B32A32_SFLOAT);
    hdr_image_.transition_layout(device_.logical(), device_.graphics_queue(),
                                  device_.families().graphics.value(),
                                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    create_render_pass();
    create_framebuffers();
    create_command_pool();
    create_sync();

    std::string shader_dir = BH2_SHADER_DIR;
    compute_pipeline_.init(device_.logical(),
                           shader_dir + "/trace.comp.glsl.spv",
                           descriptors_.compute_layout());

    struct TonemapPC { float exposure; float gamma; };
    tonemap_pipeline_.init(device_.logical(),
                           shader_dir + "/fullscreen.vert.glsl.spv",
                           shader_dir + "/tonemap.frag.glsl.spv",
                           descriptors_.tonemap_layout(),
                           render_pass_,
                           sizeof(TonemapPC));
}

void App::create_render_pass() {
    VkAttachmentDescription color_att{};
    color_att.format = swapchain_.format();
    color_att.samples = VK_SAMPLE_COUNT_1_BIT;
    color_att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments = &color_att;
    ci.subpassCount = 1;
    ci.pSubpasses = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies = &dep;

    if (vkCreateRenderPass(device_.logical(), &ci, nullptr, &render_pass_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void App::create_framebuffers() {
    framebuffers_.resize(swapchain_.image_count());
    for (uint32_t i = 0; i < swapchain_.image_count(); i++) {
        VkImageView attachment = swapchain_.image_views()[i];
        VkFramebufferCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass = render_pass_;
        ci.attachmentCount = 1;
        ci.pAttachments = &attachment;
        ci.width = swapchain_.extent().width;
        ci.height = swapchain_.extent().height;
        ci.layers = 1;
        if (vkCreateFramebuffer(device_.logical(), &ci, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void App::create_command_pool() {
    VkCommandPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.queueFamilyIndex = device_.families().graphics.value();
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device_.logical(), &ci, nullptr, &cmd_pool_);

    cmd_buffers_.resize(1);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmd_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_.logical(), &ai, cmd_buffers_.data());
}

void App::create_sync() {
    uint32_t n = swapchain_.image_count();
    image_available_.resize(n);
    render_finished_.resize(n);

    VkSemaphoreCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < n; i++) {
        vkCreateSemaphore(device_.logical(), &sci, nullptr, &image_available_[i]);
        vkCreateSemaphore(device_.logical(), &sci, nullptr, &render_finished_[i]);
    }

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device_.logical(), &fci, nullptr, &in_flight_);
}

void App::load_resources() {
    // UBO
    ubo_buffer_ = allocator_.create_buffer(sizeof(RenderConfigGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    // Skybox
    io::HDRImage sky;
    if (!cfg_->skybox_path.empty() && std::filesystem::exists(cfg_->skybox_path)) {
        sky = io::load_hdr_any(cfg_->skybox_path);
        printf("Loaded skybox: %dx%d\n", sky.width, sky.height);
    } else {
        sky.width = 64;
        sky.height = 32;
        sky.pixels.resize(64 * 32 * 4, 0.001f);
        printf("Using default dark skybox (no HDR panorama loaded)\n");
    }
    skybox_image_.init_sampled_2d(allocator_, device_.logical(),
                                  sky.width, sky.height, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  sky.pixels.data(), sky.pixels.size() * sizeof(float),
                                  device_.graphics_queue(), device_.families().graphics.value());

    // VDB grids (dummy for now)
    auto vdb_data = io::create_dummy_vdb();
#ifdef BH2_HAS_NANOVDB
    if (!cfg_->vdb_path.empty() && std::filesystem::exists(cfg_->vdb_path)) {
        vdb_data = io::load_vdb(cfg_->vdb_path);
    }
#endif

    auto upload_grid = [&](const io::NanoVDBGridData& grid) {
        VkDeviceSize size = std::max(grid.buffer.size(), size_t(64));
        auto buf = allocator_.create_buffer(size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        if (!grid.buffer.empty()) {
            void* mapped = allocator_.map(buf.allocation);
            memcpy(mapped, grid.buffer.data(), grid.buffer.size());
            allocator_.unmap(buf.allocation);
        }
        return buf;
    };

    density_buffer_ = upload_grid(vdb_data.density);
    temp_buffer_ = upload_grid(vdb_data.temperature);

    // Descriptor sets
    compute_set_ = descriptors_.allocate_compute_set(device_.logical());
    descriptors_.update_compute_set(device_.logical(), compute_set_,
        hdr_image_.view(),
        ubo_buffer_.buffer, sizeof(RenderConfigGPU),
        skybox_image_.view(), skybox_image_.sampler(),
        density_buffer_.buffer, density_buffer_.size,
        temp_buffer_.buffer, temp_buffer_.size);

    tonemap_set_ = descriptors_.allocate_tonemap_set(device_.logical());
    descriptors_.update_tonemap_set(device_.logical(), tonemap_set_,
        hdr_image_.view(), hdr_image_.sampler());
}

void App::update_ubo(int sample_idx) {
    auto gpu = to_gpu_config(*cfg_, sample_idx);
    gpu.cam_pos[0] = camera_.r();
    gpu.cam_pos[1] = camera_.theta();
    gpu.cam_pos[2] = camera_.phi();

    void* mapped = allocator_.map(ubo_buffer_.allocation);
    memcpy(mapped, &gpu, sizeof(gpu));
    allocator_.unmap(ubo_buffer_.allocation);
}

void App::record_frame(uint32_t image_index) {
    auto cmd = cmd_buffers_[0];
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &bi);

    // Dispatch compute
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline_.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            compute_pipeline_.layout(), 0, 1, &compute_set_, 0, nullptr);

    uint32_t gx = (cfg_->width + 15) / 16;
    uint32_t gy = (cfg_->height + 15) / 16;
    vkCmdDispatch(cmd, gx, gy, 1);

    // Barrier: compute write → fragment read
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = hdr_image_.image();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    // Render pass: tonemap + present
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = render_pass_;
    rpbi.framebuffer = framebuffers_[image_index];
    rpbi.renderArea.extent = swapchain_.extent();
    VkClearValue clear{};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, tonemap_pipeline_.handle());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            tonemap_pipeline_.layout(), 0, 1, &tonemap_set_, 0, nullptr);

    VkViewport vp{};
    vp.width = static_cast<float>(swapchain_.extent().width);
    vp.height = static_cast<float>(swapchain_.extent().height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.extent = swapchain_.extent();
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    struct { float exposure; float gamma; } pc{cfg_->exposure, cfg_->gamma};
    vkCmdPushConstants(cmd, tonemap_pipeline_.layout(),
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);

    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);
}

void App::draw_frame() {
    vkWaitForFences(device_.logical(), 1, &in_flight_, VK_TRUE, UINT64_MAX);
    vkResetFences(device_.logical(), 1, &in_flight_);

    static uint32_t frame_idx = 0;

    uint32_t image_index;
    VkResult result = vkAcquireNextImageKHR(device_.logical(), swapchain_.handle(),
                                             UINT64_MAX, image_available_[frame_idx],
                                             VK_NULL_HANDLE, &image_index);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        rebuild_swapchain();
        return;
    }

    if (camera_dirty_) {
        sample_index_ = 0;
        camera_dirty_ = false;
    }

    update_ubo(sample_index_);
    vkResetCommandBuffer(cmd_buffers_[0], 0);
    record_frame(image_index);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available_[frame_idx];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_buffers_[0];
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished_[frame_idx];

    vkQueueSubmit(device_.graphics_queue(), 1, &si, in_flight_);

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[frame_idx];
    pi.swapchainCount = 1;
    auto sc = swapchain_.handle();
    pi.pSwapchains = &sc;
    pi.pImageIndices = &image_index;
    vkQueuePresentKHR(device_.graphics_queue(), &pi);

    frame_idx = (frame_idx + 1) % swapchain_.image_count();

    sample_index_++;
}

void App::rebuild_swapchain() {
    vkDeviceWaitIdle(device_.logical());
    // Simplified: would need full cleanup + recreate
}

void App::run() {
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Mouse drag for orbit
        static bool dragging = false;
        static double last_x, last_y;
        if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            double mx, my;
            glfwGetCursorPos(window_, &mx, &my);
            if (dragging) {
                float dx = static_cast<float>(mx - last_x) * 0.005f;
                float dy = static_cast<float>(my - last_y) * 0.005f;
                camera_.orbit(dy, -dx);
                camera_dirty_ = true;
            }
            last_x = mx;
            last_y = my;
            dragging = true;
        } else {
            dragging = false;
        }

        draw_frame();
    }
    vkDeviceWaitIdle(device_.logical());
}

void App::destroy() {
    vkDeviceWaitIdle(device_.logical());

    for (auto s : image_available_) vkDestroySemaphore(device_.logical(), s, nullptr);
    for (auto s : render_finished_) vkDestroySemaphore(device_.logical(), s, nullptr);
    vkDestroyFence(device_.logical(), in_flight_, nullptr);
    vkDestroyCommandPool(device_.logical(), cmd_pool_, nullptr);

    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_.logical(), fb, nullptr);
    vkDestroyRenderPass(device_.logical(), render_pass_, nullptr);

    tonemap_pipeline_.destroy(device_.logical());
    compute_pipeline_.destroy(device_.logical());

    hdr_image_.destroy(allocator_, device_.logical());
    skybox_image_.destroy(allocator_, device_.logical());
    allocator_.destroy_buffer(ubo_buffer_);
    allocator_.destroy_buffer(density_buffer_);
    allocator_.destroy_buffer(temp_buffer_);

    descriptors_.destroy(device_.logical());
    swapchain_.destroy(device_.logical());
    if (surface_) vkDestroySurfaceKHR(instance_.handle(), surface_, nullptr);
    allocator_.destroy();
    device_.destroy();
    instance_.destroy();

    glfwDestroyWindow(window_);
    glfwTerminate();
}

} // namespace bh2

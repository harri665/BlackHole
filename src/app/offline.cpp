#include "app/offline.h"
#include "vk/instance.h"
#include "vk/device.h"
#include "vk/allocator.h"
#include "vk/compute_pipeline.h"
#include "vk/descriptor.h"
#include "vk/image.h"
#include "io/hdr_loader.h"
#include "io/exr_writer.h"
#include "io/vdb_loader.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <filesystem>
#include <string>

namespace bh2 {

void run_offline(RenderConfig& cfg) {
    printf("Offline render: %dx%d, %d samples\n",
           cfg.offline_width, cfg.offline_height, cfg.offline_samples);

    // Headless Vulkan init (no surface/window)
    vk::InstanceCreateInfo ici;
    ici.enable_validation = false;
    vk::Instance instance;

    // For headless, we need a dummy GLFW window to get instance extensions,
    // or we can create a minimal instance without surface extensions.
    // For simplicity, we'll use GLFW but immediately hide the window.

    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto* window = glfwCreateWindow(1, 1, "BlackHole2 Offline", nullptr, nullptr);

    instance.init(ici);

    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance.handle(), window, nullptr, &surface);

    vk::Device device;
    device.init(instance.handle(), surface);

    vk::Allocator allocator;
    allocator.init(instance.handle(), device.physical(), device.logical());

    vk::DescriptorManager descriptors;
    descriptors.init(device.logical());

    int w = cfg.offline_width;
    int h = cfg.offline_height;

    vk::ManagedImage hdr_image;
    hdr_image.init_storage_2d(allocator, device.logical(), w, h, VK_FORMAT_R32G32B32A32_SFLOAT);
    hdr_image.transition_layout(device.logical(), device.graphics_queue(),
                                 device.families().graphics.value(),
                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    // Skybox
    io::HDRImage sky;
    if (!cfg.skybox_path.empty() && std::filesystem::exists(cfg.skybox_path)) {
        sky = io::load_hdr_any(cfg.skybox_path);
    } else {
        sky.width = 64; sky.height = 32;
        sky.pixels.resize(64 * 32 * 4, 0.001f);
    }
    vk::ManagedImage skybox_image;
    skybox_image.init_sampled_2d(allocator, device.logical(),
                                  sky.width, sky.height, VK_FORMAT_R32G32B32A32_SFLOAT,
                                  sky.pixels.data(), sky.pixels.size() * sizeof(float),
                                  device.graphics_queue(), device.families().graphics.value());

    // VDB
    auto vdb_data = io::create_dummy_vdb();
#ifdef BH2_HAS_NANOVDB
    if (!cfg.vdb_path.empty() && std::filesystem::exists(cfg.vdb_path)) {
        vdb_data = io::load_vdb(cfg.vdb_path);
    }
#endif

    auto upload_grid = [&](const io::NanoVDBGridData& grid) {
        VkDeviceSize size = std::max(grid.buffer.size(), size_t(64));
        auto buf = allocator.create_buffer(size,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        if (!grid.buffer.empty()) {
            void* mapped = allocator.map(buf.allocation);
            memcpy(mapped, grid.buffer.data(), grid.buffer.size());
            allocator.unmap(buf.allocation);
        }
        return buf;
    };
    auto density_buf = upload_grid(vdb_data.density);
    auto temp_buf = upload_grid(vdb_data.temperature);

    auto ubo_buf = allocator.create_buffer(sizeof(RenderConfigGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    auto compute_set = descriptors.allocate_compute_set(device.logical());
    descriptors.update_compute_set(device.logical(), compute_set,
        hdr_image.view(), ubo_buf.buffer, sizeof(RenderConfigGPU),
        skybox_image.view(), skybox_image.sampler(),
        density_buf.buffer, density_buf.size,
        temp_buf.buffer, temp_buf.size);

    std::string shader_dir = BH2_SHADER_DIR;
    vk::ComputePipeline compute_pipeline;
    compute_pipeline.init(device.logical(),
                          shader_dir + "/trace.comp.glsl.spv",
                          descriptors.compute_layout());

    // Command pool
    VkCommandPool cmd_pool;
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = device.families().compute.value();
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    vkCreateCommandPool(device.logical(), &cpci, nullptr, &cmd_pool);

    VkCommandBuffer cmd;
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = cmd_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device.logical(), &cbai, &cmd);

    VkFence fence;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device.logical(), &fci, nullptr, &fence);

    cfg.offline_mode = true;

    for (int s = 0; s < cfg.offline_samples; s++) {
        auto gpu = to_gpu_config(cfg, s);
        void* mapped = allocator.map(ubo_buf.allocation);
        memcpy(mapped, &gpu, sizeof(gpu));
        allocator.unmap(ubo_buf.allocation);

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &bi);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compute_pipeline.handle());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                compute_pipeline.layout(), 0, 1, &compute_set, 0, nullptr);

        uint32_t gx = (w + 15) / 16;
        uint32_t gy = (h + 15) / 16;
        vkCmdDispatch(cmd, gx, gy, 1);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(device.compute_queue(), 1, &si, fence);
        vkWaitForFences(device.logical(), 1, &fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device.logical(), 1, &fence);
        vkResetCommandBuffer(cmd, 0);

        printf("\rSample %d/%d", s + 1, cfg.offline_samples);
        fflush(stdout);
    }
    printf("\n");

    // Read back and save
    hdr_image.transition_layout(device.logical(), device.compute_queue(),
                                 device.families().compute.value(),
                                 VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    size_t pixel_count = w * h * 4;
    std::vector<float> pixels(pixel_count);
    hdr_image.read_pixels(allocator, device.logical(), device.compute_queue(),
                           device.families().compute.value(),
                           pixels.data(), pixel_count * sizeof(float));

    io::write_exr(cfg.output_path, w, h, pixels.data());
    printf("Saved: %s\n", cfg.output_path.c_str());

    // Cleanup
    vkDestroyFence(device.logical(), fence, nullptr);
    vkDestroyCommandPool(device.logical(), cmd_pool, nullptr);
    compute_pipeline.destroy(device.logical());
    hdr_image.destroy(allocator, device.logical());
    skybox_image.destroy(allocator, device.logical());
    allocator.destroy_buffer(ubo_buf);
    allocator.destroy_buffer(density_buf);
    allocator.destroy_buffer(temp_buf);
    descriptors.destroy(device.logical());
    allocator.destroy();
    device.destroy();
    vkDestroySurfaceKHR(instance.handle(), surface, nullptr);
    instance.destroy();
    glfwDestroyWindow(window);
    glfwTerminate();
}

} // namespace bh2

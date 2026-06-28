#include "Renderer.h"

#include "app/AssetLoad.h"
#include "app/Blackbody.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <tinyexr.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace app {

std::string shaderPath(const char* name)
{
    namespace fs = std::filesystem;
    fs::path buildDir = fs::path(BH_SHADER_DIR) / name;
    if (fs::exists(buildDir))
        return buildDir.string();
    fs::path local = fs::path("shaders") / name;
    if (fs::exists(local))
        return local.string();
    throw std::runtime_error(std::string("shader not found: ") + name);
}

struct PostPush
{
    float exposure;
    float invSampleCount;
    int32_t tonemap;
    int32_t pad;
};

Renderer::Renderer(vk::Context& ctx, const Config& cfg)
    : m_ctx(ctx), m_cfg(cfg), m_settings(cfg)
{
    m_camera.distance = cfg.camDist;
    m_camera.azimuthDeg = cfg.camAzimuth;
    m_camera.elevationDeg = cfg.camElevation;
    m_camera.fovYDeg = cfg.fovY;
    createStaticResources();
}

Renderer::~Renderer()
{
    m_ctx.waitIdle();
    if (m_imguiPool)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(m_ctx.device(), m_imguiPool, nullptr);
    }
    if (m_semImageAvailable)
        vkDestroySemaphore(m_ctx.device(), m_semImageAvailable, nullptr);
    for (auto sem : m_semRenderDone)
        vkDestroySemaphore(m_ctx.device(), sem, nullptr);
    if (m_fence)
        vkDestroyFence(m_ctx.device(), m_fence, nullptr);
    if (m_descPool)
        vkDestroyDescriptorPool(m_ctx.device(), m_descPool, nullptr);
    if (m_samplerRepeat)
        vkDestroySampler(m_ctx.device(), m_samplerRepeat, nullptr);
    if (m_samplerClamp)
        vkDestroySampler(m_ctx.device(), m_samplerClamp, nullptr);
}

float Renderer::horizonRadius() const
{
    return static_cast<float>(kerrHorizonRadius(m_settings.spin));
}

// --------------------------------------------------------- static resources

void Renderer::createStaticResources()
{
    // UBO
    m_ubo = vk::Buffer(m_ctx, sizeof(ParamsUBO),
                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);

    // Blackbody LUT (1D)
    {
        auto texels = makeBlackbodyLUT(kLutSize, kLutTmin, kLutTmax);
        m_lutImage = vk::Image(m_ctx, VK_IMAGE_TYPE_1D,
                               {kLutSize, 1, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_lutImage.upload(m_ctx, texels.data(), texels.size() * sizeof(float));
    }

    // Skybox (or 1x1 black dummy)
    {
        ImageData2D sky = loadEquirectHDR(m_cfg.skyPath);
        m_hasSky = sky.valid();
        if (!m_hasSky)
        {
            sky.width = sky.height = 1;
            sky.rgba = {0, 0, 0, 1};
        }
        m_skyImage = vk::Image(m_ctx, VK_IMAGE_TYPE_2D,
                               {sky.width, sky.height, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
                               VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_skyImage.upload(m_ctx, sky.rgba.data(), sky.rgba.size() * sizeof(float));
    }

    // Volume: start with 1x1x1 zero dummies, then collect the file list
    // (single file or a directory holding a numbered sequence) and load the
    // first requested frame.
    {
        float zero = 0.0f;
        VkExtent3D one{1, 1, 1};
        m_volDensity = vk::Image(m_ctx, VK_IMAGE_TYPE_3D, one, VK_FORMAT_R32_SFLOAT,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_volDensity.upload(m_ctx, &zero, sizeof(zero));
        m_volTemp = vk::Image(m_ctx, VK_IMAGE_TYPE_3D, one, VK_FORMAT_R32_SFLOAT,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        m_volTemp.upload(m_ctx, &zero, sizeof(zero));

        namespace fs = std::filesystem;
        if (!m_cfg.vdbPath.empty())
        {
            std::error_code ec;
            if (fs::is_directory(m_cfg.vdbPath, ec))
            {
                for (const auto& e : fs::directory_iterator(m_cfg.vdbPath, ec))
                {
                    if (!e.is_regular_file())
                        continue;
                    std::string ext = e.path().extension().string();
                    for (auto& c : ext) c = char(::tolower(c));
                    if (ext == ".vdb" || ext == ".nvdb")
                        m_seqPaths.push_back(e.path().string());
                }
                std::sort(m_seqPaths.begin(), m_seqPaths.end());
                std::printf("[assets] VDB sequence: %zu frames in %s\n",
                            m_seqPaths.size(), m_cfg.vdbPath.c_str());
            }
            else
            {
                m_seqPaths.push_back(m_cfg.vdbPath);
            }
        }
    }

    // Samplers
    {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; // equirect wraps in phi
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_samplerRepeat));
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VK_CHECK(vkCreateSampler(m_ctx.device(), &si, nullptr, &m_samplerClamp));
    }

    // Tracer pipeline
    using vk::BindingDesc;
    std::vector<BindingDesc> traceBindings = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
        {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
        {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT},
    };
    m_tracePipeline = std::make_unique<vk::ComputePipeline>(
        m_ctx, shaderPath("trace.comp.spv"), traceBindings, sizeof(uint32_t));

    // Descriptor pool + compute set (post set allocated in initPreview)
    {
        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
        };
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.maxSets = 4;
        pi.poolSizeCount = 3;
        pi.pPoolSizes = sizes;
        VK_CHECK(vkCreateDescriptorPool(m_ctx.device(), &pi, nullptr, &m_descPool));

        VkDescriptorSetLayout layout = m_tracePipeline->setLayout();
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VK_CHECK(vkAllocateDescriptorSets(m_ctx.device(), &ai, &m_computeSet));
    }

    createAccumImage(m_cfg.width, m_cfg.height);

    if (!m_seqPaths.empty())
        loadVolumeFrame(m_cfg.seqStart);
}

// Loads sequence frame (1-based), bakes it to 3D textures and swaps them in.
// Keeps the previous frame on failure.
bool Renderer::loadVolumeFrame(int frame1Based)
{
    if (m_seqPaths.empty())
        return false;
    int frame = std::clamp(frame1Based, 1, sequenceCount());
    if (frame == m_seqCurrent)
        return true;

    VolumeLoadOptions opt;
    opt.maxDim = static_cast<uint32_t>(std::clamp(m_cfg.vdbRes, 16, 512));
    opt.scale = m_cfg.vdbScale;
    opt.yUp = m_cfg.vdbYup;
    VolumeData vol = loadVolume(m_seqPaths[size_t(frame) - 1], opt);
    if (!vol.valid())
        return false;

    m_volHasTemp = !vol.temperature.empty();
    if (!m_volHasTemp)
        vol.temperature.assign(vol.density.size(), 0.0f);

    // First load: derive sensible scales from the grid value ranges.
    if (m_seqCurrent == 0)
    {
        if (m_settings.volTempScale <= 0.0f)
        {
            // Houdini pyro temperature grids are typically normalized ~0..1;
            // map the peak to ~6500 K. Kelvin-valued grids pass through.
            m_settings.volTempScale =
                (m_volHasTemp && vol.maxTemperature > 0 && vol.maxTemperature < 10.0f)
                    ? 6500.0f / vol.maxTemperature : 1.0f;
            if (m_settings.volTempScale != 1.0f)
                std::printf("[assets] normalized temperature grid detected; "
                            "auto temp scale %.0f K/unit (--vdbtemp overrides)\n",
                            m_settings.volTempScale);
        }
        if (m_settings.volDensityScale == 1.0f && vol.maxDensity > 4.0f)
        {
            // Keep peak extinction ~2/M so the disk is thick but not opaque.
            m_settings.volDensityScale = 2.0f / vol.maxDensity;
            std::printf("[assets] dense grid; auto density scale %.4f "
                        "(--voldens overrides)\n", m_settings.volDensityScale);
        }
    }
    std::copy(vol.boxMin, vol.boxMin + 3, m_volBoxMin);
    std::copy(vol.boxMax, vol.boxMax + 3, m_volBoxMax);

    m_ctx.waitIdle(); // textures may be referenced by in-flight work
    VkExtent3D ext{vol.dim[0], vol.dim[1], vol.dim[2]};
    m_volDensity = vk::Image(m_ctx, VK_IMAGE_TYPE_3D, ext, VK_FORMAT_R32_SFLOAT,
                             VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    m_volDensity.upload(m_ctx, vol.density.data(), vol.density.size() * sizeof(float));
    m_volTemp = vk::Image(m_ctx, VK_IMAGE_TYPE_3D, ext, VK_FORMAT_R32_SFLOAT,
                          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    m_volTemp.upload(m_ctx, vol.temperature.data(), vol.temperature.size() * sizeof(float));

    m_hasVolume = true;
    m_seqCurrent = frame;
    writeComputeDescriptors();
    m_frameIndex = 0; // new volume: restart accumulation
    m_hasLastParams = false;
    return true;
}

void Renderer::applyPendingSequenceFrame()
{
    if (m_seqPending > 0)
    {
        loadVolumeFrame(m_seqPending);
        m_seqPending = -1;
    }
}

void Renderer::createAccumImage(uint32_t width, uint32_t height)
{
    m_ctx.waitIdle();
    m_accumImage = vk::Image(m_ctx, VK_IMAGE_TYPE_2D, {width, height, 1},
                             VK_FORMAT_R32G32B32A32_SFLOAT,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    // Storage images are written and resolved in GENERAL layout.
    m_accumImage.transition(m_ctx, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    m_frameIndex = 0;
    m_hasLastParams = false;
    writeComputeDescriptors();
}

void Renderer::writeComputeDescriptors()
{
    VkDescriptorImageInfo accumInfo{VK_NULL_HANDLE, m_accumImage.view(),
                                    VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorBufferInfo uboInfo{m_ubo.handle(), 0, sizeof(ParamsUBO)};
    VkDescriptorImageInfo skyInfo{m_samplerRepeat, m_skyImage.view(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo lutInfo{m_samplerClamp, m_lutImage.view(),
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo densInfo{m_samplerClamp, m_volDensity.view(),
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo tempInfo{m_samplerClamp, m_volTemp.view(),
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    std::vector<VkWriteDescriptorSet> writes;
    auto add = [&](uint32_t binding, VkDescriptorType type,
                   const VkDescriptorImageInfo* img, const VkDescriptorBufferInfo* buf) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = m_computeSet;
        w.dstBinding = binding;
        w.descriptorCount = 1;
        w.descriptorType = type;
        w.pImageInfo = img;
        w.pBufferInfo = buf;
        writes.push_back(w);
    };
    add(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &accumInfo, nullptr);
    add(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &uboInfo);
    add(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &skyInfo, nullptr);
    add(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &lutInfo, nullptr);
    add(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &densInfo, nullptr);
    add(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &tempInfo, nullptr);
    vkUpdateDescriptorSets(m_ctx.device(), static_cast<uint32_t>(writes.size()),
                           writes.data(), 0, nullptr);

    if (m_postSet)
    {
        VkDescriptorImageInfo resolveInfo{m_samplerClamp, m_accumImage.view(),
                                          VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = m_postSet;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &resolveInfo;
        vkUpdateDescriptorSets(m_ctx.device(), 1, &w, 0, nullptr);
    }
}

// ------------------------------------------------------------------- params

ParamsUBO Renderer::buildParams(uint32_t width, uint32_t height) const
{
    const Settings& s = m_settings;
    Camera::Frame f = m_camera.computeFrame();

    float a = s.spin;
    float rh = static_cast<float>(kerrHorizonRadius(a));
    float risco = static_cast<float>(kerrIscoRadius(a));
    float aspect = height > 0 ? float(width) / float(height) : 1.0f;

    ParamsUBO p{};
    auto set4 = [](float* dst, float x, float y, float z, float w) {
        dst[0] = x; dst[1] = y; dst[2] = z; dst[3] = w;
    };
    set4(p.camPos, f.posBL.x, f.posBL.y, f.posBL.z, f.tanHalfFov);
    set4(p.camRight, f.right.x, f.right.y, f.right.z, aspect);
    set4(p.camUp, f.up.x, f.up.y, f.up.z, 0);
    set4(p.camFwd, f.fwd.x, f.fwd.y, f.fwd.z, 0);
    set4(p.bh, a, rh, risco, m_cfg.rFar);
    set4(p.disk, risco, s.diskOuter, s.diskTmax, s.diskExposure);
    set4(p.disk2, s.diskOpacity, s.diskNoise, m_animTime, kLutTmin);
    set4(p.integ, s.hInit, s.tol, float(s.maxSteps), float(s.integrator));
    set4(p.frame, 0, float(width), float(height), float(s.debugView));
    set4(p.env, m_hasSky ? 1.0f : 0.0f, s.skyIntensity, 0, kLutTmax);
    set4(p.vol, m_hasVolume ? 1.0f : 0.0f, s.volDensityScale, s.volEmissionScale, 8.0f);
    // volBoxMin.w = 1: no temperature grid in the file, the shader derives the
    // gas temperature from the thin-disk profile at the cylindrical radius
    set4(p.volBoxMin, m_volBoxMin[0], m_volBoxMin[1], m_volBoxMin[2],
         (m_hasVolume && !m_volHasTemp) ? 1.0f : 0.0f);
    // volBoxMax.w: kelvin per temperature-grid unit
    set4(p.volBoxMax, m_volBoxMax[0], m_volBoxMax[1], m_volBoxMax[2],
         s.volTempScale > 0.0f ? s.volTempScale : 1.0f);
    return p;
}

void Renderer::updateUBOAndMaybeReset(uint32_t width, uint32_t height)
{
    ParamsUBO p = buildParams(width, height);
    if (!m_hasLastParams || std::memcmp(&p, &m_lastParams, sizeof(p)) != 0)
    {
        m_frameIndex = 0; // scene changed: restart progressive accumulation
        m_lastParams = p;
        m_hasLastParams = true;
    }
    std::memcpy(m_ubo.mapped(), &p, sizeof(p));
}

void Renderer::recordCompute(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                             uint32_t frameIndex)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_tracePipeline->pipeline());
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_tracePipeline->layout(), 0, 1, &m_computeSet, 0, nullptr);
    vkCmdPushConstants(cmd, m_tracePipeline->layout(), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(uint32_t), &frameIndex);
    vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
}

// ------------------------------------------------------------------ preview

void Renderer::initPreview(GLFWwindow* window)
{
    m_window = window;
    int w, h;
    glfwGetFramebufferSize(window, &w, &h);
    m_swapchain = std::make_unique<vk::Swapchain>(m_ctx, uint32_t(w), uint32_t(h));
    createAccumImage(m_swapchain->extent().width, m_swapchain->extent().height);

    // post pipeline + descriptor set
    m_postPipeline = std::make_unique<vk::GraphicsPipeline>(
        m_ctx, shaderPath("post.vert.spv"), shaderPath("post.frag.spv"),
        m_swapchain->renderPass(),
        std::vector<vk::BindingDesc>{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT}},
        sizeof(PostPush));
    {
        VkDescriptorSetLayout layout = m_postPipeline->setLayout();
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &layout;
        VK_CHECK(vkAllocateDescriptorSets(m_ctx.device(), &ai, &m_postSet));
    }
    writeComputeDescriptors(); // also fills m_postSet now that it exists

    // command buffer + sync
    {
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = m_ctx.commandPool();
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(m_ctx.device(), &ai, &m_cmd));

        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(m_ctx.device(), &si, nullptr, &m_semImageAvailable));
        createRenderDoneSemaphores();
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(m_ctx.device(), &fi, nullptr, &m_fence));
    }

    // ImGui
    {
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16}};
        VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pi.maxSets = 16;
        pi.poolSizeCount = 1;
        pi.pPoolSizes = sizes;
        VK_CHECK(vkCreateDescriptorPool(m_ctx.device(), &pi, nullptr, &m_imguiPool));

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui_ImplGlfw_InitForVulkan(window, true);

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = m_ctx.instance();
        info.PhysicalDevice = m_ctx.physicalDevice();
        info.Device = m_ctx.device();
        info.QueueFamily = m_ctx.queueFamily();
        info.Queue = m_ctx.queue();
        info.DescriptorPool = m_imguiPool;
        info.RenderPass = m_swapchain->renderPass();
        info.MinImageCount = 2;
        info.ImageCount = m_swapchain->imageCount();
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&info);
    }
}

void Renderer::createRenderDoneSemaphores()
{
    for (auto sem : m_semRenderDone)
        vkDestroySemaphore(m_ctx.device(), sem, nullptr);
    m_semRenderDone.assign(m_swapchain->imageCount(), VK_NULL_HANDLE);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& sem : m_semRenderDone)
        VK_CHECK(vkCreateSemaphore(m_ctx.device(), &si, nullptr, &sem));
}

void Renderer::handleResize()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(m_window, &w, &h);
    while ((w == 0 || h == 0) && !glfwWindowShouldClose(m_window))
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &w, &h);
    }
    m_ctx.waitIdle();
    m_swapchain->recreate(uint32_t(w), uint32_t(h));
    createRenderDoneSemaphores();
    createAccumImage(m_swapchain->extent().width, m_swapchain->extent().height);
    m_resizePending = false;
}

void Renderer::drawFrame(const std::function<void()>& buildUI)
{
    applyPendingSequenceFrame(); // blocking load; resets accumulation

    if (m_resizePending)
        handleResize();

    VK_CHECK(vkWaitForFences(m_ctx.device(), 1, &m_fence, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(m_ctx.device(), m_swapchain->handle(),
                                             UINT64_MAX, m_semImageAvailable,
                                             VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_resizePending = true;
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    // UI first (it edits settings), then UBO upload with dirty detection
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    buildUI();
    ImGui::Render();

    if (m_settings.animate)
        m_animTime += ImGui::GetIO().DeltaTime * 10.0f; // affine time, in M

    VkExtent2D extent = m_swapchain->extent();
    updateUBOAndMaybeReset(extent.width, extent.height);

    VK_CHECK(vkResetFences(m_ctx.device(), 1, &m_fence));
    VK_CHECK(vkResetCommandBuffer(m_cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(m_cmd, &bi));

    recordCompute(m_cmd, extent.width, extent.height, m_frameIndex);

    // compute write -> fragment sample
    vk::cmdImageBarrier(m_cmd, m_accumImage.handle(),
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VkClearValue clear{};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = m_swapchain->renderPass();
    rp.framebuffer = m_swapchain->framebuffer(imageIndex);
    rp.renderArea = {{0, 0}, extent};
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(m_cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(m_cmd, 0, 1, &viewport);
    vkCmdSetScissor(m_cmd, 0, 1, &scissor);

    vkCmdBindPipeline(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_postPipeline->pipeline());
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_postPipeline->layout(), 0, 1, &m_postSet, 0, nullptr);
    PostPush push{m_settings.exposure, 1.0f / float(m_frameIndex + 1),
                  m_settings.tonemap ? 1 : 0, 0};
    vkCmdPushConstants(m_cmd, m_postPipeline->layout(), VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(m_cmd, 3, 1, 0, 0);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), m_cmd);

    vkCmdEndRenderPass(m_cmd);
    VK_CHECK(vkEndCommandBuffer(m_cmd));

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &m_semImageAvailable;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &m_cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &m_semRenderDone[imageIndex];
    VK_CHECK(vkQueueSubmit(m_ctx.queue(), 1, &si, m_fence));

    VkSwapchainKHR sc = m_swapchain->handle();
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_semRenderDone[imageIndex];
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imageIndex;
    VkResult present = vkQueuePresentKHR(m_ctx.queue(), &pi);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR)
        m_resizePending = true;
    else if (present != VK_SUCCESS)
        throw std::runtime_error("vkQueuePresentKHR failed");

    ++m_frameIndex;
}

// ------------------------------------------------------------------ offline

std::vector<float> Renderer::readbackRGB(uint32_t width, uint32_t height, float scale)
{
    VkDeviceSize bytes = VkDeviceSize(width) * height * 4 * sizeof(float);
    vk::Buffer staging(m_ctx, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);

    m_ctx.oneShot([&](VkCommandBuffer cmd) {
        vk::cmdImageBarrier(cmd, m_accumImage.handle(),
                            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_TRANSFER_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyImageToBuffer(cmd, m_accumImage.handle(), VK_IMAGE_LAYOUT_GENERAL,
                               staging.handle(), 1, &region);
    });

    const float* src = static_cast<const float*>(staging.mapped());
    std::vector<float> rgb(size_t(width) * height * 3);
    for (size_t i = 0; i < size_t(width) * height; ++i)
    {
        rgb[i * 3 + 0] = src[i * 4 + 0] * scale;
        rgb[i * 3 + 1] = src[i * 4 + 1] * scale;
        rgb[i * 3 + 2] = src[i * 4 + 2] * scale;
    }
    return rgb;
}

// Renders cfg.spp samples into the accumulator and writes one EXR.
// Assumes accumulation has been reset (m_frameIndex == 0 via parameter or
// volume change) — sample indices always start at 0 here.
void Renderer::renderOneEXR(const std::string& outPath)
{
    const uint32_t w = m_cfg.width, h = m_cfg.height;
    const int spp = std::max(1, m_cfg.spp);

    updateUBOAndMaybeReset(w, h);

    auto t0 = std::chrono::steady_clock::now();
    constexpr int kBatch = 16;
    for (int s = 0; s < spp; s += kBatch)
    {
        int count = std::min(kBatch, spp - s);
        m_ctx.oneShot([&](VkCommandBuffer cmd) {
            for (int i = 0; i < count; ++i)
            {
                recordCompute(cmd, w, h, uint32_t(s + i));
                // sample N+1 reads what sample N wrote
                vk::cmdImageBarrier(cmd, m_accumImage.handle(),
                                    VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                                    VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
            }
        });
        std::printf("\r[offline] %d / %d spp", s + count, spp);
        std::fflush(stdout);
    }
    auto t1 = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(t1 - t0).count();
    std::printf("\n[offline] rendered %dx%d @ %d spp in %.1f s\n", w, h, spp, secs);

    std::vector<float> rgb = readbackRGB(w, h, 1.0f / float(spp));

    const char* err = nullptr;
    int ret = SaveEXR(rgb.data(), int(w), int(h), 3, /*fp16*/ 0,
                      outPath.c_str(), &err);
    if (ret != TINYEXR_SUCCESS)
    {
        std::string msg = err ? err : "unknown";
        FreeEXRErrorMessage(err);
        throw std::runtime_error("failed to write EXR: " + msg);
    }
    std::printf("[offline] wrote %s (32-bit float RGB)\n", outPath.c_str());
}

// "render.exr" + frame 7 -> "render.0007.exr"
static std::string framePath(const std::string& base, int frame)
{
    char num[16];
    std::snprintf(num, sizeof(num), ".%04d", frame);
    size_t dot = base.find_last_of('.');
    if (dot == std::string::npos)
        return base + num;
    return base.substr(0, dot) + num + base.substr(dot);
}

void Renderer::renderOffline()
{
    int s0 = m_seqPaths.empty() ? 0 : std::clamp(m_cfg.seqStart, 1, sequenceCount());
    int s1 = m_cfg.seqEnd > 0 && !m_seqPaths.empty()
           ? std::clamp(m_cfg.seqEnd, s0, sequenceCount()) : s0;

    if (s1 > s0)
    {
        // animation: one EXR per sequence frame
        for (int f = s0; f <= s1; f += m_cfg.seqStep)
        {
            std::printf("[offline] sequence frame %d / %d\n", f, s1);
            if (!loadVolumeFrame(f))
                std::fprintf(stderr, "[offline] frame %d failed to load; "
                                     "re-using previous volume\n", f);
            m_frameIndex = 0;
            m_hasLastParams = false;
            renderOneEXR(framePath(m_cfg.outPath, f));
        }
    }
    else
    {
        renderOneEXR(m_cfg.outPath);
    }
}

} // namespace app

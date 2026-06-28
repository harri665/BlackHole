// Renderer — owns all rendering resources and drives both modes:
//   preview: GLFW window, 1 spp/frame compute pass + progressive accumulation,
//            post-process (ACES) fragment pass and ImGui overlay.
//   offline: headless progressive accumulation of N spp, EXR export.
#pragma once

#include "app/Camera.h"
#include "app/Config.h"
#include "vk/Context.h"
#include "vk/Pipeline.h"
#include "vk/Resources.h"
#include "vk/Swapchain.h"

#include <functional>
#include <memory>

struct GLFWwindow;

namespace app {

// std140 mirror of the Params UBO in shaders/common.glsl (13 vec4s).
struct ParamsUBO
{
    float camPos[4];
    float camRight[4];
    float camUp[4];
    float camFwd[4];
    float bh[4];
    float disk[4];
    float disk2[4];
    float integ[4];
    float frame[4];
    float env[4];
    float vol[4];
    float volBoxMin[4];
    float volBoxMax[4];
};
static_assert(sizeof(ParamsUBO) == 13 * 16);

// Live-tweakable render settings (edited by the ImGui panel).
struct Settings
{
    float spin;
    float diskOuter, diskTmax, diskExposure, diskOpacity, diskNoise;
    float fovY;
    int integrator, maxSteps;
    float hInit, tol;
    float exposure, skyIntensity;
    float volDensityScale, volEmissionScale, volTempScale;
    int debugView;
    bool tonemap = true;
    bool animate = false;

    explicit Settings(const Config& c)
        : spin(c.spin), diskOuter(c.diskOuter), diskTmax(c.diskTmax),
          diskExposure(c.diskExposure), diskOpacity(c.diskOpacity),
          diskNoise(c.diskNoise), fovY(c.fovY), integrator(c.integrator),
          maxSteps(c.maxSteps), hInit(c.hInit), tol(c.tol),
          exposure(c.exposure), skyIntensity(c.skyIntensity),
          volDensityScale(c.volDensityScale), volEmissionScale(c.volEmissionScale),
          volTempScale(c.volTempScale), debugView(c.debugView)
    {}
};

class Renderer
{
public:
    Renderer(vk::Context& ctx, const Config& cfg);
    ~Renderer();

    Settings& settings() { return m_settings; }
    Camera& camera() { return m_camera; }
    uint32_t accumulatedSamples() const { return m_frameIndex; }
    float horizonRadius() const;

    // -------------------------------------------------------- VDB sequence
    int sequenceCount() const { return static_cast<int>(m_seqPaths.size()); }
    int sequenceFrame() const { return m_seqPending > 0 ? m_seqPending : m_seqCurrent; }
    // Defers the (multi-second) load to the start of the next drawFrame.
    void requestSequenceFrame(int frame1Based) { m_seqPending = frame1Based; }

    // ------------------------------------------------------------- preview
    void initPreview(GLFWwindow* window);
    // Returns false if the swapchain had to be rebuilt and the frame skipped.
    void drawFrame(const std::function<void()>& buildUI);
    void notifyResize() { m_resizePending = true; }

    // ------------------------------------------------------------- offline
    void renderOffline();

private:
    void createStaticResources();
    void createAccumImage(uint32_t width, uint32_t height);
    void writeComputeDescriptors();
    ParamsUBO buildParams(uint32_t width, uint32_t height) const;
    void updateUBOAndMaybeReset(uint32_t width, uint32_t height);
    void recordCompute(VkCommandBuffer cmd, uint32_t width, uint32_t height,
                       uint32_t frameIndex);
    void createRenderDoneSemaphores();
    void handleResize();
    std::vector<float> readbackRGB(uint32_t width, uint32_t height, float scale);
    bool loadVolumeFrame(int frame1Based);
    void applyPendingSequenceFrame();
    void renderOneEXR(const std::string& outPath);

    vk::Context& m_ctx;
    Config m_cfg;
    Settings m_settings;
    Camera m_camera;

    // static resources
    vk::Image m_accumImage;
    vk::Buffer m_ubo;
    vk::Image m_lutImage;
    vk::Image m_skyImage;
    vk::Image m_volDensity;
    vk::Image m_volTemp;
    bool m_hasSky = false;
    bool m_hasVolume = false;
    bool m_volHasTemp = false;
    float m_volBoxMin[3]{}, m_volBoxMax[3]{};
    std::vector<std::string> m_seqPaths; // single file => one entry
    int m_seqCurrent = 0;                // 1-based, 0 = nothing loaded
    int m_seqPending = -1;
    VkSampler m_samplerRepeat = VK_NULL_HANDLE; // sky (wraps in azimuth)
    VkSampler m_samplerClamp = VK_NULL_HANDLE;  // LUT, volume, accum resolve

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_computeSet = VK_NULL_HANDLE;
    VkDescriptorSet m_postSet = VK_NULL_HANDLE;
    std::unique_ptr<vk::ComputePipeline> m_tracePipeline;
    std::unique_ptr<vk::GraphicsPipeline> m_postPipeline;

    // preview state
    GLFWwindow* m_window = nullptr;
    std::unique_ptr<vk::Swapchain> m_swapchain;
    VkDescriptorPool m_imguiPool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
    VkSemaphore m_semImageAvailable = VK_NULL_HANDLE;
    // one per swapchain image: presentation may still hold the semaphore of a
    // previously presented image, so they cannot be shared across images
    std::vector<VkSemaphore> m_semRenderDone;
    VkFence m_fence = VK_NULL_HANDLE;
    bool m_resizePending = false;

    // accumulation
    uint32_t m_frameIndex = 0;
    ParamsUBO m_lastParams{};
    bool m_hasLastParams = false;
    float m_animTime = 0.0f;

    static constexpr int kLutSize = 256;
    static constexpr float kLutTmin = 100.0f;
    static constexpr float kLutTmax = 100000.0f;
};

std::string shaderPath(const char* name);

} // namespace app

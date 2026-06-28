// blackhole4 — physically-based Kerr black hole renderer.
//   preview: interactive GLFW window (drag to orbit, scroll to zoom)
//   offline: --offline renders N spp headless and writes a 32-bit EXR
#include "app/Config.h"
#include "app/Renderer.h"
#include "vk/Context.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <imgui.h>

#include <cstdio>
#include <stdexcept>

namespace {

struct InputState
{
    app::Renderer* renderer = nullptr;
    double lastX = 0, lastY = 0;
    bool dragging = false;
};

void scrollCallback(GLFWwindow* window, double, double yoffset)
{
    auto* in = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
        return;
    float rMin = in->renderer->horizonRadius() * 1.5f;
    in->renderer->camera().zoom(static_cast<float>(yoffset), rMin);
}

void framebufferSizeCallback(GLFWwindow* window, int, int)
{
    auto* in = static_cast<InputState*>(glfwGetWindowUserPointer(window));
    in->renderer->notifyResize();
}

void pollMouseOrbit(GLFWwindow* window, InputState& in)
{
    if (ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse)
    {
        in.dragging = false;
        return;
    }
    double x, y;
    glfwGetCursorPos(window, &x, &y);
    bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (down && in.dragging)
        in.renderer->camera().orbit(float(x - in.lastX), float(y - in.lastY));
    in.dragging = down;
    in.lastX = x;
    in.lastY = y;
}

void buildSettingsUI(app::Renderer& renderer)
{
    app::Settings& s = renderer.settings();
    app::Camera& cam = renderer.camera();

    ImGui::SetNextWindowPos({10, 10}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Kerr Renderer");

    ImGui::Text("%.1f fps | %u spp accumulated",
                ImGui::GetIO().Framerate, renderer.accumulatedSamples());

    if (ImGui::CollapsingHeader("Black hole", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("spin a", &s.spin, 0.0f, 0.998f, "%.3f");
        ImGui::Text("r+ = %.3f M   r_isco = %.3f M",
                    app::kerrHorizonRadius(s.spin), app::kerrIscoRadius(s.spin));
    }

    if (ImGui::CollapsingHeader("Accretion disk", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::SliderFloat("outer radius", &s.diskOuter, 5.0f, 60.0f, "%.1f M");
        ImGui::SliderFloat("peak T", &s.diskTmax, 1000.0f, 40000.0f, "%.0f K",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("emission", &s.diskExposure, 0.01f, 100.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("opacity", &s.diskOpacity, 0.0f, 1.0f);
        ImGui::SliderFloat("turbulence", &s.diskNoise, 0.0f, 1.0f);
        ImGui::Checkbox("animate (differential rotation)", &s.animate);
    }

    if (ImGui::CollapsingHeader("Integrator"))
    {
        ImGui::Combo("method", &s.integrator, "RK4 (fixed)\0RKF45 Cash-Karp\0");
        ImGui::SliderInt("max steps", &s.maxSteps, 64, 4000);
        ImGui::SliderFloat("step h0", &s.hInit, 0.005f, 0.5f, "%.3f",
                           ImGuiSliderFlags_Logarithmic);
        if (s.integrator == 1)
            ImGui::SliderFloat("tolerance", &s.tol, 1e-8f, 1e-3f, "%.1e",
                               ImGuiSliderFlags_Logarithmic);
        ImGui::Combo("debug view", &s.debugView,
                     "off\0Carter constant drift\0step count\0");
    }

    if (ImGui::CollapsingHeader("Camera / display"))
    {
        ImGui::SliderFloat("distance", &cam.distance, 3.0f, 200.0f, "%.1f M",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("elevation", &cam.elevationDeg, -89.0f, 89.0f, "%.1f deg");
        ImGui::SliderFloat("fov", &cam.fovYDeg, 20.0f, 110.0f, "%.0f deg");
        ImGui::SliderFloat("exposure", &s.exposure, 0.05f, 20.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("sky intensity", &s.skyIntensity, 0.0f, 10.0f);
        ImGui::Checkbox("ACES tonemap", &s.tonemap);
    }

    if (ImGui::CollapsingHeader("Volume"))
    {
        ImGui::SliderFloat("density scale", &s.volDensityScale, 0.01f, 100.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("emission scale", &s.volEmissionScale, 0.01f, 100.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        if (s.volTempScale > 0.0f)
            ImGui::SliderFloat("temp scale", &s.volTempScale, 1.0f, 200000.0f,
                               "%.0f K/unit", ImGuiSliderFlags_Logarithmic);

        if (renderer.sequenceCount() > 1)
        {
            // The load takes seconds, so the slider only applies its value
            // when released; uiFrame holds the in-flight drag value.
            static int uiFrame = 0;
            if (!ImGui::IsAnyItemActive())
                uiFrame = renderer.sequenceFrame();
            ImGui::SliderInt("seq frame", &uiFrame, 1, renderer.sequenceCount());
            if (ImGui::IsItemDeactivatedAfterEdit())
                renderer.requestSequenceFrame(uiFrame);
            ImGui::SameLine();
            if (ImGui::ArrowButton("##seqprev", ImGuiDir_Left))
                renderer.requestSequenceFrame(renderer.sequenceFrame() - 1);
            ImGui::SameLine();
            if (ImGui::ArrowButton("##seqnext", ImGuiDir_Right))
                renderer.requestSequenceFrame(renderer.sequenceFrame() + 1);
        }
        else
        {
            ImGui::TextWrapped("Load a grid or sequence with --vdb "
                               "<file.vdb|file.nvdb|directory>.");
        }
    }

    ImGui::End();
}

int runPreview(const app::Config& cfg)
{
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(int(cfg.width), int(cfg.height),
                                          "blackhole4 — Kerr renderer", nullptr, nullptr);
    if (!window)
        throw std::runtime_error("glfwCreateWindow failed");

    {
        vk::Context ctx(window, cfg.validation);
        app::Renderer renderer(ctx, cfg);

        InputState input;
        input.renderer = &renderer;
        glfwSetWindowUserPointer(window, &input);
        // installed before ImGui's backend so it chains into them
        glfwSetScrollCallback(window, scrollCallback);
        glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

        renderer.initPreview(window);

        while (!glfwWindowShouldClose(window))
        {
            glfwPollEvents();
            pollMouseOrbit(window, input);
            renderer.drawFrame([&] { buildSettingsUI(renderer); });
        }
    } // renderer + context destroyed before the window

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

int runOffline(const app::Config& cfg)
{
    vk::Context ctx(nullptr, cfg.validation);
    app::Renderer renderer(ctx, cfg);
    renderer.renderOffline();
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try
    {
        app::Config cfg = app::parseArgs(argc, argv);
        return cfg.offline ? runOffline(cfg) : runPreview(cfg);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}

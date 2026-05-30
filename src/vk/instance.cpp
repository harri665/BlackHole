#include "vk/instance.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <cstdio>
#include <cstring>

namespace bh2::vk {

VKAPI_ATTR VkBool32 VKAPI_CALL Instance::debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

void Instance::init(const InstanceCreateInfo& info) {
    VkApplicationInfo app_info{};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = info.app_name.c_str();
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "BlackHole2";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    uint32_t glfw_ext_count = 0;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
    std::vector<const char*> extensions(glfw_exts, glfw_exts + glfw_ext_count);

    std::vector<const char*> layers;

    if (info.enable_validation) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app_info;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance");
    }

    if (info.enable_validation) {
        VkDebugUtilsMessengerCreateInfoEXT dbg_ci{};
        dbg_ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbg_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                               | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbg_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbg_ci.pfnUserCallback = debug_callback;

        auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (func) func(instance_, &dbg_ci, nullptr, &debug_messenger_);
    }
}

void Instance::destroy() {
    if (debug_messenger_) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (func) func(instance_, debug_messenger_, nullptr);
        debug_messenger_ = VK_NULL_HANDLE;
    }
    if (instance_) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

} // namespace bh2::vk
